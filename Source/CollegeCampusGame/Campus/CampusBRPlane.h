// Battle royale deployment: the fenced waiting platform, the plane that crosses the map, and the
// freefall / parachute drop itself.
//
// Three actors, one phase of the match:
//   ACampusBRWaitingArea - a floating fenced slab at the map edge where the lobby gathers.
//   ACampusBRPlane       - flies one straight line across the map at altitude, carrying the drops.
//   ACampusBRDrop        - ONE diver's deployment. Owned by that diver's controller so the phone can
//                          send its JUMP, and it is the only home of the dive state: the plane keeps
//                          a list of drops, never a second copy of their state.
//
// The drop applies the same velocity clamps on the server and on the owning phone, so the predicted
// fall and the authoritative one agree and the movement component never has to correct a diver.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusBRPlane.generated.h"

class ACampusCharacter;
class UStaticMeshComponent;

UENUM()
enum class ECampusBRDive : uint8
{
	/** Landed (or never dropped). Kept for a grace period so landing cannot be charged as fall damage. */
	Done,
	InPlane,
	Freefall,
	Parachute
};

UCLASS()
class ACampusBRDrop : public AActor
{
	GENERATED_BODY()

public:
	ACampusBRDrop();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
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

	/** JUMP on the phone: drops from the plane, then opens the chute. True when it was used for that. */
	bool HandleJumpPressed();

	/** Metres above the ground straight below (a long way when nothing is there). */
	float HeightAboveGround() const;

private:
	UFUNCTION(Server, Reliable)
	void ServerJump();

	/** The velocity clamps of the current state. Runs on the server AND on the owning phone. */
	void ApplyDiveMovement();
	/** Puts the movement component back exactly as it was before the drop. */
	void RestoreMovement();

	UPROPERTY(Replicated)
	ECampusBRDive State = ECampusBRDive::Done;

	UPROPERTY(Replicated)
	TObjectPtr<ACampusCharacter> Diver = nullptr;

	UPROPERTY(Replicated)
	TObjectPtr<ACampusBRPlane> Plane = nullptr;

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

	/** Where a passenger sits this frame. */
	FVector SeatLocation() const;

	/** 0 at the start of the line, 1 at the end. */
	float FlightAlpha() const;

private:
	/** Server: everyone still aboard leaves now. */
	void EjectAll();

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Body;

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
};

UCLASS()
class ACampusBRWaitingArea : public AActor
{
	GENERATED_BODY()

public:
	ACampusBRWaitingArea();

	/** Spawn transform for the Nth player waiting here (a grid on the slab, all facing the campus). */
	FTransform SlotFor(int32 Index) const;

	/** The slab's own place: a corner of the map square, high above anything that can be shot. */
	static FVector Place();

private:
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Slab;
};
