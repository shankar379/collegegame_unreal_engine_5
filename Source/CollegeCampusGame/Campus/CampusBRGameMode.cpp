#include "Campus/CampusBRGameMode.h"

#include "Campus/CampusBRPlane.h"
#include "Campus/CampusBRZone.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusCharacter.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusPlayerControllers.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

namespace CampusBRRoster
{
	/** Virtual combatants think at 2 Hz. Fifty rows at 2 Hz is nothing; fifty pawns is a dead phone. */
	constexpr float VirtualInterval = 0.5f;
	/** Promotion bubble (design §3.3). On this 227 m campus that is "anywhere", which is exactly right:
	 *  the ten physical slots should always be full while humans are alive. */
	constexpr float PromotionRadius = 30000.f;
	/** Virtual drift toward the circle, cm/s (a plausible jog). */
	constexpr float DriftSpeed = 420.f;

	/** Attrition curve (design §3.3): seconds into the match -> alive count the players should see. */
	struct FPoint { float Seconds; float Alive; };
	const FPoint Curve[] = {
		{ 0.f, 50.f }, { 90.f, 34.f }, { 180.f, 24.f }, { 300.f, 15.f }, { 420.f, 8.f }, { 540.f, 4.f },
	};

	const TCHAR* BotNames[] = {
		TEXT("Arjun"), TEXT("Rohit"), TEXT("Kiran"), TEXT("Deepak"), TEXT("Sahil"), TEXT("Varun"),
		TEXT("Nikhil"), TEXT("Aditya"), TEXT("Manish"), TEXT("Pranav"), TEXT("Rahul"), TEXT("Sandeep"),
		TEXT("Yash"), TEXT("Imran"), TEXT("Vivek"), TEXT("Harsh"), TEXT("Gaurav"), TEXT("Tarun"),
	};

	TWeakObjectPtr<UObject> BotFactory;
}

void ACampusBRGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusBRGameState, BRPhase);
	DOREPLIFETIME(ACampusBRGameState, BRPhaseEndTime);
	DOREPLIFETIME(ACampusBRGameState, AliveCombatants);
	DOREPLIFETIME(ACampusBRGameState, TotalCombatants);
	DOREPLIFETIME(ACampusBRGameState, AliveSquads);
	DOREPLIFETIME(ACampusBRGameState, WinningSquad);
	DOREPLIFETIME(ACampusBRGameState, HumansJoined);
	DOREPLIFETIME(ACampusBRGameState, Zone);
	DOREPLIFETIME(ACampusBRGameState, HumanCap);
}

ACampusBRGameMode::ACampusBRGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ACampusCharacter::StaticClass();
	PlayerControllerClass = ACampusMatchPlayerController::StaticClass();
	PlayerStateClass = ACampusPlayerState::StaticClass();
	GameStateClass = ACampusBRGameState::StaticClass();
}

FString ACampusBRGameMode::HostOptionsFor(const FString& MapChoice)
{
	// "?Mode=BR" is what the match reads; "?game=" is what the engine needs to pick the class at all
	// (UWorld::GetGameModeForURL), and a game mode cannot select itself.
	return MapChoice == TEXT("BR")
		? FString(TEXT("?Mode=BR?game=/Script/CollegeCampusGame.CampusBRGameMode"))
		: FString();
}

void ACampusBRGameMode::SetBotFactory(UObject* Factory)
{
	CampusBRRoster::BotFactory = Factory;
}

ACampusBRGameState* ACampusBRGameMode::GetBRState() const
{
	return GetGameState<ACampusBRGameState>();
}

void ACampusBRGameMode::InitGameState()
{
	Super::InitGameState();
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	FString Code = UGameplayStatics::ParseOption(OptionsString, TEXT("Code")).ToUpper();
	if (Code.IsEmpty())
	{
		Code = UGameplayStatics::ParseOption(OptionsString, TEXT("Room")).ToUpper();
	}
	if (Code.IsEmpty())
	{
		static const TCHAR Letters[] = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Code.AppendChar(Letters[FMath::RandRange(0, UE_ARRAY_COUNT(Letters) - 2)]);
		}
	}
	State->RoomCode = Code;
	State->MatchMap = FName(TEXT("RealMap"));

	const int32 TeamOption = UGameplayStatics::GetIntOption(OptionsString, TEXT("Team"), 4);
	SquadSize = TeamOption == 1 || TeamOption == 2 ? TeamOption : 4;
	MaxHumans = FMath::Clamp(UGameplayStatics::GetIntOption(OptionsString, TEXT("Humans"), MaxHumans), 1, 16);
	State->TeamSize = SquadSize;
	State->HumanCap = MaxHumans;
	State->MaxPlayers = MaxHumans;
	State->TotalCombatants = MaxCombatants;
	State->AliveCombatants = MaxCombatants;
	if (GameSession)
	{
		GameSession->MaxPlayers = MaxHumans;
	}

	WaitingArea = GetWorld()->SpawnActor<ACampusBRWaitingArea>(ACampusBRWaitingArea::StaticClass(),
		FTransform(ACampusBRWaitingArea::Place()));
	ZoneActor = GetWorld()->SpawnActor<ACampusBRZone>(ACampusBRZone::StaticClass(), FTransform::Identity);
	State->Zone = ZoneActor;
	State->Phase = ECampusPhase::Waiting;
	EnterPhase(ECampusBRPhase::Waiting);
}

void ACampusBRGameMode::EnterPhase(ECampusBRPhase NewPhase)
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	State->BRPhase = NewPhase;
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	const float Now = float(GetWorld()->GetTimeSeconds());

	switch (NewPhase)
	{
	case ECampusBRPhase::Waiting:
		State->BRPhaseEndTime = Now + WaitingSeconds;
		GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			EnterPhase(ECampusBRPhase::Countdown);
		}), WaitingSeconds, false);
		break;

	case ECampusBRPhase::Countdown:
		FillWithBots();
		State->BRPhaseEndTime = Now + CountdownSeconds;
		GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			EnterPhase(ECampusBRPhase::Flight);
		}), CountdownSeconds, false);
		break;

	case ECampusBRPhase::Flight:
		StartFlight();
		break;

	case ECampusBRPhase::Playing:
		BeginPlaying();
		break;

	case ECampusBRPhase::Ended:
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Joining

void ACampusBRGameMode::PostLogin(APlayerController* NewPlayer)
{
	ACampusBRGameState* State = GetBRState();
	if (ACampusPlayerState* PlayerState = NewPlayer->GetPlayerState<ACampusPlayerState>())
	{
		// Every BR player is nominally "Red": friendly fire is decided by SQUAD here, not by the two
		// TDM colours, and AreFriendly is the one place that is asked.
		PlayerState->Team = ECampusTeam::Red;
		PlayerState->bIsHost = NewPlayer->IsLocalPlayerController();
	}

	FCampusBRCombatant& Slot = Combatants.AddDefaulted_GetRef();
	Slot.bHuman = true;
	Slot.bPhysical = true;
	Slot.Controller = NewPlayer;
	Slot.Squad = NextSquadWithRoom();
	Slot.Name = NewPlayer->PlayerState ? NewPlayer->PlayerState->GetPlayerName() : TEXT("Player");

	Super::PostLogin(NewPlayer);

	if (State)
	{
		int32 Humans = 0;
		for (const FCampusBRCombatant& Each : Combatants)
		{
			Humans += Each.bHuman ? 1 : 0;
		}
		State->HumansJoined = Humans;
		// Full house: stop waiting for people who are not coming.
		if (Humans >= MaxHumans && State->BRPhase == ECampusBRPhase::Waiting)
		{
			EnterPhase(ECampusBRPhase::Countdown);
		}
	}
	RefreshCounts();
}

void ACampusBRGameMode::Logout(AController* Exiting)
{
	const int32 Index = IndexOf(Exiting);
	if (Index != INDEX_NONE && Combatants[Index].bAlive)
	{
		Eliminate(Index, TEXT("left"));
	}
	Super::Logout(Exiting);
}

int32 ACampusBRGameMode::NextSquadWithRoom() const
{
	const int32 Squads = FMath::Max(1, MaxCombatants / FMath::Max(1, SquadSize));
	for (int32 Squad = 1; Squad <= Squads; ++Squad)
	{
		int32 Taken = 0;
		for (const FCampusBRCombatant& Each : Combatants)
		{
			Taken += Each.Squad == Squad ? 1 : 0;
		}
		if (Taken < SquadSize)
		{
			return Squad;
		}
	}
	return Squads;
}

int32 ACampusBRGameMode::IndexOf(const AController* Controller) const
{
	for (int32 Index = 0; Index < Combatants.Num(); ++Index)
	{
		if (Combatants[Index].Controller.Get() == Controller)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 ACampusBRGameMode::SquadOf(const AController* Controller) const
{
	const int32 Index = IndexOf(Controller);
	return Index == INDEX_NONE ? 0 : Combatants[Index].Squad;
}

bool ACampusBRGameMode::AreFriendly(const AController* First, const AController* Second) const
{
	const ACampusBRGameState* State = GetBRState();
	// Before the drop nobody can hurt anybody: the waiting platform is a lobby, not a map.
	if (State && State->BRPhase != ECampusBRPhase::Playing && State->BRPhase != ECampusBRPhase::Flight)
	{
		return true;
	}
	if (!First || !Second || First == Second)
	{
		return true;
	}
	const int32 Mine = SquadOf(First);
	const int32 Theirs = SquadOf(Second);
	return Mine != 0 && Mine == Theirs;
}

void ACampusBRGameMode::RestartPlayer(AController* NewPlayer)
{
	const ACampusBRGameState* State = GetBRState();
	if (!NewPlayer || NewPlayer->IsPendingKillPending() || !State)
	{
		return;
	}
	// BR has no respawn. Before the drop everyone stands on the platform; after it, a dead player
	// spectates (see Eliminate) and never gets a pawn back.
	if (State->BRPhase != ECampusBRPhase::Waiting && State->BRPhase != ECampusBRPhase::Countdown)
	{
		return;
	}
	const int32 Index = IndexOf(NewPlayer);
	const FTransform Where = WaitingArea ? WaitingArea->SlotFor(FMath::Max(Index, 0))
		: FTransform(ACampusBRWaitingArea::Place() + FVector(0.f, 0.f, 200.f));
	RestartPlayerAtTransform(NewPlayer, Where);
}

void ACampusBRGameMode::RequestStart(APlayerController* Requester)
{
	const ACampusBRGameState* State = GetBRState();
	const ACampusPlayerState* PlayerState = Requester ? Requester->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (State && PlayerState && PlayerState->bIsHost && State->BRPhase == ECampusBRPhase::Waiting)
	{
		EnterPhase(ECampusBRPhase::Countdown);
	}
}

// ---------------------------------------------------------------------------------------------
// Filling the roster

void ACampusBRGameMode::FillWithBots()
{
	// Every remaining slot starts VIRTUAL. Physical pawns are only ever made by promotion, so the
	// host never pays for ten AI pawns during the countdown, when it is busiest.
	int32 Next = 0;
	while (Combatants.Num() < MaxCombatants)
	{
		FCampusBRCombatant& Slot = Combatants.AddDefaulted_GetRef();
		Slot.Squad = NextSquadWithRoom();
		Slot.Name = FString::Printf(TEXT("%s%02d"),
			CampusBRRoster::BotNames[Next % int32(UE_ARRAY_COUNT(CampusBRRoster::BotNames))], Next + 1);
		Slot.Where = FVector(CampusBRMap::Center().X, CampusBRMap::Center().Y, 0.f);
		++Next;
	}
	RefreshCounts();
}

int32 ACampusBRGameMode::PhysicalBotsAlive() const
{
	int32 Count = 0;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		Count += !Each.bHuman && Each.bPhysical && Each.bAlive ? 1 : 0;
	}
	return Count;
}

bool ACampusBRGameMode::VirtualAllowed() const
{
	// No ghosts in the endgame: in the last two circles every combatant left must be real or dead.
	return !ZoneActor || ZoneActor->GetPhase() < ZoneActor->GetPhaseCount() - 2;
}

bool ACampusBRGameMode::Promote(int32 Index)
{
	if (!Combatants.IsValidIndex(Index) || Combatants[Index].bPhysical || !Combatants[Index].bAlive)
	{
		return false;
	}
	FCampusBRCombatant& Slot = Combatants[Index];
	const TArray<int32> Squads = { Slot.Squad };
	const TArray<FVector> Where = { Slot.Where };
	TArray<AController*> Made;

	if (UObject* Factory = CampusBRRoster::BotFactory.Get())
	{
		if (ICampusBRBotFactory* Bots = Cast<ICampusBRBotFactory>(Factory))
		{
			Bots->SpawnBots(1, Squads, Where, Made);
		}
	}
	if (Made.Num() == 0)
	{
		// No bot module in this build yet: a plain ACampusCharacter under an AIController found by
		// name. It stands and can be shot, which is still better than a ghost in the endgame.
		UClass* ControllerClass = FindObject<UClass>(nullptr, TEXT("/Script/CollegeCampusGame.CampusBotController"));
		if (!ControllerClass)
		{
			ControllerClass = AAIController::StaticClass();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACampusCharacter* Pawn = GetWorld()->SpawnActor<ACampusCharacter>(ACampusCharacter::StaticClass(),
			FTransform(Slot.Where + FVector(0.f, 0.f, 120.f)), Params);
		AController* Bot = Pawn ? GetWorld()->SpawnActor<AController>(ControllerClass, FTransform::Identity, Params) : nullptr;
		if (!Pawn || !Bot)
		{
			if (Pawn)
			{
				Pawn->Destroy();
			}
			return false;
		}
		// A bot needs a player state so the scoreboard, the kill feed and the team HUD see it at all.
		Bot->InitPlayerState();
		if (ACampusPlayerState* BotState = Bot->GetPlayerState<ACampusPlayerState>())
		{
			BotState->SetPlayerName(Slot.Name);
			BotState->Team = ECampusTeam::Red;
		}
		Bot->Possess(Pawn);
		// 50 actors on a phone: a bot nobody can see must not cost bandwidth or a tick.
		Pawn->SetNetUpdateFrequency(10.f);
		Pawn->SetMinNetUpdateFrequency(2.f);
		Pawn->SetNetCullDistanceSquared(4.0e8f); // 200 m, per the design's net budget table
		Made.Add(Bot);
	}

	Slot.bPhysical = true;
	Slot.Controller = Made[0];
	return true;
}

// ---------------------------------------------------------------------------------------------
// Flight and play

void ACampusBRGameMode::StartFlight()
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	State->Phase = ECampusPhase::Playing; // the HUD and the touch controls key off this
	Plane = GetWorld()->SpawnActor<ACampusBRPlane>(ACampusBRPlane::StaticClass(), FTransform::Identity);
	if (!Plane)
	{
		EnterPhase(ECampusBRPhase::Playing);
		return;
	}
	Plane->Launch();
	// Only humans ride. A promoted bot appears on the ground where its virtual row had drifted to,
	// which costs nothing and nobody can tell the difference from below.
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (!Each.bHuman)
		{
			continue;
		}
		if (ACampusCharacter* Pawn = Each.Controller.IsValid() ? Cast<ACampusCharacter>(Each.Controller->GetPawn()) : nullptr)
		{
			Plane->Board(Pawn);
		}
	}
	// The circle opens as the plane leaves the map, which is the design's 0:45 mark.
	GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		EnterPhase(ECampusBRPhase::Playing);
	}), 45.f, false);
}

void ACampusBRGameMode::BeginPlaying()
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	State->Phase = ECampusPhase::Playing;
	MatchStartTime = float(GetWorld()->GetTimeSeconds());
	if (ZoneActor)
	{
		ZoneActor->Begin();
	}
	State->MulticastFeed(TEXT("The play area is closing in"));
}

void ACampusBRGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ACampusBRGameState* State = GetBRState();
	if (!State || State->BRPhase != ECampusBRPhase::Playing)
	{
		return;
	}
	const float Now = float(GetWorld()->GetTimeSeconds());
	if (Now < NextVirtualTime)
	{
		return;
	}
	NextVirtualTime = Now + CampusBRRoster::VirtualInterval;
	TickVirtual();
}

int32 ACampusBRGameMode::TargetAlive(float SecondsIntoMatch) const
{
	const auto& Curve = CampusBRRoster::Curve;
	const int32 Count = int32(UE_ARRAY_COUNT(CampusBRRoster::Curve));
	if (SecondsIntoMatch <= Curve[0].Seconds)
	{
		return MaxCombatants;
	}
	for (int32 Index = 1; Index < Count; ++Index)
	{
		if (SecondsIntoMatch <= Curve[Index].Seconds)
		{
			const float Alpha = (SecondsIntoMatch - Curve[Index - 1].Seconds)
				/ FMath::Max(Curve[Index].Seconds - Curve[Index - 1].Seconds, 1.f);
			const float Alive = FMath::Lerp(Curve[Index - 1].Alive, Curve[Index].Alive, Alpha);
			// The curve is written for fifty; a smaller match scales with it.
			return FMath::CeilToInt(Alive * MaxCombatants / 50.f);
		}
	}
	return 1;
}

void ACampusBRGameMode::TickVirtual()
{
	const FVector2D Middle = ZoneActor ? ZoneActor->GetCenter() : CampusBRMap::Center();
	const float Drift = CampusBRRoster::DriftSpeed * CampusBRRoster::VirtualInterval;
	const bool bGhostsAllowed = VirtualAllowed();

	// Gather the humans once: the promotion test is the only per-combatant distance check we do.
	TArray<FVector> Humans;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bHuman && Each.bAlive && Each.Controller.IsValid())
		{
			if (const APawn* Pawn = Each.Controller->GetPawn())
			{
				Humans.Add(Pawn->GetActorLocation());
			}
		}
	}

	for (int32 Index = 0; Index < Combatants.Num(); ++Index)
	{
		FCampusBRCombatant& Slot = Combatants[Index];
		if (!Slot.bAlive || Slot.bPhysical)
		{
			continue;
		}
		const FVector2D Flat(Slot.Where.X, Slot.Where.Y);
		const FVector2D Toward = (Middle - Flat).GetSafeNormal();
		Slot.Where = FVector(Flat + Toward * Drift, 0.f);

		bool bNear = !bGhostsAllowed; // endgame: everyone left must become real (or die below)
		for (const FVector& Human : Humans)
		{
			bNear |= FVector2D::DistSquared(FVector2D(Human.X, Human.Y), Flat)
				< FMath::Square(CampusBRRoster::PromotionRadius);
		}
		if (bNear && PhysicalBotsAlive() < MaxPhysicalBots)
		{
			Promote(Index);
		}
	}

	// Attrition: bring the ALIVE counter down the curve the players expect. Only virtual rows are
	// ever taken this way - a real pawn has to actually be killed.
	const int32 Target = TargetAlive(float(GetWorld()->GetTimeSeconds()) - MatchStartTime);
	const ACampusBRGameState* State = GetBRState();
	if (State && State->AliveCombatants > Target)
	{
		for (int32 Index = 0; Index < Combatants.Num(); ++Index)
		{
			if (Combatants[Index].bAlive && !Combatants[Index].bPhysical)
			{
				KillVirtual(Index);
				break; // one a tick: deaths should trickle into the feed, not arrive in a block
			}
		}
	}
}

void ACampusBRGameMode::KillVirtual(int32 Index)
{
	// A plausible killer, so the feed reads like a world that is alive.
	FString Killer = TEXT("the zone");
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive && Each.Squad != Combatants[Index].Squad && !Each.bHuman)
		{
			Killer = Each.Name;
			break;
		}
	}
	Eliminate(Index, Killer);
}

// ---------------------------------------------------------------------------------------------
// Dying and winning

void ACampusBRGameMode::OnKilled(AController* Victim, AController* Killer)
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	// A fall off the waiting platform is not an elimination: put the player back on the slab.
	if (State->BRPhase == ECampusBRPhase::Waiting || State->BRPhase == ECampusBRPhase::Countdown)
	{
		if (Victim)
		{
			if (APawn* Pawn = Victim->GetPawn())
			{
				Pawn->Destroy();
			}
			RestartPlayer(Victim);
		}
		return;
	}
	if (ACampusPlayerState* VictimState = Victim ? Victim->GetPlayerState<ACampusPlayerState>() : nullptr)
	{
		++VictimState->Deaths;
	}
	ACampusPlayerState* KillerState = Killer ? Killer->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (KillerState && Killer != Victim)
	{
		++KillerState->Kills;
	}
	const int32 Index = IndexOf(Victim);
	if (Index != INDEX_NONE)
	{
		Eliminate(Index, KillerState ? KillerState->GetPlayerName() : TEXT("the zone"));
	}
}

void ACampusBRGameMode::Eliminate(int32 Index, const FString& KillerName)
{
	ACampusBRGameState* State = GetBRState();
	if (!Combatants.IsValidIndex(Index) || !Combatants[Index].bAlive || !State)
	{
		return;
	}
	FCampusBRCombatant& Slot = Combatants[Index];
	Slot.bAlive = false;
	Slot.Placement = FMath::Max(1, State->AliveCombatants); // the place they finished in
	State->MulticastFeed(FString::Printf(TEXT("%s  >  %s"), *KillerName, *Slot.Name));

	AController* Controller = Slot.Controller.Get();
	if (Controller)
	{
		if (APawn* Pawn = Controller->GetPawn())
		{
			Pawn->Destroy();
		}
		// Spectate a squadmate who is still up; with nobody left the win screen takes over and the
		// existing "Leave" button walks back to the lobby.
		if (APlayerController* Player = Cast<APlayerController>(Controller))
		{
			if (APawn* Mate = LivingSquadmatePawn(Slot.Squad, Controller))
			{
				Player->SetViewTargetWithBlend(Mate, 1.f);
			}
		}
	}
	RefreshCounts();

	if (State->AliveSquads <= 1)
	{
		EndMatch();
	}
}

APawn* ACampusBRGameMode::LivingSquadmatePawn(int32 Squad, const AController* Except) const
{
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive && Each.Squad == Squad && Each.Controller.IsValid() && Each.Controller.Get() != Except)
		{
			if (APawn* Pawn = Each.Controller->GetPawn())
			{
				return Pawn;
			}
		}
	}
	return nullptr;
}

void ACampusBRGameMode::RefreshCounts()
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	TSet<int32> Squads;
	int32 Alive = 0;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive)
		{
			++Alive;
			Squads.Add(Each.Squad);
		}
	}
	State->AliveCombatants = Alive;
	State->AliveSquads = Squads.Num();
	State->TotalCombatants = Combatants.Num();
}

void ACampusBRGameMode::EndMatch()
{
	ACampusBRGameState* State = GetBRState();
	if (!State || State->BRPhase == ECampusBRPhase::Ended)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	if (ZoneActor)
	{
		ZoneActor->Freeze();
	}
	State->BRPhase = ECampusBRPhase::Ended;
	State->Phase = ECampusPhase::Ended;

	int32 WinningSquad = -1;
	for (FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive)
		{
			WinningSquad = Each.Squad;
			Each.Placement = 1;
		}
	}
	State->WinningSquad = WinningSquad;
	State->Winner = 0; // the existing win screen reads Winner; BOOYAH is keyed off WinningSquad
	State->MulticastFeed(TEXT("BOOYAH!"));
	ReportResult();
}

void ACampusBRGameMode::ReportResult()
{
	const ACampusBRGameState* State = GetBRState();
	UCampusBackend* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
	if (!State || !Backend || State->RoomCode.IsEmpty())
	{
		return;
	}
	// Contract: { placements: [ { player_id | bot, team, kills, bot_kills, placement } ], duration_s }
	FString Rows;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		const ACampusPlayerState* PlayerState = Each.Controller.IsValid()
			? Each.Controller->GetPlayerState<ACampusPlayerState>() : nullptr;
		// A human with no admitted account (editor play, LAN) is reported as a bot: the backend keys
		// placements on the uuid, and inventing one would corrupt somebody else's record.
		const bool bAccount = Each.bHuman && PlayerState && !PlayerState->AccountId.IsEmpty();
		const FString Who = bAccount
			? FString::Printf(TEXT("\"player_id\":\"%s\""), *PlayerState->AccountId)
			: FString(TEXT("\"bot\":true"));
		Rows += FString::Printf(TEXT("%s{%s,\"team\":%d,\"kills\":%d,\"bot_kills\":%d,\"placement\":%d}"),
			Rows.IsEmpty() ? TEXT("") : TEXT(","), *Who, Each.Squad,
			PlayerState ? PlayerState->Kills : 0, 0, Each.Placement);
	}
	const int32 Duration = FMath::Max(0, FMath::RoundToInt(float(GetWorld()->GetTimeSeconds()) - MatchStartTime));
	const FString Body = FString::Printf(TEXT("{\"placements\":[%s],\"duration_s\":%d}"), *Rows, Duration);

	// Fire and forget. The endpoint is still being built; a 404 must never hold up the win screen.
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetURL(FString::Printf(TEXT("%s/br/%s/result"), *GetDefault<UCampusNetSettings>()->ApiBaseUrl(), *State->RoomCode));
	Http->SetVerb(TEXT("POST"));
	Http->SetTimeout(10.f);
	Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Http->SetHeader(TEXT("ngrok-skip-browser-warning"), TEXT("1"));
	// One result per room, whatever the network does on the way: the room code IS the natural key.
	Http->SetHeader(TEXT("Idempotency-Key"), FString::Printf(TEXT("br-result-%s"), *State->RoomCode));
	if (!Backend->GetAccessToken().IsEmpty())
	{
		Http->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Backend->GetAccessToken());
	}
	Http->SetContentAsString(Body);
	Http->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		UE_LOG(LogTemp, Log, TEXT("[CampusBR] result POST -> %d"), bConnected && Response ? Response->GetResponseCode() : 0);
	});
	Http->ProcessRequest();
}
