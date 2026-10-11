#include "Campus/CampusGameMode.h"

#include "Campus/CampusBackend.h"
#include "Campus/CampusCharacter.h"
#include "Campus/CampusDoor.h"
#include "EngineUtils.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusPlayerControllers.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "Kismet/GameplayStatics.h"
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
		// "?Room=ABC123": the code the API issued for this room (ACampusLobbyPlayerController::HostRoom); a local
		// code only when hosting without the API (editor play)
		FString Code = UGameplayStatics::ParseOption(OptionsString, TEXT("Room")).ToUpper();
		if (Code.IsEmpty())
		{
			static const TCHAR Letters[] = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
			for (int32 Index = 0; Index < 6; ++Index)
			{
				Code.AppendChar(Letters[FMath::RandRange(0, UE_ARRAY_COUNT(Letters) - 2)]);
			}
		}
		State->RoomCode = Code;
		State->KillsToWin = KillsToWin;

		// "?Map=RealMap?Team=2" from the host's lobby (ACampusLobbyPlayerController::HostRoom).
		// "?Rounds=5?Minutes=5": picked by the host when creating the room (GAME_PLAN 1.2, three.js lobby: 4-7 rounds,
		// 3 / 5 / 7 / 10 minutes each)
		State->Rounds = FMath::Clamp(UGameplayStatics::GetIntOption(OptionsString, TEXT("Rounds"), 5), 4, 7);
		State->RoundMinutes = FMath::Clamp(UGameplayStatics::GetIntOption(OptionsString, TEXT("Minutes"), 5), 3, 10);
		const FString MapOption = UGameplayStatics::ParseOption(OptionsString, TEXT("Map"));
		const int32 TeamOption = UGameplayStatics::GetIntOption(OptionsString, TEXT("Team"), 4);
		State->MatchMap = MapOption == TEXT("RealMap") ? FName(TEXT("RealMap")) : FName(TEXT("Classic"));
		State->TeamSize = TeamOption == 1 || TeamOption == 2 ? TeamOption : 4;
		const bool bRealMap = State->MatchMap == TEXT("RealMap");
		// 50 does not split into squads of 4: 12 squads = 48.
		State->MaxPlayers = bRealMap ? (State->TeamSize == 4 ? 48 : 50) : 2 * FMath::Max(State->TeamSize, 1) * (State->TeamSize == 1 ? 4 : 1);
		if (GameSession)
		{
			GameSession->MaxPlayers = State->MaxPlayers;
		}
	}
}

void ACampusMatchGameMode::PostLogin(APlayerController* NewPlayer)
{
	// Team and host flag must be set before the pawn spawns at its team's side.
	if (ACampusPlayerState* PlayerState = NewPlayer->GetPlayerState<ACampusPlayerState>())
	{
		int32 Red = 0;
		int32 Blue = 0;
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			if (const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Other); Campus && Campus != PlayerState)
			{
				(Campus->Team == ECampusTeam::Red ? Red : Blue)++;
			}
		}
		PlayerState->Team = Red <= Blue ? ECampusTeam::Red : ECampusTeam::Blue;
		// the listen server's own player is the host (exact), not whoever logged in first
		PlayerState->bIsHost = NewPlayer->IsLocalPlayerController();
	}
	Super::PostLogin(NewPlayer);
	AdmitWithTicket(NewPlayer);
	ReportRoom();
	RefreshVoiceTeams();
}

void ACampusMatchGameMode::RefreshVoiceTeams()
{
	TArray<APlayerController*> Players;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Player = It->Get(); Player && Player->PlayerState)
		{
			Players.Add(Player);
		}
	}
	for (APlayerController* Listener : Players)
	{
		const ACampusPlayerState* Mine = Listener->GetPlayerState<ACampusPlayerState>();
		for (APlayerController* Speaker : Players)
		{
			const ACampusPlayerState* Theirs = Speaker->GetPlayerState<ACampusPlayerState>();
			if (Speaker == Listener || !Mine || !Theirs || !Theirs->GetUniqueId().IsValid())
			{
				continue;
			}
			if (Mine->Team == Theirs->Team)
			{
				Listener->GameplayUnmutePlayer(Theirs->GetUniqueId());
			}
			else
			{
				Listener->GameplayMutePlayer(Theirs->GetUniqueId());
			}
		}
	}
}

FString ACampusMatchGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
	const FString& Portal)
{
	const FString Result = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (ACampusPlayerState* PlayerState = NewPlayerController ? NewPlayerController->GetPlayerState<ACampusPlayerState>() : nullptr)
	{
		PlayerState->JoinTicket = UGameplayStatics::ParseOption(Options, TEXT("Ticket"));
	}
	return Result;
}

void ACampusMatchGameMode::AdmitWithTicket(APlayerController* NewPlayer)
{
	const ACampusGameState* State = GetCampusState();
	UCampusBackend* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
	ACampusPlayerState* PlayerState = NewPlayer->GetPlayerState<ACampusPlayerState>();
	if (!State || !Backend || !PlayerState || !Backend->GetHostedRoom().IsValid() || Backend->GetHostedRoom().Code != State->RoomCode)
	{
		return; // not an API room (editor play, LAN quick match)
	}
	if (NewPlayer->IsLocalPlayerController())
	{
		PlayerState->AccountId = Backend->PlayerId;
		return;
	}
	TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayer);
	TWeakObjectPtr<ACampusMatchGameMode> WeakThis(this);
	Backend->AdmitPlayer(PlayerState->JoinTicket, [WeakThis, WeakPlayer](bool bRejected, const FString& PlayerId)
	{
		APlayerController* Player = WeakPlayer.Get();
		ACampusMatchGameMode* Self = WeakThis.Get();
		if (!Player || !Self)
		{
			return;
		}
		ACampusPlayerState* Admitted = Player->GetPlayerState<ACampusPlayerState>();
		// the same account twice (a copied ticket) is refused like a bad one
		bool bDuplicate = false;
		for (const APlayerState* Other : Self->GameState->PlayerArray)
		{
			const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Other);
			bDuplicate |= Campus && Campus != Admitted && !PlayerId.IsEmpty() && Campus->AccountId == PlayerId;
		}
		if (bRejected || bDuplicate)
		{
			if (Self->GameSession)
			{
				Self->GameSession->KickPlayer(Player, FText::FromString(TEXT("Join this room from the lobby with its code.")));
			}
			return;
		}
		if (Admitted)
		{
			Admitted->AccountId = PlayerId; // empty when the API was unreachable: plays, but no recorded result
		}
	});
}

void ACampusMatchGameMode::ReportResult()
{
	const ACampusGameState* State = GetCampusState();
	UCampusBackend* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
	if (!State || !Backend || !Backend->GetHostedRoom().IsValid() || Backend->GetHostedRoom().Code != State->RoomCode)
	{
		return;
	}
	TArray<FCampusMatchResult> Lines;
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player);
		if (!Campus || Campus->AccountId.IsEmpty())
		{
			continue;
		}
		FCampusMatchResult& Line = Lines.AddDefaulted_GetRef();
		Line.PlayerId = Campus->AccountId;
		Line.Team = int32(Campus->Team);
		Line.Kills = Campus->Kills;
		Line.Deaths = Campus->Deaths;
		Line.DamageDealt = FMath::RoundToInt(Campus->DamageDealt);
		Line.DamageTaken = FMath::RoundToInt(Campus->DamageTaken);
		Line.bWon = State->Winner == int32(Campus->Team);
	}
	Backend->ReportMatchResult(Lines);
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
	// The team base pads of the three.js game (src/game/game.js TEAM_BASES / BASE_SLOTS), where the campus sits
	// in L_Match (Unreal = (x, z, y) * 100 + campus origin): BLUE on the road outside the main gate (z -143),
	// RED at the open end behind the courtyard (z -15), 4 spots 3 m apart across each pad, facing each other.
	static const FVector CampusOrigin(15710.f, 8222.6f, 0.f);
	static const float BaseSlots[] = { -450.f, -150.f, 150.f, 450.f };
	const bool bRed = Team == ECampusTeam::Red;
	const float BaseY = (bRed ? -15.f : -143.f) * 100.f;
	// more than 4 on a team: a second row 2 m further from the enemy
	const float Row = (Slot / 4) * 200.f * (bRed ? 1.f : -1.f);
	const FVector Location(CampusOrigin.X + BaseSlots[Slot % 4], CampusOrigin.Y + BaseY + Row, 110.f);
	const FRotator Facing(0.f, bRed ? -90.f : 90.f, 0.f);
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

	State->RedScore = 0;
	State->BlueScore = 0;
	State->Winner = -1;
	StartRound(1, false);
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
	RefreshVoiceTeams();
}

void ACampusMatchGameMode::OnKilled(AController* Victim, AController* Killer)
{
	ACampusGameState* State = GetCampusState();
	if (!State)
	{
		return;
	}
	// A death outside a match (fell off the map in the waiting room or on the end screen) still removes the dead
	// pawn and respawns it; it just does not score. Otherwise the player stays a "ghost": standing, unable to
	// shoot or die (bDead is server-only).
	if (State->Phase != ECampusPhase::Playing)
	{
		RespawnLater(Victim);
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
		(KillerState->Team == ECampusTeam::Red ? RedRoundKills : BlueRoundKills)++;
	}
	State->MulticastFeed(FString::Printf(TEXT("%s  >  %s"),
		KillerState ? *KillerState->GetPlayerName() : TEXT("?"),
		VictimState ? *VictimState->GetPlayerName() : TEXT("?")));

	// elimination: out until the next round (the pawn goes, no respawn timer)
	if (Victim)
	{
		if (APawn* Pawn = Victim->GetPawn())
		{
			Pawn->Destroy();
		}
	}
	CheckRoundOver(false);
}

void ACampusMatchGameMode::RespawnLater(AController* Victim)
{
	if (!Victim)
	{
		return;
	}
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
			RestartPlayer(Controller); // (no respawn once the match has Ended: RestartPlayer checks)
		}
	}), RespawnSeconds, false);
}

void ACampusMatchGameMode::CheckClock()
{
	const ACampusGameState* State = GetCampusState();
	if (State && State->Phase == ECampusPhase::Playing && GetWorld()->GetTimeSeconds() >= State->EndTime)
	{
		CheckRoundOver(true);
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
	GetWorldTimerManager().ClearTimer(BreakTimer);
	State->Phase = ECampusPhase::Ended;
	State->Winner = State->RedScore == State->BlueScore ? -1 : (State->RedScore > State->BlueScore ? 0 : 1);
	State->MulticastFeed(FString::Printf(TEXT("Match over  ·  %s wins %d - %d"),
		State->Winner == 0 ? TEXT("RED") : TEXT("BLUE"), FMath::Max(State->RedScore, State->BlueScore), FMath::Min(State->RedScore, State->BlueScore)));
	ReportRoom();
	ReportResult();
}

void ACampusMatchGameMode::StartRound(int32 Round, bool bDecider)
{
	// every round starts with all doors whole
	for (TActorIterator<ACampusDoor> It(GetWorld()); It; ++It)
	{
		It->Repair();
	}
	ACampusGameState* State = GetCampusState();
	if (!State)
	{
		return;
	}
	State->Round = Round;
	State->bDecider = bDecider;
	State->LastRoundWinner = -1;
	State->Phase = ECampusPhase::Playing;
	State->EndTime = GetWorld()->GetTimeSeconds() + State->RoundMinutes * 60.f;
	RedRoundKills = 0;
	BlueRoundKills = 0;
	ReportRoom();
	// everyone back in, on their team's side
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
	State->MulticastFeed(bDecider
		? FString::Printf(TEXT("DECIDING ROUND  ·  %d - %d"), State->RedScore, State->BlueScore)
		: FString::Printf(TEXT("ROUND %d of %d"), Round, State->Rounds));
}

int32 ACampusMatchGameMode::AliveOn(ECampusTeam Team, int32* OutPlayers) const
{
	int32 Alive = 0;
	int32 Players = 0;
	for (const APlayerState* Other : GameState->PlayerArray)
	{
		const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Other);
		if (!Campus || Campus->Team != Team)
		{
			continue;
		}
		++Players;
		const APawn* Pawn = Campus->GetPawn();
		Alive += Pawn && !Pawn->IsPendingKillPending() ? 1 : 0;
	}
	if (OutPlayers)
	{
		*OutPlayers = Players;
	}
	return Alive;
}

void ACampusMatchGameMode::CheckRoundOver(bool bForce)
{
	const ACampusGameState* State = GetCampusState();
	if (!State || State->Phase != ECampusPhase::Playing)
	{
		return;
	}
	int32 RedPlayers = 0;
	int32 BluePlayers = 0;
	const int32 RedAlive = AliveOn(ECampusTeam::Red, &RedPlayers);
	const int32 BlueAlive = AliveOn(ECampusTeam::Blue, &BluePlayers);
	// a team with players and nobody left standing loses the round (alone in a room: dying ends it as a draw,
	// instead of waiting dead for the whole clock)
	const bool bRedOut = RedPlayers > 0 && RedAlive == 0;
	const bool bBlueOut = BluePlayers > 0 && BlueAlive == 0;
	if (bRedOut || bBlueOut)
	{
		FinishRound(bRedOut == bBlueOut ? -1 : (bRedOut ? (BlueAlive > 0 ? 1 : -1) : (RedAlive > 0 ? 0 : -1)));
		return;
	}
	if (bForce)
	{
		// time up: more players standing wins, then more kills this round, else a draw
		const int32 Winner = RedAlive != BlueAlive ? (RedAlive > BlueAlive ? 0 : 1)
			: (RedRoundKills != BlueRoundKills ? (RedRoundKills > BlueRoundKills ? 0 : 1) : -1);
		FinishRound(Winner);
	}
}

void ACampusMatchGameMode::FinishRound(int32 Winner)
{
	ACampusGameState* State = GetCampusState();
	State->LastRoundWinner = Winner;
	if (Winner == 0)
	{
		++State->RedScore;
	}
	else if (Winner == 1)
	{
		++State->BlueScore;
	}
	State->Phase = ECampusPhase::RoundOver;
	State->MulticastFeed(FString::Printf(TEXT("%s  ·  RED %d - %d BLUE"),
		Winner < 0 ? TEXT("Round drawn") : (Winner == 0 ? TEXT("RED wins the round") : TEXT("BLUE wins the round")), State->RedScore, State->BlueScore));
	GetWorldTimerManager().SetTimer(BreakTimer, this, &ACampusMatchGameMode::NextRoundOrEnd, RoundBreakSeconds, false);
}

void ACampusMatchGameMode::NextRoundOrEnd()
{
	const ACampusGameState* State = GetCampusState();
	if (!State)
	{
		return;
	}
	// every picked round is played, even when one team can no longer be caught; then a tie gets a decider
	if (State->Round < State->Rounds)
	{
		StartRound(State->Round + 1, false);
	}
	else if (State->RedScore == State->BlueScore)
	{
		StartRound(State->Round + 1, true);
	}
	else
	{
		EndRound();
	}
}

void ACampusMatchGameMode::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
	ReportRoom();
	CheckRoundOver(false); // the last player of a team leaving ends the round
}

void ACampusMatchGameMode::ReportRoom()
{
	// The host's phone keeps its API room alive and tells it how full it is and whether the match has started
	// (a started room keeps its Room Card spent).
	const ACampusGameState* State = GetCampusState();
	UCampusBackend* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
	if (State && Backend && Backend->GetHostedRoom().Code == State->RoomCode)
	{
		Backend->SetHostedRoomState(State->Phase != ECampusPhase::Waiting, GameState->PlayerArray.Num());
	}
}
