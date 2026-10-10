// Two game modes, one per level: the lobby (UI only, no pawn) and the match
// (teams, waiting room, timed round, kills to win, respawn).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CampusGameMode.generated.h"

class ACampusGameState;
class ACampusPlayerState;

UCLASS()
class ACampusLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACampusLobbyGameMode();

	/** The lobby has no playable pawn; the controller shows the character on a podium instead. */
	virtual void RestartPlayer(AController* NewPlayer) override {}
};

UCLASS()
class ACampusMatchGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACampusMatchGameMode();

	virtual void InitGameState() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Host only, from the waiting room. */
	void RequestStart(APlayerController* Requester);

	/** Waiting room only. */
	void SwitchTeam(APlayerController* Requester);

	/** Called by a character when its health reaches zero. */
	void OnKilled(AController* Victim, AController* Killer);

	UPROPERTY(EditAnywhere, Category="Match")
	float MatchSeconds = 300.f;

	UPROPERTY(EditAnywhere, Category="Match")
	int32 KillsToWin = 10;

	UPROPERTY(EditAnywhere, Category="Match")
	float RespawnSeconds = 3.f;

private:
	ACampusGameState* GetCampusState() const;
	FTransform SpawnTransformFor(const ACampusPlayerState* PlayerState) const;
	void CheckClock();
	void EndRound();

	FTimerHandle ClockTimer;
};
