#include "Campus/CampusSocial.h"

#include "Algo/Count.h"
#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusParty.h"
#include "Campus/CampusSave.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Misc/CoreDelegates.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_WEBSOCKETS
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#endif

#define LOCTEXT_NAMESPACE "CampusSocial"

namespace
{
	/** campus.social.v1 - the subprotocol IS the version: a server that does not speak it fails the
	 *  handshake, and this client falls back to GET /v1/social/events rather than guessing. */
	const TCHAR* Subprotocol = TEXT("campus.social.v1");

	/** The server's own cadence (the `hello` frame says 30); one rhythm with the room heartbeat. */
	constexpr float HeartbeatSeconds = 30.f;
	/** Three missed beats before a friend reads offline - survives a Wi-Fi to 4G handover. */
	constexpr float BackoffCeiling = 60.f;
	constexpr int32 BackoffMaxAttempt = 6;
	/** Acks are coalesced: at most one frame per five seconds however many events land. */
	constexpr float AckEvery = 5.f;
	/** An unanswered `sync` falls back to HTTP after this long. */
	constexpr float SyncFallback = 2.f;
	/** The id ring that catches the same event over two transports. */
	constexpr int32 DedupeRing = 32;
	/** More than this far behind and a replay is pointless: re-read party, invites and friends instead. */
	constexpr int64 CatchUpMax = 100;
	/** A friend request has no deadline, so its card simply holds for this long. */
	constexpr float FriendCardSeconds = 6.f;

	FString Str(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Value;
		if (Json.IsValid())
		{
			Json->TryGetStringField(Field, Value);
		}
		return Value;
	}

	double Num(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		double Value = 0.0;
		if (Json.IsValid())
		{
			Json->TryGetNumberField(Field, Value);
		}
		return Value;
	}

	bool Bool(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		bool Value = false;
		if (Json.IsValid())
		{
			Json->TryGetBoolField(Field, Value);
		}
		return Value;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Json.IsValid() && Json->TryGetObjectField(Field, Out) ? *Out : nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
		return Json.IsValid() && Json->TryGetArrayField(Field, Out) ? Out : nullptr;
	}

	FDateTime Stamp(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FDateTime Out;
		const FString Text = Str(Json, Field);
		return !Text.IsEmpty() && FDateTime::ParseIso8601(*Text, Out) ? Out : FDateTime();
	}

	/** "#f08a24" from the API. There are no avatar textures in this project, so the colour IS the
	 *  avatar - which is why every card shape carries one. An unparseable value falls back to a
	 *  neutral plate grey rather than drawing nothing. */
	FLinearColor Colour(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		const FLinearColor Fallback(0.35f, 0.38f, 0.44f, 1.f);
		FString Hex = Str(Json, Field);
		Hex.RemoveFromStart(TEXT("#"));
		if (Hex.Len() != 6)
		{
			return Fallback;
		}
		uint32 Packed = 0;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const TCHAR Digit = Hex[Index];
			const int32 Value = (Digit >= TEXT('0') && Digit <= TEXT('9')) ? Digit - TEXT('0')
				: (Digit >= TEXT('a') && Digit <= TEXT('f')) ? Digit - TEXT('a') + 10
				: (Digit >= TEXT('A') && Digit <= TEXT('F')) ? Digit - TEXT('A') + 10
				: -1;
			if (Value < 0)
			{
				return Fallback;
			}
			Packed = (Packed << 4) | uint32(Value);
		}
		return FLinearColor::FromSRGBColor(FColor(uint8(Packed >> 16), uint8(Packed >> 8), uint8(Packed), 255));
	}

	FString Encode(const TSharedPtr<FJsonObject>& Object)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Text;
	}

	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
		return Out;
	}

	const TCHAR* PresenceName(ECampusPresence State)
	{
		switch (State)
		{
		case ECampusPresence::Lobby: return TEXT("lobby");
		case ECampusPresence::Match: return TEXT("match");
		default: return TEXT("menu");
		}
	}

	/** One card out of any object that carries the playerCard fields. */
	FCampusPlayerCard ReadCard(const TSharedPtr<FJsonObject>& Json)
	{
		FCampusPlayerCard Card;
		if (!Json.IsValid())
		{
			return Card;
		}
		Card.PlayerId = Str(Json, TEXT("player_id"));
		if (Card.PlayerId.IsEmpty())
		{
			Card.PlayerId = Str(Json, TEXT("id"));
		}
		Card.Name = Str(Json, TEXT("display_name"));
		Card.Color = Colour(Json, TEXT("color"));
		Card.Presence = Str(Json, TEXT("presence"));
		if (Card.Presence.IsEmpty())
		{
			Card.Presence = TEXT("offline");
		}
		Card.bInParty = Bool(Json, TEXT("in_party"));
		Card.RoomCode = Str(Json, TEXT("in_room_code"));
		Card.Level = FMath::Max(1, int32(Num(Json, TEXT("level"))));
		Card.FriendState = Str(Json, TEXT("friend_state"));
		Card.MatchesTogether = int32(Num(Json, TEXT("matches_together")));
		// Both of these are additive server fields: a build talking to an older API simply gets empty
		// values and the UI falls back (uuid short form, and a flat OFFLINE instead of "4 HOURS AGO").
		Card.PublicId = Str(Json, TEXT("public_id"));
		Card.LastSeenAt = Stamp(Json, TEXT("last_seen_at"));
		Card.Reason = Str(Json, TEXT("reason"));
		return Card;
	}

	void ReadCards(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, const FString& ForcedState, TArray<FCampusPlayerCard>& Out)
	{
		Out.Reset();
		if (const TArray<TSharedPtr<FJsonValue>>* Values = Arr(Json, Field))
		{
			Out.Reserve(Values->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FCampusPlayerCard Card = ReadCard(Value->AsObject());
				if (Card.PlayerId.IsEmpty())
				{
					continue;
				}
				if (!ForcedState.IsEmpty())
				{
					Card.FriendState = ForcedState;
				}
				Out.Add(MoveTemp(Card));
			}
		}
	}

	/** A party member or a pending invite, as a lobby name plate. */
	FCampusPartyMember ReadMember(const TSharedPtr<FJsonObject>& Json, bool bAccepted, const FString& LeaderId)
	{
		FCampusPartyMember Member;
		Member.PlayerId = Str(Json, TEXT("player_id"));
		Member.Name = Str(Json, TEXT("display_name"));
		Member.Level = FMath::Max(1, int32(Num(Json, TEXT("level"))));
		Member.Presence = Str(Json, TEXT("presence"));
		Member.bAccepted = bAccepted;
		Member.bLeader = bAccepted && !LeaderId.IsEmpty() && Member.PlayerId == LeaderId;
		Member.InviteId = Str(Json, TEXT("invite_id"));
		// The chevron pip is cosmetic and there is no server rank, so the level earns it: the plate
		// shows a pip from level 5 rather than inventing a rank the profile page would contradict.
		Member.Rank = FMath::Clamp((Member.Level - 1) / 4, 0, 5);
		return Member;
	}
}

// ---------------------------------------------------------------------------------------------
// Small shapes

FText FCampusPlayerCard::StatusLabel() const
{
	if (Presence == TEXT("match"))
	{
		return LOCTEXT("StatusMatch", "IN MATCH");
	}
	if (Presence == TEXT("lobby"))
	{
		return bInParty ? LOCTEXT("StatusSquad", "IN A SQUAD") : LOCTEXT("StatusLobby", "IN LOBBY");
	}
	if (Presence == TEXT("menu") || Presence == TEXT("online"))
	{
		// GET /v1/players/search answers a COARSE presence ("online" / "offline") because a stranger
		// is not entitled to know which screen you are on. Both land here.
		return bInParty ? LOCTEXT("StatusSquad", "IN A SQUAD") : LOCTEXT("StatusOnline", "ONLINE");
	}
	if (Presence == TEXT("unknown"))
	{
		return LOCTEXT("StatusUnknown", "—");
	}
	return LOCTEXT("StatusOffline", "OFFLINE");
}

FText FCampusPlayerCard::LastSeenLabel(const FDateTime& Now) const
{
	if (IsOnline())
	{
		// A friend who is here right now gets the accent word, not a timestamp: that is the whole
		// question the list answers.
		return Presence == TEXT("match") ? LOCTEXT("SeenMatch", "IN MATCH") : LOCTEXT("SeenOnline", "ONLINE");
	}
	if (LastSeenAt == FDateTime() || Now == FDateTime())
	{
		return LOCTEXT("SeenNever", "OFFLINE");
	}
	const int32 Minutes = FMath::Max(0, int32((Now - LastSeenAt).GetTotalMinutes()));
	if (Minutes < 60)
	{
		return LOCTEXT("SeenJustNow", "MINUTES AGO");
	}
	const int32 Hours = Minutes / 60;
	if (Hours < 24)
	{
		return FText::Format(LOCTEXT("SeenHours", "{0} HOURS AGO"), FText::AsNumber(Hours));
	}
	return FText::Format(LOCTEXT("SeenDays", "{0} DAYS AGO"), FText::AsNumber(Hours / 24));
}

FString FCampusPlayerCard::PrettyPublicId() const
{
	// Grouped in threes, because this number exists to be read out loud to a friend.
	FString Out;
	for (int32 Index = 0; Index < PublicId.Len(); ++Index)
	{
		if (Index > 0 && (PublicId.Len() - Index) % 3 == 0)
		{
			Out.AppendChar(TEXT(' '));
		}
		Out.AppendChar(PublicId[Index]);
	}
	return Out;
}

FLinearColor FCampusPlayerCard::StatusColor() const
{
	if (Presence == TEXT("match"))
	{
		return CampusAngular::Accent;
	}
	if (Presence == TEXT("lobby"))
	{
		return CampusAngular::Go;
	}
	if (Presence == TEXT("menu") || Presence == TEXT("online"))
	{
		return FLinearColor(1.f, 1.f, 1.f, 0.72f);
	}
	return CampusAngular::TextMuted;
}

FString FCampusPlayerCard::Initial() const
{
	return Name.IsEmpty() ? TEXT("?") : Name.Left(1).ToUpper();
}

FString FCampusServerParty::LeaderName() const
{
	for (const FCampusPartyMember& Member : Members)
	{
		if (Member.PlayerId == LeaderId)
		{
			return Member.Name;
		}
	}
	return FString();
}

TArray<FCampusPartyMember> FCampusServerParty::Roster() const
{
	TArray<FCampusPartyMember> Out;
	Out.Reserve(Members.Num() + Pending.Num());
	Out.Append(Members);
	Out.Append(Pending);
	return Out;
}

const FCampusSummaryRow* FCampusSummary::Mvp() const
{
	// The server orders the rows and flags the MVP; the client never re-derives it, or two phones
	// would disagree about who was player of the match.
	for (const FCampusSummaryRow& Row : Players)
	{
		if (Row.bMvp)
		{
			return &Row;
		}
	}
	return Players.Num() > 0 ? &Players[0] : nullptr;
}

FString FCampusProfile::ShortId() const
{
	FString Plain = PlayerId.Replace(TEXT("-"), TEXT(""));
	return Plain.IsEmpty() ? FString() : TEXT("#") + Plain.Left(8).ToUpper();
}

FString FCampusProfile::ShareText() const
{
	// The id is in the text so a friend can find this player; there is no public web origin yet, and a
	// dead link would be worse than no link.
	const FString Id = PublicId.IsEmpty() ? ShortId() : TEXT("ID ") + PublicId;
	return FString::Printf(TEXT("CAMPUS COMBAT — %s · LV %d · %d KILLS · %d MVPs · %s"),
		*Name.ToUpper(), Level, Kills, Mvps, *Id);
}

// ---------------------------------------------------------------------------------------------
// Lifetime

bool UCampusSocial::IsSocketAvailable()
{
#if WITH_WEBSOCKETS
	return true;
#else
	return false;
#endif
}

void UCampusSocial::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_WEBSOCKETS
	// Load the module here, not on the first invite, so a packaging mistake surfaces at boot.
	FModuleManager::Get().LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
#endif
	if (const UCampusSave* Profile = Save())
	{
		LastAppliedSeq = Profile->SocialSeq;
	}
	HeartbeatHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UCampusSocial::HeartbeatTick), 1.f);
	HousekeepingHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UCampusSocial::HousekeepingTick), 0.25f);

	// Android Doze gives us no background socket, so the honest behaviour is to close cleanly on the
	// way out (presence goes offline at once, and an invite is not spent on a ghost) and to reconnect
	// immediately on the way back in - a user-initiated event gets no backoff.
	BackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &UCampusSocial::HandleBackground);
	ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &UCampusSocial::HandleForeground);
}

void UCampusSocial::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(HousekeepingHandle);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(BackgroundHandle);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
	bWantConnection = false;
	CloseSocket(1000);
	Super::Deinitialize();
}

UCampusBackend* UCampusSocial::Api() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
}

UCampusSave* UCampusSocial::Save() const
{
	UCampusGameInstance* Instance = Cast<UCampusGameInstance>(GetGameInstance());
	return Instance ? Instance->GetProfile() : nullptr;
}

FString UCampusSocial::Me() const
{
	const UCampusBackend* Backend = Api();
	return Backend ? Backend->PlayerId : FString();
}

const FString& UCampusSocial::MyPublicId() const
{
	// Cached from the profile read; GET /me carries it too, so the ADD tab can show "MY ID" from the
	// moment the player signs in rather than only after they open their own profile.
	if (PublicId.IsEmpty())
	{
		if (const UCampusBackend* Backend = Api(); Backend && !Backend->PublicId.IsEmpty())
		{
			return Backend->PublicId;
		}
	}
	return PublicId;
}

// ---------------------------------------------------------------------------------------------
// The channel

void UCampusSocial::SetState(ECampusSocialState Next)
{
	if (State == Next)
	{
		return;
	}
	State = Next;
	OnStateChanged.Broadcast();
}

FText UCampusSocial::StatusLabel() const
{
	switch (State)
	{
	case ECampusSocialState::Live: return LOCTEXT("SocialLive", "ONLINE");
	case ECampusSocialState::Connecting: return LOCTEXT("SocialConnecting", "CONNECTING...");
	case ECampusSocialState::Retrying: return LOCTEXT("SocialRetry", "RECONNECTING...");
	case ECampusSocialState::GaveUp: return LOCTEXT("SocialDown", "SOCIAL OFFLINE — TAP TO RETRY");
	default: return LOCTEXT("SocialOff", "OFFLINE");
	}
}

void UCampusSocial::Start()
{
	const UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		return;
	}
	bWantConnection = true;
	Attempt = 0;
	ReconnectIn = 0.f;
	ColdRead();
	OpenSocket();
}

void UCampusSocial::Stop()
{
	bWantConnection = false;
	ReconnectIn = 0.f;
	Attempt = 0;
	CloseSocket(1000);
	SetState(ECampusSocialState::Off);
	ClearParty(FString());
	Notices.Reset();
	Roster = FCampusRoster();
	OnNoticesChanged.Broadcast();
	OnRosterChanged.Broadcast();
}

void UCampusSocial::OpenSocket()
{
#if WITH_WEBSOCKETS
	if (!bWantConnection || bBackgrounded || Socket.IsValid())
	{
		return;
	}
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn() || Backend->GetAccessToken().IsEmpty())
	{
		return;
	}
	const FString Url = GetDefault<UCampusNetSettings>()->WsBaseUrl() + TEXT("/social/ws");
	// THE TOKEN IS A HEADER, NEVER A QUERY PARAMETER: a URL token lands in ngrok's traffic inspector and
	// every proxy log in between. UE supports upgrade headers, and the clients are native, so there is no
	// browser to accommodate. The ngrok header rides the handshake exactly as it rides every API call.
	const TMap<FString, FString> Headers = {
		{ TEXT("Authorization"), TEXT("Bearer ") + Backend->GetAccessToken() },
		{ TEXT("ngrok-skip-browser-warning"), TEXT("1") },
	};
	SetState(Attempt > 0 ? ECampusSocialState::Retrying : ECampusSocialState::Connecting);
	Socket = FWebSocketsModule::Get().CreateWebSocket(Url, FString(Subprotocol), Headers);
	if (!Socket.IsValid())
	{
		ScheduleReconnect();
		return;
	}
	// Every IWebSocket delegate fires on the game thread (the lws manager ticks there), so touching
	// UObjects and Slate straight out of OnMessage is safe and no AsyncTask is wanted.
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Socket->OnConnected().AddLambda([WeakThis]()
	{
		if (UCampusSocial* Self = WeakThis.Get())
		{
			// `hello` is what actually resets the backoff; this only says the TCP/TLS part worked.
			Self->SetState(ECampusSocialState::Connecting);
		}
	});
	Socket->OnConnectionError().AddLambda([WeakThis](const FString& Error)
	{
		if (UCampusSocial* Self = WeakThis.Get())
		{
			UE_LOG(LogTemp, Warning, TEXT("[CampusSocial] connect failed: %s"), *Error);
			Self->Socket.Reset();
			Self->ScheduleReconnect();
		}
	});
	Socket->OnClosed().AddLambda([WeakThis](int32 Code, const FString& Reason, bool bWasClean)
	{
		UCampusSocial* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		UE_LOG(LogTemp, Log, TEXT("[CampusSocial] closed %d (%s)"), Code, *Reason);
		Self->Socket.Reset();
		switch (Code)
		{
		case 4001:
			// UNAUTHORIZED: do not retry on the same token. The next API call refreshes it, and the
			// lobby's own Start() on the next sign-in or foreground brings the socket back.
			Self->bWantConnection = false;
			Self->SetState(ECampusSocialState::GaveUp);
			return;
		case 4009:
			// TOO_MANY_SOCKETS: this account already has three. Reconnecting would just be refused.
			Self->bWantConnection = false;
			Self->SetState(ECampusSocialState::GaveUp);
			return;
		case 4003:
			// TOKEN_EXPIRED: a fresh token is one API call away, so reconnect at once.
			Self->Attempt = 0;
			break;
		case 4008:
			// RATE_LIMITED: go straight to the ceiling rather than hammering a free tunnel.
			Self->Attempt = BackoffMaxAttempt;
			break;
		default:
			break;
		}
		Self->ScheduleReconnect();
	});
	Socket->OnMessage().AddLambda([WeakThis](const FString& Message)
	{
		if (UCampusSocial* Self = WeakThis.Get())
		{
			Self->HandleMessage(Message);
		}
	});
	Socket->Connect();
#else
	// A build with no WebSocket still works: every mutation is HTTP, and the cold read below is the
	// whole picture. Presence is coarse (POST /v1/social/presence, 2/min) and invites arrive on the
	// next screen open rather than instantly - degraded, never broken.
	SetState(ECampusSocialState::Off);
#endif
}

void UCampusSocial::CloseSocket(int32 Code)
{
#if WITH_WEBSOCKETS
	if (Socket.IsValid())
	{
		TSharedPtr<IWebSocket> Closing = Socket;
		Socket.Reset();
		Closing->OnConnected().Clear();
		Closing->OnConnectionError().Clear();
		Closing->OnClosed().Clear();
		Closing->OnMessage().Clear();
		if (Closing->IsConnected())
		{
			Closing->Close(Code);
		}
	}
#endif
	SentPresence = ECampusPresence::None;
	bReauthWanted = false;
	SyncDeadline = 0.f;
}

void UCampusSocial::ScheduleReconnect()
{
	if (!bWantConnection || bBackgrounded)
	{
		SetState(bWantConnection ? ECampusSocialState::Retrying : ECampusSocialState::Off);
		return;
	}
	if (Attempt >= BackoffMaxAttempt)
	{
		// Give up VISIBLY. A socket that retries for ever is a battery bug, and the room-code path
		// keeps working with zero social features - which is the whole reason not to delete it.
		SetState(ECampusSocialState::GaveUp);
		return;
	}
	++Attempt;
	// FULL jitter, not "exponential plus a bit": twenty phones reconnecting after the owner's laptop
	// lid closes must not land inside the same 200 ms against a free tunnel's 4,000 req/min ceiling.
	ReconnectIn = FMath::FRandRange(0.f, FMath::Min(BackoffCeiling, float(1 << Attempt)));
	SetState(ECampusSocialState::Retrying);
}

void UCampusSocial::SendFrame(const FString& Frame)
{
#if WITH_WEBSOCKETS
	if (Socket.IsValid() && Socket->IsConnected())
	{
		Socket->Send(Frame);
	}
#endif
}

void UCampusSocial::HandleBackground()
{
	bBackgrounded = true;
	// Close(1000) marks presence offline immediately on the server, so the 90 s TTL only ever bites on
	// a hard kill - exactly the case where nobody can know.
	CloseSocket(1000);
	SetState(bWantConnection ? ECampusSocialState::Retrying : ECampusSocialState::Off);
}

void UCampusSocial::HandleForeground()
{
	bBackgrounded = false;
	if (!bWantConnection)
	{
		return;
	}
	// No backoff: coming back to the app is a user-initiated event.
	Attempt = 0;
	ReconnectIn = 0.f;
	OpenSocket();
}

bool UCampusSocial::HeartbeatTick(float DeltaTime)
{
	if (!bWantConnection)
	{
		return true;
	}
	HeartbeatIn -= DeltaTime;
	const bool bConnected = IsLive();
	// A screen change pushes a beat straight away so a friend's list is right within a second of them
	// opening the lobby; otherwise it is one frame every 30 s and costs no HTTP request at all.
	if (bConnected && (HeartbeatIn <= 0.f || SentPresence != Presence))
	{
		HeartbeatIn = HeartbeatSeconds;
		SentPresence = Presence;
		const TSharedPtr<FJsonObject> Frame = MakeShared<FJsonObject>();
		Frame->SetStringField(TEXT("t"), TEXT("hb"));
		Frame->SetStringField(TEXT("st"), PresenceName(Presence));
		if (Party.bValid)
		{
			Frame->SetStringField(TEXT("party"), Party.Id);
		}
		SendFrame(Encode(Frame));
	}
	return true;
}

bool UCampusSocial::HousekeepingTick(float DeltaTime)
{
	if (ReconnectIn > 0.f)
	{
		ReconnectIn -= DeltaTime;
		if (ReconnectIn <= 0.f)
		{
			ReconnectIn = 0.f;
			OpenSocket();
		}
	}

	if (bReauthWanted && IsLive())
	{
		bReauthWanted = false;
		if (const UCampusBackend* Backend = Api(); Backend && !Backend->GetAccessToken().IsEmpty())
		{
			const TSharedPtr<FJsonObject> Frame = MakeShared<FJsonObject>();
			Frame->SetStringField(TEXT("t"), TEXT("auth"));
			Frame->SetStringField(TEXT("token"), Backend->GetAccessToken());
			SendFrame(Encode(Frame));
		}
	}

	if (AckSeq > 0)
	{
		AckIn -= DeltaTime;
		if (AckIn <= 0.f)
		{
			const TSharedPtr<FJsonObject> Frame = MakeShared<FJsonObject>();
			Frame->SetStringField(TEXT("t"), TEXT("ack"));
			Frame->SetNumberField(TEXT("seq"), double(AckSeq));
			SendFrame(Encode(Frame));
			AckSeq = 0;
			AckIn = AckEvery;
		}
	}

	if (SyncDeadline > 0.f)
	{
		SyncDeadline -= DeltaTime;
		if (SyncDeadline <= 0.f)
		{
			SyncDeadline = 0.f;
			CatchUpOverHttp();
		}
	}

	// A card's own clock: it animates out at expires_at without asking anybody, and sends NOTHING.
	// An ignored invite therefore costs zero requests, which on a 20,000-a-month budget is the
	// difference between "ignore it" being free and costing quota.
	bool bExpired = false;
	for (FCampusNotice& Notice : Notices)
	{
		if (Notice.Exit.IsEmpty() && Notice.ExpiresAt != FDateTime() && SecondsUntilPrecise(Notice.ExpiresAt) <= 0.f)
		{
			Notice.Exit = TEXT("EXPIRED");
			bExpired = true;
		}
	}
	if (bExpired)
	{
		OnNoticesChanged.Broadcast();
	}
	return true;
}

void UCampusSocial::SetScreenState(ECampusPresence InState)
{
	if (Presence == InState)
	{
		return;
	}
	Presence = InState;
	// Card visibility has exactly ONE owner, and it is the lobby widget's Show() - two sources would
	// fight over it the moment a dialog opened. This function owns presence and nothing else.
}

// ---------------------------------------------------------------------------------------------
// The server clock

void UCampusSocial::TakeServerTime(const TSharedPtr<FJsonObject>& Json)
{
	FDateTime Server;
	if (Json.IsValid() && FDateTime::ParseIso8601(*Str(Json, TEXT("server_time")), Server))
	{
		ServerTime = Server;
		ServerReadAt = FPlatformTime::Seconds();
	}
}

FDateTime UCampusSocial::Now() const
{
	if (ServerTime == FDateTime())
	{
		return FDateTime::UtcNow();
	}
	return ServerTime + FTimespan::FromSeconds(FPlatformTime::Seconds() - ServerReadAt);
}

int32 UCampusSocial::SecondsUntil(const FDateTime& When) const
{
	return FMath::Max(0, int32(SecondsUntilPrecise(When)));
}

float UCampusSocial::SecondsUntilPrecise(const FDateTime& When) const
{
	if (When == FDateTime())
	{
		return 0.f;
	}
	return float((When - Now()).GetTotalSeconds());
}

// ---------------------------------------------------------------------------------------------
// Frames

void UCampusSocial::HandleMessage(const FString& Message)
{
	const TSharedPtr<FJsonObject> Frame = Parse(Message);
	if (!Frame.IsValid())
	{
		return;
	}
	const FString Type = Str(Frame, TEXT("t"));
	if (Type == TEXT("hello"))
	{
		HandleHello(Frame);
	}
	else if (Type == TEXT("ev"))
	{
		HandleEvent(Frame);
	}
	else if (Type == TEXT("px"))
	{
		HandleEphemeral(Frame);
	}
	else if (Type == TEXT("synced"))
	{
		SyncDeadline = 0.f;
		const int64 NextSeq = int64(Num(Frame, TEXT("next_seq")));
		if (Held.Num() > 0 && NextSeq <= LastAppliedSeq)
		{
			// The server has nothing more to send, so whatever we held was never coming: take the
			// buffer at face value rather than stalling the stack for ever.
			LastAppliedSeq = Held[0].Seq - 1;
		}
		// Drain anything the resend filled in.
		ApplyEvent(0, FString(), FString(), nullptr, FDateTime());
	}
	else if (Type == TEXT("authed"))
	{
		bReauthWanted = false;
	}
}

void UCampusSocial::HandleHello(const TSharedPtr<FJsonObject>& Frame)
{
	FDateTime Server;
	if (FDateTime::ParseIso8601(*Str(Frame, TEXT("server_time")), Server))
	{
		ServerTime = Server;
		ServerReadAt = FPlatformTime::Seconds();
	}
	Attempt = 0;
	SetState(ECampusSocialState::Live);
	HeartbeatIn = 0.f;
	SentPresence = ECampusPresence::None;   // push one beat immediately

	const int64 NextSeq = int64(Num(Frame, TEXT("next_seq")));
	if (NextSeq < LastAppliedSeq)
	{
		// The inbox was pruned (or this is a different account): trust the server's number.
		LastAppliedSeq = NextSeq;
	}
	else if (NextSeq > LastAppliedSeq + CatchUpMax)
	{
		// Too far behind for a replay to be worth it: re-read the state and jump the cursor.
		LastAppliedSeq = NextSeq;
		ColdRead();
	}
	else if (NextSeq > LastAppliedSeq)
	{
		RequestSync();
	}
	if (UCampusSave* Profile = Save())
	{
		Profile->SocialSeq = LastAppliedSeq;
	}
}

void UCampusSocial::HandleEphemeral(const TSharedPtr<FJsonObject>& Frame)
{
	const FString Type = Str(Frame, TEXT("type"));
	const TSharedPtr<FJsonObject> Payload = Obj(Frame, TEXT("p"));
	if (Type == TEXT("server.notice"))
	{
		const FString Kind = Str(Payload, TEXT("kind"));
		if (Kind == TEXT("reauth"))
		{
			// Zero HTTP requests: push the token we already hold up the socket we already have.
			bReauthWanted = true;
		}
		else if (Kind == TEXT("shutdown") || Kind == TEXT("maintenance"))
		{
			// Pre-stagger over 30 s so twenty phones do not land in the same 200 ms after a deploy.
			Attempt = 0;
			ReconnectIn = FMath::FRandRange(1.f, 30.f);
		}
		return;
	}
	if (Type == TEXT("presence.changed"))
	{
		const FString Who = Str(Payload, TEXT("player_id"));
		const FString NowState = Str(Payload, TEXT("presence"));
		bool bTouched = false;
		for (FCampusPlayerCard& Card : Roster.Friends)
		{
			if (Card.PlayerId == Who)
			{
				Card.Presence = NowState;
				Card.bInParty = Bool(Payload, TEXT("in_party"));
				bTouched = true;
			}
		}
		if (bTouched)
		{
			// Presence is a live top-up on a list the panel read in full when it opened; it never
			// re-sorts, because the server owns the order and two screens must not disagree.
			OnRosterChanged.Broadcast();
		}
		return;
	}
	if (Type == TEXT("party.presence"))
	{
		const FString Who = Str(Payload, TEXT("player_id"));
		const FString Next = Str(Payload, TEXT("presence"));
		for (FCampusPartyMember& Member : Party.Members)
		{
			if (Member.PlayerId == Who && Member.Presence != Next)
			{
				Member.Presence = Next;
				PushRosterToLobby();
				OnPartyChanged.Broadcast();
				return;
			}
		}
	}
}

void UCampusSocial::HandleEvent(const TSharedPtr<FJsonObject>& Frame)
{
	const int64 Seq = int64(Num(Frame, TEXT("seq")));
	const FString Id = Str(Frame, TEXT("id"));
	const FString Type = Str(Frame, TEXT("type"));
	const TSharedPtr<FJsonObject> Payload = Obj(Frame, TEXT("p"));
	FDateTime Expires;
	FDateTime::ParseIso8601(*Str(Frame, TEXT("exp")), Expires);
	ApplyEvent(Seq, Id, Type, Payload, Expires);
}

void UCampusSocial::RequestSync()
{
	const TSharedPtr<FJsonObject> Frame = MakeShared<FJsonObject>();
	Frame->SetStringField(TEXT("t"), TEXT("sync"));
	Frame->SetNumberField(TEXT("after"), double(LastAppliedSeq));
	SendFrame(Encode(Frame));
	// If the resend does not answer, repair over HTTP - one request, not a poll.
	SyncDeadline = SyncFallback;
}

void UCampusSocial::CatchUpOverHttp()
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn() || bCatchingUp)
	{
		return;
	}
	bCatchingUp = true;
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("GET"), FString::Printf(TEXT("/social/events?after=%lld"), static_cast<long long>(LastAppliedSeq)), nullptr, false,
		[WeakThis](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusSocial* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bCatchingUp = false;
		if (!bOk)
		{
			return;
		}
		FDateTime Server;
		if (FDateTime::ParseIso8601(*Str(Json, TEXT("server_time")), Server))
		{
			Self->ServerTime = Server;
			Self->ServerReadAt = FPlatformTime::Seconds();
		}
		if (const TArray<TSharedPtr<FJsonValue>>* Values = Arr(Json, TEXT("events")))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject> Row = Value->AsObject();
				Self->ApplyEvent(int64(Num(Row, TEXT("seq"))), Str(Row, TEXT("id")), Str(Row, TEXT("type")),
					Obj(Row, TEXT("payload")), Stamp(Row, TEXT("expires_at")));
			}
		}
		const int64 NextSeq = int64(Num(Json, TEXT("next_seq")));
		if (NextSeq > Self->LastAppliedSeq)
		{
			// Everything between was expired and skipped by the server; do not stall on it.
			Self->LastAppliedSeq = NextSeq;
			Self->Held.Reset();
		}
		if (int32(Num(Json, TEXT("dropped_expired"))) > 0)
		{
			Self->OnNotice.Broadcast(TEXT("YOU MISSED AN INVITE WHILE YOU WERE AWAY."));
		}
	});
}

void UCampusSocial::ColdRead()
{
	// THREE requests, so this is throttled. Start() is called both from the lobby controller's
	// BeginPlay and from AfterSignIn, which on a cold launch is two calls inside a second - and on a
	// tunnel metered at 20,000 requests a MONTH, six requests where three will do is not free.
	const double Now = FPlatformTime::Seconds();
	if (LastColdReadAt > 0.0 && Now - LastColdReadAt < 5.0)
	{
		return;
	}
	LastColdReadAt = Now;

	// One round trip each: the party, my invite tray, and the friends list.
	RefreshParty(nullptr);
	RefreshFriends(nullptr);
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		return;
	}
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("GET"), TEXT("/party-invites"), nullptr, false,
		[WeakThis](bool bOk, const FString&, TSharedPtr<FJsonObject> Json)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->ReadInviteTray(Json);
		}
	});
}

void UCampusSocial::ApplyEvent(int64 Seq, const FString& Id, const FString& Type, const TSharedPtr<FJsonObject>& Payload, const FDateTime& Expires)
{
	if (Seq > 0)
	{
		// 1. Already applied -> drop silently. 2. The id ring catches a repeat over a second transport.
		if (Seq <= LastAppliedSeq || (!Id.IsEmpty() && RecentEventIds.Contains(Id)))
		{
			return;
		}
		// 3. Expiry beats delivery: a dead card must not slide in six minutes late.
		if (Expires != FDateTime() && SecondsUntilPrecise(Expires) <= 0.f)
		{
			if (Seq == LastAppliedSeq + 1)
			{
				LastAppliedSeq = Seq;
			}
			return;
		}
		// 4. A gap: hold this one, ask for a resend, and fall back to HTTP if nothing comes.
		if (Seq > LastAppliedSeq + 1)
		{
			FHeldEvent Hold;
			Hold.Seq = Seq;
			Hold.Id = Id;
			Hold.Type = Type;
			Hold.Payload = Payload;
			Hold.Expires = Expires;
			Held.Add(MoveTemp(Hold));
			Held.Sort([](const FHeldEvent& A, const FHeldEvent& B) { return A.Seq < B.Seq; });
			if (SyncDeadline <= 0.f)
			{
				RequestSync();
			}
			return;
		}
		LastAppliedSeq = Seq;
		if (!Id.IsEmpty())
		{
			RecentEventIds.Add(Id);
			if (RecentEventIds.Num() > DedupeRing)
			{
				RecentEventIds.RemoveAt(0, RecentEventIds.Num() - DedupeRing, EAllowShrinking::No);
			}
		}
		AckSeq = FMath::Max(AckSeq, Seq);
		if (UCampusSave* Profile = Save())
		{
			Profile->SocialSeq = LastAppliedSeq;
		}
	}

	if (!Type.IsEmpty())
	{
		// ---- party roster: one shape, one call. The plate appears only on the server's word, never
		// optimistically - an accept that races a disband would otherwise show a ghost member.
		if (Type == TEXT("party.joined") || Type == TEXT("party.left") || Type == TEXT("party.leader_changed")
			|| Type == TEXT("party.mode_changed"))
		{
			ReadParty(Obj(Payload, TEXT("party")));
			if (Type == TEXT("party.left") && Str(Payload, TEXT("reason")) == TEXT("kicked")
				&& Str(Payload, TEXT("player_id")) == Me())
			{
				OnNotice.Broadcast(TEXT("YOU WERE REMOVED FROM THE SQUAD."));
			}
		}
		else if (Type == TEXT("party.disbanded"))
		{
			const FString Reason = Str(Payload, TEXT("reason"));
			ClearParty(Reason);
		}
		else if (Type == TEXT("party.invited"))
		{
			FCampusNotice Notice;
			Notice.Kind = ECampusNoticeKind::Invite;
			Notice.Key = Str(Payload, TEXT("invite_id"));
			Notice.Sender = ReadCard(Obj(Payload, TEXT("inviter")));
			Notice.Mode = FMath::Max(2, int32(Num(Payload, TEXT("mode"))));
			Notice.MemberCount = FMath::Max(1, int32(Num(Payload, TEXT("member_count"))));
			Notice.ExpiresAt = Stamp(Payload, TEXT("expires_at"));
			Notice.TotalSeconds = FMath::Max(1.f, SecondsUntilPrecise(Notice.ExpiresAt));
			PushNotice(MoveTemp(Notice));
		}
		else if (Type == TEXT("party.invite_resolved") || Type == TEXT("party.invite_superseded"))
		{
			// A card on screen that the server has closed slides out at once with an honest caption.
			const FString InviteId = Str(Payload, TEXT("invite_id"));
			const FString Resolution = Str(Payload, TEXT("state"));
			if (FindNotice(InviteId))
			{
				const FString Exit = Resolution == TEXT("cancelled") ? TEXT("CANCELLED")
					: (Resolution == TEXT("superseded") || Type == TEXT("party.invite_superseded")) ? TEXT("TAKEN")
					: TEXT("EXPIRED");
				ResolveNotice(InviteId, Exit);
			}
			// The inviter's own pending plate resolves off the party view, which arrives with it.
			if (const TSharedPtr<FJsonObject> View = Obj(Payload, TEXT("party")))
			{
				ReadParty(View);
			}
			else if (Party.bValid)
			{
				RefreshParty(nullptr);
			}
		}
		else if (Type == TEXT("party.invite_sent"))
		{
			RefreshParty(nullptr);
		}
		else if (Type == TEXT("party.room_ready"))
		{
			const FString Code = Str(Payload, TEXT("code"));
			if (Party.bValid)
			{
				Party.RoomCode = Code;
				Party.State = TEXT("in_match");
			}
			// The leader is already hosting; only a member needs pulling in.
			if (!Code.IsEmpty() && Str(Payload, TEXT("leader_id")) != Me())
			{
				OnRoomReady.Broadcast(Code);
			}
		}
		else if (Type == TEXT("friend.requested"))
		{
			FCampusNotice Notice;
			Notice.Kind = ECampusNoticeKind::FriendRequest;
			Notice.Sender = ReadCard(Obj(Payload, TEXT("player")));
			Notice.Key = Notice.Sender.PlayerId;
			// A friend request has no deadline, so it simply holds for six seconds.
			Notice.ExpiresAt = Now() + FTimespan::FromSeconds(FriendCardSeconds);
			Notice.TotalSeconds = FriendCardSeconds;
			PushNotice(MoveTemp(Notice));
			RefreshFriends(nullptr);
		}
		else if (Type == TEXT("friend.accepted"))
		{
			const FCampusPlayerCard Card = ReadCard(Obj(Payload, TEXT("player")));
			OnNotice.Broadcast(FString::Printf(TEXT("%s IS NOW YOUR FRIEND."), *Card.Name.ToUpper()));
			RefreshFriends(nullptr);
		}
		else if (Type == TEXT("friend.changed"))
		{
			RefreshFriends(nullptr);
		}
		else if (Type == TEXT("match.summary_ready"))
		{
			const FString Code = Str(Payload, TEXT("room_code"));
			if (!Code.IsEmpty())
			{
				PendingSummary = Code;
				OnSummaryReady.Broadcast(Code);
			}
		}
		else if (Type == TEXT("profile.milestone"))
		{
			// Applause needs no answer, so this is a toast and not a card. Cards are for decisions,
			// and a card for every milestone would train players to ignore the channel invites need.
			OnNotice.Broadcast(FString::Printf(TEXT("MILESTONE: %s"), *Str(Payload, TEXT("name")).ToUpper()));
		}
	}

	// Drain anything that was held past a gap this event may have just closed.
	for (int32 Index = 0; Index < Held.Num(); )
	{
		if (Held[Index].Seq <= LastAppliedSeq)
		{
			Held.RemoveAt(Index, EAllowShrinking::No);
			continue;
		}
		if (Held[Index].Seq != LastAppliedSeq + 1)
		{
			break;
		}
		const FHeldEvent Next = Held[Index];
		Held.RemoveAt(Index, EAllowShrinking::No);
		ApplyEvent(Next.Seq, Next.Id, Next.Type, Next.Payload, Next.Expires);
		Index = 0;
	}
}

// ---------------------------------------------------------------------------------------------
// Party

bool UCampusSocial::IsPartyLeader() const
{
	return Party.IsLeader(Me());
}

void UCampusSocial::ReadParty(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	FDateTime Server;
	if (FDateTime::ParseIso8601(*Str(Json, TEXT("server_time")), Server))
	{
		ServerTime = Server;
		ServerReadAt = FPlatformTime::Seconds();
	}
	const FString LeaderId = Str(Json, TEXT("leader_id"));
	FCampusServerParty Next;
	Next.bValid = true;
	Next.Id = Str(Json, TEXT("id"));
	Next.LeaderId = LeaderId;
	Next.State = Str(Json, TEXT("state"));
	Next.RoomCode = Str(Json, TEXT("room_code"));
	Next.Mode = FMath::Clamp(int32(Num(Json, TEXT("mode"))), 2, UCampusParty::MaxMembers);
	if (const TArray<TSharedPtr<FJsonValue>>* Members = Arr(Json, TEXT("members")))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Members)
		{
			Next.Members.Add(ReadMember(Value->AsObject(), true, LeaderId));
		}
	}
	if (const TArray<TSharedPtr<FJsonValue>>* Pending = Arr(Json, TEXT("pending_invites")))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Pending)
		{
			Next.Pending.Add(ReadMember(Value->AsObject(), false, LeaderId));
		}
	}
	// A party of one is not a party: the server disbands it, and this keeps the client honest if a
	// stale view ever says otherwise.
	if (Next.Members.Num() <= 1 && Next.Pending.Num() == 0)
	{
		const FString WasId = Party.Id;
		Party = FCampusServerParty();
		if (!WasId.IsEmpty())
		{
			MarkPendingInvites();
			PushRosterToLobby();
			OnPartyChanged.Broadcast();
			OnRosterChanged.Broadcast();
		}
		return;
	}
	Party = MoveTemp(Next);
	MarkPendingInvites();
	PushRosterToLobby();
	OnPartyChanged.Broadcast();
	// The picker's rows live on the roster, so they have to be told the party moved.
	OnRosterChanged.Broadcast();
}

void UCampusSocial::ClearParty(const FString& Reason)
{
	const bool bHad = Party.bValid;
	Party = FCampusServerParty();
	if (!bHad)
	{
		return;
	}
	MarkPendingInvites();
	PushRosterToLobby();
	OnPartyChanged.Broadcast();
	OnRosterChanged.Broadcast();
	if (Reason == TEXT("leader_left") || Reason == TEXT("leader_timeout"))
	{
		OnNotice.Broadcast(TEXT("THE SQUAD BROKE UP — THE LEADER LEFT."));
	}
	else if (!Reason.IsEmpty() && Reason != TEXT("mode_solo") && Reason != TEXT("empty"))
	{
		OnNotice.Broadcast(TEXT("YOUR SQUAD DISBANDED."));
	}
}

void UCampusSocial::PushRosterToLobby()
{
	UCampusParty* Squad = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusParty>() : nullptr;
	if (!Squad)
	{
		return;
	}
	if (!Party.bValid)
	{
		// No server party: empty the roster and hand the lobby back to its own local behaviour. The
		// next SyncParty() re-adds the local player as leader - which is right even when the party we
		// just left was somebody else's, where ClearGuests() would have left THEIR plate standing.
		Squad->ApplyRoster(TArray<FCampusPartyMember>(), true);
		return;
	}
	// The server decides how wide the plate row is, and who holds the crown.
	Squad->SetCapacity(Party.Mode);
	Squad->ApplyRoster(Party.Roster(), true);
}

void UCampusSocial::RefreshParty(FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		if (Done)
		{
			Done(false, TEXT("Not signed in."));
		}
		return;
	}
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("GET"), TEXT("/parties/me"), nullptr, false,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusSocial* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (bOk)
		{
			// 204 (no body) means "you are alone", which is the common case.
			const TSharedPtr<FJsonObject> View = Obj(Json, TEXT("party"));
			if (View.IsValid())
			{
				Self->ReadParty(View);
			}
			else
			{
				Self->ClearParty(FString());
			}
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

/** Every party mutation shares this tail: read the party view out of the response and repaint. */
void UCampusSocial::InvitePlayer(const FString& PlayerId, int32 Mode, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	const TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("player_id"), PlayerId);
	// An existing party keeps its own mode; a new one opens at the width the lobby is showing. Either
	// way this is ONE round trip, because POST /invites creates the party when the caller has none.
	// The API only accepts 2 or 4: a party of one is not a party, and there is no 3.
	const int32 Opening = Mode == 2 ? 2 : 4;
	Body->SetNumberField(TEXT("mode"), Party.bValid ? Party.Mode : Opening);
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("POST"), TEXT("/parties/me/invites"), Body, true,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusSocial* Self = WeakThis.Get();
		if (Self && bOk)
		{
			Self->ReadParty(Obj(Json, TEXT("party")));
			// "OFFLINE — THEY WON'T SEE THIS" is better than silence that reads as "they ignored me".
			if (Str(Json, TEXT("invitee_state")) == TEXT("offline"))
			{
				Self->OnNotice.Broadcast(TEXT("THEY ARE OFFLINE — THEY WILL SEE IT WHEN THEY OPEN THE GAME."));
			}
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::CancelInvite(const FString& InviteId, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("DELETE"), TEXT("/parties/me/invites/") + InviteId, nullptr, true,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->RefreshParty(nullptr);
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::AcceptInvite(const FString& InviteId, bool bLeaveCurrent, FCodeDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet."), FString()); }
		return;
	}
	const TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetBoolField(TEXT("leave_current"), bLeaveCurrent);
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("POST"), FString::Printf(TEXT("/party-invites/%s/accept"), *InviteId), Body, true,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->ReadParty(Obj(Json, TEXT("party")));
		}
		if (Done)
		{
			// The error envelope is one shape everywhere: {"error":{"code","message","request_id"}}.
			Done(bOk, Error, bOk ? FString() : Str(Obj(Json, TEXT("error")), TEXT("code")));
		}
	});
}

void UCampusSocial::RejectInvite(const FString& InviteId, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	Backend->CallApi(TEXT("POST"), FString::Printf(TEXT("/party-invites/%s/reject"), *InviteId), nullptr, true,
		[Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::LeaveParty(FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("DELETE"), TEXT("/parties/me"), nullptr, true,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->ClearParty(FString());
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::KickMember(const FString& PlayerId, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("DELETE"), TEXT("/parties/me/members/") + PlayerId, nullptr, true,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->ReadParty(Obj(Json, TEXT("party")));
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::SetPartyMode(int32 Mode, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	const TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("mode"), Mode);
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("PATCH"), TEXT("/parties/me"), Body, true,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->ReadParty(Obj(Json, TEXT("party")));
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::HandOffRoom(const FString& Code, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	const TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("code"), Code);
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("POST"), TEXT("/parties/me/room"), Body, true,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->ReadParty(Obj(Json, TEXT("party")));
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

// ---------------------------------------------------------------------------------------------
// Friends

const FCampusPlayerCard* UCampusSocial::FindFriend(const FString& PlayerId) const
{
	return Roster.Friends.FindByPredicate([&PlayerId](const FCampusPlayerCard& Card) { return Card.PlayerId == PlayerId; });
}

void UCampusSocial::ReadRoster(const TSharedPtr<FJsonObject>& Json)
{
	// GET /v1/friends carries server_time, and "4 HOURS AGO" is measured from it - a phone whose
	// clock is a day out would otherwise tell the player their friend was last seen tomorrow.
	TakeServerTime(Json);
	// The server's order is the order: lobby, then menu, then in a match, then offline, then by name.
	// The client does NOT re-sort - two screens must not disagree about who is at the top of the list.
	ReadCards(Json, TEXT("friends"), TEXT("friends"), Roster.Friends);
	ReadCards(Json, TEXT("incoming"), TEXT("incoming"), Roster.Incoming);
	ReadCards(Json, TEXT("outgoing"), TEXT("outgoing"), Roster.Outgoing);
	ReadCards(Json, TEXT("blocked"), TEXT("blocked"), Roster.Blocked);
	Roster.bDegraded = Bool(Json, TEXT("presence_degraded"));
	if (const TSharedPtr<FJsonObject> Limits = Obj(Json, TEXT("limits")))
	{
		if (const TSharedPtr<FJsonObject> Friends = Obj(Limits, TEXT("friends")))
		{
			Roster.FriendsUsed = int32(Num(Friends, TEXT("used")));
			Roster.FriendsMax = FMath::Max(1, int32(Num(Friends, TEXT("max"))));
		}
	}
	Roster.bValid = true;
	MarkPendingInvites();
	OnRosterChanged.Broadcast();
}

/**
 * Stamps every friend row with my pending invite to them, so the picker reads INVITED instead of
 * offering a button that would come back 409. Called from BOTH reads: the party view changes far more
 * often than the roster does (an invite answers in one round trip, and re-fetching 100 friends to
 * repaint one row would be absurd on a metered tunnel).
 */
void UCampusSocial::MarkPendingInvites()
{
	for (FCampusPlayerCard& Card : Roster.Friends)
	{
		Card.PendingInviteId.Reset();
		for (const FCampusPartyMember& Invited : Party.Pending)
		{
			if (Invited.PlayerId == Card.PlayerId)
			{
				Card.PendingInviteId = Invited.InviteId;
			}
		}
	}
}

void UCampusSocial::RefreshFriends(FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		if (Done) { Done(false, TEXT("Not signed in.")); }
		return;
	}
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("GET"), TEXT("/friends"), nullptr, false,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->ReadRoster(Json);
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::RefreshRecent(FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		if (Done) { Done(false, TEXT("Not signed in.")); }
		return;
	}
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	// GET /v1/players/suggested is the curated list; GET /v1/players/recent is what shipped first and
	// is the fallback, so this works against either server without a build flag.
	Backend->CallApi(TEXT("GET"), TEXT("/players/suggested"), nullptr, false,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusSocial* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (bOk)
		{
			Self->TakeServerTime(Json);
			ReadCards(Json, TEXT("players"), FString(), Self->Roster.Recent);
			Self->Roster.bRecentLoaded = true;
			Self->OnRosterChanged.Broadcast();
			if (Done)
			{
				Done(true, FString());
			}
			return;
		}
		UCampusBackend* Backend2 = Self->Api();
		if (!Backend2)
		{
			if (Done) { Done(false, Error); }
			return;
		}
		Backend2->CallApi(TEXT("GET"), TEXT("/players/recent"), nullptr, false,
			[WeakThis, Done](bool bOk2, const FString& Error2, TSharedPtr<FJsonObject> Json2)
		{
			if (UCampusSocial* Again = WeakThis.Get(); Again && bOk2)
			{
				ReadCards(Json2, TEXT("players"), FString(), Again->Roster.Recent);
				Again->Roster.bRecentLoaded = true;
				Again->OnRosterChanged.Broadcast();
			}
			if (Done)
			{
				Done(bOk2, Error2);
			}
		});
	});
}

void UCampusSocial::ClearSearch()
{
	if (!Roster.bSearched && Roster.Results.Num() == 0 && Roster.SearchTerm.IsEmpty())
	{
		return;
	}
	Roster.bSearched = false;
	Roster.SearchTerm.Reset();
	Roster.Results.Reset();
	OnRosterChanged.Broadcast();
}

/**
 * ADD-by-ID / by-name. The server decides what a query means (all digits -> an exact public id,
 * anything else -> a name), so the client never has to guess and the two cannot drift apart.
 *
 * It is a REQUEST PER SEARCH, which is why it fires on the button and never while typing: a
 * search-as-you-type field on a tunnel metered at 20,000 requests a month is a quota bonfire.
 */
void UCampusSocial::SearchPlayers(const FString& Query, FDone Done)
{
	UCampusBackend* Backend = Api();
	const FString Trimmed = Query.TrimStartAndEnd();
	if (!Backend || Trimmed.Len() < 2)
	{
		// The route answers 400 VALIDATION under two characters. Say so here rather than spending a
		// request to be told.
		if (Done) { Done(false, TEXT("Type at least two characters, or a player ID.")); }
		return;
	}
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("GET"), TEXT("/players/search?q=") + FGenericPlatformHttp::UrlEncode(Trimmed), nullptr, false,
		[WeakThis, Trimmed, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusSocial* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (bOk)
		{
			// The route answers {query, matched_by, players:[...], server_time}; an older server has
			// no route at all, and that 404 is reported honestly rather than shown as "nobody found".
			Self->TakeServerTime(Json);
			ReadCards(Json, TEXT("players"), FString(), Self->Roster.Results);
			Self->Roster.SearchTerm = Trimmed;
			Self->Roster.bSearched = true;
			Self->OnRosterChanged.Broadcast();
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

namespace
{
	/** Every friends mutation answers {player_id, state, player}, so one tail serves all six. */
	void FriendCall(UCampusSocial* Self, UCampusBackend* Backend, const FString& Verb, const FString& Path,
		UCampusSocial::FDone Done)
	{
		if (!Backend)
		{
			if (Done) { Done(false, TEXT("Not ready yet.")); }
			return;
		}
		TWeakObjectPtr<UCampusSocial> WeakThis(Self);
		Backend->CallApi(Verb, Path, nullptr, true, [WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
		{
			if (UCampusSocial* Social = WeakThis.Get(); Social && bOk)
			{
				Social->RefreshFriends(nullptr);
			}
			if (Done)
			{
				Done(bOk, Error);
			}
		});
	}
}

void UCampusSocial::RequestFriend(const FString& PlayerId, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	const TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("player_id"), PlayerId);
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("POST"), TEXT("/friends/requests"), Body, true,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (UCampusSocial* Self = WeakThis.Get(); Self && bOk)
		{
			Self->RefreshFriends(nullptr);
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::AcceptFriend(const FString& PlayerId, FDone Done)
{
	FriendCall(this, Api(), TEXT("POST"), FString::Printf(TEXT("/friends/requests/%s/accept"), *PlayerId), Done);
}

void UCampusSocial::DeclineFriend(const FString& PlayerId, FDone Done)
{
	FriendCall(this, Api(), TEXT("POST"), FString::Printf(TEXT("/friends/requests/%s/decline"), *PlayerId), Done);
}

void UCampusSocial::RemoveFriend(const FString& PlayerId, FDone Done)
{
	FriendCall(this, Api(), TEXT("DELETE"), TEXT("/friends/") + PlayerId, Done);
}

void UCampusSocial::BlockPlayer(const FString& PlayerId, FDone Done)
{
	FriendCall(this, Api(), TEXT("POST"), FString::Printf(TEXT("/friends/%s/block"), *PlayerId), Done);
}

void UCampusSocial::UnblockPlayer(const FString& PlayerId, FDone Done)
{
	FriendCall(this, Api(), TEXT("DELETE"), FString::Printf(TEXT("/friends/%s/block"), *PlayerId), Done);
}

// ---------------------------------------------------------------------------------------------
// Notice cards

void UCampusSocial::PushNotice(FCampusNotice&& Notice)
{
	if (Notice.Key.IsEmpty() || Notice.Sender.PlayerId.IsEmpty())
	{
		return;
	}
	// Re-inviting the same friend refreshes the card rather than stacking a second one: the server
	// pushed expires_at forward and handed back the SAME invite_id.
	for (FCampusNotice& Existing : Notices)
	{
		if (Existing.Key == Notice.Key)
		{
			Existing = MoveTemp(Notice);
			OnNoticesChanged.Broadcast();
			return;
		}
	}
	// Newest first, and invites above friend requests: an invite has a deadline, a request does not.
	const int32 At = Notice.Kind == ECampusNoticeKind::Invite
		? 0
		: int32(Algo::CountIf(Notices, [](const FCampusNotice& Card) { return Card.Kind == ECampusNoticeKind::Invite; }));
	Notices.Insert(MoveTemp(Notice), FMath::Clamp(At, 0, Notices.Num()));
	OnNoticesChanged.Broadcast();
}

void UCampusSocial::SetNoticesVisible(bool bVisible)
{
	if (bNoticesVisible == bVisible)
	{
		return;
	}
	bNoticesVisible = bVisible;
	if (bVisible)
	{
		// Re-evaluated on lobby entry: anything that died while we were in a match is simply gone,
		// and a friend request that queued through the match gets a fresh six-second hold so it is
		// not animated out before the player's eyes have landed on it.
		Notices.RemoveAll([this](const FCampusNotice& Card)
		{
			return Card.Kind == ECampusNoticeKind::Invite
				&& Card.ExpiresAt != FDateTime() && SecondsUntilPrecise(Card.ExpiresAt) <= 0.f;
		});
		for (FCampusNotice& Card : Notices)
		{
			if (Card.Kind == ECampusNoticeKind::FriendRequest && SecondsUntilPrecise(Card.ExpiresAt) <= 0.f)
			{
				Card.ExpiresAt = Now() + FTimespan::FromSeconds(FriendCardSeconds);
				Card.TotalSeconds = FriendCardSeconds;
			}
		}
	}
	OnNoticesChanged.Broadcast();
}

FCampusNotice* UCampusSocial::FindNotice(const FString& Key)
{
	return Notices.FindByPredicate([&Key](const FCampusNotice& Card) { return Card.Key == Key; });
}

void UCampusSocial::ResolveNotice(const FString& Key, const FString& Exit)
{
	if (FCampusNotice* Card = FindNotice(Key))
	{
		Card->Exit = Exit.IsEmpty() ? TEXT("DONE") : Exit;
		Card->bBusy = false;
		OnNoticesChanged.Broadcast();
	}
}

void UCampusSocial::DropNotice(const FString& Key)
{
	if (Notices.RemoveAll([&Key](const FCampusNotice& Card) { return Card.Key == Key; }) > 0)
	{
		OnNoticesChanged.Broadcast();
	}
}

void UCampusSocial::ReadInviteTray(const TSharedPtr<FJsonObject>& Json)
{
	FDateTime Server;
	if (FDateTime::ParseIso8601(*Str(Json, TEXT("server_time")), Server))
	{
		ServerTime = Server;
		ServerReadAt = FPlatformTime::Seconds();
	}
	const TArray<TSharedPtr<FJsonValue>>* Values = Arr(Json, TEXT("invites"));
	if (!Values)
	{
		return;
	}
	// Oldest first so the newest ends up on top of the stack after the inserts.
	for (int32 Index = Values->Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<FJsonObject> Row = (*Values)[Index]->AsObject();
		FCampusNotice Notice;
		Notice.Kind = ECampusNoticeKind::Invite;
		Notice.Key = Str(Row, TEXT("invite_id"));
		Notice.Sender = ReadCard(Obj(Row, TEXT("player")));
		Notice.Mode = FMath::Max(2, int32(Num(Row, TEXT("mode"))));
		Notice.MemberCount = FMath::Max(1, int32(Num(Row, TEXT("member_count"))));
		Notice.ExpiresAt = Stamp(Row, TEXT("expires_at"));
		Notice.TotalSeconds = FMath::Max(1.f, SecondsUntilPrecise(Notice.ExpiresAt));
		if (Notice.TotalSeconds > 0.5f)
		{
			PushNotice(MoveTemp(Notice));
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Profile

namespace
{
	void ReadHistoryRows(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, TArray<FCampusHistoryRow>& Out)
	{
		Out.Reset();
		if (const TArray<TSharedPtr<FJsonValue>>* Values = Arr(Json, Field))
		{
			Out.Reserve(Values->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject> Row = Value->AsObject();
				FCampusHistoryRow Line;
				Line.RoomCode = Str(Row, TEXT("room_code"));
				Line.Map = Str(Row, TEXT("map"));
				Line.At = Stamp(Row, TEXT("at"));
				Line.Kills = int32(Num(Row, TEXT("kills")));
				Line.Deaths = int32(Num(Row, TEXT("deaths")));
				Line.Headshots = int32(Num(Row, TEXT("headshots")));
				Line.Points = int32(Num(Row, TEXT("points_awarded")));
				Line.Xp = int32(Num(Row, TEXT("xp_awarded")));
				Line.bWon = Bool(Row, TEXT("won"));
				Line.bRated = Bool(Row, TEXT("rated"));
				Line.bMvp = Bool(Row, TEXT("was_mvp"));
				Out.Add(MoveTemp(Line));
			}
		}
	}

	void ReadOwned(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, TArray<FCampusOwnedItem>& Out)
	{
		Out.Reset();
		if (const TArray<TSharedPtr<FJsonValue>>* Values = Arr(Json, Field))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject> Row = Value->AsObject();
				FCampusOwnedItem Item;
				Item.ItemId = Str(Row, TEXT("item_id"));
				Item.Name = Str(Row, TEXT("name"));
				Item.bEquipped = Bool(Row, TEXT("equipped"));
				Out.Add(MoveTemp(Item));
			}
		}
	}

	FCampusProfile ReadProfile(const TSharedPtr<FJsonObject>& Json, bool bMine)
	{
		FCampusProfile Out;
		if (!Json.IsValid())
		{
			return Out;
		}
		Out.bValid = true;
		Out.bMine = bMine;
		Out.bCached = Bool(Json, TEXT("cached"));
		Out.ServerTime = Stamp(Json, TEXT("server_time"));

		const TSharedPtr<FJsonObject> Player = Obj(Json, TEXT("player"));
		Out.PlayerId = Str(Player, TEXT("id"));
		Out.PublicId = Str(Player, TEXT("public_id"));
		Out.Name = Str(Player, TEXT("display_name"));
		Out.Color = Colour(Player, TEXT("color"));
		Out.CollegeName = Str(Player, TEXT("college_name"));
		Out.Branch = Str(Player, TEXT("branch"));
		Out.bBoardOptOut = Bool(Player, TEXT("board_opt_out"));

		const TSharedPtr<FJsonObject> Progress = Obj(Json, TEXT("progress"));
		Out.Level = FMath::Max(1, int32(Num(Progress, TEXT("level"))));
		Out.Xp = int32(Num(Progress, TEXT("xp")));
		Out.XpInto = int32(Num(Progress, TEXT("xp_into_level")));
		Out.XpForNext = int32(Num(Progress, TEXT("xp_for_next")));
		Out.LevelMax = FMath::Max(Out.Level, int32(Num(Progress, TEXT("level_max"))));
		Out.Likes = int32(Num(Progress, TEXT("likes")));

		const TSharedPtr<FJsonObject> Stats = Obj(Json, TEXT("stats"));
		Out.Matches = int32(Num(Stats, TEXT("matches")));
		Out.RatedMatches = int32(Num(Stats, TEXT("rated_matches")));
		Out.Wins = int32(Num(Stats, TEXT("wins")));
		Out.Kills = int32(Num(Stats, TEXT("kills")));
		Out.Deaths = int32(Num(Stats, TEXT("deaths")));
		Out.Headshots = int32(Num(Stats, TEXT("headshots")));
		Out.Mvps = int32(Num(Stats, TEXT("mvps")));
		Out.BestMatchKills = int32(Num(Stats, TEXT("best_match_kills")));
		Out.MatchesAsHost = int32(Num(Stats, TEXT("matches_as_host")));
		Out.DamageDealt = Num(Stats, TEXT("damage_dealt"));
		Out.WinRate = float(Num(Stats, TEXT("win_rate")));
		Out.Kd = float(Num(Stats, TEXT("kd")));
		Out.HeadshotRate = float(Num(Stats, TEXT("headshot_rate")));

		// `rank` is OMITTED (null) for a player who opted out of the public boards. Their own view
		// keeps it: opting out of a public board is not opting out of knowing your own score.
		if (const TSharedPtr<FJsonObject> Rank = Obj(Json, TEXT("rank")))
		{
			Out.bHasRank = true;
			if (const TSharedPtr<FJsonObject> Title = Obj(Rank, TEXT("title")))
			{
				Out.TitleId = Str(Title, TEXT("item_id"));
				Out.TitleName = Str(Title, TEXT("name"));
			}
			if (const TSharedPtr<FJsonObject> Best = Obj(Rank, TEXT("best_weekend")))
			{
				Out.bHasBestWeekend = true;
				Out.BestWeekendRank = int32(Num(Best, TEXT("rank")));
				Out.BestWeekendLabel = Str(Best, TEXT("label"));
				Out.BestWeekendBracket = Str(Best, TEXT("bracket"));
			}
			if (const TSharedPtr<FJsonObject> Weekend = Obj(Rank, TEXT("this_weekend")))
			{
				Out.bHasThisWeekend = true;
				Out.WeekendPoints = int32(Num(Weekend, TEXT("points")));
				Out.WeekendRank = int32(Num(Weekend, TEXT("rank")));
				Out.WeekendRatedMatches = int32(Num(Weekend, TEXT("rated_matches")));
				Out.bWeekendProvisional = Bool(Weekend, TEXT("provisional"));
				Out.WeekendEndsAt = Stamp(Weekend, TEXT("ends_at"));
			}
		}

		if (const TSharedPtr<FJsonObject> Milestones = Obj(Json, TEXT("milestones")))
		{
			Out.MilestonesEarned = int32(Num(Milestones, TEXT("earned")));
			Out.MilestonesTotal = int32(Num(Milestones, TEXT("total")));
			if (const TArray<TSharedPtr<FJsonValue>>* Values = Arr(Milestones, TEXT("items")))
			{
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					const TSharedPtr<FJsonObject> Row = Value->AsObject();
					FCampusMilestone Badge;
					Badge.Id = Str(Row, TEXT("id"));
					Badge.Name = Str(Row, TEXT("name"));
					Badge.Subtitle = Str(Row, TEXT("subtitle"));
					Badge.Glyph = Str(Row, TEXT("glyph"));
					Badge.Tier = FMath::Clamp(int32(Num(Row, TEXT("tier"))), 1, 3);
					Badge.AchievedAt = Stamp(Row, TEXT("achieved_at"));
					Badge.bBackfilled = Bool(Row, TEXT("backfilled"));
					Badge.Target = int32(Num(Row, TEXT("target")));
					Badge.Progress = int32(Num(Row, TEXT("progress")));
					// An unearned badge is the only one that carries `target`; earned ones never do.
					Badge.bEarned = Badge.Target == 0;
					Out.Milestones.Add(MoveTemp(Badge));
				}
			}
		}

		if (const TSharedPtr<FJsonObject> Showcase = Obj(Json, TEXT("showcase")))
		{
			ReadOwned(Showcase, TEXT("titles"), Out.Titles);
			ReadOwned(Showcase, TEXT("banners"), Out.Banners);
			if (const TSharedPtr<FJsonObject> Gun = Obj(Showcase, TEXT("gun")))
			{
				Out.bHasGun = true;
				Out.GunName = Str(Gun, TEXT("name"));
				Out.GunLevel = int32(Num(Gun, TEXT("level")));
				Out.GunMaxLevel = FMath::Max(1, int32(Num(Gun, TEXT("max_level"))));
				Out.GunMagazine = int32(Num(Gun, TEXT("magazine")));
				Out.GunDamage = float(Num(Gun, TEXT("damage")));
				Out.GunFireRate = float(Num(Gun, TEXT("fire_rate")));
				Out.GunAccuracy = float(Num(Gun, TEXT("accuracy")));
			}
		}

		ReadHistoryRows(Json, TEXT("recent"), Out.Recent);

		if (const TSharedPtr<FJsonObject> Relationship = Obj(Json, TEXT("relationship")))
		{
			Out.FriendState = Str(Relationship, TEXT("friend_state"));
			Out.Presence = Str(Relationship, TEXT("presence"));
			Out.bInParty = Bool(Relationship, TEXT("in_party"));
			Out.bCanLike = Bool(Relationship, TEXT("can_like"));
			Out.bCanInvite = Bool(Relationship, TEXT("can_invite"));
		}
		return Out;
	}
}

void UCampusSocial::LoadProfile(const FString& PlayerId, FProfileDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		if (Done)
		{
			Done(false, TEXT("Not signed in."), FCampusProfile());
		}
		return;
	}
	const bool bMine = PlayerId.IsEmpty() || PlayerId == TEXT("me") || PlayerId == Backend->PlayerId;
	const FString Path = bMine ? TEXT("/me/profile") : FString::Printf(TEXT("/players/%s/profile"), *PlayerId);
	TWeakObjectPtr<UCampusSocial> WeakThis(this);
	Backend->CallApi(TEXT("GET"), Path, nullptr, false,
		[WeakThis, bMine, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusSocial* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		FCampusProfile Profile;
		if (bOk)
		{
			Profile = ReadProfile(Json, bMine);
			if (Profile.ServerTime != FDateTime())
			{
				Self->ServerTime = Profile.ServerTime;
				Self->ServerReadAt = FPlatformTime::Seconds();
			}
			// The ONE place the client learns its own level. Pushed into UCampusBackend (and the save)
			// so the top bar and the squad plates read the same number the profile page shows.
			if (bMine)
			{
				Self->PublicId = Profile.PublicId;
				if (UCampusBackend* Backend2 = Self->Api())
				{
					Backend2->ApplyProgress(Profile.Level, Profile.XpInto, Profile.XpForNext, Profile.LevelMax);
				}
			}
		}
		if (Done)
		{
			Done(bOk, Error, Profile);
		}
	});
}

void UCampusSocial::LoadHistory(const FString& PlayerId, const FDateTime& Before, FProfileDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		if (Done)
		{
			Done(false, TEXT("Not signed in."), FCampusProfile());
		}
		return;
	}
	const FString Who = PlayerId.IsEmpty() ? TEXT("me") : PlayerId;
	FString Path = FString::Printf(TEXT("/players/%s/matches?limit=20"), *Who);
	if (Before != FDateTime())
	{
		Path += TEXT("&before=") + Before.ToIso8601();
	}
	Backend->CallApi(TEXT("GET"), Path, nullptr, false,
		[Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		FCampusProfile Page;
		if (bOk)
		{
			Page.bValid = true;
			ReadHistoryRows(Json, TEXT("matches"), Page.Recent);
			Page.NextBefore = Stamp(Json, TEXT("next_before"));
		}
		if (Done)
		{
			Done(bOk, Error, Page);
		}
	});
}

void UCampusSocial::LikePlayer(const FString& PlayerId, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	Backend->CallApi(TEXT("POST"), FString::Printf(TEXT("/players/%s/like"), *PlayerId), nullptr, true,
		[Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusSocial::EquipTitle(const FString& ItemId, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend)
	{
		if (Done) { Done(false, TEXT("Not ready yet.")); }
		return;
	}
	const TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	if (ItemId.IsEmpty())
	{
		Body->SetField(TEXT("item_id"), MakeShared<FJsonValueNull>());
	}
	else
	{
		Body->SetStringField(TEXT("item_id"), ItemId);
	}
	Backend->CallApi(TEXT("POST"), TEXT("/me/loadout/title"), Body, true,
		[Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

// ---------------------------------------------------------------------------------------------
// Post-match

void UCampusSocial::NoteFinishedRoom(const FString& Code)
{
	if (!Code.IsEmpty())
	{
		PendingSummary = Code;
	}
}

void UCampusSocial::LoadSummary(const FString& Code, FSummaryDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn() || Code.IsEmpty())
	{
		if (Done)
		{
			Done(false, TEXT("No match to show."), FCampusSummary());
		}
		return;
	}
	Backend->CallApi(TEXT("GET"), FString::Printf(TEXT("/rooms/%s/summary"), *Code), nullptr, false,
		[Code, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		FCampusSummary Summary;
		if (bOk)
		{
			Summary.bValid = true;
			Summary.Code = Code;
			if (const TSharedPtr<FJsonObject> Room = Obj(Json, TEXT("room")))
			{
				Summary.Map = Str(Room, TEXT("map"));
				Summary.bRated = Bool(Room, TEXT("rated"));
				Summary.RatedReason = Str(Room, TEXT("rated_reason"));
			}
			Summary.MvpPlayerId = Str(Json, TEXT("mvp_player_id"));
			// winning_team is null for a draw, so it is read defensively rather than with Num().
			double Winner = -1.0;
			if (Json.IsValid())
			{
				Json->TryGetNumberField(TEXT("winning_team"), Winner);
			}
			Summary.WinningTeam = int32(Winner);
			if (const TArray<TSharedPtr<FJsonValue>>* Values = Arr(Json, TEXT("players")))
			{
				Summary.Players.Reserve(Values->Num());
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					const TSharedPtr<FJsonObject> Row = Value->AsObject();
					FCampusSummaryRow Line;
					Line.PlayerId = Str(Row, TEXT("player_id"));
					Line.Name = Str(Row, TEXT("display_name"));
					Line.Color = Colour(Row, TEXT("color"));
					Line.FriendState = Str(Row, TEXT("friend_state"));
					Line.Team = int32(Num(Row, TEXT("team")));
					Line.Level = int32(Num(Row, TEXT("level")));
					Line.Kills = int32(Num(Row, TEXT("kills")));
					Line.Deaths = int32(Num(Row, TEXT("deaths")));
					Line.Headshots = int32(Num(Row, TEXT("headshots")));
					Line.DamageDealt = int32(Num(Row, TEXT("damage_dealt")));
					Line.Points = int32(Num(Row, TEXT("points_awarded")));
					Line.Xp = int32(Num(Row, TEXT("xp_awarded")));
					Line.bWon = Bool(Row, TEXT("won"));
					Line.bMvp = Bool(Row, TEXT("is_mvp"));
					Line.bMe = Bool(Row, TEXT("is_me"));
					Summary.Players.Add(MoveTemp(Line));
				}
			}
		}
		if (Done)
		{
			Done(bOk, Error, Summary);
		}
	});
}

#undef LOCTEXT_NAMESPACE
