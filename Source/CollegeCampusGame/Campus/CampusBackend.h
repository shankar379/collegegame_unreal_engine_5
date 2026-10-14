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

/** A pack or offer bought with money. */
struct FCampusOffer
{
	FString Id;
	FString Name;
	FString Description;
	FString Badge;
	TArray<FString> Perks;
	float PriceInr = 0.f;
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

	bool IsEmpty() const { return Diamonds == 0 && RoomCards == 0 && !bTitle; }
	/** "50 DIAMONDS + 1 CARD" - already uppercase for the display face. */
	FString Describe() const;
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
	void ClaimStreak(FDone Done);
	/** A settled weekend's reward (last_weekend.unclaimed_reward). */
	void ClaimPeriodReward(const FString& PeriodId, FDone Done);

	// ---- store
	void LoadStore(FDone Done);
	void BuyGun(const FString& Id, FDone Done);
	void UpgradeGun(const FString& Id, FDone Done);
	void SellGun(const FString& Id, FDone Done);
	void EquipGun(const FString& Id, FDone Done);
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
	void CreateRoom(const FString& Map, int32 TeamSize, int32 Rounds, int32 Minutes, const FString& EosPuid, FRoomDone Done);
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
	FString EquippedGun;
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
	void ReadEngagement(TSharedPtr<FJsonObject> Json);
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
