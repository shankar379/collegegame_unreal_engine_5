// The gun table. One row per weapon, ids identical to the backend catalogue (catalog_items.kind = 'gun'),
// so the store's numbers and the match's numbers are the same numbers: a player who reads "AK-47, 24 damage,
// 2x fire rate" in the shop gets exactly that in the round.
//
// Archetypes follow the Free Fire / PUBG mobile convention: assault rifles are the baseline (medium rpm,
// 30 rounds, damage falls off past ~30 m), SMGs trade damage and range for rate of fire, the LMG trades
// accuracy and reload time for a 100-round box, and the hand cannon trades everything for one-shot weight.
//
// No art is added: every gun is the one SM_Rifle mesh at a different scale and tint, with its own sound pitch.
// ACampusCharacter copies a row into its combat fields when it equips (see ApplyWeapon), so the whole
// existing fire / falloff / reload path keeps working untouched and simply reads the current gun's stats.

#pragma once

#include "CoreMinimal.h"

/** What the gun behaves like. Drives nothing on its own; it is how loot tiers and the HUD group the rows. */
enum class ECampusWeaponClass : uint8
{
	AssaultRifle,
	Smg,
	Lmg,
	Pistol
};

/** One gun. Plain struct (like FCampusGun): read-only data, never replicated - only its Id travels. */
struct FCampusWeaponDef
{
	/** Backend catalogue id: m4a1, ak47, m416, scar, an94, xm8, mp40, m249, deagle. */
	FName Id;
	/** Short HUD label (the weapons panel has room for about four characters). */
	const TCHAR* Short = TEXT("GUN");
	const TCHAR* Name = TEXT("Gun");
	ECampusWeaponClass Class = ECampusWeaponClass::AssaultRifle;

	/** Body damage per hit, before zone and falloff. Same number as the store's "damage". */
	float Damage = 25.f;
	/** Rounds per minute. FireInterval = 60 / Rpm. */
	float Rpm = 600.f;
	int32 Magazine = 30;
	/** Rounds carried outside the magazine when the gun is equipped fresh. */
	int32 Reserve = 120;
	float ReloadSeconds = 2.1f;

	/** Hip-fire cone half-angle in degrees (from the store's accuracy); x AdsSpreadShare while aiming. */
	float SpreadDeg = 1.6f;
	float AdsSpreadShare = 0.25f;
	float RecoilUpDeg = 0.92f;
	float RecoilSideDeg = 0.34f;

	/** Full damage out to FalloffStart, down to FalloffMinShare at FalloffEnd (cm). */
	float FalloffStart = 3000.f;
	float FalloffEnd = 6000.f;
	float FalloffMinShare = 0.7f;
	/** How far the shot trace reaches (cm). */
	float Range = 10000.f;

	float HeadMultiplier = 2.f;
	float LimbMultiplier = 0.75f;

	/** Highest scope step this gun offers (1x / 2x / 4x); the zoom button stops here. */
	float MaxZoom = 4.f;

	/** Shot sound pitch: the hand cannon booms, the SMG cracks. */
	float SoundPitch = 1.f;
	/** Rifle mesh reuse: uniform scale and tint so the guns read apart in the hand and on the ground. */
	float MeshScale = 1.f;
	FLinearColor Tint = FLinearColor(0.55f, 0.55f, 0.58f);

	/** Ground-loot rarity: 1 common, 2 uncommon, 3 rare. */
	int32 Tier = 1;
};

/** Store upgrades, Free Fire style: a gun the player BOUGHT and levelled up beats the same gun off the floor.
 *  Ground loot and the starter always run on the base row (no mods at all).
 *
 *  Percentages on top of a row. The cap is enforced by Clamped() and is deliberately tight: at most TWO
 *  positive mods of at most +MaxPositivePct each, and at most ONE negative of at least -MaxNegativePct
 *  (interim rule until Docs/GUN_ATTRIBUTES_DESIGN.md lands; the numbers live here, in one place).
 *  AccuracyPct positive TIGHTENS the cone; ReloadPct positive makes the reload SHORTER. */
struct FCampusWeaponMods
{
	float DamagePct = 0.f;
	float FireRatePct = 0.f;
	float AccuracyPct = 0.f;
	float ReloadPct = 0.f;
	float MagazinePct = 0.f;
	float RangePct = 0.f;
	/** Recoil control: positive means less kick. */
	float RecoilPct = 0.f;

	/** Docs/GUN_ATTRIBUTES_DESIGN.md Section 5.2: hard clamp +-25 % per attribute, shipped values stop
	 *  well short of it. The negative is flat at every level and never lands on damage or accuracy. */
	static constexpr float MaxPositivePct = 25.f;
	static constexpr float MaxNegativePct = 25.f;
	static constexpr int32 MaxPositiveMods = 2;
	static constexpr int32 MaxNegativeMods = 1;

	bool IsNone() const;
	/** A legal set, whatever this one was: the largest positives and the single worst negative survive. */
	FCampusWeaponMods Clamped() const;
};

namespace CampusWeapons
{
	/** campus.GunStatsEnabled: 0 makes every gun run flat on its base row, store level or not (fair rooms). */
	bool GunStatsEnabled();
	/** The mods an OWNED gun of this id has earned at this upgrade level, per Docs/GUN_ATTRIBUTES_DESIGN.md
	 *  Section 5.4: one primary positive, one secondary positive, one flat negative, with
	 *  bonus(L) = cap x (0.5 + 0.125 x (L - 1)). Level 0 / 1 means "not upgraded": positives start at
	 *  half the cap from L1. LOOTED guns never get this - ground loot is always the base row. */
	FCampusWeaponMods ModsForLevel(FName Id, int32 Level);
	/** Base row + mods, clamped. Returns Base untouched when the cvar is off or the mods are empty. */
	FCampusWeaponDef WithMods(const FCampusWeaponDef& Base, const FCampusWeaponMods& Mods);
	/** The low-poly gun mesh for an id by convention (/Game/Weapons/Guns/SM_Gun_<id>); the caller falls
	 *  back to the shared rifle when that asset is not cooked yet. */
	FString MeshPathFor(FName Id);
	/** The shared fallback: the rifle every hand already holds. */
	const TCHAR* RifleMeshPath();

	/** Every row, in catalogue order. */
	const TArray<FCampusWeaponDef>& All();
	/** The row with this id, or nullptr. Ids are case-insensitive FNames, as they arrive from the API. */
	const FCampusWeaponDef* Find(FName Id);
	/** The starter M4A1: what an unarmed / unknown loadout falls back to, never nullptr. */
	const FCampusWeaponDef& Starter();
	/** Find(Id) with the starter as the fallback, so callers never branch. */
	const FCampusWeaponDef& FindOrStarter(FName Id);
	/** A random gun of this tier (loot). Falls back to any gun when the tier is empty. */
	FName RandomOfTier(int32 Tier);
}
