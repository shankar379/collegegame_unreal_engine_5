// WHAT THE ADMIN CONSOLE CAN CHANGE WITHOUT A RELEASE (UCampusBackend): the maintenance window,
// the minimum client build, and the announcement cards. ADMIN_CONSOLE_DESIGN "Game integration"
// 1 and 3; the player's side of all three is Campus/CampusLiveConfigUI.cpp.
//
// ONE PUBLIC, UNAUTHENTICATED, CONDITIONAL GET, and no timer anywhere: the tunnel is metered at
// 20,000 requests a month, so a lobby entry costs one If-None-Match request and nothing else.
//
// TWO RULES THIS FILE EXISTS TO ENFORCE:
//   1. ONLY A LIVE ANSWER MAY TAKE THE GAME AWAY. A blob read off the disk can fill the
//      announcement column and can nudge about an update, but it may not raise the maintenance
//      lock and it may not raise the UPDATE REQUIRED gate - a phone that is offline or LAN-only
//      cannot tell a floor that still stands from one the owner rolled back an hour ago.
//   2. A 304 IS A LIVE ANSWER. The server has just certified that the bytes already on disk ARE
//      the current revision, so the cache is re-read as live. That is the only thing that can
//      clear a lock some other call's 503 raised, and RETRY on the lock screen is this request.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Campus/CampusSave.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace CampusApi;

// -------------------------------------------------------------------------------------------------
// Live config: maintenance, minimum build, announcements (ADMIN_CONSOLE_DESIGN "Game integration" 1
// and 3). ONE public, unauthenticated, conditional GET. It does not go through Request()/Send():
// there is no bearer to refresh, no idempotency key to replay and no 18+ gate on this route, and it
// needs two things Send() has no business growing - a request header in and a response header out.
// What it DOES share is OpenRequest(), so the ngrok opt-out and the "//" defended base stay in one
// place. Compiled-in values and the cached blob are the fallback, in that order; nothing here can
// block, empty or delay the lobby.

int32 UCampusBackend::ClientBuild()
{
	// THE SOURCE OF TRUTH, and the only one: the APK's version code. Config/DefaultEngine.ini
	// [/Script/AndroidRuntimeSettings.AndroidRuntimeSettings] StoreVersion is what UBT stamps into the
	// manifest, what Play orders releases by, and what the server's MIN_CLIENT_BUILD has always been
	// comparing `client_build` against. Read once; the ini is cooked into the package.
	static int32 Cached = 0;
	if (Cached == 0)
	{
		int32 Value = 0;
		if (GConfig)
		{
			GConfig->GetInt(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("StoreVersion"), Value, GEngineIni);
		}
		// A build that cannot name itself is build 1, which is the floor the API tolerates.
		Cached = FMath::Max(1, Value);
	}
	return Cached;
}

FString UCampusBackend::MaintenanceText() const
{
	if (!LiveConfig.MaintenanceMessage.IsEmpty())
	{
		return LiveConfig.MaintenanceMessage;
	}
	if (!CallMaintenanceMessage.IsEmpty())
	{
		return CallMaintenanceMessage;
	}
	return TEXT("Campus Combat is down for maintenance. Please try again shortly.");
}

void UCampusBackend::ReadLiveConfig(TSharedPtr<FJsonObject> Json, bool bFromCache)
{
	if (!Json.IsValid())
	{
		return;
	}
	FCampusLiveConfig Out;
	Out.bLoaded = true;
	Out.Revision = int32(Num(Json, TEXT("revision")));
	const TSharedPtr<FJsonObject> Maintenance = Obj(Json, TEXT("maintenance"));
	// A LOCK SCREEN IS NEVER RESTORED FROM DISK (see CampusSave.h). Yesterday's blob is kept for the
	// minimum build and the announcements, but only a live answer - this fetch, or a 503 on any call -
	// may take the game away from the player.
	Out.bMaintenance = !bFromCache && Bool(Maintenance, TEXT("on"));
	// The message is kept ONLY while this blob is the thing asserting the lock. Yesterday's sentence
	// sitting next to today's 503 would make the lock screen quote the wrong window.
	Out.MaintenanceMessage = Out.bMaintenance ? Str(Maintenance, TEXT("message")) : FString();
	Out.MinClientBuild = int32(Num(Json, TEXT("min_client_build")));
	// SAME RULE AS THE LOCK, for the same reason: a version gate is a dead end with no way out, so
	// only a live answer may raise one. A floor off the disk is a card, not a wall (IsUpdateAvailable).
	// A floor a 426 already raised stays live - Send() sets bClientTooOld, which gates on its own.
	Out.bMinBuildLive = !bFromCache;
	// Whichever spelling the console publishes; both are optional and the screen works without either.
	Out.UpdateUrl = Str(Json, TEXT("update_url"));
	if (Out.UpdateUrl.IsEmpty())
	{
		Out.UpdateUrl = Str(Json, TEXT("store_url"));
	}
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Json->TryGetArrayField(TEXT("announcements"), Items))
	{
		Out.Announcements.Reserve(Items->Num());
		for (const TSharedPtr<FJsonValue>& Value : *Items)
		{
			const TSharedPtr<FJsonObject> Item = Value->AsObject();
			const FString RowId = Id(Item, TEXT("id"));
			if (RowId.IsEmpty())
			{
				continue; // nothing to remember a dismissal under: not showable
			}
			FCampusAnnouncement& Row = Out.Announcements.AddDefaulted_GetRef();
			Row.Id = RowId;
			Row.Kind = Str(Item, TEXT("kind")).ToLower();
			// CLAMPED AT PARSE, not at paint. These strings are cached in the save and re-read on
			// every launch, and the console puts no ceiling on either: one 50,000-character body
			// would be carried for ever by a card that can only show three lines of it.
			Row.Title = Str(Item, TEXT("title")).Left(AnnouncementTitleMaxChars);
			Row.Body = Str(Item, TEXT("body")).Left(AnnouncementBodyMaxChars);
			Row.StartsAt = Stamp(Item, TEXT("starts_at"));
			Row.EndsAt = Stamp(Item, TEXT("ends_at"));
		}
	}
	// Every other published key is ignored on purpose: this build can only act on ids it already
	// knows, and a key it invents a meaning for is a second source of truth waiting to drift.
	LiveConfig = MoveTemp(Out);
	if (!bFromCache && LiveConfig.MinClientBuild <= ClientBuild())
	{
		// A LIVE BLOB WHOSE FLOOR THIS BUILD MEETS IS THE SERVER SAYING THIS BUILD IS FINE, which is
		// the one thing that can answer a 426 latch without a new sign-in - and it is what RETRY on
		// the update plate actually does. An owner who rolled a bad MIN_CLIENT_BUILD back down should
		// not need every player in the field to cold-restart.
		bClientTooOld = false;
	}
	if (!bFromCache)
	{
		// A LIVE BLOB IS NOW THE ONLY AUTHORITY ON THE LOCK, whichever way it reads. /v1/live-config
		// is exempt from the 503, so this is the one answer that can clear a lock some other call's
		// 503 raised - which is what RETRY actually does. When the blob says the window is ON, the
		// lock stands on LiveConfig.bMaintenance instead, which is both truthful and quotable:
		// yesterday's sentence from CallMaintenanceMessage next to today's window was the bug.
		bMaintenanceFromCall = false;
		CallMaintenanceMessage.Reset();
	}
}

bool UCampusBackend::ReadLiveConfigFromSave(bool bFromCache)
{
	const UCampusSave* Profile = Save();
	if (!Profile || Profile->LiveConfigCache.IsEmpty())
	{
		return false;
	}
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Profile->LiveConfigCache), Json))
	{
		return false;
	}
	ReadLiveConfig(Json, bFromCache);
	return true;
}

void UCampusBackend::LoadLiveConfigFromCache()
{
	// TRUE: nothing has confirmed these bytes are current, so they may not lock or gate.
	ReadLiveConfigFromSave(true);
}

void UCampusBackend::FetchLiveConfig(bool bForce)
{
	if (bLiveConfigInFlight)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	// ONE FETCH PER APP RUN plus one per stale resume. The lobby calls this on every entry and the
	// resume hook on every wake; this test is what keeps that honest on a 20,000-a-month tunnel.
	if (!bForce && LiveConfigFetchedAt > 0.0 && Now - LiveConfigFetchedAt < LiveConfigMaxAgeSeconds)
	{
		return;
	}
	bLiveConfigInFlight = true;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = OpenRequest(TEXT("GET"), TEXT("/live-config"));
	const UCampusSave* Profile = Save();
	// The conditional request. An ETag with no blob behind it would turn a 304 into an empty screen,
	// so that pair is only sent when both halves are there.
	if (Profile && !Profile->LiveConfigEtag.IsEmpty() && !Profile->LiveConfigCache.IsEmpty())
	{
		Http->SetHeader(TEXT("If-None-Match"), Profile->LiveConfigEtag);
	}
	// The route is public, but the bearer is harmless and keeps the server's rate limiting per account.
	if (!AccessToken.IsEmpty())
	{
		Http->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
	}
	TWeakObjectPtr<UCampusBackend> WeakThis(this);
	Http->OnProcessRequestComplete().BindLambda(
		[WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			UCampusBackend* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bLiveConfigInFlight = false;
			const int32 Status = bConnected && Response.IsValid() ? Response->GetResponseCode() : 0;
			if (Status == 304)
			{
				// THE SERVER HAS JUST CERTIFIED THE CACHE. A 304 against our ETag says these exact
				// bytes ARE the current revision, which makes the blob on disk a LIVE answer and not
				// a stale one - so it is re-read with bFromCache FALSE.
				//
				// THIS IS THE ONLY WAY OUT OF A LOCK ON THE COMMON PATH. The first read of the day is
				// a cached read, which drops `maintenance.on`; the fetch that follows it is a 304
				// nine times out of ten. Treating that 304 as "nothing to do" left a lock raised by
				// some other call's 503 standing with nothing in the game able to clear it - RETRY
				// included, because RETRY is this exact request.
				Self->ReadLiveConfigFromSave(false);
				// AND BELT AND BRACES: /live-config answered, so the lock the CALL latch is asserting
				// has had its one chance to be re-confirmed by the blob. If there was no blob to read
				// at all (an ETag with nothing behind it, which the request guards against) the latch
				// would otherwise be unclearable for the rest of the app run.
				Self->bMaintenanceFromCall = false;
				Self->CallMaintenanceMessage.Reset();
				Self->LiveConfigFetchedAt = FPlatformTime::Seconds();
				Self->OnLiveConfigChanged.Broadcast();
				return;
			}
			if (Status != 200)
			{
				// A timeout, a 5xx or no network at all: keep the cached blob and the compiled-in
				// defaults, say nothing, and leave the timestamp alone so the next resume tries again.
				return;
			}
			const FString BodyText = Response->GetContentAsString();
			TSharedPtr<FJsonObject> Json;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BodyText), Json))
			{
				return;
			}
			Self->ReadLiveConfig(Json, false);
			Self->LiveConfigFetchedAt = FPlatformTime::Seconds();
			if (UCampusSave* Profile2 = Self->Save())
			{
				// CAPPED. The blob goes into the save file verbatim and the save is read synchronously
				// on every launch, so a console that publishes something enormous would make every
				// start-up slower for ever. Past the ceiling this run still uses the parsed blob and
				// simply does not keep it - and the ETag goes with it, because an ETag with no bytes
				// behind it turns the next launch's 304 into an empty screen.
				if (BodyText.Len() <= LiveConfigCacheMaxBytes)
				{
					// The body verbatim, not a re-serialised copy: the ETag the server gave belongs to
					// these exact bytes, and the next launch has to paint from them before the network.
					Profile2->LiveConfigCache = BodyText;
					Profile2->LiveConfigEtag = Response->GetHeader(TEXT("ETag"));
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[CampusNet] live-config blob is %d bytes (cap %d): used, not cached."),
						BodyText.Len(), LiveConfigCacheMaxBytes);
					Profile2->LiveConfigCache.Reset();
					Profile2->LiveConfigEtag.Reset();
				}
				Profile2->Persist();
			}
			Self->OnLiveConfigChanged.Broadcast();
		});
	Http->ProcessRequest();
}

TArray<FCampusAnnouncement> UCampusBackend::ActiveAnnouncements() const
{
	TArray<FCampusAnnouncement> Out;
	const UCampusSave* Profile = Save();
	// The API already filters the window; this is the device's own check, so a blob cached before an
	// announcement opened - or kept past the moment it closed - still shows the truth offline.
	const FDateTime Now = FDateTime::UtcNow();
	for (const FCampusAnnouncement& Row : LiveConfig.Announcements)
	{
		if (!Row.IsActiveAt(Now))
		{
			continue;
		}
		if (Profile && Profile->DismissedAnnouncements.Contains(Row.Id))
		{
			continue;
		}
		Out.Add(Row);
	}
	return Out;
}

void UCampusBackend::DismissAnnouncement(const FString& InId)
{
	if (InId.IsEmpty())
	{
		return;
	}
	if (UCampusSave* Profile = Save(); Profile && !Profile->DismissedAnnouncements.Contains(InId))
	{
		Profile->DismissedAnnouncements.Add(InId);
		// CAPPED, OLDEST FIRST OUT. The ids are the server's own sequence numbers and only ever climb,
		// so the oldest dismissal is also the one with the least chance of ever being shown again -
		// while an unbounded list is a save file that grows for the life of the account.
		const int32 Excess = Profile->DismissedAnnouncements.Num() - MaxDismissedAnnouncements;
		if (Excess > 0)
		{
			Profile->DismissedAnnouncements.RemoveAt(0, Excess);
		}
		Profile->Persist();
	}
}
