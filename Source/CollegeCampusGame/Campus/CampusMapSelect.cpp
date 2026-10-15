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

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
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
// The lobby plate, above START

TSharedRef<SWidget> SCampusLobbyWidget::BuildModePlate()
{
	const CampusCatalog::FModeEntry& Mode = CampusCatalog::Mode(SelectedMode);
	const CampusCatalog::FMapEntry& PickedMap = CampusCatalog::Map(SelectedMap);

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
						SelectTitle(Mode.Name, 26, FLinearColor::White)
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
				for (int32 Scan = 0; Scan < CampusCatalog::NumModes; ++Scan)
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

	if (Entry.Window && *Entry.Window)
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
				SNew(STextBlock).Text(FText::FromString(Entry.Window)).Font(CampusAngular::Body(13))
				.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.42f))
			]
		];
	}

	// BATTLE ROYALE carries its own DAY / NIGHT choice. It sits on the card (not in a submenu) because
	// it is the only per-mode setting there is, and it is drawn ABOVE the card so the tap toggles the
	// lighting instead of re-selecting the mode - the same trick the green "?" uses.
	const bool bHasDayNight = !bLocked && FName(Entry.Id) == FName(TEXT("BattleRoyale"));
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
	// Map cards in one row that scrolls sideways: the large battle-royale map first as the one wide
	// card, then the 4v4 maps as squares. Fixed sizes, so the art is cropped to a known shape and
	// swapping the selection never reflows the row.
	constexpr float CardHeight = 500.f;
	constexpr float WideWidth = 800.f;
	/** A 4v4 card is SQUARE. Named, because reading `WidthOverride(CardHeight)` below looked like a
	 *  copy-paste slip every time and the next reader was going to "fix" it. */
	constexpr float SquareWidth = CardHeight;
	TSharedRef<SScrollBox> Banners = SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		.ScrollBarVisibility(EVisibility::Collapsed)
		.AllowOverscroll(EAllowOverscroll::Yes);
	bool bFirst = true;
	for (const bool bLargePass : { true, false })
	{
		for (int32 Index = 0; Index < CampusCatalog::NumMaps; ++Index)
		{
			if (CampusCatalog::Map(Index).bLarge != bLargePass)
			{
				continue;
			}
			Banners->AddSlot().VAlign(VAlign_Top).Padding(bFirst ? 0.f : 12.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(bLargePass ? WideWidth : SquareWidth).HeightOverride(CardHeight)[BuildMapBanner(Index)]
			];
			bFirst = false;
		}
	}

	TSharedRef<SScrollBox> ModeList = SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed);
	for (int32 Index = 0; Index < CampusCatalog::NumModes; ++Index)
	{
		ModeList->AddSlot().Padding(0.f, Index == 0 ? 0.f : 10.f, 0.f, 0.f)
		[
			// Fixed height: the list must not reflow when the selection moves between cards, and the
			// tallest card (BATTLE ROYALE, with its DAY / NIGHT chip) sets it for all of them.
			SNew(SBox).HeightOverride(240.f)[BuildModeCard(Index)]
		];
	}

	// A mode can offer more than one map now (FModeEntry::MapIds), so a random pick finally HAS
	// something to choose between - but nothing rolls it yet: the lobby plate and the room dialog both
	// name the map before it is played, and a roll behind them would make them lie. Shown, disabled,
	// and it says so; the plumbing (UCampusSave::bRandomMap) is already here for whoever wires it.
	const bool bRandomPossible = false;
	const bool bRandomOn = bRandomPossible && bPendingRandomMap;

	TSharedRef<SWidget> RandomToggle = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).MinDesiredHeight(48.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Slanted(10.f))
				.Fill(FLinearColor(0.f, 0.f, 0.f, 0.55f))
				.Border(bRandomOn ? Accent : CampusAngular::Hairline)
				.BorderWidth(bRandomOn ? 2.f : 1.f)
				.ContentPadding(FMargin(18.f, 10.f, 20.f, 10.f))
				.IsEnabled(bRandomPossible)
				.OnClicked(FOnClicked::CreateLambda([this]()
				{
					bPendingRandomMap = !bPendingRandomMap;
					Refresh();
					return FReply::Handled();
				}))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SBox).WidthOverride(24.f).HeightOverride(24.f)
						[
							SNew(SCampusAngularPanel)
							.Shape(FCampusShape::Notched(5.f))
							.Fill(bRandomOn ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.10f))
							.Border(CampusAngular::Hairline)
							.BorderWidth(1.f)
							[
								bRandomOn
									? StaticCastSharedRef<SWidget>(SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Tick)
										.Size(18.f).Color(FLinearColor::Black))
									: SNullWidget::NullWidget
							]
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("RandomMap", "RANDOM MAP"))
						.Font(CampusAngular::Display(20))
						.ColorAndOpacity(bRandomOn ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.78f))
					]
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f, 0.f, 0.f)
		[
			bRandomPossible
				? SNullWidget::NullWidget
				: StaticCastSharedRef<SWidget>(SNew(STextBlock)
					.Text(LOCTEXT("RandomSoon", "not live yet - pick a map on the left"))
					.Font(CampusAngular::Body(14))
					.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.38f)))
		];

	TSharedRef<SOverlay> Screen = SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.84f)]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(26.f, 16.f))
			[
				SNew(SVerticalBox)
				// Header: the accent flash, the title, and the X that drops the pending choice.
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
						Label(LOCTEXT("MapSelectTitle", "SELECT MAP"), 32, FLinearColor::White)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
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
				// 62 / 38, the owner's split.
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.62f)[Banners]
					+ SHorizontalBox::Slot().FillWidth(0.38f).Padding(14.f, 0.f, 0.f, 0.f)[ModeList]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[RandomToggle]
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

	// The "?" sheet: a plain panel over the screen, dismissed by its own X or by tapping the badge
	// again. It never navigates, so the pending choice behind it survives.
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
