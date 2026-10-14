#include "Campus/CampusBRZone.h"

#include "Campus/CampusBRPlane.h"
#include "Campus/CampusCharacter.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

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
	/** THE SMALLEST A CIRCLE MAY GET, on any map. The table above is shares of the FIRST circle, so the
	 *  final one is ~10 m of radius on the 800 m valley but would be 2.2 m on the 167 m Campus Grounds -
	 *  narrower than the clock tower it closes on, which is a coin toss and not a fight. The valley
	 *  never reaches this floor; the two campus squares stop here. */
	constexpr float MinRadius = 600.f;
	/** Damage ticks once a second, like every battle royale: a readable bite, not a trickle. */
	constexpr float BurnInterval = 1.f;

	TArray<TWeakObjectPtr<ACampusBRZone>> Live;
}

/** The blue wall's shape and colour, copied off the three.js game (src/game/zone.js + game.js
 *  showZone): SAFE_ZONE_COLOR #3aa0ff, a 60 m-tall see-through glowing cylinder standing at the
 *  circle's edge, faint while the circle waits and stronger while it closes. */
namespace CampusBRWall
{
	/** Panels round the ring. 64 reads as a circle at 200 m across and still draws in one call. */
	constexpr int32 Panels = 64;
	/** The ring is built at this radius and then scaled; 100 cm keeps the scale factor = radius / 100. */
	constexpr float BuildRadius = 100.f;
	/** 100 m of wall, sunk 5 m so no gap shows on a slope. The three.js game uses 60 m on a ~230 m
	 *  map; Varsity Valley's first circle is 380 m across the radius, and at that distance 60 m of
	 *  wall is a low fence. Height does not scale with the circle - only the ring's radius does. */
	constexpr float Height = 10000.f;
	constexpr float Sink = 500.f;
	/** Panel thickness AT THE BUILD RADIUS, so it grows with the circle: 0.25 cm -> ~1 m at r = 380 m,
	 *  which is thin enough that two overlapping translucent faces never read as a solid. */
	constexpr float Thickness = 0.25f;
	/** #3aa0ff, the three.js SAFE_ZONE_COLOR, in linear space. */
	const FLinearColor Colour = FLinearColor(FColor(0x3A, 0xA0, 0xFF));
	/** game.js: opacity 0.12 while the circle holds, 0.28 while it closes. */
	constexpr float OpacityIdle = 0.12f;
	constexpr float OpacityClosing = 0.28f;
}

namespace CampusBRMap
{
	namespace
	{
		/** L_VarsityValley, Scripts/varsity_layout.py: HALF = 40000, the whole 800 m square centred on
		 *  the world origin. The ONE source of truth is that script; this is its mirror in C++ and the
		 *  two have to be changed together. */
		const FSquare Valley = { FVector2D(0.f, 0.f), 80000.f, ELevel::Valley };
		/** L_Match, the 4v4 college room: the college bounds + 15 m, campus origin (15710, 8222.6).
		 *  Also the square the T_Icon_campusmap picture covers. */
		const FSquare College = { FVector2D(-12210.f + 15710.f + 11355.f, -19549.45f + 8222.6f + 11355.f), 22710.f, ELevel::College };
		/** L_CampusMap (Campus Grounds), from campusmap_facts.md §2 - the level is built ON THE WORLD
		 *  ORIGIN (no campus shift, unlike L_Match) with the usual rule Unreal = (x, z, y) * 100 off
		 *  src/world/campusmap/layout.js, where PX = 0.15 m and the image pixel CENTER = [700, 560]
		 *  IS the origin.
		 *
		 *  166.65 m and NOT the 207.75 m of everything built: this is the biggest square that fits the
		 *  ground slab, so every metre of the play area is standable and the minimap PNG covers exactly
		 *  it (ToUV is 1:1 with the picture). The slab itself runs x -102.75 .. 105.00 m and
		 *  y -82.50 .. 84.15 m - a circle sized off that would close over ground the player cannot
		 *  reach on two sides. */
		const FSquare CampusGrounds = { FVector2D(112.5f, 82.5f), 16665.f, ELevel::CampusGrounds };

		/** Resolving costs a level-name string compare, and the HUD asks per circle per frame, so the
		 *  answer is remembered for as long as the level lasts. */
		TWeakObjectPtr<UWorld> CachedWorld;
		const FSquare* Cached = nullptr;
	}

	const FSquare& For(const UWorld* World)
	{
		if (!World)
		{
			return Valley; // the default battle-royale level (CampusBR::LevelForMap)
		}
		if (Cached && CachedWorld.Get() == World)
		{
			return *Cached;
		}
		// The LEVEL, not the game state's MatchMap: the same level name is what the host and every
		// client have in hand before any replication arrives, so both resolve the same square. Named
		// levels first and the valley as the fallback, so an unknown level still opens a circle over
		// the map it was built on rather than over nothing.
		const FString Level = World->GetMapName();
		CachedWorld = const_cast<UWorld*>(World);
		if (Level.Contains(TEXT("L_Match")))
		{
			Cached = &College;
		}
		else if (Level.Contains(TEXT("L_CampusMap")))
		{
			Cached = &CampusGrounds;
		}
		else
		{
			Cached = &Valley;
		}
		return *Cached;
	}
}

float CampusBRServerTime(const UWorld* World)
{
	const AGameStateBase* State = World ? World->GetGameState() : nullptr;
	return State ? float(State->GetServerWorldTimeSeconds()) : float(World ? World->GetTimeSeconds() : 0.f);
}

ACampusBRZone::ACampusBRZone()
{
	PrimaryActorTick.bCanEverTick = true;
	// EVERY FRAME, and not the 0.25 s it used to be: the wall has to slide in smoothly while the
	// circle closes, and a frame of this actor is two transform calls plus a once-a-second burn.
	PrimaryActorTick.TickInterval = 0.f;
	bReplicates = true;
	bAlwaysRelevant = true; // everyone needs the circle, wherever they are standing
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);
	SetCanBeDamaged(false);

	// FObjectFinder in the constructor, not LoadObject later: that is what puts the mesh and the
	// material in the class default object, and the CDO is the only thing the cooker can see. The
	// unlit translucent master has no instances of its own in /Game, so without this reference it
	// would not ship in the Android build at all (the same note ACampusBRPlane's art carries).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PanelMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PanelPaint(
		TEXT("/Game/Campus/Materials/M_Campus_Unlit_Translucent.M_Campus_Unlit_Translucent"));

	// A plain root with the ring attached under it, and NOT the ring as the root: the wall is moved
	// and scaled every frame on every machine from replicated numbers both sides already hold, so it
	// must be a COMPONENT-relative transform. As the actor's root it would have been an actor move,
	// which replicates - the host would then fight each client's own interpolation and pay for it.
	SetReplicatingMovement(false);
	USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	SetRootComponent(Pivot);

	Wall = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ZoneWall"));
	Wall->SetupAttachment(Pivot);
	// NO MESH WITHOUT THE PAINT. If /Game/Campus/Materials/M_Campus_Unlit_Translucent ever fails to
	// resolve, GetMaterial(0) hands back the engine's grey opaque default - and a 760 m-wide solid
	// grey wall across the map is far worse than no wall at all. Leaving the mesh null makes BeginPlay
	// build nothing and the whole thing a no-op.
	Wall->SetStaticMesh(PanelPaint.Object ? PanelMesh.Object : nullptr);
	if (PanelPaint.Object)
	{
		Wall->SetMaterial(0, PanelPaint.Object);
	}
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Wall->SetCollisionProfileName(TEXT("NoCollision"));
	Wall->SetCastShadow(false);
	Wall->bReceivesDecals = false;
	Wall->SetTranslucentSortPriority(5); // game.js renderOrder = 5
	// It is 200 m across and the whole point is to be seen from the far side of the circle.
	Wall->SetCullDistance(0.f);
	Wall->SetBoundsScale(8.f);
	Wall->SetHiddenInGame(true);
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

	// THE RING, BUILT ONCE. Each panel is the engine cube (100 cm on every axis) squashed to a thin
	// tall slab, stood at the build radius and turned to face out of the circle, so its local +X is
	// the radial direction. UpdateWall then only ever moves and scales the component as a whole.
	if (Wall && Wall->GetStaticMesh())
	{
		const float Chord = 2.f * CampusBRWall::BuildRadius * FMath::Sin(PI / float(CampusBRWall::Panels)) * 1.06f;
		Wall->ClearInstances();
		for (int32 Index = 0; Index < CampusBRWall::Panels; ++Index)
		{
			const float Angle = 2.f * PI * float(Index) / float(CampusBRWall::Panels);
			const FVector Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
			const FTransform Panel(
				FRotationMatrix::MakeFromX(Radial).Rotator(),
				Radial * CampusBRWall::BuildRadius + FVector(0.f, 0.f, CampusBRWall::Height * 0.5f - CampusBRWall::Sink),
				FVector(CampusBRWall::Thickness * 0.01f, Chord * 0.01f, CampusBRWall::Height * 0.01f));
			Wall->AddInstance(Panel, false);
		}
		if (UMaterialInterface* Parent = Wall->GetMaterial(0))
		{
			WallPaint = UMaterialInstanceDynamic::Create(Parent, this);
			if (WallPaint)
			{
				WallPaint->SetVectorParameterValue(TEXT("Color"), CampusBRWall::Colour);
				WallPaint->SetScalarParameterValue(TEXT("Opacity"), CampusBRWall::OpacityIdle);
				Wall->SetMaterial(0, WallPaint);
			}
		}
	}
	UpdateWall();
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
	const FVector2D Middle = CampusBRMap::Center(GetWorld());
	const float First = CampusBRMap::HalfSpan(GetWorld()) * CampusBRZoneTuning::FirstRadiusShare;
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
	// PhaseIndex back to "no circle", because that is the one replicated flag every reader already
	// treats as "there is nothing to be outside of": the wall hides itself on each machine's next
	// tick, MetresOutside answers 0, and the HUD drops the warning. Setting bRunning alone would not
	// do it - bRunning is the host's own bookkeeping and is false on a client by construction.
	PhaseIndex = -1;
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
	// NOTHING BURNS BEFORE THE FIRST CIRCLE HAS MOVED. This phase's rate belongs to this phase's
	// SHRINK, not to the wait in front of it - so the wait of phase 0 (the 75 s right after the drop,
	// while the circle still covers the whole map) does no damage at all, and the wait of every later
	// phase keeps the rate the shrink before it had. That is the three.js rule exactly (zone.js: dps 0
	// while 'open', and dps[k-1] while waiting on step k), and without it a player who landed on the
	// far side of the map was already burning before the zone had asked anything of them.
	DamagePerSecond = Index > 0 ? PhaseAt(Index - 1).DamagePerSecond : 0.f;
	// Announce the next circle at the START of the wait, so players can run for it before it closes -
	// that choice is the whole game, and hiding it until the shrink begins makes the zone feel unfair.
	PickNextCircle(CampusBRMap::HalfSpan(GetWorld()) * CampusBRZoneTuning::FirstRadiusShare * Phase.RadiusShare);
	bShrinking = false;
	EventStartTime = float(GetWorld()->GetTimeSeconds());
	EventEndTime = EventStartTime + Phase.WaitSeconds;

	GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [this, Index]()
	{
		const FCampusBRZonePhase& Shrinking = PhaseAt(Index);
		bShrinking = true;
		DamagePerSecond = Shrinking.DamagePerSecond;
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
	// Never below the floor, and never wider than the circle we are standing in: a map small enough to
	// hit MinRadius early simply holds its last circle instead of shrinking past it.
	const float Wanted = FMath::Min(FMath::Max(NewRadius, CampusBRZoneTuning::MinRadius), RadiusFrom);
	// The next circle must sit wholly inside this one, else players inside the safe area would be
	// dropped outside by a shrink they could not have run from.
	const float Slack = FMath::Max(RadiusFrom - Wanted, 0.f);
	const float Angle = FMath::FRandRange(0.f, 2.f * PI);
	const float Distance = Slack * FMath::Sqrt(FMath::FRand()); // uniform over the disc, not bunched in the middle
	CenterTo = FVector_NetQuantize(
		CenterFrom.X + FMath::Cos(Angle) * Distance,
		CenterFrom.Y + FMath::Sin(Angle) * Distance,
		0.f);
	RadiusTo = Wanted;
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

void ACampusBRZone::UpdateWall()
{
	if (!Wall || Wall->GetInstanceCount() == 0)
	{
		return; // no ring was built (no mesh, or no translucent material in this build)
	}
	// PhaseIndex < 0 is "the schedule has not opened": there is no circle to stand a wall on, which is
	// the whole of the waiting area and the plane ride.
	const float Radius = PhaseIndex >= 0 ? GetRadius() : 0.f;
	if (Radius <= 1.f)
	{
		Wall->SetHiddenInGame(true);
		return;
	}
	const FVector2D Middle = GetCenter();
	const float Scale = Radius / CampusBRWall::BuildRadius;
	Wall->SetHiddenInGame(false);
	Wall->SetWorldLocation(FVector(Middle.X, Middle.Y, 0.f));
	Wall->SetRelativeScale3D(FVector(Scale, Scale, 1.f));
	if (WallPaint)
	{
		WallPaint->SetScalarParameterValue(TEXT("Opacity"),
			bShrinking ? CampusBRWall::OpacityClosing : CampusBRWall::OpacityIdle);
	}
}

void ACampusBRZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// THE WALL IS DRAWN ON EVERY MACHINE, so this comes before the authority test: a client reads the
	// same replicated circle the host burns against, so the blue edge it sees IS the damage line.
	UpdateWall();
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
			// THROUGH THE ARMOUR (bIgnoreArmor = true). A vest stops bullets, not the gas: letting it
			// soak the burn made the circle roughly half as dangerous for anyone carrying one, and
			// quietly ate the armour they would need for the fight at the end of the rotation. The
			// zone is the only caller in the project that passes this.
			Character->ApplyHit(DamagePerSecond * Scale * CampusBRZoneTuning::BurnInterval, nullptr, true);
		}
	}
}
