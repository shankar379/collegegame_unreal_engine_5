// Shot effects, ported from the three.js game (game.js makeEffectsPool / addDecal / addTracer):
// a recycled pool of 60 small dark marks where bullets hit the world, and a short yellow tracer
// (0.05 s) from the gun to the hit point. Cosmetic only: never runs on a dedicated server.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CampusFx.generated.h"

class UStaticMeshComponent;

UCLASS()
class UCampusFxSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Dark 10 cm mark on a surface (oldest mark is reused once all 60 are placed). */
	void AddImpact(const FVector& Point, const FVector& Normal);

	/** Thin yellow line from A to B, visible for 0.05 s. */
	void AddTracer(const FVector& From, const FVector& To);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCampusFxSubsystem, STATGROUP_Tickables); }
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	bool EnsurePool();

	UPROPERTY(Transient)
	TObjectPtr<AActor> PoolActor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Impacts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Tracers;

	TArray<float> TracerLife;
	int32 NextImpact = 0;
	int32 NextTracer = 0;
};
