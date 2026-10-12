#include "Campus/CampusMapArt.h"

#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "CampusMapArt"

// -------------------------------------------------------------------------------------------------
// Catalog

namespace CampusCatalog
{
	// One real level (L_Match, the college campus). The other two are deliberately empty plates,
	// not faked screenshots - see Scripts/render_map_art.py.
	const FMapEntry Maps[] = {
		{ TEXT("Classic"), TEXT("Campus"),      TEXT("The college  ·  quad, corridors and rooftops"), TEXT("map_campus"), true },
		{ TEXT("Hostel"),  TEXT("Hostel Block"), TEXT("Coming soon"),                                  TEXT("map_hostel"), false },
		{ TEXT("Arena"),   TEXT("Sports Arena"), TEXT("Coming soon"),                                  TEXT("map_arena"),  false },
	};
	const int32 NumMaps = UE_ARRAY_COUNT(Maps);

	const FModeEntry Modes[] = {
		{ TEXT("TeamClash"), TEXT("Team Clash"), TEXT("Classic"), TEXT("mode_clash"),
			TEXT("Two squads, one campus. You pick how many rounds (4-7) and how long each one runs; every round is "
			     "played and the squad that wins the most takes the match. Everyone respawns between rounds. "
			     "Hosting costs one Room Card and gives you a 6-letter code to share."),
			nullptr, true },
		{ TEXT("QuickMatch"), TEXT("Quick Match"), TEXT("Classic"), TEXT("mode_quick"),
			TEXT("One tap into the room already open on your network. Free, no Room Card, no code to type - "
			     "the match starts on the host's settings."),
			nullptr, true },
		// Placeholder schedule: the window text below is UI only until a real rotation exists.
		{ TEXT("NightRaid"), TEXT("Night Raid"), TEXT("Classic"), TEXT("mode_locked"),
			TEXT("Not live yet. Team Clash after dark, on a shorter clock. The daily window below is a placeholder "
			     "until the rotation is switched on."),
			TEXT("Opens daily 18:00 - 22:00"), false },
		{ TEXT("BattleRoyale"), TEXT("Battle Royale"), TEXT("Arena"), TEXT("mode_locked"),
			TEXT("Not live yet. Up to 50 players on one open map, last squad standing. Needs a map we have not built."),
			nullptr, false },
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
			IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
			if (!Png.IsValid() || !Png->SetCompressed(Bytes.GetData(), Bytes.Num()))
			{
				return nullptr;
			}
			TArray64<uint8> Raw;
			if (!Png->GetRaw(ERGBFormat::BGRA, 8, Raw))
			{
				return nullptr;
			}
			const int32 Width = Png->GetWidth();
			const int32 Height = Png->GetHeight();
			if (Width <= 0 || Height <= 0 || Raw.Num() < int64(Width) * Height * 4)
			{
				return nullptr;
			}
			UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
			if (!Texture)
			{
				return nullptr;
			}
			Texture->SRGB = true;
			Texture->NeverStream = true;
			Texture->LODGroup = TEXTUREGROUP_UI;
			Texture->Filter = TF_Bilinear;
			Texture->AddressX = TA_Clamp;
			Texture->AddressY = TA_Clamp;
			FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
			void* Pixels = Mip.BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Pixels, Raw.GetData(), int64(Width) * Height * 4);
			Mip.BulkData.Unlock();
			Texture->UpdateResource();
			Texture->AddToRoot();
			return Texture;
		}
	}

	const FArt& Get(const TCHAR* Name)
	{
		static const FArt Missing;
		if (!Name || *Name == TEXT('\0'))
		{
			return Missing;
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
			return Entry.Art;
		}

		TSharedPtr<FCacheEntry> Entry = MakeShared<FCacheEntry>();
		Cache().Add(Key, Entry);

		const FString Path = FPaths::ProjectContentDir() / TEXT("CampusUI/MapArt/") + Key + TEXT(".png");
		UTexture2D* Texture = DecodePng(Path);
		if (!Texture || !FSlateApplication::IsInitialized())
		{
			// No picture shipped (or no renderer yet): the plate paints its gradient instead.
			return Entry->Art;
		}
		Entry->Brush.SetResourceObject(Texture);
		Entry->Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
		Entry->Brush.DrawAs = ESlateBrushDrawType::Image;
		Entry->Art.Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(Entry->Brush);
		Entry->Art.Size = FVector2f(float(Texture->GetSizeX()), float(Texture->GetSizeY()));
		return Entry->Art;
	}
}

// -------------------------------------------------------------------------------------------------
// SCampusArtPlate

void SCampusArtPlate::Construct(const FArguments& InArgs)
{
	Shape = InArgs._Shape;
	Art = InArgs._Art;
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

	const CampusMapArt::FArt& Picture = CampusMapArt::Get(Art);
	if (Picture.IsValid())
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
