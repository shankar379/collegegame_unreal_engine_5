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
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The travel options that pick this mode, for a lobby that has a BR button.
	 *  Returns "" for every other map choice, so the call is safe to make unconditionally. */
	static FString HostOptionsFor(const FString& MapChoice);

	/** The bot module registers its factory here (see ICampusBRBotFactory). Optional. */
	static void SetBotFactory(UObject* Factory);

	/** Called by ACampusCharacter::ApplyHit when a pawn's health hits zero. */
	void OnKilled(AController* Victim, AController* Killer);

	/** Squadmates (and everyone at all, before the match starts) never damage each other.
	 *  Knows about bot squads too, which a player state alone could not. */
	bool AreFriendly(const AController* First, const AController* Second) const;

	/** Squad of a controller, or 0 when it is not in this match. */
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

	/** Hard cap on the gather: a queue that feels like a match, not a lobby. */
	UPROPERTY(EditAnywhere, Category="BR")
	float WaitingSeconds = 25.f;

	UPROPERTY(EditAnywhere, Category="BR")
	float CountdownSeconds = 10.f;

private:
	ACampusBRGameState* GetBRState() const;

	void EnterPhase(ECampusBRPhase NewPhase);
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
	/** The next squad with a free seat, filling squads in order. */
	int32 NextSquadWithRoom() const;

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
	float MatchStartTime = 0.f;
	float NextVirtualTime = 0.f;
	int32 NextPlacement = 0;
	FTimerHandle PhaseTimer;
};
