// Map / mode select (Slate, lobby level). Free Fire's layout, our content:
//
//   LEFT  ~62%  one horizontal row of map cards that scrolls sideways: the 50-player battle-royale map
//               as the one wide card first, the 4v4 maps as squares after it. Selected = accent border.
//   RIGHT ~38%  mode cards: key-art strip, mode name, the map it runs on, a green "?" that opens
//               the rules, and a clock row on anything time-limited.
//   BOTTOM      RANDOM MAP on the left, the yellow slanted CONFIRM on the right.
//
// Reached from the plate above START in the lobby, and it writes back to that plate. Nothing here
// is decided twice: the rows come from CampusCatalog and the pictures from CampusMapArt.
//
// WHAT THE ADMIN CONSOLE MAY CHANGE ABOUT THIS SCREEN (PHASE_B_CONTRACT B3): which modes and maps
// are offered and in what order (`modes`), the banner pictures (`content`, keyed `map/<client id>`),
// and when battle royale is open (`br_schedule`, IST). It may only ever HIDE or REORDER what this
// APK already has - the compiled catalogue is still the only list of levels this build can open.

#include "Campus/CampusUI.h"
#include "Campus/CampusRankBadge.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusMapSelect"

using namespace CampusStyle;

namespace
{
	/** Banner tints. Colour is never the only signal here - a locked card also carries a padlock
	 *  and the words COMING SOON, and the selected one also gets a border and a tick. */
	const FLinearColor SelectTintOn(1.f, 1.f, 1.f, 1.f);
	const FLinearColor SelectTintOff(0.62f, 0.63f, 0.68f, 1.f);
	const FLinearColor SelectTintLocked(0.54f, 0.56f, 0.62f, 1.f);

	const FLinearColor SelectCardInk(0.024f, 0.031f, 0.043f, 0.96f);
	const FLinearColor SelectShadow(0.f, 0.f, 0.f, 0.85f);

	/** Big italic title laid over artwork: always with the dark offset shadow, or the first bright
	 *  pixel behind it wins. */
	TSharedRef<SWidget> SelectTitle(const FString& Text, int32 Size, const FLinearColor& Color)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(Text.ToUpper()))
			.Font(CampusAngular::Display(Size))
			.ColorAndOpacity(Color)
			.ShadowOffset(FVector2D(3.f, 3.f))
			.ShadowColorAndOpacity(SelectShadow);
	}

	/** The small dark chip used for COMING SOON / SELECTED / the clock row. */
	TSharedRef<SWidget> SelectChip(CampusAngular::EGlyph Glyph, const FText& Label, const FLinearColor& Color, float Alpha = 0.78f)
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(8.f))
			.Fill(FLinearColor(0.f, 0.f, 0.f, Alpha))
			.Border(Color.CopyWithNewOpacity(0.55f))
			.BorderWidth(1.f)
			.ContentPadding(FMargin(12.f, 6.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(Glyph).Size(17.f).Color(Color)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Label).Font(CampusAngular::Display(15)).ColorAndOpacity(Color)
				]
			];
	}
}

// -------------------------------------------------------------------------------------------------
// What the console is allowed to change about this screen (PHASE_B_CONTRACT B3): which modes and maps
// are offered, and in what order. The compiled catalogue is what this APK can actually open, so the
// blob may only HIDE or REORDER its rows - never add one.

FString SCampusLobbyWidget::MapBannerKey(const TCHAR* MapId)
{
	return MapId ? FString(TEXT("map/")) + FString(MapId).ToLower() : FString();
}

namespace
{
	/** The console's running order for one id, or a key past every published sort so an id the blob
	 *  says nothing about keeps its compiled position at the end. */
	int64 SelectSortKey(int32 Published, int32 CompiledIndex)
	{
		return Published == INDEX_NONE ? 1000000 + int64(CompiledIndex) : int64(Published) * 1000 + CompiledIndex;
	}
}

void SCampusLobbyWidget::OrderedMaps(TArray<int32>& Out) const
{
	const UCampusBackend* Api = Backend();
	Out.Reset(CampusCatalog::NumMaps);
	for (int32 Index = 0; Index < CampusCatalog::NumMaps; ++Index)
	{
		if (!Api || Api->IsMapEnabled(CampusCatalog::Map(Index).Id))
		{
			Out.Add(Index);
		}
	}
	if (Out.Num() == 0)
	{
		// EVERYTHING OFF IS A MISTAKE, not an instruction. A screen with no maps on it is the game
		// taken away by a switch nobody meant to flip, so the compiled list stands.
		for (int32 Index = 0; Index < CampusCatalog::NumMaps; ++Index)
		{
			Out.Add(Index);
		}
		return;
	}
	if (!Api)
	{
		return;
	}
	Out.Sort([Api](int32 A, int32 B)
	{
		return SelectSortKey(Api->MapSortOf(CampusCatalog::Map(A).Id), A)
			< SelectSortKey(Api->MapSortOf(CampusCatalog::Map(B).Id), B);
	});
}

void SCampusLobbyWidget::OrderedModes(TArray<int32>& Out) const
{
	const UCampusBackend* Api = Backend();
	Out.Reset(CampusCatalog::NumModes);
	for (int32 Index = 0; Index < CampusCatalog::NumModes; ++Index)
	{
		if (!Api || Api->IsModeEnabled(CampusCatalog::Mode(Index).Id))
		{
			Out.Add(Index);
		}
	}
	if (Out.Num() == 0)
	{
		for (int32 Index = 0; Index < CampusCatalog::NumModes; ++Index)
		{
			Out.Add(Index);
		}
		return;
	}
	if (!Api)
	{
		return;
	}
	Out.Sort([Api](int32 A, int32 B)
	{
		return SelectSortKey(Api->ModeSortOf(CampusCatalog::Mode(A).Id), A)
			< SelectSortKey(Api->ModeSortOf(CampusCatalog::Mode(B).Id), B);
	});
}

// -------------------------------------------------------------------------------------------------
// The lobby plate, above START

TSharedRef<SWidget> SCampusLobbyWidget::BuildModePlate()
{
	const CampusCatalog::FModeEntry& Mode = CampusCatalog::Mode(SelectedMode);
	const CampusCatalog::FMapEntry& PickedMap = CampusCatalog::Map(SelectedMap);
	// RANKED: the long name does not fit the plate's 300 px, and the tier is the thing worth seeing here.
	const bool bRankedPlate = FName(Mode.Id) == FName(TEXT("RankedBR"));
	const UCampusBackend* RankApi = Backend();
	const FString RankTier = RankApi && RankApi->Ranks.bLoaded ? RankApi->Ranks.Br.TierId : FString(TEXT("fresher"));

	// Same 300px width as the START button below it, so the two read as one block.
	return SNew(SBox).WidthOverride(300.f).HeightOverride(88.f)
		[
			SNew(SCampusArtPlate)
			.Shape(FCampusShape::Wedge(20.f, 14.f))
			.Art(Mode.Art)
			.FocusY(0.38f)
			.Tint(FLinearColor(0.52f, 0.54f, 0.60f, 1.f))
			.ScrimStrength(0.45f)
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.AccentEdge(Accent)
			.AccentEdgeWidth(5.f)
			.ContentPadding(FMargin(26.f, 8.f, 16.f, 8.f))
			.OnClicked(FOnClicked::CreateLambda([this]() { OpenMapSelect(); return FReply::Handled(); }))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SelectTitle(bRankedPlate ? TEXT("Ranked BR") : Mode.Name, 26, FLinearColor::White)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString(PickedMap.Name).ToUpper()))
						.Font(CampusAngular::Display(14))
						.ColorAndOpacity(Accent)
						.ShadowOffset(FVector2D(2.f, 2.f))
						.ShadowColorAndOpacity(SelectShadow)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[
					bRankedPlate ? CampusRank::Badge(RankTier, 40.f) : SNullWidget::NullWidget
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Chevron).Size(22.f).Color(Accent)
				]
			]
		];
}

// -------------------------------------------------------------------------------------------------
// Open / confirm

void SCampusLobbyWidget::OpenMapSelect()
{
	// The screen edits a copy: closing with X leaves the lobby exactly as it was.
	PendingMap = SelectedMap;
	PendingMode = SelectedMode;
	HelpMode = INDEX_NONE;
	// A SELECTION THE CONSOLE HAS SINCE SWITCHED OFF cannot be shown as selected, and must not be
	// what CONFIRM writes back: the screen opens on the first row that is still offered instead.
	TArray<int32> Maps;
	TArray<int32> Modes;
	OrderedMaps(Maps);
	OrderedModes(Modes);
	if (!Modes.Contains(PendingMode))
	{
		PendingMode = Modes[0];
	}
	if (!Maps.Contains(PendingMap))
	{
		PendingMap = Maps[0];
	}
	if (const UCampusSave* Save = Profile())
	{
		bPendingRandomMap = Save->bRandomMap;
		bPendingBrNight = Save->bBrNight;
	}
	Show(EScreen::MapSelect);
}

void SCampusLobbyWidget::ConfirmMapSelect()
{
	SelectedMap = CampusCatalog::Map(PendingMap).bPlayable ? PendingMap : SelectedMap;
	SelectedMode = CampusCatalog::Mode(PendingMode).bPlayable ? PendingMode : SelectedMode;
	if (UCampusSave* Save = Profile())
	{
		Save->bRandomMap = bPendingRandomMap;
		Save->bBrNight = bPendingBrNight;
	}
	SaveMatchChoice();
	HelpMode = INDEX_NONE;
	Show(EScreen::Lobby);
}

// -------------------------------------------------------------------------------------------------
// Left column: map banners

TSharedRef<SWidget> SCampusLobbyWidget::BuildMapBanner(int32 Index)
{
	const CampusCatalog::FMapEntry& Entry = CampusCatalog::Map(Index);
	const bool bLocked = !Entry.bPlayable;
	const bool bSelected = !bLocked && PendingMap == Index;
	const FString Name(Entry.Name);

	TSharedRef<SOverlay> Body = SNew(SOverlay)
		// The map name, bottom-right over the scrim, exactly where the eye lands last.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SelectTitle(Name, Entry.bLarge ? 44 : 30, bLocked ? FLinearColor(1.f, 1.f, 1.f, 0.55f) : FLinearColor::White)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Entry.Tagline))
				.AutoWrapText(true)
				.Justification(ETextJustify::Right)
				.Font(CampusAngular::Body(14))
				.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, bLocked ? 0.45f : 0.72f))
				.ShadowOffset(FVector2D(2.f, 2.f))
				.ShadowColorAndOpacity(SelectShadow)
			]
		];

	if (bLocked)
	{
		Body->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		[
			SelectChip(CampusAngular::EGlyph::Lock, LOCTEXT("ComingSoon", "COMING SOON"), FLinearColor(1.f, 1.f, 1.f, 0.62f))
		];
	}
	else if (bSelected)
	{
		Body->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Top)
		[
			SelectChip(CampusAngular::EGlyph::Tick, LOCTEXT("Selected", "SELECTED"), Accent, 0.86f)
		];
	}

	return SNew(SCampusArtPlate)
		.Shape(FCampusShape::Notched(18.f))
		.Art(Entry.Art)
		// The console's banner for this map when it has uploaded one; the packaged PNG otherwise.
		.RemoteKey(MapBannerKey(Entry.Id))
		.FocusY(0.56f)
		.Tint(bLocked ? SelectTintLocked : (bSelected ? SelectTintOn : SelectTintOff))
		.ScrimStrength(bSelected ? 0.30f : 0.42f)
		.Border(bSelected ? Accent : CampusAngular::Hairline)
		.BorderWidth(bSelected ? 3.f : 1.f)
		.AccentEdge(bSelected ? Accent : FLinearColor::Transparent)
		.AccentEdgeWidth(bSelected ? 7.f : 0.f)
		.ContentPadding(FMargin(22.f, 16.f, 26.f, 14.f))
		.OnClicked(FOnClicked::CreateLambda([this, Index, bLocked, Name]()
		{
			// A locked banner still answers the tap - a card that does nothing feels broken.
			if (bLocked)
			{
				Toast(FString::Printf(TEXT("%s is not built yet. Pick one of the maps above it."), *Name));
				return FReply::Handled();
			}
			PendingMap = Index;
			// Keep the mode honest: if it does not run on this map, move to one that does.
			if (!CampusCatalog::ModeHasMap(PendingMode, FName(CampusCatalog::Map(Index).Id)))
			{
				// Only a mode that is still OFFERED can be moved to: a console that switched Quick
				// Match off must not have it selected behind the player's back by a map tap.
				TArray<int32> Offered;
				OrderedModes(Offered);
				for (const int32 Scan : Offered)
				{
					if (CampusCatalog::Mode(Scan).bPlayable && CampusCatalog::ModeHasMap(Scan, FName(CampusCatalog::Map(Index).Id)))
					{
						PendingMode = Scan;
						break;
					}
				}
			}
			Refresh();
			return FReply::Handled();
		}))
		[
			Body
		];
}

// -------------------------------------------------------------------------------------------------
// Right column: mode cards

TSharedRef<SWidget> SCampusLobbyWidget::BuildModeCard(int32 Index)
{
	const CampusCatalog::FModeEntry& Entry = CampusCatalog::Mode(Index);
	const bool bLocked = !Entry.bPlayable;
	const bool bSelected = !bLocked && PendingMode == Index;
	// The map under the mode name is the one THIS CARD would play: the pending pick when the mode
	// offers it, else the mode's own first map. A card that named a map the mode cannot run on was
	// the whole reason this is a list and not one id.
	const int32 MapIndex = CampusCatalog::ModeHasMap(Index, FName(CampusCatalog::Map(PendingMap).Id))
		? PendingMap : CampusCatalog::ModeDefaultMap(Index);
	const FString MapName(CampusCatalog::Map(MapIndex).Name);
	const FString Name(Entry.Name);

	TSharedRef<SVerticalBox> Text = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Name.ToUpper()))
			.Font(CampusAngular::Display(29))
			.ColorAndOpacity(bLocked ? FLinearColor(1.f, 1.f, 1.f, 0.42f) : FLinearColor::White)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, -2.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(MapName.ToUpper()))
			.Font(CampusAngular::Display(15))
			.ColorAndOpacity(bLocked ? FLinearColor(1.f, 1.f, 1.f, 0.30f) : Muted)
		];

	// THE CLOCK ROW, from either of two places. A compiled window (FModeEntry::Window) is a card that
	// has always been time-limited; the BATTLE-ROYALE SCHEDULE is the console's, published in the
	// `modes` blob as IST wall-clock rules, and it is only worth the row when the window is SHUT - a
	// mode that can be played right now should say nothing at all.
	const UCampusBackend* Api = Backend();
	const bool bIsBattleRoyale = FName(Entry.Id) == FName(TEXT("BattleRoyale")) || FName(Entry.Id) == FName(TEXT("RankedBR"));
	FString WindowText = Entry.Window ? FString(Entry.Window) : FString();
	if (WindowText.IsEmpty() && bIsBattleRoyale && Api && !Api->IsBattleRoyaleOpenNow())
	{
		WindowText = Api->BattleRoyaleWindowText();
	}
	if (!WindowText.IsEmpty())
	{
		// Time-limited card: the clock row says when, the padlock says not now.
		Text->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Clock).Size(17.f)
				.Color(FLinearColor(1.f, 1.f, 1.f, 0.42f))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(WindowText)).Font(CampusAngular::Body(13))
				.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.42f)).AutoWrapText(true)
			]
		];
	}

	// BATTLE ROYALE carries its own DAY / NIGHT choice. It sits on the card (not in a submenu) because
	// it is the only per-mode setting there is, and it is drawn ABOVE the card so the tap toggles the
	// lighting instead of re-selecting the mode - the same trick the green "?" uses.
	const bool bHasDayNight = !bLocked && bIsBattleRoyale;
	if (bHasDayNight)
	{
		const bool bNight = bPendingBrNight;
		Text->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SBox).MinDesiredHeight(46.f).HAlign(HAlign_Left)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Slanted(10.f))
				.Fill(bNight ? FLinearColor(0.05f, 0.06f, 0.14f, 0.92f) : FLinearColor(0.12f, 0.09f, 0.02f, 0.92f))
				.Border(Accent.CopyWithNewOpacity(0.75f))
				.BorderWidth(1.f)
				.ContentPadding(FMargin(14.f, 8.f, 16.f, 8.f))
				.OnClicked(FOnClicked::CreateLambda([this]()
				{
					bPendingBrNight = !bPendingBrNight;
					Refresh();
					return FReply::Handled();
				}))
				[
					SNew(SHorizontalBox)
					// Never colour alone: the glyph changes with the words, so this reads at a glance
					// and still reads for a player who cannot tell the two fills apart.
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
					[
						SNew(SCampusGlyph)
						.Glyph(bNight ? CampusAngular::EGlyph::Gem : CampusAngular::EGlyph::Coin)
						.Size(18.f)
						.Color(bNight ? FLinearColor(0.72f, 0.78f, 1.f, 1.f) : Accent)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(bNight ? LOCTEXT("BrNight", "NIGHT") : LOCTEXT("BrDay", "DAY"))
						.Font(CampusAngular::Display(17))
						.ColorAndOpacity(bNight ? FLinearColor(0.82f, 0.86f, 1.f, 1.f) : Accent)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 2.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("BrTapToSwap", "TAP TO SWAP"))
						.Font(CampusAngular::Display(11)).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.42f))
					]
				]
			]
		];
	}

	TSharedRef<SOverlay> Strip = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusArtPlate)
			.Shape(FCampusShape::Notched(14.f))
			.Art(Entry.Art)
			.FocusY(0.44f)
			.Tint(bLocked ? SelectTintLocked : (bSelected ? SelectTintOn : SelectTintOff))
			.ScrimStrength(0.28f)
		];
	if (bLocked)
	{
		Strip->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Lock).Size(40.f)
			.Color(FLinearColor(1.f, 1.f, 1.f, 0.55f))
		];
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(bSelected ? FLinearColor(0.10f, 0.085f, 0.028f, 0.96f) : SelectCardInk)
			.Border(bSelected ? Accent : CampusAngular::Hairline)
			.BorderWidth(bSelected ? 3.f : 1.f)
			.AccentEdge(bSelected ? Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bSelected ? 6.f : 0.f)
			.ContentPadding(FMargin(0.f))
			.OnClicked(FOnClicked::CreateLambda([this, Index, bLocked, Name]()
			{
				if (bLocked)
				{
					Toast(FString::Printf(TEXT("%s is not live yet. Tap ? to see what it will be."), *Name));
					return FReply::Handled();
				}
				PendingMode = Index;
				// The map only moves when this mode cannot run on the one already picked: choosing
				// BATTLE ROYALE must not throw away a Campus Grounds pick that it honours.
				if (!CampusCatalog::ModeHasMap(Index, FName(CampusCatalog::Map(PendingMap).Id)))
				{
					PendingMap = CampusCatalog::ModeDefaultMap(Index);
				}
				Refresh();
				return FReply::Handled();
			}))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(104.f)[Strip]
				]
				+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center).Padding(FMargin(20.f, 8.f, 64.f, 10.f))
				[
					Text
				]
			]
		]
		// The green "?" sits above the card, so it takes the tap instead of selecting the mode.
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(10.f)
		[
			SNew(SBox).WidthOverride(46.f).HeightOverride(46.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Clipped(10.f))
				.Fill(CampusAngular::Go)
				.Border(FLinearColor(1.f, 1.f, 1.f, 0.35f))
				.BorderWidth(1.f)
				.ContentPadding(FMargin(0.f))
				.OnClicked(FOnClicked::CreateLambda([this, Index]()
				{
					HelpMode = HelpMode == Index ? INDEX_NONE : Index;
					Refresh();
					return FReply::Handled();
				}))
				[
					SNew(STextBlock).Text(LOCTEXT("HelpBadge", "?"))
					.Font(CampusAngular::Display(23)).ColorAndOpacity(FLinearColor::White)
				]
			]
		];
}

// -------------------------------------------------------------------------------------------------
// The screen

TSharedRef<SWidget> SCampusLobbyWidget::BuildMapSelect()
{
	// THE MODE SCREEN (Docs/visual/modes-ranked-plan.html): one grid of cards, each card a MODE ON A MAP.
	// Gold border = ranked, blue tag = 50-player battle royale, green tag = 4v4. Our own arrangement:
	//
	//   [ RANKED BR       ] [ CLASSIC BR valley  ] [ RANKED 4v4 (soon)   ] [ CLASH campus grounds ]
	//   [ (tall)          ] [ CLASSIC BR grounds ] [ CLASH campus        ] [ FESTIVAL (soon)      ]
	//   CUSTOM ROOM                     DAY / NIGHT                                           CONFIRM
	const UCampusBackend* Api = Backend();
	static const FLinearColor RankedGold(0.95f, 0.72f, 0.24f, 1.f);
	static const FLinearColor BrBlue(0.56f, 0.65f, 1.f, 1.f);
	static const FLinearColor ClashGreen(0.37f, 0.83f, 0.65f, 1.f);

	// One card: a mode on a map. Locked cards (not built yet, or switched off in the console) say so
	// instead of selecting anything.
	auto Card = [this, Api](const TCHAR* ModeId, const TCHAR* MapId, const FText& Title, const FText& Sub,
		const TCHAR* Art, const FText& CardTag, const FLinearColor& CardTagColor, bool bRankedCard, bool bComingSoon,
		TSharedRef<SWidget> Extra) -> TSharedRef<SWidget>
	{
		const int32 ModeIndex = ModeId ? CampusCatalog::FindMode(FName(ModeId)) : INDEX_NONE;
		const int32 MapIndex = MapId ? CampusCatalog::FindMap(FName(MapId)) : INDEX_NONE;
		const bool bOffered = ModeIndex != INDEX_NONE && MapIndex != INDEX_NONE
			&& CampusCatalog::Mode(ModeIndex).bPlayable && CampusCatalog::Map(MapIndex).bPlayable
			&& (!Api || (Api->IsModeEnabled(ModeId) && Api->IsMapEnabled(MapId)));
		const bool bLocked = bComingSoon || !bOffered;
		const bool bOn = !bLocked && PendingMode == ModeIndex && PendingMap == MapIndex;
		const FLinearColor Frame = bOn ? CampusAngular::Accent : (bRankedCard ? RankedGold : CampusAngular::Hairline);
		const FText Title2 = Title;
		return SNew(SCampusArtPlate)
			.Shape(FCampusShape::Notched(14.f))
			.Art(Art)
			.FocusY(0.42f)
			.Tint(bLocked ? FLinearColor(0.38f, 0.40f, 0.45f, 1.f) : FLinearColor::White)
			.ScrimStrength(0.55f)
			.Border(Frame)
			.BorderWidth(bOn ? 3.f : (bRankedCard ? 2.f : 1.f))
			.AccentEdge(bOn ? CampusAngular::Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bOn ? 5.f : 0.f)
			.ContentPadding(FMargin(14.f, 10.f))
			.OnClicked(FOnClicked::CreateLambda([this, bLocked, ModeIndex, MapIndex, Title2]()
			{
				if (bLocked)
				{
					Toast(Title2.ToString() + TEXT(": coming soon"));
					return FReply::Handled();
				}
				PendingMode = ModeIndex;
				PendingMap = MapIndex;
				Refresh();
				return FReply::Handled();
			}))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(6.f)).Fill(bLocked ? FLinearColor(0.f, 0.f, 0.f, 0.6f) : CardTagColor)
						.ContentPadding(FMargin(8.f, 1.f))
						[
							SNew(STextBlock).Text(bComingSoon ? LOCTEXT("TagSoon", "SOON") : CardTag)
							.Font(CampusAngular::Display(12)).ColorAndOpacity(bLocked ? CardTagColor : FLinearColor(0.05f, 0.05f, 0.06f))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						bLocked ? StaticCastSharedRef<SWidget>(SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Lock).Size(18.f).Color(FLinearColor(1.f, 1.f, 1.f, 0.6f)))
							: (bOn ? StaticCastSharedRef<SWidget>(SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Tick).Size(20.f).Color(CampusAngular::Accent))
								: SNullWidget::NullWidget)
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
				+ SVerticalBox::Slot().AutoHeight()[Extra]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Title).Font(CampusAngular::Display(22)).ColorAndOpacity(FLinearColor::White)
					.ShadowOffset(FVector2D(2.f, 2.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Sub).Font(CampusAngular::Body(13)).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.78f))
					.ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f))
				]
			];
	};

	// The tall ranked card carries the player's own tier: the badge, the name, and the bar.
	const FCampusRank Rank = Api ? Api->Ranks.Br : FCampusRank();
	const FText RankLine = Api && Api->Ranks.bLoaded
		? FText::Format(LOCTEXT("RankLine", "YOUR TIER  {0}  -  {1} RP"), FText::FromString(Rank.Label.ToUpper()), FText::AsNumber(Rank.Rp))
		: LOCTEXT("RankLineLoading", "YOUR TIER");
	TSharedRef<SWidget> RankBlock = SNew(SVerticalBox)
		// HAlign_Left: a fill slot hands the badge's SBox the whole card width, and the square stretches.
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f, 0.f, 0.f, 10.f)
		[
			CampusRank::Badge(Rank.TierId, 70.f)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(RankLine).Font(CampusAngular::Display(14)).ColorAndOpacity(CampusRank::TierColor(Rank.TierId))
			.ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 8.f)
		[
			SNew(SBox).WidthOverride(200.f).HeightOverride(6.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[SNew(SCampusAngularPanel).Fill(FLinearColor(1.f, 1.f, 1.f, 0.18f))]
				+ SOverlay::Slot().HAlign(HAlign_Left)
				[
					SNew(SBox).WidthOverride(FMath::Max(3.f, 200.f * Rank.Progress))
					[
						SNew(SCampusAngularPanel).Fill(CampusRank::TierColor(Rank.TierId))
					]
				]
			]
		];

	const FText Br50 = LOCTEXT("Tag50", "50 PLAYERS");
	const FText Tag4v4 = LOCTEXT("Tag4v4", "4 v 4");
	TSharedRef<SWidget> Grid = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.15f).Padding(0.f, 0.f, 10.f, 0.f)
		[
			Card(TEXT("RankedBR"), TEXT("VarsityValley"), LOCTEXT("CardRankedBr", "RANKED BATTLE ROYALE"),
				LOCTEXT("CardRankedBrSub", "Varsity Valley  -  placement and kills earn RP"), TEXT("map_valley"),
				LOCTEXT("TagRanked", "RANKED"), RankedGold, true, false, RankBlock)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 10.f)
			[
				Card(TEXT("BattleRoyale"), TEXT("VarsityValley"), LOCTEXT("CardBrValley", "CLASSIC BR"),
					LOCTEXT("CardBrValleySub", "Varsity Valley"), TEXT("map_valley"), Br50, BrBlue, false, false, SNullWidget::NullWidget)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				Card(TEXT("BattleRoyale"), TEXT("CampusMap"), LOCTEXT("CardBrGrounds", "CLASSIC BR"),
					LOCTEXT("CardBrGroundsSub", "Campus Grounds"), TEXT("map_campusmap"), Br50, BrBlue, false, false, SNullWidget::NullWidget)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 10.f)
			[
				Card(nullptr, nullptr, LOCTEXT("CardRanked4v4", "RANKED 4v4"),
					LOCTEXT("CardRanked4v4Sub", "your squad vs a random squad"), TEXT("mode_clash"),
					LOCTEXT("TagRanked2", "RANKED"), RankedGold, true, true, SNullWidget::NullWidget)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				Card(TEXT("QuickMatch"), TEXT("Classic"), LOCTEXT("CardClashCampus", "CAMPUS CLASH"),
					LOCTEXT("CardClashCampusSub", "Campus  -  4v4 for fun"), TEXT("map_campus"), Tag4v4, ClashGreen, false, false, SNullWidget::NullWidget)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 0.f, 0.f, 10.f)
			[
				Card(TEXT("QuickMatch"), TEXT("CampusMap"), LOCTEXT("CardClashGrounds", "CAMPUS CLASH"),
					LOCTEXT("CardClashGroundsSub", "Campus Grounds  -  4v4 for fun"), TEXT("map_campusmap"), Tag4v4, ClashGreen, false, false, SNullWidget::NullWidget)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				Card(nullptr, nullptr, LOCTEXT("CardFestival", "FESTIVAL MODE"),
					LOCTEXT("CardFestivalSub", "during Diwali and Christmas"), TEXT("mode_quick"),
					LOCTEXT("TagEvent", "EVENT"), FLinearColor(1.f, 0.44f, 0.78f, 1.f), false, true, SNullWidget::NullWidget)
			]
		];

	// DAY / NIGHT is the battle royale's own preference; it is only shown when a BR card is picked.
	const FName PendingModeId(CampusCatalog::Mode(PendingMode).Id);
	const bool bBrPicked = PendingModeId == FName(TEXT("BattleRoyale")) || PendingModeId == FName(TEXT("RankedBR"));
	TSharedRef<SWidget> DayNight = bBrPicked
		? StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				CampusStyle::Pill(LOCTEXT("PickDay", "DAY"), !bPendingBrNight,
					FOnClicked::CreateLambda([this]() { bPendingBrNight = false; Refresh(); return FReply::Handled(); }), FVector2D(110.f, 48.f))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				CampusStyle::Pill(LOCTEXT("PickNight", "NIGHT"), bPendingBrNight,
					FOnClicked::CreateLambda([this]() { bPendingBrNight = true; Refresh(); return FReply::Handled(); }), FVector2D(110.f, 48.f))
			])
		: SNullWidget::NullWidget;

	// CUSTOM ROOM: a Room Card, a code, friends and enemies you invite (Team Clash). Picks it and confirms.
	const int32 RoomMode = CampusCatalog::FindMode(FName(TEXT("TeamClash")));
	TSharedRef<SWidget> CustomRoom = RoomMode == INDEX_NONE ? SNullWidget::NullWidget
		: StaticCastSharedRef<SWidget>(Button(LOCTEXT("CustomRoomBtn", "CUSTOM ROOM"),
			FOnClicked::CreateLambda([this, RoomMode]()
			{
				PendingMode = RoomMode;
				PendingMap = CampusCatalog::ModeDefaultMap(RoomMode);
				ConfirmMapSelect();
				return FReply::Handled();
			}), EButton::Dark, 18, FVector2D(230.f, 64.f)));

	const int32 HelpFor = PendingMode;
	TSharedRef<SOverlay> Screen = SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.86f)]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(26.f, 16.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
					[
						SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(9.f)).Fill(Accent)
						[
							SNew(SBox).WidthOverride(16.f).HeightOverride(32.f)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Label(LOCTEXT("ModeSelectTitle", "SELECT MODE"), 32, FLinearColor::White)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
					[
						Api && Api->Ranks.bLoaded && !Api->Ranks.SeasonName.IsEmpty()
							? StaticCastSharedRef<SWidget>(Label(FText::FromString(Api->Ranks.SeasonName.ToUpper()), 16, RankedGold))
							: SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Chevron,
							FOnClicked::CreateLambda([this, HelpFor]() { HelpMode = HelpFor; Refresh(); return FReply::Handled(); }), 56.f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close,
							FOnClicked::CreateLambda([this]()
							{
								HelpMode = INDEX_NONE;
								Show(EScreen::Lobby);
								return FReply::Handled();
							}), 62.f)
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)[Grid]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CustomRoom]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)[DayNight]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						CampusStyle::StartButton(LOCTEXT("ConfirmBtn", "CONFIRM"),
							FOnClicked::CreateLambda([this]() { ConfirmMapSelect(); return FReply::Handled(); }),
							FVector2D(312.f, 88.f))
					]
				]
			]
		];

	// The rules sheet for the picked card's mode: a plain panel over the screen, dismissed by its own X.
	if (HelpMode >= 0 && HelpMode < CampusCatalog::NumModes)
	{
		const CampusCatalog::FModeEntry& Entry = CampusCatalog::Mode(HelpMode);
		Screen->AddSlot()
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[Backdrop(0.55f)]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(640.f)
				[
					CampusStyle::Panel(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								Label(FText::FromString(FString(Entry.Name).ToUpper()), 28, FLinearColor::White)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								CampusStyle::IconButton(CampusAngular::EGlyph::Close,
									FOnClicked::CreateLambda([this]()
									{
										HelpMode = INDEX_NONE;
										Refresh();
										return FReply::Handled();
									}), 56.f)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(Entry.Rules))
							.Font(CampusAngular::Body(17))
							.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.86f))
							.AutoWrapText(true)
						],
						FMargin(32.f, 24.f), 20.f, true)
				]
			]
		];
	}
	return Screen;
}

#undef LOCTEXT_NAMESPACE
