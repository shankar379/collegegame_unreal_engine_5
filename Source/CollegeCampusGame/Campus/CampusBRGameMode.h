// Battle royale: 50 combatants, one circle, last squad standing.
// Selected with the travel option "?Mode=BR" (see HostOptionsFor, which also hands the engine the
// "?game=" it needs to pick this class). The TDM mode in CampusGameMode.* is untouched.
//
// THE 50 ARE NOT 50 PAWNS. A phone hosting this match can afford about eight human connections and
// ten AI pawns, so the roster (Docs/BATTLE_ROYALE_DESIGN.md §3.3) is hybrid:
//   human     - a real connection, up to MaxHumans.
//   physical  - a real AI pawn, up to MaxPhysicalBots alive at once.
//   virtual   - a row in an array. Drifts toward the circle at 2 Hz, dies on the attrition curve,
//               writes to the kill feed and the ALIVE counter, and is PROMOTED to a physical bot when
//               a human comes near and the budget has room. Never exists in the last two circles.
//
// The game state derives from ACampusGameState on purpose: the existing HUD, minimap, scoreboard and
// win screen keep working with no changes, and BR only adds what is genuinely new.

#pragma once

#include "CoreMinimal.h"
#include "Campus/CampusMatchState.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/Interface.h"
#include "CampusBRGameMode.generated.h"

class ACampusBRPlane;
class ACampusBRWaitingArea;
class ACampusBRZone;
class ACampusCharacter;

UENUM()
enum class ECampusBRPhase : uint8
{
	/** Gathering on the waiting platform: free movement, no damage. */
	Waiting,
	/** Replicated ten-second countdown before the plane. */
	Countdown,
	/** The plane is crossing the map; players drop when they choose. */
	Flight,
	Playing,
	Ended
};

/** What the bot module is asked for. The AI agent's ACampusBotController module registers an object
 *  implementing this with ACampusBRGameMode::SetBotFactory; without one, the mode falls back to
 *  looking the controller class up by name. Either way BR runs. */
UINTERFACE(MinimalAPI)
class UCampusBRBotFactory : public UInterface
{
	GENERATED_BODY()
};

class ICampusBRBotFactory
{
	GENERATED_BODY()

public:
	/** Spawn Count bots at Where[i] on squad Squads[i]; append every controller made to OutControllers. */
	virtual void SpawnBots(int32 Count, const TArray<int32>& Squads, const TArray<FVector>& Where,
		TArray<AController*>& OutControllers) = 0;
};

UCLASS()
class ACampusBRGameState : public ACampusGameState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	ECampusBRPhase BRPhase = ECampusBRPhase::Waiting;

	/** Server world time this phase ends (the waiting timeout, then the countdown). */
	UPROPERTY(Replicated)
	float BRPhaseEndTime = 0.f;

	/** Every combatant counted, however it is simulated. */
	UPROPERTY(Replicated)
	int32 AliveCombatants = 0;

	UPROPERTY(Replicated)
	int32 TotalCombatants = 0;

	UPROPERTY(Replicated)
	int32 AliveSquads = 0;

	/** The winning squad number once the match ends, else -1. */
	UPROPERTY(Replicated)
	int32 WinningSquad = -1;

	UPROPERTY(Replicated)
	int32 HumansJoined = 0;

	UPROPERTY(Replicated)
	TObjectPtr<ACampusBRZone> Zone = nullptr;

	/** How many humans this host will take before it starts filling with bots. */
	UPROPERTY(Replicated)
	int32 HumanCap = 8;

	/** BS-SQUAD-IN-000731: the battle session this level is running, from the host's "?Session=" travel
	 *  option. Empty in editor play and in a custom room. The HUD prints it so a player can read it out;
	 *  no gameplay keys off it. */
	UPROPERTY(Replicated)
	FString SessionId;
};

/** One of the fifty. Humans and physical bots carry a controller; virtual ones are only this row. */
USTRUCT()
struct FCampusBRCombatant
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	int32 Squad = 0;

	UPROPERTY()
	TWeakObjectPtr<AController> Controller;

	UPROPERTY()
	bool bAlive = true;

	UPROPERTY()
	bool bHuman = false;

	/** True once it owns a real pawn (a human always does while alive). */
	UPROPERTY()
	bool bPhysical = false;

	/** Finishing place, set the moment it dies (1 = won). */
	UPROPERTY()
	int32 Placement = 0;

	/** Virtual only: where it is pretending to be, so promotion can place a pawn there. */
	UPROPERTY()
	FVector Where = FVector::ZeroVector;
};

UCLASS()
class ACampusBRGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACampusBRGameMode();

	virtual void InitGameState() override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
		FString& ErrorMessage) override;
	/** Only to catch the joining URL's "?Squad=" (the session's team id) before PostLogin seats the
	 *  player: by then the options are gone and the squad would have to be guessed from join order. */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
		const FString& Options, const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** THE ONE SEAM THE BACKEND PULLS (Docs/BATTLE_SESSION_DESIGN.md §4). It closes joins, fills the
	 *  remaining slots with bots exactly as the old local timeout did, and starts the countdown to the
	 *  plane. Bound to UCampusBRQueue::OnSessionSealed on the host, so wiring the real event is a few
	 *  lines in the subsystem and nothing here.
	 *
	 *  IDEMPOTENT: the seal arrives twice by design (the socket's `bs.sealed` AND the heartbeat
	 *  response), and the second one is a confirmation, not a second match.
	 *
	 *  Humans is what the server PLACED and is advisory - THE SEAL COUNTS WHO IS PRESENT, so a placed
	 *  player whose travel failed costs nobody their match; Bots fills the rest of the fifty. A
	 *  CountdownSeconds of zero or less keeps this mode's own ten. */
	void SealSession(int32 Humans, int32 Bots, float InCountdownSeconds);

	/** Joins are closed and the roster is fixed. */
	bool IsSealed() const { return bSealed; }

	/** The travel options that pick this mode, for a lobby that has a BR button.
	 *  Returns "" for every other map choice, so the call is safe to make unconditionally. */
	static FString HostOptionsFor(const FString& MapChoice);

	/** The bot module registers its factory here (see ICampusBRBotFactory). Optional. */
	static void SetBotFactory(UObject* Factory);

	/** Called by ACampusCharacter::ApplyHit when a pawn's health hits zero. */
	void OnKilled(AController* Victim, AController* Killer);

	/** Squadmates (and everyone at all, before the match starts) never damage each other. EVERYONE
	 *  ELSE IS AN ENEMY: in solo that is every other player and every bot, because each of them is
	 *  their own squad of one. Knows about bot squads too, which a player state alone could not. */
	bool AreFriendly(const AController* First, const AController* Second) const;

	/** Squad of a controller, or 0 when it is not in this match. Squad 0 is friendly with nobody. */
	int32 SquadOf(const AController* Controller) const;

	/** Host only: the waiting room's START, exactly like TDM's. */
	void RequestStart(APlayerController* Requester);

	// ---- tuning (Docs/BATTLE_ROYALE_DESIGN.md §3.3 / §3.4)
	UPROPERTY(EditAnywhere, Category="BR")
	int32 MaxHumans = 8;

	UPROPERTY(EditAnywhere, Category="BR")
	int32 MaxCombatants = 50;

	UPROPERTY(EditAnywhere, Category="BR")
	int32 MaxPhysicalBots = 10;

	/** The BACKEND's fill cap, mirrored here only so the safety net below knows when the seal is late.
	 *  This mode does NOT start a countdown on it: the server decides the seal (design §4). */
	UPROPERTY(EditAnywhere, Category="BR")
	float WaitingSeconds = 25.f;

	/** How long past the fill cap the mode waits for a seal before sealing itself. THE SAFETY NET: a
	 *  lost `bs.sealed` and a heartbeat that 500s must not leave a player standing on a slab forever. */
	UPROPERTY(EditAnywhere, Category="BR")
	float SealGraceSeconds = 15.f;

	UPROPERTY(EditAnywhere, Category="BR")
	float CountdownSeconds = 10.f;

private:
	ACampusBRGameState* GetBRState() const;

	void EnterPhase(ECampusBRPhase NewPhase);
	/** Humans actually connected right now - not the ones the matchmaker placed. */
	int32 HumansPresent() const;
	/** The safety net fired: no seal arrived in time, so this host seals its own session. */
	void SealLocally();
	void FillWithBots();
	void StartFlight();
	void BeginPlaying();
	void EndMatch();

	/** 2 Hz: drift the virtual combatants, run the attrition curve, promote near humans. */
	void TickVirtual();
	/** Alive count the attrition curve wants at this point in the match. */
	int32 TargetAlive(float SecondsIntoMatch) const;
	/** Server: a virtual combatant dies with a plausible killer in the feed. */
	void KillVirtual(int32 Index);
	/** Server: turn a virtual row into a real AI pawn where it stands. */
	bool Promote(int32 Index);
	/** A virtual row carries a 2D map position with no height, so standing it up needs the ground
	 *  under that position. Returns a spawn point one capsule above whatever is actually there. */
	FVector GroundedSpawn(const FVector& Where) const;
	/** How many physical bots are alive right now. */
	int32 PhysicalBotsAlive() const;
	/** The virtual simulation is switched off for the last two circles: no ghosts in the endgame. */
	bool VirtualAllowed() const;

	void Eliminate(int32 Index, const FString& KillerName);
	/** Alive combatants and distinct alive squads, written to the game state for the HUD. */
	void RefreshCounts();
	/** Someone still alive on the same squad, for the elimination camera. */
	APawn* LivingSquadmatePawn(int32 Squad, const AController* Except) const;

	int32 IndexOf(const AController* Controller) const;
	/** A HUMAN's squad, from the session's team id it travelled in with. One team id is one squad, so
	 *  a party of two lands together - and two strangers who queued separately never do, because they
	 *  carry different ids. SOLO (SquadSize 1) and an empty key are always a team of one. */
	int32 SquadForKey(const FString& Key);
	/** How many combatants are already seated on Squad. */
	int32 SeatsTaken(int32 Squad) const;

	/** POST /v1/br/:code/result. Fire and forget: a 404 from a server that has not shipped the
	 *  endpoint yet must never hold up the win screen. */
	void ReportResult();

	UPROPERTY(Transient)
	TArray<FCampusBRCombatant> Combatants;

	UPROPERTY(Transient)
	TObjectPtr<ACampusBRWaitingArea> WaitingArea;

	UPROPERTY(Transient)
	TObjectPtr<ACampusBRPlane> Plane;

	UPROPERTY(Transient)
	TObjectPtr<ACampusBRZone> ZoneActor;

	/** 1 / 2 / 4, from "?Team=". */
	int32 SquadSize = 4;
	/** Squads are handed out 1, 2, 3 ... and NEVER reused: squad 0 stays the "no squad" value, and a
	 *  number that has been given out is never given to a second, unrelated party. */
	int32 NextSquadNumber = 1;
	/** Session team id -> squad number, so every phone on one ticket gets the same squad. */
	TMap<FString, int32> SquadByKey;
	float MatchStartTime = 0.f;
	float NextVirtualTime = 0.f;
	int32 NextPlacement = 0;
	/** Set by SealSession: joins are refused and the countdown owns the clock. */
	bool bSealed = false;
	FTimerHandle PhaseTimer;
	/** Kept apart from PhaseTimer on purpose - EnterPhase clears that one, and the safety net has to
	 *  survive every phase change until a seal actually lands. */
	FTimerHandle SealSafetyTimer;
	/** UCampusBRQueue::OnSessionSealed, taken off in EndPlay: the subsystem outlives this level. */
	FDelegateHandle SealedHandle;
};
