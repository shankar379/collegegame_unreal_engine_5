#include "Campus/CampusOutfit.h"

#include "Campus/CampusCosmetics.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	/** The basic shapes are 100 cm across, so a size in cm is that size over a hundred. */
	constexpr float ShapeSizeCm = 100.f;

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
	USkeletalMesh* Wanted = LoadObject<USkeletalMesh>(nullptr, Entry.Mesh);
	if (Wanted && Body->GetSkeletalMeshAsset() != Wanted)
	{
		const EAnimationMode::Type Mode = Body->GetAnimationMode();
		UClass* AnimClass = Body->GetAnimClass();
		Body->SetSkeletalMeshAsset(Wanted);
		if (AnimClass && Body->GetAnimClass() != AnimClass)
		{
			Body->SetAnimationMode(Mode);
			Body->SetAnimInstanceClass(AnimClass);
		}
	}

	// ---- the skin.
	// The starter look is the mesh's OWN authored material, so clearing the overrides is the whole of
	// it; a flat tint over that texture would be a downgrade dressed up as a cosmetic.
	if (Entry.bKeepMeshMaterial)
	{
		Body->EmptyOverrideMaterials();
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, CampusCosmetics::BodyMaterialPath());
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
	for (const TObjectPtr<UStaticMeshComponent>& Part : Parts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	Parts.Reset();
}

void ACampusLobbyDecor::Rebuild(const TMap<FName, FName>& Decor, float FloorZ)
{
	Clear();
	UStaticMesh* Loaded = nullptr;
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, CampusCosmetics::ShapeMaterialPath());

	for (int32 Index = 0; Index < CampusCosmetics::NumAnchors; ++Index)
	{
		const CampusCosmetics::FAnchor& Anchor = CampusCosmetics::Anchor(Index);
		const FName* Wanted = Decor.Find(FName(Anchor.Id));
		if (!Wanted || Wanted->IsNone())
		{
			continue;
		}
		const CampusCosmetics::FDecorEntry* Entry = CampusCosmetics::DecorFor(*Wanted);
		if (!Entry || !CampusCosmetics::DecorAllowedAt(*Entry, Index))
		{
			// An id this build does not know, or one the server put somewhere it is not allowed: the
			// anchor stays bare rather than the lobby showing a prop across somebody's face.
			continue;
		}
		// A HANGING anchor holds the prop's TOP, so the whole recipe drops by its own height. Every
		// other anchor holds its base.
		const float BaseZ = FloorZ + Anchor.Z - (Anchor.bHang ? CampusCosmetics::DecorHeight(*Entry) : 0.f);
		const FTransform AnchorToWorld(FRotator(0.f, Anchor.Yaw, 0.f), FVector(Anchor.X, Anchor.Y, BaseZ));

		for (const CampusCosmetics::FDecorPart& Part : Entry->Parts)
		{
			if (!Part.Mesh)
			{
				continue;
			}
			Loaded = LoadObject<UStaticMesh>(nullptr, Part.Mesh);
			if (!Loaded)
			{
				continue;
			}
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
			if (!Component)
			{
				continue;
			}
			Component->SetupAttachment(RootComponent);
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetStaticMesh(Loaded);
			// Display only. A prop with collision would be something the line-up's floor trace could
			// land on, and something a player could be pushed through in the match levels this actor
			// is deliberately never spawned in.
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCanEverAffectNavigation(false);
			Component->RegisterComponent();

			const FTransform PartToAnchor(FRotator(Part.Pitch, Part.Yaw, 0.f), FVector(Part.X, Part.Y, Part.Z),
				FVector(Part.SizeX, Part.SizeY, Part.SizeZ) / ShapeSizeCm);
			Component->SetWorldTransform(PartToAnchor * AnchorToWorld);

			if (UMaterialInstanceDynamic* Mid = SlotMid(Component, 0, Base))
			{
				Mid->SetVectorParameterValue(CampusCosmetics::ParamShapeColor(), Part.Color);
			}
			Parts.Add(Component);
		}
	}
}
