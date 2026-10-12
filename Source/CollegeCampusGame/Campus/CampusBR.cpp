#include "Campus/CampusBR.h"

#include "Campus/CampusBackend.h"
#include "Campus/CampusEOS.h"
#include "Campus/CampusSocial.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"

#define LOCTEXT_NAMESPACE "CampusBR"

namespace
{
	// Unity builds put several .cpp files in one translation unit, so every local helper here is
	// prefixed - a bare Str()/Num() would collide with the ones in CampusSocial.cpp.
	FString BrStr(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Out;
		return Json.IsValid() && Json->TryGetStringField(Field, Out) ? Out : FString();
	}

	double BrNum(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		double Out = 0.0;
		return Json.IsValid() && Json->TryGetNumberField(Field, Out) ? Out : 0.0;
	}

	TSharedPtr<FJsonObject> BrObj(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Json.IsValid() && Json->TryGetObjectField(Field, Out) && Out ? *Out : nullptr;
	}

	/** The fallback poll interval. Only ever used while the social socket is down - see CampusBR.h. */
	constexpr float PollSeconds = 5.f;
}

namespace CampusBR
{
	const TCHAR* LevelForMap(const FString& MapId)
	{
		// The API calls the BR map "real" (its own catalogue name); the client's catalogue id is
		// "VarsityValley". Both mean /Game/Maps/L_VarsityValley, and an empty id does too - a BR
		// match can only be on the BR map.
		if (MapId.IsEmpty() || MapId.Equals(TEXT("real"), ESearchCase::IgnoreCase)
			|| MapId.Equals(TEXT("VarsityValley"), ESearchCase::IgnoreCase))
		{
			return TEXT("/Game/Maps/L_VarsityValley");
		}
		return TEXT("/Game/Maps/L_Match");
	}

	const TCHAR* ModeForTeamSize(int32 TeamSize)
	{
		if (TeamSize <= 1)
		{
			return TEXT("solo");
		}
		return TeamSize == 2 ? TEXT("duo") : TEXT("squad");
	}

	FText TeamSizeLabel(int32 TeamSize)
	{
		if (TeamSize <= 1)
		{
			return LOCTEXT("BrSolo", "SOLO");
		}
		return TeamSize == 2 ? LOCTEXT("BrDuo", "DUO") : LOCTEXT("BrSquad", "SQUAD");
	}
}

void UCampusBRQueue::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The push side. UCampusSocial dedupes and orders the event; all we do is read the object.
	if (UCampusSocial* Social = Live())
	{
		MatchedHandle = Social->OnBRMatched.AddLambda([this](const TSharedPtr<FJsonObject>& Payload)
		{
			// The event carries the match either bare or under "match", depending on the producer.
			const TSharedPtr<FJsonObject> Inner = BrObj(Payload, TEXT("match"));
			TakeMatch(Inner.IsValid() ? Inner : Payload);
		});
	}
}

void UCampusBRQueue::Deinitialize()
{
	StopTimer();
	if (MatchedHandle.IsValid())
	{
		if (UCampusSocial* Social = Live())
		{
			Social->OnBRMatched.Remove(MatchedHandle);
		}
		MatchedHandle.Reset();
	}
	Super::Deinitialize();
}

UCampusBackend* UCampusBRQueue::Api() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
}

UCampusSocial* UCampusBRQueue::Live() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusSocial>() : nullptr;
}

bool UCampusBRQueue::AmHost() const
{
	const UCampusBackend* Backend = Api();
	// No host id at all (an older server, or a LAN-only match) = whoever is listed first hosts, and
	// that is not us: travelling to a listen server that nobody opened is the one unrecoverable case.
	return Backend && !Match.HostPlayerId.IsEmpty() && Match.HostPlayerId == Backend->PlayerId;
}

void UCampusBRQueue::Join(int32 InTeamSize, bool bNightWanted, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		LastError = TEXT("Sign in first.");
		if (Done)
		{
			Done(false, LastError);
		}
		return;
	}
	TeamSize = FMath::Clamp(InTeamSize, 1, 4);
	bNight = bNightWanted;
	Match = FCampusBRMatch();
	LastError.Reset();
	EtaSeconds = 0;
	Found = 0;
	Elapsed = 0.f;
	ShownSecond = -1;
	State = ECampusBRState::Joining;
	OnChanged.Broadcast();
	StartTimer();

	TWeakObjectPtr<UCampusBRQueue> WeakThis(this);
	auto Post = [WeakThis, Done](const FString& EosPuid)
	{
		UCampusBRQueue* Self = WeakThis.Get();
		UCampusBackend* Api = Self ? Self->Api() : nullptr;
		if (!Self || !Api)
		{
			return;
		}
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("mode"), CampusBR::ModeForTeamSize(Self->TeamSize));
		// The matchmaker needs the puid UP FRONT: if it picks this phone as the host, the match object
		// it hands every other player has to carry the address they will dial. Empty = LAN only.
		if (!EosPuid.IsEmpty())
		{
			Body->SetStringField(TEXT("eos_puid"), EosPuid);
		}
		// The server owns the lighting; this is only the player's preference riding along, and a
		// server that ignores the field simply sends day_night back and the HUD follows it.
		Body->SetStringField(TEXT("day_night"), Self->bNight ? TEXT("night") : TEXT("day"));

		Api->CallApi(TEXT("POST"), TEXT("/br/queue"), Body, true,
			[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
		{
			UCampusBRQueue* Me = WeakThis.Get();
			if (!Me)
			{
				return;
			}
			if (!bOk)
			{
				// 409 NOT_PARTY_LEADER / PARTY_TOO_BIG_FOR_MODE / BR_ALREADY_IN_MATCH and
				// 503 BR_UNAVAILABLE all land here as one sentence the overlay prints and leaves.
				Me->State = ECampusBRState::Idle;
				Me->StopTimer();
				Me->LastError = Error;
				Me->OnChanged.Broadcast();
				if (Done)
				{
					Done(false, Error);
				}
				return;
			}
			// 201 with a body, or 204 with none: either way we are queued until told otherwise.
			Me->State = ECampusBRState::Queued;
			Me->ReadQueue(Json);
			if (Done)
			{
				Done(true, FString());
			}
		});
	};

	// Log in to EOS first when it is configured, so the puid is real by the time the ticket is cut.
	// A failure is not fatal - the ticket simply goes up without one and the match stays on the LAN.
	UCampusEOS* Eos = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusEOS>() : nullptr;
	if (!Eos || !Eos->IsConfigured())
	{
		Post(FString());
		return;
	}
	TWeakObjectPtr<UCampusEOS> WeakEos(Eos);
	Eos->EnsureLogin([Post, WeakEos](bool bOk)
	{
		Post(bOk && WeakEos.IsValid() ? WeakEos->GetLocalPuid() : FString());
	});
}

void UCampusBRQueue::Leave(FDone Done)
{
	UCampusBackend* Backend = Api();
	const bool bWasQueued = IsQueued();
	// The UI must be free the instant CANCEL is tapped: the local state goes back now and the
	// DELETE is best-effort. A queue entry the server still holds is dropped by its own TTL.
	State = ECampusBRState::Idle;
	Match = FCampusBRMatch();
	Elapsed = 0.f;
	EtaSeconds = 0;
	Found = 0;
	ShownSecond = -1;
	StopTimer();
	OnChanged.Broadcast();

	if (!Backend || !bWasQueued)
	{
		if (Done)
		{
			Done(true, FString());
		}
		return;
	}
	Backend->CallApi(TEXT("DELETE"), TEXT("/br/queue"), nullptr, true,
		[Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusBRQueue::Refresh(FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || State == ECampusBRState::Idle)
	{
		if (Done)
		{
			Done(false, FString());
		}
		return;
	}
	TWeakObjectPtr<UCampusBRQueue> WeakThis(this);
	Backend->CallApi(TEXT("GET"), TEXT("/br/queue/me"), nullptr, false,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusBRQueue* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		// A failed poll is NOT a failed queue: the fallback is there because the network is unreliable,
		// so it stays queued and tries again in five seconds. Only the server saying "not queued"
		// takes the player out of the overlay.
		if (bOk)
		{
			Self->ReadQueue(Json);
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusBRQueue::ReadQueue(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		OnChanged.Broadcast();
		return;
	}
	// POST answers with {ticket:{state, eta_s, max_humans, total_slots, ...}}; GET /br/queue/me
	// answers with the same fields at the top level. Read the ticket when it is there, the body
	// otherwise, so one function covers both.
	const TSharedPtr<FJsonObject> Ticket = BrObj(Json, TEXT("ticket"));
	const TSharedPtr<FJsonObject>& Source = Ticket.IsValid() ? Ticket : Json;

	const FString Reported = BrStr(Source, TEXT("state"));
	const double Eta = BrNum(Source, TEXT("eta_s"));
	if (Eta > 0.0)
	{
		EtaSeconds = int32(Eta);
	}
	// The queue already knows how big a match is; showing 50 before the first player is found is the
	// honest shape of the thing.
	const int32 Slots = int32(BrNum(Source, TEXT("total_slots")));
	if (Slots > 0)
	{
		TotalSlots = Slots;
	}
	const int32 Party = int32(BrNum(Source, TEXT("party_size")));
	if (Party > 0)
	{
		// Until the server reports a lobby fill, "players found" is at least my own squad.
		Found = FMath::Max(Found, Party);
	}
	if (const TSharedPtr<FJsonObject> Inner = BrObj(Json, TEXT("match")))
	{
		TakeMatch(Inner);
		return;
	}
	if (Reported == TEXT("queued"))
	{
		State = ECampusBRState::Queued;
	}
	else if (!Reported.IsEmpty() && Reported != TEXT("matched"))
	{
		// idle / cancelled / expired: the server has no entry for us, so neither do we.
		State = ECampusBRState::Idle;
		StopTimer();
	}
	// Some servers report the lobby fill on the queue itself; take it when it is there.
	const int32 Humans = int32(BrNum(Json, TEXT("humans")));
	if (Humans > 0)
	{
		Found = Humans;
	}
	OnChanged.Broadcast();
}

void UCampusBRQueue::TakeMatch(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	FCampusBRMatch Next;
	Next.Code = BrStr(Json, TEXT("code"));
	Next.HostPlayerId = BrStr(Json, TEXT("host_player_id"));
	Next.TeamId = BrStr(Json, TEXT("team_id"));
	Next.Map = BrStr(Json, TEXT("map"));
	Next.bNight = BrStr(Json, TEXT("day_night")).Equals(TEXT("night"), ESearchCase::IgnoreCase);
	Next.Humans = int32(BrNum(Json, TEXT("humans")));
	Next.MaxHumans = int32(BrNum(Json, TEXT("max_humans")));
	const int32 Slots = int32(BrNum(Json, TEXT("total_slots")));
	Next.TotalSlots = Slots > 0 ? Slots : 50;
	FDateTime Starts;
	if (FDateTime::ParseIso8601(*BrStr(Json, TEXT("starts_at")), Starts))
	{
		Next.StartsAt = Starts;
	}
	if (const TSharedPtr<FJsonObject> Connect = BrObj(Json, TEXT("connect")))
	{
		Next.ConnectType = BrStr(Connect, TEXT("type"));
		Next.HostPuid = BrStr(Connect, TEXT("puid"));
		Next.Address = BrStr(Connect, TEXT("address"));
		const int32 Port = int32(BrNum(Connect, TEXT("port")));
		Next.Port = Port > 0 ? Port : 7777;
	}
	if (!Next.IsValid())
	{
		return;
	}
	const bool bNew = Next.Code != Match.Code || State != ECampusBRState::Matched;
	Match = Next;
	Found = FMath::Max(Found, Next.Humans);
	TotalSlots = Next.TotalSlots;
	State = ECampusBRState::Matched;
	StopTimer();
	OnChanged.Broadcast();
	// Exactly once per match: the socket and the fallback poll both deliver it, and travelling twice
	// would tear the level down mid-load.
	if (bNew)
	{
		OnMatched.Broadcast(Match);
	}
}

void UCampusBRQueue::StartTimer()
{
	if (TickHandle.IsValid())
	{
		return;
	}
	PollIn = PollSeconds;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UCampusBRQueue::Tick), 0.f);
}

void UCampusBRQueue::StopTimer()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

bool UCampusBRQueue::Tick(float DeltaTime)
{
	if (State != ECampusBRState::Queued && State != ECampusBRState::Joining)
	{
		return true;
	}
	Elapsed += DeltaTime;
	const int32 Second = int32(Elapsed);
	if (Second != ShownSecond)
	{
		ShownSecond = Second;
		// One repaint a second, which is what a counting-up timer actually needs.
		OnChanged.Broadcast();
	}

	// THE FALLBACK, AND ONLY THE FALLBACK. With the socket live the server pushes br.matched and this
	// never spends a request (see the metered-tunnel note in CampusBR.h).
	const UCampusSocial* Social = Live();
	const bool bSocketUp = Social && Social->IsLive();
	if (bSocketUp)
	{
		PollIn = PollSeconds;
		return true;
	}
	PollIn -= DeltaTime;
	if (PollIn <= 0.f)
	{
		PollIn = PollSeconds;
		Refresh(nullptr);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
