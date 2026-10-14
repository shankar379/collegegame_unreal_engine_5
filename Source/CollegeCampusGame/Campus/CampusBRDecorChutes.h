// Decorative parachutes for the BR flight: five cosmetic figures that leave the plane's path while it
// crosses the map and come down under canopy beside the real drop.
//
// WHY THIS EXISTS. Bots never ride the plane. They are promoted on the ground (ACampusBRGameMode::
// Promote / GroundedSpawn) so the phone that hosts the match never pays for fifty passengers during
// the busiest second of the match. The sky of a "fifty player" drop therefore held only the humans the
// session actually found, which on a quiet queue is one. These figures put the crowd back.
//
// THEY ARE SCENERY, AND THEY ARE NOT REPLICATED. Every machine spawns its OWN set from the flight's
// seed - drawn from the plane's replicated launch clock - so the same five jumps happen on the host and
// on every phone for no bandwidth, no actor channels and no relevancy work. One canopy on the wire
// (ACampusBRChute) costs two updates a second each; five of these cost nothing.
//
// NOTHING IN THE GAME MAY EVER READ ONE. No collision, no damage, no team, no player state, no trace
// channel: a cosmetic jumper that could be shot at, bumped into or counted as alive would be a bug in
// the scoreboard, the kill feed and the zone all at once. The descent is scripted from a single ground
// trace taken at release; it never asks physics for anything.
//
// The look is deliberately ACampusBRChute's - the same imported parachute (CampusChuteModel, declared
// in CampusBRPlane.h) over a cylinder body, and the same flattened-dome fallback behind it - and the
// figure's own materials are the ones the plane's class default object already references, so this
// file adds no asset to the cook; see CampusBRPlane.cpp's note on ConstructorHelpers in constructors.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusBRDecorChutes.generated.h"

class ACampusBRPlane;
class USceneComponent;
class UStaticMeshComponent;
class UWorld;

/**
 * One figure's whole jump, drawn from the flight seed BEFORE anything is spawned. Every value that
 * could differ between two machines lives in here and nowhere else, which is what makes "same seed,
 * same sky" a property of the spawner rather than a hope about the descent code.
 */
struct FCampusBRDecorPlan
{
	/** Flight progress (ACampusBRPlane::FlightAlpha) at which this figure leaves. */
	float ReleaseAlpha = 0.f;
	/** Where it leaves from, relative to the plane's door and in the plane's own axes. */
	FVector ExitOffset = FVector::ZeroVector;
	/** How far, and which way, it glides from the door before touching down. */
	FVector2D GlideOffset = FVector2D::ZeroVector;
	/** Canopy opens this far above the landing point (60-80 m, like the divers' AutoChuteHeight band). */
	float PopHeight = 7000.f;
	/** Sway across the line of travel: cycles per second and where in the cycle it starts. */
	float SwaySpeed = 0.3f;
	float SwayPhase = 0.f;
};

/** Where one figure is in its jump. Local state only - none of it is replicated or read by gameplay. */
enum class ECampusBRDecorPhase : uint8
{
	/** Spawned and hidden, waiting for the plane to reach ReleaseAlpha over the map square. */
	Waiting,
	Freefall,
	Canopy,
	Landed
};

/**
 * One cosmetic jumper: a cylinder body and a sphere head under ACampusBRChute's canopy. It moves itself
 * and nothing else - three flat unlit primitives and the parachute, no collision, no shadows, no
 * physics (the five fallback primitives behind the parachute are never drawn unless its asset is gone).
 */
UCLASS()
class ACampusBRDecorChute : public AActor
{
	GENERATED_BODY()

public:
	ACampusBRDecorChute();

	virtual void Tick(float DeltaSeconds) override;

	/** Hand this figure the jump it is going to make. Local only: called by CampusBRDecor::SpawnForFlight
	 *  on whichever machine spawned it. The flight itself is not passed in - it is read from
	 *  ACampusBRPlane::Get each frame until release, so scenery holds no reference to the aircraft. */
	void Ride(const FCampusBRDecorPlan& InPlan);

private:
	/** The plane reached our alpha: take the exit point, find the ground once, start falling. */
	void Release(const ACampusBRPlane* InPlane);
	/** One frame of the scripted descent, freefall or canopy. */
	void Descend(float DeltaSeconds);
	void PopCanopy();
	void Land();
	/** Dome and rigging together: they are one object to the eye and must never be half shown. */
	void ShowCanopy(bool bShown);

	FCampusBRDecorPlan Plan;
	ECampusBRDecorPhase Phase = ECampusBRDecorPhase::Waiting;

	/** Taken at release: where the jump started and the point the one ground trace found. The descent is
	 *  a function of these two and the current height, so it cannot drift with the frame rate. */
	FVector ExitPoint = FVector::ZeroVector;
	FVector TouchPoint = FVector::ZeroVector;
	/** Yaw along the glide, degrees. The figure faces the way it is going. */
	float Bearing = 0.f;
	/** Seconds since release, for the sway only. */
	float AirTime = 0.f;
	/** World time of touchdown, or negative while still in the air. */
	float LandTime = -1.f;
	/** Did the imported canopy load? Answered once, in the constructor: it decides which of the two
	 *  parachutes below ShowCanopy is allowed to turn on. */
	bool bUsesModel = false;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Harness;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Torso;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Head;

	/** The imported parachute (CampusChuteModel), the same one the real divers hang under. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CanopyModel;

	/** Fallback canopy, drawn only when that asset is missing. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Canopy;

	/** Four rigging lines, one per diagonal. Named one at a time rather than held in an array, because a
	 *  component UPROPERTY has to be fixed up when the class default object is instanced - the same
	 *  reason ACampusBRChute spells its four out. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineFrontLeft;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineBackLeft;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineBackRight;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineFrontRight;
};

namespace CampusBRDecor
{
	/**
	 * Fill this flight's sky. Called on EVERY machine with the SAME seed, which is why the result needs
	 * no replication: the seed comes from the plane's replicated launch clock, so the host and every
	 * phone draw the same five jumps.
	 *
	 * Safe to call every frame - the first call for a given plane spawns the set and every call after it
	 * returns immediately, so the one hook can sit in the plane's Tick where all machines reach it.
	 * Does nothing on a dedicated server: nobody there is looking at the sky.
	 */
	void SpawnForFlight(UWorld* World, const ACampusBRPlane* Plane, int32 Seed);
}
