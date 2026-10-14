// Replicated match state: teams on the player state, phase / clock / score on the game state.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "CampusMatchState.generated.h"

UENUM()
enum class ECampusTeam : uint8
{
	Red,
	Blue
};

class AController;
class UWorld;

/** THE ONE TEST for "this is the fifty-player battle royale, not the 4v4 room", and the squad
 *  identity every BR rule keys off. Gate anything that says RED or BLUE on IsBattleRoyale and give
 *  BR the squad version instead; never read ACampusPlayerState::Squad directly.
 *
 *  Defined in CampusMatchState.cpp so that the lightest header in the match owns the test and no UI
 *  file has to pull in the whole BR game mode to ask the question. */
namespace CampusBR
{
	/** True when the level is running ACampusBRGameState (ACampusBRGameMode's state class). */
	bool IsBattleRoyale(const UWorld* World);

	/** 1-based squad, or 0 for "no squad" - a room match, a state that has not replicated, or a
	 *  combatant the mode does not know. ZERO IS FRIENDLY WITH NOBODY, on purpose: a default must
	 *  never put two solo players on one team. */
	int32 GetSquadId(const APlayerState* PlayerState);

	/** Same squad, and that squad is a real one. Everyone else is an enemy - in SOLO that is every
	 *  other player and every bot, because each of them is their own squad of one. */
	bool AreSquadmates(const APlayerState* First, const APlayerState* Second);

	/** THE ONE "do these two hurt each other" test, for both game types and every weapon that asks:
	 *  bullets, roadkill and a vehicle going up. ROOM: the two RED/BLUE colours, exactly as before.
	 *  BR: the SQUAD - through the game mode when there is one, because only it knows the bot squads
	 *  and the truce before the drop, and through the replicated squad otherwise. */
	bool AreFriendly(const UWorld* World, const AController* First, const AController* Second);
}

UENUM()
enum class ECampusPhase : uint8
{
	Waiting,
	Playing,
	Ended,
	/** Between two rounds (the round result is shown, then everyone respawns for the next one). */
	RoundOver
};

UCLASS()
class ACampusPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	ECampusTeam Team = ECampusTeam::Red;

	/** BATTLE ROYALE ONLY (ACampusBRGameMode owns it, replicated so a client can colour its own
	 *  squadmates and draw the squad scoreboard). 1-based; 0 means NO SQUAD and is friendly with
	 *  NOBODY - which is what a state that has not replicated yet, and the whole ROOM 4v4 path, read
	 *  as. The room mode never writes it and never reads it: there the two colours in Team are the
	 *  teams, exactly as before. Always ask through CampusBR::GetSquadId. */
	UPROPERTY(Replicated)
	int32 Squad = 0;

	/** BATTLE ROYALE ONLY: finishing place, written the moment this player is eliminated (1 = won).
	 *  0 = still alive, which is also what every room match reads. */
	UPROPERTY(Replicated)
	int32 Placement = 0;

	/** BATTLE ROYALE ONLY: the SQUAD's finishing place, out of ACampusBRGameState::TotalSquads.
	 *
	 *  Placement above counts COMBATANTS (a solo player dying 37th out of 50); this counts TEAMS, and
	 *  it is the number the result screen shows as "#3 / 13" because it is the one the whole squad
	 *  shares. Written to every member the moment the squad is wiped, and to the winners at the end.
	 *  0 = the squad was still standing. In SOLO a squad is one player, so the two agree by nature. */
	UPROPERTY(Replicated)
	int32 SquadPlacement = 0;

	/** Server only: the account the API admitted for this connection, and the ticket it came with. */
	FString AccountId;
	FString JoinTicket;

	/** Server only, BATTLE ROYALE: the session's team id this connection travelled in with ("?Squad=").
	 *  ONE TEAM ID IS ONE SQUAD - it is how two phones in the same party end up on the same squad and,
	 *  just as importantly, how two strangers who queued separately never do. Empty = a team of one. */
	FString SquadKey;

	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;

	/** Totals for this room (all rounds so far), for the scoreboard. */
	UPROPERTY(Replicated)
	float DamageDealt = 0.f;

	UPROPERTY(Replicated)
	float DamageTaken = 0.f;

	/** Hits that landed on an enemy's HEAD (zone 1 in ACampusCharacter::ServerFire). Counted on the
	 *  server, where the zone is decided, so a client cannot inflate it. */
	UPROPERTY(Replicated)
	int32 Headshots = 0;

	/** Centimetres this player has actually moved under their own power, summed on the server from
	 *  the pawn's own position. Teleports (the drop, a respawn, a zipline hand-off) jump further in
	 *  one frame than anyone can run, so they are discarded rather than counted - see
	 *  ACampusCharacter::TickDistance. The result screen divides by 100000 for kilometres. */
	UPROPERTY(Replicated)
	float DistanceCm = 0.f;

	/** Seconds this player lasted in the match: written when they are eliminated, and at the final
	 *  whistle for everybody still standing. 0 until the match ends one way or the other. */
	UPROPERTY(Replicated)
	float SurvivalSeconds = 0.f;

	UPROPERTY(Replicated)
	bool bIsHost = false;
};

UCLASS()
class ACampusGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Kill feed lines, newest last. Sent to everyone; each client keeps the last few. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFeed(const FString& Line);

	int32 GetScore(ECampusTeam Team) const { return Team == ECampusTeam::Red ? RedScore : BlueScore; }

	UPROPERTY(Replicated)
	ECampusPhase Phase = ECampusPhase::Waiting;

	/** Server world time when the match ends; clients derive the countdown from it. */
	UPROPERTY(Replicated)
	float EndTime = 0.f;

	/** ROUND WINS of each team (a round = elimination; the match = every picked round, see ACampusMatchGameMode). */
	UPROPERTY(Replicated)
	int32 RedScore = 0;

	UPROPERTY(Replicated)
	int32 BlueScore = 0;

	/** Current round (1-based), rounds picked by the host (4-7), round length in minutes. */
	UPROPERTY(Replicated)
	int32 Round = 0;

	UPROPERTY(Replicated)
	int32 Rounds = 5;

	UPROPERTY(Replicated)
	int32 RoundMinutes = 5;

	/** The extra round played when the picked rounds end in a tie. */
	UPROPERTY(Replicated)
	bool bDecider = false;

	/** Winner of the round that just ended: 0 red, 1 blue, -1 draw (shown during RoundOver). */
	UPROPERTY(Replicated)
	int32 LastRoundWinner = -1;

	UPROPERTY(Replicated)
	int32 KillsToWin = 10;

	/** Set when the match ends: 0 red, 1 blue, -1 draw. */
	UPROPERTY(Replicated)
	int32 Winner = -1;

	UPROPERTY(Replicated)
	FString RoomCode;

	/** From the host's lobby choice: "Classic" or "RealMap", and 1 / 2 / 4 players per team. */
	UPROPERTY(Replicated)
	FName MatchMap = TEXT("Classic");

	UPROPERTY(Replicated)
	int32 TeamSize = 4;

	/** Room limit for this map and team size (Real Map: 50, or 48 for squads). */
	UPROPERTY(Replicated)
	int32 MaxPlayers = 8;

	TArray<FString> Feed;
};
