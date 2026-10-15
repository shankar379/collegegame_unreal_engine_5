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
	 */
	void Apply(USkeletalMeshComponent* Body, FName OutfitId);
}

/**
 * THE LOBBY'S DECOR SET. One actor owning one static mesh component per primitive of per occupied
 * anchor, rebuilt whole whenever the player changes something - six boxes are cheaper to respawn
 * than to diff, and a rebuild cannot leave a stale prop behind.
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

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;
};
