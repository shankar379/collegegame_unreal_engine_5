#include "Campus/CampusMapArt.h"

#include "Campus/CampusRemoteImage.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "CampusMapArt"

// -------------------------------------------------------------------------------------------------
// Catalog

namespace CampusCatalog
{
	// Three real levels: L_Match (the college room), L_CampusMap (Campus Grounds, the walled campus
	// ported from the web game's CAMPUS map) and L_VarsityValley (the open 800 m battle-royale map).
	const FMapEntry Maps[] = {
		{ TEXT("Classic"), TEXT("Campus"),      TEXT("The college  ·  quad, corridors and rooftops"), TEXT("map_campus"),
			TEXT("/Game/Maps/L_Match"), true },
		{ TEXT("CampusMap"), TEXT("Campus Grounds"), TEXT("Clock tower  ·  seven blocks, one courtyard"), TEXT("map_campusmap"),
			TEXT("/Game/Maps/L_CampusMap"), true },
		{ TEXT("VarsityValley"), TEXT("Varsity Valley"), TEXT("Open valley  ·  50 players, one circle"), TEXT("map_valley"),
			TEXT("/Game/Maps/L_VarsityValley"), true, true },
	};
	const int32 NumMaps = UE_ARRAY_COUNT(Maps);

	const FModeEntry Modes[] = {
		// RANKED (backend migration 035): the same fifty-player session, its own queue, rank points on the result.
		{ TEXT("RankedBR"), TEXT("Ranked Battle Royale"), { TEXT("VarsityValley"), nullptr }, TEXT("mode_br"),
			TEXT("The same fifty-player drop, but every match counts: your placement, your kills and your damage earn "
			     "or cost Rank Points. Climb Fresher, Sophomore, Junior, Senior, Topper, Dean's List and Gold Medalist; "
			     "the season's top 100 are Campus Legends. Ranked players only meet ranked players.\n\n"
			     "Just for fun: friendly matches between players and colleges. No cash prizes, no betting."),
			nullptr, true },
		{ TEXT("TeamClash"), TEXT("Team Clash"), { TEXT("Classic"), TEXT("CampusMap") }, TEXT("mode_clash"),
			TEXT("Two squads, one campus. You pick the map, how many rounds (4-7) and how long each one runs; every "
			     "round is played and the squad that wins the most takes the match. Everyone respawns between rounds. "
			     "Hosting costs one Room Card and gives you a 6-letter code to share.\n\n"
			     "Just for fun: friendly matches between players and colleges. No cash prizes, no betting."),
			nullptr, true },
		{ TEXT("QuickMatch"), TEXT("Quick Match"), { TEXT("Classic"), TEXT("CampusMap") }, TEXT("mode_quick"),
			TEXT("One tap into the room already open on your network. Free, no Room Card, no code to type - "
			     "the match starts on the host's settings, including the host's map.\n\n"
			     "Just for fun: friendly matches between players and colleges. No cash prizes, no betting."),
			nullptr, true },
		{ TEXT("BattleRoyale"), TEXT("Battle Royale"), { TEXT("VarsityValley"), TEXT("CampusMap") }, TEXT("mode_br"),
			TEXT("Fifty players drop into Varsity Valley - or onto the tighter Campus Grounds - and the circle closes "
			     "until one squad is left. No Room Card: tap START and the server puts you in the next match, solo / "
			     "duo / squad as you picked. The DAY / NIGHT chip is your preference - the match you are given says "
			     "which it is.\n\n"
			     "Just for fun: friendly matches between players and colleges. No cash prizes, no betting."),
			nullptr, true },
	};
	const int32 NumModes = UE_ARRAY_COUNT(Modes);

	int32 FindMap(FName Id)
	{
		for (int32 Index = 0; Index < NumMaps; ++Index)
		{
			if (Id == FName(Maps[Index].Id))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 FindMode(FName Id)
	{
		for (int32 Index = 0; Index < NumModes; ++Index)
		{
			if (Id == FName(Modes[Index].Id))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	int32 DefaultMap()
	{
		for (int32 Index = 0; Index < NumMaps; ++Index)
		{
			if (Maps[Index].bPlayable)
			{
				return Index;
			}
		}
		return 0;
	}

	int32 DefaultMode()
	{
		for (int32 Index = 0; Index < NumModes; ++Index)
		{
			if (Modes[Index].bPlayable)
			{
				return Index;
			}
		}
		return 0;
	}

	int32 PlayableMap(FName Id)
	{
		// An old save can still hold "RealMap" from before this screen existed; fall back quietly.
		const int32 Index = FindMap(Id);
		return (Index != INDEX_NONE && Maps[Index].bPlayable) ? Index : DefaultMap();
	}

	int32 PlayableMode(FName Id)
	{
		const int32 Index = FindMode(Id);
		return (Index != INDEX_NONE && Modes[Index].bPlayable) ? Index : DefaultMode();
	}

	const FMapEntry& Map(int32 Index)
	{
		return Maps[FMath::Clamp(Index, 0, NumMaps - 1)];
	}

	const FModeEntry& Mode(int32 Index)
	{
		return Modes[FMath::Clamp(Index, 0, NumModes - 1)];
	}

	bool ModeHasMap(int32 ModeIndex, FName MapId)
	{
		const FModeEntry& Entry = Mode(ModeIndex);
		for (const TCHAR* Each : Entry.MapIds)
		{
			if (Each && MapId == FName(Each))
			{
				return true;
			}
		}
		return false;
	}

	int32 ModeDefaultMap(int32 ModeIndex)
	{
		const FModeEntry& Entry = Mode(ModeIndex);
		for (const TCHAR* Each : Entry.MapIds)
		{
			const int32 Index = Each ? FindMap(FName(Each)) : INDEX_NONE;
			if (Index != INDEX_NONE && Maps[Index].bPlayable)
			{
				return Index;
			}
		}
		// A mode whose whole list is unbuilt still has to answer with something playable.
		return DefaultMap();
	}

	int32 MapForMode(FName ModeId, FName MapId)
	{
		const int32 ModeIndex = FindMode(ModeId);
		if (ModeIndex == INDEX_NONE)
		{
			return PlayableMap(MapId);
		}
		const int32 MapIndex = FindMap(MapId);
		const bool bKeep = MapIndex != INDEX_NONE && Maps[MapIndex].bPlayable && ModeHasMap(ModeIndex, MapId);
		return bKeep ? MapIndex : ModeDefaultMap(ModeIndex);
	}

	const TCHAR* LevelFor(FName MapId)
	{
		const int32 Index = FindMap(MapId);
		return Index == INDEX_NONE ? nullptr : Maps[Index].Level;
	}

	FName MapIdForLevel(const FString& LevelName)
	{
		for (int32 Index = 0; Index < NumMaps; ++Index)
		{
			// Contains, not a compare: GetMapName hands back "UEDPIE_0_L_Match" in the editor and the
			// level path carries its folders. No two level names here are substrings of each other.
			if (Maps[Index].Level && LevelName.Contains(FPaths::GetBaseFilename(Maps[Index].Level)))
			{
				return FName(Maps[Index].Id);
			}
		}
		return NAME_None;
	}
}

// -------------------------------------------------------------------------------------------------
// Runtime art

namespace CampusMapArt
{
	namespace
	{
		/** The decoded textures outlive every widget that draws them, so they are rooted once and
		 *  kept for the session: six small UI pictures, no reload when a screen is rebuilt. */
		struct FCacheEntry
		{
			FArt Art;
			FSlateBrush Brush;
		};

		TMap<FString, TSharedPtr<FCacheEntry>>& Cache()
		{
			static TMap<FString, TSharedPtr<FCacheEntry>> Map;
			return Map;
		}

		UTexture2D* DecodePng(const FString& Path)
		{
			TArray<uint8> Bytes;
			// Goes through the pak-aware file system, so this works from the staged .obb too.
			if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() == 0)
			{
				return nullptr;
			}
			// ONE DECODER FOR THE WHOLE GAME (Campus/CampusRemoteImage.h): a packaged banner, an
			// avatar and the console's uploaded art are the same problem - image bytes in, a rooted
			// transient UI texture out - and this file used to carry its own copy of the answer.
			return CampusRemoteImage::DecodeTexture(Bytes.GetData(), Bytes.Num());
		}
	}

	namespace
	{
		/** The cache entry for a name, decoding it on the first ask. Null only for an empty name.
		 *  Both Get (resource handle, MakeCustomVerts) and Brush (FSlateBrush, MakeBox) come through
		 *  here, so one PNG is decoded once however it is drawn. */
		FCacheEntry* Resolve(const TCHAR* Name)
		{
			if (!Name || *Name == TEXT('\0'))
			{
				return nullptr;
			}
			const FString Key(Name);
			if (const TSharedPtr<FCacheEntry>* Found = Cache().Find(Key))
			{
				FCacheEntry& Entry = **Found;
				// Handles can be dropped when the renderer flushes its atlases; re-acquire, never re-decode.
				if (!Entry.Art.Handle.IsValid() && Entry.Brush.GetResourceObject() && FSlateApplication::IsInitialized())
				{
					Entry.Art.Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Entry.Brush);
				}
				return &Entry;
			}

			TSharedPtr<FCacheEntry> Entry = MakeShared<FCacheEntry>();
			Cache().Add(Key, Entry);

			const FString Path = FPaths::ProjectContentDir() / TEXT("CampusUI/MapArt/") + Key + TEXT(".png");
			UTexture2D* Texture = DecodePng(Path);
			if (!Texture || !FSlateApplication::IsInitialized())
			{
				// No picture shipped (or no renderer yet): the plate paints its gradient instead.
				return Entry.Get();
			}
			Entry->Brush.SetResourceObject(Texture);
			Entry->Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Entry->Brush.DrawAs = ESlateBrushDrawType::Image;
			Entry->Art.Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Entry->Brush);
			Entry->Art.Size = FVector2f(float(Texture->GetSizeX()), float(Texture->GetSizeY()));
			return Entry.Get();
		}
	}

	const FArt& Get(const TCHAR* Name)
	{
		static const FArt Missing;
		const FCacheEntry* Entry = Resolve(Name);
		return Entry ? Entry->Art : Missing;
	}

	const FSlateBrush* Brush(const TCHAR* Name)
	{
		const FCacheEntry* Entry = Resolve(Name);
		// GetResourceObject(), not Art.IsValid(): a brush can be drawn as a box the frame before the
		// renderer has handed out a resource handle for it, and a null return here would blank the map.
		return Entry && Entry->Brush.GetResourceObject() ? &Entry->Brush : nullptr;
	}
}

// -------------------------------------------------------------------------------------------------
// SCampusArtPlate

void SCampusArtPlate::Construct(const FArguments& InArgs)
{
	Shape = InArgs._Shape;
	Art = InArgs._Art;
	RemoteKey = InArgs._RemoteKey;
	Tint = InArgs._Tint;
	Border = InArgs._Border;
	BorderWidth = InArgs._BorderWidth;
	AccentEdge = InArgs._AccentEdge;
	AccentEdgeWidth = InArgs._AccentEdgeWidth;
	FocusY = InArgs._FocusY;
	ScrimStrength = InArgs._ScrimStrength;

	if (InArgs._OnClicked.IsBound())
	{
		ChildSlot
		[
			SAssignNew(Inner, SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(InArgs._ContentPadding)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			// PreciseTap: a swipe that starts on the card scrolls its list; only a tap clicks.
			.TouchMethod(EButtonTouchMethod::PreciseTap)
			.OnClicked(InArgs._OnClicked)
			[
				InArgs._Content.Widget
			]
		];
	}
	else
	{
		ChildSlot.Padding(InArgs._ContentPadding)[InArgs._Content.Widget];
	}
}

int32 SCampusArtPlate::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FVector2f Zero = FVector2f::ZeroVector;

	FLinearColor Face = Tint.Get(FLinearColor::White);
	if (Inner.IsValid() && bEnabled)
	{
		// Press and hover land on the picture itself; no extra element, no layout change.
		if (Inner->IsPressed())
		{
			Face = Face * FLinearColor(0.80f, 0.80f, 0.80f, 1.f);
		}
		else if (Inner->IsHovered())
		{
			Face = Face * FLinearColor(1.12f, 1.12f, 1.12f, 1.f);
		}
		Face.A = Tint.Get(FLinearColor::White).A;
	}

	// THE CONSOLE'S PICTURE FIRST, when there is one and it has landed. Get() is what starts the ONE
	// fetch for this key, and it refuses outright for a key the published blob does not name - so a
	// card with no remote art costs a TMap lookup and nothing else.
	const CampusRemoteImage::FImage* Remote = RemoteKey.IsEmpty() ? nullptr : &CampusRemoteImage::Get(RemoteKey);
	const CampusMapArt::FArt& Picture = CampusMapArt::Get(Art);
	if (Remote && Remote->IsReady())
	{
		CampusAngular::DrawShapedImage(OutDrawElements, LayerId, AllottedGeometry, Zero, Size, Shape,
			Remote->Handle, Remote->Size, Face, FocusY);
	}
	else if (Picture.IsValid())
	{
		CampusAngular::DrawShapedImage(OutDrawElements, LayerId, AllottedGeometry, Zero, Size, Shape,
			Picture.Handle, Picture.Size, Face, FocusY);
	}
	else
	{
		// Stand-in: the same dusk ramp the real banners are graded to, so a card with no picture
		// still reads as a card and never as a hole in the screen.
		const FLinearColor Top = FLinearColor(0.07f, 0.09f, 0.17f, 1.f) * FLinearColor(Face.R, Face.G, Face.B, 1.f);
		const FLinearColor Bottom = FLinearColor(0.20f, 0.13f, 0.11f, 1.f) * FLinearColor(Face.R, Face.G, Face.B, 1.f);
		CampusAngular::DrawVerticalFade(OutDrawElements, LayerId, AllottedGeometry, Zero, Size, Shape,
			Top.CopyWithNewOpacity(Face.A), Bottom.CopyWithNewOpacity(Face.A));
	}

	if (ScrimStrength > 0.f)
	{
		CampusAngular::DrawVerticalFade(OutDrawElements, LayerId + 1, AllottedGeometry, Zero, Size, Shape,
			FLinearColor(0.f, 0.f, 0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, FMath::Clamp(ScrimStrength, 0.f, 1.f)));
	}

	const FLinearColor Edge = Border.Get(FLinearColor::Transparent);
	if (Edge.A > 0.f && BorderWidth > 0.f)
	{
		CampusAngular::DrawSlantedPanel(OutDrawElements, LayerId + 2, AllottedGeometry, Zero, Size, Shape,
			FLinearColor::Transparent, bEnabled ? Edge : Edge.CopyWithNewOpacity(Edge.A * 0.4f), BorderWidth);
	}
	if (AccentEdgeWidth > 0.f)
	{
		CampusAngular::DrawAccentEdge(OutDrawElements, LayerId + 3, AllottedGeometry, Zero, Size, Shape,
			AccentEdge.Get(FLinearColor::Transparent), AccentEdgeWidth);
	}
	// Keyboard / gamepad focus has to beat the artwork behind it: accent outline, 2px.
	if (bEnabled && Inner.IsValid() && Inner->HasKeyboardFocus())
	{
		TArray<FVector2f> Points;
		CampusAngular::BuildOutline(FVector2f(1.f, 1.f), Size - FVector2f(2.f, 2.f), Shape, Points);
		Points.Add(Points[0]);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 4, AllottedGeometry.ToPaintGeometry(), Points,
			ESlateDrawEffect::None, CampusAngular::Accent, true, 2.f);
	}
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 5, InWidgetStyle, bParentEnabled);
}

#undef LOCTEXT_NAMESPACE
