#include "Campus/CampusWeapons.h"
#include "HAL/IConsoleManager.h"

// The numbers. Damage, magazine and the relative fire-rate order come straight from the backend catalogue
// (migrations/004_store.up.sql); rpm, recoil, reload and falloff are the mobile-shooter archetype values the
// catalogue only hints at ("2x fire rate", "short range", "slow reload"), expressed in engine units.
//
// Spread is the store's accuracy read as a cone: SpreadDeg ~= (100 - accuracy) / 12, so an 80-accuracy M416
// holds a 1.7 deg cone from the hip and a 60-accuracy M249 sprays over 3.3 deg.
namespace
{
	TAutoConsoleVariable<int32> CVarGunStatsEnabled(
		TEXT("campus.GunStatsEnabled"),
		1,
		TEXT("Store upgrade bonuses on bought guns (FCampusWeaponMods). 0 = every gun runs on its base row."),
		ECVF_Default);

	/** Which attribute a trait moves (Docs/GUN_ATTRIBUTES_DESIGN.md Section 5.4). */
	enum class EAttr : uint8 { None, Damage, FireRate, Accuracy, Reload, Magazine, Range, Recoil };

	/** One gun's purchased trait: two positives that scale with level and one negative that never does. */
	struct FTrait
	{
		const TCHAR* Id;
		EAttr Primary;
		float PrimaryMaxPct;
		EAttr Secondary;
		float SecondaryMaxPct;
		EAttr Negative;      // only ever Reload, Magazine or Range
		float NegativePct;   // flat at every level
	};

	// Section 5.4's table, verbatim. Positives are the value at MAX level; the ladder below scales them.
	// m4a1 is free and starter-owned, so it gets the smallest positives and no negative at all.
	constexpr FTrait Traits[] = {
		{ TEXT("m4a1"),   EAttr::Accuracy, 17.5f, EAttr::Reload,    8.75f, EAttr::None,      0.f },
		{ TEXT("ak47"),   EAttr::Damage,   10.f,  EAttr::FireRate,  9.f,   EAttr::Magazine, -10.f },
		{ TEXT("m416"),   EAttr::Accuracy, 20.f,  EAttr::Recoil,   10.f,   EAttr::Reload,   -10.f },
		{ TEXT("scar"),   EAttr::Damage,   10.f,  EAttr::Accuracy, 10.f,   EAttr::Reload,   -10.f },
		{ TEXT("an94"),   EAttr::FireRate, 18.f,  EAttr::Damage,    5.f,   EAttr::Magazine, -10.f },
		{ TEXT("xm8"),    EAttr::FireRate, 18.f,  EAttr::Accuracy, 10.f,   EAttr::Range,    -10.f },
		{ TEXT("mp40"),   EAttr::FireRate, 18.f,  EAttr::Magazine, 15.f,   EAttr::Range,    -10.f },
		{ TEXT("m249"),   EAttr::Reload,   20.f,  EAttr::Accuracy, 10.f,   EAttr::Magazine, -10.f },
		{ TEXT("deagle"), EAttr::Damage,   10.f,  EAttr::Reload,   10.f,   EAttr::Magazine, -10.f },
	};

	/** Section 5.3: bonus(L) = cap x (0.5 + 0.125 x (L - 1)), L in [1, 5]. */
	float LadderShare(int32 Level)
	{
		const int32 L = FMath::Clamp(Level, 1, 5);
		return 0.5f + 0.125f * float(L - 1);
	}

	float* FieldFor(FCampusWeaponMods& Mods, EAttr Attr)
	{
		switch (Attr)
		{
		case EAttr::Damage:   return &Mods.DamagePct;
		case EAttr::FireRate: return &Mods.FireRatePct;
		case EAttr::Accuracy: return &Mods.AccuracyPct;
		case EAttr::Reload:   return &Mods.ReloadPct;
		case EAttr::Magazine: return &Mods.MagazinePct;
		case EAttr::Range:    return &Mods.RangePct;
		case EAttr::Recoil:   return &Mods.RecoilPct;
		default:              return nullptr;
		}
	}

	const TArray<FCampusWeaponDef>& Table()
	{
		static const TArray<FCampusWeaponDef> Rows = []
		{
			TArray<FCampusWeaponDef> Out;

			// ---- assault rifles: the baseline. 30 rounds, full damage to 30 m, 70 % at 60-70 m.
			{
				FCampusWeaponDef W;
				W.Id = TEXT("m4a1");
				W.Short = TEXT("M4");
				W.Name = TEXT("M4A1");
				W.Class = ECampusWeaponClass::AssaultRifle;
				W.Damage = 18.f;
				W.Rpm = 600.f;
				W.Magazine = 30;
				W.Reserve = 120;
				W.ReloadSeconds = 2.1f;
				W.SpreadDeg = 2.5f;
				W.RecoilUpDeg = 0.80f;
				W.RecoilSideDeg = 0.30f;
				W.FalloffStart = 3000.f;
				W.FalloffEnd = 6000.f;
				W.Range = 10000.f;
				W.MeshScale = 1.f;
				W.Tint = FLinearColor(0.52f, 0.54f, 0.58f);
				W.Tier = 1;
				Out.Add(W);
			}
			{
				FCampusWeaponDef W;
				W.Id = TEXT("ak47");
				W.Short = TEXT("AK");
				W.Name = TEXT("AK-47");
				W.Class = ECampusWeaponClass::AssaultRifle;
				W.Damage = 24.f;
				W.Rpm = 600.f;
				W.Magazine = 30;
				W.Reserve = 120;
				W.ReloadSeconds = 2.3f;
				W.SpreadDeg = 1.9f;
				W.RecoilUpDeg = 1.15f;   // "heavy recoil" in the store
				W.RecoilSideDeg = 0.45f;
				W.FalloffStart = 3250.f;
				W.FalloffEnd = 6500.f;
				W.Range = 11000.f;
				W.SoundPitch = 0.92f;
				W.MeshScale = 1.06f;
				W.Tint = FLinearColor(0.42f, 0.26f, 0.14f); // wood furniture
				W.Tier = 3;
				Out.Add(W);
			}
			{
				FCampusWeaponDef W;
				W.Id = TEXT("m416");
				W.Short = TEXT("M416");
				W.Name = TEXT("M416");
				W.Class = ECampusWeaponClass::AssaultRifle;
				W.Damage = 19.f;
				W.Rpm = 700.f;
				W.Magazine = 30;
				W.Reserve = 120;
				W.ReloadSeconds = 2.0f;
				W.SpreadDeg = 1.7f;      // "+15 % accuracy, low recoil"
				W.RecoilUpDeg = 0.66f;
				W.RecoilSideDeg = 0.22f;
				W.FalloffStart = 3500.f;
				W.FalloffEnd = 7000.f;
				W.Range = 11000.f;
				W.SoundPitch = 1.04f;
				W.MeshScale = 1.f;
				W.Tint = FLinearColor(0.30f, 0.33f, 0.30f);
				W.Tier = 2;
				Out.Add(W);
			}
			{
				FCampusWeaponDef W;
				W.Id = TEXT("scar");
				W.Short = TEXT("SCAR");
				W.Name = TEXT("SCAR-L");
				W.Class = ECampusWeaponClass::AssaultRifle;
				W.Damage = 21.f;
				W.Rpm = 620.f;
				W.Magazine = 30;
				W.Reserve = 120;
				W.ReloadSeconds = 2.1f;
				W.SpreadDeg = 2.1f;
				W.RecoilUpDeg = 0.84f;
				W.RecoilSideDeg = 0.28f;
				W.FalloffStart = 3250.f;
				W.FalloffEnd = 6500.f;
				W.Range = 11000.f;
				W.SoundPitch = 0.97f;
				W.MeshScale = 1.03f;
				W.Tint = FLinearColor(0.58f, 0.48f, 0.26f); // desert tan
				W.Tier = 2;
				Out.Add(W);
			}
			{
				FCampusWeaponDef W;
				W.Id = TEXT("an94");
				W.Short = TEXT("AN94");
				W.Name = TEXT("AN-94");
				W.Class = ECampusWeaponClass::AssaultRifle;
				W.Damage = 23.f;
				W.Rpm = 680.f;        // the store's "2-round burst" reads here as a fast, hard-hitting rifle
				W.Magazine = 30;
				W.Reserve = 120;
				W.ReloadSeconds = 2.4f;
				W.SpreadDeg = 2.2f;
				W.RecoilUpDeg = 1.00f;
				W.RecoilSideDeg = 0.36f;
				W.FalloffStart = 3250.f;
				W.FalloffEnd = 6500.f;
				W.Range = 11000.f;
				W.SoundPitch = 0.95f;
				W.MeshScale = 1.05f;
				W.Tint = FLinearColor(0.20f, 0.24f, 0.30f);
				W.Tier = 3;
				Out.Add(W);
			}
			{
				FCampusWeaponDef W;
				W.Id = TEXT("xm8");
				W.Short = TEXT("XM8");
				W.Name = TEXT("XM8");
				W.Class = ECampusWeaponClass::AssaultRifle;
				W.Damage = 17.f;
				W.Rpm = 720.f;
				W.Magazine = 30;
				W.Reserve = 120;
				W.ReloadSeconds = 1.8f;  // "+20 % aim speed, fast handling"
				W.SpreadDeg = 1.8f;
				W.RecoilUpDeg = 0.70f;
				W.RecoilSideDeg = 0.24f;
				W.FalloffStart = 3000.f;
				W.FalloffEnd = 6000.f;
				W.Range = 10000.f;
				W.SoundPitch = 1.08f;
				W.MeshScale = 0.97f;
				W.Tint = FLinearColor(0.14f, 0.15f, 0.17f);
				W.Tier = 2;
				Out.Add(W);
			}

			// ---- SMG: shreds inside a room, useless across the quad.
			{
				FCampusWeaponDef W;
				W.Id = TEXT("mp40");
				W.Short = TEXT("MP40");
				W.Name = TEXT("MP40");
				W.Class = ECampusWeaponClass::Smg;
				W.Damage = 14.f;
				W.Rpm = 900.f;           // "very fast fire rate"
				W.Magazine = 32;
				W.Reserve = 128;
				W.ReloadSeconds = 1.7f;
				W.SpreadDeg = 3.2f;
				W.AdsSpreadShare = 0.35f;
				W.RecoilUpDeg = 0.52f;
				W.RecoilSideDeg = 0.40f;
				W.FalloffStart = 1500.f; // short range: damage drops from 15 m
				W.FalloffEnd = 3000.f;
				W.FalloffMinShare = 0.55f;
				W.Range = 7000.f;
				W.HeadMultiplier = 1.8f;
				W.MaxZoom = 2.f;
				W.SoundPitch = 1.22f;
				W.MeshScale = 0.78f;
				W.Tint = FLinearColor(0.16f, 0.16f, 0.18f);
				W.Tier = 1;
				Out.Add(W);
			}

			// ---- LMG: a 100-round box and a long reload. Holds a corridor, loses a duel it has to reload in.
			{
				FCampusWeaponDef W;
				W.Id = TEXT("m249");
				W.Short = TEXT("M249");
				W.Name = TEXT("M249");
				W.Class = ECampusWeaponClass::Lmg;
				W.Damage = 20.f;
				W.Rpm = 700.f;
				W.Magazine = 100;
				W.Reserve = 200;
				W.ReloadSeconds = 6.0f;  // "slow reload"
				W.SpreadDeg = 3.3f;
				W.AdsSpreadShare = 0.4f;
				W.RecoilUpDeg = 0.95f;
				W.RecoilSideDeg = 0.50f;
				W.FalloffStart = 3500.f;
				W.FalloffEnd = 7000.f;
				W.FalloffMinShare = 0.75f;
				W.Range = 12000.f;
				W.HeadMultiplier = 1.75f;
				W.MaxZoom = 2.f;
				W.SoundPitch = 0.86f;
				W.MeshScale = 1.22f;
				W.Tint = FLinearColor(0.10f, 0.11f, 0.12f);
				W.Tier = 3;
				Out.Add(W);
			}

			// ---- hand cannon: seven rounds, two of them end a fight.
			{
				FCampusWeaponDef W;
				W.Id = TEXT("deagle");
				W.Short = TEXT("D.E.");
				W.Name = TEXT("Desert Eagle");
				W.Class = ECampusWeaponClass::Pistol;
				W.Damage = 34.f;
				W.Rpm = 200.f;           // "few shots, big damage"
				W.Magazine = 7;
				W.Reserve = 35;
				W.ReloadSeconds = 1.5f;
				W.SpreadDeg = 2.3f;
				W.AdsSpreadShare = 0.2f;
				W.RecoilUpDeg = 1.70f;
				W.RecoilSideDeg = 0.55f;
				W.FalloffStart = 2500.f;
				W.FalloffEnd = 5000.f;
				W.FalloffMinShare = 0.6f;
				W.Range = 9000.f;
				W.HeadMultiplier = 2.2f;  // "+10 % headshot damage"
				W.MaxZoom = 2.f;
				W.SoundPitch = 0.78f;
				W.MeshScale = 0.55f;
				W.Tint = FLinearColor(0.72f, 0.62f, 0.30f); // gold slide
				W.Tier = 1;
				Out.Add(W);
			}
			return Out;
		}();
		return Rows;
	}
}

bool FCampusWeaponMods::IsNone() const
{
	return FMath::IsNearlyZero(DamagePct) && FMath::IsNearlyZero(FireRatePct) && FMath::IsNearlyZero(AccuracyPct)
		&& FMath::IsNearlyZero(ReloadPct) && FMath::IsNearlyZero(MagazinePct) && FMath::IsNearlyZero(RangePct)
		&& FMath::IsNearlyZero(RecoilPct);
}

FCampusWeaponMods FCampusWeaponMods::Clamped() const
{
	// Work on pointers into a copy, so the cap is applied by sorting the fields, not by naming them twice.
	FCampusWeaponMods Out = *this;
	float* Fields[] = { &Out.DamagePct, &Out.FireRatePct, &Out.AccuracyPct, &Out.ReloadPct, &Out.MagazinePct,
		&Out.RangePct, &Out.RecoilPct };
	TArray<float*, TInlineAllocator<6>> Positive;
	TArray<float*, TInlineAllocator<6>> Negative;
	for (float* Field : Fields)
	{
		*Field = FMath::Clamp(*Field, -MaxNegativePct, MaxPositivePct);
		if (*Field > 0.f)
		{
			Positive.Add(Field);
		}
		else if (*Field < 0.f)
		{
			Negative.Add(Field);
		}
	}
	// Keep the strongest allowed ones; the rest go to zero. Biggest first.
	Positive.Sort([](const float& A, const float& B) { return A > B; }); // TArray<T*>::Sort hands the predicate references
	Negative.Sort([](const float& A, const float& B) { return A < B; });
	for (int32 Index = MaxPositiveMods; Index < Positive.Num(); ++Index)
	{
		*Positive[Index] = 0.f;
	}
	for (int32 Index = MaxNegativeMods; Index < Negative.Num(); ++Index)
	{
		*Negative[Index] = 0.f;
	}
	return Out;
}

bool CampusWeapons::GunStatsEnabled()
{
	return CVarGunStatsEnabled.GetValueOnAnyThread() != 0;
}

FCampusWeaponMods CampusWeapons::ModsForLevel(FName Id, int32 Level)
{
	FCampusWeaponMods Mods;
	if (Id.IsNone() || Level <= 0)
	{
		return Mods; // not owned / not upgraded, and every looted gun: the base row
	}
	const FTrait* Trait = nullptr;
	for (const FTrait& Candidate : Traits)
	{
		if (Id == FName(Candidate.Id))
		{
			Trait = &Candidate;
			break;
		}
	}
	if (!Trait)
	{
		return Mods;
	}
	const float Share = LadderShare(Level);
	if (float* Primary = FieldFor(Mods, Trait->Primary))
	{
		*Primary = Trait->PrimaryMaxPct * Share;
	}
	if (float* Secondary = FieldFor(Mods, Trait->Secondary))
	{
		*Secondary = Trait->SecondaryMaxPct * Share;
	}
	// The tax is flat: it does not grow with the level, so upgrading is never a net negative.
	if (float* Negative = FieldFor(Mods, Trait->Negative))
	{
		*Negative = Trait->NegativePct;
	}
	return Mods.Clamped();
}

FCampusWeaponDef CampusWeapons::WithMods(const FCampusWeaponDef& Base, const FCampusWeaponMods& Mods)
{
	if (!GunStatsEnabled() || Mods.IsNone())
	{
		return Base;
	}
	const FCampusWeaponMods Legal = Mods.Clamped();
	FCampusWeaponDef Out = Base;
	// Every multiplier is clamped to [0.75, 1.25] on the way out: the cap is the load-bearing wall, and it
	// holds even if a trait table or a level ever says otherwise.
	const auto Mult = [](float Pct) { return FMath::Clamp(1.f + Pct * 0.01f, 0.75f, 1.25f); };
	Out.Damage = Base.Damage * Mult(Legal.DamagePct);
	Out.Rpm = Base.Rpm * Mult(Legal.FireRatePct);
	// Accuracy, reload and recoil are "lower is better": the bonus divides them.
	Out.SpreadDeg = Base.SpreadDeg / Mult(Legal.AccuracyPct);
	Out.ReloadSeconds = Base.ReloadSeconds / Mult(Legal.ReloadPct);
	Out.RecoilUpDeg = Base.RecoilUpDeg / Mult(Legal.RecoilPct);
	Out.RecoilSideDeg = Base.RecoilSideDeg / Mult(Legal.RecoilPct);
	Out.Magazine = FMath::Max(1, FMath::RoundToInt(float(Base.Magazine) * Mult(Legal.MagazinePct)));
	Out.Reserve = FMath::Max(0, FMath::RoundToInt(float(Base.Reserve) * Mult(Legal.MagazinePct)));
	Out.FalloffStart = Base.FalloffStart * Mult(Legal.RangePct);
	Out.FalloffEnd = Base.FalloffEnd * Mult(Legal.RangePct);
	Out.Range = Base.Range * Mult(Legal.RangePct);
	return Out;
}

const TCHAR* CampusWeapons::RifleMeshPath()
{
	return TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle");
}

FString CampusWeapons::MeshPathFor(FName Id)
{
	// The per-gun low-poly meshes land at /Game/Weapons/Guns/SM_Gun_<id>. Resolved by name at runtime on
	// purpose: the guns appear as they are cooked, with no compile-time dependency on that work.
	if (Id.IsNone())
	{
		return FString(RifleMeshPath());
	}
	const FString Name = FString::Printf(TEXT("SM_Gun_%s"), *Id.ToString().ToLower());
	return FString::Printf(TEXT("/Game/Weapons/Guns/%s.%s"), *Name, *Name);
}

const TArray<FCampusWeaponDef>& CampusWeapons::All()
{
	return Table();
}

const FCampusWeaponDef* CampusWeapons::Find(FName Id)
{
	if (Id.IsNone())
	{
		return nullptr;
	}
	for (const FCampusWeaponDef& Def : Table())
	{
		if (Def.Id == Id)
		{
			return &Def;
		}
	}
	return nullptr;
}

const FCampusWeaponDef& CampusWeapons::Starter()
{
	// The table always has m4a1 first; the empty-table case cannot happen but must still return something.
	static const FCampusWeaponDef Fallback;
	const TArray<FCampusWeaponDef>& Rows = Table();
	return Rows.Num() > 0 ? Rows[0] : Fallback;
}

const FCampusWeaponDef& CampusWeapons::FindOrStarter(FName Id)
{
	const FCampusWeaponDef* Found = Find(Id);
	return Found ? *Found : Starter();
}

FName CampusWeapons::RandomOfTier(int32 Tier)
{
	const TArray<FCampusWeaponDef>& Rows = Table();
	TArray<FName, TInlineAllocator<8>> Candidates;
	for (const FCampusWeaponDef& Def : Rows)
	{
		if (Def.Tier == Tier)
		{
			Candidates.Add(Def.Id);
		}
	}
	if (Candidates.Num() == 0)
	{
		return Rows.Num() > 0 ? Rows[FMath::RandHelper(Rows.Num())].Id : NAME_None;
	}
	return Candidates[FMath::RandHelper(Candidates.Num())];
}
