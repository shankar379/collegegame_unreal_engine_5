#include "Campus/CampusMatchState.h"

#include "Net/UnrealNetwork.h"

void ACampusPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusPlayerState, Team);
	DOREPLIFETIME(ACampusPlayerState, Kills);
	DOREPLIFETIME(ACampusPlayerState, Deaths);
	DOREPLIFETIME(ACampusPlayerState, DamageDealt);
	DOREPLIFETIME(ACampusPlayerState, DamageTaken);
	DOREPLIFETIME(ACampusPlayerState, bIsHost);
}

void ACampusGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusGameState, Phase);
	DOREPLIFETIME(ACampusGameState, EndTime);
	DOREPLIFETIME(ACampusGameState, RedScore);
	DOREPLIFETIME(ACampusGameState, BlueScore);
	DOREPLIFETIME(ACampusGameState, Round);
	DOREPLIFETIME(ACampusGameState, Rounds);
	DOREPLIFETIME(ACampusGameState, RoundMinutes);
	DOREPLIFETIME(ACampusGameState, bDecider);
	DOREPLIFETIME(ACampusGameState, LastRoundWinner);
	DOREPLIFETIME(ACampusGameState, KillsToWin);
	DOREPLIFETIME(ACampusGameState, Winner);
	DOREPLIFETIME(ACampusGameState, RoomCode);
	DOREPLIFETIME(ACampusGameState, MatchMap);
	DOREPLIFETIME(ACampusGameState, TeamSize);
	DOREPLIFETIME(ACampusGameState, MaxPlayers);
}

void ACampusGameState::MulticastFeed_Implementation(const FString& Line)
{
	Feed.Add(Line);
	if (Feed.Num() > 4)
	{
		Feed.RemoveAt(0);
	}
}
