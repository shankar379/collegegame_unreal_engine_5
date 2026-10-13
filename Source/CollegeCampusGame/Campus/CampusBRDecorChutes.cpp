#include "Campus/CampusBRDecorChutes.h"

#include "Campus/CampusBRPlane.h"
#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/ObjectKey.h"

/** The descent's numbers. The fall speeds and the canopy band are DELIBERATE COPIES of the divers' own
 *  (CampusBRDive::FreefallFallSpeed / ChuteFallSpeed / AutoChuteHeight in CampusBRPlane.cpp): those live
 *  in that translation unit and are not published, and scenery has no business being the reason they
 *  become public. If the real drop is ever retuned, match these by eye - nothing breaks if they drift,
 *  the sky just stops agreeing with the players in it. */
namespace CampusBRDecorFall
{
	/** Five. Enough that the sky reads as busy from 300 m up, cheap enough that a phone never notices:
	 *  seven flat unlit primitives each, no shadows, no collision, no net. */
	constexpr int32 Figures = 5;
	/** The slice of the crossing jumps are drawn from. Inside the run-in and well short of the run-out,
	 *  so every figure leaves over the map square and lands inside the first circle - and so the last
	 *  canopy is open long before the aircraft deletes itself. */
	constexpr float FirstAlpha = 0.18f;
	constexpr float LastAlpha = 0.72f;

	/** 40 m/s down, then 4.5 m/s under the canopy. */
	constexpr float FreefallSpeed = 4000.f;
	constexpr float CanopySpeed = 450.f;
	/** The canopy pops 60-80 m up: high enough to be seen opening, low enough that the freefall is the
	 *  part of the drop you actually watch. */
	constexpr float PopLow = 6000.f;
	constexpr float PopHigh = 8000.f;

	/** How far down to look for the ground, and how far the figure's feet stay off it. */
	constexpr float GroundReach = 60000.f;
	constexpr float FootClearance = 4.f;

	/** How far from the door a figure glides, in cm. Small against the map's 227 m square: these are
	 *  background, and a jumper crossing half the campus would read as a player worth shooting at. */
	constexpr float GlideNear = 1500.f;
	constexpr float GlideFar = 4500.f;

	/** The sway: 1.4 m across the line of travel, with 6 degrees of roll to sell it. */
	constexpr float SwayAmplitude = 140.f;
	constexpr float SwayTilt = 6.f;
	/** Head-down lean during freefall. Negative pitch is nose-forward. */
	constexpr float DiveTilt = -28.f;

	/** How long a landed figure stays, and how much of that it spends shrinking away. Nobody may walk up
	 *  to one: it is a cylinder with a sphere on top and no collision, and the illusion only survives
	 *  being seen from the air. */
	constexpr float LingerSeconds = 2.f;
	constexpr float FadeSeconds = 0.6f;

	/** Planes already given a set, so the one hook can sit in a Tick. Identity only (never a reference)
	 *  and pruned as soon as an entry's plane is gone: there is one plane per world, two with play-in-
	 *  editor, and a finished match must leave nothing behind. */
	TArray<FObjectKey> Decorated;
}

/** The art: engine primitives and the four campus material instances ACampusBRPlane and ACampusBRChute
 *  ALREADY hold on their class default objects, so this file adds nothing to the cook. Loaded with
 *  FObjectFinder from the constructor, which is what puts them in this class's own default object - and
 *  that is what the cooker can see. Its own namespace because CampusBRArt's helpers are file-local to
 *  CampusBRPlane.cpp and these translation units share a unity build. */
namespace CampusBRDecorArt
{
	UStaticMesh* Cylinder()
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Found(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		return Found.Object;
	}
	UStaticMesh* Sphere()
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Found(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		return Found.Object;
	}

	/** Jumpsuit slate - the plane's own trim colour, which is the one that reads against a bright sky. */
	UMaterialInterface* Suit()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_3d4650_100.MI_Campus_3d4650_100"));
		return Found.Object;
	}
	/** Helmet white. */
	UMaterialInterface* Helmet()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_d9d9d9_100.MI_Campus_d9d9d9_100"));
		return Found.Object;
	}
	/** The same canopy amber the real chute uses: a decorative jumper that was a different colour from
	 *  a real one would be read as information. */
	UMaterialInterface* Cloth()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_d4a017_100.MI_Campus_d4a017_100"));
		return Found.Object;
	}
	/** Rigging: near-black. */
	UMaterialInterface* Rigging()
	{
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Found(
			TEXT("/Game/Campus/Materials/Instances/MI_Campus_1b1c1f_100.MI_Campus_1b1c1f_100"));
		return Found.Object;
	}

	/** The figure, in centimetres, feet on the actor's own origin - so the landing point IS the actor
	 *  location and the touchdown test is one float compare. */
	constexpr float BodyHeight = 90.f;
	constexpr float HeadSize = 30.f;
	constexpr float ShoulderHeight = 85.f;
	/** The canopy rides 4.2 m over the shoulders, with the rigging meeting the dome 2.5 m out: the same
	 *  three numbers ACampusBRChute hangs its dome with. */
	constexpr float CanopyRise = 420.f;
	constexpr float RiggingRadius = 250.f;
	constexpr float ShoulderRadius = 18.f;

	/** One cosmetic primitive: no collision, no shadow, no decals. */
	void Dress(UStaticMeshComponent* Part, UStaticMesh* Mesh, UMaterialInterface* Material,
		const FVector& Where, const FRotator& Turn, const FVector& Size)
	{
		if (!Part)
		{
			return;
		}
		if (Mesh)
		{
			Part->SetStaticMesh(Mesh);
		}
		if (Material)
		{
			Part->SetMaterial(0, Material);
		}
		Part->SetRelativeLocation(Where);
		Part->SetRelativeRotation(Turn);
		Part->SetRelativeScale3D(Size);
		Part->SetCollisionProfileName(TEXT("NoCollision"));
		Part->SetCastShadow(false);
		Part->bReceivesDecals = false;
	}
}

// ---------------------------------------------------------------------------------------------
// One cosmetic jumper

ACampusBRDecorChute::ACampusBRDecorChute()
{
	PrimaryActorTick.bCanEverTick = true;
	// NOT replicated and never will be: every machine runs this same jump from the same seed. See the
	// header - that is the whole reason this class exists instead of five more ACampusBRChutes.
	SetCanBeDamaged(false);
	// Hidden until the plane reaches this figure's release point. Done on the ACTOR and not by hiding
	// the root component, because a propagating SetVisibility in a constructor does nothing useful (the
	// components have not registered yet, so AttachChildren is empty - the note on ACampusBRPlane::
	// BeginPlay was written over exactly that bug).
	SetActorHiddenInGame(true);

	// Unscaled root at the figure's feet: every part hangs off this, and the landing point is the
	// actor's own location.
	Harness = CreateDefaultSubobject<USceneComponent>(TEXT("Harness"));
	SetRootComponent(Harness);

	// A 90 cm cylinder and a 30 cm sphere. From 300 m up on a phone that is a person; legs and arms
	// would be four more draw calls of sub-pixel detail, five times over.
	Torso = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Torso"));
	Torso->SetupAttachment(Harness);
	CampusBRDecorArt::Dress(Torso, CampusBRDecorArt::Cylinder(), CampusBRDecorArt::Suit(),
		FVector(0.f, 0.f, CampusBRDecorArt::BodyHeight * 0.5f), FRotator::ZeroRotator,
		FVector(0.42f, 0.42f, CampusBRDecorArt::BodyHeight * 0.01f));

	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(Harness);
	CampusBRDecorArt::Dress(Head, CampusBRDecorArt::Sphere(), CampusBRDecorArt::Helmet(),
		FVector(0.f, 0.f, CampusBRDecorArt::BodyHeight + CampusBRDecorArt::HeadSize * 0.5f),
		FRotator::ZeroRotator, FVector(CampusBRDecorArt::HeadSize * 0.01f));

	// The canopy, exactly as ACampusBRChute builds it: a sphere squashed on Z - 7 m across, 1.2 m deep -
	// which is a dome from underneath and the lens every parachute silhouette already is from the side.
	Canopy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Canopy"));
	Canopy->SetupAttachment(Harness);
	CampusBRDecorArt::Dress(Canopy, CampusBRDecorArt::Sphere(), CampusBRDecorArt::Cloth(),
		FVector(0.f, 0.f, CampusBRDecorArt::ShoulderHeight + CampusBRDecorArt::CanopyRise),
		FRotator::ZeroRotator, FVector(7.f, 7.f, 2.4f));

	// One line per diagonal, each built from the two points it joins - the dome's rim and the shoulder -
	// so the geometry is stated once and the dome can move without the rigging drifting.
	auto MakeLine = [this](const TCHAR* Name, int32 Corner) -> UStaticMeshComponent*
	{
		const float Angle = FMath::DegreesToRadians(45.f + 90.f * Corner);
		const FVector2D Around(FMath::Cos(Angle), FMath::Sin(Angle));
		const FVector Top(Around.X * CampusBRDecorArt::RiggingRadius, Around.Y * CampusBRDecorArt::RiggingRadius,
			CampusBRDecorArt::ShoulderHeight + CampusBRDecorArt::CanopyRise - 60.f);
		const FVector Bottom(Around.X * CampusBRDecorArt::ShoulderRadius, Around.Y * CampusBRDecorArt::ShoulderRadius,
			CampusBRDecorArt::ShoulderHeight);
		const FVector Along = Top - Bottom;

		UStaticMeshComponent* Line = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Line->SetupAttachment(Harness);
		CampusBRDecorArt::Dress(Line, CampusBRDecorArt::Cylinder(), CampusBRDecorArt::Rigging(),
			(Top + Bottom) * 0.5f, FRotationMatrix::MakeFromZ(Along).Rotator(),
			FVector(0.03f, 0.03f, Along.Size() * 0.01f));
		return Line;
	};
	LineFrontLeft = MakeLine(TEXT("LineFrontLeft"), 0);
	LineBackLeft = MakeLine(TEXT("LineBackLeft"), 1);
	LineBackRight = MakeLine(TEXT("LineBackRight"), 2);
	LineFrontRight = MakeLine(TEXT("LineFrontRight"), 3);

	// Packed, until it pops. Set on the default object, so an instance is never born with it open.
	ShowCanopy(false);
}

void ACampusBRDecorChute::ShowCanopy(bool bShown)
{
	UStaticMeshComponent* const Parts[5] = { Canopy, LineFrontLeft, LineBackLeft, LineBackRight, LineFrontRight };
	for (UStaticMeshComponent* Part : Parts)
	{
		if (Part)
		{
			Part->SetVisibility(bShown);
		}
	}
}

void ACampusBRDecorChute::Ride(const FCampusBRDecorPlan& InPlan)
{
	Plan = InPlan;
	Phase = ECampusBRDecorPhase::Waiting;
}

void ACampusBRDecorChute::Release(const ACampusBRPlane* InPlane)
{
	UWorld* World = GetWorld();
	// The real divers' door - below and behind the belly (ACampusBRPlane::SeatLocation) - nudged in the
	// plane's own axes, so five jumps out of one aircraft are not five jumps out of one point.
	ExitPoint = InPlane->SeatLocation() + InPlane->GetActorQuat().RotateVector(Plan.ExitOffset);
	const FVector Aim(ExitPoint.X + Plan.GlideOffset.X, ExitPoint.Y + Plan.GlideOffset.Y, ExitPoint.Z);

	// ONE trace for the whole descent, taken at the point the figure is heading for rather than the one
	// it left. Against the WORLD only - ECC_WorldStatic and ECC_WorldDynamic, the pair ACampusBRDrop::
	// HeightAboveGround and ACampusBRGameMode::GroundedSpawn both use - so a pawn's capsule under the
	// flight path can never be read as ground and leave a jumper standing in the air above somebody.
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("CampusBRDecorGround"), false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const bool bFound = World && World->LineTraceSingleByObjectType(Hit, Aim,
		Aim - FVector(0.f, 0.f, CampusBRDecorFall::GroundReach), Objects, Params);
	// Nothing under that point at all (a hole in the terrain, or streaming has not caught up): sea
	// level, which is where this campus's ground is anyway.
	TouchPoint = FVector(Aim.X, Aim.Y, (bFound ? Hit.Location.Z : 0.0) + CampusBRDecorFall::FootClearance);

	Bearing = float(FMath::RadiansToDegrees(FMath::Atan2(Plan.GlideOffset.Y, Plan.GlideOffset.X)));
	// Nothing below this point ever looks at the aircraft again: a figure in the air is independent of
	// it, which matters because the plane deletes itself well before the last canopy is down.
	Phase = ECampusBRDecorPhase::Freefall;
	SetActorLocationAndRotation(ExitPoint, FRotator(CampusBRDecorFall::DiveTilt, Bearing, 0.f));
	SetActorHiddenInGame(false);
}

void ACampusBRDecorChute::PopCanopy()
{
	Phase = ECampusBRDecorPhase::Canopy;
	ShowCanopy(true);
}

void ACampusBRDecorChute::Land()
{
	Phase = ECampusBRDecorPhase::Landed;
	LandTime = float(GetWorld()->GetTimeSeconds());
	// The canopy collapses on touchdown: what lingers for two seconds is the figure, and a 7 m dome
	// lying over the ground would be the one part of this nobody would believe.
	ShowCanopy(false);
}

void ACampusBRDecorChute::Descend(float DeltaSeconds)
{
	AirTime += DeltaSeconds;
	const float Speed = Phase == ECampusBRDecorPhase::Canopy
		? CampusBRDecorFall::CanopySpeed : CampusBRDecorFall::FreefallSpeed;
	// Heights are kept as float on purpose, cast at the one place the world's double-precision location
	// is read: the whole descent is 300 m of cosmetics, and FMath::Max / Clamp will not deduce a mixed
	// double-and-float call at all.
	const float Height = float(GetActorLocation().Z) - Speed * DeltaSeconds;

	if (Phase == ECampusBRDecorPhase::Freefall && Height - TouchPoint.Z <= Plan.PopHeight)
	{
		PopCanopy();
	}
	if (Height <= TouchPoint.Z)
	{
		SetActorLocationAndRotation(TouchPoint, FRotator(0.f, Bearing, 0.f));
		Land();
		return;
	}

	// The glide is driven by HEIGHT, not by elapsed time: whatever the sway does and whatever the frame
	// rate is, the figure arrives exactly on the point the one ground trace found.
	const float Total = FMath::Max(float(ExitPoint.Z - TouchPoint.Z), 1.f);
	const float Progress = FMath::Clamp(float(ExitPoint.Z) - Height, 0.f, Total) / Total;
	FVector Where = FMath::Lerp(ExitPoint, TouchPoint, Progress);
	Where.Z = Height;

	float Roll = 0.f;
	if (Phase == ECampusBRDecorPhase::Canopy)
	{
		// A sine across the line of travel, faded out by the approach so the swing cannot carry the
		// figure off its landing point in the last metre.
		const float Swing = FMath::Sin(AirTime * Plan.SwaySpeed * 2.f * PI + Plan.SwayPhase) * (1.f - Progress);
		Where += FRotator(0.f, Bearing + 90.f, 0.f).Vector() * Swing * CampusBRDecorFall::SwayAmplitude;
		Roll = Swing * CampusBRDecorFall::SwayTilt;
	}
	SetActorLocationAndRotation(Where,
		FRotator(Phase == ECampusBRDecorPhase::Canopy ? 0.f : CampusBRDecorFall::DiveTilt, Bearing, Roll));
}

void ACampusBRDecorChute::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	switch (Phase)
	{
	case ECampusBRDecorPhase::Waiting:
	{
		// Asked for each frame rather than held: there is one plane per world (ACampusBRPlane::Get), and
		// scenery must not own a reference to it for one frame longer than the aircraft wants to live.
		const ACampusBRPlane* InPlane = ACampusBRPlane::Get(GetWorld());
		if (!InPlane)
		{
			Destroy(); // the flight ended early (or never started): there is nothing to jump out of
			return;
		}
		const float Alpha = InPlane->FlightAlpha();
		if (Alpha >= Plan.ReleaseAlpha && InPlane->IsOverMap())
		{
			Release(InPlane);
		}
		else if (Alpha >= 1.f)
		{
			// The crossing finished and the plane was never over the square while we were due out - a
			// line that only clips a corner can do that. Better no jumper than one landing outside the
			// first circle, where a real diver would never be.
			Destroy();
		}
		return;
	}
	case ECampusBRDecorPhase::Freefall:
	case ECampusBRDecorPhase::Canopy:
		Descend(DeltaSeconds);
		return;
	case ECampusBRDecorPhase::Landed:
	{
		const float Since = float(GetWorld()->GetTimeSeconds()) - LandTime;
		if (Since >= CampusBRDecorFall::LingerSeconds)
		{
			Destroy();
			return;
		}
		// Shrink away over the last stretch rather than fade: a fade needs a dynamic material instance
		// per figure, which is five more material slots on a phone to hide something nobody is looking
		// at any more.
		const float Left = CampusBRDecorFall::LingerSeconds - Since;
		if (Left < CampusBRDecorFall::FadeSeconds)
		{
			SetActorScale3D(FVector(FMath::Max(Left / CampusBRDecorFall::FadeSeconds, 0.01f)));
		}
		return;
	}
	}
}

// ---------------------------------------------------------------------------------------------
// The spawner: one flight's worth of sky, drawn on every machine from one number

void CampusBRDecor::SpawnForFlight(UWorld* World, const ACampusBRPlane* Plane, int32 Seed)
{
	if (!World || !Plane || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Once per flight. The guard lives HERE rather than in the caller because the only place that runs
	// on every machine the moment the flight is real is the plane's Tick, and a hook has no business
	// carrying a flag for scenery.
	const FObjectKey Flight(Plane);
	for (int32 Index = CampusBRDecorFall::Decorated.Num() - 1; Index >= 0; --Index)
	{
		if (!CampusBRDecorFall::Decorated[Index].ResolveObjectPtr())
		{
			CampusBRDecorFall::Decorated.RemoveAtSwap(Index);
		}
		else if (CampusBRDecorFall::Decorated[Index] == Flight)
		{
			return;
		}
	}
	CampusBRDecorFall::Decorated.Add(Flight);

	// The seed is the whole contract with the other machines. Mixed before use because it arrives as a
	// launch clock scaled to an integer - a tidy, low, slowly-changing number, which is the one thing a
	// linear congruential stream handles badly.
	FRandomStream Stream(int32(HashCombine(uint32(Seed), 0x9E3779B9u) & 0x7FFFFFFF));

	const float Slice = (CampusBRDecorFall::LastAlpha - CampusBRDecorFall::FirstAlpha) / float(CampusBRDecorFall::Figures);
	for (int32 Index = 0; Index < CampusBRDecorFall::Figures; ++Index)
	{
		FCampusBRDecorPlan Plan;
		// One jumper per equal slice of the window, placed anywhere inside its own slice: drawing five
		// free times out of the whole window clumps them, and a fixed stride is a metronome.
		Plan.ReleaseAlpha = CampusBRDecorFall::FirstAlpha + (float(Index) + Stream.FRandRange(0.15f, 0.85f)) * Slice;
		// Behind the door and out to either side, in the plane's own axes.
		Plan.ExitOffset = FVector(Stream.FRandRange(-700.f, -100.f), Stream.FRandRange(-900.f, 900.f),
			Stream.FRandRange(-250.f, 0.f));
		const float Bearing = Stream.FRandRange(0.f, 2.f * PI);
		const float Reach = Stream.FRandRange(CampusBRDecorFall::GlideNear, CampusBRDecorFall::GlideFar);
		Plan.GlideOffset = FVector2D(FMath::Cos(Bearing), FMath::Sin(Bearing)) * Reach;
		Plan.PopHeight = Stream.FRandRange(CampusBRDecorFall::PopLow, CampusBRDecorFall::PopHigh);
		Plan.SwaySpeed = Stream.FRandRange(0.2f, 0.4f);
		Plan.SwayPhase = Stream.FRandRange(0.f, 2.f * PI);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient; // scenery is never saved and never travels
		ACampusBRDecorChute* Figure = World->SpawnActor<ACampusBRDecorChute>(
			ACampusBRDecorChute::StaticClass(), FTransform(Plane->GetActorLocation()), Params);
		if (Figure)
		{
			Figure->Ride(Plan);
		}
	}
}
