// What the player can actually play, and the pictures that sell it.
//
//   * CampusCatalog - the one list of maps and modes. The lobby plate, the map-select screen and
//                     the room dialog all read it, so a new map is one row here and one PNG.
//   * CampusMapArt  - banner art loaded at runtime from Content/CampusUI/MapArt/*.png (staged as
//                     UFS, see DefaultGame.ini). Decoded once, cached, and handed out as a Slate
//                     resource handle so nothing is looked up or allocated per frame. A missing
//                     file is not an error: SCampusArtPlate paints its gradient stand-in instead.
//   * SCampusArtPlate - an angular, tappable card whose fill is that picture, cropped "cover" - or
//                     the one the ADMIN CONSOLE uploaded, when a published content key has one
//                     (Campus/CampusRemoteImage.h); the packaged PNG is then the fallback.
//
// Three levels are built - the college room (L_Match), Campus Grounds (L_CampusMap) and the open
// battle-royale valley (L_VarsityValley); the rest are honest "coming soon" plates that cannot be
// selected. A MODE OFFERS A LIST OF MAPS (FModeEntry::MapIds), not one map: Team Clash and Quick
// Match run on either campus, and battle royale on the valley or on Campus Grounds.

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
	/** How many maps one mode may offer. Two today; bump it when a mode needs a third. */
	constexpr int32 MaxModeMaps = 2;

	/** One row of the map list. Id is what the save file and the room API carry. */
	struct FMapEntry
	{
		/** Persisted in UCampusSave::MatchMap, replicated as ACampusGameState::MatchMap, and translated
		 *  to the API's own name for the same map by CampusBR::ApiMapFor before it is sent anywhere. */
		const TCHAR* Id;
		const TCHAR* Name;
		const TCHAR* Tagline;
		/** Base name under Content/CampusUI/MapArt, without the extension. */
		const TCHAR* Art;
		/** THE LEVEL THIS MAP OPENS, and the only place that is written down: CampusBR::LevelForMap, the
		 *  room host and the end screen all come through here. Null on a "coming soon" plate. */
		const TCHAR* Level;
		bool bPlayable;
		/** The 50-player battle-royale map: drawn as the one wide card, the 4v4 maps as squares. */
		bool bLarge = false;
	};

	/** One row of the mode list, in the order the right-hand column shows them. */
	struct FModeEntry
	{
		const TCHAR* Id;
		const TCHAR* Name;
		/** The maps this mode runs on, best first; unused slots are null. The first PLAYABLE one is
		 *  where a mode lands when the saved choice is not on this list. */
		const TCHAR* MapIds[MaxModeMaps];
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

	/** True when this mode runs on this map. */
	bool ModeHasMap(int32 ModeIndex, FName MapId);
	/** The map index this mode falls back to: the first playable row on its own list. */
	int32 ModeDefaultMap(int32 ModeIndex);
	/** THE MAP A MODE WILL ACTUALLY PLAY: MapId when the mode offers it and it is built, else the
	 *  mode's own first map. The one place a stale saved choice is corrected before a level is opened. */
	int32 MapForMode(FName ModeId, FName MapId);

	/** The level this map opens, or null when it is a plate with nothing behind it. */
	const TCHAR* LevelFor(FName MapId);
	/** The map id of the level this name belongs to (UWorld::GetMapName, which carries a PIE prefix),
	 *  or NAME_None for a level that is not in the list. The level is what the host and every client
	 *  already hold, so it is the one answer both sides can agree on without replication. */
	FName MapIdForLevel(const FString& LevelName);
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

	/** The SAME cached picture as a Slate brush, for the drawing code that goes through
	 *  FSlateDrawElement::MakeBox instead of MakeCustomVerts - the in-match minimap and full map
	 *  (SCampusTouchControls::DrawMap). Null when the PNG is not shipped or not yet decoded, which
	 *  the caller reads as "paint the flat backing instead". Decodes on the first ask, like Get. */
	const FSlateBrush* Brush(const TCHAR* Name);
}

/** An angular card filled with banner art: the map banners and the mode key-art strips.
 *  Paints art (or a gradient stand-in) + a bottom scrim + the border, then its own content.
 *
 *  THE REMOTE PICTURE WINS WHEN THERE IS ONE. Pass a published content key (RemoteKey) and the plate
 *  paints whatever the admin console uploaded for it the moment the bytes land - the packaged PNG in
 *  Art stays the fallback for every other state: no blob, no art published for this key, still
 *  downloading, or a download that failed. One card, two sources, no flicker and no rebuild: the
 *  lookup happens in OnPaint, so a picture that arrives mid-session simply appears. */
class SCampusArtPlate : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusArtPlate)
		: _Shape(FCampusShape())
		, _Art(nullptr)
		, _RemoteKey()
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
		/** A published `content` key ("map/campusmap", "event/diwali_banner"). Empty = packaged art
		 *  only, and no request is ever made for a key the published blob does not name. */
		SLATE_ARGUMENT(FString, RemoteKey)
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
	FString RemoteKey;
	TAttribute<FLinearColor> Tint;
	TAttribute<FLinearColor> Border;
	TAttribute<FLinearColor> AccentEdge;
	float BorderWidth = 1.f;
	float AccentEdgeWidth = 0.f;
	float FocusY = 0.5f;
	float ScrimStrength = 0.f;
	TSharedPtr<SButton> Inner;
};
