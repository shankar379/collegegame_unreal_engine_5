#include "Campus/CampusLoot.h"

#include "Campus/CampusCharacter.h"
#include "Campus/CampusWeapons.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace CampusLootTuning
{
	/** Bob: 6 cm up and down, one cycle a second, plus a slow spin. Cosmetic, runs on every machine. */
	constexpr float BobHeight = 6.f;
	constexpr float BobRate = 1.6f;
	constexpr float SpinRate = 55.f;
	/** How far above a candidate point the ground trace starts, and how far down it looks. */
	constexpr float TraceUp = 1200.f;
	constexpr float TraceDown = 6000.f;
	/** Rounds in an ammo box, vest points, helmet (one), and the medkit's health. */
	constexpr int32 AmmoRounds = 30;
	constexpr int32 ArmorPoints = 50;
	constexpr int32 MedkitHeal = 75;
}

namespace
{
	TArray<TWeakObjectPtr<ACampusPickup>> GCampusPickups;

	TAutoConsoleVariable<int32> CVarLootEnabled(
		TEXT("campus.LootEnabled"),
		1,
		TEXT("Ground loot (ACampusPickup / ACampusLootSpawner). 0 stops spawners and refuses every pickup."),
		ECVF_Default);

	UStaticMesh* ShapeMesh(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}
}

ACampusPickup::ACampusPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	// Three small values that change once (at spawn): cheap enough to keep every box relevant to everyone,
	// the same posture ACampusDoor takes.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	Reach = CreateDefaultSubobject<USphereComponent>(TEXT("Reach"));
	RootComponent = Reach;
	Reach->SetSphereRadius(ReachDistance * 0.5f);
	Reach->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Reach->SetGenerateOverlapEvents(true);
	Reach->SetCanEverAffectNavigation(false);

	Art = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Art"));
	Art->SetupAttachment(Reach);
	Art->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Art->SetGenerateOverlapEvents(false);
	Art->SetCanEverAffectNavigation(false);
	Art->CastShadow = true;
}

bool ACampusPickup::LootEnabled()
{
	return CVarLootEnabled.GetValueOnAnyThread() != 0;
}

const TArray<TWeakObjectPtr<ACampusPickup>>& ACampusPickup::All()
{
	return GCampusPickups;
}

void ACampusPickup::Setup(ECampusPickupKind InKind, FName InWeaponId, int32 InAmount)
{
	Kind = InKind;
	WeaponId = InWeaponId;
	Amount = InAmount;
	ApplyLook();
}

void ACampusPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusPickup, Kind);
	DOREPLIFETIME(ACampusPickup, WeaponId);
	DOREPLIFETIME(ACampusPickup, Amount);
}

void ACampusPickup::BeginPlay()
{
	Super::BeginPlay();
	GCampusPickups.Add(this);
	RestLocation = GetActorLocation();
	BobTime = FMath::FRand() * 10.f; // boxes next to each other must not bob in lockstep
	ApplyLook();
	if (HasAuthority())
	{
		Reach->OnComponentBeginOverlap.AddDynamic(this, &ACampusPickup::OnOverlap);
	}
}

void ACampusPickup::EndPlay(const EEndPlayReason::Type Reason)
{
	GCampusPickups.RemoveAllSwap([this](const TWeakObjectPtr<ACampusPickup>& Other) { return Other.Get() == this; });
	Super::EndPlay(Reason);
}

void ACampusPickup::OnRep_Kind()
{
	ApplyLook();
}

void ACampusPickup::ApplyLook()
{
	if (!Art)
	{
		return;
	}
	// One mesh per kind, all of them engine primitives except the gun, which is the rifle the hands already hold.
	const TCHAR* Path = TEXT("/Engine/BasicShapes/Cube.Cube");
	FVector Scale(0.3f, 0.3f, 0.2f);
	FLinearColor Tint(0.8f, 0.8f, 0.2f);
	switch (Kind)
	{
	case ECampusPickupKind::Weapon:
	{
		const FCampusWeaponDef& Def = CampusWeapons::FindOrStarter(WeaponId);
		Path = TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle");
		Scale = FVector(Def.MeshScale);
		Tint = Def.Tint;
		break;
	}
	case ECampusPickupKind::Ammo:
		Scale = FVector(0.25f, 0.18f, 0.14f);
		Tint = FLinearColor(0.75f, 0.60f, 0.15f);
		break;
	case ECampusPickupKind::Medkit:
		Scale = FVector(0.34f, 0.26f, 0.12f);
		Tint = FLinearColor(0.85f, 0.15f, 0.15f);
		break;
	case ECampusPickupKind::Armor:
		Scale = FVector(0.16f, 0.34f, 0.30f);
		Tint = FLinearColor(0.20f, 0.45f, 0.85f);
		break;
	case ECampusPickupKind::Helmet:
		Path = TEXT("/Engine/BasicShapes/Sphere.Sphere");
		Scale = FVector(0.22f);
		Tint = FLinearColor(0.25f, 0.70f, 0.45f);
		break;
	}
	if (UStaticMesh* Mesh = ShapeMesh(Path))
	{
		Art->SetStaticMesh(Mesh);
	}
	Art->SetRelativeScale3D(Scale);
	// A no-op on materials without the parameter (the engine shapes); the rifle picks it up where it has one.
	Art->SetVectorParameterValueOnMaterials(TEXT("Tint"), FVector(Tint.R, Tint.G, Tint.B));
	Art->SetVectorParameterValueOnMaterials(TEXT("Color"), FVector(Tint.R, Tint.G, Tint.B));
}

FString ACampusPickup::Label() const
{
	switch (Kind)
	{
	case ECampusPickupKind::Weapon:
		return FString(CampusWeapons::FindOrStarter(WeaponId).Name);
	case ECampusPickupKind::Ammo:
		return FString::Printf(TEXT("AMMO %d"), Amount);
	case ECampusPickupKind::Medkit:
		return TEXT("MEDKIT");
	case ECampusPickupKind::Armor:
		return TEXT("VEST");
	case ECampusPickupKind::Helmet:
		return TEXT("HELMET");
	}
	return TEXT("PICK UP");
}

bool ACampusPickup::ServerTake(ACampusCharacter* Who)
{
	if (!HasAuthority() || !LootEnabled() || !Who || Who->GetHealth() <= 0.f)
	{
		return false;
	}
	// Reach is checked here, on the host: the taker's phone only asks.
	if (FVector::DistSquared(Who->GetActorLocation(), GetActorLocation()) > FMath::Square(ReachDistance + 80.f))
	{
		return false;
	}
	if (!Who->ReceivePickup(Kind, WeaponId, Amount))
	{
		return false;
	}
	Destroy();
	return true;
}

void ACampusPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	BobTime += DeltaSeconds;
	const float Bob = FMath::Sin(BobTime * CampusLootTuning::BobRate * UE_PI) * CampusLootTuning::BobHeight;
	Art->SetRelativeLocation(FVector(0.f, 0.f, Bob));
	Art->SetRelativeRotation(FRotator(0.f, BobTime * CampusLootTuning::SpinRate, 0.f));
}

void ACampusPickup::OnOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (!IsAutoTaken())
	{
		return; // guns, vests, helmets and medkits wait for the PICK UP button
	}
	if (ACampusCharacter* Who = Cast<ACampusCharacter>(OtherActor))
	{
		ServerTake(Who);
	}
}

// ---------------------------------------------------------------------------------------------

ACampusLootSpawner::ACampusLootSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // the spawner is host-side furniture; only what it spawns replicates
}

void ACampusLootSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		SpawnWave();
	}
}

void ACampusLootSpawner::SpawnWave()
{
	if (!HasAuthority() || !ACampusPickup::LootEnabled())
	{
		return;
	}
	for (const TWeakObjectPtr<ACampusPickup>& Old : Spawned)
	{
		if (ACampusPickup* Pickup = Old.Get())
		{
			Pickup->Destroy();
		}
	}
	Spawned.Reset();

	TArray<FVector> Spots;
	CollectTaggedSpots(Spots);
	if (Spots.Num() > 0)
	{
		// Tagged spots are exact and finite: shuffle and take as many as the count asks for.
		for (int32 Index = Spots.Num() - 1; Index > 0; --Index)
		{
			Spots.Swap(Index, FMath::RandHelper(Index + 1));
		}
		const int32 Wanted = FMath::Min(Count, Spots.Num());
		for (int32 Index = 0; Index < Wanted; ++Index)
		{
			SpawnOne(Spots[Index]);
		}
		return;
	}
	// No tagged spots: random points in the box, each dropped onto whatever is under it.
	const FVector Center = GetActorLocation();
	int32 Attempts = 0;
	while (Spawned.Num() < Count && Attempts < Count * 12)
	{
		++Attempts;
		const FVector Candidate = Center + FVector(
			FMath::FRandRange(-BoundsExtent.X, BoundsExtent.X),
			FMath::FRandRange(-BoundsExtent.Y, BoundsExtent.Y),
			FMath::FRandRange(0.f, BoundsExtent.Z));
		FVector Ground;
		if (GroundAt(Candidate, Ground))
		{
			SpawnOne(Ground);
		}
	}
}

bool ACampusLootSpawner::GroundAt(const FVector& Above, FVector& OutPoint) const
{
	const FVector Start = Above + FVector(0.f, 0.f, CampusLootTuning::TraceUp);
	const FVector End = Above - FVector(0.f, 0.f, CampusLootTuning::TraceDown);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusLootGround), false, this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return false;
	}
	OutPoint = Hit.ImpactPoint + FVector(0.f, 0.f, 25.f);
	return true;
}

void ACampusLootSpawner::CollectTaggedSpots(TArray<FVector>& OutSpots) const
{
	if (SpotTag.IsNone())
	{
		return;
	}
	const FVector Center = GetActorLocation();
	const FBox Bounds(Center - BoundsExtent, Center + BoundsExtent);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor == this || !Actor->ActorHasTag(SpotTag))
		{
			continue;
		}
		const FVector Where = Actor->GetActorLocation();
		if (Bounds.IsInside(Where))
		{
			OutSpots.Add(Where + FVector(0.f, 0.f, 25.f));
		}
	}
}

void ACampusLootSpawner::SpawnOne(const FVector& Where)
{
	// The mix: guns by WeaponShare, then ammo twice as common as the rest of the consumables.
	ECampusPickupKind Kind = ECampusPickupKind::Ammo;
	FName WeaponId;
	int32 Amount = CampusLootTuning::AmmoRounds;
	if (FMath::FRand() < WeaponShare)
	{
		const float Roll = FMath::FRand();
		const int32 Tier = Roll < Tier3Chance ? 3 : (Roll < Tier3Chance + Tier2Chance ? 2 : 1);
		Kind = ECampusPickupKind::Weapon;
		WeaponId = CampusWeapons::RandomOfTier(Tier);
		Amount = CampusWeapons::FindOrStarter(WeaponId).Magazine;
	}
	else
	{
		const float Roll = FMath::FRand();
		if (Roll < 0.4f)
		{
			Kind = ECampusPickupKind::Ammo;
			Amount = CampusLootTuning::AmmoRounds;
		}
		else if (Roll < 0.65f)
		{
			Kind = ECampusPickupKind::Medkit;
			Amount = CampusLootTuning::MedkitHeal;
		}
		else if (Roll < 0.85f)
		{
			Kind = ECampusPickupKind::Armor;
			Amount = CampusLootTuning::ArmorPoints;
		}
		else
		{
			Kind = ECampusPickupKind::Helmet;
			Amount = 1;
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACampusPickup* Pickup = GetWorld()->SpawnActor<ACampusPickup>(ACampusPickup::StaticClass(),
		FTransform(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Where), Params);
	if (!Pickup)
	{
		return;
	}
	Pickup->Setup(Kind, WeaponId, Amount);
	Spawned.Add(Pickup);
}
