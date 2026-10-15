// THE COSMETICS CATALOGUE. One row per thing a player can wear or stand in their lobby, and the one
// place any of it is written down - the wardrobe grid, the decor sheet, the lobby figures and
// ACampusCharacter all read it, exactly as CampusGunMeshes.h is the one place a gun's art lives.
//
// TWO TABLES, NOT ONE. An OUTFIT is a body (a skeletal mesh plus a tint); a DECOR prop is a little
// pile of primitives standing on an anchor. They share nothing but an id space, so a single row type
// would be half dead fields whichever kind it held. CampusCatalog's Maps / Modes split is the same
// shape and the same reason.
//
// V1 ART REALITY: there is no outfit art yet. Every outfit points at the ONE mannequin
// (SKM_CampusMannequin) or its twin (SKM_UEFN_Mannequin - same SK_UEFN_Mannequin skeleton, verified
// against both .uasset's skeleton reference, so the native UCampusAnimInstance keeps working) and
// reads apart by COLOUR. The day an artist delivers SKM_Outfit_Varsity, that is one string in the
// row below and no code anywhere.
//
// DECOR props are built from the engine's basic shapes at runtime rather than from authored assets,
// for the same reason the UI glyphs are drawn rather than textured: a handful of boxes and cylinders
// cannot fail to load, cannot miss a cook, and need nobody in Blender today.

#pragma once

#include "CoreMinimal.h"

namespace CampusCosmetics
{
	/** Which table an id belongs to. The backend's owned list mixes both, so it has to be askable. */
	enum class EKind : uint8 { Unknown, Outfit, Decor };

	enum class ERarity : uint8 { Common, Rare, Epic, Legendary };

	// ---------------------------------------------------------------------------------------------
	// How an outfit is tinted.
	//
	// THE BODY MATERIAL. M_UEFN_Mannequin - the material both mannequin meshes actually ship with -
	// exposes NO named parameters at all: it is a texture sample straight into BaseColor, so
	// SetVectorParameterValue on it is a silent no-op and a dynamic instance of it has nothing to set.
	// Its sibling M_UEFN_Mannequin_Masked_Bicolor is the one that does: it has `bUsedWithSkeletalMesh`,
	// a two-tone body mask (T_UEFN_Mannequin_Mask_Bicolor) and the four parameters named below, which
	// is exactly what a colour-only outfit needs. Verified by reading the parameter names out of both
	// .uasset files; MI_UEFN_Mannequin_CMC is Epic's own instance of it and overrides the same two.

	/** Full object path of the material every tinted outfit wears. */
	inline const TCHAR* BodyMaterialPath()
	{
		return TEXT("/Game/Characters/UEFN_Mannequin/Materials/M_UEFN_Mannequin_Masked_Bicolor.M_UEFN_Mannequin_Masked_Bicolor");
	}

	/** The parameter names on that material. Spaces included - they are the artist's, not ours. */
	inline const TCHAR* ParamPrimaryColor()     { return TEXT("Primary Color"); }
	inline const TCHAR* ParamSecondaryColor()   { return TEXT("Secondary Color"); }
	inline const TCHAR* ParamPrimaryRough()     { return TEXT("Primary Roughness"); }
	inline const TCHAR* ParamSecondaryRough()   { return TEXT("Secondary Roughness"); }
	inline const TCHAR* ParamPrimaryMetallic()  { return TEXT("Primary Metallic"); }
	inline const TCHAR* ParamSecondaryMetallic(){ return TEXT("Secondary Metallic"); }

	/** The mannequin, and what an outfit row leaves the body as when it brings no mesh of its own. */
	inline const TCHAR* DefaultBodyMesh()
	{
		return TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin");
	}

	// ---------------------------------------------------------------------------------------------
	// Outfits

	struct FOutfitEntry
	{
		/** Persisted in UCampusSave::EquippedOutfit, replicated as ACampusCharacter::OutfitId, and the
		 *  id the API's /me/cosmetics carries. Lower case with an outfit_ prefix, like the gun ids. */
		const TCHAR* Id;
		const TCHAR* Name;
		ERarity Rarity;
		/** The whole body. A future SKM_Outfit_* row changes only this. */
		const TCHAR* Mesh;
		/** TRUE leaves the mesh's own material alone: the starter look is the authored texture, and a
		 *  flat tint over it would be a downgrade dressed as a feature. */
		bool bKeepMeshMaterial;
		/** The two body tones the mask splits the body into, and how they catch the light. */
		FLinearColor Primary;
		FLinearColor Secondary;
		float Roughness;
		float Metallic;
		/** Owned by everybody, always, with no backend: the one outfit a signed-out player still wears. */
		bool bFree;
	};

	/** THE OUTFIT LIST, in the order the wardrobe grid shows it. */
	inline const FOutfitEntry* OutfitTable(int32& OutNum)
	{
		static const FOutfitEntry Table[] = {
			// The look every build has shipped: the authored mannequin, untinted. Free, always owned.
			{ TEXT("outfit_starter"), TEXT("Campus Standard"), ERarity::Common,
				TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin"), true,
				FLinearColor::White, FLinearColor::White, 0.5f, 0.f, true },
			// College colours: deep blue body, accent-yellow trim. Cloth, so rough and not metal.
			{ TEXT("outfit_varsity"), TEXT("Varsity Jacket"), ERarity::Rare,
				TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin"), false,
				FLinearColor(0.055f, 0.105f, 0.420f, 1.f), FLinearColor(1.000f, 0.720f, 0.000f, 1.f), 0.72f, 0.f, false },
			// Street grey over a hot magenta lining; the loudest of the four.
			{ TEXT("outfit_hoodie"), TEXT("Street Hoodie"), ERarity::Rare,
				TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin"), false,
				FLinearColor(0.180f, 0.190f, 0.215f, 1.f), FLinearColor(0.780f, 0.090f, 0.420f, 1.f), 0.85f, 0.f, false },
			// Matte olive webbing with gunmetal plates - the only one with any metal in it.
			{ TEXT("outfit_tactical"), TEXT("Tactical Rig"), ERarity::Epic,
				TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"), false,
				FLinearColor(0.095f, 0.115f, 0.070f, 1.f), FLinearColor(0.255f, 0.270f, 0.295f, 1.f), 0.38f, 0.65f, false },
		};
		OutNum = UE_ARRAY_COUNT(Table);
		return Table;
	}

	inline int32 NumOutfits()
	{
		int32 Num = 0;
		OutfitTable(Num);
		return Num;
	}

	inline const FOutfitEntry& Outfit(int32 Index)
	{
		int32 Num = 0;
		const FOutfitEntry* Table = OutfitTable(Num);
		return Table[FMath::Clamp(Index, 0, Num - 1)];
	}

	/** Index of Id, or INDEX_NONE. */
	inline int32 FindOutfit(FName Id)
	{
		int32 Num = 0;
		const FOutfitEntry* Table = OutfitTable(Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (FName(Table[Index].Id) == Id)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/** The id of the free starter outfit: the fallback for everything. */
	inline FName StarterOutfit()
	{
		return FName(Outfit(0).Id);
	}

	/**
	 * NEVER NULL. An id this build does not know - an old save's "Default", a cosmetic the server sold
	 * after this APK shipped, NAME_None - is the starter outfit, because a body with no mesh is not a
	 * player anybody can see.
	 */
	inline const FOutfitEntry& OutfitFor(FName Id)
	{
		const int32 Index = FindOutfit(Id);
		return Outfit(Index == INDEX_NONE ? 0 : Index);
	}

	// ---------------------------------------------------------------------------------------------
	// The eight fixed lobby anchors.
	//
	// WHERE THEY ARE AND WHY. The lobby camera stands at (380, 0, 135) looking down -X at 70 degrees
	// horizontal FOV (CampusLobbyScene / CampusLineUp - those numbers are shared, not re-guessed), and
	// the line-up stands between X -26 and +22 with |Y| up to 115. So an anchor is only allowed to be
	// BEHIND the figures (more negative X) or OUT at the screen edges; nothing may sit between the
	// camera and a face. The visible half-width at distance d is d * tan(35) = 0.70 d, which is what
	// puts floor_l / floor_r at about 72 % out and the wall pair at about 75 %.
	//
	// The ids are the API's (`decor: {anchor: id}`) and must not be renamed on a whim.

	struct FAnchor
	{
		/** What PUT /me/lobby carries. */
		const TCHAR* Id;
		/** What the decor sheet calls it. */
		const TCHAR* Name;
		/** Lobby-space cm. Z is measured UP FROM THE FLOOR, not from the world origin. */
		float X;
		float Y;
		float Z;
		/** Turned to face the lobby camera, so a prop with a front shows it. */
		float Yaw;
		/** The prop HANGS from this anchor instead of standing on it: its top sits at Z and it is
		 *  built downwards. Only the one high backdrop anchor is like this. */
		bool bHang;
	};

	constexpr int32 NumAnchors = 8;

	inline const FAnchor& Anchor(int32 Index)
	{
		static const FAnchor Table[NumAnchors] = {
			// Beside the line-up at the screen edges, a step back from the player.
			{ TEXT("floor_l"),      TEXT("Floor Left"),       -40.f,  215.f,   0.f, -27.f, false },
			{ TEXT("floor_r"),      TEXT("Floor Right"),      -40.f, -215.f,   0.f,  27.f, false },
			// Behind the two outer teammates, far enough back to read as a second row.
			{ TEXT("floor_back_l"), TEXT("Back Left"),       -210.f,  165.f,   0.f, -16.f, false },
			{ TEXT("floor_back_r"), TEXT("Back Right"),      -210.f, -165.f,   0.f,  16.f, false },
			// Against the side walls: the furthest out anything can go and still be on screen.
			{ TEXT("wall_l"),       TEXT("Wall Left"),       -230.f,  320.f,   0.f, -27.f, false },
			{ TEXT("wall_r"),       TEXT("Wall Right"),      -230.f, -320.f,   0.f,  27.f, false },
			// Dead centre BEHIND the line-up, seen in the gap between the player and their neighbour.
			{ TEXT("pedestal"),     TEXT("Centre Pedestal"), -165.f,    0.f,   0.f,   0.f, false },
			// High and far back, hanging. At 640 cm out the top of frame is Z 306, so a 210 cm banner
			// hung from 300 is fully visible and still 120 cm clear of the tallest head.
			{ TEXT("overhead"),     TEXT("Overhead Banner"), -260.f,    0.f, 300.f,   0.f, true  },
		};
		return Table[FMath::Clamp(Index, 0, NumAnchors - 1)];
	}

	inline int32 FindAnchor(FName Id)
	{
		for (int32 Index = 0; Index < NumAnchors; ++Index)
		{
			if (FName(Anchor(Index).Id) == Id)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/** 1 << anchor index, for FDecorEntry::Anchors. */
	inline uint32 AnchorBit(int32 Index)
	{
		return Index >= 0 && Index < NumAnchors ? (1u << Index) : 0u;
	}

	/** Every anchor a prop may stand on, minus the hanging one. */
	inline uint32 StandingAnchors()
	{
		uint32 Mask = 0;
		for (int32 Index = 0; Index < NumAnchors; ++Index)
		{
			if (!Anchor(Index).bHang)
			{
				Mask |= AnchorBit(Index);
			}
		}
		return Mask;
	}

	// ---------------------------------------------------------------------------------------------
	// Decor props

	/** One primitive in a prop's recipe. */
	struct FDecorPart
	{
		/** Full object path of an engine basic shape, or null for an unused row. */
		const TCHAR* Mesh;
		/** Centre of this primitive in cm, relative to the prop's own base (Z 0 = standing on it). */
		float X;
		float Y;
		float Z;
		/** How big it is in cm. The basic shapes are 100 cm across, so the scale is Size / 100. */
		float SizeX;
		float SizeY;
		float SizeZ;
		/** Local turn applied BEFORE the anchor's own yaw. Pitch 90 lays a cylinder on its side. */
		float Yaw;
		float Pitch;
		FLinearColor Color;
	};

	/** The longest recipe below uses five. Raise it when a prop needs a sixth. */
	constexpr int32 MaxDecorParts = 5;

	struct FDecorEntry
	{
		const TCHAR* Id;
		const TCHAR* Name;
		ERarity Rarity;
		/** Every anchor this prop is allowed on, as AnchorBit flags. */
		uint32 Anchors;
		FDecorPart Parts[MaxDecorParts];
	};

	/** The basic shapes, named once so a typo is a compile error in one place. */
	inline const TCHAR* ShapeCube()     { return TEXT("/Engine/BasicShapes/Cube.Cube"); }
	inline const TCHAR* ShapeCylinder() { return TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); }
	inline const TCHAR* ShapeCone()     { return TEXT("/Engine/BasicShapes/Cone.Cone"); }
	inline const TCHAR* ShapeSphere()   { return TEXT("/Engine/BasicShapes/Sphere.Sphere"); }

	/** /Engine/BasicShapes' own material. Unlike the mannequin's, it DOES expose a vector parameter
	 *  ("Color"), which is the whole reason the props are tinted and not grey. */
	inline const TCHAR* ShapeMaterialPath()
	{
		return TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	}

	inline const TCHAR* ParamShapeColor() { return TEXT("Color"); }

	/** THE PROP LIST, in the order the decor sheet shows it. */
	inline const FDecorEntry* DecorTable(int32& OutNum)
	{
		// The palette, spelled out once so two props cannot drift apart.
		const FLinearColor Dark(0.045f, 0.050f, 0.060f, 1.f);
		const FLinearColor Steel(0.330f, 0.350f, 0.390f, 1.f);
		const FLinearColor Gold(1.000f, 0.720f, 0.000f, 1.f);
		const FLinearColor Wood(0.330f, 0.190f, 0.095f, 1.f);
		const FLinearColor Red(0.720f, 0.090f, 0.075f, 1.f);
		const FLinearColor Orange(0.920f, 0.330f, 0.030f, 1.f);
		const FLinearColor White(0.900f, 0.900f, 0.900f, 1.f);

		static const FDecorEntry Table[] = {
			// A bike seen side-on: two cylinders laid flat (pitch 90 puts their axis across the view),
			// a crossbar and two posts.
			{ TEXT("decor_bike"), TEXT("Campus Bike"), ERarity::Rare, StandingAnchors(), {
				{ ShapeCylinder(),  0.f, -35.f,  30.f,  60.f,  60.f,  10.f, 0.f, 90.f, Dark },
				{ ShapeCylinder(),  0.f,  35.f,  30.f,  60.f,  60.f,  10.f, 0.f, 90.f, Dark },
				{ ShapeCube(),      0.f,   0.f,  48.f,   8.f,  84.f,   8.f, 0.f,  0.f, Red },
				{ ShapeCube(),      0.f, -22.f,  62.f,   8.f,   8.f,  36.f, 0.f,  0.f, Steel },
				{ ShapeCube(),      0.f,  28.f,  66.f,   8.f,   8.f,  44.f, 0.f,  0.f, Steel },
			} },
			// The only prop allowed on the hanging anchor as well: a pole with a cloth and a finial,
			// which reads as a banner whether it stands on the floor or hangs over the line-up.
			{ TEXT("decor_banner"), TEXT("Club Banner"), ERarity::Common, 0u, {
				{ ShapeCylinder(),  0.f,   0.f, 105.f,   6.f,   6.f, 210.f, 0.f,  0.f, Steel },
				{ ShapeCube(),      0.f,  46.f, 150.f,   4.f,  90.f, 120.f, 0.f,  0.f, Gold },
				{ ShapeSphere(),    0.f,   0.f, 214.f,  14.f,  14.f,  14.f, 0.f,  0.f, Gold },
				{ nullptr, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, FLinearColor::White },
				{ nullptr, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, FLinearColor::White },
			} },
			// Base, plinth, stem, an upside-down cone for the cup, and a knob.
			{ TEXT("decor_trophy"), TEXT("Champion Trophy"), ERarity::Legendary, StandingAnchors(), {
				{ ShapeCube(),      0.f,   0.f,   9.f,  60.f,  60.f,  18.f, 0.f,   0.f, Dark },
				{ ShapeCube(),      0.f,   0.f,  23.f,  40.f,  40.f,  10.f, 0.f,   0.f, Dark },
				{ ShapeCylinder(),  0.f,   0.f,  46.f,  14.f,  14.f,  36.f, 0.f,   0.f, Gold },
				{ ShapeCone(),      0.f,   0.f,  91.f,  56.f,  56.f,  54.f, 0.f, 180.f, Gold },
				{ ShapeSphere(),    0.f,   0.f, 122.f,  18.f,  18.f,  18.f, 0.f,   0.f, Gold },
			} },
			// A PA stack: stand, cabinet, and two drivers facing the camera (+X).
			{ TEXT("decor_speaker"), TEXT("Event Speaker"), ERarity::Rare, StandingAnchors(), {
				{ ShapeCylinder(),  0.f,   0.f,  35.f,  24.f,  24.f,  70.f, 0.f,  0.f, Dark },
				{ ShapeCube(),      0.f,   0.f, 110.f,  56.f,  46.f,  80.f, 0.f,  0.f, Dark },
				{ ShapeCylinder(), 26.f,   0.f,  96.f,  34.f,  34.f,   8.f, 0.f, 90.f, Steel },
				{ ShapeCylinder(), 26.f,   0.f, 136.f,  16.f,  16.f,   8.f, 0.f, 90.f, Gold },
				{ nullptr, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, FLinearColor::White },
			} },
			{ TEXT("decor_bench"), TEXT("Quad Bench"), ERarity::Common, StandingAnchors(), {
				{ ShapeCube(),      0.f,   0.f,  45.f,  46.f, 150.f,  10.f, 0.f, 0.f, Wood },
				{ ShapeCube(),      0.f, -62.f,  22.f,  40.f,  12.f,  45.f, 0.f, 0.f, Dark },
				{ ShapeCube(),      0.f,  62.f,  22.f,  40.f,  12.f,  45.f, 0.f, 0.f, Dark },
				{ ShapeCube(),    -18.f,   0.f,  68.f,  10.f, 150.f,  40.f, 0.f, 0.f, Wood },
				{ nullptr, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, FLinearColor::White },
			} },
			{ TEXT("decor_cone"), TEXT("Traffic Cone"), ERarity::Common, StandingAnchors(), {
				{ ShapeCube(),      0.f,   0.f,   4.f,  46.f,  46.f,   8.f, 0.f, 0.f, Dark },
				{ ShapeCone(),      0.f,   0.f,  43.f,  40.f,  40.f,  70.f, 0.f, 0.f, Orange },
				{ ShapeCylinder(),  0.f,   0.f,  46.f,  30.f,  30.f,  10.f, 0.f, 0.f, White },
				{ nullptr, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, FLinearColor::White },
				{ nullptr, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, FLinearColor::White },
			} },
		};
		OutNum = UE_ARRAY_COUNT(Table);
		return Table;
	}

	inline int32 NumDecor()
	{
		int32 Num = 0;
		DecorTable(Num);
		return Num;
	}

	inline const FDecorEntry& Decor(int32 Index)
	{
		int32 Num = 0;
		const FDecorEntry* Table = DecorTable(Num);
		return Table[FMath::Clamp(Index, 0, Num - 1)];
	}

	inline int32 FindDecor(FName Id)
	{
		int32 Num = 0;
		const FDecorEntry* Table = DecorTable(Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (FName(Table[Index].Id) == Id)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/**
	 * NULL IS A REAL ANSWER HERE, unlike OutfitFor: an anchor with nothing on it is the default state
	 * of seven of the eight, so "no prop" has to be expressible. An unknown id reads as empty, which
	 * is how a decor item sold after this APK shipped fails: the anchor stays bare.
	 */
	inline const FDecorEntry* DecorFor(FName Id)
	{
		const int32 Index = FindDecor(Id);
		return Index == INDEX_NONE ? nullptr : &Decor(Index);
	}

	inline bool DecorAllowedAt(const FDecorEntry& Entry, int32 AnchorIndex)
	{
		return Entry.Anchors == 0u || (Entry.Anchors & AnchorBit(AnchorIndex)) != 0u;
	}

	/** How tall this prop is, measured off its own recipe. What the hanging anchor drops it by. */
	inline float DecorHeight(const FDecorEntry& Entry)
	{
		float Top = 0.f;
		for (const FDecorPart& Part : Entry.Parts)
		{
			if (Part.Mesh)
			{
				Top = FMath::Max(Top, Part.Z + 0.5f * Part.SizeZ);
			}
		}
		return Top;
	}

	// ---------------------------------------------------------------------------------------------
	// Across both tables

	inline EKind KindOf(FName Id)
	{
		if (FindOutfit(Id) != INDEX_NONE)
		{
			return EKind::Outfit;
		}
		return FindDecor(Id) != INDEX_NONE ? EKind::Decor : EKind::Unknown;
	}

	inline const TCHAR* RarityName(ERarity Rarity)
	{
		switch (Rarity)
		{
		case ERarity::Rare:      return TEXT("RARE");
		case ERarity::Epic:      return TEXT("EPIC");
		case ERarity::Legendary: return TEXT("LEGENDARY");
		default:                 return TEXT("COMMON");
		}
	}

	/** The frame colour a card draws around a rarity. Never the only signal: the word is on the card. */
	inline FLinearColor RarityColor(ERarity Rarity)
	{
		switch (Rarity)
		{
		case ERarity::Rare:      return FLinearColor(0.30f, 0.62f, 1.00f, 1.f);
		case ERarity::Epic:      return FLinearColor(0.68f, 0.38f, 1.00f, 1.f);
		case ERarity::Legendary: return FLinearColor(1.00f, 0.72f, 0.00f, 1.f);
		default:                 return FLinearColor(0.62f, 0.66f, 0.72f, 1.f);
		}
	}

	/** How many wardrobe presets there are. The owner's number; the API indexes them from 1. */
	constexpr int32 NumPresets = 3;
}
