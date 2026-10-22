// BATTLE SESSION matchmaking, client side. One game-instance subsystem so the queue survives the
// lobby widget being rebuilt (and a level travel) and there is exactly one place that knows whether
// this phone is queued.
//
//   POST   /v1/br/queue      {mode, region, eos_puid?, day_night?}  -> the LEADER queues the TEAM
//   DELETE /v1/br/queue                                             -> cancel, whole team
//   GET    /v1/br/queue/me                                          -> {state, session_state, eta_s,
//                                                                       battle_session{...}}
//   POST   /v1/br/:handle/heartbeat                                 -> host only, every 20 s
//   ws     "bs.placed" / "br.matched"                               -> the session object, pushed
//   ws     "bs.sealed"                                              -> joins are closed: {humans, bots,
//                                                                       countdown_s}. The host seals the
//                                                                       match on it; it also rides back
//                                                                       on the heartbeat response.
//   ws     "bs.ended"                                               -> the host went quiet
//   ws     "br.result"                                              -> my placement and XP
//
// THE PATHS ARE STILL /v1/br/*: the backend renamed the concept to Battle Session (its
// docs/BATTLE_SESSION.md) and deliberately did NOT rename the URLs. `battle_session` and `match` are
// the same object on the wire, and `bs.placed` and `br.matched` are the same payload under two names -
// exactly ONE of them may be acted on, which is what the session-id dedupe below is for.
//
// THE SOCKET IS THE PRIMARY CHANNEL. The API lives behind a tunnel metered at 20,000 requests a
// MONTH (see CampusSocial.h), so GET /br/queue/me runs at 5 s ONLY while the socket is not live -
// it is the fallback, not the design. With the socket up the whole queue costs two requests: the
// join and the cancel.
//
// ONLY THE LEADER QUEUES. One ticket is one team; a member's phone never posts, it simply waits for
// `bs.placed` and travels with everyone else.
//
// Nothing here travels or opens a level: UCampusBRQueue reports, and ACampusLobbyPlayerController::
// JoinBRMatch does the moving. One owner for the queue, one owner for travel.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CampusBR.generated.h"

class AActor;
class FJsonObject;
class UCampusBackend;
class UCampusSocial;

/** The battle session object: `battle_session` (= the legacy `match`) from the queue read, from
 *  `bs.placed` and from `br.matched`. One shape, whichever door it came through. */
struct FCampusBRMatch
{
	/** The six-character handle every route accepts first. */
	FString Code;
	/** BS-SQUAD-IN-000206: what a player reads out, and what the dedupe keys on. */
	FString SessionId;
	/** "solo" | "duo" | "squad", the server's word on the team size this session was packed for. */
	FString Mode;
	FString Region;
	/** "FILLING" .. "ENDED" (BATTLE_SESSION_DESIGN §3), upper case on the wire. */
	FString State;
	/** The server's own flag: joins are closed, the roster is fixed. While it is false the session is
	 *  still FILLING, which is the only time DELETE /br/queue can cancel it. */
	bool bSealed = false;
	/** "eos" (P2P, works over the internet) or "dedicated" (Address:Port - the AWS path). */
	FString ConnectType;
	FString HostPuid;
	FString Address;
	int32 Port = 7777;
	/** Who the server named as host. Equal to my player id AND an EOS session = I host (see AmHost). */
	FString HostPlayerId;
	FString TeamId;
	/** Catalog map id (CampusCatalog::Maps), e.g. "VarsityValley". */
	FString Map;
	/** "day" | "night" from the server, which has the final word on the lighting. */
	bool bNight = false;
	int32 Humans = 0;
	/** What the game spawns to fill the drop; the server does the subtraction so no client gets it wrong. */
	int32 Bots = 0;
	int32 MaxHumans = 0;
	int32 TotalSlots = 50;
	/** Seconds left on the server's COUNTDOWN when this object was cut. 0 while the session is filling. */
	int32 CountdownSeconds = 0;
	/** Seconds until the FILL CAP, after which the server seals with bots. 0 once sealed. The host
	 *  carries it into the level ("?Fill=") so the game mode's safety net knows when a seal is late. */
	int32 FillSeconds = 0;
	/** Server clock; only used for the "STARTING" beat, never for gameplay timing. */
	FDateTime StartsAt = FDateTime();

	bool IsValid() const { return !Code.IsEmpty() || !SessionId.IsEmpty(); }
	bool IsEos() const { return ConnectType == TEXT("eos") && !HostPuid.IsEmpty(); }
	/** A server somebody else runs: travel to Address:Port, and NOBODY opens a listen server. The name
	 *  counts on its own - a "dedicated" session that arrived without an address is a server fault, and
	 *  answering it by hosting on a phone the other players will never dial is worse than the error. */
	bool IsDedicated() const
	{
		return ConnectType.Equals(TEXT("dedicated"), ESearchCase::IgnoreCase) || (!IsEos() && HasAddress());
	}
	bool HasAddress() const { return !Address.IsEmpty(); }
	/** What /v1/br/:handle is called with, and what two placed events are deduped on. */
	const FString& Handle() const { return Code.IsEmpty() ? SessionId : Code; }
	const FString& Key() const { return SessionId.IsEmpty() ? Code : SessionId; }
};

/** br.result: my own line of the host's report. */
struct FCampusBRResult
{
	FString SessionId;
	FString Mode;
	int32 Placement = 0;
	int32 Kills = 0;
	int32 XpAwarded = 0;
	bool bRated = true;
};

/** Where this phone is in the queue. Idle is also where a cancel and a failure land. */
enum class ECampusBRState : uint8 { Idle, Joining, Queued, Matched, Leaving };

/** The session states a HOST may announce on POST /v1/br/:handle/heartbeat.
 *
 *  FILLING is where the beat STARTS, the instant the host opens the level: the session is still taking
 *  players and the server put it in FILLING when it allocated, so that first beat is BARE - announcing
 *  a state the session is already in is a 409 BS_BAD_STATE. The move to COUNTDOWN is the SERVER's to
 *  make (it owns the seal), so the host only announces it once it has actually sealed. Ended stops the
 *  beat; the result route closes the session. */
enum class ECampusBRBeat : uint8 { Filling, Countdown, InFlight, InProgress, Ended };

/** A session object arrived (socket or poll). Declared outside the class, like the social delegates,
 *  so UHT never has to parse it. */
DECLARE_MULTICAST_DELEGATE_OneParam(FCampusBRMatched, const FCampusBRMatch&);
/** bs.ended: the session was settled with no result. The payload is the sentence to show. */
DECLARE_MULTICAST_DELEGATE_OneParam(FCampusBREnded, const FString&);
/** THE SEAL (Humans, Bots, CountdownS). The BACKEND decides it - the session filled, or the fill cap
 *  ran out and the empty slots became bots - and the HOST acts on it by calling
 *  ACampusBRGameMode::SealSession. Humans is what the server PLACED and is advisory: the game mode
 *  counts who is actually standing on the platform. A zero CountdownS means "use your own ten". */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FCampusBRSealed, int32, int32, float);
DECLARE_MULTICAST_DELEGATE_OneParam(FCampusBRScored, const FCampusBRResult&);

namespace CampusBR
{
	/** Map id -> level to open, taking EITHER language: a catalogue id ("CampusMap") or the API's own
	 *  name for the same level ("campus"). Anything a battle session cannot be played on falls back to
	 *  the valley, so a server that names a map this build does not have still starts a match instead of
	 *  a black screen. */
	const TCHAR* LevelForMap(const FString& MapId);

	/** The API's name for a catalogue map id ("CampusMap" -> "campus"), for the queue request and for
	 *  POST /v1/rooms. NULL for a map the API has no name for, which the caller answers for itself: a
	 *  battle session falls back to the valley and a 4v4 room to the college, and neither may send a
	 *  name the route would refuse. */
	const TCHAR* ApiMapFor(FName CatalogId);

	/** The other way: a catalogue id for whatever the server sent ("campus" -> "CampusMap"), or
	 *  NAME_None for a map this build does not have. A catalogue id passes straight through. */
	FName CatalogMapFor(const FString& MapId);

	/** 1 -> "solo", 2 -> "duo", anything else -> "squad". The API's three queue names. */
	const TCHAR* ModeForTeamSize(int32 TeamSize);

	/** "solo" -> 1, "duo" -> 2, anything else -> 4. The host needs it for "?Team=". */
	int32 TeamSizeForMode(const FString& Mode);

	/** "SOLO" / "DUO" / "SQUAD" for the overlay's subtitle. */
	FText TeamSizeLabel(int32 TeamSize);
}

UCLASS()
class UCampusBRQueue : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FDone = TFunction<void(bool bOk, const FString& Error)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** POST /v1/br/queue. TeamSize is the lobby's 1 / 2 / 4, MapId the catalogue map picked on the
	 *  map-select screen (NAME_None = the valley). LEADER ONLY: a member is refused here as well as in
	 *  the UI, because one ticket is one team and a second ticket would split the squad.
	 *
	 *  THE MAP IS PART OF THE QUEUE, not a preference: the server runs one queue per mode x region x map
	 *  (api/src/lib/battlesession/maps.js), because a session's host has already opened its level by the
	 *  time anybody else is placed into it. */
	void Join(int32 TeamSize, bool bNightWanted, FName MapId, bool bRankedWanted, FDone Done);
	/** The last Join asked for the RANKED queue (backend migration 035). */
	bool IsRanked() const { return bRanked; }
	/** DELETE /v1/br/queue. Leaves for the WHOLE TEAM (the ticket is the party). Safe to call when not
	 *  queued; it just resets. A member only resets locally - the server refuses them by design. */
	void Leave(FDone Done = nullptr);
	/** GET /v1/br/queue/me. Called by the fallback timer and once on entering the overlay. */
	void Refresh(FDone Done = nullptr);
	/** The one read that runs while this phone thinks it is IDLE: a member whose `bs.placed` frame was
	 *  lost (app restarted, socket down past the event's expiry) has no other way to learn it was
	 *  placed. Throttled, because the tunnel is metered - see the note at the top of this file.
	 *  bForce skips the throttle for the one case that cannot wait: the server has just told us we are
	 *  in a session (BR_ALREADY_IN_MATCH) and the only way out is to read it and walk in. */
	void ColdCheck(bool bForce = false);
	/** THE SEAL, from whichever door it arrived through - the `bs.sealed` socket event or the body of a
	 *  heartbeat response. Reads {session_id, humans, bots, countdown_s} DEFENSIVELY (bare, or wrapped
	 *  in `battle_session` / `match`), ignores a seal for a session this phone is not in, and broadcasts
	 *  OnSessionSealed exactly once per session. Confirming the field names is all the wiring left. */
	void TakeSeal(const TSharedPtr<FJsonObject>& Json);
	/** The travel failed, so the session we already hold must be allowed to fire OnMatched again.
	 *  Without this a player whose EOS login dropped on the way in is stuck in the lobby, queued out
	 *  of a session they are still a member of. */
	void AllowRetry();

	ECampusBRState GetState() const { return State; }
	/** The session we hold has been SEALED: joins are closed, cancelling is refused (409
	 *  BS_SESSION_STARTED) and the countdown is the server's promise. */
	bool IsSealed() const { return Match.bSealed; }
	bool IsBusy() const { return State == ECampusBRState::Joining || State == ECampusBRState::Leaving; }
	bool IsQueued() const { return State == ECampusBRState::Queued || State == ECampusBRState::Joining; }
	bool IsMatched() const { return State == ECampusBRState::Matched; }
	const FCampusBRMatch& GetMatch() const { return Match; }
	/** Seconds since JOIN, counted locally so the timer runs on with the socket down. */
	float GetElapsed() const { return Elapsed; }
	/** The server's estimate, 0 when it did not send one. */
	int32 GetEta() const { return EtaSeconds; }
	int32 GetFound() const { return Found; }
	int32 GetTotalSlots() const { return TotalSlots; }
	/** Bots the session will spawn for the slots no human took. 0 until a session is formed. */
	int32 GetBots() const { return Match.Bots; }
	/** Seconds left on the server's countdown, counted down locally between reads. */
	int32 GetCountdown() const;
	/** BS-SQUAD-IN-000206, or empty while still queued. The one id a player can read out. */
	const FString& GetSessionId() const { return Match.SessionId; }
	/** The queue the player joined, for the overlay's subtitle. */
	int32 GetTeamSize() const { return TeamSize; }
	bool WantsNight() const { return bNight; }
	/** The API map name this ticket is queued on. The overlay prints the SESSION's map once there is
	 *  one; this is what was asked for while there is not. */
	const FString& WantedMapId() const { return WantedMap; }
	/** True when THIS phone runs the listen server for the session we were handed. A dedicated session
	 *  still names a player as host (the result route authorises on it), so the connect type decides:
	 *  on AWS nobody opens a listen server and everybody travels to the address. */
	bool AmHost() const;
	/** True when a party exists and this phone does not lead it: START belongs to the leader. */
	bool MustWaitForLeader() const;
	/** The last refusal worth printing, or empty. */
	const FString& GetError() const { return LastError; }
	/** The "host disconnected" line, taken ONCE. A session that ends while the player is in the match
	 *  level has no lobby to toast at, so the sentence waits here for the lobby that comes next. */
	FString TakeEndedNotice();

	/** Repaint: state, timer bucket or found-count changed. */
	FSimpleMulticastDelegate OnChanged;
	/** A session object arrived (socket or poll), exactly once per session. The lobby travels; nothing
	 *  here does. */
	FCampusBRMatched OnMatched;
	/** bs.ended: the host went quiet and the server settled the session. */
	FCampusBREnded OnSessionEnded;
	/** The backend sealed the session. ONLY THE HOST acts on it (ACampusBRGameMode binds this in
	 *  InitGameState and calls SealSession); on every other phone it is just news. */
	FCampusBRSealed OnSessionSealed;
	/** br.result: the host reported and this is my line of it. */
	FCampusBRScored OnScored;

private:
	UCampusBackend* Api() const;
	UCampusSocial* Live() const;

	/** Reads {state, session_state, eta_s, battle_session} - the body of GET /br/queue/me and of
	 *  POST /br/queue alike. */
	void ReadQueue(const TSharedPtr<FJsonObject>& Json);
	/** Reads one session object and, if its session id is new, fires OnMatched exactly once. */
	void TakeMatch(const TSharedPtr<FJsonObject>& Json);
	/** bs.left: the LEADER cancelled a session that was still filling, so the members go back. */
	void TakeLeft(const TSharedPtr<FJsonObject>& Json);
	/** bs.ended. `requeued` keeps the player on the SEARCHING screen - their ticket is back in the
	 *  queue, and throwing them out to the lobby would make a recovery look like a failure. */
	void TakeEnded(const TSharedPtr<FJsonObject>& Json);
	/** br.result. */
	void TakeResult(const TSharedPtr<FJsonObject>& Json);
	/** Back to IDLE with nothing held: a cancel, a refusal, and the end of a session all land here. */
	void Reset();
	bool Tick(float DeltaTime);
	void StartTimer();
	void StopTimer();

	ECampusBRState State = ECampusBRState::Idle;
	FCampusBRMatch Match;
	int32 TeamSize = 1;
	bool bNight = false;
	/** The API's name for the map this ticket is queued on ("real" / "campus"), resolved once in Join
	 *  from the catalogue id the lobby picked. The valley until a map is asked for. */
	FString WantedMap = TEXT("real");
	/** Ranked BR: its own queue and sessions on the server, rank points on the result. */
	bool bRanked = false;
	int32 EtaSeconds = 0;
	int32 Found = 0;
	int32 TotalSlots = 50;
	float Elapsed = 0.f;
	/** Seconds until the next fallback poll; only counted down while the socket is dead. */
	float PollIn = 0.f;
	/** The last whole second broadcast, so OnChanged fires at 1 Hz and not per frame. */
	int32 ShownSecond = -1;
	/** Session id (or code) already handed to the lobby. THE dedupe: the server emits `bs.placed` AND
	 *  `br.matched` with the same payload, and travelling twice tears the level down mid-load. */
	FString PlacedKey;
	/** Session already SEALED, for the same reason as PlacedKey: the seal arrives on the socket AND on
	 *  the heartbeat response, and one session is sealed once. */
	FString SealedKey;
	/** FPlatformTime::Seconds() of the last ColdCheck, so a reconnect storm cannot spend the quota. */
	double LastColdCheckAt = 0.0;
	/** Engine time the countdown was read at, so GetCountdown can tick between reads. */
	double CountdownReadAt = 0.0;
	FString LastError;
	/** Set by bs.ended, cleared by the first lobby that reads it. */
	FString EndedNotice;
	FDelegateHandle PlacedHandle;
	FDelegateHandle SealedHandle;
	FDelegateHandle LeftHandle;
	FDelegateHandle EndedHandle;
	FDelegateHandle ResultHandle;
	FTSTicker::FDelegateHandle TickHandle;
};

/**
 * THE HOST'S BEAT (POST /v1/br/:handle/heartbeat). Every 5 s while the session is FILLING, every 20 s
 * once it is sealed. Miss enough of them and the server settles the session and sends every player
 * `bs.ended`, so a host whose phone died on the loading screen frees its lobby in minutes.
 *
 * IT STARTS IN THE LOBBY, NOT IN THE MATCH. The server deletes a session nobody has beaten within TEN
 * SECONDS as empty - and ten seconds is less than a level load - so UCampusBRQueue calls Begin() the
 * instant it reads a placement naming this phone as host, long before there is a game mode. The game
 * mode then calls Report() to take ownership and announce the phases it is allowed to announce.
 *
 * It lives in the game instance and not in the game mode because the beat has to outlive the travel.
 * Owner bounds its life ONCE A GAME MODE HAS CLAIMED IT - when the mode goes (the player quit, the
 * level tore down) the next tick stops the beat rather than beating for a match nobody is in. Before
 * that it is bounded by LobbyGraceSeconds: a travel that never lands must not beat forever.
 */
UCLASS()
class UCampusBRHostBeat : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** How many combatants are still up, read at beat time so the count is never stale. */
	using FAliveCount = TFunction<int32()>;

	virtual void Deinitialize() override;

	/** THE LOBBY'S CALL, made the moment this phone learns it is the host (UCampusBRQueue::TakeMatch).
	 *  Beats FILLING immediately - inside the server's ten-second empty-session window - and keeps
	 *  beating through the travel and the level load. No-op when already running. */
	void Begin(const FString& InHandle);

	/** THE GAME MODE'S CALL: claims the beat (InOwner bounds its life from here on) and announces the
	 *  states a host is allowed to announce. Ended stops it. No-op with no handle, with nobody signed in
	 *  (a dedicated server carries no player credential yet - see docs/BATTLE_SESSION.md §3) or off the
	 *  authority. */
	void Report(AActor* InOwner, const FString& InHandle, ECampusBRBeat Beat, FAliveCount InAlive);
	void Stop();

private:
	UCampusBackend* Api() const;
	/** Arms the ticker and sends the first beat. Shared by Begin and Report. */
	void Start(const FString& InHandle);
	void Send();
	/** THE SECOND DOOR FOR THE SEAL: the beat itself can seal the session, so its response carries
	 *  {sealed, humans, bots, countdown_s, starts_at}. There is nothing to disambiguate - a host NEVER
	 *  announces COUNTDOWN (the server answers 400; the seal is the server's alone) - so any sealed body
	 *  is news. Handed to UCampusBRQueue::TakeSeal, which owns the parsing and the dedupe. */
	void TakeBeatState(const TSharedPtr<FJsonObject>& Json);
	bool Tick(float DeltaTime);

	TWeakObjectPtr<AActor> Owner;
	FString Handle;
	FAliveCount Alive;
	/** The last state the SERVER accepted, and the one we want it in. Equal = beat bare. */
	ECampusBRBeat Announced = ECampusBRBeat::Filling;
	ECampusBRBeat Wanted = ECampusBRBeat::Filling;
	/** A game mode has claimed the beat, so Owner is now what bounds its life. False while it is still
	 *  the lobby's beat, where there is no actor to watch. */
	bool bOwned = false;
	bool bSending = false;
	float NextBeatIn = 0.f;
	/** How long the beat has run without an owner. The travel has to land inside this. */
	float UnownedFor = 0.f;
	FTSTicker::FDelegateHandle TickHandle;
};
