// WARDROBE and LOBBY DECORATION (Slate, lobby level). Two screens, one file, because they are the
// two halves of one feature and they share every rule below.
//
//   WARDROBE (EScreen::Wardrobe) - PUBG's closet. LEFT 45 % is the 3D figure, standing where the
//       profile page stands it (ACampusLobbyPlayerController::FocusProfile + ProfileCamera) and
//       turned by the same RotateDisplay drag. RIGHT 55 % is one opaque angular panel: a three
//       column grid of the outfits the player OWNS, the worn one ticked, and the three preset slots
//       along the bottom. Equipping previews INSTANTLY - the figure changes on the tap.
//
//   DECOR (EScreen::Decor) - a BOTTOM SHEET, not a full screen, because the whole point is to watch
//       the prop appear: the line-up and all eight anchors stay visible above it. Pick one of the
//       eight anchors, then pick an owned prop or EMPTY.
//
// THREE RULES THIS FILE OBEYS:
//   1. OWNED ONLY. The grid never shows something the player cannot wear, so there is no locked
//      card, no price and no upsell here - that is the store's job, not the closet's.
//   2. THE SAVE FIRST, THE SERVER SECOND. Every choice is written to UCampusSave and shown in the
//      scene before any request leaves, so the lobby is right offline and right on the next launch.
//      GET /me/cosmetics overwrites all of it on sign-in: the server is truth ON LOAD.
//   3. NOTHING HERE DECIDES WHAT ANYTHING LOOKS LIKE. Meshes, colours, recipes and anchor positions
//      are all CampusCosmetics.h; this file moves ids.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusCosmetics.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusWardrobeUI"

using namespace CampusStyle;

namespace
{
	/** The figure owns the left 45 %, exactly as it does on the profile page. */
	constexpr float ClosetSceneShare = 0.45f;
	constexpr float ClosetPanelShare = 0.55f;
	constexpr float ClosetPanelMaxWidth = 980.f;

	/** Three columns, the owner's number. Fixed card size, so equipping never reflows the grid. */
	constexpr int32 GridColumns = 3;
	constexpr float CardWidth = 196.f;
	constexpr float CardHeight = 214.f;
	constexpr float CardGap = 10.f;

	/** The decor sheet's height. Everything above it is the live lobby. */
	constexpr float SheetHeight = 318.f;
	constexpr float DecorCardWidth = 168.f;
	constexpr float DecorCardHeight = 150.f;

	const FLinearColor CardInk(0.024f, 0.031f, 0.043f, 0.96f);
	const FLinearColor CardInkOn(0.10f, 0.085f, 0.028f, 0.96f);

	/** The small dark chip the cards carry: WORN, EMPTY, a rarity word. */
	TSharedRef<SWidget> ClosetChip(CampusAngular::EGlyph Glyph, const FText& Label, const FLinearColor& Color)
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(7.f))
			.Fill(FLinearColor(0.f, 0.f, 0.f, 0.80f))
			.Border(Color.CopyWithNewOpacity(0.55f))
			.BorderWidth(1.f)
			.ContentPadding(FMargin(9.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(Glyph).Size(14.f).Color(Color)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Label).Font(CampusAngular::Display(13)).ColorAndOpacity(Color)
				]
			];
	}

	/**
	 * THE OUTFIT'S WHOLE PICTURE, and there is no PNG pipeline behind it on purpose: the two colours
	 * the body material is actually tinted with, stacked, are a truer preview than any icon somebody
	 * would have to keep in step with the catalogue. The starter outfit wears the mesh's own authored
	 * texture rather than a tint, so it shows a neutral pair and says ORIGINAL on the card.
	 */
	TSharedRef<SWidget> OutfitSwatch(const CampusCosmetics::FOutfitEntry& Entry, float Height)
	{
		const bool bOriginal = Entry.bKeepMeshMaterial;
		const FLinearColor Top = bOriginal ? FLinearColor(0.58f, 0.60f, 0.64f, 1.f) : Entry.Primary;
		const FLinearColor Bottom = bOriginal ? FLinearColor(0.30f, 0.32f, 0.36f, 1.f) : Entry.Secondary;
		return SNew(SBox).HeightOverride(Height)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(0.62f)
				[
					SNew(SCampusAngularPanel).Shape(FCampusShape::Notched(10.f)).Fill(Top)
				]
				+ SVerticalBox::Slot().FillHeight(0.38f)
				[
					SNew(SCampusAngularPanel).Shape(FCampusShape::Notched(8.f)).Fill(Bottom)
				]
			];
	}
}

// -------------------------------------------------------------------------------------------------
// Ownership and the one read

bool SCampusLobbyWidget::OwnsCosmetic(FName Id) const
{
	if (Id.IsNone())
	{
		return false;
	}
	if (const UCampusBackend* Api = Backend())
	{
		return Api->OwnsCosmetic(Id.ToString());
	}
	// No backend at all (editor, or a build without one): only what is free, which is the starter.
	const int32 Outfit = CampusCosmetics::FindOutfit(Id);
	return Outfit != INDEX_NONE && CampusCosmetics::Outfit(Outfit).bFree;
}

void SCampusLobbyWidget::RefreshCosmetics()
{
	UCampusBackend* Api = Backend();
	if (bCosmeticsAsked || !Api || !Api->IsSignedIn())
	{
		return;
	}
	bCosmeticsAsked = true;
	Api->LoadCosmetics([this](bool bOk, const FString&)
	{
		if (!bOk)
		{
			// A wardrobe that could not be read is not an error worth a dialog: the player wears the
			// starter and the closet says so. The latch stays set so this cannot loop.
			return;
		}
		// LoadCosmetics has just mirrored the server's answer into the save, so the scene can be
		// rebuilt straight from it - both of these read the save, not the response.
		if (Controller.IsValid())
		{
			Controller->ApplyLobbyOutfit();
			Controller->RefreshLobbyDecor();
		}
		Refresh();
	});
}

// -------------------------------------------------------------------------------------------------
// Open / close / equip

void SCampusLobbyWidget::OpenWardrobe()
{
	// A FRESH VISIT MAY FIND A WARDROBE THE LAST ONE COULD NOT. The latch exists to stop a failed
	// read feeding itself, not to cost the player their closet for the rest of the session - so it is
	// released here, and only while the read has never actually succeeded. Once it has, reopening the
	// closet costs nothing, which on a tunnel metered at 20,000 requests a month is the point.
	const UCampusBackend* Api = Backend();
	if (Api && !Api->bCosmeticsLoaded)
	{
		bCosmeticsAsked = false;
	}
	RefreshCosmetics();
	Show(EScreen::Wardrobe);
}

void SCampusLobbyWidget::CloseWardrobe()
{
	// Nothing to commit here: every equip already wrote the save and sent its PUT, so closing cannot
	// lose a choice and cannot be the thing that saves one either.
	if (UCampusSave* Save = Profile())
	{
		Save->Persist();
	}
	Show(EScreen::Lobby);
}

void SCampusLobbyWidget::EquipOutfit(FName OutfitId)
{
	if (!OwnsCosmetic(OutfitId))
	{
		Toast(TEXT("YOU DO NOT OWN THAT OUTFIT YET."));
		return;
	}
	// THE PREVIEW IS THE SAVE. One write, then the figure is re-dressed from it, so what the player
	// sees and what the next match sends can never be two different things.
	if (UCampusSave* Save = Profile())
	{
		Save->EquippedOutfit = OutfitId;
		Save->Persist();
	}
	if (Controller.IsValid())
	{
		Controller->ApplyLobbyOutfit();
	}
	Refresh();

	UCampusBackend* Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		return;
	}
	bCosmeticsBusy = true;
	Api->SaveCosmetics(OutfitId.ToString(), [this](bool bOk, const FString& Error)
	{
		bCosmeticsBusy = false;
		if (!bOk)
		{
			// The outfit STAYS ON. The device already believes it and so does the next launch; the
			// honest thing is to say the server has not caught up, not to take the shirt back.
			Toast(Error.IsEmpty() ? TEXT("SAVED ON THIS DEVICE - THE SERVER DID NOT ANSWER.") : Error);
		}
		Refresh();
	});
}

void SCampusLobbyWidget::WearPreset(int32 Index)
{
	const UCampusSave* Save = Profile();
	const FName Stored = Save && Save->OutfitPresets.IsValidIndex(Index) ? Save->OutfitPresets[Index] : NAME_None;
	if (Stored.IsNone())
	{
		Toast(FString::Printf(TEXT("PRESET %d IS EMPTY - TAP ITS SAVE CHIP TO STORE THIS LOOK."), Index + 1));
		return;
	}
	EquipOutfit(Stored);
}

void SCampusLobbyWidget::StorePreset(int32 Index)
{
	UCampusSave* Save = Profile();
	if (!Save || Index < 0 || Index >= CampusCosmetics::NumPresets)
	{
		return;
	}
	const FName Worn = Save->EquippedOutfit;
	// Sized on write, never in the save's declaration: an existing save file has no presets array at
	// all and still has to load.
	if (Save->OutfitPresets.Num() < CampusCosmetics::NumPresets)
	{
		Save->OutfitPresets.SetNum(CampusCosmetics::NumPresets);
	}
	Save->OutfitPresets[Index] = Worn;
	Save->Persist();
	Refresh();
	Toast(FString::Printf(TEXT("PRESET %d SAVED."), Index + 1));

	UCampusBackend* Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		return;
	}
	bCosmeticsBusy = true;
	Api->SavePreset(Index, Worn.ToString(), [this](bool bOk, const FString& Error)
	{
		bCosmeticsBusy = false;
		if (!bOk)
		{
			// THE PRESET STAYS SAVED on this device, whatever the server said (409 NOT_OWNED, 422
			// SLOT_MISMATCH, a dead tunnel). Taking it back would be the one outcome the player
			// cannot act on; the next GET /me/cosmetics is where the server gets the last word.
			Toast(Error.IsEmpty() ? TEXT("SAVED ON THIS DEVICE - THE SERVER DID NOT ANSWER.") : Error);
		}
		Refresh();
	});
}

void SCampusLobbyWidget::SetDecorAt(int32 AnchorIndex, FName ItemId)
{
	UCampusSave* Save = Profile();
	if (!Save || AnchorIndex < 0 || AnchorIndex >= CampusCosmetics::NumAnchors)
	{
		return;
	}
	const FName Key(CampusCosmetics::Anchor(AnchorIndex).Id);
	if (ItemId.IsNone())
	{
		Save->LobbyDecor.Remove(Key);
	}
	else
	{
		const CampusCosmetics::FDecorEntry* Entry = CampusCosmetics::DecorFor(ItemId);
		if (!Entry || !CampusCosmetics::DecorAllowedAt(*Entry, AnchorIndex))
		{
			Toast(TEXT("THAT ONE DOES NOT GO THERE."));
			return;
		}
		if (!OwnsCosmetic(ItemId))
		{
			Toast(TEXT("YOU DO NOT OWN THAT YET."));
			return;
		}
		Save->LobbyDecor.Add(Key, ItemId);
	}
	Save->Persist();
	// The scene changes under the finger: the whole sheet exists so this is watchable.
	if (Controller.IsValid())
	{
		Controller->RefreshLobbyDecor();
	}
	Refresh();

	UCampusBackend* Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		return;
	}
	// The WHOLE room every time, bare anchors included (UCampusBackend::SaveLobbyDecor sends those as
	// null), so "I took the trophy off the pedestal" is something the server can actually be told.
	TMap<FString, FString> Wire;
	Wire.Reserve(Save->LobbyDecor.Num());
	for (const TPair<FName, FName>& Pair : Save->LobbyDecor)
	{
		Wire.Add(Pair.Key.ToString(), Pair.Value.ToString());
	}
	bCosmeticsBusy = true;
	Api->SaveLobbyDecor(Wire, [this](bool bOk, const FString& Error)
	{
		bCosmeticsBusy = false;
		if (!bOk)
		{
			// Same rule as the closet: the prop STAYS where the player put it. A 422 UNKNOWN_ANCHOR
			// or NOT_DECOR means this build and the server disagree about the catalogue, and the
			// answer to that is the next load, not snatching the room back mid-tap.
			Toast(Error.IsEmpty() ? TEXT("SAVED ON THIS DEVICE - THE SERVER DID NOT ANSWER.") : Error);
		}
		Refresh();
	});
}

// -------------------------------------------------------------------------------------------------
// The closet

TSharedRef<SWidget> SCampusLobbyWidget::BuildOutfitCard(int32 OutfitIndex)
{
	const CampusCosmetics::FOutfitEntry& Entry = CampusCosmetics::Outfit(OutfitIndex);
	const FName Id(Entry.Id);
	const UCampusSave* Save = Profile();
	// Through OutfitFor, so an old save holding a retired id still ticks the starter card rather than
	// leaving the grid with nothing selected at all.
	const bool bWorn = Save && FName(CampusCosmetics::OutfitFor(Save->EquippedOutfit).Id) == Id;
	const FLinearColor Frame = CampusCosmetics::RarityColor(Entry.Rarity);

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Notched(14.f))
			.Fill(bWorn ? CardInkOn : CardInk)
			// RARITY IS THE FRAME, SELECTION IS THE ACCENT, and neither is ever the only signal: the
			// rarity word is printed on the card and the worn one carries a WORN tick.
			.Border(bWorn ? Accent : Frame.CopyWithNewOpacity(0.75f))
			.BorderWidth(bWorn ? 3.f : 2.f)
			.AccentEdge(bWorn ? Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bWorn ? 6.f : 0.f)
			.ContentPadding(FMargin(12.f, 10.f))
			.OnClicked(FOnClicked::CreateLambda([this, Id]()
			{
				EquipOutfit(Id);
				return FReply::Handled();
			}))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.f)[OutfitSwatch(Entry, 110.f)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString(Entry.Name).ToUpper()))
					.Font(CampusAngular::Display(17))
					.ColorAndOpacity(FLinearColor::White)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Entry.bKeepMeshMaterial
						? TEXT("ORIGINAL") : CampusCosmetics::RarityName(Entry.Rarity)))
					.Font(CampusAngular::Display(12))
					.ColorAndOpacity(Frame)
				]
			]
		]
		// Above the card, so it is painted over the plate rather than inside its padding.
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(8.f)
		[
			bWorn
				? ClosetChip(CampusAngular::EGlyph::Tick, LOCTEXT("Worn", "WORN"), Accent)
				: SNullWidget::NullWidget
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildPresetRow()
{
	const UCampusSave* Save = Profile();
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < CampusCosmetics::NumPresets; ++Index)
	{
		const FName Stored = Save && Save->OutfitPresets.IsValidIndex(Index) ? Save->OutfitPresets[Index] : NAME_None;
		const bool bFilled = !Stored.IsNone();
		const FString Label = bFilled ? FString(CampusCosmetics::OutfitFor(Stored).Name).ToUpper() : TEXT("EMPTY");

		Row->AddSlot().FillWidth(1.f).Padding(Index == 0 ? 0.f : 8.f, 0.f, 0.f, 0.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox).HeightOverride(66.f)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Slanted(9.f))
					.Fill(bFilled ? CardInk : FLinearColor(0.f, 0.f, 0.f, 0.5f))
					.Border(bFilled ? Accent.CopyWithNewOpacity(0.65f) : CampusAngular::Hairline)
					.BorderWidth(1.f)
					.ContentPadding(FMargin(14.f, 6.f, 44.f, 6.f))
					.OnClicked(FOnClicked::CreateLambda([this, Index]()
					{
						WearPreset(Index);
						return FReply::Handled();
					}))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("PresetNumber", "PRESET {0}"), FText::AsNumber(Index + 1)))
							.Font(CampusAngular::Display(15))
							.ColorAndOpacity(bFilled ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.45f))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Label))
							.Font(CampusAngular::Body(12))
							.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, bFilled ? 0.80f : 0.35f))
						]
					]
				]
			]
			// SAVE sits ABOVE the tile so it takes the tap instead of wearing the preset - the same
			// trick the mode cards' green "?" uses, and it beats a hidden long press on a phone.
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SBox).WidthOverride(34.f).HeightOverride(34.f)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Clipped(7.f))
					.Fill(CampusAngular::Go)
					.Border(FLinearColor(1.f, 1.f, 1.f, 0.35f))
					.BorderWidth(1.f)
					.ContentPadding(FMargin(0.f))
					.OnClicked(FOnClicked::CreateLambda([this, Index]()
					{
						StorePreset(Index);
						return FReply::Handled();
					}))
					[
						SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Plus).Size(16.f)
								.Color(FLinearColor::White)
						]
					]
				]
			]
		];
	}
	return Row;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildWardrobe()
{
	// The camera moves to the figure and the rest of the line-up hides: the SAME call the profile page
	// makes, so there is still exactly one 3D path in the lobby.
	if (Controller.IsValid())
	{
		Controller->FocusProfile(true);
	}

	// OWNED ONLY, and the order is the catalogue's so a new outfit lands where the designer put it.
	TArray<int32> Wearable;
	for (int32 Index = 0; Index < CampusCosmetics::NumOutfits(); ++Index)
	{
		if (OwnsCosmetic(FName(CampusCosmetics::Outfit(Index).Id)))
		{
			Wearable.Add(Index);
		}
	}

	TSharedRef<SScrollBox> Grid = SNew(SScrollBox).ScrollBarVisibility(EVisibility::Collapsed);
	for (int32 First = 0; First < Wearable.Num(); First += GridColumns)
	{
		TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
		for (int32 Column = 0; Column < GridColumns; ++Column)
		{
			const int32 At = First + Column;
			Line->AddSlot().AutoWidth().Padding(Column == 0 ? 0.f : CardGap, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(CardWidth).HeightOverride(CardHeight)
				[
					// A fixed-size empty box in the tail of the last line, so three cards and one
					// card leave the grid the same width and the panel never jumps.
					Wearable.IsValidIndex(At) ? BuildOutfitCard(Wearable[At]) : SNullWidget::NullWidget
				]
			];
		}
		Grid->AddSlot().Padding(0.f, First == 0 ? 0.f : CardGap, 0.f, 0.f)[Line];
	}

	const UCampusBackend* Api = Backend();
	const bool bSignedIn = Api && Api->IsSignedIn();
	// One honest line about WHY the grid is short, which is the difference between an empty screen
	// and a screen that is telling the player something.
	FText Hint = LOCTEXT("ClosetHint", "TAP AN OUTFIT TO WEAR IT.");
	if (!bSignedIn)
	{
		Hint = LOCTEXT("ClosetOffline", "SIGNED OUT - ONLY THE FREE OUTFIT IS AVAILABLE.");
	}
	else if (Api && !Api->bCosmeticsLoaded)
	{
		Hint = LOCTEXT("ClosetLoading", "LOADING YOUR WARDROBE...");
	}
	else if (Wearable.Num() <= 1)
	{
		Hint = LOCTEXT("ClosetBare", "NOTHING UNLOCKED YET - OUTFITS ARRIVE FROM EVENTS AND THE STORE.");
	}

	TSharedRef<SWidget> Panel = SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Notched(20.f))
		// Opaque: this panel sits over the live 3D scene and has to stay readable against any of it.
		.Fill(CampusAngular::InkStrong)
		.Border(CampusAngular::Hairline)
		.BorderWidth(1.f)
		.ContentPadding(FMargin(24.f, 20.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(8.f)).Fill(Accent)
					[
						SNew(SBox).WidthOverride(14.f).HeightOverride(28.f)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Label(LOCTEXT("WardrobeTitle", "WARDROBE"), 29, FLinearColor::White)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 12.f)
			[
				SNew(STextBlock).Text(Hint).Font(CampusAngular::Body(14))
				.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.55f)).AutoWrapText(true)
			]
			+ SVerticalBox::Slot().FillHeight(1.f)[Grid]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
			[
				SNew(SBox).HeightOverride(1.f)[SNew(SCampusAngularPanel).Fill(CampusAngular::Hairline)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)[BuildPresetRow()]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[
				// The decor sheet lives behind the closet rather than behind a tab of its own: both
				// are "how my lobby looks", and one menu row for the pair keeps the left column short.
				Button(LOCTEXT("OpenDecor", "LOBBY DECOR"), FOnClicked::CreateLambda([this]()
				{
					DecorAnchor = FMath::Clamp(DecorAnchor, 0, CampusCosmetics::NumAnchors - 1);
					Show(EScreen::Decor);
					return FReply::Handled();
				}), EButton::Dark, 18, FVector2D(260.f, 56.f))
			]
		];

	// No backdrop, for the same reason the profile page has none: a scrim over the left half would
	// hide the very thing the screen is for.
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(24.f, 16.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(ClosetSceneShare)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SCampusDragArea)
						.OnDrag_Lambda([this](float DeltaX)
						{
							if (Controller.IsValid())
							{
								Controller->RotateDisplay(-DeltaX * 0.4f);
							}
						})
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 10.f)
					[
						Label(LOCTEXT("ClosetDrag", "DRAG TO TURN"), 13, FLinearColor(1.f, 1.f, 1.f, 0.35f))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(ClosetPanelShare)
				[
					SNew(SVerticalBox)
					// The X is OUTSIDE the panel - so the grid can never scroll it away, and so an
					// equip still in flight cannot trap the player on this screen. Only the cards and
					// the presets go dead while a PUT is in the air.
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 8.f)
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close, FOnClicked::CreateLambda([this]()
						{
							CloseWardrobe();
							return FReply::Handled();
						}), 56.f)
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SBox).MaxDesiredWidth(ClosetPanelMaxWidth).IsEnabled(!bCosmeticsBusy)[Panel]
					]
				]
			]
		];
}

// -------------------------------------------------------------------------------------------------
// The decor sheet

TSharedRef<SWidget> SCampusLobbyWidget::BuildDecorAnchorChip(int32 AnchorIndex)
{
	const CampusCosmetics::FAnchor& Anchor = CampusCosmetics::Anchor(AnchorIndex);
	const bool bSelected = DecorAnchor == AnchorIndex;
	const UCampusSave* Save = Profile();
	const FName* OnIt = Save ? Save->LobbyDecor.Find(FName(Anchor.Id)) : nullptr;
	const CampusCosmetics::FDecorEntry* Placed = OnIt ? CampusCosmetics::DecorFor(*OnIt) : nullptr;

	return SNew(SBox).WidthOverride(176.f).HeightOverride(64.f)
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Slanted(9.f))
			.Fill(bSelected ? CardInkOn : FLinearColor(0.f, 0.f, 0.f, 0.60f))
			.Border(bSelected ? Accent : CampusAngular::Hairline)
			.BorderWidth(bSelected ? 2.f : 1.f)
			.AccentEdge(bSelected ? Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bSelected ? 5.f : 0.f)
			.ContentPadding(FMargin(14.f, 6.f))
			.OnClicked(FOnClicked::CreateLambda([this, AnchorIndex]()
			{
				DecorAnchor = AnchorIndex;
				Refresh();
				return FReply::Handled();
			}))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString(Anchor.Name).ToUpper()))
					.Font(CampusAngular::Display(15))
					.ColorAndOpacity(bSelected ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.82f))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Placed ? FString(Placed->Name).ToUpper() : TEXT("EMPTY")))
					.Font(CampusAngular::Body(12))
					.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, Placed ? 0.72f : 0.32f))
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildDecorCard(int32 DecorIndex)
{
	const UCampusSave* Save = Profile();
	const FName Key(CampusCosmetics::Anchor(DecorAnchor).Id);
	const FName* OnIt = Save ? Save->LobbyDecor.Find(Key) : nullptr;

	// DecorIndex < 0 is the EMPTY card: clearing an anchor has to be a card like any other, or it
	// becomes a gesture the player has to be told about.
	if (DecorIndex < 0)
	{
		const bool bBare = OnIt == nullptr;
		return SNew(SBox).WidthOverride(DecorCardWidth).HeightOverride(DecorCardHeight)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Notched(12.f))
				.Fill(bBare ? CardInkOn : CardInk)
				.Border(bBare ? Accent : CampusAngular::Hairline)
				.BorderWidth(bBare ? 3.f : 1.f)
				.ContentPadding(FMargin(12.f))
				.OnClicked(FOnClicked::CreateLambda([this]()
				{
					SetDecorAt(DecorAnchor, NAME_None);
					return FReply::Handled();
				}))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Close).Size(34.f)
							.Color(FLinearColor(1.f, 1.f, 1.f, 0.55f))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						Label(LOCTEXT("DecorEmpty", "EMPTY"), 16, FLinearColor(1.f, 1.f, 1.f, 0.78f))
					]
				]
			];
	}

	const CampusCosmetics::FDecorEntry& Entry = CampusCosmetics::Decor(DecorIndex);
	const FName Id(Entry.Id);
	const bool bPlaced = OnIt && *OnIt == Id;
	const FLinearColor Frame = CampusCosmetics::RarityColor(Entry.Rarity);
	// The prop's OWN first colour is its swatch: no icon to keep in step with the recipe.
	const FLinearColor Swatch = Entry.Parts[0].Mesh ? Entry.Parts[0].Color : Frame;

	return SNew(SBox).WidthOverride(DecorCardWidth).HeightOverride(DecorCardHeight)
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Notched(12.f))
			.Fill(bPlaced ? CardInkOn : CardInk)
			.Border(bPlaced ? Accent : Frame.CopyWithNewOpacity(0.7f))
			.BorderWidth(bPlaced ? 3.f : 2.f)
			.ContentPadding(FMargin(12.f, 10.f))
			.OnClicked(FOnClicked::CreateLambda([this, Id]()
			{
				SetDecorAt(DecorAnchor, Id);
				return FReply::Handled();
			}))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SCampusAngularPanel).Shape(FCampusShape::Notched(8.f)).Fill(Swatch)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 7.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString(Entry.Name).ToUpper()))
					.Font(CampusAngular::Display(15))
					.ColorAndOpacity(bPlaced ? Accent : FLinearColor::White)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(CampusCosmetics::RarityName(Entry.Rarity)))
					.Font(CampusAngular::Display(11))
					.ColorAndOpacity(Frame)
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildDecorScreen()
{
	// The anchors along the top of the sheet. One horizontal SScrollBox: the cards tap on RELEASE
	// (SCampusAngularButton), so a sideways swipe across them scrolls instead of placing something.
	TSharedRef<SScrollBox> Anchors = SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		.ScrollBarVisibility(EVisibility::Collapsed)
		.AllowOverscroll(EAllowOverscroll::Yes);
	for (int32 Index = 0; Index < CampusCosmetics::NumAnchors; ++Index)
	{
		Anchors->AddSlot().Padding(Index == 0 ? 0.f : 8.f, 0.f, 0.f, 0.f)[BuildDecorAnchorChip(Index)];
	}

	// The props the selected anchor will take, EMPTY first. OWNED ONLY, like the closet.
	TSharedRef<SScrollBox> Props = SNew(SScrollBox)
		.Orientation(Orient_Horizontal)
		.ScrollBarVisibility(EVisibility::Collapsed)
		.AllowOverscroll(EAllowOverscroll::Yes);
	Props->AddSlot()[BuildDecorCard(INDEX_NONE)];
	int32 Offered = 0;
	for (int32 Index = 0; Index < CampusCosmetics::NumDecor(); ++Index)
	{
		const CampusCosmetics::FDecorEntry& Entry = CampusCosmetics::Decor(Index);
		if (!CampusCosmetics::DecorAllowedAt(Entry, DecorAnchor) || !OwnsCosmetic(FName(Entry.Id)))
		{
			continue;
		}
		Props->AddSlot().Padding(CardGap, 0.f, 0.f, 0.f)[BuildDecorCard(Index)];
		++Offered;
	}

	TSharedRef<SWidget> Sheet = SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Notched(18.f))
		.Fill(CampusAngular::InkStrong)
		.Border(CampusAngular::Hairline)
		.BorderWidth(1.f)
		.ContentPadding(FMargin(22.f, 16.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				Label(LOCTEXT("DecorPickAnchor", "1. PICK A SPOT"), 15, FLinearColor(1.f, 1.f, 1.f, 0.55f))
			]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(68.f)[Anchors]]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 6.f)
			[
				Label(Offered > 0
					? LOCTEXT("DecorPickItem", "2. PICK WHAT GOES THERE")
					: LOCTEXT("DecorNone", "2. NOTHING YOU OWN FITS THIS SPOT YET"),
					15, FLinearColor(1.f, 1.f, 1.f, 0.55f))
			]
			+ SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(DecorCardHeight + 4.f)[Props]]
		];

	// A SHEET, NOT A SCREEN: everything above it is the live lobby, so the prop that just landed on
	// the pedestal is visible the instant it is tapped. The drag area keeps working up there too.
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusDragArea)
			.OnDrag_Lambda([this](float DeltaX)
			{
				if (Controller.IsValid())
				{
					Controller->RotateDisplay(-DeltaX * 0.4f);
				}
			})
		]
		+ SOverlay::Slot()
		[
			// SELF-HIT-TEST-INVISIBLE ALL THE WAY DOWN THE FRAME. The drag area is this overlay's
			// SIBLING, not its child, so a hit-testable container stacked on top of it would swallow
			// every drag that did not land on an actual control - and turning the line-up is half of
			// what this screen is for. Only the header row and the sheet itself take touches.
			SNew(SSafeZone).Visibility(EVisibility::SelfHitTestInvisible).Padding(FMargin(24.f, 16.f))
			[
				SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(8.f)).Fill(Accent)
						[
							SNew(SBox).WidthOverride(14.f).HeightOverride(28.f)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Label(LOCTEXT("DecorTitle", "DECORATE LOBBY"), 28, FLinearColor::White)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close, FOnClicked::CreateLambda([this]()
						{
							// Back to the closet, which is where this was opened from.
							Show(EScreen::Wardrobe);
							return FReply::Handled();
						}), 56.f)
					]
				]
				// The gap between the header and the sheet IS the lobby: it must pass touches down to
				// the drag area rather than absorb them.
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SSpacer).Visibility(EVisibility::HitTestInvisible)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(SheetHeight).IsEnabled(!bCosmeticsBusy)[Sheet]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
