#include "Campus/CampusBackend.h"

#include "Campus/CampusGameInstance.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusRazorpay.h"
#include "Campus/CampusSave.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr float HeartbeatSeconds = 30.f;

	FString PlatformName()
	{
#if PLATFORM_ANDROID
		return TEXT("android");
#elif PLATFORM_IOS
		return TEXT("ios");
#else
		return TEXT("windows");
#endif
	}

	/** 32 random bytes as base64url (no padding): the guest device secret. */
	FString NewDeviceSecret()
	{
		TArray<uint8> Bytes;
		Bytes.Reserve(32);
		for (int32 Part = 0; Part < 2; ++Part)
		{
			const FGuid Guid = FGuid::NewGuid();
			for (uint32 Word : { Guid.A, Guid.B, Guid.C, Guid.D })
			{
				for (int32 Byte = 0; Byte < 4; ++Byte)
				{
					Bytes.Add(uint8(Word >> (Byte * 8)));
				}
			}
		}
		FString Text = FBase64::Encode(Bytes, EBase64Mode::UrlSafe);
		Text.RemoveFromEnd(TEXT("="));
		Text.RemoveFromEnd(TEXT("="));
		return Text;
	}

	FString NewKey()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	}

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

	/** An ISO-8601 instant from the API. Returns a zero date when the field is missing or unparseable. */
	FDateTime Stamp(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FDateTime Out;
		const FString Text = Str(Json, Field);
		return !Text.IsEmpty() && FDateTime::ParseIso8601(*Text, Out) ? Out : FDateTime();
	}

	/** Every reward in the engagement API is the same open shape: {diamonds?, room_cards?, title?}. */
	FCampusReward ReadReward(const TSharedPtr<FJsonObject>& Json)
	{
		FCampusReward Out;
		Out.Diamonds = int32(Num(Json, TEXT("diamonds")));
		Out.RoomCards = int32(Num(Json, TEXT("room_cards")));
		Out.bTitle = Bool(Json, TEXT("title"));
		return Out;
	}

	TArray<FString> Strings(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				Out.Add(Value->AsString());
			}
		}
		return Out;
	}

	FCampusRoom ReadRoom(const TSharedPtr<FJsonObject>& Room)
	{
		FCampusRoom Out;
		Out.Code = Str(Room, TEXT("code"));
		Out.Address = Str(Room, TEXT("address"));
		// Keep the struct's 7777 when the API omits the port (an EOS room has no port at all).
		if (const int32 ReadPort = int32(Num(Room, TEXT("port"))); ReadPort > 0)
		{
			Out.Port = ReadPort;
		}
		// Newer API: one "connect" object says how to reach the host. type "eos" = P2P over the
		// internet (puid), anything else = the LAN form, which may repeat address/port in here.
		if (const TSharedPtr<FJsonObject> Connect = Obj(Room, TEXT("connect")))
		{
			Out.ConnectType = Str(Connect, TEXT("type")).ToLower();
			Out.HostPuid = Str(Connect, TEXT("puid"));
			if (const FString ConnectAddress = Str(Connect, TEXT("address")); !ConnectAddress.IsEmpty())
			{
				Out.Address = ConnectAddress;
			}
			if (const int32 ConnectPort = int32(Num(Connect, TEXT("port"))); ConnectPort > 0)
			{
				Out.Port = ConnectPort;
			}
		}
		Out.Map = Str(Room, TEXT("map"));
		Out.TeamSize = int32(Num(Room, TEXT("team_size")));
		Out.MaxPlayers = int32(Num(Room, TEXT("max_players")));
		return Out;
	}
}

void UCampusBackend::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	HeartbeatHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCampusBackend::HeartbeatTick), HeartbeatSeconds);
	// The server's last word on level / XP, so the top bar paints a true number on the first frame
	// instead of a 1 that jumps when the profile response lands.
	if (const UCampusSave* Profile = Save())
	{
		Level = FMath::Max(1, Profile->ServerLevel);
		XpIntoLevel = FMath::Max(0, Profile->ServerXpInto);
		XpForNext = FMath::Max(0, Profile->ServerXpForNext);
	}
}

void UCampusBackend::ApplyProgress(int32 InLevel, int32 InXpInto, int32 InXpForNext, int32 InLevelMax)
{
	Level = FMath::Max(1, InLevel);
	XpIntoLevel = FMath::Max(0, InXpInto);
	XpForNext = FMath::Max(0, InXpForNext);
	LevelMax = FMath::Max(Level, InLevelMax);
	if (UCampusSave* Profile = Save())
	{
		Profile->ServerLevel = Level;
		Profile->ServerXpInto = XpIntoLevel;
		Profile->ServerXpForNext = XpForNext;
		Profile->Persist();
	}
}

void UCampusBackend::CallApi(const FString& Verb, const FString& Path, TSharedPtr<FJsonObject> Body, bool bIdempotent, FJsonDone Done)
{
	Request(Verb, Path, Body, true, bIdempotent, [Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		// 2xx is the whole success test: 201 (invite created), 204 (left the squad, no body) and 200 all
		// mean the call worked, and the caller reads whatever shape it asked for out of Json.
		const bool bOk = Status >= 200 && Status < 300;
		if (Done)
		{
			Done(bOk, bOk ? FString() : ErrorText(Status, Json), Json);
		}
	});
}

void UCampusBackend::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(HeartbeatHandle);
	Super::Deinitialize();
}

UCampusSave* UCampusBackend::Save() const
{
	UCampusGameInstance* Instance = Cast<UCampusGameInstance>(GetGameInstance());
	return Instance ? Instance->GetProfile() : nullptr;
}

FString UCampusBackend::ApiBase() const
{
	return GetDefault<UCampusNetSettings>()->ApiBaseUrl();
}

bool UCampusBackend::HasSavedSession() const
{
	const UCampusSave* Profile = Save();
	return Profile && !Profile->RefreshToken.IsEmpty();
}

/** Last transport failure (reason | url), shown with the "could not reach" message. */
static FString GLastNetDetail;

FString UCampusBackend::ErrorText(int32 Status, TSharedPtr<FJsonObject> Json)
{
	if (Status == 0)
	{
		return GLastNetDetail.IsEmpty() ? FString(TEXT("Could not reach the server. Check your internet."))
			: FString::Printf(TEXT("Could not reach the server (%s)"), *GLastNetDetail);
	}
	const FString Message = Str(Obj(Json, TEXT("error")), TEXT("message"));
	return Message.IsEmpty() ? FString::Printf(TEXT("Server error (%d)."), Status) : Message;
}

// ---------------------------------------------------------------------------------------------
// HTTP

// -------------------------------------------------------------------------------------------------
// Engagement value types

FString FCampusReward::Describe() const
{
	TArray<FString> Parts;
	if (Diamonds > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d DIAMONDS"), Diamonds));
	}
	if (RoomCards > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d ROOM CARD%s"), RoomCards, RoomCards == 1 ? TEXT("") : TEXT("S")));
	}
	if (bTitle)
	{
		Parts.Add(TEXT("A TITLE"));
	}
	return Parts.Num() ? FString::Join(Parts, TEXT(" + ")) : FString();
}

int32 FCampusMissionPanel::ClaimableCount() const
{
	int32 Count = 0;
	for (const FCampusMission& Item : Items)
	{
		Count += Item.IsClaimable() ? 1 : 0;
	}
	// the set bonus is one more thing to tap
	Count += bHasBonus && BonusState == TEXT("claimable") ? 1 : 0;
	return Count;
}

FDateTime FCampusEngagement::Now() const
{
	if (ServerTime == FDateTime())
	{
		return FDateTime::UtcNow();
	}
	// The phone's wall clock is never trusted for a countdown (players change it); only the elapsed
	// time since the response landed is taken from the device.
	const double Elapsed = FMath::Max(0.0, FPlatformTime::Seconds() - ReadAt);
	return ServerTime + FTimespan::FromSeconds(Elapsed);
}

int32 FCampusEngagement::SecondsUntil(const FDateTime& When) const
{
	if (When == FDateTime())
	{
		return 0;
	}
	return FMath::Max(0, int32((When - Now()).GetTotalSeconds()));
}

int32 FCampusEngagement::TotalClaimable() const
{
	return Daily.ClaimableCount() + Weekly.ClaimableCount() + WeekendMissions.ClaimableCount();
}

void UCampusBackend::Request(const FString& Verb, const FString& Path, TSharedPtr<FJsonObject> Body, bool bAuth, bool bIdempotent, FResponse OnResponse, bool bRetried)
{
	FString BodyText;
	if (Body.IsValid())
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyText);
		FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
	}
	// one key per user action: a retry of this call (after a token refresh) reuses it
	Send(Verb, Path, BodyText, bAuth, bIdempotent ? NewKey() : FString(), MoveTemp(OnResponse), bRetried);
}

void UCampusBackend::Send(const FString& Verb, const FString& Path, const FString& BodyText, bool bAuth, const FString& IdempotencyKey, FResponse OnResponse, bool bRetried)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetURL(ApiBase() + Path);
	Http->SetVerb(Verb);
	Http->SetTimeout(10.f);
	Http->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Http->SetHeader(TEXT("ngrok-skip-browser-warning"), TEXT("1")); // tunnel interstitial off for API calls
	// Mutations always carry a JSON body ("{}" when there is nothing to send): without one, Android's HTTP stack
	// labels the empty POST as a form and the API refuses it ("Unsupported Media Type").
	if (Verb != TEXT("GET"))
	{
		Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Http->SetContentAsString(BodyText.IsEmpty() ? FString(TEXT("{}")) : BodyText);
	}
	if (bAuth && !AccessToken.IsEmpty())
	{
		Http->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	}
	if (!IdempotencyKey.IsEmpty())
	{
		Http->SetHeader(TEXT("Idempotency-Key"), IdempotencyKey);
	}
	TWeakObjectPtr<UCampusBackend> WeakThis(this);
	Http->OnProcessRequestComplete().BindLambda(
		[WeakThis, Verb, Path, BodyText, bAuth, IdempotencyKey, OnResponse, bRetried](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected)
		{
			UCampusBackend* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			TSharedPtr<FJsonObject> Json;
			if (Response.IsValid())
			{
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
			}
			// an expired access token: refresh once, then repeat the same call with the same idempotency key
			const FString Code = Str(Obj(Json, TEXT("error")), TEXT("code"));
			if (Status == 401 && bAuth && !bRetried && (Code == TEXT("TOKEN_EXPIRED") || Code == TEXT("TOKEN_MISSING")) && Self->HasSavedSession())
			{
				Self->RefreshAccess([WeakThis, Verb, Path, BodyText, bAuth, IdempotencyKey, OnResponse](bool bOk)
				{
					if (UCampusBackend* Again = WeakThis.Get())
					{
						if (bOk)
						{
							Again->Send(Verb, Path, BodyText, bAuth, IdempotencyKey, OnResponse, true);
						}
						else
						{
							OnResponse(401, nullptr);
						}
					}
				});
				return;
			}
			// 18+ gate: an account that pre-dates the rule gets 403 on every protected route until it
			// confirms. If the player has already ticked "I am 18 or older" on this phone, settle it
			// here and replay the same call with the same idempotency key - the retry is as safe as
			// the token refresh above. Otherwise raise the flag and let the lobby ask.
			if (Status == 403 && bAuth && !bRetried && Code == TEXT("AGE_CONFIRM_REQUIRED"))
			{
				if (Self->HasAdultTick() && !Self->bAdultRetryUsed)
				{
					Self->bAdultRetryUsed = true;
					Self->ConfirmAdult([WeakThis, Verb, Path, BodyText, bAuth, IdempotencyKey, OnResponse](bool bOk, const FString&)
					{
						if (UCampusBackend* Again = WeakThis.Get())
						{
							if (bOk)
							{
								Again->Send(Verb, Path, BodyText, bAuth, IdempotencyKey, OnResponse, true);
							}
							else
							{
								Again->bAgeConfirmRequired = true;
								OnResponse(403, nullptr);
							}
						}
					});
					return;
				}
				Self->bAgeConfirmRequired = true;
			}
			if (Status == 0)
			{
				// show WHY on screen (no USB log on a friend's phone): the engine's failure reason + the address tried
				const FString Reason = Request.IsValid() ? FString(LexToString(Request->GetFailureReason())) : TEXT("NoRequest");
				GLastNetDetail = FString::Printf(TEXT("%s | %s"), *Reason, Request.IsValid() ? *Request->GetURL() : TEXT("?"));
				UE_LOG(LogTemp, Warning, TEXT("[CampusNet] request failed: %s"), *GLastNetDetail);
				Self->LastError = ErrorText(0, nullptr);
			}
			OnResponse(Status, Json);
		});
	Http->ProcessRequest();
}

void UCampusBackend::RefreshAccess(TFunction<void(bool bOk)> Then)
{
	PendingRefresh.Add(MoveTemp(Then));
	if (bRefreshing)
	{
		return; // one refresh at a time: refresh tokens are single use
	}
	bRefreshing = true;
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("refresh_token"), Save() ? Save()->RefreshToken : FString());
	Request(TEXT("POST"), TEXT("/auth/refresh"), Body, false, false, [this](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		const bool bOk = Status == 200;
		if (bOk)
		{
			AcceptSession(Json);
		}
		else if (Status == 401 || Status == 403)
		{
			// the session is over (expired, revoked, banned): sign out locally
			Logout();
		}
		bRefreshing = false;
		TArray<TFunction<void(bool)>> Waiting = MoveTemp(PendingRefresh);
		for (TFunction<void(bool)>& Callback : Waiting)
		{
			Callback(bOk);
		}
	});
}

// ---------------------------------------------------------------------------------------------
// Session

void UCampusBackend::AcceptSession(TSharedPtr<FJsonObject> Json)
{
	AccessToken = Str(Json, TEXT("access_token"));
	if (UCampusSave* Profile = Save())
	{
		Profile->RefreshToken = Str(Json, TEXT("refresh_token"));
		Profile->Persist();
	}
	ReadPlayer(Obj(Json, TEXT("player")));
}

void UCampusBackend::ReadPlayer(TSharedPtr<FJsonObject> Player)
{
	PublicId = Str(Player, TEXT("public_id"));
	if (!Player.IsValid())
	{
		return;
	}
	PlayerId = Str(Player, TEXT("id"));
	PlayerName = Str(Player, TEXT("display_name"));
	Email = Str(Player, TEXT("email"));
	RollNo = Str(Player, TEXT("roll_no"));
	Branch = Str(Player, TEXT("branch"));
	College = Str(Player, TEXT("college"));
	CollegeId = Str(Player, TEXT("college_id"));
	bShowCollege = Bool(Player, TEXT("show_college"));
	bGuest = Bool(Player, TEXT("guest"));
	bAdultConfirmed = Bool(Player, TEXT("adult_confirmed"));
	if (bAdultConfirmed)
	{
		// the server agrees this account is confirmed: no dialog is owed
		bAgeConfirmRequired = false;
	}
	if (UCampusSave* Profile = Save())
	{
		Profile->PlayerName = PlayerName;
		Profile->Persist();
	}
}

void UCampusBackend::ReadWallet(TSharedPtr<FJsonObject> Wallet)
{
	if (Wallet.IsValid())
	{
		RoomCards = int32(Num(Wallet, TEXT("room_cards")));
		Diamonds = int32(Num(Wallet, TEXT("diamonds")));
	}
}

void UCampusBackend::ResumeSession(FDone Done)
{
	if (!HasSavedSession())
	{
		Done(false, TEXT("No saved session."));
		return;
	}
	RefreshAccess([this, Done](bool bOk)
	{
		if (!bOk)
		{
			Done(false, LastError.IsEmpty() ? TEXT("Please sign in again.") : LastError);
			return;
		}
		LoadMe(Done);
	});
}

void UCampusBackend::SignInGuest(FDone Done)
{
	UCampusSave* Profile = Save();
	if (!Profile)
	{
		Done(false, TEXT("No profile."));
		return;
	}
	if (Profile->DeviceId.IsEmpty())
	{
		Profile->DeviceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		Profile->DeviceSecret = NewDeviceSecret();
		Profile->Persist();
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("device_id"), Profile->DeviceId);
	Body->SetStringField(TEXT("device_secret"), Profile->DeviceSecret);
	Body->SetStringField(TEXT("platform"), PlatformName());
	AddAdultTick(Body); // guest sign-in creates an account too: it needs the 18+ tick like register/login
	Request(TEXT("POST"), TEXT("/auth/device"), Body, false, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200 && Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		AcceptSession(Json);
		LoadMe(Done);
	});
}

void UCampusBackend::Register(const FString& InEmail, const FString& Password, const FString& Name, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("email"), InEmail.TrimStartAndEnd());
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("display_name"), Name.TrimStartAndEnd());
	Body->SetStringField(TEXT("platform"), PlatformName());
	AddAdultTick(Body);
	// signed in as a guest: the bearer makes the server upgrade that guest instead of starting over
	Request(TEXT("POST"), TEXT("/auth/register"), Body, IsSignedIn(), false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200 && Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		AcceptSession(Json);
		LoadMe(Done);
	});
}

void UCampusBackend::Login(const FString& InEmail, const FString& Password, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("email"), InEmail.TrimStartAndEnd());
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("platform"), PlatformName());
	AddAdultTick(Body);
	Request(TEXT("POST"), TEXT("/auth/login"), Body, false, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		AcceptSession(Json);
		LoadMe(Done);
	});
}

void UCampusBackend::Logout()
{
	UCampusSave* Profile = Save();
	if (Profile && !Profile->RefreshToken.IsEmpty())
	{
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("refresh_token"), Profile->RefreshToken);
		Request(TEXT("POST"), TEXT("/auth/logout"), Body, false, false, [](int32, TSharedPtr<FJsonObject>) {});
		Profile->RefreshToken.Reset();
		Profile->Persist();
	}
	AccessToken.Reset();
	PlayerId.Reset();
	Guns.Reset();
	Offers.Reset();
	Packs.Reset();
}

// ---------------------------------------------------------------------------------------------
// Profile, wallet

// -------------------------------------------------------------------------------------------------
// 18+ gate. We store the tick, never a date of birth or an age band.

bool UCampusBackend::HasAdultTick() const
{
	const UCampusSave* Profile = Save();
	return Profile && Profile->bAdultConfirmed;
}

void UCampusBackend::SetAdultTick(bool bTicked)
{
	if (UCampusSave* Profile = Save(); Profile && Profile->bAdultConfirmed != bTicked)
	{
		Profile->bAdultConfirmed = bTicked;
		Profile->Persist();
	}
}

void UCampusBackend::AddAdultTick(TSharedPtr<FJsonObject> Body) const
{
	// Sent as camelCase, which is the spelling the API documents for the game client.
	if (Body.IsValid() && HasAdultTick())
	{
		Body->SetBoolField(TEXT("adultConfirmed"), true);
	}
}

void UCampusBackend::ConfirmAdult(FDone Done)
{
	SetAdultTick(true);
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetBoolField(TEXT("adultConfirmed"), true);
	Request(TEXT("POST"), TEXT("/me/confirm-adult"), Body, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		bAgeConfirmRequired = false;
		bAdultConfirmed = true;
		ReadPlayer(Obj(Json, TEXT("player")));
		Done(true, FString());
	});
}

// -------------------------------------------------------------------------------------------------
// Colleges (one college = one leaderboard) and the board-privacy toggle

void UCampusBackend::LoadColleges(FDone Done)
{
	Request(TEXT("GET"), TEXT("/colleges"), nullptr, false, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		Colleges.Reset();
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(TEXT("colleges"), List))
		{
			Colleges.Reserve(List->Num());
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusCollege& College = Colleges.AddDefaulted_GetRef();
				College.Id = Str(Item, TEXT("id"));
				College.Name = Str(Item, TEXT("name"));
				College.City = Str(Item, TEXT("city"));
			}
		}
		Done(true, FString());
	});
}

void UCampusBackend::SetCollege(const FString& InCollegeId, bool bShow, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	if (InCollegeId.IsEmpty())
	{
		Body->SetField(TEXT("college_id"), MakeShared<FJsonValueNull>());
	}
	else
	{
		Body->SetStringField(TEXT("college_id"), InCollegeId);
	}
	Body->SetBoolField(TEXT("show_college"), bShow);
	Request(TEXT("PATCH"), TEXT("/me"), Body, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadPlayer(Obj(Json, TEXT("player")));
		ReadWallet(Obj(Json, TEXT("wallet")));
		Done(true, FString());
	});
}

// -------------------------------------------------------------------------------------------------
// Engagement: streak, missions, weekend board

void UCampusBackend::ReadEngagement(TSharedPtr<FJsonObject> Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	FCampusEngagement& Out = Engagement;
	Out.bLoaded = true;
	Out.ServerTime = Stamp(Json, TEXT("server_time"));
	Out.ReadAt = FPlatformTime::Seconds();
	Out.bDailyCardAvailable = Bool(Json, TEXT("daily_card_available"));
	bDailyCardAvailable = Out.bDailyCardAvailable;

	// ---- streak
	const TSharedPtr<FJsonObject> Streak = Obj(Json, TEXT("streak"));
	Out.Streak = FCampusStreak();
	if (Streak.IsValid())
	{
		Out.Streak.CurrentLen = int32(Num(Streak, TEXT("current_len")));
		Out.Streak.BestLen = int32(Num(Streak, TEXT("best_len")));
		Out.Streak.CycleDay = int32(Num(Streak, TEXT("cycle_day")));
		Out.Streak.bClaimable = Bool(Streak, TEXT("claimable"));
		Out.Streak.TodayReward = ReadReward(Obj(Streak, TEXT("today_reward")));
		Out.Streak.ResetsAt = Stamp(Streak, TEXT("resets_at"));
		const TArray<TSharedPtr<FJsonValue>>* Ladder = nullptr;
		if (Streak->TryGetArrayField(TEXT("ladder"), Ladder))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Ladder)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusStreakStep& Step = Out.Streak.Ladder.AddDefaulted_GetRef();
				Step.Day = int32(Num(Item, TEXT("day")));
				Step.Reward = ReadReward(Obj(Item, TEXT("reward")));
				Step.State = Str(Item, TEXT("state"));
			}
		}
	}

	// ---- missions: the same shape three times
	auto ReadPanel = [](const TSharedPtr<FJsonObject>& Panel)
	{
		FCampusMissionPanel Out2;
		if (!Panel.IsValid())
		{
			return Out2;
		}
		Out2.bValid = true;
		Out2.PeriodId = Str(Panel, TEXT("period_id"));
		Out2.EndsAt = Stamp(Panel, TEXT("ends_at"));
		const TSharedPtr<FJsonObject> Bonus = Obj(Panel, TEXT("bonus"));
		if (Bonus.IsValid())
		{
			Out2.bHasBonus = true;
			Out2.BonusNeed = int32(Num(Bonus, TEXT("need")));
			Out2.BonusDone = int32(Num(Bonus, TEXT("done")));
			Out2.BonusState = Str(Bonus, TEXT("state"));
			Out2.BonusReward = ReadReward(Obj(Bonus, TEXT("reward")));
		}
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (Panel->TryGetArrayField(TEXT("items"), Items))
		{
			Out2.Items.Reserve(Items->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Items)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusMission& Mission = Out2.Items.AddDefaulted_GetRef();
				Mission.Id = Str(Item, TEXT("mission_id"));
				Mission.Title = Str(Item, TEXT("title"));
				Mission.Subtitle = Str(Item, TEXT("subtitle"));
				Mission.State = Str(Item, TEXT("state"));
				Mission.Progress = int32(Num(Item, TEXT("progress")));
				Mission.Target = FMath::Max(1, int32(Num(Item, TEXT("target"))));
				Mission.Reward = ReadReward(Obj(Item, TEXT("reward")));
			}
		}
		// The API already sorts claimable first; keep that order rather than re-sorting it here.
		return Out2;
	};
	const TSharedPtr<FJsonObject> Missions = Obj(Json, TEXT("missions"));
	Out.Daily = ReadPanel(Obj(Missions, TEXT("daily")));
	Out.Weekly = ReadPanel(Obj(Missions, TEXT("weekly")));
	Out.WeekendMissions = ReadPanel(Obj(Missions, TEXT("weekend")));

	// ---- weekend board
	Out.Board = FCampusWeekend();
	if (const TSharedPtr<FJsonObject> Weekend = Obj(Json, TEXT("weekend")); Weekend.IsValid())
	{
		FCampusWeekend& Board = Out.Board;
		Board.bValid = true;
		Board.PeriodId = Str(Weekend, TEXT("period_id"));
		Board.Label = Str(Weekend, TEXT("label"));
		Board.State = Str(Weekend, TEXT("state"));
		Board.StartsAt = Stamp(Weekend, TEXT("starts_at"));
		Board.EndsAt = Stamp(Weekend, TEXT("ends_at"));
		// Anything short of 'settled' is provisional, and the board must say so.
		Board.bProvisional = Board.State != TEXT("settled");
		Board.bDegraded = Bool(Weekend, TEXT("degraded"));
		const TArray<TSharedPtr<FJsonValue>>* Top = nullptr;
		if (Weekend->TryGetArrayField(TEXT("top"), Top))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Top)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusBoardEntry& Entry = Board.Top.AddDefaulted_GetRef();
				Entry.Rank = int32(Num(Item, TEXT("rank")));
				Entry.Name = Str(Item, TEXT("display_name"));
				Entry.College = Str(Item, TEXT("college"));
				Entry.Points = int32(Num(Item, TEXT("points")));
				Entry.Kills = int32(Num(Item, TEXT("kills")));
				Entry.bMe = Bool(Item, TEXT("is_me"));
			}
		}
		// me is null until the player has played a rated match this weekend: that is the empty state.
		if (const TSharedPtr<FJsonObject> Me = Obj(Weekend, TEXT("me")); Me.IsValid())
		{
			Board.bHasMe = true;
			Board.MyRank = int32(Num(Me, TEXT("rank")));
			Board.MyPoints = int32(Num(Me, TEXT("points")));
			Board.MyRatedMatches = int32(Num(Me, TEXT("rated_matches")));
			Board.ProjectedBracket = Str(Me, TEXT("projected_reward"));
			if (const TSharedPtr<FJsonObject> Next = Obj(Me, TEXT("next_rank")); Next.IsValid())
			{
				Board.bHasNextRank = true;
				Board.NextRank = int32(Num(Next, TEXT("rank")));
				Board.NextGap = int32(Num(Next, TEXT("gap")));
			}
			// projected_reward may arrive as an object with a bracket rather than a bare string
			if (Board.ProjectedBracket.IsEmpty())
			{
				Board.ProjectedBracket = Str(Obj(Me, TEXT("projected_reward")), TEXT("bracket"));
			}
		}
	}

	// ---- last weekend's champion card and unclaimed reward
	Out.LastWeekend = FCampusLastWeekend();
	if (const TSharedPtr<FJsonObject> Last = Obj(Json, TEXT("last_weekend")); Last.IsValid())
	{
		FCampusLastWeekend& Prev = Out.LastWeekend;
		Prev.bValid = true;
		Prev.PeriodId = Str(Last, TEXT("period_id"));
		Prev.ChampionName = Str(Obj(Last, TEXT("champion")), TEXT("display_name"));
		Prev.MyRank = int32(Num(Last, TEXT("my_rank")));
		if (const TSharedPtr<FJsonObject> Unclaimed = Obj(Last, TEXT("unclaimed_reward")); Unclaimed.IsValid())
		{
			Prev.bUnclaimed = true;
			Prev.Bracket = Str(Unclaimed, TEXT("bracket"));
			Prev.Reward = ReadReward(Unclaimed);
		}
	}
}

void UCampusBackend::LoadEngagementFromCache()
{
	const UCampusSave* Profile = Save();
	if (Engagement.bLoaded || !Profile || Profile->EngagementCache.IsEmpty())
	{
		return;
	}
	TSharedPtr<FJsonObject> Json;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Profile->EngagementCache), Json))
	{
		ReadEngagement(Json);
		// A cached response is a stale snapshot: its countdowns must keep running from the server
		// time it carried, which ReadEngagement already anchors against. Nothing else to do.
	}
}

void UCampusBackend::LoadEngagementHome(FDone Done)
{
	Request(TEXT("GET"), TEXT("/engagement/home"), nullptr, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadEngagement(Json);
		// Cache the raw body so the next launch paints the lobby before the network answers.
		if (UCampusSave* Profile = Save(); Profile && Json.IsValid())
		{
			FString Text;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
			if (FJsonSerializer::Serialize(Json.ToSharedRef(), Writer))
			{
				Profile->EngagementCache = Text;
				Profile->Persist();
			}
		}
		Done(true, FString());
	});
}

/** Every claim answers with the new wallet; taking it from the response is what keeps the counters honest. */
void UCampusBackend::ClaimMission(const FString& PeriodId, const FString& MissionId, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("period_id"), PeriodId);
	bRewardCapReached = false;
	Request(TEXT("POST"), FString::Printf(TEXT("/missions/%s/claim"), *MissionId), Body, true, true,
		[this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status != 200)
			{
				bRewardCapReached = Status == 429 && Str(Obj(Json, TEXT("error")), TEXT("code")) == TEXT("DAILY_REWARD_CAP");
				Done(false, ErrorText(Status, Json));
				return;
			}
			ReadWallet(Obj(Json, TEXT("wallet")));
			Done(true, FString());
		});
}

void UCampusBackend::ClaimStreak(FDone Done)
{
	bRewardCapReached = false;
	Request(TEXT("POST"), TEXT("/me/streak/claim"), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			bRewardCapReached = Status == 429 && Str(Obj(Json, TEXT("error")), TEXT("code")) == TEXT("DAILY_REWARD_CAP");
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadWallet(Obj(Json, TEXT("wallet")));
		// Claimed: the popup must not reappear until the ladder next advances.
		Engagement.Streak.bClaimable = false;
		Engagement.Streak.CurrentLen = int32(Num(Json, TEXT("current_len")));
		Engagement.Streak.BestLen = int32(Num(Json, TEXT("best_len")));
		Done(true, FString());
	});
}

void UCampusBackend::ClaimPeriodReward(const FString& PeriodId, FDone Done)
{
	bRewardCapReached = false;
	Request(TEXT("POST"), FString::Printf(TEXT("/rewards/%s/claim"), *PeriodId), nullptr, true, true,
		[this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status != 200)
			{
				bRewardCapReached = Status == 429 && Str(Obj(Json, TEXT("error")), TEXT("code")) == TEXT("DAILY_REWARD_CAP");
				Done(false, ErrorText(Status, Json));
				return;
			}
			ReadWallet(Obj(Json, TEXT("wallet")));
			Engagement.LastWeekend.bUnclaimed = false;
			Done(true, FString());
		});
}

FString UCampusBackend::GooglePlayProductFor(const FString& ItemId) const
{
	const FString* Found = GoogleProducts.Find(ItemId);
	return Found ? *Found : FString();
}

void UCampusBackend::LoadMe(FDone Done)
{
	Request(TEXT("GET"), TEXT("/me"), nullptr, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadPlayer(Obj(Json, TEXT("player")));
		ReadWallet(Obj(Json, TEXT("wallet")));
		bDailyCardAvailable = Bool(Json, TEXT("daily_card_available"));
		Done(true, FString());
	});
}

void UCampusBackend::UpdateProfile(const FString& Name, const FString& InRollNo, const FString& InBranch, const FString& InCollege, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("display_name"), Name.TrimStartAndEnd());
	auto Optional = [&Body](const TCHAR* Field, const FString& Value)
	{
		const FString Trimmed = Value.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			Body->SetField(Field, MakeShared<FJsonValueNull>());
		}
		else
		{
			Body->SetStringField(Field, Trimmed);
		}
	};
	Optional(TEXT("roll_no"), InRollNo);
	Optional(TEXT("branch"), InBranch);
	Optional(TEXT("college"), InCollege);
	Request(TEXT("PATCH"), TEXT("/me"), Body, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadPlayer(Obj(Json, TEXT("player")));
		ReadWallet(Obj(Json, TEXT("wallet")));
		Done(true, FString());
	});
}

void UCampusBackend::ClaimDailyCard(FDone Done)
{
	Request(TEXT("POST"), TEXT("/me/daily-card"), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			if (Str(Obj(Json, TEXT("error")), TEXT("code")) == TEXT("ALREADY_CLAIMED"))
			{
				bDailyCardAvailable = false;
			}
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadWallet(Obj(Json, TEXT("wallet")));
		bDailyCardAvailable = false;
		Done(true, FString());
	});
}

// ---------------------------------------------------------------------------------------------
// Store

const FCampusGun* UCampusBackend::FindGun(const FString& Id) const
{
	return Guns.FindByPredicate([&Id](const FCampusGun& Gun) { return Gun.Id == Id; });
}

void UCampusBackend::ReadStore(TSharedPtr<FJsonObject> Json)
{
	ReadWallet(Obj(Json, TEXT("wallet")));
	EquippedGun = Str(Json, TEXT("equipped_gun"));
	bPaymentsEnabled = Bool(Json, TEXT("payments_enabled"));
	Guns.Reset();
	Offers.Reset();
	Packs.Reset();
	GoogleProducts.Reset();
	// Play Console id per catalogue item; the store uses it to price and buy through Google Play.
	auto NoteGoogleId = [this](const TSharedPtr<FJsonObject>& Item, const FString& Id)
	{
		FString Product = Str(Item, TEXT("google_product_id"));
		if (Product.IsEmpty())
		{
			Product = Str(Item, TEXT("googleProductId"));
		}
		if (!Product.IsEmpty() && !Id.IsEmpty())
		{
			GoogleProducts.Add(Id, Product);
		}
	};
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (Json->TryGetArrayField(TEXT("guns"), List))
	{
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> Item = Value->AsObject();
			const TSharedPtr<FJsonObject> Stats = Obj(Item, TEXT("stats"));
			FCampusGun& Gun = Guns.AddDefaulted_GetRef();
			Gun.Id = Str(Item, TEXT("id"));
			Gun.Name = Str(Item, TEXT("name"));
			Gun.Description = Str(Item, TEXT("description"));
			Gun.Badge = Str(Item, TEXT("badge"));
			Gun.Perks = Strings(Item, TEXT("perks"));
			Gun.PriceDiamonds = int32(Num(Item, TEXT("price_diamonds")));
			Gun.PriceInr = float(Num(Item, TEXT("price_inr")));
			Gun.bOwned = Bool(Item, TEXT("owned"));
			Gun.bStarter = Bool(Item, TEXT("starter"));
			Gun.Level = int32(Num(Item, TEXT("level")));
			Gun.MaxLevel = int32(Num(Item, TEXT("max_level")));
			Gun.NextUpgradeDiamonds = int32(Num(Item, TEXT("next_upgrade_diamonds")));
			Gun.SellDiamonds = int32(Num(Item, TEXT("sell_diamonds")));
			Gun.Damage = float(Num(Stats, TEXT("damage")));
			Gun.FireRate = float(Num(Stats, TEXT("fire_rate")));
			Gun.Accuracy = float(Num(Stats, TEXT("accuracy")));
			Gun.Magazine = int32(Num(Stats, TEXT("mag")));
			NoteGoogleId(Item, Gun.Id);
		}
	}
	for (const TCHAR* Field : { TEXT("offers"), TEXT("packs") })
	{
		if (Json->TryGetArrayField(Field, List))
		{
			for (const TSharedPtr<FJsonValue>& Value : *List)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusOffer& Offer = (FCString::Strcmp(Field, TEXT("offers")) == 0 ? Offers : Packs).AddDefaulted_GetRef();
				Offer.Id = Str(Item, TEXT("id"));
				Offer.Name = Str(Item, TEXT("name"));
				Offer.Description = Str(Item, TEXT("description"));
				Offer.Badge = Str(Item, TEXT("badge"));
				Offer.Perks = Strings(Item, TEXT("perks"));
				Offer.PriceInr = float(Num(Item, TEXT("price_inr")));
				NoteGoogleId(Item, Offer.Id);
			}
		}
	}
}

void UCampusBackend::LoadStore(FDone Done)
{
	Request(TEXT("GET"), TEXT("/store"), nullptr, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadStore(Json);
		Done(true, FString());
	});
}

void UCampusBackend::BuyGun(const FString& Id, FDone Done)
{
	Request(TEXT("POST"), FString::Printf(TEXT("/store/items/%s/buy"), *Id), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		LoadStore(Done); // refresh owned / levels / wallet in one go
	});
}

void UCampusBackend::UpgradeGun(const FString& Id, FDone Done)
{
	Request(TEXT("POST"), FString::Printf(TEXT("/store/items/%s/upgrade"), *Id), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		LoadStore(Done);
	});
}

void UCampusBackend::SellGun(const FString& Id, FDone Done)
{
	Request(TEXT("POST"), FString::Printf(TEXT("/store/items/%s/sell"), *Id), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		LoadStore(Done);
	});
}

void UCampusBackend::EquipGun(const FString& Id, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("gun_id"), Id);
	Request(TEXT("PUT"), TEXT("/me/loadout"), Body, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		EquippedGun = Str(Json, TEXT("equipped_gun"));
		Done(true, FString());
	});
}

void UCampusBackend::BuyWithMoney(const FString& ItemId, FDone Done)
{
	if (IsWaitingForPayment())
	{
		Done(false, TEXT("A payment is already in progress."));
		return;
	}
	// Android: Razorpay's in-app payment sheet. Elsewhere: the hosted payment page in the browser.
	const bool bNative = CampusRazorpay::IsAvailable();
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("item_id"), ItemId);
	Body->SetStringField(TEXT("method"), bNative ? TEXT("checkout") : TEXT("link"));
	Request(TEXT("POST"), TEXT("/payments/orders"), Body, true, true, [this, Done, bNative](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		PendingOrderId = Str(Json, TEXT("order_id"));
		PendingOrderPolls = 0;
		PendingOrderDone = Done;
		if (bNative)
		{
			const FString OrderId = PendingOrderId;
			CampusRazorpay::Open(Str(Json, TEXT("key_id")), Str(Json, TEXT("razorpay_order_id")), int32(Num(Json, TEXT("amount_paise"))),
				TEXT("Campus Combat"), Str(Json, TEXT("description")), Email.Contains(TEXT("*")) ? FString() : Email,
				[this, OrderId](int32 Code, const FString& RzpOrderId, const FString& PaymentId, const FString& Signature, const FString& Message)
				{
					OnNativePaymentResult(OrderId, Code, RzpOrderId, PaymentId, Signature, Message);
				});
			return;
		}
		const FString PayUrl = Str(Json, TEXT("pay_url"));
		if (PayUrl.IsEmpty() || PendingOrderId.IsEmpty())
		{
			FinishPendingOrder(false, TEXT("Payment page unavailable."));
			return;
		}
		FPlatformProcess::LaunchURL(*PayUrl, nullptr, nullptr);
		if (!PaymentPollHandle.IsValid())
		{
			PaymentPollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCampusBackend::PaymentPollTick), 4.f);
		}
	});
}

void UCampusBackend::VerifyGooglePurchase(const FString& ProductId, const FString& PurchaseToken, FJsonDone Done)
{
	if (PurchaseToken.IsEmpty())
	{
		Done(false, TEXT("Missing purchase token."), nullptr);
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("productId"), ProductId);
	Body->SetStringField(TEXT("purchaseToken"), PurchaseToken);
	// Not idempotency-keyed: the purchase token is the natural key, and a retry days later from a restore must hit
	// the same server-side record rather than a fresh one.
	Request(TEXT("POST"), TEXT("/store/google/verify"), Body, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status < 200 || Status > 299)
		{
			Done(false, ErrorText(Status, Json), Json);
			return;
		}
		// Read the wallet wherever it turns up: nested under "wallet", under "player", or flat on the response.
		if (const TSharedPtr<FJsonObject> Wallet = Obj(Json, TEXT("wallet")))
		{
			ReadWallet(Wallet);
		}
		else if (const TSharedPtr<FJsonObject> Player = Obj(Json, TEXT("player")))
		{
			const TSharedPtr<FJsonObject> Nested = Obj(Player, TEXT("wallet"));
			ReadWallet(Nested.IsValid() ? Nested : Player);
		}
		else if (Json.IsValid() && (Json->HasField(TEXT("diamonds")) || Json->HasField(TEXT("room_cards"))))
		{
			ReadWallet(Json);
		}
		Done(true, FString(), Json);
	});
}

void UCampusBackend::OnNativePaymentResult(const FString& OrderId, int32 Code, const FString& RzpOrderId, const FString& PaymentId,
	const FString& Signature, const FString& Message)
{
	if (OrderId != PendingOrderId)
	{
		return; // a stale callback for an order that is already settled
	}
	if (Code == CampusRazorpay::Success)
	{
		// the server checks the signature with our secret before it credits anything
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("razorpay_order_id"), RzpOrderId);
		Body->SetStringField(TEXT("razorpay_payment_id"), PaymentId);
		Body->SetStringField(TEXT("razorpay_signature"), Signature);
		Request(TEXT("POST"), TEXT("/payments/verify"), Body, true, false, [this, OrderId](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status == 200 && Str(Json, TEXT("status")) == TEXT("paid"))
			{
				ReadWallet(Obj(Json, TEXT("wallet")));
				LoadStore([this](bool, const FString&) { FinishPendingOrder(true, FString()); });
				return;
			}
			// couldn't confirm right now (network): the server settles it with Razorpay; keep asking for a while
			StartPaymentPolling();
		});
		return;
	}
	// Not a success: cancelled, failed, or the sheet couldn't open. Never trust the phone's view of the money:
	// the server asks Razorpay whether anything was captured, then closes the order.
	FString Reason = Message;
	TSharedPtr<FJsonObject> ErrorJson;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Message), ErrorJson) && ErrorJson.IsValid())
	{
		const TSharedPtr<FJsonObject> Error = Obj(ErrorJson, TEXT("error"));
		const FString Description = Str(Error, TEXT("description"));
		const FString Why = Str(Error, TEXT("reason"));
		Reason = Why.Contains(TEXT("cancel")) || Description.Contains(TEXT("cancel")) ? FString(TEXT("cancelled")) : Description;
	}
	const bool bCancelled = Reason == TEXT("cancelled") || Message.Contains(TEXT("cancel"));
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("reason"), Reason.Left(200));
	Request(TEXT("POST"), FString::Printf(TEXT("/payments/orders/%s/cancel"), *OrderId), Body, true, false,
		[this, bCancelled, Reason](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status == 200 && Str(Json, TEXT("status")) == TEXT("paid"))
			{
				// Razorpay had captured it after all: credited
				LoadStore([this](bool, const FString&) { FinishPendingOrder(true, FString()); });
				return;
			}
			if (Status == 0)
			{
				FinishPendingOrder(false, TEXT("No connection. If money was taken, it is added automatically next time you open the Store."));
				return;
			}
			FinishPendingOrder(false, bCancelled ? FString(TEXT("Payment cancelled. No money was taken."))
				: FString::Printf(TEXT("Payment failed%s. No money was taken."), Reason.IsEmpty() ? TEXT("") : *(TEXT(": ") + Reason)));
		});
}

void UCampusBackend::CancelPendingPayment()
{
	if (PendingOrderId.IsEmpty())
	{
		return;
	}
	OnNativePaymentResult(PendingOrderId, 0, FString(), FString(), FString(), TEXT("cancelled"));
}

void UCampusBackend::StartPaymentPolling()
{
	if (!PaymentPollHandle.IsValid())
	{
		PaymentPollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UCampusBackend::PaymentPollTick), 4.f);
	}
}

bool UCampusBackend::PaymentPollTick(float)
{
	if (PendingOrderId.IsEmpty())
	{
		PaymentPollHandle.Reset();
		return false;
	}
	// ~10 minutes of checks (the payment page itself expires after 30)
	if (++PendingOrderPolls > 150)
	{
		FinishPendingOrder(false, TEXT("Payment not completed. If money was taken, it is credited automatically once Razorpay confirms."));
		return false;
	}
	Request(TEXT("GET"), FString::Printf(TEXT("/payments/orders/%s"), *PendingOrderId), nullptr, true, false, [this](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200 || PendingOrderId.IsEmpty())
		{
			return; // keep checking (Wi-Fi blips while the browser is in front)
		}
		const FString OrderStatus = Str(Json, TEXT("status"));
		if (OrderStatus == TEXT("paid"))
		{
			ReadWallet(Obj(Json, TEXT("wallet")));
			LoadStore([this](bool, const FString&) { FinishPendingOrder(true, FString()); });
		}
		else if (OrderStatus == TEXT("failed"))
		{
			FinishPendingOrder(false, TEXT("Payment failed or expired. No money was taken."));
		}
	});
	return true;
}

void UCampusBackend::FinishPendingOrder(bool bOk, const FString& Error)
{
	PendingOrderId.Reset();
	FTSTicker::GetCoreTicker().RemoveTicker(PaymentPollHandle);
	PaymentPollHandle.Reset();
	FDone Done = MoveTemp(PendingOrderDone);
	PendingOrderDone = nullptr;
	if (Done)
	{
		Done(bOk, Error);
	}
}

// ---------------------------------------------------------------------------------------------
// Rooms

void UCampusBackend::CreateRoom(const FString& Map, int32 TeamSize, int32 Rounds, int32 Minutes, const FString& EosPuid, FRoomDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("rounds"), Rounds);
	Body->SetNumberField(TEXT("round_minutes"), Minutes);
	Body->SetStringField(TEXT("map"), Map == TEXT("RealMap") ? TEXT("real") : TEXT("classic"));
	Body->SetNumberField(TEXT("team_size"), TeamSize == 1 || TeamSize == 2 ? TeamSize : 4);
	// Sent only when this phone really is EOS-logged-in: the API hands it to joiners as
	// connect:{type:"eos",puid}. Leaving it out keeps the room on the LAN form.
	if (!EosPuid.IsEmpty())
	{
		Body->SetStringField(TEXT("eos_puid"), EosPuid);
	}
	Request(TEXT("POST"), TEXT("/rooms"), Body, true, true, [this, Done, EosPuid](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 201)
		{
			Done(false, ErrorText(Status, Json), FCampusRoom());
			return;
		}
		ReadWallet(Obj(Json, TEXT("wallet")));
		HostedRoom = ReadRoom(Obj(Json, TEXT("room")));
		bHostedPlaying = false;
		HostedPlayers = 1;
		// Remembered so every heartbeat re-states it: if the room was created before this phone
		// finished its EOS login, the first heartbeat registers the puid and joiners get the EOS form.
		HostedEosPuid = EosPuid;
		Done(true, FString(), HostedRoom);
	});
}

void UCampusBackend::JoinRoom(const FString& Code, FRoomDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("code"), Code.TrimStartAndEnd().ToUpper());
	Request(TEXT("POST"), TEXT("/rooms/join"), Body, true, false, [Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json), FCampusRoom());
			return;
		}
		FCampusRoom Room = ReadRoom(Obj(Json, TEXT("room")));
		Room.Ticket = Str(Json, TEXT("ticket"));
		Done(true, FString(), Room);
	});
}

void UCampusBackend::AdmitPlayer(const FString& Ticket, TFunction<void(bool bRejected, const FString& PlayerId)> Done)
{
	if (!HostedRoom.IsValid() || Ticket.IsEmpty())
	{
		Done(HostedRoom.IsValid(), FString());
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("ticket"), Ticket);
	Request(TEXT("POST"), FString::Printf(TEXT("/rooms/%s/admit"), *HostedRoom.Code), Body, true, false, [Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		Done(Status >= 400 && Status < 500, Status == 200 ? Str(Json, TEXT("player_id")) : FString());
	});
}

void UCampusBackend::ReportMatchResult(const TArray<FCampusMatchResult>& Players)
{
	if (!HostedRoom.IsValid() || Players.IsEmpty())
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> Lines;
	for (const FCampusMatchResult& Player : Players)
	{
		TSharedPtr<FJsonObject> Line = MakeShared<FJsonObject>();
		Line->SetStringField(TEXT("player_id"), Player.PlayerId);
		Line->SetNumberField(TEXT("team"), Player.Team);
		Line->SetNumberField(TEXT("kills"), Player.Kills);
		Line->SetNumberField(TEXT("deaths"), Player.Deaths);
		Line->SetNumberField(TEXT("damage_dealt"), Player.DamageDealt);
		Line->SetNumberField(TEXT("damage_taken"), Player.DamageTaken);
		Line->SetBoolField(TEXT("won"), Player.bWon);
		Lines.Add(MakeShared<FJsonValueObject>(Line));
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetArrayField(TEXT("players"), Lines);
	// The API requires an Idempotency-Key on the result: one key per match, reused by Send() on every
	// retry (token refresh, 18+ confirm), so a replayed report can never pay the match out twice.
	// This runs once per match - the hosted room is cleared below - so the key is minted once.
	Request(TEXT("POST"), FString::Printf(TEXT("/rooms/%s/result"), *HostedRoom.Code), Body, true, true, [](int32 Status, TSharedPtr<FJsonObject>)
	{
		UE_LOG(LogTemp, Log, TEXT("Campus: match result reported (%d)"), Status);
	});
	HostedRoom = FCampusRoom(); // the server closed it; no more heartbeats
	HostedEosPuid.Reset();
}

void UCampusBackend::SetHostedRoomState(bool bPlaying, int32 PlayerCount)
{
	const bool bChanged = bPlaying != bHostedPlaying;
	bHostedPlaying = bPlaying;
	HostedPlayers = PlayerCount;
	if (bChanged)
	{
		HeartbeatTick(0.f); // 'playing' is what keeps the Room Card spent: tell the server right away
	}
}

void UCampusBackend::CloseHostedRoom()
{
	if (!HostedRoom.IsValid())
	{
		return;
	}
	Request(TEXT("DELETE"), FString::Printf(TEXT("/rooms/%s"), *HostedRoom.Code), nullptr, true, false, [this](int32, TSharedPtr<FJsonObject>)
	{
		LoadMe([](bool, const FString&) {}); // an unplayed room gives its card back
	});
	HostedRoom = FCampusRoom();
	HostedEosPuid.Reset();
}

bool UCampusBackend::HeartbeatTick(float)
{
	if (HostedRoom.IsValid() && IsSignedIn())
	{
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("status"), bHostedPlaying ? TEXT("playing") : TEXT("open"));
		Body->SetNumberField(TEXT("player_count"), HostedPlayers);
		// Re-states (or registers) how to reach this host over EOS. Harmless when it never changes.
		if (!HostedEosPuid.IsEmpty())
		{
			Body->SetStringField(TEXT("eos_puid"), HostedEosPuid);
		}
		Request(TEXT("POST"), FString::Printf(TEXT("/rooms/%s/heartbeat"), *HostedRoom.Code), Body, true, false, [this](int32 Status, TSharedPtr<FJsonObject>)
		{
			if (Status == 404)
			{
				HostedRoom = FCampusRoom(); // closed by the server (stale) or elsewhere
			}
		});
	}
	return true; // keep ticking
}
