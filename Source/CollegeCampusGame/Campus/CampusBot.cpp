#include "Campus/CampusBot.h"

#include "Campus/CampusCharacter.h"
#include "Campus/CampusGameMode.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerState.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "AITypes.h"
#include "UObject/UnrealType.h"

namespace CampusBotTuning
{
	TAutoConsoleVariable<int32> CVarFillBotsTDM(
		TEXT("campus.FillBotsTDM"),
		0,
		TEXT("1: a team-deathmatch room fills its empty slots with AI bots when the host presses START."),
		ECVF_Default);

	/** Everything that makes a bot beatable lives here. Easy shoots late, wide and in short bursts. */
	struct FSkill
	{
		float SightRange;
		/** Degrees of drifting aim error at 20 m, scaled up with distance. */
		float AimErrorDeg;
		/** Extra error per 10 m past the first 20. */
		float AimErrorPerMeterDeg;
		float ReactionSeconds;
		float TurnRateDeg;
		int32 BurstMin;
		int32 BurstMax;
		float BurstPauseMin;
		float BurstPauseMax;
		/** How much of the target's velocity the bot leads by (1 = perfect prediction). */
		float LeadShare;
		/** Chance per engage decision of breaking for cover. */
		float CoverChance;
	};

	constexpr FSkill Easy  = { 4000.f, 4.5f, 0.09f, 0.62f, 170.f, 2, 4, 0.85f, 1.70f, 0.25f, 0.45f };
	constexpr FSkill Normal = { 6000.f, 2.4f, 0.05f, 0.30f, 320.f, 4, 7, 0.45f, 0.95f, 0.60f, 0.30f };

	const FSkill& Of(ECampusBotSkill Skill)
	{
		return Skill == ECampusBotSkill::Easy ? Easy : Normal;
	}

	/** How long a bot keeps chasing a target it can no longer see. */
	constexpr float MemorySeconds = 2.6f;
	/** Acquisition cone (half-angle). A bot has no eyes in the back of its head. */
	constexpr float ViewHalfAngleCos = 0.26f; // ~75 deg
	/** The one place the shot start must satisfy: ServerFire refuses a muzzle more than 250 cm off. */
	constexpr float ChestOffsetZ = 40.f;
	/** Beyond this from every human, nothing about a bot is worth a phone's frame. */
	constexpr float AnimCutoff = 6500.f;

	/** First-year names: a bot has to read like another student in the match, never like a bot.
	 *  The "BOT" tag belongs to the post-match table only (ACampusBotController::PostMatchTag). */
	const TCHAR* const Names[] = {
		TEXT("Fresher_Ravi"), TEXT("Fresher_Aarav"), TEXT("Fresher_Vikram"), TEXT("Fresher_Ishan"),
		TEXT("Fresher_Rohit"), TEXT("Fresher_Karan"), TEXT("Fresher_Dev"), TEXT("Fresher_Arjun"),
		TEXT("Fresher_Sahil"), TEXT("Fresher_Nikhil"), TEXT("Fresher_Yash"), TEXT("Fresher_Manav"),
		TEXT("Fresher_Tanmay"), TEXT("Fresher_Imran"), TEXT("Fresher_Pranav"), TEXT("Fresher_Zaid")
	};

	/** Every live ACampusCharacter in the world, rebuilt at most once a frame and shared by all bots:
	 *  a TActorIterator per bot per sense tick is the kind of cost that only shows up on a phone. */
	struct FCharacterCache
	{
		TWeakObjectPtr<UWorld> World;
		TArray<TWeakObjectPtr<ACampusCharacter>> Characters;
		uint64 Frame = 0;
	};
	FCharacterCache GCache;

	const TArray<TWeakObjectPtr<ACampusCharacter>>& Characters(UWorld* World)
	{
		if (GCache.World.Get() != World || GCache.Frame != GFrameCounter)
		{
			GCache.World = World;
			GCache.Frame = GFrameCounter;
			GCache.Characters.Reset();
			for (TActorIterator<ACampusCharacter> It(World); It; ++It)
			{
				if (!It->IsPendingKillPending() && It->GetHealth() > 0.f)
				{
					GCache.Characters.Add(*It);
				}
			}
		}
		return GCache.Characters;
	}

	/** Distance to the nearest pawn a person is actually looking through. Drives every budget below. */
	float DistanceToNearestHuman(const UWorld* World, const FVector& From)
	{
		float Best = FLT_MAX;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* Player = It->Get();
			const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
			if (Pawn)
			{
				Best = FMath::Min(Best, float(FVector::Dist(Pawn->GetActorLocation(), From)));
			}
		}
		return Best;
	}

	/** The BR zone, found by class name so this module never depends on the BR code (which may not
	 *  exist yet). Center / radius are read reflectively: any UPROPERTY under one of these names works. */
	TWeakObjectPtr<AActor> GZone;
	float GNextZoneFind = -1.f;

	AActor* FindZone(UWorld* World)
	{
		const float Now = World->GetTimeSeconds();
		if (AActor* Cached = GZone.Get(); Cached && Cached->GetWorld() == World && !Cached->IsPendingKillPending())
		{
			return Cached;
		}
		if (Now < GNextZoneFind)
		{
			return nullptr;
		}
		GNextZoneFind = Now + 5.f; // another agent's actor may appear at any time; look again, but not often
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			const FString ClassName = It->GetClass()->GetName();
			if (ClassName == TEXT("CampusBRZone") || ClassName == TEXT("CampusBRZone_C"))
			{
				GZone = *It;
				return *It;
			}
		}
		return nullptr;
	}

	bool ReadVector(const AActor* Actor, const TCHAR* Name, FVector& Out)
	{
		if (const FStructProperty* Property = FindFProperty<FStructProperty>(Actor->GetClass(), Name))
		{
			if (Property->Struct == TBaseStructure<FVector>::Get())
			{
				Out = *Property->ContainerPtrToValuePtr<FVector>(Actor);
				return true;
			}
		}
		return false;
	}

	bool ReadScalar(const AActor* Actor, const TCHAR* Name, float& Out)
	{
		if (const FProperty* Property = FindFProperty<FProperty>(Actor->GetClass(), Name))
		{
			if (const FFloatProperty* AsFloat = CastField<FFloatProperty>(Property))
			{
				Out = AsFloat->GetPropertyValue_InContainer(Actor);
				return true;
			}
			if (const FDoubleProperty* AsDouble = CastField<FDoubleProperty>(Property))
			{
				Out = float(AsDouble->GetPropertyValue_InContainer(Actor));
				return true;
			}
		}
		return false;
	}

	/** The safe circle, when a BR zone actor is in the level. False means "no zone": roam normally. */
	bool SafeCircle(UWorld* World, FVector& OutCenter, float& OutRadius)
	{
		AActor* Zone = FindZone(World);
		if (!Zone)
		{
			return false;
		}
		static const TCHAR* const CenterNames[] = { TEXT("Center"), TEXT("SafeCenter"), TEXT("ZoneCenter"), TEXT("CurrentCenter") };
		static const TCHAR* const RadiusNames[] = { TEXT("Radius"), TEXT("SafeRadius"), TEXT("ZoneRadius"), TEXT("CurrentRadius") };
		OutCenter = Zone->GetActorLocation();
		OutRadius = 0.f;
		for (const TCHAR* Name : CenterNames)
		{
			if (ReadVector(Zone, Name, OutCenter))
			{
				break;
			}
		}
		for (const TCHAR* Name : RadiusNames)
		{
			if (ReadScalar(Zone, Name, OutRadius) && OutRadius > 0.f)
			{
				break;
			}
		}
		return OutRadius > 100.f; // a zone we cannot read the size of is a zone we ignore
	}
}

ACampusBotController::ACampusBotController()
{
	// The whole reason a bot works with teams, the scoreboard, the kill feed and the result table:
	// it owns a real ACampusPlayerState (AController::InitPlayerState uses the game mode's class).
	bWantsPlayerState = true;
	// Path following must never steer the aim; AimAt owns the control rotation (UpdateControlRotation is a no-op).
	bAllowStrafe = true;
	bSetControlRotationFromPawnOrientation = false;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f; // 10 Hz until TickBudget decides otherwise
}

void ACampusBotController::SetSkill(ECampusBotSkill InSkill)
{
	Skill = InSkill;
}

ACampusPlayerState* ACampusBotController::GetCampusState() const
{
	return GetPlayerState<ACampusPlayerState>();
}

FString ACampusBotController::PostMatchTag(const APlayerState* State)
{
	return State && State->IsABot() ? TEXT("  BOT") : TEXT("");
}

int32 ACampusBotController::CountBots(const UWorld* World)
{
	int32 Count = 0;
	if (World)
	{
		for (TActorIterator<ACampusBotController> It(World); It; ++It)
		{
			Count += It->IsPendingKillPending() ? 0 : 1;
		}
	}
	return Count;
}

void ACampusBotController::SpawnBots(UWorld* World, int32 Count, const TArray<int32>& TeamIds, const TArray<FTransform>& Spawns)
{
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameMode || !GameState || Count <= 0)
	{
		return; // clients never run bots, and without a game mode there is no PlayerState class to use
	}
	Count = FMath::Min(Count, MaxPhysicalBots - CountBots(World));
	if (Count <= 0)
	{
		return;
	}

	// Names a human in the room does not already have.
	TSet<FString> Taken;
	int32 Red = 0;
	int32 Blue = 0;
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		if (const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player))
		{
			Taken.Add(Campus->GetPlayerName());
			(Campus->Team == ECampusTeam::Red ? Red : Blue)++;
		}
	}

	for (int32 Index = 0; Index < Count; ++Index)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ACampusBotController* Bot = World->SpawnActor<ACampusBotController>(ACampusBotController::StaticClass(), FTransform::Identity, Params);
		ACampusPlayerState* State = Bot ? Bot->GetCampusState() : nullptr;
		if (!State)
		{
			continue; // no PlayerState: this bot could not be part of a team, so it is not a bot at all
		}
		// Teams: the caller's list wins, otherwise balance the room exactly like PostLogin does.
		const bool bRed = TeamIds.Num() > 0 ? TeamIds[Index % TeamIds.Num()] == 0 : Red <= Blue;
		State->Team = bRed ? ECampusTeam::Red : ECampusTeam::Blue;
		(bRed ? Red : Blue)++;
		State->SetIsABot(true); // InitPlayerState does not set this; the post-match tag reads it
		// AccountId stays empty on purpose: ReportResult skips bots, so no bot ever lands in the API's table.
		const int32 NameCount = int32(UE_ARRAY_COUNT(CampusBotTuning::Names));
		for (int32 Try = 0; Try < NameCount; ++Try)
		{
			const FString Candidate = CampusBotTuning::Names[(Index + Try) % NameCount];
			if (!Taken.Contains(Candidate))
			{
				Taken.Add(Candidate);
				State->SetPlayerName(Candidate);
				break;
			}
		}
		// A mix, so a squad of bots is never uniformly sharp. Easy is the majority on purpose.
		Bot->SetSkill(Index % 3 == 0 ? ECampusBotSkill::Normal : ECampusBotSkill::Easy);

		if (Spawns.IsValidIndex(Index))
		{
			GameMode->RestartPlayerAtTransform(Bot, Spawns[Index]);
		}
		else
		{
			GameMode->RestartPlayer(Bot); // the match game mode puts it on its team's pad
		}
	}
}

void ACampusBotController::FillTeamDeathmatch(UWorld* World)
{
	if (CampusBotTuning::CVarFillBotsTDM.GetValueOnGameThread() <= 0)
	{
		return;
	}
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	if (!State)
	{
		return;
	}
	int32 Red = 0;
	int32 Blue = 0;
	for (const APlayerState* Player : State->PlayerArray)
	{
		if (const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player))
		{
			(Campus->Team == ECampusTeam::Red ? Red : Blue)++;
		}
	}
	const int32 PerTeam = FMath::Max(State->TeamSize, 1);
	TArray<int32> TeamIds;
	for (int32 Index = Red; Index < PerTeam; ++Index)
	{
		TeamIds.Add(0);
	}
	for (int32 Index = Blue; Index < PerTeam; ++Index)
	{
		TeamIds.Add(1);
	}
	SpawnBots(World, TeamIds.Num(), TeamIds, TArray<FTransform>());
}

void ACampusBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ACampusCharacter* Self = Cast<ACampusCharacter>(InPawn);
	if (!Self)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const ACampusGameState* MatchState = World ? World->GetGameState<ACampusGameState>() : nullptr;
	// Elimination: this pawn belongs to THIS round, and a bot killed in it stays out until the next.
	SpawnedRound = MatchState ? MatchState->Round : 0;
	PawnLostTime = -1.f;

	// Raise the rifle through the server's own path, so bArmed replicates and the equip anim plays
	// on every client exactly as it does for a player who pressed the button.
	Self->ServerSetArmed_Implementation(true);
	if (USkeletalMeshComponent* Mesh = Self->GetMesh())
	{
		Mesh->bEnableUpdateRateOptimizations = true; // a bot's pose may be coarse at distance
	}
	// Start facing where it spawned, not at world zero.
	SetControlRotation(FRotator(0.f, Self->GetActorRotation().Yaw, 0.f));
	Target.Reset();
	State = EState::Roam;
	bHasRoamPoint = false;
	NextRoamTime = 0.f;
	BurstLeft = 0;
}

void ACampusBotController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	// Called every AI tick by AAIController::Tick. The aim itself was already decided by AimAt / TickRoam;
	// this is only the handover: ACampusCharacter runs bUseControllerRotationYaw, which is applied in
	// APawn::FaceRotation and nowhere else - without this line a bot's body never turns.
	if (APawn* MyPawn = GetPawn(); MyPawn && bUpdatePawn)
	{
		MyPawn->FaceRotation(GetControlRotation(), DeltaTime);
	}
}

void ACampusBotController::OnUnPossess()
{
	Super::OnUnPossess();
	Target.Reset();
	BurstLeft = 0;
}

void ACampusBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}
	const ACampusGameState* MatchState = World->GetGameState<ACampusGameState>();
	ACampusCharacter* Self = Cast<ACampusCharacter>(GetPawn());
	if (!Self || Self->IsPendingKillPending() || Self->GetHealth() <= 0.f)
	{
		TickRespawn(MatchState);
		return;
	}
	PawnLostTime = -1.f;
	if (MatchState && MatchState->Phase == ECampusPhase::Playing && MatchState->Round != SpawnedRound)
	{
		TickRespawn(MatchState); // a round started while this bot was still standing: back to its pad
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now >= NextBudgetTime)
	{
		NextBudgetTime = Now + 1.f;
		TickBudget(Self);
	}
	// Outside a live round a bot stands still: no shooting in the waiting room or on the end screen.
	if (MatchState && MatchState->Phase != ECampusPhase::Playing)
	{
		SetStick(Self, FVector2D::ZeroVector);
		return;
	}
	TickSenses(Self);

	switch (State)
	{
	case EState::Engage:
		TickEngage(Self, DeltaSeconds);
		break;
	case EState::Cover:
		TickCover(Self);
		break;
	default:
		TickRoam(Self);
		break;
	}
}

void ACampusBotController::TickRespawn(const ACampusGameState* MatchState)
{
	UWorld* World = GetWorld();
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	if (!GameMode)
	{
		return;
	}
	const ECampusPhase Phase = MatchState ? MatchState->Phase : ECampusPhase::Waiting;
	const int32 Round = MatchState ? MatchState->Round : 0;
	if (Phase == ECampusPhase::Ended)
	{
		return;
	}
	if (APawn* Stale = GetPawn())
	{
		// Only reached when the round moved on (or the pawn is dying): the game mode destroys player
		// pawns itself in StartRound, but it only iterates player controllers - this is our half of it.
		if (Phase != ECampusPhase::Playing || Round == SpawnedRound)
		{
			return;
		}
		Stale->Destroy();
		PawnLostTime = -1.f;
	}
	if (Phase == ECampusPhase::Playing)
	{
		if (Round == SpawnedRound)
		{
			return; // eliminated for this round, exactly like a player
		}
		GameMode->RestartPlayer(this);
		return;
	}
	// Waiting / RoundOver: ACampusMatchGameMode::RespawnLater owns the timer. Step in only if it never came.
	const float Now = World->GetTimeSeconds();
	if (PawnLostTime < 0.f)
	{
		PawnLostTime = Now;
	}
	else if (Now - PawnLostTime > 4.f)
	{
		PawnLostTime = Now;
		GameMode->RestartPlayer(this);
	}
}

void ACampusBotController::TickBudget(ACampusCharacter* Self)
{
	UWorld* World = GetWorld();
	const float Distance = CampusBotTuning::DistanceToNearestHuman(World, Self->GetActorLocation());

	// Four bands. A bot fighting the player thinks at 20 Hz; one across the campus thinks twice a second.
	const float Interval = Distance < 2500.f ? 0.05f : (Distance < 6000.f ? 0.1f : (Distance < 14000.f ? 0.25f : 0.5f));
	if (!FMath::IsNearlyEqual(PrimaryActorTick.TickInterval, Interval))
	{
		PrimaryActorTick.TickInterval = Interval;
	}

	// Replication: a bot nobody can see does not need 60 updates a second of its own.
	Self->SetNetUpdateFrequency(Distance < 6000.f ? 60.f : (Distance < 14000.f ? 20.f : 6.f));
	Self->SetMinNetUpdateFrequency(Distance < 6000.f ? 30.f : 4.f);

	// Anim: the mesh already skips its pose when nothing renders it (OnlyTickMontages...), which is what
	// keeps the animated-character count near the host. Past AnimCutoff we stop the component outright,
	// so a far bot costs one capsule move and nothing else.
	if (USkeletalMeshComponent* Mesh = Self->GetMesh())
	{
		const bool bWantsAnim = Distance < CampusBotTuning::AnimCutoff;
		if (Mesh->IsComponentTickEnabled() != bWantsAnim)
		{
			Mesh->SetComponentTickEnabled(bWantsAnim);
		}
	}
}

void ACampusBotController::TickSenses(ACampusCharacter* Self)
{
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	const ACampusCharacter* Known = Target.Get();
	if (Known && (Known->IsPendingKillPending() || Known->GetHealth() <= 0.f))
	{
		Target.Reset();
		Known = nullptr;
	}
	if (Now < NextSenseTime)
	{
		return;
	}
	NextSenseTime = Now + 0.25f; // 4 Hz, whatever rate the state machine runs at

	const CampusBotTuning::FSkill& Tuning = CampusBotTuning::Of(Skill);
	const ACampusPlayerState* Mine = GetCampusState();
	const FVector Eye = Self->GetPawnViewLocation();
	const FVector Facing = GetControlRotation().Vector();

	// Nearest enemy in the cone, then at most two line checks (the cheapest thing that still feels fair).
	TArray<ACampusCharacter*, TInlineAllocator<8>> Candidates;
	for (const TWeakObjectPtr<ACampusCharacter>& Weak : CampusBotTuning::Characters(World))
	{
		ACampusCharacter* Other = Weak.Get();
		const ACampusPlayerState* Theirs = Other ? Other->GetPlayerState<ACampusPlayerState>() : nullptr;
		// SIDES, through the one shared test: RED/BLUE in a room, the SQUAD in battle royale. Reading
		// the colours here made every BR bot think all fifty combatants were teammates - they all carry
		// the same default - so a bot never fired a shot in a battle session.
		if (!Other || Other == Self || !Theirs || !Mine
			|| CampusBR::AreFriendly(World, this, Other->GetController()))
		{
			continue;
		}
		const FVector ToOther = Other->GetActorLocation() - Eye;
		const float DistanceSq = ToOther.SizeSquared();
		if (DistanceSq > FMath::Square(Tuning.SightRange))
		{
			continue;
		}
		if (Other != Known && FVector::DotProduct(ToOther.GetSafeNormal(), Facing) < CampusBotTuning::ViewHalfAngleCos)
		{
			continue; // outside the view cone (the one it is already fighting stays visible)
		}
		Candidates.Add(Other);
	}
	Candidates.Sort([&Eye](const ACampusCharacter& A, const ACampusCharacter& B)
	{
		return FVector::DistSquared(A.GetActorLocation(), Eye) < FVector::DistSquared(B.GetActorLocation(), Eye);
	});

	ACampusCharacter* Seen = nullptr;
	for (int32 Index = 0; Index < FMath::Min(2, Candidates.Num()); ++Index)
	{
		if (HasLineOfSight(Self, Candidates[Index]))
		{
			Seen = Candidates[Index];
			break;
		}
	}
	if (Seen)
	{
		if (Seen != Known)
		{
			// A new face costs a reaction: this is most of what separates a bot from an aimbot.
			ReactionTime = Now + Tuning.ReactionSeconds * FMath::FRandRange(0.8f, 1.4f);
			BurstLeft = 0;
			NextBurstTime = ReactionTime;
		}
		Target = Seen;
		LastSeenTime = Now;
		if (State != EState::Cover)
		{
			State = EState::Engage;
		}
		return;
	}
	if (Known && Now - LastSeenTime < CampusBotTuning::MemorySeconds)
	{
		return; // lost sight for a moment: keep pushing toward where it was
	}
	Target.Reset();
	if (State != EState::Cover)
	{
		State = EState::Roam;
	}
}

bool ACampusBotController::HasLineOfSight(const ACampusCharacter* Self, const ACampusCharacter* Other) const
{
	// Same object set the server's own bullet uses, so "I can see you" and "I can hit you" agree.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotSight), false, Self);
	Params.AddIgnoredActor(Other);
	FHitResult Hit;
	const FVector Eye = Self->GetPawnViewLocation();
	const FVector Chest = Other->GetActorLocation() + FVector(0.f, 0.f, CampusBotTuning::ChestOffsetZ);
	return !GetWorld()->LineTraceSingleByObjectType(Hit, Eye, Chest, Objects, Params);
}

FVector ACampusBotController::AimPointFor(const ACampusCharacter* InTarget, float Distance) const
{
	const CampusBotTuning::FSkill& Tuning = CampusBotTuning::Of(Skill);
	// Centre mass, never the head: a bot should not farm 2x headshots through the server's hit zones.
	FVector Point = InTarget->GetActorLocation() + FVector(0.f, 0.f, CampusBotTuning::ChestOffsetZ);
	// Partial lead on a running target, with the travel time a hitscan does not even have: it is a
	// believable error, not a prediction (and it is what makes strafing work against bots).
	const float Travel = FMath::Min(Distance / 9000.f, 0.25f);
	return Point + InTarget->GetVelocity() * Travel * Tuning.LeadShare;
}

void ACampusBotController::AimAt(ACampusCharacter* Self, const FVector& Point, float DeltaSeconds)
{
	const CampusBotTuning::FSkill& Tuning = CampusBotTuning::Of(Skill);
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	const FVector Eye = Self->GetPawnViewLocation();
	const float Distance = FVector::Dist(Eye, Point);

	// Error drifts between held values instead of jittering per shot: a bot that shakes reads as a bug,
	// a bot that drifts reads as a person who cannot hold the sight still.
	const float Spread = Tuning.AimErrorDeg + Tuning.AimErrorPerMeterDeg * FMath::Max(0.f, (Distance - 2000.f) / 100.f);
	if (Now >= NextErrorTime)
	{
		NextErrorTime = Now + FMath::FRandRange(0.35f, 0.8f);
		AimErrorTarget = FVector2D(FMath::FRandRange(-Spread, Spread), FMath::FRandRange(-Spread, Spread) * 0.6f);
	}
	AimError = FMath::Vector2DInterpTo(AimError, AimErrorTarget, DeltaSeconds, 6.f);

	FRotator Wanted = (Point - Eye).Rotation();
	Wanted.Yaw += AimError.X;
	Wanted.Pitch = FMath::Clamp(Wanted.Pitch + AimError.Y, -70.f, 70.f);
	Wanted.Roll = 0.f;
	// Turning takes time, and that time is a window the player can use.
	SetControlRotation(FMath::RInterpConstantTo(GetControlRotation(), Wanted, DeltaSeconds, Tuning.TurnRateDeg));
}

bool ACampusBotController::ManageAmmo(ACampusCharacter* Self)
{
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	if (Now < Self->ServerReloadEndTime)
	{
		return false;
	}
	if (Self->ServerAmmoInMag > 0)
	{
		return true;
	}
	// The server's own reload, same function the player's RPC lands in (anim multicast included).
	Self->ServerReload_Implementation();
	return false;
}

void ACampusBotController::FireOnce(ACampusCharacter* Self)
{
	// THE player path: ServerFire is where a phone's shot arrives, so a bot's bullet is judged by the
	// same ammo / rate / reload gate, the same lag compensation, falloff, hit zones and team check.
	// ServerFire refuses a start more than 250 cm from the body, hence the pawn's own view location.
	const FVector Start = Self->GetPawnViewLocation();
	const FVector Direction = GetControlRotation().Vector();
	Self->ServerFire_Implementation(Start, Direction);
}

void ACampusBotController::TickEngage(ACampusCharacter* Self, float DeltaSeconds)
{
	ACampusCharacter* Enemy = Target.Get();
	if (!Enemy)
	{
		State = EState::Roam;
		return;
	}
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	const CampusBotTuning::FSkill& Tuning = CampusBotTuning::Of(Skill);
	const FVector Eye = Self->GetPawnViewLocation();
	const float Distance = FVector::Dist(Eye, Enemy->GetActorLocation());
	const FVector AimPoint = AimPointFor(Enemy, Distance);
	AimAt(Self, AimPoint, DeltaSeconds);

	// Hurt, or out of bullets in the middle of a fight: break away instead of standing and dying.
	const bool bHurt = Self->GetHealth() < Self->GetMaxHealth() * 0.35f;
	if (Now >= NextCoverChance)
	{
		NextCoverChance = Now + 2.5f;
		if ((bHurt || Self->ServerAmmoInMag <= 0) && FMath::FRand() < Tuning.CoverChance)
		{
			FVector Cover;
			if (PickCoverPoint(Self, Cover))
			{
				State = EState::Cover;
				CoverUntil = Now + FMath::FRandRange(2.f, 4.f);
				MoveToPoint(Self, Cover);
				return;
			}
		}
	}

	// Aim down the sight only at range: a hip-fire bot up close is easier to beat, which is the point.
	Self->ServerSetAiming_Implementation(Distance > 1800.f);

	// Hold the distance: close in when far, back off when it is in your face, strafe in between.
	StopMovement();
	if (Now >= NextStrafeTime)
	{
		NextStrafeTime = Now + FMath::FRandRange(0.8f, 2.2f);
		StrafeSign = FMath::FRand() < 0.5f ? -1.f : 1.f;
	}
	const float Forward = Distance > 2600.f ? 0.8f : (Distance < 700.f ? -0.6f : 0.f);
	SetStick(Self, FVector2D(StrafeSign * 0.55f, Forward));

	if (!ManageAmmo(Self) || Now < ReactionTime)
	{
		return;
	}
	// Only shoot once the sight is actually near the target; the turn rate decides when that is.
	const FRotator ToTarget = (AimPoint - Eye).Rotation();
	const float Off = FMath::Abs(FRotator::NormalizeAxis(ToTarget.Yaw - GetControlRotation().Yaw))
		+ FMath::Abs(FRotator::NormalizeAxis(ToTarget.Pitch - GetControlRotation().Pitch));
	if (Off > 9.f)
	{
		return;
	}
	if (BurstLeft <= 0)
	{
		if (Now < NextBurstTime)
		{
			return;
		}
		BurstLeft = FMath::RandRange(Tuning.BurstMin, Tuning.BurstMax);
	}
	// One round per AI update: at 20 Hz close in that is ~1200 rpm of intent, which ServerFire's own
	// fire-rate credit clamps to the rifle's 600. The gun, not the AI, sets the cadence.
	FireOnce(Self);
	if (--BurstLeft <= 0)
	{
		NextBurstTime = Now + FMath::FRandRange(Tuning.BurstPauseMin, Tuning.BurstPauseMax);
	}
}

void ACampusBotController::TickCover(ACampusCharacter* Self)
{
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	ManageAmmo(Self); // the point of cover: reload here
	if (Now >= CoverUntil)
	{
		State = Target.IsValid() ? EState::Engage : EState::Roam;
		return;
	}
	if (const ACampusCharacter* Enemy = Target.Get())
	{
		// Keep the body pointed away from the threat while running, so the move is a real retreat.
		const FVector Away = (Self->GetActorLocation() - Enemy->GetActorLocation()).GetSafeNormal2D();
		SetControlRotation(FMath::RInterpConstantTo(GetControlRotation(), Away.Rotation(), World->GetDeltaSeconds(), 240.f));
	}
	Self->ServerSetAiming_Implementation(false);
	// Path following (or the stick fallback in MoveToPoint) is already carrying it to the cover point.
}

void ACampusBotController::TickRoam(ACampusCharacter* Self)
{
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();
	Self->ServerSetAiming_Implementation(false);

	const bool bArrived = bHasRoamPoint && FVector::DistSquared2D(RoamPoint, Self->GetActorLocation()) < FMath::Square(250.f);
	if (!bHasRoamPoint || bArrived || Now >= NextRoamTime)
	{
		PickRoamPoint(Self);
		NextRoamTime = Now + 12.f; // a stuck bot gives up on its point instead of pushing a wall forever
	}
	if (bHasRoamPoint)
	{
		// Face the way it walks; a roaming bot looking sideways reads as broken.
		const FVector Direction = (RoamPoint - Self->GetActorLocation()).GetSafeNormal2D();
		if (!Direction.IsNearlyZero())
		{
			SetControlRotation(FMath::RInterpConstantTo(GetControlRotation(), Direction.Rotation(), World->GetDeltaSeconds(), 180.f));
		}
		MoveToPoint(Self, RoamPoint);
	}
}

void ACampusBotController::PickRoamPoint(ACampusCharacter* Self)
{
	UWorld* World = GetWorld();
	const FVector Here = Self->GetActorLocation();
	FVector Center = Here;
	float Radius = 3500.f;

	// A BR zone, when one exists: roam inside the safe circle, and run for it when caught outside.
	FVector SafeCenter;
	float SafeRadius = 0.f;
	if (CampusBotTuning::SafeCircle(World, SafeCenter, SafeRadius))
	{
		const float Out = FVector::Dist2D(Here, SafeCenter) - SafeRadius * 0.8f;
		if (Out > 0.f)
		{
			bHasRoamPoint = true;
			RoamPoint = SafeCenter + (Here - SafeCenter).GetSafeNormal2D() * SafeRadius * 0.5f;
			RoamPoint.Z = Here.Z;
			return;
		}
		Center = SafeCenter;
		Radius = FMath::Min(Radius, SafeRadius * 0.8f);
	}

	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		FNavLocation Result;
		if (Nav->GetRandomReachablePointInRadius(Here, Radius, Result))
		{
			RoamPoint = Result.Location;
			bHasRoamPoint = true;
			return;
		}
	}
	// No NavMesh in this level: pick a direction that is not into a wall and walk it (MoveToPoint
	// falls back to the character's own stick, so this still produces real movement).
	const FVector Facing = GetControlRotation().Vector().GetSafeNormal2D();
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotRoam), false, Self);
	for (int32 Try = 0; Try < 5; ++Try)
	{
		const FVector Direction = Try == 0 ? Facing : FRotator(0.f, FMath::FRandRange(-180.f, 180.f), 0.f).Vector();
		const FVector Candidate = Here + Direction * FMath::FRandRange(900.f, 2600.f);
		FHitResult Hit;
		if (!World->LineTraceSingleByObjectType(Hit, Here + FVector(0.f, 0.f, 50.f), Candidate + FVector(0.f, 0.f, 50.f), Objects, Params))
		{
			RoamPoint = Candidate;
			bHasRoamPoint = true;
			return;
		}
	}
	bHasRoamPoint = false;
}

bool ACampusBotController::PickCoverPoint(ACampusCharacter* Self, FVector& OutPoint) const
{
	const ACampusCharacter* Enemy = Target.Get();
	const FVector Here = Self->GetActorLocation();
	if (!Enemy)
	{
		return false;
	}
	UWorld* World = GetWorld();
	const FVector Chest = Enemy->GetActorLocation() + FVector(0.f, 0.f, CampusBotTuning::ChestOffsetZ);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusBotCover), false, Self);
	Params.AddIgnoredActor(Enemy);

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	// Three candidates, three traces: a spot nearby that the enemy cannot see into.
	for (int32 Try = 0; Try < 3; ++Try)
	{
		FVector Candidate = Here + FRotator(0.f, FMath::FRandRange(-180.f, 180.f), 0.f).Vector() * FMath::FRandRange(600.f, 1400.f);
		if (Nav)
		{
			FNavLocation Result;
			if (!Nav->GetRandomReachablePointInRadius(Here, 1400.f, Result))
			{
				continue;
			}
			Candidate = Result.Location;
		}
		FHitResult Hit;
		if (World->LineTraceSingleByObjectType(Hit, Chest, Candidate + FVector(0.f, 0.f, CampusBotTuning::ChestOffsetZ), Objects, Params))
		{
			OutPoint = Candidate;
			return true;
		}
	}
	return false;
}

bool ACampusBotController::MoveToPoint(ACampusCharacter* Self, const FVector& Point)
{
	// With a NavMesh, path following moves the pawn every frame on its own - which is exactly what a
	// 2 Hz AI update needs. Without one, the character's own stick path does it instead.
	if (FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		// Only ever ask for a NEW path: a pathfind per AI update (up to 20 Hz, ten bots) is the one
		// cost that would actually break a phone host.
		if (GetMoveStatus() == EPathFollowingStatus::Moving
			&& FVector::DistSquared(LastMoveGoal, Point) < FMath::Square(150.f))
		{
			return true;
		}
		const EPathFollowingRequestResult::Type Result = MoveToLocation(Point, 120.f, true, true, false, true);
		if (Result != EPathFollowingRequestResult::Failed)
		{
			LastMoveGoal = Point;
			SetStick(Self, FVector2D::ZeroVector);
			return true;
		}
	}
	const FVector Direction = (Point - Self->GetActorLocation()).GetSafeNormal2D();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Local = Yaw.UnrotateVector(Direction);
	SetStick(Self, FVector2D(Local.Y, Local.X));
	return false;
}

void ACampusBotController::SetStick(ACampusCharacter* Self, const FVector2D& Axis)
{
	// The on-screen stick: ACampusCharacter::Tick applies it every frame, relative to the control
	// rotation, through ApplyMove - the same code a phone's thumb drives, speed rules included.
	Self->SetTouchMove(Axis);
}
