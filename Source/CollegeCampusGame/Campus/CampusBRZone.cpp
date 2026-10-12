#include "Campus/CampusBRZone.h"

#include "Campus/CampusBRPlane.h"
#include "Campus/CampusCharacter.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace CampusBRZoneTuning
{
	// Five circles over ~9 minutes (Docs/BATTLE_ROYALE_DESIGN.md 3.11). The design table is written in
	// metres for a 1500 m map; stored here as SHARES of the first circle so the same schedule holds on
	// this 227 m campus and on the big map later. Damage is per second as a share of 100 max HP.
	const FCampusBRZonePhase Phases[] = {
		//  wait, shrink, share of the FIRST circle (R(n) = R(n-1) * 0.58), damage / second
		{ 75.f, 60.f, 0.580f,  0.5f },
		{ 45.f, 50.f, 0.336f,  1.0f },
		{ 40.f, 45.f, 0.195f,  2.5f },
		{ 35.f, 40.f, 0.113f,  5.0f },
		{ 30.f, 45.f, 0.028f,  9.0f },
	};

	/** After the last circle: sudden death, to force a conclusion inside the thermal budget. */
	constexpr float SuddenDeathDamage = 14.f;

	/** The first circle: the whole map square, less a margin so the edge is reachable on every side. */
	constexpr float FirstRadiusShare = 0.95f;
	/** Damage ticks once a second, like every battle royale: a readable bite, not a trickle. */
	constexpr float BurnInterval = 1.f;

	TArray<TWeakObjectPtr<ACampusBRZone>> Live;
}

float CampusBRServerTime(const UWorld* World)
{
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State ? float(State->GetServerWorldTimeSeconds()) : float(World ? World->GetTimeSeconds() : 0.f);
}

ACampusBRZone::ACampusBRZone()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f; // the circle is a slow thing; the HUD interpolates between ticks
	bReplicates = true;
	bAlwaysRelevant = true; // everyone needs the circle, wherever they are standing
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);
	SetCanBeDamaged(false);
}

void ACampusBRZone::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusBRZone, CenterFrom);
	DOREPLIFETIME(ACampusBRZone, CenterTo);
	DOREPLIFETIME(ACampusBRZone, RadiusFrom);
	DOREPLIFETIME(ACampusBRZone, RadiusTo);
	DOREPLIFETIME(ACampusBRZone, EventStartTime);
	DOREPLIFETIME(ACampusBRZone, EventEndTime);
	DOREPLIFETIME(ACampusBRZone, bShrinking);
	DOREPLIFETIME(ACampusBRZone, PhaseIndex);
	DOREPLIFETIME(ACampusBRZone, DamagePerSecond);
}

void ACampusBRZone::BeginPlay()
{
	Super::BeginPlay();
	// Registered on EVERY machine, not just the host: the HUD ring asks Get() on the client.
	CampusBRZoneTuning::Live.AddUnique(this);
}

void ACampusBRZone::EndPlay(const EEndPlayReason::Type Reason)
{
	CampusBRZoneTuning::Live.Remove(this);
	Super::EndPlay(Reason);
}

ACampusBRZone* ACampusBRZone::Get(const UWorld* World)
{
	for (int32 Index = CampusBRZoneTuning::Live.Num() - 1; Index >= 0; --Index)
	{
		ACampusBRZone* Zone = CampusBRZoneTuning::Live[Index].Get();
		if (!Zone)
		{
			CampusBRZoneTuning::Live.RemoveAtSwap(Index);
		}
		else if (Zone->GetWorld() == World)
		{
			return Zone;
		}
	}
	return nullptr;
}

const FCampusBRZonePhase& ACampusBRZone::PhaseAt(int32 Index)
{
	const int32 Count = int32(UE_ARRAY_COUNT(CampusBRZoneTuning::Phases));
	return CampusBRZoneTuning::Phases[FMath::Clamp(Index, 0, Count - 1)];
}

int32 ACampusBRZone::GetPhaseCount() const
{
	return int32(UE_ARRAY_COUNT(CampusBRZoneTuning::Phases));
}

void ACampusBRZone::Begin()
{
	if (!HasAuthority())
	{
		return;
	}
	const FVector2D Middle = CampusBRMap::Center();
	const float First = CampusBRMap::HalfSpan() * CampusBRZoneTuning::FirstRadiusShare;
	CenterFrom = FVector_NetQuantize(Middle.X, Middle.Y, 0.f);
	CenterTo = CenterFrom;
	RadiusFrom = First;
	RadiusTo = First;
	bRunning = true;
	OpenPhase(0);
}

void ACampusBRZone::Freeze()
{
	bRunning = false;
	DamagePerSecond = 0.f;
	bShrinking = false;
	GetWorldTimerManager().ClearTimer(PhaseTimer);
}

void ACampusBRZone::OpenPhase(int32 Index)
{
	if (!HasAuthority() || !bRunning)
	{
		return;
	}
	if (Index >= GetPhaseCount())
	{
		// Final circle reached: it stays where it is and keeps burning at the last rate.
		bShrinking = false;
		DamagePerSecond = CampusBRZoneTuning::SuddenDeathDamage;
		EventStartTime = EventEndTime = float(GetWorld()->GetTimeSeconds());
		return;
	}
	const FCampusBRZonePhase& Phase = PhaseAt(Index);
	PhaseIndex = Index;
	DamagePerSecond = Phase.DamagePerSecond;
	// Announce the next circle at the START of the wait, so players can run for it before it closes -
	// that choice is the whole game, and hiding it until the shrink begins makes the zone feel unfair.
	PickNextCircle(CampusBRMap::HalfSpan() * CampusBRZoneTuning::FirstRadiusShare * Phase.RadiusShare);
	bShrinking = false;
	EventStartTime = float(GetWorld()->GetTimeSeconds());
	EventEndTime = EventStartTime + Phase.WaitSeconds;

	GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [this, Index]()
	{
		const FCampusBRZonePhase& Shrinking = PhaseAt(Index);
		bShrinking = true;
		EventStartTime = float(GetWorld()->GetTimeSeconds());
		EventEndTime = EventStartTime + Shrinking.ShrinkSeconds;
		GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [this, Index]()
		{
			// the circle we were closing on is now the circle we are in
			CenterFrom = CenterTo;
			RadiusFrom = RadiusTo;
			OpenPhase(Index + 1);
		}), Shrinking.ShrinkSeconds, false);
	}), Phase.WaitSeconds, false);
}

void ACampusBRZone::PickNextCircle(float NewRadius)
{
	// The next circle must sit wholly inside this one, else players inside the safe area would be
	// dropped outside by a shrink they could not have run from.
	const float Slack = FMath::Max(RadiusFrom - NewRadius, 0.f);
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const float Distance = Slack * FMath::Sqrt(FMath::FRand()); // uniform over the disc, not bunched in the middle
	CenterTo = FVector_NetQuantize(
		CenterFrom.X + FMath::Cos(Angle) * Distance,
		CenterFrom.Y + FMath::Sin(Angle) * Distance,
		0.f);
	RadiusTo = NewRadius;
}

FVector2D ACampusBRZone::GetCenter() const
{
	if (!bShrinking || EventEndTime <= EventStartTime)
	{
		return FVector2D(CenterFrom.X, CenterFrom.Y);
	}
	const float Now = CampusBRServerTime(GetWorld());
	const float Alpha = FMath::Clamp((Now - EventStartTime) / (EventEndTime - EventStartTime), 0.f, 1.f);
	return FMath::Lerp(FVector2D(CenterFrom.X, CenterFrom.Y), FVector2D(CenterTo.X, CenterTo.Y), Alpha);
}

float ACampusBRZone::GetRadius() const
{
	if (!bShrinking || EventEndTime <= EventStartTime)
	{
		return RadiusFrom;
	}
	const float Now = CampusBRServerTime(GetWorld());
	const float Alpha = FMath::Clamp((Now - EventStartTime) / (EventEndTime - EventStartTime), 0.f, 1.f);
	return FMath::Lerp(RadiusFrom, RadiusTo, Alpha);
}

float ACampusBRZone::SecondsToEvent() const
{
	const float Now = CampusBRServerTime(GetWorld());
	return FMath::Max(0.f, EventEndTime - Now);
}

float ACampusBRZone::MetresOutside(const FVector& Where) const
{
	if (PhaseIndex < 0)
	{
		return 0.f;
	}
	const float Distance = FVector2D::Distance(FVector2D(Where.X, Where.Y), GetCenter());
	return FMath::Max(0.f, Distance - GetRadius()) * 0.01f;
}

void ACampusBRZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bRunning || DamagePerSecond <= 0.f)
	{
		return;
	}
	BurnAccumulator += DeltaSeconds;
	if (BurnAccumulator < CampusBRZoneTuning::BurnInterval)
	{
		return;
	}
	BurnAccumulator = 0.f;
	BurnOutside();
}

void ACampusBRZone::BurnOutside()
{
	const FVector2D Middle = GetCenter();
	const float RadiusSquared = FMath::Square(GetRadius());
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		ACampusCharacter* Character = *It;
		// Still in the plane or under the chute: the circle cannot reach you yet. Without this, anyone
		// who drops on the far side of the map lands already hurt by a zone they never had a chance to beat.
		if (!Character || Character->GetHealth() <= 0.f || ACampusBRPlane::IsDeploying(Character))
		{
			continue;
		}
		const FVector Where = Character->GetActorLocation();
		if (FVector2D::DistSquared(FVector2D(Where.X, Where.Y), Middle) > RadiusSquared)
		{
			// Further out hurts more (design 3.11): a step over the line is a nuisance, a rotation
			// you left too late is fatal.
			const float Outside = FMath::Sqrt(FVector2D::DistSquared(FVector2D(Where.X, Where.Y), Middle)) - GetRadius();
			const float Scale = 1.f + FMath::Min(Outside * 0.01f / 150.f, 1.5f);
			Character->ApplyHit(DamagePerSecond * Scale * CampusBRZoneTuning::BurnInterval, nullptr);
		}
	}
}
