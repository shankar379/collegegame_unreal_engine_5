// Ground loot for battle royale: the things lying on the campus floor and the spawner that scatters them.
//
// ACampusPickup is a small replicated actor (kind + which gun + how much) that bobs and spins on every phone.
// The host alone decides who gets it: the taker's phone asks through ACampusCharacter::PickupPressed (the HUD's
// PICK UP button) or simply walks over an ammo box, and the server calls ServerTake. The actor is destroyed on
// the host and the destruction replicates - no client ever removes loot on its own.
//
// ACampusLootSpawner fills an area once per round: either at actors tagged "LootSpot" inside its bounds, or at
// random points in the bounds traced down onto the ground. Tiers decide how good the guns are (CampusWeapons).
//
// No new art: guns reuse SM_Rifle at the gun's own scale and tint; the rest are engine primitives, tinted.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusLoot.generated.h"

class ACampusCharacter;
class USphereComponent;
class UStaticMeshComponent;

UENUM()
enum class ECampusPickupKind : uint8
{
	/** A gun, identified by WeaponId (a CampusWeapons / backend catalogue id). */
	Weapon,
	/** Rounds for whatever is in hand. */
	Ammo,
	/** Heals over MedkitSeconds; interrupted by nothing, so it is a commitment. */
	Medkit,
	/** Vest: soaks a share of incoming damage until its points are gone. */
	Armor,
	/** Cuts headshot damage. One is enough; a second is refused. */
	Helmet
};

UCLASS()
class ACampusPickup : public AActor
{
	GENERATED_BODY()

public:
	ACampusPickup();

	/** campus.LootEnabled: the whole ground-loot feature's gate (battle royale). */
	static bool LootEnabled();
	/** Every pickup alive in any world; filter by GetWorld(). Avoids a TActorIterator per HUD frame. */
	static const TArray<TWeakObjectPtr<ACampusPickup>>& All();

	/** Server, before BeginPlay: what this box is. WeaponId only matters for Weapon. */
	void Setup(ECampusPickupKind InKind, FName InWeaponId, int32 InAmount);

	ECampusPickupKind GetKind() const { return Kind; }
	FName GetWeaponId() const { return WeaponId; }
	int32 GetAmount() const { return Amount; }
	/** HUD label for the PICK UP button ("AK-47", "AMMO 30", "MEDKIT"). */
	FString Label() const;
	/** Ammo is taken by walking over it; everything else waits for the button, so you never lose a gun by accident. */
	bool IsAutoTaken() const { return Kind == ECampusPickupKind::Ammo; }

	/** Server only: give this to Who and destroy it. False when they cannot use it (full on helmets, say). */
	bool ServerTake(ACampusCharacter* Who);

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** How far a player may be and still take it (cm). The HUD uses the same number to show the button. */
	static constexpr float ReachDistance = 220.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION()
	void OnRep_Kind();

	/** Mesh, scale and tint for the current kind (and gun, for weapons). Runs on every machine. */
	void ApplyLook();

	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere, Category="Loot")
	TObjectPtr<USphereComponent> Reach;

	UPROPERTY(VisibleAnywhere, Category="Loot")
	TObjectPtr<UStaticMeshComponent> Art;

	UPROPERTY(ReplicatedUsing=OnRep_Kind)
	ECampusPickupKind Kind = ECampusPickupKind::Ammo;

	UPROPERTY(ReplicatedUsing=OnRep_Kind)
	FName WeaponId;

	UPROPERTY(Replicated)
	int32 Amount = 30;

	float BobTime = 0.f;
	FVector RestLocation = FVector::ZeroVector;
};

/** Scatters pickups over an area at the start of a round. Place one per loot zone (or one per map). */
UCLASS()
class ACampusLootSpawner : public AActor
{
	GENERATED_BODY()

public:
	ACampusLootSpawner();

	/** Half-size of the box the loot lands in, around this actor (cm). Ignored for tagged spots. */
	UPROPERTY(EditAnywhere, Category="Loot")
	FVector BoundsExtent = FVector(6000.f, 6000.f, 1500.f);

	/** How many pickups this zone holds. Tagged spots cap it at the number of spots. */
	UPROPERTY(EditAnywhere, Category="Loot")
	int32 Count = 24;

	/** Actors with this tag inside the bounds become exact loot spots (a desk, a crate). Empty = random points. */
	UPROPERTY(EditAnywhere, Category="Loot")
	FName SpotTag = TEXT("LootSpot");

	/** Share of the pickups that are guns; the rest are ammo, medkits, vests and helmets. */
	UPROPERTY(EditAnywhere, Category="Loot")
	float WeaponShare = 0.45f;

	/** Gun rarity mix: chance of tier 2 and tier 3; the remainder is tier 1. */
	UPROPERTY(EditAnywhere, Category="Loot")
	float Tier2Chance = 0.3f;

	UPROPERTY(EditAnywhere, Category="Loot")
	float Tier3Chance = 0.12f;

	/** Server only: clears what is left and lays out a fresh set. Called by BeginPlay and by the round reset. */
	void SpawnWave();
	/** Round start: identical to SpawnWave, named for the caller that resets doors and cars the same way. */
	void ResetForRound() { SpawnWave(); }

protected:
	virtual void BeginPlay() override;

private:
	/** Ground point under a candidate location, or false when nothing solid is below it. */
	bool GroundAt(const FVector& Above, FVector& OutPoint) const;
	/** The tagged spots inside the bounds, world space. */
	void CollectTaggedSpots(TArray<FVector>& OutSpots) const;
	void SpawnOne(const FVector& Where);

	/** What this spawner made, so a new wave can clear the old one. */
	TArray<TWeakObjectPtr<ACampusPickup>> Spawned;
};
