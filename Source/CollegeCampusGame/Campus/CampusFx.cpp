#include "Campus/CampusFx.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"

namespace CampusFx
{
	constexpr int32 ImpactCount = 60;   // three.js: 60 decals, recycled
	constexpr int32 TracerCount = 6;
	constexpr float ImpactSize = 10.f;  // cm (three.js 0.1 m plane)
	constexpr float TracerLife = 0.05f; // s
	constexpr float TracerThickness = 1.2f;
}

bool UCampusFxSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UCampusFxSubsystem::EnsurePool()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}
	if (PoolActor)
	{
		return true;
	}

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* ImpactMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CampusFX/MI_Impact.MI_Impact"));
	UMaterialInterface* TracerMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CampusFX/MI_Tracer.MI_Tracer"));
	if (!Plane || !Cube)
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	PoolActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	USceneComponent* Root = NewObject<USceneComponent>(PoolActor, TEXT("Root"));
	PoolActor->SetRootComponent(Root);
	Root->RegisterComponent();

	auto MakeMesh = [this, Root](UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(PoolActor);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		if (Material)
		{
			Component->SetMaterial(0, Material);
		}
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetVisibility(false);
		Component->SetupAttachment(Root);
		Component->RegisterComponent();
		return Component;
	};
	for (int32 Index = 0; Index < CampusFx::ImpactCount; ++Index)
	{
		Impacts.Add(MakeMesh(Plane, ImpactMaterial));
	}
	for (int32 Index = 0; Index < CampusFx::TracerCount; ++Index)
	{
		Tracers.Add(MakeMesh(Cube, TracerMaterial));
	}
	TracerLife.Init(0.f, CampusFx::TracerCount);
	return true;
}

void UCampusFxSubsystem::AddImpact(const FVector& Point, const FVector& Normal)
{
	if (!EnsurePool())
	{
		return;
	}
	UStaticMeshComponent* Mark = Impacts[NextImpact];
	NextImpact = (NextImpact + 1) % Impacts.Num();
	// The plane faces +Z; lift it 1 cm off the surface so it never z-fights.
	const FVector SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Mark->SetWorldLocationAndRotation(Point + SafeNormal, FRotationMatrix::MakeFromZ(SafeNormal).Rotator());
	Mark->SetWorldScale3D(FVector(CampusFx::ImpactSize / 100.f));
	Mark->SetVisibility(true);
}

void UCampusFxSubsystem::AddTracer(const FVector& From, const FVector& To)
{
	if (!EnsurePool())
	{
		return;
	}
	const FVector Delta = To - From;
	const float Length = Delta.Size();
	if (Length < 1.f)
	{
		return;
	}
	const int32 Index = NextTracer;
	NextTracer = (NextTracer + 1) % Tracers.Num();
	UStaticMeshComponent* Line = Tracers[Index];
	Line->SetWorldLocationAndRotation((From + To) * 0.5f, Delta.Rotation());
	Line->SetWorldScale3D(FVector(Length / 100.f, CampusFx::TracerThickness / 100.f, CampusFx::TracerThickness / 100.f));
	Line->SetVisibility(true);
	TracerLife[Index] = CampusFx::TracerLife;
}

void UCampusFxSubsystem::Tick(float DeltaTime)
{
	for (int32 Index = 0; Index < TracerLife.Num(); ++Index)
	{
		if (TracerLife[Index] > 0.f)
		{
			TracerLife[Index] -= DeltaTime;
			if (TracerLife[Index] <= 0.f)
			{
				Tracers[Index]->SetVisibility(false);
			}
		}
	}
}
