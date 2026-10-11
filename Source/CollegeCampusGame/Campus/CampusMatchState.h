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

	/** Server only: the account the API admitted for this connection, and the ticket it came with. */
	FString AccountId;
	FString JoinTicket;

	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;

	/** Totals for this room (all rounds so far), for the scoreboard. */
	UPROPERTY(Replicated)
	float DamageDealt = 0.f;

	UPROPERTY(Replicated)
	float DamageTaken = 0.f;

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
