// What the player can actually play, and the pictures that sell it.
//
//   * CampusCatalog - the one list of maps and modes. The lobby plate, the map-select screen and
//                     the room dialog all read it, so a new map is one row here and one PNG.
//   * CampusMapArt  - banner art loaded at runtime from Content/CampusUI/MapArt/*.png (staged as
//                     UFS, see DefaultGame.ini). Decoded once, cached, and handed out as a Slate
//                     resource handle so nothing is looked up or allocated per frame. A missing
//                     file is not an error: SCampusArtPlate paints its gradient stand-in instead.
//   * SCampusArtPlate - an angular, tappable card whose fill is that picture, cropped "cover".
//
// Only the campus (L_Match) exists, so it is the only playable map; the rest are honest
// "coming soon" plates that cannot be selected.

#pragma once

#include "CoreMinimal.h"
#include "Campus/CampusAngular.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SButton;

namespace CampusCatalog
{
	/** One row of the map list. Id is what the save file and the room API carry. */
	struct FMapEntry
	{
		/** Persisted in UCampusSave::MatchMap and sent to POST /v1/rooms (the API folds it to "classic"). */
		const TCHAR* Id;
		const TCHAR* Name;
		const TCHAR* Tagline;
		/** Base name under Content/CampusUI/MapArt, without the extension. */
		const TCHAR* Art;
		bool bPlayable;
	};

	/** One row of the mode list, in the order the right-hand column shows them. */
	struct FModeEntry
	{
		const TCHAR* Id;
		const TCHAR* Name;
		/** The map this mode runs on; shown in grey under the mode name. */
		const TCHAR* MapId;
		const TCHAR* Art;
		/** What the green "?" badge explains. */
		const TCHAR* Rules;
		/** Non-empty = a time-limited card: shows a clock row instead of a plain lock. */
		const TCHAR* Window;
		bool bPlayable;
	};

	extern const FMapEntry Maps[];
	extern const int32 NumMaps;
	extern const FModeEntry Modes[];
	extern const int32 NumModes;

	/** Index of Id, or INDEX_NONE. */
	int32 FindMap(FName Id);
	int32 FindMode(FName Id);
	/** The first playable row: where an unknown or retired saved choice lands. */
	int32 DefaultMap();
	int32 DefaultMode();
	/** FindMap/FindMode clamped to something playable, so the UI never shows a locked selection. */
	int32 PlayableMap(FName Id);
	int32 PlayableMode(FName Id);

	const FMapEntry& Map(int32 Index);
	const FModeEntry& Mode(int32 Index);
}

namespace CampusMapArt
{
	/** A decoded banner: the Slate handle to draw with and the pixel size the crop needs. */
	struct FArt
	{
		FSlateResourceHandle Handle;
		FVector2f Size = FVector2f::ZeroVector;
		bool IsValid() const { return Handle.IsValid() && Size.X > 0.f; }
	};

	/** Decodes Content/CampusUI/MapArt/<Name>.png once and caches it. Never null; an entry whose
	 *  file is missing simply reports !IsValid() and the card falls back to its gradient. */
	const FArt& Get(const TCHAR* Name);
}

/** An angular card filled with banner art: the map banners and the mode key-art strips.
 *  Paints art (or a gradient stand-in) + a bottom scrim + the border, then its own content. */
class SCampusArtPlate : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusArtPlate)
		: _Shape(FCampusShape())
		, _Art(nullptr)
		, _Tint(FLinearColor::White)
		, _Border(CampusAngular::Hairline)
		, _BorderWidth(1.f)
		, _AccentEdge(FLinearColor::Transparent)
		, _AccentEdgeWidth(0.f)
		, _FocusY(0.5f)
		, _ScrimStrength(0.f)
		, _ContentPadding(FMargin(0.f))
		{}
		SLATE_ARGUMENT(FCampusShape, Shape)
		/** Base name under Content/CampusUI/MapArt; null paints the gradient only. */
		SLATE_ARGUMENT(const TCHAR*, Art)
		/** Multiplies the picture: dim an unselected card, grey out a locked one. */
		SLATE_ATTRIBUTE(FLinearColor, Tint)
		SLATE_ATTRIBUTE(FLinearColor, Border)
		SLATE_ARGUMENT(float, BorderWidth)
		SLATE_ATTRIBUTE(FLinearColor, AccentEdge)
		SLATE_ARGUMENT(float, AccentEdgeWidth)
		/** Which band of the picture survives the crop: 0 the top, 1 the bottom. */
		SLATE_ARGUMENT(float, FocusY)
		/** 0..1 extra darkening down the lower half, for titles laid over the art. */
		SLATE_ARGUMENT(float, ScrimStrength)
		SLATE_ARGUMENT(FMargin, ContentPadding)
		SLATE_EVENT(FOnClicked, OnClicked)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	FCampusShape Shape;
	const TCHAR* Art = nullptr;
	TAttribute<FLinearColor> Tint;
	TAttribute<FLinearColor> Border;
	TAttribute<FLinearColor> AccentEdge;
	float BorderWidth = 1.f;
	float AccentEdgeWidth = 0.f;
	float FocusY = 0.5f;
	float ScrimStrength = 0.f;
	TSharedPtr<SButton> Inner;
};
