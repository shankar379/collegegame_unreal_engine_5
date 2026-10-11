// Two game modes, one per level: the lobby (UI only, no pawn) and the match
// (teams, waiting room, timed round, kills to win, respawn).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Campus/CampusMatchState.h"
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
	virtual void Logout(AController* Exiting) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Host only: room fill + started state to the API (UCampusBackend heartbeat). */
	void ReportRoom();

	/** Host only, from the waiting room. */
	void RequestStart(APlayerController* Requester);

	/** Waiting room only. */
	void SwitchTeam(APlayerController* Requester);

	/** Called by a character when its health reaches zero. */
	void OnKilled(AController* Victim, AController* Killer);

	/** Removes the dead pawn now and brings the player back after RespawnSeconds. */
	void RespawnLater(AController* Victim);

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
	/** Ends the whole match (after every picked round, + a decider on a tie). */
	void EndRound();

	// ---- rounds (room option ?Rounds=4..7?Minutes=3..10): every picked round is always played; a round is
	// elimination (no respawn until the next round); after the last round the team with more round wins takes
	// the match, a tie plays one deciding round.
	void StartRound(int32 Round, bool bDecider);
	/** Ends the round if a team is wiped out (or bForce: the round timer ran out). */
	void CheckRoundOver(bool bForce);
	void FinishRound(int32 Winner);
	void NextRoundOrEnd();
	int32 AliveOn(ECampusTeam Team, int32* OutPlayers = nullptr) const;
	int32 RedRoundKills = 0;
	int32 BlueRoundKills = 0;
	FTimerHandle BreakTimer;
	/** Pause between rounds (round result on screen, then everyone respawns). */
	float RoundBreakSeconds = 6.f;

	FTimerHandle ClockTimer;
};
