// Battle royale deployment: the fenced waiting platform, the plane that crosses the map, the
// freefall / parachute drop itself, and the canopy everyone else can see.
//
// Four actors, one phase of the match:
//   ACampusBRWaitingArea - a floating fenced slab at the map edge where the lobby gathers.
//   ACampusBRPlane       - flies one straight line across the map at altitude, carrying the drops.
//                          Blender transport with three LODs, event liveries and a rigid rear ramp;
//                          six mesh components and no physics simulation.
//   ACampusBRDrop        - ONE diver's deployment. Owned by that diver's controller so the phone can
//                          send its JUMP, and it is the only home of the dive state: the plane keeps
//                          a list of drops, never a second copy of their state.
//   ACampusBRChute       - the canopy, PURELY cosmetic. The drop is relevant to its owner only (the
//                          diver's own phone), so the thing other players must see cannot live on it:
//                          the chute is its own small replicated actor that the drop spawns when the
//                          canopy opens and destroys on landing.
//
// The drop applies the same velocity clamps on the server and on the owning phone, so the predicted
// fall and the authoritative one agree and the movement component never has to correct a diver.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusBRPlane.generated.h"

class ACampusCharacter;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

/**
 * The imported canopy: /Game/CampusVehicles/Parachute/SM_Parachute, built by Scripts/import_parachute.py
 * out of the downloaded "Parachute Simple" model (CC-BY-4.0 by sxnneh - see CREDITS.md, and keep that
 * entry as long as this asset ships).
 *
 * Shared with the five decorative jumpers in CampusBRDecorChutes.cpp, so the sky never holds two
 * different parachutes. Null when the asset is missing from the cook: both the diver's canopy and the
 * decorative ones then fall back to the dome-and-cone primitives they were built out of, which is why
 * those components are still here.
 */
namespace CampusChuteModel
{
	/** The mesh, or null when it did not load. FROM A CONSTRUCTOR ONLY (it is a ConstructorHelpers
	 *  find, which is also what puts the asset on the class default object for the cooker). */
	UStaticMesh* Mesh();

	/** Its unlit, two-sided material instance, or null. Constructor only, same reason. */
	UMaterialInterface* Cloth();

	/** Where to hang the model on a wearer: the file's origin is the harness hook under the risers, so
	 *  this drops the hook at the shoulders (ShoulderRise above the component's own parent), centres
	 *  the canopy over the head, and scales the 7.1 m model down to a 5 m span. */
	FTransform OverShoulders(float ShoulderRise);
}

UENUM()
enum class ECampusBRDive : uint8
{
	/** Landed (or never dropped). Kept for a grace period so landing cannot be charged as fall damage. */
	Done,
	InPlane,
	Freefall,
	Parachute
};

/**
 * The open canopy over one diver: the imported parachute model (CampusChuteModel) hung off the
 * shoulders, or - when that asset is missing - the primitives it replaced, an arched canopy (a
 * flattened dome with a cone ridge over it) and four rigging lines down to the shoulders.
 * No gameplay - the dive state stays in ACampusBRDrop, which
 * owns this actor's whole life. It exists only because the drop itself is owner-relevant and a
 * parachute is not: this is what the other forty-nine players see coming down.
 *
 * It ticks for ONE thing: the 0.6 s inflation, run from the local clock on every machine.
 */
UCLASS()
class ACampusBRChute : public AActor
{
	GENERATED_BODY()

public:
	ACampusBRChute();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: hang a canopy over this diver. Null when there is nothing to hang it on. */
	static ACampusBRChute* Deploy(ACampusCharacter* Diver);

private:
	UFUNCTION()
	void OnRep_Wearer();

	/** Attach to the diver's shoulders on whichever machine we are. Safe to call twice. Null hides the
	 *  canopy: a far diver's pawn is out of relevancy range, and a canopy with nothing under it would
	 *  hang in the sky at the altitude it opened. */
	void HangOn(ACampusCharacter* Who);

	/** The inflation, 0..1 over OpenSeconds from the first frame this machine saw the canopy. */
	void Inflate(float Alpha);

	/** Who is under it. Every machine attaches itself from this, rather than trusting attachment
	 *  replication to arrive in the same bunch as the spawn. */
	UPROPERTY(ReplicatedUsing = OnRep_Wearer)
	TObjectPtr<ACampusCharacter> Wearer = nullptr;

	/** Seconds since this machine first drew the canopy; -1 until it has a wearer to hang on. */
	float OpenAge = -1.f;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> Harness;

	/** The imported parachute: one mesh, one material, one draw call, and the only thing drawn when the
	 *  asset is in the cook. The six components below are then hidden for good. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CanopyModel;

	/** Fallback canopy, drawn only when CampusChuteModel::Mesh() is null. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Canopy;

	/** The arch: a cone over the dome, so the canopy is a parachute from the side and not an egg. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> CanopyRidge;

	/** Four rigging lines, one per diagonal. Named rather than an array because a component UPROPERTY
	 *  has to be fixed up when the class default object is instanced, and the whole file declares its
	 *  components one at a time. */
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineFrontLeft;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineBackLeft;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineBackRight;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> LineFrontRight;
};

UCLASS()
class ACampusBRDrop : public AActor
{
	GENERATED_BODY()

public:
	ACampusBRDrop();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Server: take this character out of the world and into the plane. */
	void Board(ACampusCharacter* Diver, ACampusBRPlane* InPlane);

	/** Server: leave the plane now (the button, or the auto-eject at the end of the line). */
	void Drop();

	/** Server: open the chute (the button, or automatically near the ground). */
	void OpenChute();

	ECampusBRDive GetState() const { return State; }
	ACampusCharacter* GetDiver() const { return Diver; }
	bool IsDeploying() const { return State != ECampusBRDive::Done; }

	/** Aboard AND the plane is over the map square. Before that a jump would land you outside the
	 *  first circle, which is a death sentence dressed up as a choice. The UI asks this. */
	bool CanDropNow() const;

	/** JUMP on the phone: drops from the plane, then opens the chute. True when it was used for that. */
	bool HandleJumpPressed();

	/** Centimetres above the ground straight below (GroundTraceLength when nothing is there). */
	float HeightAboveGround() const;

private:
	UFUNCTION(Server, Reliable)
	void ServerJump();

	UFUNCTION()
	void OnRep_Dive();

	/** The velocity clamps of the current state. Runs on the server AND on the owning phone. */
	void ApplyDiveMovement();
	/** Remembers the movement values this dive is about to overwrite. Runs on BOTH machines, because
	 *  both of them overwrite their own copy and both have to put it back (see RestoreMovement). */
	void CaptureMovement();
	/** Puts the movement component back exactly as it was before the drop. */
	void RestoreMovement();
	/** Server: feet on the ground - restore, drop the canopy, and start the fall-damage grace. */
	void Land();
	/** Server: take the canopy away (landed, died, or the drop is going). */
	void CutChute();

	UPROPERTY(ReplicatedUsing = OnRep_Dive)
	ECampusBRDive State = ECampusBRDive::Done;

	UPROPERTY(ReplicatedUsing = OnRep_Dive)
	TObjectPtr<ACampusCharacter> Diver = nullptr;

	UPROPERTY(Replicated)
	TObjectPtr<ACampusBRPlane> Plane = nullptr;

	/** Server: the canopy everyone can see, alive only while State is Parachute. */
	UPROPERTY(Transient)
	TObjectPtr<ACampusBRChute> Chute = nullptr;

	/** Movement values captured at Board, restored on landing. */
	float SavedGravityScale = 1.f;
	float SavedAirControl = 0.05f;
	float SavedMaxAcceleration = 2048.f;
	float SavedLateralFriction = 0.f;
	bool bSaved = false;
	/** Server: world time the dive finished; the actor lingers so the landing frame cannot be taxed. */
	float DoneTime = -1.f;
};

UCLASS()
class ACampusBRPlane : public AActor
{
	GENERATED_BODY()

public:
	ACampusBRPlane();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The plane of this world, or null when nothing is flying. */
	static ACampusBRPlane* Get(const UWorld* World);

	/** Still in the plane, falling, or under the chute: no zone damage, no fall damage. */
	static bool IsDeploying(const ACampusCharacter* Character);
	/** The landing of a drop is never fall damage, including the frame it touches down. */
	static bool IsFallDamageSuppressed(const ACampusCharacter* Character);
	/** Jump hook for ACampusCharacter::JumpPressed. True when the press was the drop or the chute. */
	static bool HandleJumpPressed(ACampusCharacter* Character);
	/** The drop of this character, or null. */
	static ACampusBRDrop* FindDrop(const ACampusCharacter* Character);

	/** Server: pick a random straight line across the map and start flying it. */
	void Launch();

	/** Server: put a character on board (spawns its ACampusBRDrop). */
	ACampusBRDrop* Board(ACampusCharacter* Diver);

	/** Server: nobody is left to drop (everyone jumped or was ejected). */
	bool IsEmpty() const;

	/** Where a passenger sits (and leaves from) this frame: under the belly, so the ride looks out at
	 *  the map with the aircraft overhead and the exit is a step down, not a pop through the hull. */
	FVector SeatLocation() const;

	/** 0 at the start of the line, 1 at the end. Clamped: the plane flies on past 1, the ride does not. */
	float FlightAlpha() const;

	/** The plane is inside the map square right now - the window in which jumping is allowed. */
	bool IsOverMap() const;

	// ---- the flight route, for the maps (SCampusTouchControls::DrawFlightRoute). All three are
	// derived from the REPLICATED endpoints and clock, so every client draws the same line.
	/** Launch has handed this plane a line: before that there is no route to draw. */
	bool HasRoute() const { return LaunchTime > 0.f && FlightSeconds > 0.f && !FVector(Start).Equals(FVector(Finish)); }
	/** Where the line begins and ends, world cm. Both sit OUTSIDE the map square (LineOvershoot). */
	FVector GetRouteStart() const { return FVector(Start); }
	FVector GetRouteFinish() const { return FVector(Finish); }
	/** Where the aircraft is this frame, unclamped - it flies on past the end of the line and leaves. */
	FVector GetRouteLocation() const { return FlightLocation(); }

	/** Server/event Blueprint: choose a cooked material instance; replicated to every passenger. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Plane|Appearance")
	void SetEventLivery(UMaterialInterface* Material);

private:
	UFUNCTION()
	void OnRep_EventLivery();
	UPROPERTY(EditDefaultsOnly, ReplicatedUsing=OnRep_EventLivery, Category="Plane|Appearance")
	TObjectPtr<UMaterialInterface> EventLivery;

	/** Server: everyone still aboard leaves now. */
	void EjectAll();

	/** Where the aircraft itself is, UNCLAMPED, so it keeps going and leaves instead of parking in the
	 *  sky at the end of the line. Derived from the replicated endpoints and clock on every machine. */
	FVector FlightLocation() const;
	/** Nose along the line, from the replicated endpoints: a client must not depend on having caught
	 *  the rotation the server set in Launch. */
	FQuat Heading() const;
	/** Local cosmetic: the two engines turn. Not run on a dedicated server. */
	void SpinPropellers(float DeltaSeconds);

	/** Unscaled root, so a passenger attached to it inherits nothing: every part hangs off this. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> Hull;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Fuselage;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Nose;








	/** Two crossed blades per engine, spun about the flight axis in Tick. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> BladeLeftA;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> BladeLeftB;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> BladeRightA;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> BladeRightB;

	UPROPERTY(Replicated)
	FVector_NetQuantize Start = FVector_NetQuantize();

	UPROPERTY(Replicated)
	FVector_NetQuantize Finish = FVector_NetQuantize();

	/** Server world time the line started, and how long it takes. */
	UPROPERTY(Replicated)
	float LaunchTime = 0.f;

	UPROPERTY(Replicated)
	float FlightSeconds = 45.f;

	/** Server only: the divers still aboard plus the ones falling (so the plane can tell when it is done). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACampusBRDrop>> Drops;

	/** Server: the line has been inside the map square at least once, so leaving it now means the far
	 *  edge and not the run-in. Without this the forced eject would fire on the first frame. */
	bool bEnteredMap = false;

	/** Propeller angle, degrees. Cosmetic and local: never replicated, never read by gameplay. */
	float PropAngle = 0.f;
};

UCLASS()
class ACampusBRWaitingArea : public AActor
{
	GENERATED_BODY()

public:
	ACampusBRWaitingArea();

	/** The Nth cell of the slab's grid, facing the campus. The scatter inside a cell is derived from
	 *  Index and never changes, because FreeSlot tests a cell for clearance and then hands out that
	 *  exact transform - the two have to agree. */
	FTransform SlotFor(int32 Index) const;

	/** A RANDOM FREE CELL, for a player who has just arrived while the session is still filling. Cells
	 *  are tried in a random order and one with a pawn already standing inside 1.5 m is skipped, so
	 *  fifty arrivals spread over the slab instead of stacking on whoever joined first. */
	FTransform FreeSlot() const;

	/** Cells on the slab. More than fifty, so the last of the fifty still has a choice. */
	static int32 SlotCount();

	/** The slab's own place: a corner of THIS LEVEL's map square (CampusBRMap::For), high above
	 *  anything that can be shot. The world is an argument because the square is per level - the BR
	 *  map is 800 m and the college room map is 227 m, and a slab placed off the college numbers
	 *  landed a tenth of the way across Varsity Valley. */
	static FVector Place(const UWorld* World);

private:
	/** No pawn standing on the slab within the design's 1.5 m of this spot. */
	bool IsSpotClear(const FVector& Where) const;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Slab;
};
