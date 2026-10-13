// Friends, squads, invites, presence and the profile page's data - one game-instance subsystem holding
// ONE WebSocket to /v1/social/ws (subprotocol campus.social.v1).
//
// WHY A SOCKET AND NOT A POLL. The API lives behind an ngrok free tunnel metered at 20,000 HTTP requests
// a MONTH. A 15-second poll is 5,760 requests a day PER PLAYER: twenty testers would exhaust the month in
// four hours. A socket costs one request per connection and turns the heartbeat, the acks and the token
// refresh into free frames (docs/SOCIAL_PARTY_DESIGN.md §3.4).
//
// THE SOCKET IS A DELIVERY CHANNEL, NOT A SECOND API. Nothing that changes state is sent up it. Every
// mutation is an ordinary UCampusBackend::CallApi with its Idempotency-Key, its rate limit and its
// transaction; the socket only CONFIRMS. So when the socket is dead every button still works and the UI
// repaints from the HTTP response. The only frames this client sends are hb / ack / auth / sync / ping.
//
// AT-LEAST-ONCE, DEDUPED HERE. Events carry a gapless per-recipient `seq`. We keep LastAppliedSeq in the
// save, drop anything at or below it, and on a gap ask for a resend (`sync`) and fall back to
// GET /v1/social/events?after= two seconds later. A ring of the last 32 event ids catches the same event
// arriving over two transports.
//
// EVERY DEADLINE IS MEASURED FROM THE SERVER CLOCK (Now() below), never FDateTime::UtcNow(), so a phone
// whose clock is a day out still shows a correct invite countdown.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Campus/CampusParty.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CampusSocial.generated.h"

class FJsonObject;
class FJsonValue;
class IWebSocket;
class UCampusBackend;
class UCampusSave;

/** What the presence heartbeat tells the server this phone is doing. */
enum class ECampusPresence : uint8 { None, Menu, Lobby, Match };

/** How the social channel is doing, for the one honest line the friends panel shows. */
enum class ECampusSocialState : uint8 { Off, Connecting, Live, Retrying, GaveUp };

/**
 * One `playerCard` (SOCIAL_PARTY §6.1): the single player shape the whole social UI paints from - the
 * friends list, the invite picker, the post-match rows and the notice cards. There is no avatar art in
 * this project, so `Color` IS the avatar, which is why the API returns it on every card.
 */
struct FCampusPlayerCard
{
	FString PlayerId;
	FString Name;
	FLinearColor Color = FLinearColor(0.35f, 0.38f, 0.44f, 1.f);
	/** menu | lobby | match | offline | unknown */
	FString Presence = TEXT("offline");
	/** They are already in somebody's squad, so the INVITE button is a lie. */
	bool bInParty = false;
	/** Their live room, friends only - it is a join credential, not a label. */
	FString RoomCode;
	int32 Level = 1;
	/** none | outgoing | incoming | friends | blocked */
	FString FriendState;
	/** The human-readable account number a player reads out to a friend ("1026 384 779"). Empty on a
	 *  server that predates it, in which case the UI falls back to the uuid's short form. */
	FString PublicId;
	/** When this account was last used, for the "4 HOURS AGO" column on an offline friend. */
	FDateTime LastSeenAt = FDateTime();
	/** Suggested-list rows only: played_together | same_college, for the one line that explains why
	 *  this stranger is being put in front of the player. */
	FString Reason;
	/** Recent-players rows only. */
	int32 MatchesTogether = 0;
	/** Set while an invite from me to them is pending, so the row reads INVITED. */
	FString PendingInviteId;

	bool IsOnline() const { return Presence != TEXT("offline") && Presence != TEXT("unknown") && !Presence.IsEmpty(); }
	bool IsInMatch() const { return Presence == TEXT("match"); }
	bool IsFriend() const { return FriendState == TEXT("friends"); }
	/** IN LOBBY / IN A SQUAD / ONLINE / IN MATCH / OFFLINE - the four labels, from two fields. */
	FText StatusLabel() const;
	/** The friends-list column: ONLINE in accent while they are here, else "4 HOURS AGO" from
	 *  last_seen_at - which is a far kinder answer than a flat OFFLINE. */
	FText LastSeenLabel(const FDateTime& Now) const;
	/** "1026 384 779", grouped in threes so it can be read out loud. */
	FString PrettyPublicId() const;
	FLinearColor StatusColor() const;
	/** The first letter of the name, for the colour avatar plate. */
	FString Initial() const;
};

/** The server's party, mirrored. `Roster()` is what drops into UCampusParty::ApplyRoster. */
struct FCampusServerParty
{
	bool bValid = false;
	FString Id;
	FString LeaderId;
	/** open | in_match */
	FString State;
	FString RoomCode;
	int32 Mode = 4;
	/** Accepted members, server order (joined_at). */
	TArray<FCampusPartyMember> Members;
	/** Pending invites: the dimmed "INVITED" plates. */
	TArray<FCampusPartyMember> Pending;

	bool IsLeader(const FString& PlayerId) const { return bValid && !PlayerId.IsEmpty() && LeaderId == PlayerId; }
	bool IsInMatch() const { return State == TEXT("in_match"); }
	int32 Occupancy() const { return Members.Num() + Pending.Num(); }
	bool HasRoom() const { return bValid && Occupancy() < Mode; }
	/** The leader's display name, for "WAITING FOR ARJUN". */
	FString LeaderName() const;
	/** Field for field, over everything the lobby draws. A party frame that says nothing new must not
	 *  reach OnPartyChanged: every one of those repaints the lobby, and the presence heartbeat alone
	 *  would do it for no reason. Same contract as UCampusParty::ApplyRoster's guard. */
	bool Matches(const FCampusServerParty& Other) const;
	/** Members then pending, which is exactly the plate order the lobby draws. */
	TArray<FCampusPartyMember> Roster() const;
};

/** A slide-in card. Invites and friend requests share the stack; invites sort above (they have a clock). */
enum class ECampusNoticeKind : uint8 { Invite, FriendRequest };

struct FCampusNotice
{
	ECampusNoticeKind Kind = ECampusNoticeKind::Invite;
	/** invite_id, or the asker's player id for a friend request. The dedupe key. */
	FString Key;
	FCampusPlayerCard Sender;
	/** 2 Duo, 4 Squad. */
	int32 Mode = 4;
	int32 MemberCount = 1;
	/** Server clock. A friend request gets a 6-second hold instead of a real deadline. */
	FDateTime ExpiresAt = FDateTime();
	/** Seconds the bar started from, so it drains proportionally. */
	float TotalSeconds = 45.f;
	/** Empty while the card is live; CANCELLED / TAKEN / EXPIRED while it slides out. */
	FString Exit;
	/** A tap is in flight: both buttons go disabled so nothing is answered twice. */
	bool bBusy = false;
};

/** A milestone badge on the SHOWCASE wall. */
struct FCampusMilestone
{
	FString Id;
	FString Name;
	FString Subtitle;
	FString Glyph;
	int32 Tier = 1;
	bool bEarned = false;
	/** Null for a backfilled badge: "when we noticed" is not when it happened, and no date beats a wrong one. */
	FDateTime AchievedAt = FDateTime();
	bool bBackfilled = false;
	int32 Progress = 0;
	int32 Target = 0;
};

/** An owned title or banner. */
struct FCampusOwnedItem
{
	FString ItemId;
	FString Name;
	bool bEquipped = false;
};

/** One HISTORY row. RoomCode empty = redacted, so the row must not look tappable. */
struct FCampusHistoryRow
{
	FString RoomCode;
	FString Map;
	FDateTime At = FDateTime();
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 Headshots = 0;
	int32 Points = 0;
	int32 Xp = 0;
	bool bWon = false;
	bool bRated = true;
	bool bMvp = false;
};

/** GET /v1/me/profile or /v1/players/:id/profile - one request paints the whole page, every tab. */
struct FCampusProfile
{
	bool bValid = false;
	bool bMine = false;
	bool bCached = false;

	FString PlayerId;
	/** The readable account number (migration: players.public_id). Empty = fall back to the uuid. */
	FString PublicId;
	FString Name;
	FLinearColor Color = FLinearColor(0.35f, 0.38f, 0.44f, 1.f);
	FString CollegeName;
	FString Branch;
	bool bBoardOptOut = false;

	int32 Level = 1;
	int32 Xp = 0;
	int32 XpInto = 0;
	int32 XpForNext = 0;
	int32 LevelMax = 60;
	int32 Likes = 0;

	int32 Matches = 0;
	int32 RatedMatches = 0;
	int32 Wins = 0;
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 Headshots = 0;
	int32 Mvps = 0;
	int32 BestMatchKills = 0;
	int32 MatchesAsHost = 0;
	double DamageDealt = 0.0;
	float WinRate = 0.f;
	float Kd = 0.f;
	float HeadshotRate = 0.f;

	/** The rank block, designed honestly: there are no ranked tiers, so these three true things. */
	bool bHasRank = false;
	FString TitleId;
	FString TitleName;
	bool bHasBestWeekend = false;
	int32 BestWeekendRank = 0;
	FString BestWeekendLabel;
	FString BestWeekendBracket;
	bool bHasThisWeekend = false;
	int32 WeekendPoints = 0;
	int32 WeekendRank = 0;
	int32 WeekendRatedMatches = 0;
	bool bWeekendProvisional = true;
	FDateTime WeekendEndsAt = FDateTime();

	int32 MilestonesEarned = 0;
	int32 MilestonesTotal = 0;
	TArray<FCampusMilestone> Milestones;
	TArray<FCampusOwnedItem> Titles;
	TArray<FCampusOwnedItem> Banners;

	bool bHasGun = false;
	FString GunName;
	int32 GunLevel = 0;
	int32 GunMaxLevel = 1;
	int32 GunMagazine = 0;
	float GunDamage = 0.f;
	float GunFireRate = 0.f;
	float GunAccuracy = 0.f;

	TArray<FCampusHistoryRow> Recent;
	/** null = the end of history; LOAD MORE shows only while this is set. */
	FDateTime NextBefore = FDateTime();

	/** Someone else's page only. */
	FString FriendState;
	FString Presence;
	bool bInParty = false;
	bool bCanLike = false;
	bool bCanInvite = false;

	FDateTime ServerTime = FDateTime();

	/** "#8F2A1C4D" - the short form shown on the page; the copy button takes the whole uuid. */
	FString ShortId() const;
	/** The one-line share card put on the clipboard. */
	FString ShareText() const;
};

/** One row of the post-match scoreboard. */
struct FCampusSummaryRow
{
	FString PlayerId;
	/** Empty = an erased account: the row stays (the match happened) and reads PLAYER LEFT. */
	FString Name;
	FLinearColor Color = FLinearColor(0.35f, 0.38f, 0.44f, 1.f);
	FString FriendState;
	int32 Team = 0;
	int32 Level = 0;
	int32 Kills = 0;
	int32 Deaths = 0;
	int32 Headshots = 0;
	int32 DamageDealt = 0;
	int32 Points = 0;
	int32 Xp = 0;
	bool bWon = false;
	bool bMvp = false;
	bool bMe = false;
};

/** GET /v1/rooms/:code/summary - the player-of-the-match screen's one and only source. */
struct FCampusSummary
{
	bool bValid = false;
	FString Code;
	FString Map;
	bool bRated = true;
	FString RatedReason;
	int32 WinningTeam = -1;
	FString MvpPlayerId;
	TArray<FCampusSummaryRow> Players;

	const FCampusSummaryRow* Mvp() const;
};

/** GET /v1/friends plus GET /v1/players/recent, in the server's order. The client never re-sorts. */
struct FCampusRoster
{
	bool bValid = false;
	/** Redis was unreachable, so presence came from the last_seen_at fallback: draw a muted dot. */
	bool bDegraded = false;
	TArray<FCampusPlayerCard> Friends;
	TArray<FCampusPlayerCard> Incoming;
	TArray<FCampusPlayerCard> Outgoing;
	TArray<FCampusPlayerCard> Blocked;
	/** Loaded separately, on the ADD tab: who to suggest before anything is typed. */
	bool bRecentLoaded = false;
	TArray<FCampusPlayerCard> Recent;
	/** The last GET /v1/players/search answer. */
	bool bSearched = false;
	FString SearchTerm;
	TArray<FCampusPlayerCard> Results;
	int32 FriendsUsed = 0;
	int32 FriendsMax = 100;
};

/** The push side of the channel. Declared outside the class so UHT never has to parse it. */
DECLARE_MULTICAST_DELEGATE_OneParam(FCampusSocialString, const FString&);
/** An event whose payload is a whole object (br.matched), handed over as-is so the subsystem that
 *  owns that feature parses its own shape and this file does not grow a second match struct. */
DECLARE_MULTICAST_DELEGATE_OneParam(FCampusSocialJson, const TSharedPtr<FJsonObject>&);

UCLASS()
class UCampusSocial : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FDone = TFunction<void(bool bOk, const FString& Error)>;
	/** Some answers turn on WHICH refusal it was, not on its wording: accept needs IN_OTHER_PARTY to
	 *  know whether to raise the "leave your squad?" confirm. Matching on the message would break the
	 *  day somebody rewords it, so the error CODE comes back too. */
	using FCodeDone = TFunction<void(bool bOk, const FString& Error, const FString& Code)>;
	using FProfileDone = TFunction<void(bool bOk, const FString& Error, const FCampusProfile& Profile)>;
	using FSummaryDone = TFunction<void(bool bOk, const FString& Error, const FCampusSummary& Summary)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- the channel
	/** True when this build has a WebSocket at all (#if WITH_WEBSOCKETS). */
	static bool IsSocketAvailable();
	/** Connects (or reconnects at once, no backoff) and does a cold read of party, invites and friends. */
	void Start();
	/** Signed out, or the app went to the background: close cleanly and stop every timer. */
	void Stop();
	ECampusSocialState GetState() const { return State; }
	bool IsLive() const { return State == ECampusSocialState::Live; }
	/** The one line the friends panel shows: LIVE / CONNECTING / SOCIAL OFFLINE - TAP TO RETRY. */
	FText StatusLabel() const;

	/** What this phone is doing, for the presence heartbeat. One call site per screen change. */
	void SetScreenState(ECampusPresence InState);

	// ---- the server clock (every countdown in the social UI runs off this, never the phone's clock)
	FDateTime Now() const;
	int32 SecondsUntil(const FDateTime& When) const;
	float SecondsUntilPrecise(const FDateTime& When) const;

	// ---- party
	/** My own server player id (UCampusBackend::PlayerId), empty when not signed in. Public because the
	 *  squad UI has to tell MY plate from everybody else's: my plate never offers REMOVE. */
	FString Me() const;
	bool HasParty() const { return Party.bValid; }
	const FCampusServerParty& GetServerParty() const { return Party; }
	bool IsPartyLeader() const;
	void RefreshParty(FDone Done = nullptr);
	/** Mode is 2 (Duo) or 4 (Squad): the server creates the party on the FIRST invite, so the mode
	 *  the lobby is showing has to ride along or the squad opens at the wrong width. */
	void InvitePlayer(const FString& PlayerId, int32 Mode, FDone Done);
	void CancelInvite(const FString& InviteId, FDone Done);
	/** bLeaveCurrent answers the 409 IN_OTHER_PARTY confirm ("Leave your squad and join X?"). */
	void AcceptInvite(const FString& InviteId, bool bLeaveCurrent, FCodeDone Done);
	void RejectInvite(const FString& InviteId, FDone Done);
	void LeaveParty(FDone Done);
	void KickMember(const FString& PlayerId, FDone Done);
	void SetPartyMode(int32 Mode, FDone Done);
	/** Leader only, after CreateRoom: the squad's phones get party.room_ready and join themselves. */
	void HandOffRoom(const FString& Code, FDone Done);

	// ---- friends
	const FCampusRoster& GetRoster() const { return Roster; }
	void RefreshFriends(FDone Done = nullptr);
	void RefreshRecent(FDone Done = nullptr);
	/** GET /v1/players/search?q= - digits are an exact public id, anything else is a name. Falls back
	 *  to filtering the suggested list when the server has no search route yet. */
	void SearchPlayers(const FString& Query, FDone Done);
	void ClearSearch();
	/** My own public id, cached from the profile read, for the lobby's "copy my ID". */
	const FString& MyPublicId() const;
	void RequestFriend(const FString& PlayerId, FDone Done);
	void AcceptFriend(const FString& PlayerId, FDone Done);
	void DeclineFriend(const FString& PlayerId, FDone Done);
	void RemoveFriend(const FString& PlayerId, FDone Done);
	void BlockPlayer(const FString& PlayerId, FDone Done);
	void UnblockPlayer(const FString& PlayerId, FDone Done);
	/** The friends card for an id, or null - so a post-match row can read a live friend_state. */
	const FCampusPlayerCard* FindFriend(const FString& PlayerId) const;

	// ---- notice cards
	const TArray<FCampusNotice>& GetNotices() const { return Notices; }
	/** Cards are drawn on every signed-in screen (an invite over the Store must be answerable) and
	 *  suppressed before sign-in and during a match, where they queue and are re-evaluated here. */
	void SetNoticesVisible(bool bVisible);
	bool AreNoticesVisible() const { return bNoticesVisible; }
	/** Marks a card as leaving with its exit caption; the stack animates it out and then drops it. */
	void ResolveNotice(const FString& Key, const FString& Exit);
	void DropNotice(const FString& Key);
	FCampusNotice* FindNotice(const FString& Key);

	// ---- profile, likes, titles
	/** Empty id or "me" = my own page. Updates the cached level on the way through. */
	void LoadProfile(const FString& PlayerId, FProfileDone Done);
	void LoadHistory(const FString& PlayerId, const FDateTime& Before, FProfileDone Done);
	void LikePlayer(const FString& PlayerId, FDone Done);
	void EquipTitle(const FString& ItemId, FDone Done);

	// ---- post-match
	void LoadSummary(const FString& Code, FSummaryDone Done);
	/** The room whose summary has not been shown yet (from match.summary_ready, or the room we hosted). */
	const FString& PendingSummaryRoom() const { return PendingSummary; }
	void NoteFinishedRoom(const FString& Code);
	void ClearPendingSummary() { PendingSummary.Reset(); }

	// ---- delegates (the UI rebuilds once on a change instead of diffing every frame)
	FSimpleMulticastDelegate OnPartyChanged;
	FSimpleMulticastDelegate OnNoticesChanged;
	FSimpleMulticastDelegate OnRosterChanged;
	FSimpleMulticastDelegate OnStateChanged;

	/** party.room_ready: the member's lobby joins this code with a visible "JOINING..." beat. */
	FCampusSocialString OnRoomReady;
	/** match.summary_ready: open the player-of-the-match screen for this room code. */
	FCampusSocialString OnSummaryReady;
	/** br.matched: the BATTLE ROYALE queue found a match. UCampusBRQueue reads the object. */
	FCampusSocialJson OnBRMatched;
	/** Something the player should simply be told: a milestone, a kick, a disband. */
	FCampusSocialString OnNotice;

private:
	UCampusBackend* Api() const;
	UCampusSave* Save() const;

	// ---- socket
	void OpenSocket();
	void CloseSocket(int32 Code);
	void ScheduleReconnect();
	void SetState(ECampusSocialState Next);
	void SendFrame(const FString& Frame);
	void HandleMessage(const FString& Message);
	void HandleHello(const TSharedPtr<FJsonObject>& Frame);
	void HandleEvent(const TSharedPtr<FJsonObject>& Frame);
	void HandleEphemeral(const TSharedPtr<FJsonObject>& Frame);
	/** Applies one durable event: dedupe, then the switch on `type`. */
	void ApplyEvent(int64 Seq, const FString& Id, const FString& Type, const TSharedPtr<FJsonObject>& Payload, const FDateTime& Expires);
	void RequestSync();
	/** GET /v1/social/events?after= - the gap repair when `sync` did not answer in time. */
	void CatchUpOverHttp();
	/** A full re-read after a gap too big to repair (or a cold start). */
	void ColdRead();

	bool HeartbeatTick(float DeltaTime);
	bool HousekeepingTick(float DeltaTime);

	void HandleBackground();
	void HandleForeground();

	// ---- parsing
	void ReadParty(const TSharedPtr<FJsonObject>& Json);
	void ClearParty(const FString& Reason);
	void PushRosterToLobby();
	void ReadRoster(const TSharedPtr<FJsonObject>& Json);
	/** Pulls `server_time` out of any response that carries one. Every countdown and every
	 *  "X HOURS AGO" in the social UI is measured from it, never from the phone's clock. */
	void TakeServerTime(const TSharedPtr<FJsonObject>& Json);
	/** Stamps the friend rows with my pending invite to them; run after EITHER read. */
	void MarkPendingInvites();
	void ReadInviteTray(const TSharedPtr<FJsonObject>& Json);
	void PushNotice(FCampusNotice&& Notice);

	TSharedPtr<IWebSocket> Socket;
	ECampusSocialState State = ECampusSocialState::Off;
	/** Exponential backoff with FULL jitter: twenty phones must not reconnect in the same 200 ms. */
	int32 Attempt = 0;
	float ReconnectIn = 0.f;
	bool bWantConnection = false;
	bool bBackgrounded = false;

	/** The cursor. Mirrored into the save so an app restart does not replay yesterday's cards. */
	int64 LastAppliedSeq = 0;
	/** Belt to the sequence's braces: the same event over two transports is dropped by id. */
	TArray<FString> RecentEventIds;
	/** Events past a gap, held until the missing ones arrive. */
	struct FHeldEvent
	{
		int64 Seq = 0;
		FString Id;
		FString Type;
		TSharedPtr<FJsonObject> Payload;
		FDateTime Expires = FDateTime();
	};
	TArray<FHeldEvent> Held;
	/** Seconds left before the unanswered `sync` falls back to HTTP; 0 = not waiting. */
	float SyncDeadline = 0.f;
	bool bCatchingUp = false;

	/** The server clock, plus the local seconds since it was read. */
	FDateTime ServerTime = FDateTime();
	double ServerReadAt = 0.0;

	ECampusPresence Presence = ECampusPresence::None;
	/** The last state actually sent, so a screen change pushes a beat immediately and nothing else does. */
	ECampusPresence SentPresence = ECampusPresence::None;
	float HeartbeatIn = 0.f;
	float ReauthIn = 0.f;
	bool bReauthWanted = false;
	/** Acks are coalesced to at most one per 5 s. */
	float AckIn = 0.f;
	int64 AckSeq = 0;

	FCampusServerParty Party;
	FCampusRoster Roster;
	TArray<FCampusNotice> Notices;
	bool bNoticesVisible = false;
	FString PendingSummary;
	FString PublicId;
	/** FPlatformTime::Seconds() of the last three-request cold read, so two Start() calls in a row
	 *  cost three requests and not six. */
	double LastColdReadAt = 0.0;

	FTSTicker::FDelegateHandle HeartbeatHandle;
	FTSTicker::FDelegateHandle HousekeepingHandle;
	FDelegateHandle BackgroundHandle;
	FDelegateHandle ForegroundHandle;
};
