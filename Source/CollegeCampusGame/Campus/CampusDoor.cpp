#include "Campus/CampusDoor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace CampusDoorTuning
{
	// three.js doors.js
	constexpr float OpenAngle = 100.f;
	constexpr float OpenDistance = 180.f;
	constexpr float HeightRange = 220.f;
	constexpr float SwingSpeed = 5.f;
	constexpr float SolidAngle = 17.f;
	constexpr float CheckInterval = 0.1f;
}

ACampusDoor::ACampusDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.f;
	// One float per door, and only when it changes: cheap enough to keep every door relevant to everyone.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	RootComponent = Hinge;
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Hinge);
	Leaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Leaf"));
	Leaf->SetupAttachment(Pivot);
	Leaf->SetMobility(EComponentMobility::Movable);
	Leaf->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Leaf->SetCanEverAffectNavigation(false);
	Leaf->CastShadow = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Leaf->SetStaticMesh(Cube.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Wood(TEXT("/Game/College/M_CollegeDoor.M_CollegeDoor"));
	if (Wood.Succeeded())
	{
		Leaf->SetMaterial(0, Wood.Object);
	}
}

void ACampusDoor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// The engine cube is 100 cm and centred: scale it to the leaf and move it off the hinge.
	Leaf->SetRelativeScale3D(FVector(Width, Thickness, Height) / 100.f);
	Leaf->SetRelativeLocation(FVector(Width * 0.5f, 0.f, Height * 0.5f));
}

void ACampusDoor::BeginPlay()
{
	Super::BeginPlay();
	// Placed from a script, so the size may not have gone through the construction script.
	Leaf->SetRelativeScale3D(FVector(Width, Thickness, Height) / 100.f);
	Leaf->SetRelativeLocation(FVector(Width * 0.5f, 0.f, Height * 0.5f));
	// Spread the proximity checks over frames.
	CheckTimer = FMath::FRand() * CampusDoorTuning::CheckInterval;
	Health = MaxHealth;
	LastHealth = MaxHealth;
}

void ACampusDoor::ApplyDamage(float Amount)
{
	if (!HasAuthority() || Health <= 0.f || Amount <= 0.f)
	{
		return;
	}
	Health = FMath::Max(0.f, Health - Amount);
	ForceNetUpdate(); // doors replicate at 2 Hz: a hit must show now
	OnRep_Health();   // the host's own view
}

void ACampusDoor::Repair()
{
	if (!HasAuthority())
	{
		return;
	}
	Health = MaxHealth;
	ForceNetUpdate();
	OnRep_Health();
}

void ACampusDoor::OnRep_Health()
{
	if (Health < LastHealth && Health > 0.f)
	{
		ShakeTime = 0.15f;
	}
	LastHealth = Health;
	ApplyBrokenState();
}

void ACampusDoor::ApplyBrokenState()
{
	const bool bBroken = Health <= 0.f;
	Leaf->SetVisibility(!bBroken);
	Leaf->SetCollisionEnabled(bBroken || !bSolid ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
}

void ACampusDoor::LookForPlayers()
{
	using namespace CampusDoorTuning;
	const FVector Center = Hinge->GetComponentLocation() + GetActorForwardVector() * (Width * 0.5f);
	const FVector Normal = GetActorRightVector();
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const FVector Feet = It->GetActorLocation();
		const FVector Delta = Feet - Center;
		if (FMath::Abs(Delta.Z) > HeightRange || Delta.Size2D() > OpenDistance)
		{
			continue;
		}
		if (Target == 0.f)
		{
			// Swing away from whoever walks up.
			Target = FVector::DotProduct(Delta, Normal) > 0.f ? -OpenAngle : OpenAngle;
		}
		return;
	}
}

void ACampusDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	using namespace CampusDoorTuning;

	if (ShakeTime > 0.f)
	{
		ShakeTime = FMath::Max(0.f, ShakeTime - DeltaSeconds);
		const float Jolt = ShakeTime > 0.f ? FMath::Sin(ShakeTime * 120.f) * 1.5f : 0.f;
		Pivot->SetRelativeRotation(FRotator(0.f, Angle + Jolt, 0.f));
	}
	if (Health <= 0.f)
	{
		return; // gone: no swinging, no collision
	}
	CheckTimer -= DeltaSeconds;
	if (CheckTimer <= 0.f && HasAuthority())
	{
		CheckTimer = CheckInterval;
		LookForPlayers();
	}
	if (FMath::IsNearlyEqual(Angle, Target, 0.05f))
	{
		return;
	}
	Angle = FMath::FInterpTo(Angle, Target, DeltaSeconds, SwingSpeed);
	Pivot->SetRelativeRotation(FRotator(0.f, Angle, 0.f));
	const bool bNowSolid = FMath::Abs(Angle) < SolidAngle;
	if (bNowSolid != bSolid)
	{
		bSolid = bNowSolid;
		Leaf->SetCollisionEnabled(bSolid ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

void ACampusDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusDoor, Target);
	DOREPLIFETIME(ACampusDoor, Health);
}
