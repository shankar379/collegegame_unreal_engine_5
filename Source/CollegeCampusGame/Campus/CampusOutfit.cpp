#include "Campus/CampusOutfit.h"

#include "Campus/CampusCosmetics.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	/** The basic shapes are 100 cm across, so a size in cm is that size over a hundred. */
	constexpr float ShapeSizeCm = 100.f;

	/**
	 * THE ASSET CACHE. Path -> the loaded object, held strongly so the GC cannot take it back between
	 * two OnRep_Outfit frames and make the second one pay for the disk again.
	 *
	 * TStrongObjectPtr and not a raw UObject*: this map outlives any actor and nothing else references
	 * what is in it, so a raw pointer here would be collected and then dereferenced.
	 */
	TMap<FString, TStrongObjectPtr<UObject>>& AssetCache()
	{
		static TMap<FString, TStrongObjectPtr<UObject>> Cache;
		return Cache;
	}

	/**
	 * The cached object at Path, loading it once if CampusOutfit::Preload has not already. Null is a
	 * real answer (the asset is missing from this build's cook) and every caller handles it.
	 *
	 * A FAILED LOAD IS CACHED TOO, as an empty entry: a path that is not in the package will not become
	 * one, and re-trying it on every repaint is a stall per frame rather than a stall once.
	 */
	template <typename T>
	T* Loaded(const TCHAR* Path)
	{
		if (!Path || !*Path)
		{
			return nullptr;
		}
		const FString Key(Path);
		if (const TStrongObjectPtr<UObject>* Found = AssetCache().Find(Key))
		{
			return Cast<T>(Found->Get());
		}
		T* Object = LoadObject<T>(nullptr, Path);
		AssetCache().Add(Key, TStrongObjectPtr<UObject>(Object));
		return Object;
	}

	/**
	 * A dynamic instance of Base on this slot, re-used when there already is one with that parent.
	 * Null when the material could not be loaded, which the callers read as "leave it alone".
	 */
	UMaterialInstanceDynamic* SlotMid(UMeshComponent* Component, int32 Slot, UMaterialInterface* Base)
	{
		if (!Component || !Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Slot));
		if (Existing && Existing->Parent == Base)
		{
			return Existing;
		}
		return Component->CreateAndSetMaterialInstanceDynamicFromMaterial(Slot, Base);
	}
}

void CampusOutfit::Preload()
{
	static bool bPreloaded = false;
	if (bPreloaded)
	{
		return;
	}
	bPreloaded = true;

	// Every body the wardrobe can put on. ALSO ON A DEDICATED SERVER: SetSkeletalMeshAsset runs there
	// too (the host owns the bounds and the sockets the gun hangs off), it is only the SKIN below that
	// has no observer.
	int32 NumOutfits = 0;
	const CampusCosmetics::FOutfitEntry* Outfits = CampusCosmetics::OutfitTable(NumOutfits);
	for (int32 Index = 0; Index < NumOutfits; ++Index)
	{
		if (!Loaded<USkeletalMesh>(Outfits[Index].Mesh))
		{
			UE_LOG(LogTemp, Warning, TEXT("[CampusOutfit] outfit '%s' mesh did not load: %s"),
				Outfits[Index].Id, Outfits[Index].Mesh);
		}
	}
	if (IsRunningDedicatedServer())
	{
		return;
	}
	if (!Loaded<UMaterialInterface>(CampusCosmetics::BodyMaterialPath()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CampusOutfit] the tinted body material did not load: %s"),
			CampusCosmetics::BodyMaterialPath());
	}
	// The lobby's props. Four engine primitives and one material between all six recipes, so the whole
	// decor table costs less than one outfit.
	Loaded<UMaterialInterface>(CampusCosmetics::ShapeMaterialPath());
	int32 NumDecor = 0;
	const CampusCosmetics::FDecorEntry* Decor = CampusCosmetics::DecorTable(NumDecor);
	for (int32 Index = 0; Index < NumDecor; ++Index)
	{
		for (const CampusCosmetics::FDecorPart& Part : Decor[Index].Parts)
		{
			if (Part.Mesh && !Loaded<UStaticMesh>(Part.Mesh))
			{
				UE_LOG(LogTemp, Warning, TEXT("[CampusOutfit] decor '%s' part mesh did not load: %s"),
					Decor[Index].Id, Part.Mesh);
			}
		}
	}
}

void CampusOutfit::Apply(USkeletalMeshComponent* Body, FName OutfitId)
{
	if (!Body)
	{
		return;
	}
	const CampusCosmetics::FOutfitEntry& Entry = CampusCosmetics::OutfitFor(OutfitId);

	// ---- the body itself.
	// READ THE ANIM CLASS FIRST. SetSkeletalMeshAsset re-initialises the component's animation, and the
	// class it was running is the only honest record of what it should go back to - hard-coding
	// UCampusAnimInstance here would make this file a second owner of that decision and would be
	// wrong the first time something else uses it.
	//
	// NO SYNCHRONOUS DISK HERE on any machine that called CampusOutfit::Preload (every level that can
	// run OnRep_Outfit does, from ACampusCharacter::BeginPlay): this is a map lookup.
	USkeletalMesh* Wanted = Loaded<USkeletalMesh>(Entry.Mesh);
	if (!Wanted)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CampusOutfit] outfit '%s' has no mesh in this build (%s); the body keeps what it wears."),
			Entry.Id, Entry.Mesh);
	}
	else if (Body->GetSkeletalMeshAsset() != Wanted)
	{
		// ONE SKELETON, AND IT IS CHECKED. Every row in the catalogue is authored on SK_UEFN_Mannequin,
		// which is the whole reason the native UCampusAnimInstance survives a swap. A mesh on a
		// different skeleton re-initialises the component onto an anim graph that cannot drive it and
		// the player stands in a reference pose for the rest of the match - a worse outcome than
		// wearing the wrong shirt, so it is refused rather than attempted.
		const USkeletalMesh* Current = Body->GetSkeletalMeshAsset();
		if (Current && Current->GetSkeleton() != Wanted->GetSkeleton())
		{
			UE_LOG(LogTemp, Warning, TEXT("[CampusOutfit] outfit '%s' is authored on a different skeleton than the body already wears; refusing the swap."),
				Entry.Id);
		}
		else
		{
			const EAnimationMode::Type Mode = Body->GetAnimationMode();
			UClass* AnimClass = Body->GetAnimClass();
			Body->SetSkeletalMeshAsset(Wanted);
			if (AnimClass && Body->GetAnimClass() != AnimClass)
			{
				Body->SetAnimationMode(Mode);
				Body->SetAnimInstanceClass(AnimClass);
			}
			// THE ONLY HONEST TEST THAT THE ANIMATION CAME BACK. GetAnimClass() agreeing proves the
			// class pointer was stored, not that an instance was built behind it - and a component with
			// a class and no instance is a body frozen in its reference pose, which is exactly the
			// failure this whole block exists to avoid. One re-init, then say so and move on.
			if (Body->GetAnimClass() && !Body->GetAnimInstance())
			{
				Body->InitAnim(false);
				if (!Body->GetAnimInstance())
				{
					UE_LOG(LogTemp, Warning, TEXT("[CampusOutfit] outfit '%s': the anim instance did not initialise after the mesh swap; this body will not animate."),
						Entry.Id);
				}
			}
		}
	}

	// A DEDICATED SERVER STOPS HERE. It draws nothing, so the material, its dynamic instances and all
	// twelve parameter writes below are work with no observer - and loading the material at all would
	// put a cooked texture chain in a headless process's memory for nobody.
	if (IsRunningDedicatedServer())
	{
		return;
	}

	// ---- the skin.
	// The starter look is the mesh's OWN authored material, so clearing the overrides is the whole of
	// it; a flat tint over that texture would be a downgrade dressed up as a cosmetic.
	if (Entry.bKeepMeshMaterial)
	{
		Body->EmptyOverrideMaterials();
		return;
	}
	UMaterialInterface* Base = Loaded<UMaterialInterface>(CampusCosmetics::BodyMaterialPath());
	if (!Base)
	{
		// No tint material cooked into this build: the authored look is a better answer than a blank
		// body, so leave whatever the mesh already wears.
		Body->EmptyOverrideMaterials();
		return;
	}
	// EVERY slot, with the same skin. The mannequin has exactly one (verified against both
	// SKM_CampusMannequin and SKM_UEFN_Mannequin), and an outfit mesh that arrives with more should
	// still come out dressed rather than half grey.
	const int32 NumSlots = Body->GetNumMaterials();
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		UMaterialInstanceDynamic* Mid = SlotMid(Body, Slot, Base);
		if (!Mid)
		{
			continue;
		}
		Mid->SetVectorParameterValue(CampusCosmetics::ParamPrimaryColor(), Entry.Primary);
		Mid->SetVectorParameterValue(CampusCosmetics::ParamSecondaryColor(), Entry.Secondary);
		Mid->SetScalarParameterValue(CampusCosmetics::ParamPrimaryRough(), Entry.Roughness);
		Mid->SetScalarParameterValue(CampusCosmetics::ParamSecondaryRough(), Entry.Roughness);
		Mid->SetScalarParameterValue(CampusCosmetics::ParamPrimaryMetallic(), Entry.Metallic);
		Mid->SetScalarParameterValue(CampusCosmetics::ParamSecondaryMetallic(), Entry.Metallic);
	}
}

// -------------------------------------------------------------------------------------------------
// The lobby decor set

ACampusLobbyDecor::ACampusLobbyDecor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	// A root of its own, so the props can be attached and moved as one set.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
	SetActorEnableCollision(false);
}

void ACampusLobbyDecor::Clear()
{
	for (int32 Index = 0; Index < Anchors.Num(); ++Index)
	{
		ClearAnchor(Index);
	}
}

void ACampusLobbyDecor::ClearAnchor(int32 AnchorIndex)
{
	if (!Anchors.IsValidIndex(AnchorIndex))
	{
		return;
	}
	FCampusDecorAnchor& Slot = Anchors[AnchorIndex];
	for (const TObjectPtr<UStaticMeshComponent>& Part : Slot.Parts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	Slot.Parts.Reset();
	Slot.ItemId = NAME_None;
}

void ACampusLobbyDecor::BuildAnchor(int32 AnchorIndex, FName ItemId, float FloorZ, UMaterialInterface* Base)
{
	if (!Anchors.IsValidIndex(AnchorIndex) || ItemId.IsNone())
	{
		return;
	}
	const CampusCosmetics::FDecorEntry* Entry = CampusCosmetics::DecorFor(ItemId);
	if (!Entry || !CampusCosmetics::DecorAllowedAt(*Entry, AnchorIndex))
	{
		// An id this build does not know, or one the server put somewhere it is not allowed: the
		// anchor stays bare rather than the lobby showing a prop across somebody's face.
		return;
	}
	const CampusCosmetics::FAnchor& Anchor = CampusCosmetics::Anchor(AnchorIndex);
	// A HANGING anchor holds the prop's TOP, so the whole recipe drops by its own height. Every
	// other anchor holds its base.
	const float BaseZ = FloorZ + Anchor.Z - (Anchor.bHang ? CampusCosmetics::DecorHeight(*Entry) : 0.f);
	const FTransform AnchorToWorld(FRotator(0.f, Anchor.Yaw, 0.f), FVector(Anchor.X, Anchor.Y, BaseZ));

	FCampusDecorAnchor& Slot = Anchors[AnchorIndex];
	for (const CampusCosmetics::FDecorPart& Part : Entry->Parts)
	{
		if (!Part.Mesh)
		{
			continue;
		}
		UStaticMesh* Mesh = Loaded<UStaticMesh>(Part.Mesh);
		if (!Mesh)
		{
			// THE COOK IS THE USUAL CAUSE: /Engine/BasicShapes is reachable only by this path, so
			// Config/DefaultGame.ini has to name it in DirectoriesToAlwaysCook. Say so out loud -
			// this used to be a silent `continue` and a lobby with nothing in it.
			UE_LOG(LogTemp, Warning, TEXT("[CampusOutfit] decor '%s' part mesh did not load: %s (is it in DirectoriesToAlwaysCook?)"),
				*ItemId.ToString(), Part.Mesh);
			continue;
		}
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		if (!Component)
		{
			continue;
		}
		Component->SetupAttachment(RootComponent);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		// Display only. A prop with collision would be something the line-up's floor trace could
		// land on, and something a player could be pushed through in the match levels this actor
		// is deliberately never spawned in.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		// AND IT CASTS NOTHING. Up to forty little primitives stood behind the line-up, each adding
		// itself to the lobby light's shadow pass, for shadows that fall on a floor the camera barely
		// sees. The props are read as silhouettes against the backdrop, not by their shadows.
		Component->SetCastShadow(false);
		Component->RegisterComponent();

		const FTransform PartToAnchor(FRotator(Part.Pitch, Part.Yaw, 0.f), FVector(Part.X, Part.Y, Part.Z),
			FVector(Part.SizeX, Part.SizeY, Part.SizeZ) / ShapeSizeCm);
		Component->SetWorldTransform(PartToAnchor * AnchorToWorld);

		if (UMaterialInstanceDynamic* Mid = SlotMid(Component, 0, Base))
		{
			Mid->SetVectorParameterValue(CampusCosmetics::ParamShapeColor(), Part.Color);
		}
		Slot.Parts.Add(Component);
	}
	Slot.ItemId = ItemId;
}

void ACampusLobbyDecor::Rebuild(const TMap<FName, FName>& Decor, float FloorZ)
{
	if (Anchors.Num() != CampusCosmetics::NumAnchors)
	{
		Anchors.SetNum(CampusCosmetics::NumAnchors);
	}
	// THE FLOOR MOVED, so every anchor's Z did: nothing standing can be kept. This is the lobby being
	// entered, not the player editing it, so the cost is paid where nobody is watching for it.
	const bool bAll = !bBuilt || !FMath::IsNearlyEqual(BuiltFloorZ, FloorZ);
	if (bAll)
	{
		Clear();
	}
	bBuilt = true;
	BuiltFloorZ = FloorZ;

	// Loaded once for the whole pass, and only if there is actually something to stand up.
	UMaterialInterface* Base = nullptr;
	bool bBaseAsked = false;

	for (int32 Index = 0; Index < CampusCosmetics::NumAnchors; ++Index)
	{
		const FName* Found = Decor.Find(FName(CampusCosmetics::Anchor(Index).Id));
		const FName WantedId = Found ? *Found : NAME_None;
		// THE DIFF, and the whole reason this is not a whole-set rebuild: the decor sheet changes one
		// anchor per tap, and re-standing the other seven threw away their components and their dynamic
		// material instances for a prop that did not move.
		if (!bAll && Anchors[Index].ItemId == WantedId)
		{
			continue;
		}
		ClearAnchor(Index);
		if (WantedId.IsNone())
		{
			continue;
		}
		if (!bBaseAsked)
		{
			bBaseAsked = true;
			Base = Loaded<UMaterialInterface>(CampusCosmetics::ShapeMaterialPath());
			if (!Base)
			{
				UE_LOG(LogTemp, Warning, TEXT("[CampusOutfit] the basic-shape material did not load: %s (is it in DirectoriesToAlwaysCook?)"),
					CampusCosmetics::ShapeMaterialPath());
			}
		}
		BuildAnchor(Index, WantedId, FloorZ, Base);
	}
}
