// THE API CLIENT'S TRANSPORT (UCampusBackend), and nothing above it.
//
// WHAT LIVES HERE: the subsystem's lifetime (Initialize / Deinitialize, the resume hook and the
// host heartbeat's ticker), the one function that sends an HTTP request, and the three things every
// route in the game gets for free because they are implemented exactly once:
//
//   THE TOKEN REFRESH   a 401 refreshes the access token and REPLAYS the call with the same
//                       Idempotency-Key, so a POST that crossed an expiry is not charged twice.
//   THE 18+ REPLAY      a 403 AGE_CONFIRM_REQUIRED on an account that has already ticked the box
//                       on this phone settles it and replays, once per app run.
//   THE LATCHES         a 503 MAINTENANCE raises the lock from ANY route, so a player already in
//                       the lobby when the switch is thrown is told why; a 426 CLIENT_TOO_OLD
//                       raises the update gate even on a phone that has never read a live-config
//                       blob. Both are cleared where they can be proved over: the 503 by the next
//                       2xx or a live blob, the 426 by the next accepted sign-in (AcceptSession).
//
// AND ErrorText, which is the only place in the game a status code becomes a sentence a player
// reads. One place, so no two screens phrase the same failure differently.
//
// EVERYTHING ABOVE THE TRANSPORT IS SPLIT BY ROUTE GROUP into the CampusBackend*.cpp files beside
// this one; Campus/CampusBackendInternal.h carries the map and the JSON readers they share.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusSave.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CoreDelegates.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

using namespace CampusApi;

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
		// Same reasoning for the profile picture: the badge paints yesterday's picture out of the disk
		// cache now, and the server's word replaces it the moment GET /me lands.
		AvatarUrl = Profile->AvatarUrl;
	}
	// The published blob, from the save, BEFORE anything asks for it: the minimum build and the live
	// announcements have to be right on the first frame and with no network at all.
	LoadLiveConfigFromCache();
	// THE RESUME. The design's second trigger is a resume older than ten minutes, and the staleness
	// test lives inside FetchLiveConfig - so this is the whole of it. No timer, no poll: a phone that
	// is woken twice in a minute costs one request, not two.
	ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddLambda([this]()
	{
		FetchLiveConfig(false);
	});
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
	if (ForegroundHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
		ForegroundHandle.Reset();
	}
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

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> UCampusBackend::OpenRequest(const FString& Verb, const FString& Path) const
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetURL(ApiBase() + Path);
	Http->SetVerb(Verb);
	Http->SetTimeout(10.f);
	Http->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Http->SetHeader(TEXT("ngrok-skip-browser-warning"), TEXT("1")); // tunnel interstitial off for API calls
	return Http;
}

void UCampusBackend::Send(const FString& Verb, const FString& Path, const FString& BodyText, bool bAuth, const FString& IdempotencyKey, FResponse OnResponse, bool bRetried)
{
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = OpenRequest(Verb, Path);
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
			// MAINTENANCE. Every route but health, /config and /live-config answers 503 MAINTENANCE
			// while the owner has the game locked, so ANY call is enough to raise the lock screen -
			// a phone that was already in the lobby when the switch was thrown does not have to
			// wait for its next live-config read to be told why nothing works.
			if (Status == 503 && Code == TEXT("MAINTENANCE"))
			{
				Self->bMaintenanceFromCall = true;
				Self->CallMaintenanceMessage = ErrorText(Status, Json);
			}
			else if (Status >= 200 && Status < 300)
			{
				// The window is over: the server is answering again, so the lock lets go by itself.
				Self->bMaintenanceFromCall = false;
				Self->CallMaintenanceMessage.Reset();
			}
			// 426 CLIENT_TOO_OLD (routes/auth.js): the floor was raised past this build. The server
			// hands back the new floor, which is what lets the update screen stand even on a phone
			// that has never read a live-config blob.
			if (Status == 426 || Code == TEXT("CLIENT_TOO_OLD"))
			{
				Self->bClientTooOld = true;
				const TSharedPtr<FJsonObject> Details = Obj(Obj(Json, TEXT("error")), TEXT("details"));
				if (const int32 Floor = int32(Num(Details, TEXT("min_client_build"))); Floor > Self->LiveConfig.MinClientBuild)
				{
					Self->LiveConfig.MinClientBuild = Floor;
					// A 426 IS A LIVE ANSWER about the floor, so the number it carries is allowed to
					// gate on its own - unlike one restored from the cached blob.
					Self->LiveConfig.bMinBuildLive = true;
				}
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
	// A resumed session is a sign-in: RefreshBody takes client_build too, so a retired build is told
	// to update on app start rather than halfway through its first menu.
	AddClientBuild(Body);
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
