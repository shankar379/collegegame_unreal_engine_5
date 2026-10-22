// The game's client for the Campus Combat API (campuscombat-backend). One per game instance, so it survives level
// travel. Holds the session (access token in memory, refresh token in the save), caches the profile, wallet and
// store, and keeps a hosted room alive with heartbeats. No gameplay code talks HTTP directly; it calls this.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "CampusBackend.generated.h"

class FJsonObject;
class UCampusSave;

/** A gun in the store (text only until there are icons). */
struct FCampusGun
{
	FString Id;
	FString Name;
	FString Description;
	FString Badge;
	TArray<FString> Perks;
	/** `image_key`: a `content` key for the tile picture the console uploaded. Empty on a server (or
	 *  an item) with no art published, and the drawn silhouette is then what the tile shows. */
	FString ImageKey;
	int32 PriceDiamonds = 0;   // 0 = not sold for Diamonds
	float PriceInr = 0.f;      // 0 = not sold for money
	bool bOwned = false;
	bool bStarter = false;
	int32 Level = 0;
	int32 MaxLevel = 1;
	int32 NextUpgradeDiamonds = 0;   // 0 = maxed / not owned
	int32 SellDiamonds = 0;
	float Damage = 0.f;
	float FireRate = 0.f;
	float Accuracy = 0.f;
	int32 Magazine = 0;
};

/**
 * One owned cosmetic, from GET /v1/me/cosmetics `owned[]`. Deliberately a THIN row: everything that
 * decides how the thing looks lives in CampusCosmetics.h, keyed by Id, so the server never has to
 * ship art, colours or anchor rules and a new row there needs no API change. Name and Rarity are
 * kept only so an item this build's catalogue has not heard of can still be listed honestly.
 *
 * NOT READ, DELIBERATELY: the response's `art` (a placeholder path by the API's own account), and the
 * top-level `slots[]` / `anchors[]` lists. The client catalogue is the authority on every one of
 * those, and mirroring them here would be a second copy that can only ever drift.
 */
struct FCampusCosmetic
{
	FString Id;
	FString Name;
	/** "outfit" | "decor". */
	FString Kind;
	/** Which equipment slot it fills ("outfit"); decor items carry none. */
	FString Slot;
	FString Rarity;
	/** Gun skins only (backend migration 032): the gun id this skin fits. */
	FString ForGun;
};

/** A gun skin for sale (backend migration 032): a cosmetic that paints every gun of one type. */
struct FCampusGunSkinItem
{
	FString Id;
	FString Name;
	/** The gun id it fits. */
	FString ForGun;
	FString Rarity;
	/** What it costs right now (a running sale already applied). */
	int32 PriceDiamonds = 0;
	bool bOwned = false;
};

/** A Diamond pack (bought with money) or an offer (bought with Diamonds since backend migration 030). */
struct FCampusOffer
{
	FString Id;
	FString Name;
	FString Description;
	FString Badge;
	TArray<FString> Perks;
	/** `image_key`, as on FCampusGun: the console's own picture for this pack or bundle. */
	FString ImageKey;
	float PriceInr = 0.f;
	/** Offers: what they cost in Diamonds (the sale price when one is running). 0 on a Diamond pack. */
	int32 PriceDiamonds = 0;
};

struct FCampusRoom
{
	FString Code;
	FString Address;
	int32 Port = 7777;
	FString Map;
	int32 TeamSize = 4;
	int32 MaxPlayers = 8;
	/** Join only: proof for the match server that the API let this player in (short-lived). */
	FString Ticket;
	/** How to reach the host, from the API's "connect" object: "eos" (P2P, works over the internet)
	 *  or empty / anything else (the Address:Port LAN form). Old responses have no connect object. */
	FString ConnectType;
	/** The host phone's EOS ProductUserId, when ConnectType is "eos". */
	FString HostPuid;
	bool IsValid() const { return !Code.IsEmpty(); }
	/** Travel to this room over EOS P2P rather than ip:port. */
	bool IsEos() const { return ConnectType == TEXT("eos") && !HostPuid.IsEmpty(); }
};

/** One player's final line, reported by the match server (never by the player's own phone). */
struct FCampusMatchResult
{
	FString PlayerId;
	int32 Team = 0;
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 DamageDealt = 0;
	int32 DamageTaken = 0;
	bool bWon = false;
};

/** A college from the curated list (GET /colleges). One college = one leaderboard. */
struct FCampusCollege
{
	FString Id;
	FString Name;
	FString City;
};

// -------------------------------------------------------------------------------------------------
// Engagement (GET /engagement/home): streak + missions + weekend board, one round trip on lobby entry.
// Every reward in the API is an open object {diamonds?, room_cards?, title?}; this is the whole of it.

struct FCampusReward
{
	int32 Diamonds = 0;
	int32 RoomCards = 0;
	bool bTitle = false;
	/** migration 031: what missions pay now (XP + Campus Pass points), and the items a pass level gives */
	int32 Xp = 0;
	int32 PassPoints = 0;
	TArray<FString> Items;

	bool IsEmpty() const { return Diamonds == 0 && RoomCards == 0 && !bTitle && Xp == 0 && PassPoints == 0 && Items.Num() == 0; }
	/** "50 DIAMONDS + 1 CARD" - already uppercase for the display face. */
	FString Describe() const;
};

/** One level of the CAMPUS PASS: a FREE and a PREMIUM reward. State: claimable | claimed | locked | locked_premium */
struct FCampusPassLevel
{
	int32 Level = 0;
	FCampusReward Free;
	FCampusReward Premium;
	FString FreeState;
	FString PremiumState;
};

/** GET /v1/pass: the running season and this player's place in it (migration 031). */
struct FCampusPass
{
	bool bValid = false;      // a season is running and the server answered
	FString SeasonName;
	FDateTime EndsAt;
	int32 MaxLevel = 30;
	int32 PointsPerLevel = 100;
	int32 PremiumPrice = 499;
	int32 Points = 0;
	int32 Level = 0;
	int32 PointsIntoLevel = 0;
	bool bPremium = false;
	int32 Claimable = 0;
	TArray<FCampusPassLevel> Levels;

	float LevelFraction() const { return PointsPerLevel > 0 ? FMath::Clamp(float(PointsIntoLevel) / float(PointsPerLevel), 0.f, 1.f) : 0.f; }
};

struct FCampusMission
{
	FString Id;
	FString Title;
	FString Subtitle;
	/** claimable | in_progress | claimed | expired */
	FString State;
	int32 Progress = 0;
	int32 Target = 1;
	FCampusReward Reward;

	bool IsClaimable() const { return State == TEXT("claimable"); }
	float Fraction() const { return Target > 0 ? FMath::Clamp(float(Progress) / float(Target), 0.f, 1.f) : 0.f; }
};

/** One of the DAILY / WEEKLY / WEEKEND panels. */
struct FCampusMissionPanel
{
	bool bValid = false;
	FString PeriodId;
	FDateTime EndsAt = FDateTime();
	TArray<FCampusMission> Items;
	/** The "N of M" set bonus, when the period has one. */
	bool bHasBonus = false;
	int32 BonusNeed = 0;
	int32 BonusDone = 0;
	FString BonusState;
	FCampusReward BonusReward;

	/** Missions ready to claim right now - the number on the menu tab's dot. */
	int32 ClaimableCount() const;
};

struct FCampusStreakStep
{
	int32 Day = 0;
	FCampusReward Reward;
	/** claimed | claimable | locked */
	FString State;
};

struct FCampusStreak
{
	int32 CurrentLen = 0;
	int32 BestLen = 0;
	int32 CycleDay = 0;
	bool bClaimable = false;
	FCampusReward TodayReward;
	TArray<FCampusStreakStep> Ladder;
	FDateTime ResetsAt = FDateTime();
};

struct FCampusBoardEntry
{
	int32 Rank = 0;
	FString Name;
	FString College;
	int32 Points = 0;
	int32 Kills = 0;
	bool bMe = false;
};

/** The weekend board panel: the live board during a weekend, the upcoming one mid-week. */
struct FCampusWeekend
{
	bool bValid = false;
	FString PeriodId;
	FString Label;
	/** upcoming | live | ended | settled */
	FString State;
	FDateTime StartsAt = FDateTime();
	FDateTime EndsAt = FDateTime();
	/** True until the weekend settles: the board MUST say so (eventual consistency + anti-cheat hold). */
	bool bProvisional = false;
	bool bDegraded = false;
	TArray<FCampusBoardEntry> Top;

	/** null in the API when the player has not played a rated match this weekend. */
	bool bHasMe = false;
	int32 MyRank = 0;
	int32 MyPoints = 0;
	int32 MyRatedMatches = 0;
	bool bHasNextRank = false;
	int32 NextRank = 0;
	int32 NextGap = 0;
	FString ProjectedBracket;

	bool IsUpcoming() const { return State == TEXT("upcoming"); }
};

struct FCampusLastWeekend
{
	bool bValid = false;
	FString PeriodId;
	FString ChampionName;
	int32 MyRank = 0;
	bool bUnclaimed = false;
	FString Bracket;
	FCampusReward Reward;
};

struct FCampusEngagement
{
	bool bLoaded = false;
	/** Every countdown is measured from the server clock, never from the phone's. */
	FDateTime ServerTime = FDateTime();
	/** FPlatformTime::Seconds() when ServerTime was read, so the clock can run on locally. */
	double ReadAt = 0.0;
	bool bDailyCardAvailable = false;

	FCampusStreak Streak;
	FCampusMissionPanel Daily;
	FCampusMissionPanel Weekly;
	FCampusMissionPanel WeekendMissions;
	FCampusWeekend Board;
	FCampusLastWeekend LastWeekend;

	/** Server "now", advanced by the time since the response landed. */
	FDateTime Now() const;
	/** Seconds until When by the server clock; 0 once it has passed. */
	int32 SecondsUntil(const FDateTime& When) const;
	int32 TotalClaimable() const;
};

// -------------------------------------------------------------------------------------------------
// Live config (GET /v1/live-config, admin console phase A): ONE published blob, one revision, one
// strong ETag. It can only enable, disable, order, price, schedule and relabel ids this build already
// knows - every mesh, tint and stat curve still needs a new APK - so an unknown key here is ignored
// rather than interpreted, and the compiled-in values stay the fallback.

/** One row of `announcements[]`: the lobby card, the news item, the maintenance line. */
struct FCampusAnnouncement
{
	/** The server's numeric id as text; the key the dismissal is remembered under. */
	FString Id;
	/** news | notice | maintenance. Anything else is shown as a plain card rather than dropped. */
	FString Kind;
	FString Title;
	FString Body;
	/** Zero = no edge. The API already filters the window; this is the second check, on the device. */
	FDateTime StartsAt = FDateTime();
	FDateTime EndsAt = FDateTime();

	bool IsActiveAt(const FDateTime& Now) const
	{
		return (StartsAt == FDateTime() || StartsAt <= Now) && (EndsAt == FDateTime() || EndsAt > Now);
	}
};

// ---- the PHASE B published keys. Three more blobs ride in the same answer, and each one can only
//      point at, enable, order or schedule something this build already knows how to draw.

/** Blob key `content`: which version of each uploaded picture is current. The bytes live at
 *  <base_url>/<key>?v=<version> and are immutable, so a version IS a cache key - see
 *  Campus/CampusRemoteImage.h, which is the only reader. */
struct FCampusContentIndex
{
	bool bLoaded = false;
	/** "/v1/content" as published; origin-relative, resolved by CampusRemoteImage::ResolveUrl. */
	FString BaseUrl;
	TMap<FString, int32> Versions;
};

/** One row of an event's `rewards[]`. The CONDITION is counted by the server; the client only draws
 *  the progress GET /v1/events/:id/progress hands back, and never decides eligibility itself. */
struct FCampusEventReward
{
	/** diamonds | room_cards | item */
	FString Kind;
	/** Set only when Kind is "item". */
	FString ItemId;
	int32 Amount = 0;
	/** A MAP wire name (classic | campus | real) narrowing the count, or empty for any rated match.
	 *  `rooms.map` is the only per-match discriminator the schema records (routes/adminevents.js). */
	FString Mode;
	int32 Matches = 0;

	/** "50 DIAMONDS" / "1 ROOM CARD" / an item id, already uppercase for the display face. */
	FString Describe() const;
};

/** One row of blob key `events`. Every entry carries its own window, because the blob is cached: the
 *  CLIENT decides "running now" versus "starts Friday" from its own clock. */
struct FCampusEvent
{
	int32 Id = 0;
	FString Label;
	/** scheduled | live, as the console last published it. The WINDOW is what the client trusts. */
	FString State;
	FDateTime StartsAt = FDateTime();
	FDateTime EndsAt = FDateTime();
	/** A `content` key for the banner, empty when the event ships without art. */
	FString BannerKey;
	/** 1.0 when the console published no multiplier, which is why neither field is optional here. */
	float XpMultiplier = 1.f;
	float PointsMultiplier = 1.f;
	TArray<FCampusEventReward> Rewards;

	bool IsLiveAt(const FDateTime& Now) const { return StartsAt <= Now && EndsAt > Now; }
	bool HasMultiplier() const { return XpMultiplier != 1.f || PointsMultiplier != 1.f; }
};

struct FCampusEvents
{
	bool bLoaded = false;
	/** How far ahead the console asks the game to show an upcoming event. Advice, not a filter. */
	int32 HorizonDays = 7;
	TArray<FCampusEvent> Events;
};

/** GET /v1/events/:id/progress, one row per reward. `claimable` is the SERVER's answer, so the
 *  button is never enabled for a claim the API would refuse. */
struct FCampusEventRewardProgress
{
	int32 Index = 0;
	FString Kind;
	FString ItemId;
	int32 Amount = 0;
	FString Mode;
	int32 Progress = 0;
	int32 Target = 0;
	bool bClaimed = false;
	bool bClaimable = false;

	float Fraction() const { return Target > 0 ? FMath::Clamp(float(Progress) / float(Target), 0.f, 1.f) : 1.f; }
	FString Describe() const;
};

struct FCampusEventProgress
{
	bool bValid = false;
	int32 EventId = 0;
	FString Label;
	FString State;
	FString BannerKey;
	FDateTime EndsAt = FDateTime();
	TArray<FCampusEventRewardProgress> Rewards;
};

/** One row of blob key `modes`. Ids are the ones CampusCatalog already knows; anything else is
 *  ignored, because this build can only offer a mode it has code for. */
struct FCampusModeRow
{
	FString Id;
	bool bEnabled = true;
	int32 Sort = 0;
	TArray<int32> TeamSizes;
	/** MAP WIRE NAMES (classic | campus | real), not client ids. */
	TArray<FString> MapIds;
	int32 MaxPlayers = 0;
};

/** One row of `modes.maps[]`. `client_id` is the bridge to CampusCatalog::FMapEntry::Id. */
struct FCampusMapRow
{
	/** The wire name: classic | campus | real. */
	FString Id;
	/** Classic | CampusMap | VarsityValley - what this build calls the same map. */
	FString ClientId;
	FString Name;
	bool bEnabled = true;
	int32 Sort = 0;
	int32 MaxPlayers = 0;
};

/** One row of `modes.br_schedule[]`: an IST WALL-CLOCK rule with no date, which is why the times are
 *  carried as "HH:MM" and evaluated on the device rather than stored as an instant. */
struct FCampusBrWindow
{
	/** ISO weekday numbers, 1 = Monday. A window that wraps past midnight names the day it STARTS. */
	TArray<int32> Days;
	FString StartIst;
	FString EndIst;
	/** day | night | auto - the lighting the server will hand out; the card's chip is a preference. */
	FString TimeOfDay;
	bool bEnabled = false;
	FString Note;

	/** Minutes since midnight for StartIst / EndIst, or INDEX_NONE when the string is unreadable. */
	int32 StartMinutes() const;
	int32 EndMinutes() const;
	/** True when IstNow falls inside this window, midnight wrap included. */
	bool IsOpenAt(const FDateTime& IstNow) const;
	/** "FRI-SUN 21:00-23:30", for the card that has to say when. */
	FString Describe() const;
};

struct FCampusModes
{
	bool bLoaded = false;
	/** "Asia/Kolkata". Carried so the card can say IST rather than assuming it. */
	FString Tz;
	TArray<FCampusModeRow> Modes;
	TArray<FCampusMapRow> Maps;
	TArray<FCampusBrWindow> BrSchedule;
};

/** The published blob, as this build reads it. Unknown keys are left alone, deliberately. */
struct FCampusLiveConfig
{
	/** A blob has been read, from the network or from the save. */
	bool bLoaded = false;
	int32 Revision = 0;
	/** The owner has locked the game in the admin console. NEVER restored from the save - see
	 *  CampusSave.h: a stale cache must not be able to invent a lock screen. */
	bool bMaintenance = false;
	FString MaintenanceMessage;
	/** A build below this must update before it can play. 0 / absent = no floor. */
	int32 MinClientBuild = 0;
	/** THIS FLOOR CAME FROM A LIVE ANSWER (a /live-config 200 or 304, or a 426 on any call) and not
	 *  from the blob cached on disk. Only a live floor may put up the dead-end UPDATE REQUIRED gate:
	 *  a phone that is offline, or on a LAN with no route to the API, would otherwise be locked out of
	 *  its own game by yesterday's number with no way to prove the number had moved on. A cached
	 *  floor shows a dismissible "update available" card instead. */
	bool bMinBuildLive = false;
	/** Where the update lives, when the console has published one (`update_url` / `store_url`). */
	FString UpdateUrl;
	TArray<FCampusAnnouncement> Announcements;
	/** PHASE B's three keys, read as part of the blob so they are replaced atomically with it. Each
	 *  stays empty until the console publishes it, and the compiled-in values are the fallback
	 *  throughout: an absent `modes` key means CampusCatalog decides, exactly as it did before. */
	FCampusContentIndex Content;
	FCampusEvents Events;
	FCampusModes Modes;
};

UCLASS()
class UCampusBackend : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FDone = TFunction<void(bool bOk, const FString& Error)>;
	using FRoomDone = TFunction<void(bool bOk, const FString& Error, const FCampusRoom& Room)>;
	/** Like FDone, but also hands back the raw response body for callers that need more than ok/error. */
	using FJsonDone = TFunction<void(bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- session
	bool IsSignedIn() const { return !PlayerId.IsEmpty(); }
	bool IsWaitingForPayment() const { return !PendingOrderId.IsEmpty(); }
	/** The player gave up on an open payment: the server checks Razorpay (paid after all -> credited), else closes it. */
	void CancelPendingPayment();
	bool HasSavedSession() const;
	/** Signs back in with the saved refresh token (app restart). */
	void ResumeSession(FDone Done);
	/** "Play as Guest": this device's own id + secret, created on first use. */
	void SignInGuest(FDone Done);
	/** New email account; a signed-in guest is upgraded in place (keeps Room Cards, Diamonds, guns). */
	void Register(const FString& Email, const FString& Password, const FString& Name, FDone Done);
	void Login(const FString& Email, const FString& Password, FDone Done);
	void Logout();

	// ---- 18+ gate (migration 009). The game is 18 and over only; we store no date of birth, only the tick.
	/** The "I am 18 or older" tick from the sign-in screen. Remembered locally, and sent with every sign-in. */
	bool HasAdultTick() const;
	void SetAdultTick(bool bTicked);
	/** True once a protected route answered 403 AGE_CONFIRM_REQUIRED and we could not settle it ourselves:
	 *  the lobby shows its one-time confirm dialog. Cleared by ConfirmAdult(). */
	bool NeedsAdultConfirm() const { return bAgeConfirmRequired; }
	/** POST /me/confirm-adult. The one route an unconfirmed account may call. */
	void ConfirmAdult(FDone Done);

	// ---- profile + wallet
	void LoadMe(FDone Done);
	void UpdateProfile(const FString& Name, const FString& RollNo, const FString& Branch, const FString& College, FDone Done);
	/** The curated college list and the board-privacy toggle (PATCH /me {college_id, show_college}). */
	void LoadColleges(FDone Done);
	void SetCollege(const FString& CollegeId, bool bShow, FDone Done);
	void ClaimDailyCard(FDone Done);

	// ---- engagement (streak, missions, weekend board)
	/** GET /engagement/home: one call on lobby entry. The response is cached in the save so the next launch
	 *  paints instantly; Done fires again with the fresh copy. */
	void LoadEngagementHome(FDone Done);
	/** Re-reads the cached response from the save, for an instant first paint. Safe before any network call. */
	void LoadEngagementFromCache();
	void ClaimMission(const FString& PeriodId, const FString& MissionId, FDone Done);

	// ---- CAMPUS PASS (migration 031) ----
	/** GET /pass -> Pass */
	void LoadPass(FDone Done);
	/** POST /pass/premium: the season's Premium pass, paid in Diamonds */
	void BuyPassPremium(FDone Done);
	/** POST /pass/claim {level, track}: one level's reward on one track ("free" / "premium") */
	void ClaimPass(int32 PassLevel, const FString& Track, FDone Done);
	FCampusPass Pass;
	void ReadPass(TSharedPtr<FJsonObject> Json);
	void ClaimStreak(FDone Done);
	/** A settled weekend's reward (last_weekend.unclaimed_reward). */
	void ClaimPeriodReward(const FString& PeriodId, FDone Done);

	// ---- live config, maintenance, minimum version, announcements (ADMIN_CONSOLE_DESIGN "Game
	//      integration" 1 and 3). ONE call, on lobby entry and after a resume older than ten minutes.
	//      There is no poll and no timer anywhere in here: the tunnel is metered at 20,000 requests a
	//      month, so a lobby entry must cost a conditional request and nothing else.
	/** GET /v1/live-config with If-None-Match from the save. Does nothing when a fetch is already in
	 *  flight, or when the last one landed less than ten minutes ago and bForce is false (lobby entry
	 *  and resume both go through here). A 304, a timeout or a 5xx all leave the cached blob standing:
	 *  this call can never block or empty the lobby. */
	void FetchLiveConfig(bool bForce = false);
	/** Re-reads the blob cached in the save, for the first frame and for offline. `maintenance.on` is
	 *  dropped on the way in, by design. Safe before any network call. */
	void LoadLiveConfigFromCache();
	const FCampusLiveConfig& GetLiveConfig() const { return LiveConfig; }
	/** A fetch is in the air, so the lock screen's RETRY can say so instead of looking dead. */
	bool IsLiveConfigFetching() const { return bLiveConfigInFlight; }
	/** Fires when a 200 or a 304 lands; the lobby repaints its cards and the gate re-reads itself. */
	FSimpleMulticastDelegate OnLiveConfigChanged;

	/** The lobby and the menus are locked: either the published blob says maintenance is on, or a call
	 *  came back 503 MAINTENANCE. A match already in progress is NEVER gated by this - the lock lives
	 *  in the lobby widget, which does not exist on the match level. */
	bool IsMaintenanceOn() const { return LiveConfig.bMaintenance || bMaintenanceFromCall; }
	/** The server's own sentence, so the lock screen says why rather than guessing. */
	FString MaintenanceText() const;

	/** THE HARD GATE: this build is below a floor a LIVE answer published, or a call answered 426
	 *  CLIENT_TOO_OLD. A floor read off the cached blob deliberately does NOT count - see
	 *  FCampusLiveConfig::bMinBuildLive. A dead end with no RETRY that an offline phone can fall into
	 *  on launch is indistinguishable, to the player, from the game being broken. */
	bool IsUpdateRequired() const { return bClientTooOld || (LiveConfig.bMinBuildLive && MinBuildFloor() > ClientBuild()); }
	/** THE SOFT NUDGE: a floor we only know about from the cached blob says this build is behind. The
	 *  lobby shows a dismissible card; nothing is taken away, because nothing has been confirmed. */
	bool IsUpdateAvailable() const { return !IsUpdateRequired() && MinBuildFloor() > ClientBuild(); }
	/** The published APK / store link, empty when the console has not set one. */
	const FString& UpdateUrl() const { return LiveConfig.UpdateUrl; }
	/** THE BUILD NUMBER, and the only definition of it in the game: the APK's version code, read from
	 *  Config/DefaultEngine.ini [/Script/AndroidRuntimeSettings.AndroidRuntimeSettings] StoreVersion.
	 *  It is what Play compares, what every sign-in sends as `client_build`, and what the server's
	 *  MIN_CLIENT_BUILD check has always been comparing against. Raise StoreVersion, nothing else. */
	static int32 ClientBuild();

	/** The announcements that are live by the device clock and not dismissed, in the server's order. */
	TArray<FCampusAnnouncement> ActiveAnnouncements() const;
	/** X on a card: remembered in the save, so it never comes back on this phone. */
	void DismissAnnouncement(const FString& Id);

	// ---- content (blob key `content`, Campus/CampusBackendContent.cpp). The console's uploaded art:
	//      the only reader is Campus/CampusRemoteImage.h, which never fetches a key this does not name.
	const FCampusContentIndex& GetContent() const { return LiveConfig.Content; }
	/** The published version of a content key, or 0 when the blob does not name it. */
	int32 ContentVersionOf(const FString& Key) const;
	/** The absolute URL for <key> at its published version, or empty when there is no such key. */
	FString ContentUrlFor(const FString& Key) const;

	// ---- live events (blob key `events` + the two player routes, Campus/CampusBackendEvents.cpp)
	const FCampusEvents& GetEvents() const { return LiveConfig.Events; }
	/** The event running RIGHT NOW by the server clock, or null. The first one when several overlap:
	 *  the blob is ordered by starts_at, so that is the one that has been running longest. */
	const FCampusEvent* LiveEvent() const;
	/** What the lobby may show: the live event first, then anything starting inside the published
	 *  horizon. Filtered on the CLIENT, because the blob is cached and cannot depend on now(). */
	TArray<FCampusEvent> VisibleEvents() const;
	const FCampusEvent* FindEvent(int32 EventId) const;
	using FEventProgressDone = TFunction<void(bool bOk, const FString& Error, const FCampusEventProgress& Progress)>;
	/** GET /v1/events/:id/progress: the window, the rewards, and this player's progress against each. */
	void LoadEventProgress(int32 EventId, FEventProgressDone Done);
	/** POST /v1/events/:id/claim {reward_index} with an Idempotency-Key. The new wallet comes back on
	 *  the response and is read from it, never added up here. */
	void ClaimEventReward(int32 EventId, int32 RewardIndex, FDone Done);

	// ---- modes, maps and the battle-royale schedule (blob key `modes`,
	//      Campus/CampusBackendModes.cpp). THE BLOB MAY ONLY TAKE AWAY OR REORDER what this build
	//      already has: an id it does not know is ignored, and a key it never published is CampusCatalog.
	const FCampusModes& GetModes() const { return LiveConfig.Modes; }
	/** False only when the blob names this mode AND says it is off. No blob = the compiled answer. */
	bool IsModeEnabled(const FString& ModeId) const;
	/** The same, by the CLIENT map id (Classic / CampusMap / VarsityValley). */
	bool IsMapEnabled(const FString& ClientMapId) const;
	/** The console's running order, or INDEX_NONE when the blob says nothing about this id. */
	int32 ModeSortOf(const FString& ModeId) const;
	int32 MapSortOf(const FString& ClientMapId) const;
	/** True when a battle-royale window is open at this instant, and true when no schedule has been
	 *  published at all: an empty schedule is "no restriction", never "closed for ever". */
	bool IsBattleRoyaleOpenNow() const;
	/** "FRI-SUN 21:00-23:30 IST", or empty when nothing is scheduled. For the card that says when. */
	FString BattleRoyaleWindowText() const;

	// ---- moderation (Campus/CampusBackendReports.cpp)
	/** The report's outcome. bOk = THE REPORT IS ON THE SERVER, which includes the 409: a row that is
	 *  already there is the same fact from the player's side and must never read as a failure they
	 *  caused. bAlreadySent says which of the two it was, so the words can differ. Message is always
	 *  the one sentence to show - on success it is empty and the caller writes its own. */
	using FReportDone = TFunction<void(bool bOk, bool bAlreadySent, const FString& Message)>;
	/** POST /v1/players/:id/report with an Idempotency-Key. Reason is one of cheating | abuse | name |
	 *  afk | other, which is the whole vocabulary a moderator can act on. RoomCode is the match it
	 *  happened in, empty from a friends list. */
	void ReportPlayer(const FString& TargetId, const FString& Reason, const FString& RoomCode, FReportDone Done);

	// ---- store
	void LoadStore(FDone Done);
	void BuyGun(const FString& Id, FDone Done);
	/** Any Diamond-priced item: a gun, an offer, a cosmetic (POST /store/items/:id/buy). BuyGun is this. */
	void BuyItem(const FString& Id, FDone Done);
	void UpgradeGun(const FString& Id, FDone Done);
	void SellGun(const FString& Id, FDone Done);
	void EquipGun(const FString& Id, FDone Done);

	// ---- cosmetics (wardrobe + lobby decoration). THE SERVER IS TRUTH ON LOAD and the device is
	//      truth until then: every screen writes UCampusSave first so the lobby is right offline, then
	//      sends the same choice up. The catalogue (CampusCosmetics.h) decides what anything looks
	//      like; these calls only move ids.
	/** GET /me/cosmetics -> {equipped:{slot:id}, presets:{"1":{slot:id},..}, decor:{anchor:id}, owned:[..]}.
	 *  On success the response is mirrored into the save, so the next launch paints before the network
	 *  answers. A failure is not fatal: the player keeps the starter outfit and whatever the save holds. */
	void LoadCosmetics(FDone Done);
	/** PUT /me/cosmetics {equipped:{outfit:id}}. Idempotent - a double tap is one equip. */
	void SaveCosmetics(const FString& OutfitId, FDone Done);
	/** PUT /me/gunskins {skins:{gun:skin|null}}: a FULL REPLACE of every gun's skin. Mirrors the answer
	 *  into GunSkins and the save, so the next match paints the gun the player just chose. */
	void SaveGunSkins(const TMap<FString, FString>& Skins, FDone Done);
	/** PUT /me/cosmetics/presets/:idx {slots:{outfit:id}}. Index is 0-based here, 1-based on the wire. */
	void SavePreset(int32 Index, const FString& OutfitId, FDone Done);
	/** PUT /me/lobby {decor:{anchor:id|null}}. EVERY anchor is sent, the bare ones as null, so the
	 *  server's picture of the room is the whole room and never a half-applied patch. */
	void SaveLobbyDecor(const TMap<FString, FString>& Decor, FDone Done);
	/** True when this cosmetic id is in the owned list, or is free (the starter outfit always is). */
	bool OwnsCosmetic(const FString& Id) const;

	/** Real money (Razorpay): opens the payment page, then reports once the server confirms the payment. */
	void BuyWithMoney(const FString& ItemId, FDone Done);
	/** Google Play paid: hands the purchase token to the server, which checks it with Google, credits the wallet
	 *  and consumes the purchase (we never consume on the device). Safe to call again with the same token - the
	 *  server is idempotent on it, which is exactly how an interrupted purchase is recovered. On success the
	 *  cached wallet here is already updated; Json is the whole response body for anything else the caller wants. */
	void VerifyGooglePurchase(const FString& ProductId, const FString& PurchaseToken, FJsonDone Done);

	// ---- the social / profile surface (UCampusSocial owns the calls; this is the transport)
	/** One authenticated API call with the full response body. Everything UCampusSocial does goes through
	 *  here rather than FHttpModule, so the access-token refresh, the 18+ replay, the ngrok header and the
	 *  error text stay in ONE place. bIdempotent adds an Idempotency-Key, which every mutation needs. */
	void CallApi(const FString& Verb, const FString& Path, TSharedPtr<FJsonObject> Body, bool bIdempotent, FJsonDone Done);

	/** The bearer for the social WebSocket's upgrade headers. Empty when signed out. */
	const FString& GetAccessToken() const { return AccessToken; }

	/** The API base the HTTP client is actually using (after the ini quote / "//" defences). */
	FString ApiBaseUrl() const { return ApiBase(); }

	/** The one player-facing sentence for a status + error body, shared with UCampusSocial. */
	static FString DescribeError(int32 Status, TSharedPtr<FJsonObject> Json) { return ErrorText(Status, Json); }

	/** Server-side level and XP (migration 013). Painted from the save until GET /me/profile lands; NEVER
	 *  computed on the phone - see the note in CampusSave.h. */
	void ApplyProgress(int32 InLevel, int32 InXpInto, int32 InXpForNext, int32 InLevelMax);
	int32 Level = 1;
	int32 XpIntoLevel = 0;
	int32 XpForNext = 0;
	int32 LevelMax = 60;
	/** 0..1 through the current level, for the top bar's XP sliver. MAX level reads as full. */
	float LevelFraction() const { return XpForNext > 0 ? FMath::Clamp(float(XpIntoLevel) / float(XpForNext), 0.f, 1.f) : 1.f; }

	// ---- rooms
	/** Spends 1 Room Card; this phone becomes the host (listen server) of the returned room.
	 *  Map is THE API'S name for it ("classic" / "campus" / "real" - routes/rooms.js MAPS), which the
	 *  caller resolves from its catalogue id with CampusBR::ApiMapFor; anything else is a 400.
	 *  EosPuid is this phone's EOS ProductUserId when it can host over EOS P2P (internet play), so
	 *  the API can hand it to joiners; pass it empty to be listed as a LAN room (ip:port). */
	void CreateRoom(const FString& Map, int32 TeamSize, int32 Rounds, int32 Minutes, bool bUnlimitedAmmo, const FString& EosPuid, FRoomDone Done);
	void JoinRoom(const FString& Code, FRoomDone Done);
	/** Called by the host's match: keeps the room listed and marks it playing once the match starts. */
	void SetHostedRoomState(bool bPlaying, int32 PlayerCount);
	void CloseHostedRoom();
	/** Host: checks a connecting phone's ticket. bRejected = the API said no (a network failure is not a no). */
	void AdmitPlayer(const FString& Ticket, TFunction<void(bool bRejected, const FString& PlayerId)> Done);
	/** Host: final stats of every admitted player; ends the room on the server. */
	void ReportMatchResult(const TArray<FCampusMatchResult>& Players);
	const FCampusRoom& GetHostedRoom() const { return HostedRoom; }

	// ---- cached state (valid after the calls above)
	FString PlayerId;
	/** The readable 9-10 digit account number a player reads out to a friend. Empty on a server that
	 *  predates it, in which case the UI falls back to the uuid's short form. */
	FString PublicId;
	FString PlayerName;
	FString Email;
	FString RollNo;
	FString Branch;
	FString College;
	/** PROFILE PICTURE (`player.avatar_url`): the lobby badge, the profile page and my own squad plate.
	 *  ALWAYS set on a current server - a new account is given one of the 45 preset icons at random -
	 *  and it may be relative, so every reader goes through CampusAvatar::Resolve. Mirrored into the
	 *  save so the badge paints on frame one instead of after GET /me lands. */
	FString AvatarUrl;
	bool bGuest = true;
	int32 RoomCards = 0;
	int32 Diamonds = 0;
	bool bDailyCardAvailable = false;
	TArray<FCampusGun> Guns;
	TArray<FCampusOffer> Offers;
	TArray<FCampusOffer> Packs;
	/** Every gun skin in the store (GET /store -> cosmetics with slot "gunskin"). */
	TArray<FCampusGunSkinItem> GunSkinShop;
	FString EquippedGun;
	/** COSMETICS, valid once bCosmeticsLoaded. Until then the wardrobe shows the free starter alone -
	 *  an "owned" list the phone invented would offer a player something the server will refuse. */
	TArray<FCampusCosmetic> OwnedCosmetics;
	FString EquippedOutfit;
	/** Three entries once loaded, each an outfit id or empty for a slot nobody has saved. */
	TArray<FString> CosmeticPresets;
	/** anchor id -> decor item id, for the anchors that carry one. */
	TMap<FString, FString> LobbyDecor;
	/** gun id -> skin item id (migration 032). A gun not in the map wears its factory finish. */
	TMap<FString, FString> GunSkins;
	bool bCosmeticsLoaded = false;
	bool bPaymentsEnabled = false;
	/** The curated college list, loaded on demand by the profile screen. */
	TArray<FCampusCollege> Colleges;
	FString CollegeId;
	bool bShowCollege = false;
	/** The server says this account has confirmed 18+. */
	bool bAdultConfirmed = false;
	/** Streak, missions and the weekend board. Valid once bLoaded; painted from cache before that. */
	FCampusEngagement Engagement;
	/** Last error worth showing (e.g. "Could not reach the server"). */
	FString LastError;
	/** Set when the last claim was refused by the daily reward cap (429), so the UI can say so kindly. */
	bool bRewardCapReached = false;

	const FCampusGun* FindGun(const FString& Id) const;
	/** The Play Console product id for a catalogue item, empty when it is not sold through Play. */
	FString GooglePlayProductFor(const FString& ItemId) const;

private:
	using FResponse = TFunction<void(int32 Status, TSharedPtr<FJsonObject> Json)>;

	UCampusSave* Save() const;
	/** A request to the API with the headers every call shares (url, verb, timeout, Accept, the ngrok
	 *  interstitial opt-out). Send() and the live-config fetch are its only two callers: the fetch is
	 *  a public, unauthenticated, conditional GET and wants none of Send's session machinery, but the
	 *  tunnel header and the "//" defended base must never be written down twice. */
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> OpenRequest(const FString& Verb, const FString& Path) const;
	void Request(const FString& Verb, const FString& Path, TSharedPtr<FJsonObject> Body, bool bAuth, bool bIdempotent, FResponse OnResponse, bool bRetried = false);
	void Send(const FString& Verb, const FString& Path, const FString& BodyText, bool bAuth, const FString& IdempotencyKey, FResponse OnResponse, bool bRetried);
	void RefreshAccess(TFunction<void(bool bOk)> Then);
	void AcceptSession(TSharedPtr<FJsonObject> Json);
	void ReadPlayer(TSharedPtr<FJsonObject> Player);
	void ReadWallet(TSharedPtr<FJsonObject> Wallet);
	void ReadStore(TSharedPtr<FJsonObject> Json);
	static FString ErrorText(int32 Status, TSharedPtr<FJsonObject> Json);
	/** Adds adultConfirmed:true to a sign-in body when the player has ticked the 18+ box. */
	void AddAdultTick(TSharedPtr<FJsonObject> Body) const;
	/** Adds client_build to every sign-in body. The server has always compared this against
	 *  MIN_CLIENT_BUILD and answered 426 CLIENT_TOO_OLD; until now the game sent nothing, which the
	 *  API tolerates only while the floor is 1. */
	void AddClientBuild(TSharedPtr<FJsonObject> Body) const;
	void ReadEngagement(TSharedPtr<FJsonObject> Json);
	/** Applies a live-config body. bFromCache drops `maintenance.on` and marks the minimum build as
	 *  not-live: only a live answer may lock the game or gate it on a version. */
	void ReadLiveConfig(TSharedPtr<FJsonObject> Json, bool bFromCache);
	/** The three PHASE B keys. Pure readers of one published object each, so an absent or malformed
	 *  key simply leaves its struct empty and the compiled-in fallback standing. */
	static void ReadContentKey(const TSharedPtr<FJsonObject>& Key, FCampusContentIndex& Out);
	static void ReadEventsKey(const TSharedPtr<FJsonObject>& Key, FCampusEvents& Out);
	static void ReadModesKey(const TSharedPtr<FJsonObject>& Key, FCampusModes& Out);
	/** Now in IST wall-clock terms, which is the only frame a br_schedule row means anything in. */
	FDateTime IstNow() const;
	/** Re-reads the blob held in the save. Returns false when there is nothing there to read, or it
	 *  does not parse. bFromCache is forwarded verbatim: FALSE is correct on a 304, where the server
	 *  has just told us these exact bytes ARE the current revision. */
	bool ReadLiveConfigFromSave(bool bFromCache);
	/** The published floor, or 0 when no blob has ever been read. */
	int32 MinBuildFloor() const { return LiveConfig.MinClientBuild; }
	FString ApiBase() const;
	bool HeartbeatTick(float DeltaTime);
	bool PaymentPollTick(float DeltaTime);
	void FinishPendingOrder(bool bOk, const FString& Error);
	/** The in-app Razorpay sheet's outcome: success -> server verifies the signature; otherwise the server asks
	 *  Razorpay what really happened, then closes the order (cancelled / failed / paid after all). */
	void OnNativePaymentResult(const FString& OrderId, int32 Code, const FString& RzpOrderId, const FString& PaymentId,
		const FString& Signature, const FString& Message);
	void StartPaymentPolling();

	/** A real-money order waiting for Razorpay (the payment page is open in the browser). */
	FString PendingOrderId;
	int32 PendingOrderPolls = 0;
	FDone PendingOrderDone;
	FTSTicker::FDelegateHandle PaymentPollHandle;

	// ---- live config state. One blob, one timestamp, two latches. No timer.
	FCampusLiveConfig LiveConfig;
	/** FPlatformTime::Seconds() when the last 200 or 304 landed; 0 = never fetched this app run. A
	 *  failure deliberately does NOT stamp this, so the next resume tries again. */
	double LiveConfigFetchedAt = 0.0;
	bool bLiveConfigInFlight = false;
	/** A call came back 503 MAINTENANCE. Cleared by the next 2xx, and by a blob that says it is over. */
	bool bMaintenanceFromCall = false;
	FString CallMaintenanceMessage;
	/** A sign-in answered 426 CLIENT_TOO_OLD: the update screen is owed even before a blob lands. */
	bool bClientTooOld = false;
	/** The resume hook (FCoreDelegates). Taken off in Deinitialize or it fires into a dead subsystem. */
	FDelegateHandle ForegroundHandle;
	/** How stale a blob may be before a resume refetches it (design: ten minutes). */
	static constexpr double LiveConfigMaxAgeSeconds = 600.0;
	/** THE CACHE CEILING. The blob is written verbatim into the save file, which is read synchronously
	 *  on every launch, and nothing on the console caps what it may publish - one runaway announcement
	 *  body would make every start-up slower for ever. A blob past this is used for THIS run and then
	 *  simply not cached (and the ETag is dropped with it, so the next launch asks unconditionally
	 *  rather than sending an ETag with no bytes behind it). 64 KB is ~30x the real blob. */
	static constexpr int32 LiveConfigCacheMaxBytes = 64 * 1024;
	/** What a card can carry. Longer copy belongs on the console's own screen, not over the lobby:
	 *  past this the card is a wall of text in the corner, and the strings sit in the save for ever. */
	static constexpr int32 AnnouncementTitleMaxChars = 120;
	static constexpr int32 AnnouncementBodyMaxChars = 400;
	/** How many dismissals the save remembers. Oldest first out: the ids are server-side sequence
	 *  numbers that only ever climb, so the oldest entry is also the one least likely to come back,
	 *  and an unbounded list is a save file that grows for the life of the account. */
	static constexpr int32 MaxDismissedAnnouncements = 100;

	/** Set by Send() when a protected route answered 403 AGE_CONFIRM_REQUIRED and the local tick was absent. */
	bool bAgeConfirmRequired = false;
	/** One self-healing confirm per app run: stops a confirm/retry loop if the server keeps refusing. */
	bool bAdultRetryUsed = false;
	/** Play Console product id per catalogue item id, from GET /store. */
	TMap<FString, FString> GoogleProducts;

	FString AccessToken;
	FCampusRoom HostedRoom;
	/** This phone's EOS ProductUserId while it is hosting; repeated on every heartbeat. */
	FString HostedEosPuid;
	bool bHostedPlaying = false;
	int32 HostedPlayers = 1;
	FTSTicker::FDelegateHandle HeartbeatHandle;
	TArray<TFunction<void(bool)>> PendingRefresh;
	bool bRefreshing = false;
};
