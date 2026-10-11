// College door (three.js src/world/doors.js): swings open as a player walks up, away from them, and stays open.
// Solid only while nearly shut. The host decides (who walked up, which way it swings) and replicates that one
// number; every phone animates the swing itself. So the door is open / solid the same for everyone, late joiners
// included.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusDoor.generated.h"

class UStaticMeshComponent;

UCLASS()
class ACampusDoor : public AActor
{
	GENERATED_BODY()

public:
	ACampusDoor();

	/** Leaf size in cm; the actor sits on the hinge and the leaf extends along its +X. */
	UPROPERTY(EditAnywhere, Category="Door")
	float Width = 105.f;

	UPROPERTY(EditAnywhere, Category="Door")
	float Height = 210.f;

	UPROPERTY(EditAnywhere, Category="Door")
	float Thickness = 4.f;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Hinge;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Leaf;

	void LookForPlayers();

	float Angle = 0.f;

	/** Swing target in degrees (0 shut, +-OpenAngle open): set by the host only, replicated. */
	UPROPERTY(Replicated)
	float Target = 0.f;
	float CheckTimer = 0.f;
	bool bSolid = true;
};
