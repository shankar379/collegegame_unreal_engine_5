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
	Ended
};

UCLASS()
class ACampusPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	ECampusTeam Team = ECampusTeam::Red;

	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Deaths = 0;

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

	UPROPERTY(Replicated)
	int32 RedScore = 0;

	UPROPERTY(Replicated)
	int32 BlueScore = 0;

	UPROPERTY(Replicated)
	int32 KillsToWin = 10;

	/** Set when the match ends: 0 red, 1 blue, -1 draw. */
	UPROPERTY(Replicated)
	int32 Winner = -1;

	UPROPERTY(Replicated)
	FString RoomCode;

	TArray<FString> Feed;
};
