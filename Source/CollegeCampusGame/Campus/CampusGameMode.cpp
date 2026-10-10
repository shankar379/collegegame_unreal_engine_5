#include "Campus/CampusGameMode.h"

#include "Campus/CampusCharacter.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusPlayerControllers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

ACampusLobbyGameMode::ACampusLobbyGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ACampusLobbyPlayerController::StaticClass();
}

ACampusMatchGameMode::ACampusMatchGameMode()
{
	DefaultPawnClass = ACampusCharacter::StaticClass();
	PlayerControllerClass = ACampusMatchPlayerController::StaticClass();
	PlayerStateClass = ACampusPlayerState::StaticClass();
	GameStateClass = ACampusGameState::StaticClass();
}

ACampusGameState* ACampusMatchGameMode::GetCampusState() const
{
	return GetGameState<ACampusGameState>();
}

void ACampusMatchGameMode::InitGameState()
{
	Super::InitGameState();
	if (ACampusGameState* State = GetCampusState())
	{
		static const TCHAR Letters[] = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
		FString Code;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Code.AppendChar(Letters[FMath::RandRange(0, UE_ARRAY_COUNT(Letters) - 2)]);
		}
		State->RoomCode = Code;
		State->KillsToWin = KillsToWin;
	}
}

void ACampusMatchGameMode::PostLogin(APlayerController* NewPlayer)
{
	// Team and host flag must be set before the pawn spawns at its team's side.
	if (ACampusPlayerState* PlayerState = NewPlayer->GetPlayerState<ACampusPlayerState>())
	{
		int32 Red = 0;
		int32 Blue = 0;
		bool bHasHost = false;
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			if (const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Other); Campus && Campus != PlayerState)
			{
				(Campus->Team == ECampusTeam::Red ? Red : Blue)++;
				bHasHost |= Campus->bIsHost;
			}
		}
		PlayerState->Team = Red <= Blue ? ECampusTeam::Red : ECampusTeam::Blue;
		PlayerState->bIsHost = !bHasHost;
	}
	Super::PostLogin(NewPlayer);
}

FTransform ACampusMatchGameMode::SpawnTransformFor(const ACampusPlayerState* PlayerState) const
{
	int32 Slot = 0;
	const ECampusTeam Team = PlayerState ? PlayerState->Team : ECampusTeam::Red;
	if (PlayerState)
	{
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			if (Other == PlayerState)
			{
				break;
			}
			if (const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Other); Campus && Campus->Team == Team)
			{
				++Slot;
			}
		}
	}
	// Teams face each other across the floor; slots fan out sideways.
	const float Side = Team == ECampusTeam::Red ? -1.f : 1.f;
	const float Lateral = (Slot % 2 == 0 ? 1.f : -1.f) * 200.f * ((Slot + 1) / 2);
	const FVector Location(1200.f * Side, Lateral, 120.f);
	const FRotator Facing(0.f, Team == ECampusTeam::Red ? 0.f : 180.f, 0.f);
	return FTransform(Facing, Location);
}

void ACampusMatchGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer || NewPlayer->IsPendingKillPending())
	{
		return;
	}
	const ACampusGameState* State = GetCampusState();
	if (State && State->Phase == ECampusPhase::Ended)
	{
		return;
	}
	RestartPlayerAtTransform(NewPlayer, SpawnTransformFor(NewPlayer->GetPlayerState<ACampusPlayerState>()));
}

void ACampusMatchGameMode::RequestStart(APlayerController* Requester)
{
	ACampusGameState* State = GetCampusState();
	const ACampusPlayerState* PlayerState = Requester ? Requester->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!State || !PlayerState || !PlayerState->bIsHost || State->Phase != ECampusPhase::Waiting)
	{
		return;
	}

	State->Phase = ECampusPhase::Playing;
	State->EndTime = GetWorld()->GetTimeSeconds() + MatchSeconds;
	State->RedScore = 0;
	State->BlueScore = 0;
	State->Winner = -1;

	// Everyone back to their team's side for a clean start.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Player = It->Get())
		{
			if (APawn* Pawn = Player->GetPawn())
			{
				Pawn->Destroy();
			}
			RestartPlayer(Player);
		}
	}
	State->MulticastFeed(TEXT("Match started"));
	GetWorldTimerManager().SetTimer(ClockTimer, this, &ACampusMatchGameMode::CheckClock, 0.5f, true);
}

void ACampusMatchGameMode::SwitchTeam(APlayerController* Requester)
{
	const ACampusGameState* State = GetCampusState();
	ACampusPlayerState* PlayerState = Requester ? Requester->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (!State || !PlayerState || State->Phase != ECampusPhase::Waiting)
	{
		return;
	}
	PlayerState->Team = PlayerState->Team == ECampusTeam::Red ? ECampusTeam::Blue : ECampusTeam::Red;
	if (APawn* Pawn = Requester->GetPawn())
	{
		Pawn->Destroy();
	}
	RestartPlayer(Requester);
}

void ACampusMatchGameMode::OnKilled(AController* Victim, AController* Killer)
{
	ACampusGameState* State = GetCampusState();
	if (!State || State->Phase != ECampusPhase::Playing)
	{
		return;
	}

	ACampusPlayerState* VictimState = Victim ? Victim->GetPlayerState<ACampusPlayerState>() : nullptr;
	ACampusPlayerState* KillerState = Killer ? Killer->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (VictimState)
	{
		++VictimState->Deaths;
	}
	if (KillerState && KillerState != VictimState)
	{
		++KillerState->Kills;
		(KillerState->Team == ECampusTeam::Red ? State->RedScore : State->BlueScore)++;
	}
	State->MulticastFeed(FString::Printf(TEXT("%s  >  %s"),
		KillerState ? *KillerState->GetPlayerName() : TEXT("?"),
		VictimState ? *VictimState->GetPlayerName() : TEXT("?")));

	if (State->RedScore >= KillsToWin || State->BlueScore >= KillsToWin)
	{
		EndRound();
		return;
	}

	if (Victim)
	{
		if (APawn* Pawn = Victim->GetPawn())
		{
			Pawn->Destroy();
		}
		FTimerHandle Respawn;
		TWeakObjectPtr<AController> WeakVictim(Victim);
		GetWorldTimerManager().SetTimer(Respawn, FTimerDelegate::CreateWeakLambda(this, [this, WeakVictim]()
		{
			if (AController* Controller = WeakVictim.Get())
			{
				RestartPlayer(Controller);
			}
		}), RespawnSeconds, false);
	}
}

void ACampusMatchGameMode::CheckClock()
{
	const ACampusGameState* State = GetCampusState();
	if (State && State->Phase == ECampusPhase::Playing && GetWorld()->GetTimeSeconds() >= State->EndTime)
	{
		EndRound();
	}
}

void ACampusMatchGameMode::EndRound()
{
	ACampusGameState* State = GetCampusState();
	if (!State || State->Phase == ECampusPhase::Ended)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(ClockTimer);
	State->Phase = ECampusPhase::Ended;
	State->Winner = State->RedScore == State->BlueScore ? -1 : (State->RedScore > State->BlueScore ? 0 : 1);
	State->MulticastFeed(TEXT("Match over"));
}
