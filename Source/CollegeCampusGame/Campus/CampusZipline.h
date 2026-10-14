// Zipline (three.js src/game/zipline.js): a sagging cable between two steel gantries, ridden BOTH
// WAYS. Walk up to either gantry, press ZIPLINE (touch) or E (keyboard) and you are carried to the
// other end; press again - or Jump - and you let go and drop.
//
// ONE ACTOR PER LINE, SPAWNED FROM C++ PER LEVEL (SpawnForLevel), keyed on the level's name exactly
// the way CampusClashSpawn::For resolves its team pads: a new line is a row in the table in the .cpp
// and never an asset rebuild. The ride itself lives on the pawn (ACampusCharacter::TickZip, which is
// the port of Zipline.update); this actor is the path, the two grab spots, and - on the levels whose
// imported art does not already carry them - the gantries and the cable.

#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Actor.h"
#include "CampusZipline.generated.h"

class UInstancedStaticMeshComponent;

/** THE WHOLE DEFINITION OF ONE LINE, and the only thing that goes over the wire: both gantry floors
 *  in world cm, the gantry height over each, and whether this level needs gantries drawn at all.
 *  Every other number (the Bezier, the grab spots, the arc length) is derived from these on every
 *  machine, so the host and the phones cannot disagree about where the cable is. */
USTRUCT()
struct FCampusZipSpan
{
	GENERATED_BODY()

	/** Gantry floor, world cm: where you stand to grab the line and where you step off. End 0. */
	UPROPERTY(EditAnywhere, Category="Zipline")
	FVector_NetQuantize EndA = FVector::ZeroVector;

	/** End 1. */
	UPROPERTY(EditAnywhere, Category="Zipline")
	FVector_NetQuantize EndB = FVector::ZeroVector;

	/** Gantry height over its own floor (cm); zipline.js tower(height). The pulley the cable runs
	 *  through sits at floor + SlabTop + this - PulleyDrop, which is the web's `e.y + 0.3 + tower - 0.95`. */
	UPROPERTY(EditAnywhere, Category="Zipline")
	float TowerA = 460.f;

	UPROPERTY(EditAnywhere, Category="Zipline")
	float TowerB = 460.f;

	/** FALSE ON L_MATCH ONLY: its imported college mesh (SM_Campus_Ziplines, exported straight from
	 *  zipline.js by tools/export-unreal-recipe.page.js) already carries both gantries and the cable,
	 *  and a second set built here would sit inside the first. True everywhere else. */
	UPROPERTY(EditAnywhere, Category="Zipline")
	bool bBuildArt = true;
};

UCLASS()
class ACampusZipline : public AActor
{
	GENERATED_BODY()

public:
	ACampusZipline();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** THE LINES THIS LEVEL CARRIES, once. Server only (the actors replicate down), and a no-op if the
	 *  world already has one - the same guarantee ACampusDayNight::EnsureFor gives. */
	static void SpawnForLevel(UWorld* World);

	/** Every line alive in any world; filter by GetWorld(). The HUD asks whether a gantry is in reach
	 *  on every painted frame, which is no place for a TActorIterator (ACampusPickup::All does the
	 *  same for the ground loot, for the same reason). */
	static const TArray<TWeakObjectPtr<ACampusZipline>>& All();

	/** A point / the travel direction along the cable, Alpha 0 at end 0 and 1 at end 1. */
	FVector PointAt(float Alpha) const;
	FVector TangentAt(float Alpha) const;

	/** End 0 or 1 whose gantry floor is within reach of these feet, or INDEX_NONE. */
	int32 NearEnd(const FVector& Feet) const;

	/** Where to step off at End: on the slab, a little toward the other gantry. */
	FVector Landing(int32 End) const;

	/** Cable length (cm): the Bezier's arc length, like three.js Curve.getLength. */
	float GetLength() const { return Length; }

	// zipline.js tuning, in centimetres.
	static constexpr float Speed = 1300.f;       // SPEED 13 m/s along the cable
	static constexpr float Accel = 900.f;        // ACCEL 9 m/s^2 off the start and into the brake
	static constexpr float Hang = 160.f;         // HANG: the rider's feet below the cable
	static constexpr float UseDistance = 260.f;  // USE_DISTANCE from a gantry's centre to grab the line
	static constexpr float Sag = 80.f;           // SAG: the dip of the Bezier's control point
	/** How far above or below a gantry's floor still counts as standing at it (zipline.js near()). */
	static constexpr float ReachHeight = 250.f;
	/** The step off the far end: enough to clear the posts, not enough to be a launch. */
	static constexpr float DismountPush = 350.f;
	/** zipline.js tower(): the concrete slab's top face, and the pulley bar below the gantry's top. */
	static constexpr float SlabTop = 30.f;
	static constexpr float PulleyDrop = 95.f;

private:
	UPROPERTY(ReplicatedUsing=OnRep_Span, EditAnywhere, Category="Zipline")
	FCampusZipSpan Span;

	UFUNCTION()
	void OnRep_Span();

	/** Derive the Bezier, the grab spots and the arc length from Span, then (re)build the art. */
	void Rebuild();
	/** One gantry, zipline.js tower(): a steel gate on a concrete slab, its pulley bar along the cable.
	 *  `Along` points at the other end, so the cable runs straight through the gate. */
	void BuildGantry(const FVector& Floor, float Height, const FVector& Along);
	/** The cable itself: short cylinders along the Bezier, no collision (userData.noCollide). */
	void BuildCable();
	/** One box in gantry-local axes (across the cable, along it, up) into Into. */
	void AddBox(UInstancedStaticMeshComponent* Into, const FVector& Floor, const FVector& Across,
		const FVector& Along, const FVector& Local, const FVector& Size);
	/** One square bar between two gantry-local points (the braces of each side frame). */
	void AddBar(UInstancedStaticMeshComponent* Into, const FVector& Floor, const FVector& Across,
		const FVector& Along, const FVector& From, const FVector& To, float Thickness);

	/** Quadratic Bezier of the cable (world cm): end 0's pulley, the sag control, end 1's pulley. */
	FVector P0 = FVector::ZeroVector;
	FVector P1 = FVector::ZeroVector;
	FVector P2 = FVector::ZeroVector;
	/** Both gantry floors, index 0 and 1 - the order Span, NearEnd, Landing and the ride all use. */
	FVector Floors[2] = { FVector::ZeroVector, FVector::ZeroVector };
	float Length = 1.f;

	// One component per material, so a gantry of ~70 boxes is four draw calls - the same merge the web
	// builder does (zipline.js: "66 boxes -> 4 draw calls"), plus one for the cable.
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Steel;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> SteelLight;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Concrete;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Dark;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> CableLine;
};
