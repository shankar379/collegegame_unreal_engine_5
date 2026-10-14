#include "Campus/CampusMatchState.h"

#include "Campus/CampusBRGameMode.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"

namespace CampusBR
{
	bool IsBattleRoyale(const UWorld* World)
	{
		// The GAME STATE, not the game mode: a client has no game mode, and every one of the places
		// this gates is drawn on a client.
		return World && World->GetGameState<ACampusBRGameState>() != nullptr;
	}

	int32 GetSquadId(const APlayerState* PlayerState)
	{
		const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(PlayerState);
		return Campus ? Campus->Squad : 0;
	}

	bool AreSquadmates(const APlayerState* First, const APlayerState* Second)
	{
		const int32 Mine = GetSquadId(First);
		// Squad 0 is nobody's squadmate: that is what keeps a solo match - and a squad field that has
		// not arrived yet - from reading as one giant team.
		return Mine != 0 && Mine == GetSquadId(Second);
	}

	bool AreFriendly(const UWorld* World, const AController* First, const AController* Second)
	{
		if (IsBattleRoyale(World))
		{
			// The game mode first: it is the only thing that knows the bot squads and that the waiting
			// platform is a lobby, not a map. Without it (a client asking) the replicated squad is the
			// same answer, and squad 0 is friendly with nobody.
			if (const ACampusBRGameMode* Mode = World->GetAuthGameMode<ACampusBRGameMode>())
			{
				return Mode->AreFriendly(First, Second);
			}
			return First == Second
				|| AreSquadmates(First ? First->PlayerState : nullptr, Second ? Second->PlayerState : nullptr);
		}
		// THE ROOM PATH, UNCHANGED: two known colours that match. Anything unknown is an ENEMY, which
		// is exactly what every call site has always done with a missing player state.
		const ACampusPlayerState* Mine = First ? First->GetPlayerState<ACampusPlayerState>() : nullptr;
		const ACampusPlayerState* Theirs = Second ? Second->GetPlayerState<ACampusPlayerState>() : nullptr;
		return Mine && Theirs && Mine->Team == Theirs->Team;
	}
}

void ACampusPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusPlayerState, Team);
	DOREPLIFETIME(ACampusPlayerState, Squad);
	DOREPLIFETIME(ACampusPlayerState, Placement);
	DOREPLIFETIME(ACampusPlayerState, SquadPlacement);
	DOREPLIFETIME(ACampusPlayerState, Kills);
	DOREPLIFETIME(ACampusPlayerState, Deaths);
	DOREPLIFETIME(ACampusPlayerState, DamageDealt);
	DOREPLIFETIME(ACampusPlayerState, DamageTaken);
	DOREPLIFETIME(ACampusPlayerState, Headshots);
	DOREPLIFETIME(ACampusPlayerState, DistanceCm);
	DOREPLIFETIME(ACampusPlayerState, SurvivalSeconds);
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
