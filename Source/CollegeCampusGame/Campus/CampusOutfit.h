// PUTTING A COSMETIC INTO THE SCENE. Two jobs, one file, because they are the same job twice:
//
//   * CampusOutfit::Apply   - dresses ONE skeletal mesh component in an outfit row. The lobby
//                             line-up, the wardrobe figure and ACampusCharacter all go through here,
//                             so there is no second place that knows how an outfit is worn.
//   * ACampusLobbyDecor     - the eight fixed lobby anchors, built from the catalogue's recipes.
//
// Both are DISPLAY ONLY: no collision, no replication, local to the machine drawing them. The outfit
// a player wears in a MATCH replicates, but it replicates as an id on ACampusCharacter - what you see
// below is only the part that turns that id into pixels.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusOutfit.generated.h"

class UMaterialInterface;
class USkeletalMeshComponent;
class UStaticMeshComponent;

namespace CampusOutfit
{
	/**
	 * Dresses this body in OutfitId: the catalogue's skeletal mesh, then the tinted body material.
	 *
	 * Safe on a null component and on an unknown id (which is the starter outfit - CampusCosmetics
	 * never hands back a null row). Cheap enough to call on every equip tap: the mesh is only
	 * re-assigned when it actually changes, and the dynamic material instance is re-used rather than
	 * rebuilt when its parent already matches.
	 *
	 * THE ANIM GRAPH SURVIVES IT. Re-assigning a skeletal mesh re-initialises the component's anim
	 * instance, so the class the component was already running is read first and put back afterwards
	 * if the swap dropped it. That keeps the native UCampusAnimInstance driving both the lobby figures
	 * and the gameplay character without this file having to know either of them exists.
	 *
	 * ONE SKELETON. A row whose mesh is authored on a different skeleton than the body already wears
	 * is REFUSED (logged as a Warning) rather than swapped in: the anim graph cannot drive it and the
	 * player would stand in a reference pose for the rest of the match.
	 *
	 * A DEDICATED SERVER stops after the mesh. It draws nothing, so the body material, the dynamic
	 * instances and their parameters are all work with no observer.
	 */
	void Apply(USkeletalMeshComponent* Body, FName OutfitId);

	/**
	 * Pulls every asset the catalogue can ask for into memory and HOLDS it there (strong, GC-rooted,
	 * for the lifetime of the process). Latched: the second call and every call after it is free.
	 *
	 * WHY IT EXISTS. Apply() runs from ACampusCharacter::OnRep_Outfit, which is the network thread's
	 * frame - a synchronous LoadObject there is a disk hitch in the middle of a firefight, once per
	 * remote player who is wearing something this machine has not seen yet. Paying for all four rows
	 * at level load instead costs one hitch nobody is looking at. The catalogue is four skeletal
	 * meshes, two materials and four engine primitives, so there is nothing here worth streaming.
	 *
	 * Call it at level load (ACampusCharacter::BeginPlay does, before its first ApplyOutfit). Apply()
	 * and ACampusLobbyDecor still work without it - they fall back to a direct load and cache it.
	 */
	void Preload();
}

/** What is standing on ONE anchor, and the components that make it. */
USTRUCT()
struct FCampusDecorAnchor
{
	GENERATED_BODY()

	/** The decor item built here, NAME_None when the anchor is bare. The diff key. */
	UPROPERTY(Transient)
	FName ItemId;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;
};

/**
 * THE LOBBY'S DECOR SET. One actor owning one static mesh component per primitive of per occupied
 * anchor.
 *
 * PER-ANCHOR, NOT WHOLE-SET. Rebuild diffs the map it is handed against what is already standing and
 * touches only the anchors whose item actually moved. The player changes ONE anchor at a time from the
 * decor sheet, and tearing down all eight props to put one up re-registered up to forty components and
 * threw away every dynamic material instance on a frame the player is watching. A changed floor height
 * is the one thing that still rebuilds everything, because every anchor's Z is measured from it.
 *
 * Spawned by ACampusLobbyPlayerController after it has read the floor height, because every anchor's
 * Z is measured up from that floor.
 */
UCLASS()
class ACampusLobbyDecor : public AActor
{
	GENERATED_BODY()

public:
	ACampusLobbyDecor();

	/**
	 * Re-dresses the lobby. Decor maps an anchor id (CampusCosmetics::FAnchor::Id) to a decor item id;
	 * a missing or unknown entry leaves that anchor bare. FloorZ is the lobby floor's world height
	 * (ACampusLobbyPlayerController::ReadFloorHeight), which every anchor's Z is relative to.
	 */
	void Rebuild(const TMap<FName, FName>& Decor, float FloorZ);

private:
	/** Destroys every component of the current set. */
	void Clear();
	/** Destroys just this anchor's components and marks it bare. */
	void ClearAnchor(int32 AnchorIndex);
	/** Stands ItemId on this anchor. Assumes the anchor has already been cleared. */
	void BuildAnchor(int32 AnchorIndex, FName ItemId, float FloorZ, UMaterialInterface* Base);

	/** CampusCosmetics::NumAnchors entries, sized on the first Rebuild. */
	UPROPERTY(Transient)
	TArray<FCampusDecorAnchor> Anchors;

	/** The floor the current set was measured from. A move re-stands every prop. */
	float BuiltFloorZ = 0.f;
	bool bBuilt = false;
};
