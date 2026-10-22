#include "Campus/CampusGunSkins.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"

#define LOCTEXT_NAMESPACE "CampusGunSkins"

namespace CampusGunSkins
{
	namespace
	{
		struct FStyleRow
		{
			const TCHAR* Suffix;
			const TCHAR* Texture;
			// migration 032, column skin_mods
			float DamagePct;
			float FireRatePct;
			float AccuracyPct;
			float ReloadPct;
			float MagazinePct;
			float RangePct;
		};

		// Index = EStyle. Keep in step with migration 032's `styles` table.
		const FStyleRow Rows[] = {
			{ TEXT(""), nullptr, 0, 0, 0, 0, 0, 0 },
			{ TEXT("royal"), TEXT("/Game/Weapons/LibraryV3/Materials/Skins/T_Skin_RoyalGold.T_Skin_RoyalGold"), 0, 12, 10, -6, 0, 0 },
			{ TEXT("neon"), TEXT("/Game/Weapons/LibraryV3/Materials/Skins/T_Skin_NeonHex.T_Skin_NeonHex"), 10, 8, 0, 0, 0, -6 },
			{ TEXT("arctic"), TEXT("/Game/Weapons/LibraryV3/Materials/Skins/T_Skin_ArcticCircuit.T_Skin_ArcticCircuit"), 0, -6, 12, 0, 0, 10 },
			{ TEXT("relic"), TEXT("/Game/Weapons/LibraryV3/Materials/Skins/T_Skin_GoldRelic.T_Skin_GoldRelic"), 8, 0, 0, -8, 12, 0 },
		};
		static_assert(UE_ARRAY_COUNT(Rows) == Count, "one row per style");
	}

	uint8 StyleFor(const FString& SkinId)
	{
		for (uint8 Style = Royal; Style < Count; ++Style)
		{
			if (SkinId.EndsWith(FString(TEXT("_")) + Rows[Style].Suffix))
			{
				return Style;
			}
		}
		return None;
	}

	FString SkinIdFor(FName GunId, uint8 Style)
	{
		if (Style == None || Style >= Count || GunId.IsNone())
		{
			return FString();
		}
		return FString::Printf(TEXT("skin_%s_%s"), *GunId.ToString().ToLower(), Rows[Style].Suffix);
	}

	FText Label(uint8 Style)
	{
		switch (Style)
		{
		case Royal:  return LOCTEXT("Royal", "Royal");
		case Neon:   return LOCTEXT("Neon", "Neon Hex");
		case Arctic: return LOCTEXT("Arctic", "Arctic Circuit");
		case Relic:  return LOCTEXT("Relic", "Gold Relic");
		default:     return LOCTEXT("Factory", "Factory");
		}
	}

	const TCHAR* TextureFor(uint8 Style)
	{
		return Style < Count ? Rows[Style].Texture : nullptr;
	}

	FCampusWeaponMods ModsFor(uint8 Style)
	{
		FCampusWeaponMods Out;
		if (Style == None || Style >= Count)
		{
			return Out;
		}
		const FStyleRow& Row = Rows[Style];
		Out.DamagePct = Row.DamagePct;
		Out.FireRatePct = Row.FireRatePct;
		Out.AccuracyPct = Row.AccuracyPct;
		Out.ReloadPct = Row.ReloadPct;
		Out.MagazinePct = Row.MagazinePct;
		Out.RangePct = Row.RangePct;
		return Out;
	}

	FCampusWeaponMods Add(const FCampusWeaponMods& A, const FCampusWeaponMods& B)
	{
		FCampusWeaponMods Out;
		Out.DamagePct = A.DamagePct + B.DamagePct;
		Out.FireRatePct = A.FireRatePct + B.FireRatePct;
		Out.AccuracyPct = A.AccuracyPct + B.AccuracyPct;
		Out.ReloadPct = A.ReloadPct + B.ReloadPct;
		Out.MagazinePct = A.MagazinePct + B.MagazinePct;
		Out.RangePct = A.RangePct + B.RangePct;
		Out.RecoilPct = A.RecoilPct + B.RecoilPct;
		return Out;
	}

	void Paint(UStaticMeshComponent* Mesh, uint8 Style, TObjectPtr<UMaterialInstanceDynamic>& Mid)
	{
		if (!Mesh)
		{
			return;
		}
		const TCHAR* TexturePath = TextureFor(Style);
		UTexture* Skin = TexturePath ? LoadObject<UTexture>(nullptr, TexturePath, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
		if (!Skin)
		{
			// Factory finish (or a build that did not cook this skin's art): only undo what we painted.
			if (Mid)
			{
				Mid->SetScalarParameterValue(TEXT("SkinStrength"), 0.f);
			}
			return;
		}
		if (!Mid)
		{
			Mid = Mesh->CreateDynamicMaterialInstance(0);
		}
		if (Mid)
		{
			Mid->SetTextureParameterValue(TEXT("SkinTexture"), Skin);
			Mid->SetScalarParameterValue(TEXT("PatternMode"), 1.f); // UV1 side projection, the approved look
			Mid->SetScalarParameterValue(TEXT("SkinStrength"), 1.f);
		}
	}
}

#undef LOCTEXT_NAMESPACE
