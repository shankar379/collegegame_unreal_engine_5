// Where each gun's art lives. One row per id in CampusWeapons.h; the meshes are built by
// Scripts/build_guns.py (low-poly boxes and cylinders, flat unlit materials, see Docs/guns_preview.png)
// and cooked through +DirectoriesToAlwaysCook=(Path="/Game/Weapons/Guns") in Config/DefaultGame.ini.
//
// Every gun shares SM_Rifle's axes exactly - origin AT the grip, barrel along +Y, up +Z - so a mesh set
// on the existing gun component drops into the hand the same way the rifle does, with no extra transform.
// The one thing that is NOT shared is how far forward the barrel ends: the pistol is short and the LMG is
// long, so MuzzleOffset() gives the per-gun replacement for namespace CampusGun's constant 51 cm.
// The meshes also carry a "Muzzle" and a "Grip" socket at those same two points, for anything that would
// rather attach than offset.

#pragma once

#include "CoreMinimal.h"

namespace CampusGunMeshes
{
	/** The starter art, and what an unknown id falls back to: the gun every build already shipped. */
	inline const TCHAR* RifleMesh()
	{
		return TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle");
	}

	/**
	 * Full object path of this gun's static mesh. Never returns null or an empty string: an id with no
	 * art of its own (a new catalogue row, or NAME_None) falls back to the rifle.
	 */
	inline const TCHAR* MeshPath(FName Id)
	{
		static const TMap<FName, const TCHAR*> Paths = {
			{ TEXT("m4a1"),   TEXT("/Game/Weapons/Guns/SM_Gun_m4a1.SM_Gun_m4a1") },
			{ TEXT("ak47"),   TEXT("/Game/Weapons/Guns/SM_Gun_ak47.SM_Gun_ak47") },
			{ TEXT("m416"),   TEXT("/Game/Weapons/Guns/SM_Gun_m416.SM_Gun_m416") },
			{ TEXT("scar"),   TEXT("/Game/Weapons/Guns/SM_Gun_scar.SM_Gun_scar") },
			{ TEXT("an94"),   TEXT("/Game/Weapons/Guns/SM_Gun_an94.SM_Gun_an94") },
			{ TEXT("xm8"),    TEXT("/Game/Weapons/Guns/SM_Gun_xm8.SM_Gun_xm8") },
			{ TEXT("mp40"),   TEXT("/Game/Weapons/Guns/SM_Gun_mp40.SM_Gun_mp40") },
			{ TEXT("m249"),   TEXT("/Game/Weapons/Guns/SM_Gun_m249.SM_Gun_m249") },
			{ TEXT("deagle"), TEXT("/Game/Weapons/Guns/SM_Gun_deagle.SM_Gun_deagle") },
		};
		const TCHAR* const* Found = Paths.Find(Id);
		return Found ? *Found : RifleMesh();
	}

	/**
	 * Barrel tip in mesh space (cm), i.e. where the tracer and the muzzle flash belong. Matches the
	 * mesh's own "Muzzle" socket. Unknown ids get the rifle's 51 cm, which is also what
	 * CampusGun::MuzzleForward already assumes, so nothing moves for a gun with no art.
	 */
	inline FVector MuzzleOffset(FName Id)
	{
		static const TMap<FName, float> Forward = {
			{ TEXT("m4a1"),   51.f },
			{ TEXT("ak47"),   51.f },
			{ TEXT("m416"),   51.f },
			{ TEXT("scar"),   51.f },
			{ TEXT("an94"),   57.f },   // long barrel
			{ TEXT("xm8"),    51.f },
			{ TEXT("mp40"),   40.f },   // short SMG
			{ TEXT("m249"),   60.f },   // LMG
			{ TEXT("deagle"), 26.f },   // hand cannon
		};
		const float* Found = Forward.Find(Id);
		return FVector(0.f, Found ? *Found : 51.f, 6.f);
	}
}
