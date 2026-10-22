// Gun skins (backend migration 032): several per gun, ONE WORN PER GUN, and each with its own look AND its
// own abilities (the owner's decision, 2026-10-22: "AK-47 has three more skins, each skin has different
// abilities" - the Free Fire model). A worn skin paints, and changes the numbers of, EVERY gun of that type
// the player holds: the loadout one and anything picked up off the floor.
//
// A backend skin id is `skin_<gun>_<style>`. The STYLE decides the texture and the mods; the four below are
// the whole contract and must match migration 032's table, the same way CampusWeapons' rows match the
// catalogue's guns. The mods go through FCampusWeaponMods::Clamped like an upgrade level's, so the hard cap
// (two ups of at most +25 %, one down) holds whatever a skin and a level add up to.
//
// The paint is the Weapon Library V3 master material's own skin input (M_WeaponV3, see
// Scripts/weapon_v3/import_weapon_library_v3.py): SkinTexture through UV1's side projection (PatternMode 1),
// masked by the gun's SkinMask so barrels, bolts and sights stay bare metal. A mesh that is not V3 has no such
// parameters, and the paint is a no-op on it (the mods still apply).
//
// ON THE WIRE A SKIN IS A STYLE NUMBER, never an FName: one uint8 per row of CampusWeapons::All(), 0 = the
// factory finish. An FName RPC argument is interned into the engine's name table for ever (the reason
// ServerSetOutfit takes an index), and a number can only name a style this build ships.

#pragma once

#include "CoreMinimal.h"
#include "Campus/CampusWeapons.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

namespace CampusGunSkins
{
	enum EStyle : uint8
	{
		None = 0,
		Royal = 1,
		Neon = 2,
		Arctic = 3,
		Relic = 4,
		Count
	};

	/** The style a backend skin id paints with (`skin_<gun>_<style>`). Unknown = None: the factory
	 *  finish and no mods, never the wrong skin. */
	uint8 StyleFor(const FString& SkinId);

	/** The backend id of this gun's skin in this style ("skin_ak47_royal"), empty for None. */
	FString SkinIdFor(FName GunId, uint8 Style);

	/** "Royal", "Neon Hex", ... ("Factory" for None). */
	FText Label(uint8 Style);

	/** The texture a style paints with, or nullptr for None / unknown. */
	const TCHAR* TextureFor(uint8 Style);

	/** A style's abilities (migration 032's table). Empty for None. */
	FCampusWeaponMods ModsFor(uint8 Style);

	/** Field by field sum, for a level's mods plus a skin's; the caller's WithMods clamps the result. */
	FCampusWeaponMods Add(const FCampusWeaponMods& A, const FCampusWeaponMods& B);

	/** Paints (or clears) a style on a gun mesh. Mid is the component's own instance, created on the first
	 *  real paint and kept by the caller; a gun never skinned keeps the mesh's shared material. */
	void Paint(UStaticMeshComponent* Mesh, uint8 Style, TObjectPtr<UMaterialInstanceDynamic>& Mid);
}
