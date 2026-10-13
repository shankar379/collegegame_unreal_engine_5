#include "Campus/CampusVehicle.h"

#include "Campus/CampusCharacter.h"
#include "Campus/CampusMatchState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace CampusVehicleTuning
{
	// ---- damage model: Docs/VEHICLES_DESIGN.md Section 7, anchored to the AK (25 damage, 30-round mag)
	constexpr float MaxHealth = 600.f;          // 32 body shots = one magazine plus two
	constexpr uint8 WheelMaxHealth = 100;       // 4 AK body shots per wheel, no zones, no falloff
	constexpr float ExplosionDamage = 85.f;     // lethal inside ~1 m, survivable at 3 m on full health
	constexpr float ExplosionRadius = 450.f;
	constexpr float OccupantExplosionDamage = 999.f;
	constexpr float BurnSeconds = 3.f;          // the bail-out window
	constexpr float InstantExplodeHit = 120.f;  // one hit this big skips the burn

	// ---- roadkill (Section 7.5)
	constexpr float MinRoadkillSpeed = 400.f;   // 14 km/h: below this a shove, zero damage
	constexpr float FullRoadkillSpeed = 1200.f; // top speed, 43 km/h
	constexpr float RoadkillDamage = 180.f;
	constexpr float RoadkillCooldownSeconds = 0.6f;
	constexpr float RoadkillLaunchUp = 450.f;   // feeds the existing SafeFall / DeadlyFall path
	constexpr float RoadkillLaunchShare = 0.60f;

	// ---- wall impacts (Section 7.6)
	constexpr float ImpactFreeSpeed = 600.f;
	constexpr float ImpactDamagePerCm = 0.08f;  // top speed into a wall = 48 damage
	constexpr float ImpactSpeedKept = 0.45f;
	constexpr float ImpactOccupantShare = 0.25f;
	constexpr float HardLandingSpeed = 900.f;
	constexpr float HardLandingSpeedKept = 0.7f;

	// ---- exit (Section 8.4)
	constexpr float SafeExitSpeed = 900.f;
	constexpr float ExitDamagePerCm = 0.05f;    // 1200 cm/s = 15 damage

	// ---- drive feel. The doc hands us this one ("we author the drive feel ourselves"); these are the
	// tuned values, chosen so the car is 1.8x a sprinting player and reaches top speed in ~1.5 s.
	constexpr float MaxSpeed = 1200.f;          // owner's call: 43 km/h
	constexpr float MaxReverseSpeed = 350.f;
	constexpr float Accel = 800.f;              // cm/s^2, 0 -> 1200 in 1.5 s
	constexpr float BrakeDecel = 1800.f;
	constexpr float CoastDecel = 300.f;         // no throttle: engine braking + rolling drag
	constexpr float EngineDeadDrag = 2.f;       // Section 7.3: drag doubled once the engine is dead
	constexpr float EngineDeadSteer = 0.3f;
	constexpr float HandbrakeDecel = 2200.f;
	/** Yaw rate at walking pace; it falls away with speed so the car does not corner like a go-kart. */
	constexpr float SteerRate = 110.f;          // deg/s
	constexpr float SteerFalloff = 0.55f;       // at top speed only 45 % of SteerRate is left (~14 m radius)
	constexpr float SteerResponse = 6.f;        // how fast YawRate reaches its target (1/s)
	constexpr float SteerStartSpeed = 400.f;    // below this the steering fades in (no turning on the spot)
	constexpr float SlipShare = 0.18f;          // part of the cornering demand the tyres do not hold
	constexpr float GripDecay = 7.f;            // 1/s, how fast a slide washes out
	constexpr float HandbrakeGrip = 0.35f;
	constexpr float Gravity = 1600.f;
	constexpr float WheelRadius = 35.f;
	constexpr float WheelSteerVisualDeg = 30.f;
	constexpr float GroundProbe = 120.f;        // Section 9: 4 x 120 cm line traces per car
	constexpr float ConformResponse = 8.f;      // how fast the body eases onto a new slope (1/s)
	/** The same invisible floor the player has (ACampusCharacter::KillHeight): below it the car stops dead. */
	constexpr float KillHeight = -1500.f;

	// ---- wheel penalties (Section 7.2)
	constexpr float PoppedSpeedShare = 0.78f;
	constexpr float PoppedSteerShare = 0.70f;
	constexpr float PoppedGripShare = 0.75f;
	constexpr float SpeedFloorShare = 0.40f;

	// ---- hull. Section 7.4's geometry is quoted from the GROUND plane up (x forward, y right, z up),
	// but the Hull box has to be the root component (that is what SetActorLocation sweeps), so the actor
	// origin sits at the hull's centre, HullCentreZ above the ground. Local() does that one conversion,
	// in one place, so no "- 75" is ever written by hand.
	constexpr float HalfLength = 230.f;
	constexpr float HalfWidth = 95.f;
	constexpr float HullHeight = 150.f;
	constexpr float HullCentreZ = HullHeight * 0.5f;
	constexpr float WindowLine = 95.f;          // Section 7.4: metal below, glass above
	constexpr float RoofTop = 160.f;
	constexpr float Local(float GroundZ) { return GroundZ - HullCentreZ; }
	/** Seat socket = where a seated capsule's FEET sit; SeatTransform adds the capsule half-height.
	 *  68 + 55 (crouched half-height) puts the capsule centre at 123, so 75 % of the occupant is above
	 *  the window line - the exposure Section 7.4 was computed for. */
	constexpr float SeatFloor = 68.f;
	constexpr float SeatFrontX = -20.f;
	constexpr float SeatRearX = -130.f;
	constexpr float SeatSideY = 45.f;
	constexpr float WheelX = 150.f;
	constexpr float WheelY = 85.f;

	// ---- netcode (Section 8)
	constexpr float NetUpdateHz = 20.f;
	constexpr double HistorySeconds = 0.5;      // same KeepSeconds as the character hitbox history
	constexpr double MaxDeadReckon = 0.25;
	constexpr float SoftCorrection = 150.f;     // never snap under this (Section 8.2)
	constexpr float HardCorrection = 600.f;
	constexpr float CorrectionTauNear = 0.08f;
	constexpr float CorrectionTauFar = 0.04f;

	/** Degrees -> the 16-bit field the snapshot carries, and back. */
	inline uint16 PackYaw(float Degrees) { return uint16(FMath::RoundToInt(FRotator::ClampAxis(Degrees) * 65536.f / 360.f)) ; }
	inline float UnpackYaw(uint16 Packed) { return float(Packed) * 360.f / 65536.f; }
}

namespace
{
	TArray<TWeakObjectPtr<ACampusVehicle>> GCampusVehicles;

	TAutoConsoleVariable<int32> CVarVehiclesEnabled(
		TEXT("campus.VehiclesEnabled"),
		1,
		TEXT("Drivable cars (ACampusVehicle). 0 disables entering, driving and the car's bullet path."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarVehicleDebug(
		TEXT("campus.VehicleDebug"),
		0,
		TEXT("Draw the live hull, the seats, the roadkill sphere, the history ribbon and the REWOUND bullet boxes."),
		ECVF_Cheat);

	/** The car's oriented damage boxes in its own local frame. An implementation detail of the bullet
	 *  test, not part of the actor's interface - which is also what makes it directly testable. */
	struct FPartBox
	{
		FVector Center = FVector::ZeroVector;
		FVector Extent = FVector::ZeroVector;
		ECampusVehiclePart Part = ECampusVehiclePart::None;
		int32 WheelIndex = INDEX_NONE;
		bool bBlocks = true;
	};

	const TArray<FPartBox>& PartBoxes()
	{
		using namespace CampusVehicleTuning;
		static const TArray<FPartBox> Boxes = []()
		{
			// MinZ / MaxZ are quoted from the ground, like Section 7.4; Local() moves them into the hull frame.
			auto Box = [](float MinX, float MaxX, float MinZ, float MaxZ, ECampusVehiclePart Part, bool bBlocks)
			{
				FPartBox Out;
				Out.Center = FVector((MinX + MaxX) * 0.5f, 0.f, Local((MinZ + MaxZ) * 0.5f));
				Out.Extent = FVector((MaxX - MinX) * 0.5f, HalfWidth, (MaxZ - MinZ) * 0.5f);
				Out.Part = Part;
				Out.bBlocks = bBlocks;
				return Out;
			};
			TArray<FPartBox> Built;
			// Section 7.4: metal below the window line blocks, the greenhouse does not, the roof does.
			Built.Add(Box(140.f, HalfLength, 0.f, WindowLine, ECampusVehiclePart::Engine, true));
			Built.Add(Box(-150.f, 140.f, 0.f, WindowLine, ECampusVehiclePart::Body, true));
			Built.Add(Box(-HalfLength, -150.f, 0.f, 110.f, ECampusVehiclePart::Body, true));
			Built.Add(Box(-150.f, 140.f, WindowLine, HullHeight, ECampusVehiclePart::Glass, false));
			Built.Add(Box(-150.f, 140.f, HullHeight, RoofTop, ECampusVehiclePart::Roof, true));
			// Four tyres, poking 20 cm past the body so a side shot reaches them before the door.
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const bool bFront = Index < 2;
				const bool bLeft = (Index % 2) == 0;
				FPartBox Wheel;
				Wheel.Center = FVector(bFront ? WheelX : -WheelX, bLeft ? -WheelY : WheelY, Local(WheelRadius));
				Wheel.Extent = FVector(40.f, 20.f, WheelRadius);
				Wheel.Part = ECampusVehiclePart::Wheel;
				Wheel.WheelIndex = Index;
				Built.Add(Wheel);
			}
			return Built;
		}();
		return Boxes;
	}

	/** Section 7.5's curve, as a pure function so it can be tested against the doc's own table. */
	float RoadkillDamageFor(float ClosingSpeed)
	{
		using namespace CampusVehicleTuning;
		const float Share = FMath::Clamp((ClosingSpeed - MinRoadkillSpeed) / (FullRoadkillSpeed - MinRoadkillSpeed), 0.f, 1.f);
		return RoadkillDamage * Share;
	}

	/** Segment vs an axis-aligned box in the box's own frame: the standard slab test. */
	bool SegmentHitsBox(const FVector& Start, const FVector& Dir, float MaxDistance, const FVector& Center,
		const FVector& Extent, float& OutDistance)
	{
		float Near = 0.f;
		float Far = MaxDistance;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float Origin = Start[Axis] - Center[Axis];
			const float Direction = Dir[Axis];
			if (FMath::Abs(Direction) < UE_SMALL_NUMBER)
			{
				if (FMath::Abs(Origin) > Extent[Axis])
				{
					return false;
				}
				continue;
			}
			const float Inv = 1.f / Direction;
			float T0 = (-Extent[Axis] - Origin) * Inv;
			float T1 = (Extent[Axis] - Origin) * Inv;
			if (T0 > T1)
			{
				Swap(T0, T1);
			}
			Near = FMath::Max(Near, T0);
			Far = FMath::Min(Far, T1);
			if (Near > Far)
			{
				return false;
			}
		}
		OutDistance = Near;
		return true;
	}
}

ACampusVehicle::ACampusVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	bReplicates = true;
	bAlwaysRelevant = false;
	SetReplicateMovement(false); // the snapshot IS the movement; FRepMovement would fight it
	SetNetUpdateFrequency(CampusVehicleTuning::NetUpdateHz);
	SetMinNetUpdateFrequency(2.f);
	NetPriority = 2.5f;                       // above a door (1.0), below a player (3.0)
	SetNetCullDistanceSquared(2.25e9f);       // 150 m: a car matters long before you can walk to it

	// One sweep volume on ECC_Vehicle. It blocks the world and IGNORES pawns on purpose: the car ploughs
	// through players (roadkill is a separate overlap, Section 7.5) and ServerFire's world trace, which
	// queries WorldStatic | WorldDynamic only, cannot see it - the car is tested exclusively rewound.
	Hull = CreateDefaultSubobject<UBoxComponent>(TEXT("Hull"));
	RootComponent = Hull;
	Hull->SetBoxExtent(FVector(CampusVehicleTuning::HalfLength, CampusVehicleTuning::HalfWidth, CampusVehicleTuning::HullCentreZ));
	Hull->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Hull->SetCollisionObjectType(ECC_Vehicle);
	Hull->SetCollisionResponseToAllChannels(ECR_Ignore);
	Hull->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Hull->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Hull->SetGenerateOverlapEvents(false);
	Hull->SetCanEverAffectNavigation(false);
	Hull->SetMobility(EComponentMobility::Movable);

	auto MakeArt = [this](const TCHAR* Name)
	{
		UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(Hull);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false); // the campus is unlit and shadowless (build_campus.py master(lit=False))
		return Mesh;
	};
	Body = MakeArt(TEXT("Body"));
	Glass = MakeArt(TEXT("Glass"));

	// The migrated art (Scripts/build_vehicles.py) is named here rather than loaded in BeginPlay, so a
	// car is visible in the editor viewport and the meshes cook as CDO references.
	auto Dress = [](UStaticMeshComponent* Mesh, const TCHAR* MeshPath, const TCHAR* MaterialPath)
	{
		ConstructorHelpers::FObjectFinderOptional<UStaticMesh> FoundMesh(MeshPath);
		ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> FoundMaterial(MaterialPath);
		UMaterialInterface* Material = FoundMaterial.Get();
		if (UStaticMesh* Found = FoundMesh.Get())
		{
			Mesh->SetStaticMesh(Found);
			for (int32 Slot = 0; Material && Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				Mesh->SetMaterial(Slot, Material);
			}
		}
		return Material;
	};
	BodyMaterial = Dress(Body, TEXT("/Game/CampusVehicles/SM_SportsCar.SM_SportsCar"),
		TEXT("/Game/CampusVehicles/MI_CampusCar_Body.MI_CampusCar_Body"));
	Dress(Glass, TEXT("/Game/CampusVehicles/SM_SportsCar_Glass.SM_SportsCar_Glass"),
		TEXT("/Game/CampusVehicles/MI_CampusCar_Glass.MI_CampusCar_Glass"));

	Wheels = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Wheels"));
	Wheels->SetupAttachment(Hull);
	Wheels->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Wheels->SetGenerateOverlapEvents(false);
	Wheels->SetCanEverAffectNavigation(false);
	Wheels->SetCastShadow(false);
	Wheels->SetCullDistances(0, 6000); // same philosophy as the campus meshes' INK_CULL
	Dress(Wheels, TEXT("/Game/CampusVehicles/SM_SportsCar_Wheel.SM_SportsCar_Wheel"),
		TEXT("/Game/CampusVehicles/MI_CampusCar_Wheel.MI_CampusCar_Wheel"));
	ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> FoundWreck(
		TEXT("/Game/CampusVehicles/MI_CampusCar_Wreck.MI_CampusCar_Wreck"));
	WreckMaterial = FoundWreck.Get();

	// Four scene components, no bones: the whole "skeleton" (Section 10.3).
	static const TCHAR* SeatNames[4] = { TEXT("Seat_Driver"), TEXT("Seat_FrontPass"), TEXT("Seat_RearL"), TEXT("Seat_RearR") };
	const float SeatZ = CampusVehicleTuning::Local(CampusVehicleTuning::SeatFloor);
	const FVector SeatOffsets[4] =
	{
		FVector(CampusVehicleTuning::SeatFrontX, -CampusVehicleTuning::SeatSideY, SeatZ),
		FVector(CampusVehicleTuning::SeatFrontX, CampusVehicleTuning::SeatSideY, SeatZ),
		FVector(CampusVehicleTuning::SeatRearX, -CampusVehicleTuning::SeatSideY, SeatZ),
		FVector(CampusVehicleTuning::SeatRearX, CampusVehicleTuning::SeatSideY, SeatZ),
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Seats[Index] = CreateDefaultSubobject<USceneComponent>(SeatNames[Index]);
		Seats[Index]->SetupAttachment(Hull);
		Seats[Index]->SetRelativeLocation(SeatOffsets[Index]);
		WheelHealth[Index] = CampusVehicleTuning::WheelMaxHealth;
		Occupants[Index] = nullptr;
	}
	Health = CampusVehicleTuning::MaxHealth;
}

bool ACampusVehicle::VehiclesEnabled()
{
	return CVarVehiclesEnabled.GetValueOnAnyThread() != 0;
}

const TArray<TWeakObjectPtr<ACampusVehicle>>& ACampusVehicle::All()
{
	return GCampusVehicles;
}

float ACampusVehicle::GetMaxHealth() const
{
	return CampusVehicleTuning::MaxHealth;
}

void ACampusVehicle::BeginPlay()
{
	Super::BeginPlay();
	// campus.VehiclesEnabled 0 is a real kill switch, not just "you cannot get in": an unregistered car is
	// invisible to All(), so ServerFire's rewound car loop, the ENTER button and the HUD never see it, and
	// the level reads exactly as it did before cars existed. Read once here - set the cvar before travel.
	if (!VehiclesEnabled())
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		SetActorTickEnabled(false);
		return;
	}
	GCampusVehicles.AddUnique(this);
	HomeTransform = GetActorTransform();
	FitArt(); // OnConstruction has normally done this already; harmless and keeps spawned cars right
	PlayoutTime = 0.0;
	if (HasAuthority())
	{
		PublishState();
	}
}

void ACampusVehicle::EndPlay(const EEndPlayReason::Type Reason)
{
	GCampusVehicles.RemoveAllSwap([this](const TWeakObjectPtr<ACampusVehicle>& Other) { return Other.Get() == this; });
	Super::EndPlay(Reason);
}

void ACampusVehicle::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusVehicle, Net);
	DOREPLIFETIME(ACampusVehicle, Health);
	DOREPLIFETIME(ACampusVehicle, WheelHealth);
	DOREPLIFETIME(ACampusVehicle, Phase);
	DOREPLIFETIME(ACampusVehicle, Occupants);
}

// ---------------------------------------------------------------------------------------------
// Art: fitted from the imported mesh's own bounds, so no magic offsets are carried in code.

void ACampusVehicle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Placed from a script, so the art has to be fitted outside BeginPlay too (same reason
	// ACampusDoor::OnConstruction re-sizes its leaf).
	FitArt();
}

void ACampusVehicle::FitArt()
{
	// Deterministic and cheap, so it simply re-runs rather than latching: OnConstruction can fire
	// several times in the editor.
	auto Fit = [](UStaticMeshComponent* Mesh, float Scale, const FVector& Offset)
	{
		if (Mesh && Mesh->GetStaticMesh())
		{
			Mesh->SetRelativeScale3D(FVector(Scale));
			Mesh->SetRelativeLocation(Offset);
		}
	};
	float Scale = 1.f;
	FVector Offset = FVector::ZeroVector;
	if (Body && Body->GetStaticMesh())
	{
		const FBox Bounds = Body->GetStaticMesh()->GetBoundingBox();
		const FVector Size = Bounds.GetSize();
		if (Size.X > 1.f)
		{
			// Fit the LENGTH to the hull. The migrated SM_SportsCar is 471 x 228 x 123 cm: fitting its
			// width instead would leave the car 70 cm shorter than its own damage boxes, and a bumper
			// you can see but cannot shoot is worse than 15 cm of cosmetic overhang at the sills.
			Scale = FMath::Clamp(2.f * CampusVehicleTuning::HalfLength / Size.X, 0.5f, 2.f);
		}
		// Centre it on the hull in x / y and stand it on the ground plane (z = 0 of the car frame).
		// The Hull is the root and sits half a hull up, so the art offset is relative to that.
		const FVector Center = Bounds.GetCenter() * Scale;
		Offset = FVector(-Center.X, -Center.Y, CampusVehicleTuning::Local(-Bounds.Min.Z * Scale));
	}
	Fit(Body, Scale, Offset);
	Fit(Glass, Scale, Offset);

	if (Wheels && Wheels->GetStaticMesh())
	{
		const FVector WheelSize = Wheels->GetStaticMesh()->GetBoundingBox().GetSize();
		const float Diameter = FMath::Max(WheelSize.Y, WheelSize.Z);
		const float WheelScale = Diameter > 1.f ? FMath::Clamp(2.f * CampusVehicleTuning::WheelRadius / Diameter, 0.3f, 3.f) : 1.f;
		Wheels->ClearInstances();
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const bool bFront = Index < 2;
			const bool bLeft = (Index % 2) == 0;
			const FVector Location(bFront ? CampusVehicleTuning::WheelX : -CampusVehicleTuning::WheelX,
				bLeft ? -CampusVehicleTuning::WheelY : CampusVehicleTuning::WheelY,
				CampusVehicleTuning::Local(CampusVehicleTuning::WheelRadius));
			Wheels->AddInstance(FTransform(FRotator(0.f, bLeft ? 180.f : 0.f, 0.f), Location, FVector(WheelScale)));
		}
	}
}

void ACampusVehicle::UpdateWheelArt(float Dt)
{
	if (!Wheels || Wheels->GetInstanceCount() != 4)
	{
		return;
	}
	// Spin from the distance travelled, front pair steered: two lines, no animation asset.
	WheelSpin = FMath::Fmod(WheelSpin + FMath::RadiansToDegrees(Speed * Dt / CampusVehicleTuning::WheelRadius), 360.f);
	const float Steer = InSteer * CampusVehicleTuning::WheelSteerVisualDeg;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FTransform Transform;
		Wheels->GetInstanceTransform(Index, Transform);
		const bool bFront = Index < 2;
		const bool bLeft = (Index % 2) == 0;
		Transform.SetRotation(FRotator(0.f, (bLeft ? 180.f : 0.f) + (bFront ? Steer : 0.f), 0.f).Quaternion()
			* FRotator(0.f, 0.f, bLeft ? -WheelSpin : WheelSpin).Quaternion());
		Wheels->UpdateInstanceTransform(Index, Transform, false, Index == 3, false);
	}
}

void ACampusVehicle::ApplyWreckLook()
{
	const bool bWreck = Phase == ECampusVehiclePhase::Wreck;
	// Not latched until the art is actually there: OnRep_Phase can land before BeginPlay has loaded it.
	if (bWreck == bWreckLookApplied || !Body || !Body->GetStaticMesh())
	{
		return;
	}
	bWreckLookApplied = bWreck;
	// A material-instance swap only: no new mesh, no new draw call.
	UMaterialInterface* Material = bWreck ? WreckMaterial.Get() : BodyMaterial.Get();
	if (Body && Material)
	{
		for (int32 Slot = 0; Slot < Body->GetNumMaterials(); ++Slot)
		{
			Body->SetMaterial(Slot, Material);
		}
	}
	if (Glass)
	{
		Glass->SetVisibility(!bWreck); // the glass is gone once it has burned
	}
}

// ---------------------------------------------------------------------------------------------
// Seats

int32 ACampusVehicle::SeatIndexOf(const ACampusCharacter* Who) const
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Who && Occupants[Index] == Who)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool ACampusVehicle::IsSeatFree(ECampusSeat Seat) const
{
	const int32 Index = int32(Seat);
	return Index >= 0 && Index < 4 && Occupants[Index] == nullptr;
}

ACampusCharacter* ACampusVehicle::GetOccupant(ECampusSeat Seat) const
{
	const int32 Index = int32(Seat);
	return Index >= 0 && Index < 4 ? Occupants[Index].Get() : nullptr;
}

ECampusSeat ACampusVehicle::FindFreeSeat(const FVector& Feet) const
{
	if (Phase == ECampusVehiclePhase::Wreck)
	{
		return ECampusSeat::None;
	}
	// Driver first (that is what a player reaching for a car wants), then the other seats by distance.
	if (IsSeatFree(ECampusSeat::Driver) && FVector::Dist(Feet, Seats[0]->GetComponentLocation()) <= UseDistance)
	{
		return ECampusSeat::Driver;
	}
	ECampusSeat Best = ECampusSeat::None;
	float BestDistance = UseDistance;
	for (int32 Index = 1; Index < 4; ++Index)
	{
		const float Distance = FVector::Dist(Feet, Seats[Index]->GetComponentLocation());
		if (Occupants[Index] == nullptr && Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = ECampusSeat(Index);
		}
	}
	return Best;
}

ECampusSeat ACampusVehicle::NextFreeSeat(ECampusSeat From) const
{
	const int32 Start = int32(From) < 4 ? int32(From) : 0;
	for (int32 Step = 1; Step <= 4; ++Step)
	{
		const int32 Index = (Start + Step) % 4;
		if (Occupants[Index] == nullptr)
		{
			return ECampusSeat(Index);
		}
	}
	return ECampusSeat::None;
}

FTransform ACampusVehicle::SeatTransform(ECampusSeat Seat, float CapsuleHalfHeight) const
{
	const int32 Index = int32(Seat) < 4 ? int32(Seat) : 0;
	FTransform Transform = Seats[Index]->GetComponentTransform();
	Transform.SetScale3D(FVector::OneVector);
	Transform.AddToTranslation(FVector(0.f, 0.f, CapsuleHalfHeight));
	return Transform;
}

void ACampusVehicle::SeatAperture(ECampusSeat Seat, FVector& OutLocalForward, float& OutHalfYawDeg,
	float& OutMinPitchDeg, float& OutMaxPitchDeg) const
{
	// Section 7.7. The driver gets a zero cone: a driver never fires.
	OutMinPitchDeg = -35.f;
	OutMaxPitchDeg = 60.f;
	switch (Seat)
	{
	case ECampusSeat::FrontPass:
		OutLocalForward = FVector(0.f, 1.f, 0.f);
		OutHalfYawDeg = 100.f;
		break;
	case ECampusSeat::RearL:
		OutLocalForward = FVector(0.f, -1.f, 0.f);
		OutHalfYawDeg = 110.f;
		break;
	case ECampusSeat::RearR:
		OutLocalForward = FVector(0.f, 1.f, 0.f);
		OutHalfYawDeg = 110.f;
		break;
	default:
		OutLocalForward = FVector(1.f, 0.f, 0.f);
		OutHalfYawDeg = 0.f;
		OutMinPitchDeg = 0.f;
		OutMaxPitchDeg = 0.f;
		break;
	}
}

void ACampusVehicle::SeatOccupant(ACampusCharacter* Who, ECampusSeat Seat)
{
	const int32 Index = int32(Seat);
	Occupants[Index] = Who;
	// The character configures itself (capsule, movement, flags); the car only owns the seat socket.
	Who->SetSeated(this, Seat);
	Who->AttachToComponent(Seats[Index], FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	Who->SetActorRelativeLocation(FVector(0.f, 0.f, Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
	Who->SetActorRelativeRotation(FRotator::ZeroRotator);
	ForceNetUpdate();
}

bool ACampusVehicle::ServerTryEnter(ACampusCharacter* Who, ECampusSeat Seat)
{
	const int32 Index = int32(Seat);
	if (!HasAuthority() || !VehiclesEnabled() || !Who || Index < 0 || Index >= 4
		|| Phase == ECampusVehiclePhase::Wreck || Occupants[Index] != nullptr || Who->IsSeated())
	{
		return false;
	}
	// Distance re-checked against the SERVER's own car transform, with 1.4x grace for relay latency.
	const FVector Feet = Who->GetActorLocation() - FVector(0.f, 0.f, Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (FVector::Dist(Feet, Seats[Index]->GetComponentLocation()) > AdmitDistance)
	{
		return false;
	}
	SeatOccupant(Who, Seat);
	return true;
}

bool ACampusVehicle::ServerTrySwitch(ACampusCharacter* Who, ECampusSeat ToSeat)
{
	const int32 From = SeatIndexOf(Who);
	const int32 To = int32(ToSeat);
	// No seat-hopping out of the explosion: a switch is refused while burning.
	if (!HasAuthority() || From == INDEX_NONE || To < 0 || To >= 4 || From == To
		|| Occupants[To] != nullptr || Phase != ECampusVehiclePhase::Alive)
	{
		return false;
	}
	Occupants[From] = nullptr;
	if (From == int32(ECampusSeat::Driver))
	{
		InThrottle = 0.f;
		InSteer = 0.f;
		bInHandbrake = false; // the car coasts while nobody is at the wheel
	}
	SeatOccupant(Who, ToSeat);
	return true;
}

bool ACampusVehicle::FindExitSpot(const ACampusCharacter* Who, ECampusSeat Seat, FVector& OutFeet) const
{
	const int32 Index = int32(Seat);
	// Tested against the STANDING capsule, not the crouched one the seat uses: the player gets out upright,
	// so a spot that only fits a crouch would drop them inside geometry.
	const float HalfHeight = Who->GetDefaultHalfHeight();
	const float Radius = Who->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector Right = GetActorRightVector();
	const FVector Back = -GetActorForwardVector();
	const bool bLeftSeat = (Index % 2) == 0;
	const float Side = CampusVehicleTuning::HalfWidth + Radius + 20.f;
	// Near side first, then the far side, then out of the back.
	const FVector Candidates[3] =
	{
		Seats[Index]->GetComponentLocation() + Right * (bLeftSeat ? -Side : Side),
		Seats[Index]->GetComponentLocation() + Right * (bLeftSeat ? Side : -Side),
		GetActorLocation() + Back * (CampusVehicleTuning::HalfLength + Radius + 20.f),
	};
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusVehicleExit), false, this);
	Params.AddIgnoredActor(Who);
	FCollisionObjectQueryParams WorldObjects;
	WorldObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	WorldObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	for (const FVector& Candidate : Candidates)
	{
		FHitResult Ground;
		const FVector From(Candidate.X, Candidate.Y, Candidate.Z + 80.f);
		const FVector To(Candidate.X, Candidate.Y, Candidate.Z - 250.f);
		if (!GetWorld()->LineTraceSingleByObjectType(Ground, From, To, WorldObjects, Params))
		{
			continue;
		}
		const FVector Centre = Ground.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 4.f);
		if (GetWorld()->OverlapBlockingTestByChannel(Centre, FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(Radius, HalfHeight), Params))
		{
			continue;
		}
		OutFeet = Ground.ImpactPoint;
		return true;
	}
	return false;
}

void ACampusVehicle::ServerExit(ACampusCharacter* Who)
{
	const int32 Index = SeatIndexOf(Who);
	if (!HasAuthority() || Index == INDEX_NONE)
	{
		return;
	}
	FVector Feet = FVector::ZeroVector;
	if (!FindExitSpot(Who, ECampusSeat(Index), Feet))
	{
		return; // every side blocked: stay seated rather than teleport into geometry
	}
	Occupants[Index] = nullptr;
	if (Index == int32(ECampusSeat::Driver))
	{
		InThrottle = 0.f;
		InSteer = 0.f;
		bInHandbrake = false;
	}
	Who->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	// SetSeated stands the character back up (which itself moves the capsule), so read the height after.
	Who->SetSeated(nullptr, ECampusSeat::None);
	const float HalfHeight = Who->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	Who->SetActorLocation(Feet + FVector(0.f, 0.f, HalfHeight + 4.f), false, nullptr, ETeleportType::TeleportPhysics);
	// Carry the car's momentum out of the door, and make bailing out at speed hurt (PUBG).
	const FVector Momentum = GetActorForwardVector() * Speed;
	Who->LaunchCharacter(Momentum + FVector(0.f, 0.f, 120.f), true, true);
	if (FMath::Abs(Speed) > CampusVehicleTuning::SafeExitSpeed)
	{
		AController* DriverController = GetDriver() ? GetDriver()->GetController() : nullptr;
		Who->ApplyHit(CampusVehicleTuning::ExitDamagePerCm * (FMath::Abs(Speed) - CampusVehicleTuning::SafeExitSpeed), DriverController);
	}
	ForceNetUpdate();
}

void ACampusVehicle::PruneOccupants()
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		ACampusCharacter* Who = Occupants[Index].Get();
		if (Who && (Who->IsPendingKillPending() || Who->GetVehicle() != this))
		{
			Occupants[Index] = nullptr;
			if (Index == int32(ECampusSeat::Driver))
			{
				InThrottle = 0.f;
				InSteer = 0.f;
				bInHandbrake = false; // driver gone (killed, left, disconnected): the car coasts to a stop
			}
		}
	}
}

void ACampusVehicle::ServerSetDriveInput(const ACampusCharacter* From, float Throttle, float Steer, bool bNewHandbrake)
{
	if (!HasAuthority() || !VehiclesEnabled() || Phase == ECampusVehiclePhase::Wreck)
	{
		return;
	}
	if (Occupants[int32(ECampusSeat::Driver)].Get() != From)
	{
		return; // only the player actually in the driver's seat steers this car
	}
	// Rate limiting and violation counting live on the character: it owns the connection that sent this.
	InThrottle = FMath::Clamp(Throttle, -1.f, 1.f);
	InSteer = FMath::Clamp(Steer, -1.f, 1.f);
	bInHandbrake = bNewHandbrake;
	LastInputTime = GetWorld()->GetTimeSeconds();
}

void ACampusVehicle::SetPredictedDriveInput(float Throttle, float Steer, bool bNewHandbrake)
{
	if (!IsPredictingDriver())
	{
		return;
	}
	InThrottle = FMath::Clamp(Throttle, -1.f, 1.f);
	InSteer = FMath::Clamp(Steer, -1.f, 1.f);
	bInHandbrake = bNewHandbrake;
}

// ---------------------------------------------------------------------------------------------
// The integrator. The same code runs on the host and on the driver's own phone.

float ACampusVehicle::MaxSpeedNow() const
{
	const int32 Popped = GetPoppedWheelCount();
	float Share = FMath::Pow(CampusVehicleTuning::PoppedSpeedShare, float(Popped));
	if (Popped >= 2)
	{
		Share = CampusVehicleTuning::SpeedFloorShare; // two tyres out: a sprinting player outruns the car
	}
	return CampusVehicleTuning::MaxSpeed * FMath::Max(Share, CampusVehicleTuning::SpeedFloorShare);
}

float ACampusVehicle::SteerRateNow() const
{
	const float Base = CampusVehicleTuning::SteerRate * FMath::Pow(CampusVehicleTuning::PoppedSteerShare, float(GetPoppedWheelCount()));
	return IsEngineDead() ? Base * CampusVehicleTuning::EngineDeadSteer : Base;
}

float ACampusVehicle::GripDecayNow() const
{
	float Grip = CampusVehicleTuning::GripDecay * FMath::Pow(CampusVehicleTuning::PoppedGripShare, float(GetPoppedWheelCount()));
	if (bInHandbrake)
	{
		Grip *= CampusVehicleTuning::HandbrakeGrip;
	}
	return Grip;
}

void ACampusVehicle::SimulateStep(float Dt)
{
	using namespace CampusVehicleTuning;
	const float Throttle = IsEngineDead() ? 0.f : InThrottle;
	const float TopSpeed = MaxSpeedNow();

	// Longitudinal: throttle accelerates, the other way brakes to a stop and then reverses, nothing
	// pushes past the envelope - so a client cannot ask for more than the wheels allow.
	if (bInHandbrake)
	{
		Speed = FMath::Sign(Speed) * FMath::Max(0.f, FMath::Abs(Speed) - HandbrakeDecel * Dt);
	}
	else if (Throttle > 0.01f)
	{
		Speed = Speed < 0.f ? FMath::Min(0.f, Speed + BrakeDecel * Dt) : FMath::Min(TopSpeed, Speed + Accel * Throttle * Dt);
	}
	else if (Throttle < -0.01f)
	{
		Speed = Speed > 0.f ? FMath::Max(0.f, Speed + BrakeDecel * Throttle * Dt)
			: FMath::Max(-MaxReverseSpeed, Speed + Accel * Throttle * Dt);
	}
	else
	{
		const float Drag = CoastDecel * (IsEngineDead() ? EngineDeadDrag : 1.f);
		Speed = FMath::Sign(Speed) * FMath::Max(0.f, FMath::Abs(Speed) - Drag * Dt);
	}

	// Steering: yaw rate scales with speed (no turning on the spot) and falls away near top speed.
	const float SpeedFrac = FMath::Clamp(FMath::Abs(Speed) / FMath::Max(TopSpeed, 1.f), 0.f, 1.f);
	const float FadeIn = FMath::Clamp(FMath::Abs(Speed) / SteerStartSpeed, 0.f, 1.f);
	const float TargetYawRate = InSteer * SteerRateNow() * FadeIn * (1.f - SteerFalloff * SpeedFrac) * FMath::Sign(Speed == 0.f ? 1.f : Speed);
	YawRate = FMath::FInterpTo(YawRate, bAirborne ? 0.f : TargetYawRate, Dt, SteerResponse);

	// Lateral: the tyres hold most of the cornering demand; the rest shows up as a slide that washes out.
	const float Centripetal = FMath::DegreesToRadians(YawRate) * Speed;
	LateralSpeed += Centripetal * SlipShare * Dt;
	LateralSpeed *= FMath::Exp(-GripDecayNow() * Dt);

	if (bAirborne)
	{
		VerticalSpeed -= Gravity * Dt;
	}
}

void ACampusVehicle::ProbeGround(float Dt)
{
	using namespace CampusVehicleTuning;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusVehicleGround), false, this);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (const ACampusCharacter* Rider = Occupants[Index].Get())
		{
			Params.AddIgnoredActor(Rider);
		}
	}
	FCollisionObjectQueryParams WorldObjects;
	WorldObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	WorldObjects.AddObjectTypesToQuery(ECC_WorldDynamic);

	const FTransform Transform = GetActorTransform();
	const FVector Corners[4] =
	{
		FVector(WheelX, -WheelY, 0.f), FVector(WheelX, WheelY, 0.f),
		FVector(-WheelX, -WheelY, 0.f), FVector(-WheelX, WheelY, 0.f),
	};
	float Heights[4] = { 0.f, 0.f, 0.f, 0.f };
	int32 Found = 0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector At = Transform.TransformPosition(Corners[Index]);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.f, 0.f, GroundProbe * 0.5f),
			At - FVector(0.f, 0.f, GroundProbe), WorldObjects, Params))
		{
			Heights[Index] = Hit.ImpactPoint.Z;
			++Found;
		}
		else
		{
			Heights[Index] = -UE_BIG_NUMBER;
		}
	}
	bAirborne = Found == 0;
	if (bAirborne)
	{
		return;
	}
	// Conform: the mean of the probes gives the ride height, the differences give pitch and roll.
	// A wheel hanging over an edge borrows the body's own ground height rather than dragging the car down.
	float Sum = 0.f;
	const float Fallback = GetActorLocation().Z - HullCentreZ;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float Height = Heights[Index] > -UE_BIG_NUMBER * 0.5f ? Heights[Index] : Fallback;
		Heights[Index] = Height;
		Sum += Height;
	}
	const float Front = (Heights[0] + Heights[1]) * 0.5f;
	const float Rear = (Heights[2] + Heights[3]) * 0.5f;
	const float Left = (Heights[0] + Heights[2]) * 0.5f;
	const float Right = (Heights[1] + Heights[3]) * 0.5f;
	const float TargetPitch = FMath::RadiansToDegrees(FMath::Atan2(Front - Rear, 2.f * WheelX));
	const float TargetRoll = FMath::RadiansToDegrees(FMath::Atan2(Right - Left, 2.f * WheelY));
	GroundPitch = FMath::FInterpTo(GroundPitch, FMath::Clamp(TargetPitch, -35.f, 35.f), Dt, ConformResponse);
	GroundRoll = FMath::FInterpTo(GroundRoll, FMath::Clamp(TargetRoll, -35.f, 35.f), Dt, ConformResponse);

	// The actor origin rides HullCentreZ above the mean of the four probes.
	const float GroundZ = Sum * 0.25f + HullCentreZ;
	const FVector Location = GetActorLocation();
	if (Location.Z > GroundZ + 2.f)
	{
		// Ground in reach but below us (a crest, a kerb we just drove off): fall onto it under gravity
		// instead of snapping down. Steering is kept - this is not real air time.
		VerticalSpeed -= Gravity * Dt;
		return;
	}
	// Hard landing hurts the car (same formula as a wall, on the vertical speed).
	if (VerticalSpeed < -HardLandingSpeed && HasAuthority())
	{
		const float Impact = ImpactDamagePerCm * (FMath::Abs(VerticalSpeed) - ImpactFreeSpeed);
		ApplyPartDamage(ECampusVehiclePart::Body, INDEX_NONE, Impact, GetDriver() ? GetDriver()->GetController() : nullptr);
		Speed *= HardLandingSpeedKept;
	}
	VerticalSpeed = 0.f;
	SetActorLocation(FVector(Location.X, Location.Y, GroundZ), false, nullptr, ETeleportType::TeleportPhysics);
}

void ACampusVehicle::SweepMove(float Dt)
{
	using namespace CampusVehicleTuning;
	const FRotator Rotation = GetActorRotation();
	const float NewYaw = Rotation.Yaw + YawRate * Dt;
	const FRotator Target(GroundPitch, NewYaw, GroundRoll);
	const FVector Forward = FRotator(0.f, NewYaw, 0.f).Vector();
	const FVector Right = FRotator(0.f, NewYaw, 0.f).RotateVector(FVector::RightVector);
	const FVector Delta = (Forward * Speed + Right * LateralSpeed) * Dt + FVector(0.f, 0.f, VerticalSpeed * Dt);

	FHitResult Hit;
	SetActorLocationAndRotation(GetActorLocation() + Delta, Target, true, &Hit);
	if (!Hit.bBlockingHit || !HasAuthority())
	{
		return;
	}
	// Wall impact: the blocked sweep is already in hand, so this is nearly free (Section 7.6).
	const float Hitting = FMath::Abs(Speed);
	LateralSpeed = 0.f;
	if (Hitting > ImpactFreeSpeed)
	{
		const float Impact = ImpactDamagePerCm * (Hitting - ImpactFreeSpeed);
		AController* DriverController = GetDriver() ? GetDriver()->GetController() : nullptr;
		Speed *= ImpactSpeedKept;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (ACampusCharacter* Rider = Occupants[Index].Get())
			{
				// A crash hurts everyone in the car, team or not.
				Rider->ApplyHit(Impact * ImpactOccupantShare, DriverController);
			}
		}
		ApplyPartDamage(ECampusVehiclePart::Body, INDEX_NONE, Impact, DriverController);
	}
	else
	{
		Speed *= ImpactSpeedKept;
	}
}

void ACampusVehicle::DoRoadkill(float Dt)
{
	using namespace CampusVehicleTuning;
	if (Phase == ECampusVehiclePhase::Wreck || FMath::Abs(Speed) <= MinRoadkillSpeed)
	{
		return;
	}
	ACampusCharacter* Driver = GetDriver();
	AController* DriverController = Driver ? Driver->GetController() : nullptr;
	if (!DriverController)
	{
		return; // no driver, no damage: a rolling driverless wreck cannot kill
	}
	const FVector Centre = Hull->GetComponentLocation();
	const float SphereRadius = FMath::Sqrt(FMath::Square(HalfLength) + FMath::Square(HalfWidth)) + 60.f;
	const FVector CarVelocity = GetActorForwardVector() * Speed;

	// One sphere overlap per car per tick, pawns only. NOT a sweep: the hull deliberately ignores Pawn.
	FCollisionObjectQueryParams Pawns;
	Pawns.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusVehicleRoadkill), false, this);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Centre, FQuat::Identity, Pawns, FCollisionShape::MakeSphere(SphereRadius), Params);

	const double Now = GetWorld()->GetTimeSeconds();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		ACampusCharacter* Victim = Cast<ACampusCharacter>(Overlap.GetActor());
		if (!Victim || Victim->GetVehicle() == this || SeatIndexOf(Victim) != INDEX_NONE)
		{
			continue; // occupants are immune to their own car
		}
		if (const double* Last = RoadkillCooldown.Find(Victim); Last && Now - *Last < RoadkillCooldownSeconds)
		{
			continue; // one pass deals one hit, not 18 ticks of it
		}
		// The sphere was only the broadphase. This is the contact test: the victim's capsule against the
		// hull box, so nobody is ever run over by 3 m of air beside the car.
		const UCapsuleComponent* Capsule = Victim->GetCapsuleComponent();
		const FVector Local = GetActorTransform().InverseTransformPosition(Victim->GetActorLocation());
		const float VictimRadius = Capsule->GetScaledCapsuleRadius();
		const float VictimHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		if (FMath::Abs(Local.X) > HalfLength + VictimRadius || FMath::Abs(Local.Y) > HalfWidth + VictimRadius
			|| Local.Z - VictimHalfHeight > HullCentreZ || Local.Z + VictimHalfHeight < -HullCentreZ)
		{
			continue;
		}
		const FVector ToVictim = (Victim->GetActorLocation() - Centre).GetSafeNormal();
		const float Closing = FMath::Max(0.f, FVector::DotProduct(CarVelocity - Victim->GetVelocity(), ToVictim));
		RoadkillCooldown.Add(Victim, Now);
		// Launched either way - that is the shove - but damage only to the other team.
		Victim->LaunchCharacter(ToVictim * (Closing * RoadkillLaunchShare) + FVector(0.f, 0.f, RoadkillLaunchUp), true, true);
		// Friendly fire off, through the same one test ServerFire uses: RED/BLUE in a room, the SQUAD
		// in battle royale. Running a squadmate over still shoves them; it just costs them nothing.
		if (CampusBR::AreFriendly(GetWorld(), DriverController, Victim->GetController()))
		{
			continue;
		}
		const float Dealt = RoadkillDamageFor(Closing);
		if (Dealt > 0.f)
		{
			Victim->ApplyHit(Dealt, DriverController);
		}
	}
	// Forget stale pairs so the map cannot grow across a round.
	for (auto It = RoadkillCooldown.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || Now - It.Value() > 2.0)
		{
			It.RemoveCurrent();
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Damage, burning and the wreck

float ACampusVehicle::PartMultiplier(ECampusVehiclePart Part)
{
	switch (Part)
	{
	case ECampusVehiclePart::Engine: return 1.f;
	case ECampusVehiclePart::Body:   return 0.75f;
	case ECampusVehiclePart::Roof:   return 0.5f;
	case ECampusVehiclePart::Glass:  return 0.25f;
	// A tyre takes the shot straight, with no zone multiplier (Section 7.2).
	case ECampusVehiclePart::Wheel:  return 1.f;
	default:                         return 1.f;
	}
}

void ACampusVehicle::ApplyPartDamage(ECampusVehiclePart Part, int32 WheelIndex, float Amount, AController* InstigatorController)
{
	if (!HasAuthority() || Amount <= 0.f || Phase == ECampusVehiclePhase::Wreck)
	{
		return;
	}
	if (Part == ECampusVehiclePart::Wheel)
	{
		// Tyres are a mobility kill, not a vehicle kill: wheel damage never touches hull HP.
		if (WheelIndex < 0 || WheelIndex >= 4)
		{
			return;
		}
		WheelHealth[WheelIndex] = uint8(FMath::Max(0, int32(WheelHealth[WheelIndex]) - FMath::RoundToInt(Amount)));
		ForceNetUpdate();
		return;
	}
	if (Phase != ECampusVehiclePhase::Alive)
	{
		return; // a burning car is already dead; a wreck takes no more damage
	}
	LastDamagedBy = InstigatorController;
	Health = FMath::Max(0.f, Health - Amount);
	ForceNetUpdate();
	if (Health <= 0.f)
	{
		Phase = ECampusVehiclePhase::Burning;
		OnRep_Phase(); // the host's own view, exactly as ACampusDoor::ApplyDamage does (and starts the burn)
		if (Amount >= CampusVehicleTuning::InstantExplodeHit)
		{
			Explode();
		}
	}
}

void ACampusVehicle::Explode()
{
	using namespace CampusVehicleTuning;
	if (!HasAuthority() || Phase == ECampusVehiclePhase::Wreck)
	{
		return;
	}
	AController* Killer = LastDamagedBy.Get();
	const FVector Centre = Hull->GetComponentLocation();

	// Occupants die regardless of team: the 3 s burn was their warning.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (ACampusCharacter* Rider = Occupants[Index].Get())
		{
			Rider->ApplyHit(OccupantExplosionDamage, Killer);
		}
	}
	FCollisionObjectQueryParams Pawns;
	Pawns.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusVehicleExplosion), false, this);
	TArray<FOverlapResult> Overlaps;
	GetWorld()->OverlapMultiByObjectType(Overlaps, Centre, FQuat::Identity, Pawns, FCollisionShape::MakeSphere(ExplosionRadius), Params);
	FCollisionObjectQueryParams WorldObjects;
	WorldObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	WorldObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		ACampusCharacter* Victim = Cast<ACampusCharacter>(Overlap.GetActor());
		if (!Victim || SeatIndexOf(Victim) != INDEX_NONE)
		{
			continue;
		}
		const float Distance = FVector::Dist(Centre, Victim->GetActorLocation());
		if (Distance > ExplosionRadius)
		{
			continue;
		}
		FHitResult Blocked;
		if (GetWorld()->LineTraceSingleByObjectType(Blocked, Centre, Victim->GetActorLocation(), WorldObjects, Params))
		{
			continue; // behind a wall: nothing
		}
		const float Share = 1.f - Distance / ExplosionRadius;
		Victim->LaunchCharacter((Victim->GetActorLocation() - Centre).GetSafeNormal() * (500.f + 400.f * Share)
			+ FVector(0.f, 0.f, 300.f), true, true);
		// The killer's own side is spared - the RED/BLUE team in a room, the SQUAD in battle royale.
		// In a 4v4, 3-minute round a team-kill is pure griefing; in BR a squadmate's car must not be
		// the thing that ends your match. A car that nobody shot has no side and spares nobody.
		if (Killer && CampusBR::AreFriendly(GetWorld(), Killer, Victim->GetController()))
		{
			continue;
		}
		Victim->ApplyHit(ExplosionDamage * Share, Killer);
	}
	EnterWreck();
}

void ACampusVehicle::EnterWreck()
{
	if (!HasAuthority())
	{
		return;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (ACampusCharacter* Rider = Occupants[Index].Get())
		{
			Occupants[Index] = nullptr;
			Rider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Rider->SetSeated(nullptr, ECampusSeat::None);
		}
	}
	Phase = ECampusVehiclePhase::Wreck;
	Speed = 0.f;
	YawRate = 0.f;
	LateralSpeed = 0.f;
	InThrottle = 0.f;
	InSteer = 0.f;
	ForceNetUpdate();
	OnRep_Phase();
}

void ACampusVehicle::ResetForRound()
{
	if (!HasAuthority())
	{
		return;
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (ACampusCharacter* Rider = Occupants[Index].Get())
		{
			Occupants[Index] = nullptr;
			Rider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Rider->SetSeated(nullptr, ECampusSeat::None);
		}
		WheelHealth[Index] = CampusVehicleTuning::WheelMaxHealth;
	}
	Health = CampusVehicleTuning::MaxHealth;
	Phase = ECampusVehiclePhase::Alive;
	SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	Speed = YawRate = LateralSpeed = VerticalSpeed = 0.f;
	InThrottle = InSteer = 0.f;
	bInHandbrake = false;
	GroundPitch = GroundRoll = 0.f;
	LastDamagedBy = nullptr;
	// A stale pre-teleport frame would let a shot taken right after the reset hit a car that has moved.
	History.Empty();
	RoadkillCooldown.Empty();
	Buffer.Empty();
	SmoothPosOffset = FVector::ZeroVector;
	SmoothRotOffset = FQuat::Identity;
	bHasPredicted = false;
	PublishState();
	ForceNetUpdate();
	OnRep_Phase();
}

int32 ACampusVehicle::GetWheelHealth(int32 Index) const
{
	return Index >= 0 && Index < 4 ? int32(WheelHealth[Index]) : 0;
}

int32 ACampusVehicle::GetPoppedWheelCount() const
{
	int32 Popped = 0;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Popped += WheelHealth[Index] == 0 ? 1 : 0;
	}
	return Popped;
}

float ACampusVehicle::GetBurnSecondsLeft() const
{
	return Phase == ECampusVehiclePhase::Burning && GetWorld() ? FMath::Max(0.f, BurnEndTime - GetWorld()->GetTimeSeconds()) : 0.f;
}

// ---------------------------------------------------------------------------------------------
// Lag compensation: one transform per frame, the same shape as ACampusCharacter::FHitboxFrame.

void ACampusVehicle::RecordFrame()
{
	const double Now = GetWorld()->GetTimeSeconds();
	FVehicleFrame& Frame = History.AddDefaulted_GetRef();
	Frame.Time = Now;
	Frame.Location = GetActorLocation();
	Frame.Rotation = GetActorQuat();
	int32 Old = 0;
	while (Old < History.Num() - 1 && History[Old].Time < Now - CampusVehicleTuning::HistorySeconds)
	{
		++Old;
	}
	if (Old > 0)
	{
		History.RemoveAt(0, Old, EAllowShrinking::No);
	}
}

ACampusVehicle::FVehicleFrame ACampusVehicle::FrameAt(double Time) const
{
	if (History.IsEmpty())
	{
		FVehicleFrame Now;
		Now.Time = Time;
		Now.Location = GetActorLocation();
		Now.Rotation = GetActorQuat();
		return Now;
	}
	if (Time <= History[0].Time)
	{
		return History[0];
	}
	for (int32 Index = History.Num() - 1; Index > 0; --Index)
	{
		const FVehicleFrame& A = History[Index - 1];
		const FVehicleFrame& B = History[Index];
		if (A.Time <= Time)
		{
			const float Alpha = B.Time > A.Time ? FMath::Clamp(float((Time - A.Time) / (B.Time - A.Time)), 0.f, 1.f) : 1.f;
			FVehicleFrame Out;
			Out.Time = Time;
			Out.Location = FMath::Lerp(A.Location, B.Location, Alpha);
			Out.Rotation = FQuat::Slerp(A.Rotation, B.Rotation, Alpha);
			return Out;
		}
	}
	return History.Last();
}


bool ACampusVehicle::RewoundTrace(const FVector& Start, const FVector& Dir, float MaxDistance, double When,
	float& OutDistance, ECampusVehiclePart& OutPart, int32& OutWheelIndex, bool& bOutBlocks) const
{
	OutPart = ECampusVehiclePart::None;
	OutWheelIndex = INDEX_NONE;
	bOutBlocks = false;
	OutDistance = MaxDistance;
	if (!VehiclesEnabled())
	{
		return false;
	}
	const FVehicleFrame Frame = FrameAt(When);
	const FVector LocalStart = Frame.Rotation.UnrotateVector(Start - Frame.Location);
	const FVector LocalDir = Frame.Rotation.UnrotateVector(Dir);
	bool bAny = false;
	for (const FPartBox& Part : PartBoxes())
	{
		if (Part.Part == ECampusVehiclePart::Wheel && WheelHealth[Part.WheelIndex] == 0)
		{
			continue; // a popped tyre is a flat rag: nothing left to shoot
		}
		float Distance = 0.f;
		if (!SegmentHitsBox(LocalStart, LocalDir, OutDistance, Part.Center, Part.Extent, Distance) || Distance >= OutDistance)
		{
			continue;
		}
		if (Distance <= 0.f)
		{
			// Zero means the shot STARTED inside this box. Start is the shooter's CAMERA, and a third-person
			// camera clips into a car you are standing beside - without this, every bullet fired from cover
			// behind a car would be swallowed by that car at distance 0.
			continue;
		}
		OutDistance = Distance;
		OutPart = Part.Part;
		OutWheelIndex = Part.WheelIndex;
		// A wreck is solid cover: everything blocks, including where the glass used to be.
		bOutBlocks = Part.bBlocks || Phase == ECampusVehiclePhase::Wreck;
		bAny = true;
	}
#if ENABLE_DRAW_DEBUG
	if (CVarVehicleDebug.GetValueOnGameThread() != 0)
	{
		DrawDebugVehicle(When);
	}
#endif
	return bAny;
}

// ---------------------------------------------------------------------------------------------
// Netcode

void ACampusVehicle::PublishState()
{
	const FRotator Rotation = GetActorRotation();
	Net.Location = GetActorLocation();
	Net.Yaw = CampusVehicleTuning::PackYaw(Rotation.Yaw);
	Net.Pitch = int16(FMath::Clamp(FMath::RoundToInt(Rotation.Pitch * 100.f), -32768, 32767));
	Net.Roll = int16(FMath::Clamp(FMath::RoundToInt(Rotation.Roll * 100.f), -32768, 32767));
	Net.Speed = int16(FMath::Clamp(FMath::RoundToInt(Speed), -32768, 32767));
	Net.YawRate = int16(FMath::Clamp(FMath::RoundToInt(YawRate * 10.f), -32768, 32767));
	Net.Flags = uint8((bInHandbrake ? 1 : 0) | (bAirborne ? 2 : 0) | (IsEngineDead() ? 4 : 0));
}

void ACampusVehicle::OnRep_Phase()
{
	// Every machine starts its own 3 s burn clock from the phase change, so the occupant's BAIL OUT
	// countdown is right on a client too (BurnEndTime itself is never replicated).
	if (Phase == ECampusVehiclePhase::Burning && GetWorld())
	{
		BurnEndTime = GetWorld()->GetTimeSeconds() + CampusVehicleTuning::BurnSeconds;
	}
	ApplyWreckLook();
}

bool ACampusVehicle::IsPredictingDriver() const
{
	const ACampusCharacter* Driver = GetDriver();
	return !HasAuthority() && Driver && Driver->IsLocallyControlled();
}

void ACampusVehicle::OnRep_State()
{
	const double Now = GetWorld()->GetTimeSeconds();
	FPlayoutSample& Sample = Buffer.AddDefaulted_GetRef();
	Sample.State = Net;
	Sample.Time = Now;
	while (Buffer.Num() > 4)
	{
		Buffer.RemoveAt(0, 1, EAllowShrinking::No);
	}
	if (!IsPredictingDriver())
	{
		// A watching client keeps the car's numbers for the wheel art and the dead-reckon fallback only.
		Speed = float(Net.Speed);
		YawRate = float(Net.YawRate) * 0.1f;
		bInHandbrake = (Net.Flags & 1) != 0;
		bAirborne = (Net.Flags & 2) != 0;
		return;
	}
	// Driver reconcile: compare the snapshot with what WE predicted at the same instant. The snapshot is
	// about half a ping old, and the driver's own prediction history says where we thought we were then.
	double HalfPing = 0.0;
	if (const ACampusCharacter* Driver = GetDriver())
	{
		if (const APlayerState* State = Driver->GetPlayerState())
		{
			HalfPing = FMath::Min(0.5, State->GetPingInMilliseconds() * 0.0005);
		}
	}
	const FVehicleFrame Predicted = FrameAt(Now - HalfPing);
	const FVector Error = FVector(Net.Location) - Predicted.Location;
	const FQuat Target = FRotator(float(Net.Pitch) * 0.01f, CampusVehicleTuning::UnpackYaw(Net.Yaw), float(Net.Roll) * 0.01f).Quaternion();
	const FQuat RotationError = Target * Predicted.Rotation.Inverse();
	const float Size = Error.Size();
	if (!bHasPredicted || Size > CampusVehicleTuning::HardCorrection)
	{
		// Only a teleport or a round reset lands here: take it in one frame.
		bHasPredicted = true;
		SmoothPosOffset = FVector::ZeroVector;
		SmoothRotOffset = FQuat::Identity;
		History.Empty();
		SetActorLocationAndRotation(FVector(Net.Location), Target, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	// Never snap under 150 cm: a snapping car on a phone screen reads as a broken game.
	SmoothPosOffset += Error;
	SmoothRotOffset = RotationError * SmoothRotOffset;
	SmoothTau = Size > CampusVehicleTuning::SoftCorrection ? CampusVehicleTuning::CorrectionTauFar : CampusVehicleTuning::CorrectionTauNear;
}

void ACampusVehicle::TickDriverPrediction(float Dt)
{
	SimulateStep(Dt);
	ProbeGround(Dt);
	SweepMove(Dt);
	// Pay the outstanding correction off a slice at a time, so the state converges on the server's
	// without the transform ever jumping.
	const float Share = 1.f - FMath::Exp(-Dt / FMath::Max(SmoothTau, 0.01f));
	if (!SmoothPosOffset.IsNearlyZero(0.05f) || !SmoothRotOffset.Equals(FQuat::Identity, 0.0005f))
	{
		const FVector Slice = SmoothPosOffset * Share;
		const FQuat RotationSlice = FQuat::Slerp(FQuat::Identity, SmoothRotOffset, Share);
		SmoothPosOffset -= Slice;
		SmoothRotOffset = RotationSlice.Inverse() * SmoothRotOffset;
		SetActorLocationAndRotation(GetActorLocation() + Slice, RotationSlice * GetActorQuat(), false, nullptr, ETeleportType::TeleportPhysics);
	}
	RecordFrame(); // the driver's own history, read back by OnRep_State to measure the error
	bHasPredicted = true;
}

void ACampusVehicle::TickRemoteInterp(float Dt)
{
	using namespace CampusVehicleTuning;
	if (Buffer.IsEmpty())
	{
		return;
	}
	const double Newest = Buffer.Last().Time;
	PlayoutTime += Dt;
	// Playout clock: 100 ms behind the newest snapshot. Resynced only when it drifts out of the window,
	// so normal jitter costs nothing and a stall does not make the car sprint to catch up.
	const double Behind = Newest - PlayoutTime;
	if (Behind < PlayoutDelay * 0.5 || Behind > PlayoutDelay + MaxDeadReckon)
	{
		PlayoutTime = Newest - PlayoutDelay;
	}

	auto StateRotation = [](const FCampusVehicleState& State)
	{
		return FRotator(float(State.Pitch) * 0.01f, UnpackYaw(State.Yaw), float(State.Roll) * 0.01f).Quaternion();
	};
	if (PlayoutTime >= Newest)
	{
		// Starved: dead-reckon forward from Speed + YawRate for up to 250 ms, then freeze in place.
		const double Ahead = FMath::Min(PlayoutTime - Newest, MaxDeadReckon);
		const FCampusVehicleState& Last = Buffer.Last().State;
		const float LastYaw = UnpackYaw(Last.Yaw) + float(Last.YawRate) * 0.1f * float(Ahead);
		const FVector Forward = FRotator(0.f, LastYaw, 0.f).Vector();
		SetActorLocationAndRotation(FVector(Last.Location) + Forward * (float(Last.Speed) * float(Ahead)),
			FRotator(float(Last.Pitch) * 0.01f, LastYaw, float(Last.Roll) * 0.01f), false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	if (Buffer.Num() < 4)
	{
		// Not enough samples for a spline yet (the first 150 ms of a car's life): a lerp will do.
		const FPlayoutSample& A = Buffer[0];
		const FPlayoutSample& B = Buffer.Last();
		const float Blend = B.Time > A.Time ? FMath::Clamp(float((PlayoutTime - A.Time) / (B.Time - A.Time)), 0.f, 1.f) : 1.f;
		SetActorLocationAndRotation(FMath::Lerp(FVector(A.State.Location), FVector(B.State.Location), Blend),
			FQuat::Slerp(StateRotation(A.State), StateRotation(B.State), Blend), false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	// Find the pair the playout clock sits between, then Catmull-Rom across the four samples: a lerp
	// visibly kinks on corners, which is exactly where a car is being watched.
	int32 Next = 1;
	while (Next < Buffer.Num() - 1 && Buffer[Next].Time < PlayoutTime)
	{
		++Next;
	}
	const int32 I1 = Next - 1;
	const int32 I2 = Next;
	const FPlayoutSample& S1 = Buffer[I1];
	const FPlayoutSample& S2 = Buffer[I2];
	const FPlayoutSample& S0 = Buffer[FMath::Max(I1 - 1, 0)];
	const FPlayoutSample& S3 = Buffer[FMath::Min(I2 + 1, Buffer.Num() - 1)];
	const float Alpha = S2.Time > S1.Time ? FMath::Clamp(float((PlayoutTime - S1.Time) / (S2.Time - S1.Time)), 0.f, 1.f) : 1.f;
	const FVector Location = FMath::CubicCRSplineInterpSafe<FVector>(
		FVector(S0.State.Location), FVector(S1.State.Location), FVector(S2.State.Location), FVector(S3.State.Location),
		float(S0.Time - S1.Time), 0.f, float(S2.Time - S1.Time), float(S3.Time - S1.Time),
		Alpha * float(S2.Time - S1.Time));
	SetActorLocationAndRotation(Location, FQuat::Slerp(StateRotation(S1.State), StateRotation(S2.State), Alpha),
		false, nullptr, ETeleportType::TeleportPhysics);
}

// ---------------------------------------------------------------------------------------------

void ACampusVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ApplyWreckLook();

	if (HasAuthority())
	{
		PruneOccupants();
		// ServerDriveInput is unreliable: if the stream dries up (relay stall, app backgrounded) the car
		// must not keep the last throttle forever. One second with nothing and it coasts.
		if (LastInputTime > 0.0 && GetWorld()->GetTimeSeconds() - LastInputTime > 1.0)
		{
			InThrottle = 0.f;
			InSteer = 0.f;
			bInHandbrake = false;
		}
		if (Phase == ECampusVehiclePhase::Burning && GetWorld()->GetTimeSeconds() >= BurnEndTime)
		{
			Explode();
		}
		if (Phase != ECampusVehiclePhase::Wreck)
		{
			// A car that fell off the map stops dead: its occupants are already killed by the kill plane.
			if (GetActorLocation().Z < CampusVehicleTuning::KillHeight)
			{
				Speed = 0.f;
				VerticalSpeed = 0.f;
			}
			else
			{
				SimulateStep(DeltaSeconds);
				ProbeGround(DeltaSeconds);
				SweepMove(DeltaSeconds);
				DoRoadkill(DeltaSeconds);
			}
		}
		RecordFrame();
		PublishState();
#if ENABLE_DRAW_DEBUG
		if (CVarVehicleDebug.GetValueOnGameThread() != 0)
		{
			DrawDebugVehicle(GetWorld()->GetTimeSeconds());
		}
#endif
	}
	else if (IsPredictingDriver())
	{
		TickDriverPrediction(DeltaSeconds);
	}
	else
	{
		TickRemoteInterp(DeltaSeconds);
	}
	UpdateWheelArt(DeltaSeconds);
}

#if ENABLE_DRAW_DEBUG
void ACampusVehicle::DrawDebugVehicle(double When) const
{
	// Build this before the damage model: MaxRewind 0.3 means debugging a 300 ms-old world you
	// otherwise cannot see. Green solid, cyan glass, red roof, yellow wheels; white = the live hull.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FVehicleFrame Frame = FrameAt(When);
	for (const FPartBox& Part : PartBoxes())
	{
		FColor Colour = FColor::Green;
		switch (Part.Part)
		{
		case ECampusVehiclePart::Glass: Colour = FColor::Cyan; break;
		case ECampusVehiclePart::Roof: Colour = FColor::Red; break;
		case ECampusVehiclePart::Wheel: Colour = FColor::Yellow; break;
		default: break;
		}
		DrawDebugBox(World, Frame.Location + Frame.Rotation.RotateVector(Part.Center), Part.Extent, Frame.Rotation, Colour, false, 0.1f, 0, 1.f);
	}
	DrawDebugBox(World, Hull->GetComponentLocation(), Hull->GetScaledBoxExtent(), GetActorQuat(), FColor::White, false, 0.1f, 0, 1.f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		// The crouched capsule a seated occupant actually uses (half-height 55, radius 30).
		const FTransform SeatAt = SeatTransform(ECampusSeat(Index), 55.f);
		DrawDebugCapsule(World, SeatAt.GetLocation(), 55.f, 30.f, FQuat::Identity,
			Occupants[Index] ? FColor::Orange : FColor(60, 60, 60), false, 0.1f, 0, 1.f);
	}
	if (FMath::Abs(Speed) > CampusVehicleTuning::MinRoadkillSpeed)
	{
		const float SphereRadius = FMath::Sqrt(FMath::Square(CampusVehicleTuning::HalfLength) + FMath::Square(CampusVehicleTuning::HalfWidth)) + 60.f;
		DrawDebugSphere(World, Hull->GetComponentLocation(), SphereRadius, 12, FColor::Magenta, false, 0.1f, 0, 1.f);
	}
	const int32 First = FMath::Max(0, History.Num() - 10);
	for (int32 Index = First + 1; Index < History.Num(); ++Index)
	{
		DrawDebugLine(World, History[Index - 1].Location, History[Index].Location, FColor::Purple, false, 0.1f, 0, 2.f);
	}
}
#endif

// ---------------------------------------------------------------------------------------------
// Tests. The first one is the rollout gate from Section 13: ViewDelaySeconds() is the only change
// that touches on-foot behaviour, and for a player on foot it MUST still be the old 0.08 constant.

#if WITH_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCampusVehicleContractTest, "Campus.Vehicles.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCampusVehicleContractTest::RunTest(const FString& Parameters)
{
	// ---- view delays (Section 8.3)
	const ACampusCharacter* OnFoot = GetDefault<ACampusCharacter>();
	TestTrue(TEXT("on foot the view delay is still exactly 0.08 s"),
		FMath::IsNearlyEqual(OnFoot->ViewDelaySeconds(), 0.08, 1.e-9));
	TestTrue(TEXT("a seated player is drawn by the car's 0.10 s playout buffer"),
		FMath::IsNearlyEqual(ACampusVehicle::PlayoutDelay, 0.10, 1.e-9));
	TestFalse(TEXT("a character with no car is not seated"), OnFoot->IsSeated());

	// ---- roadkill curve against the doc's own table (Section 7.5)
	TestTrue(TEXT("400 cm/s shoves for nothing"), FMath::IsNearlyEqual(RoadkillDamageFor(400.f), 0.f, 0.01f));
	TestTrue(TEXT("600 cm/s hurts for 45"), FMath::IsNearlyEqual(RoadkillDamageFor(600.f), 45.f, 0.01f));
	TestTrue(TEXT("1000 cm/s kills a full-health player (135)"), FMath::IsNearlyEqual(RoadkillDamageFor(1000.f), 135.f, 0.01f));
	TestTrue(TEXT("top speed deals the full 180"), FMath::IsNearlyEqual(RoadkillDamageFor(1200.f), 180.f, 0.01f));

	// ---- the glass / metal boundary (Section 7.4), in the car's own frame
	const TArray<FPartBox>& Boxes = PartBoxes();
	auto ShootSideways = [&Boxes](float GroundZ, ECampusVehiclePart& OutPart, bool& bOutBlocks)
	{
		// From 500 cm off the driver's side, straight across the cabin at this height above the ground.
		const FVector Start(0.f, -500.f, CampusVehicleTuning::Local(GroundZ));
		const FVector Dir(0.f, 1.f, 0.f);
		float Best = 1000.f;
		OutPart = ECampusVehiclePart::None;
		bOutBlocks = false;
		for (const FPartBox& Box : Boxes)
		{
			float Distance = 0.f;
			if (SegmentHitsBox(Start, Dir, Best, Box.Center, Box.Extent, Distance) && Distance < Best)
			{
				Best = Distance;
				OutPart = Box.Part;
				bOutBlocks = Box.bBlocks;
			}
		}
	};
	ECampusVehiclePart Part = ECampusVehiclePart::None;
	bool bBlocks = false;
	ShootSideways(120.f, Part, bBlocks); // occupant chest height: through the window
	TestTrue(TEXT("a shot at chest height goes through the glass"), Part == ECampusVehiclePart::Glass);
	TestFalse(TEXT("glass does not block the bullet"), bBlocks);
	ShootSideways(60.f, Part, bBlocks); // door height: metal
	TestTrue(TEXT("a shot at door height hits metal"), Part == ECampusVehiclePart::Body || Part == ECampusVehiclePart::Wheel);
	TestTrue(TEXT("metal blocks the bullet"), bBlocks);
	ShootSideways(155.f, Part, bBlocks); // roof panel
	TestTrue(TEXT("a shot at roof height hits the roof"), Part == ECampusVehiclePart::Roof);
	TestTrue(TEXT("the roof blocks the bullet"), bBlocks);

	// ---- wheel penalties (Section 7.2): two popped tyres floor the car under a sprinting player
	TestTrue(TEXT("two popped tyres floor the top speed at 480 cm/s"),
		FMath::IsNearlyEqual(CampusVehicleTuning::MaxSpeed * CampusVehicleTuning::SpeedFloorShare, 480.f, 0.01f));
	return true;
}
#endif
