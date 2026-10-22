// The WEAPONS page, Free Fire's layout and the three.js WEAPONS page's behaviour:
//
//   LEFT    every gun the store sells, owned ones marked, the equipped one ticked
//   CENTRE  the gun itself, a real Weapon Library V3 mesh on the lobby's own 3D scene
//           (ACampusLobbyPlayerController::ShowWeaponStage), painted with the skin being looked at and
//           turned by a drag - no second renderer, no render target
//   TOP     gun + skin name, the skin's line, the Diamonds wallet and the close button
//   BOTTOM  ATTRIBUTES (the gun's bars with the skin's ups and downs), the SKINS row (factory + four, each
//           with its price or a tick), and the one action that makes sense: BUY GUN / BUY SKIN / EQUIP
//
// A skin is a look AND abilities (backend migration 032, CampusGunSkins): two ups, one down, worn per gun type.
// Buying goes through the store's Diamond path (POST /v1/store/items/:id/buy); wearing through
// PUT /v1/me/gunskins; the loadout gun through the existing EquipGun. Nothing here touches money.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusGunSkins.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusWeapons.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusWeapons"

using namespace CampusStyle;

namespace CampusWeaponsPage
{
	static const FLinearColor Plus(0.30f, 0.86f, 0.42f, 1.f);
	static const FLinearColor Minus(0.95f, 0.33f, 0.33f, 1.f);
	static const FLinearColor Gem(0.38f, 0.78f, 1.00f, 1.f);
	static const FLinearColor Steel(0.62f, 0.68f, 0.78f, 1.f);
	static const FLinearColor PanelFill(0.012f, 0.018f, 0.028f, 0.82f);
	static const FLinearColor Tile(0.035f, 0.045f, 0.065f, 0.94f);

	/** The rarity frame a style is sold at (migration 032). */
	FLinearColor StyleColor(uint8 Style)
	{
		switch (Style)
		{
		case CampusGunSkins::Royal:  return FLinearColor(1.00f, 0.74f, 0.20f, 1.f);
		case CampusGunSkins::Neon:   return FLinearColor(0.78f, 0.36f, 1.00f, 1.f);
		case CampusGunSkins::Arctic: return FLinearColor(0.40f, 0.82f, 1.00f, 1.f);
		case CampusGunSkins::Relic:  return FLinearColor(0.95f, 0.62f, 0.25f, 1.f);
		default:                     return Steel;
		}
	}

	/** One attribute line: its 0..1 bar for the gun AS SKINNED, and the skin's sign on it. */
	struct FRow
	{
		FText Label;
		float Fill01 = 0.f;
		int32 Delta = 0;
	};

	void RowsFor(FName GunId, uint8 Style, TArray<FRow>& Out)
	{
		const FCampusWeaponDef* Base = CampusWeapons::Find(GunId);
		if (!Base)
		{
			return;
		}
		const FCampusWeaponMods Mods = CampusGunSkins::ModsFor(Style);
		const FCampusWeaponDef Def = CampusWeapons::WithMods(*Base, Mods);
		const auto Sign = [](float Pct) { return Pct > 0.f ? FMath::RoundToInt(Pct) : (Pct < 0.f ? FMath::RoundToInt(Pct) : 0); };
		// Normalised against the whole table's spread, so the bars compare guns, not just skins.
		Out.Add({ LOCTEXT("Damage", "DAMAGE"), FMath::Clamp(Def.Damage / 60.f, 0.05f, 1.f), Sign(Mods.DamagePct) });
		Out.Add({ LOCTEXT("Rate", "RATE OF FIRE"), FMath::Clamp(Def.Rpm / 1000.f, 0.05f, 1.f), Sign(Mods.FireRatePct) });
		Out.Add({ LOCTEXT("Accuracy", "ACCURACY"), FMath::Clamp(1.f - Def.SpreadDeg / 4.f, 0.05f, 1.f), Sign(Mods.AccuracyPct) });
		Out.Add({ LOCTEXT("Range", "RANGE"), FMath::Clamp(Def.Range / 12000.f, 0.05f, 1.f), Sign(Mods.RangePct) });
		Out.Add({ LOCTEXT("Reload", "RELOAD SPEED"), FMath::Clamp(1.f - Def.ReloadSeconds / 7.f, 0.05f, 1.f), Sign(Mods.ReloadPct) });
		Out.Add({ LOCTEXT("Mag", "MAGAZINE"), FMath::Clamp(float(Def.Magazine) / 100.f, 0.05f, 1.f), Sign(Mods.MagazinePct) });
	}
}


void SCampusLobbyWidget::OpenWeapons(const FString& Gun)
{
	UCampusBackend* Api = Backend();
	if (!Api)
	{
		return;
	}
	// The selection: the gun asked for, else the equipped one, else the first; shown in its worn skin.
	auto Pick = [this, Gun]()
	{
		const UCampusBackend* Live = Backend();
		FString Id = Gun;
		if (Id.IsEmpty() && Live)
		{
			Id = !Live->EquippedGun.IsEmpty() ? Live->EquippedGun : (Live->Guns.Num() > 0 ? Live->Guns[0].Id : FString());
		}
		const FString* Worn = Live ? Live->GunSkins.Find(Id) : nullptr;
		WeaponsGun = Id;
		WeaponsStyle = Worn ? CampusGunSkins::StyleFor(*Worn) : uint8(CampusGunSkins::None);
	};
	// INSTANT when the store is already in memory (the lobby prefetches it, see Show(Lobby)): the page opens
	// on this tap, and a quiet refresh repaints it if prices or ownership moved. Only the very first open
	// of a session with no store yet waits behind the busy overlay.
	if (Api->Guns.Num() > 0)
	{
		Pick();
		Show(EScreen::Weapons);
		// WEAK: the backend outlives this widget, and the player may leave the lobby before the reply.
		TWeakPtr<SWidget> Weak = AsWeak();
		Api->LoadStore([this, Weak](bool bOk, const FString&)
		{
			if (Weak.IsValid() && bOk && Current == EScreen::Weapons)
			{
				Refresh();
			}
		});
		return;
	}
	Busy(TEXT("Opening weapons..."));
	Api->LoadStore([this, Pick](bool bOk, const FString& Error)
	{
		Done(bOk, Error, [this, Pick]()
		{
			Pick();
			Show(EScreen::Weapons);
		});
	});
}

void SCampusLobbyWidget::WeaponsSelect(const FString& Gun, uint8 Style)
{
	WeaponsGun = Gun;
	WeaponsStyle = Style;
	if (Controller.IsValid())
	{
		Controller->ShowWeaponStage(FName(*Gun), Style);
	}
	Refresh();
}

void SCampusLobbyWidget::WeaponsEquip()
{
	UCampusBackend* Api = Backend();
	if (!Api)
	{
		return;
	}
	const FString Gun = WeaponsGun;
	const FString Skin = CampusGunSkins::SkinIdFor(FName(*Gun), WeaponsStyle);
	const FCampusGun* Row = Api->FindGun(Gun);
	const bool bOwnsGun = Row && (Row->bOwned || Row->bStarter);
	// The skin first (it is what this page is about), then the gun as the loadout one when it is owned.
	auto Wear = [this, Gun, Skin, bOwnsGun]()
	{
		UCampusBackend* Live = Backend();
		if (!Live)
		{
			return;
		}
		if (bOwnsGun && Live->EquippedGun != Gun)
		{
			Live->EquipGun(Gun, [this, Gun](bool bOk, const FString& Error)
			{
				Done(bOk, Error, [this]() { Toast(TEXT("Equipped - every match from now")); });
			});
			return;
		}
		Done(true, FString(), [this]() { Toast(TEXT("Equipped - every match from now")); });
	};
	Busy(TEXT("Equipping..."));
	TMap<FString, FString> Skins = Api->GunSkins;
	const FString* Worn = Skins.Find(Gun);
	if ((Worn ? *Worn : FString()) == Skin)
	{
		Wear();
		return;
	}
	Skins.Add(Gun, Skin); // empty = the factory finish, sent as null
	Api->SaveGunSkins(Skins, [this, Wear](bool bOk, const FString& Error)
	{
		if (!bOk)
		{
			Done(false, Error);
			return;
		}
		Wear();
	});
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildWeapons()
{
	const UCampusBackend* Api = Backend();
	const FCampusGun* Gun = Api ? Api->FindGun(WeaponsGun) : nullptr;
	const FCampusWeaponDef* Def = CampusWeapons::Find(FName(*WeaponsGun));
	const FText GunName = FText::FromString((Gun ? Gun->Name : WeaponsGun).ToUpper());
	const FText Title = WeaponsStyle == CampusGunSkins::None
		? GunName
		: FText::Format(LOCTEXT("TitleSkin", "{0} - {1}"), GunName, CampusGunSkins::Label(WeaponsStyle));
	const FCampusGunSkinItem* SkinItem = nullptr;
	if (Api && WeaponsStyle != CampusGunSkins::None)
	{
		const FString SkinId = CampusGunSkins::SkinIdFor(FName(*WeaponsGun), WeaponsStyle);
		SkinItem = Api->GunSkinShop.FindByPredicate([&SkinId](const FCampusGunSkinItem& Each) { return Each.Id == SkinId; });
	}
	const FText Line = WeaponsStyle == CampusGunSkins::None
		? FText::FromString(Gun ? Gun->Description : FString())
		: LOCTEXT("SkinLine", "Look and abilities - two ups, one down. Works on every one of these guns you hold, picked up ones too.");

	const TSharedRef<SWidget> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
		[
			SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(10.f)).Fill(Accent)
			[
				SNew(SBox).WidthOverride(14.f).HeightOverride(32.f)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Label(LOCTEXT("WeaponsTitle", "WEAPONS"), 32, FLinearColor::White)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
		// Diamonds at the top, like the lobby and the three.js page; the "+" goes to the Diamonds tab.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
		[
			Counter(CampusAngular::EGlyph::Gem, CampusWeaponsPage::Gem, FText::AsNumber(Api ? Api->Diamonds : 0),
				FOnClicked::CreateLambda([this]() { StoreTab = EStoreTab::Packs; Show(EScreen::Store); return FReply::Handled(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			IconButton(CampusAngular::EGlyph::Close, FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }), 62.f)
		];

	// The name block sits over the 3D gun's top-left, the way Free Fire prints it.
	const TSharedRef<SWidget> NameBlock = SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Text(Title).Font(CampusAngular::Display(34))
			.ColorAndOpacity(WeaponsStyle == CampusGunSkins::None ? FLinearColor::White : CampusWeaponsPage::StyleColor(WeaponsStyle))
			.ShadowOffset(FVector2D(2.f, 2.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(520.f)
			[
				SNew(STextBlock).Text(Line).Font(CampusAngular::Body(15)).ColorAndOpacity(CampusWeaponsPage::Steel).AutoWrapText(true)
				.ShadowOffset(FVector2D(1.f, 1.f)).ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SkinItem && !SkinItem->Rarity.IsEmpty()
				? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(8.f))
					.Fill(CampusWeaponsPage::StyleColor(WeaponsStyle)).ContentPadding(FMargin(9.f, 2.f))
					[
						SNew(STextBlock).Text(FText::FromString(SkinItem->Rarity.ToUpper()))
						.Font(CampusAngular::Display(12)).ColorAndOpacity(FLinearColor::Black)
					])
				: (Def ? StaticCastSharedRef<SWidget>(Label(FText::Format(LOCTEXT("MagLine", "{0} ROUNDS  -  {1} RPM"),
						FText::AsNumber(Def->Magazine), FText::AsNumber(FMath::RoundToInt(Def->Rpm))), 14, CampusWeaponsPage::Steel))
					: SNullWidget::NullWidget)
		];

	return SNew(SOverlay)
		// No full backdrop: the gun on the 3D stage IS the middle of this screen. Only the left list and
		// the bottom strip get their own dark plates.
		+ SOverlay::Slot()
		[
			// The drag turns the gun (RotateDisplay hands it to the weapon stage while the page is up).
			SNew(SCampusDragArea).OnDrag_Lambda([this](float DeltaX) { if (Controller.IsValid()) { Controller->RotateDisplay(-DeltaX * 0.5f); } })
		]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(24.f, 18.f)).Visibility(EVisibility::SelfHitTestInvisible)
			[
				SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
				+ SVerticalBox::Slot().AutoHeight()[Header]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 22.f, 0.f)[BuildWeaponsList()]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
						+ SVerticalBox::Slot().AutoHeight()[NameBlock]
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Bottom).Visibility(EVisibility::HitTestInvisible)
							[
								Label(LOCTEXT("DragGun", "DRAG TO TURN"), 13, FLinearColor(1.f, 1.f, 1.f, 0.35f))
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(0.f, 0.f, 16.f, 0.f)[BuildWeaponsAttributes()]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Bottom)[BuildWeaponsSkins()]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(16.f, 0.f, 0.f, 0.f)[BuildWeaponsAction()]
						]
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildWeaponsList()
{
	const UCampusBackend* Api = Backend();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (Api)
	{
		for (const FCampusGun& Gun : Api->Guns)
		{
			const bool bOn = Gun.Id == WeaponsGun;
			const bool bOwned = Gun.bOwned || Gun.bStarter;
			const bool bEquipped = Api->EquippedGun == Gun.Id;
			const FString* Worn = Api->GunSkins.Find(Gun.Id);
			const uint8 WornStyle = Worn ? CampusGunSkins::StyleFor(*Worn) : uint8(CampusGunSkins::None);
			int32 SkinsOwned = 0;
			for (const FCampusGunSkinItem& Skin : Api->GunSkinShop)
			{
				SkinsOwned += Skin.ForGun == Gun.Id && Skin.bOwned ? 1 : 0;
			}
			const FString Id = Gun.Id;
			List->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(SBox).WidthOverride(230.f)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Notched(10.f))
					.Fill(bOn ? FLinearColor(0.11f, 0.13f, 0.17f, 0.96f) : CampusWeaponsPage::Tile)
					.Border(bOn ? Accent : (WornStyle != CampusGunSkins::None ? CampusWeaponsPage::StyleColor(WornStyle) : CampusAngular::Hairline))
					.BorderWidth(bOn ? 2.f : 1.f)
					.AccentEdge(bOn ? Accent : FLinearColor::Transparent)
					.AccentEdgeWidth(bOn ? 5.f : 0.f)
					.ContentPadding(FMargin(14.f, 9.f))
					.OnClicked(FOnClicked::CreateLambda([this, Id, WornStyle]() { WeaponsSelect(Id, WornStyle); return FReply::Handled(); }))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(FText::FromString(Gun.Name.ToUpper())).Font(CampusAngular::Display(19))
								.ColorAndOpacity(bOwned ? FLinearColor::White : CampusWeaponsPage::Steel)
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("SkinCount", "{0} / 4 SKINS"), FText::AsNumber(SkinsOwned)))
								.Font(CampusAngular::Display(11)).ColorAndOpacity(CampusWeaponsPage::Steel)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SCampusGlyph)
							.Glyph(bEquipped ? CampusAngular::EGlyph::Tick : (bOwned ? CampusAngular::EGlyph::Rank : CampusAngular::EGlyph::Lock))
							.Size(18.f)
							.Color(bEquipped ? Go : (bOwned ? CampusWeaponsPage::Steel : FLinearColor(1.f, 1.f, 1.f, 0.35f)))
						]
					]
				]
			];
		}
	}
	return SNew(SBox).HeightOverride(560.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[List]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildWeaponsAttributes()
{
	TArray<CampusWeaponsPage::FRow> Rows;
	CampusWeaponsPage::RowsFor(FName(*WeaponsGun), WeaponsStyle, Rows);
	TSharedRef<SVerticalBox> Bars = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Label(LOCTEXT("Attributes", "ATTRIBUTES"), 16, FLinearColor::White)
		];
	for (const CampusWeaponsPage::FRow& Row : Rows)
	{
		const FLinearColor Mark = Row.Delta > 0 ? CampusWeaponsPage::Plus : CampusWeaponsPage::Minus;
		Bars->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(118.f)
				[
					SNew(STextBlock).Text(Row.Label).Font(CampusAngular::Display(12))
					.ColorAndOpacity(Row.Delta == 0 ? CampusWeaponsPage::Steel : Mark)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f)
			[
				SNew(SBox).WidthOverride(150.f).HeightOverride(7.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SCampusAngularPanel).Fill(FLinearColor(1.f, 1.f, 1.f, 0.13f))]
					+ SOverlay::Slot().HAlign(HAlign_Left)
					[
						SNew(SBox).WidthOverride(150.f * Row.Fill01)
						[
							SNew(SCampusAngularPanel).Fill(Row.Delta == 0 ? CampusWeaponsPage::Steel : Mark)
						]
					]
				]
			]
			// The arrow and the number, written out: an up never depends on telling green from red.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(56.f)
				[
					SNew(STextBlock)
					.Text(Row.Delta == 0 ? FText::GetEmpty()
						: FText::FromString(FString::Printf(TEXT("%s %d%%"), Row.Delta > 0 ? TEXT("▲") : TEXT("▼"), FMath::Abs(Row.Delta))))
					.Font(CampusAngular::Numeric(13)).Justification(ETextJustify::Right)
					.ColorAndOpacity(Mark)
				]
			]
		];
	}
	return SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Notched(14.f))
		.Fill(CampusWeaponsPage::PanelFill)
		.Border(CampusAngular::Hairline).BorderWidth(1.f)
		.ContentPadding(FMargin(14.f, 10.f))
		[
			Bars
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildWeaponsSkins()
{
	const UCampusBackend* Api = Backend();
	const FString* Worn = Api ? Api->GunSkins.Find(WeaponsGun) : nullptr;
	const uint8 WornStyle = Worn ? CampusGunSkins::StyleFor(*Worn) : uint8(CampusGunSkins::None);
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (uint8 Style = CampusGunSkins::None; Style < CampusGunSkins::Count; ++Style)
	{
		const FCampusGunSkinItem* Item = nullptr;
		if (Api && Style != CampusGunSkins::None)
		{
			const FString SkinId = CampusGunSkins::SkinIdFor(FName(*WeaponsGun), Style);
			Item = Api->GunSkinShop.FindByPredicate([&SkinId](const FCampusGunSkinItem& Each) { return Each.Id == SkinId; });
			if (!Item)
			{
				continue; // the store does not sell this one for this gun
			}
		}
		const bool bOn = Style == WeaponsStyle;
		const bool bWorn = Style == WornStyle;
		const bool bOwned = Style == CampusGunSkins::None || (Item && Item->bOwned);
		const FLinearColor Frame = CampusWeaponsPage::StyleColor(Style);
		const FText Status = bWorn ? LOCTEXT("SkinWornTag", "WORN")
			: (bOwned ? LOCTEXT("SkinOwnedTag", "OWNED") : FText::AsNumber(Item ? Item->PriceDiamonds : 0));
		const FString Gun = WeaponsGun;
		Row->AddSlot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SBox).WidthOverride(132.f).HeightOverride(86.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Notched(10.f))
				.Fill(bOn ? FLinearColor(Frame.R * 0.25f, Frame.G * 0.25f, Frame.B * 0.25f, 0.95f) : CampusWeaponsPage::Tile)
				.Border(bOn ? Frame : CampusAngular::Hairline)
				.BorderWidth(bOn ? 2.f : 1.f)
				.ContentPadding(FMargin(10.f, 8.f))
				.OnClicked(FOnClicked::CreateLambda([this, Gun, Style]() { WeaponsSelect(Gun, Style); return FReply::Handled(); }))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(6.f)).Fill(Frame)
						[
							SNew(SBox).HeightOverride(10.f)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(CampusGunSkins::Label(Style)).Font(CampusAngular::Display(14))
						.ColorAndOpacity(FLinearColor::White)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 5.f, 0.f)
						[
							SNew(SCampusGlyph)
							.Glyph(bWorn ? CampusAngular::EGlyph::Tick : (bOwned ? CampusAngular::EGlyph::Rank : CampusAngular::EGlyph::Gem))
							.Size(13.f)
							.Color(bWorn ? Go : (bOwned ? CampusWeaponsPage::Steel : CampusWeaponsPage::Gem))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(Status).Font(CampusAngular::Numeric(13))
							.ColorAndOpacity(bWorn ? Go : (bOwned ? CampusWeaponsPage::Steel : FLinearColor::White))
						]
					]
				]
			]
		];
	}
	return SNew(SScrollBox).Orientation(Orient_Horizontal)
		+ SScrollBox::Slot()[Row];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildWeaponsAction()
{
	const UCampusBackend* Api = Backend();
	const FCampusGun* Gun = Api ? Api->FindGun(WeaponsGun) : nullptr;
	if (!Api || !Gun)
	{
		return SNullWidget::NullWidget;
	}
	const bool bOwnsGun = Gun->bOwned || Gun->bStarter;
	const FString SkinId = CampusGunSkins::SkinIdFor(FName(*WeaponsGun), WeaponsStyle);
	const FCampusGunSkinItem* Skin = SkinId.IsEmpty() ? nullptr
		: Api->GunSkinShop.FindByPredicate([&SkinId](const FCampusGunSkinItem& Each) { return Each.Id == SkinId; });
	const FString* Worn = Api->GunSkins.Find(WeaponsGun);
	const bool bSkinWorn = (Worn ? *Worn : FString()) == SkinId;
	const bool bAllSet = bSkinWorn && (!bOwnsGun || Api->EquippedGun == WeaponsGun);

	TSharedRef<SWidget> Action = SNullWidget::NullWidget;
	if (Skin && !Skin->bOwned)
	{
		// A skin can be bought for a gun you do not own yet: it paints (and tunes) the ones you pick up.
		const FString Name = Skin->Name;
		Action = Api->Diamonds >= Skin->PriceDiamonds
			? Button(FText::Format(LOCTEXT("BuySkin", "BUY  {0}"), FText::AsNumber(Skin->PriceDiamonds)),
				FOnClicked::CreateLambda([this, SkinId, Name]() { StoreBuySkin(SkinId, Name); return FReply::Handled(); }),
				EButton::Go, 22, FVector2D(230.f, 70.f))
			: Button(LOCTEXT("GetDiamonds", "GET DIAMONDS"),
				FOnClicked::CreateLambda([this]() { StoreTab = EStoreTab::Packs; Show(EScreen::Store); return FReply::Handled(); }),
				EButton::Dark, 18, FVector2D(230.f, 70.f));
	}
	else if (!bOwnsGun && WeaponsStyle == CampusGunSkins::None && Gun->PriceDiamonds > 0)
	{
		const int32 Index = Api->Guns.IndexOfByPredicate([this](const FCampusGun& Each) { return Each.Id == WeaponsGun; });
		Action = Api->Diamonds >= Gun->PriceDiamonds
			? Button(FText::Format(LOCTEXT("BuyGun", "BUY GUN  {0}"), FText::AsNumber(Gun->PriceDiamonds)),
				FOnClicked::CreateLambda([this, Index]() { StoreBuyGun(Index); return FReply::Handled(); }),
				EButton::Go, 20, FVector2D(230.f, 70.f))
			: Button(LOCTEXT("GetDiamonds2", "GET DIAMONDS"),
				FOnClicked::CreateLambda([this]() { StoreTab = EStoreTab::Packs; Show(EScreen::Store); return FReply::Handled(); }),
				EButton::Dark, 18, FVector2D(230.f, 70.f));
	}
	else if (bAllSet)
	{
		Action = Button(LOCTEXT("Equipped", "EQUIPPED"), FOnClicked::CreateLambda([]() { return FReply::Handled(); }),
			EButton::Ghost, 22, FVector2D(230.f, 70.f), false);
	}
	else
	{
		Action = StartButton(LOCTEXT("Equip", "EQUIP"),
			FOnClicked::CreateLambda([this]() { WeaponsEquip(); return FReply::Handled(); }), FVector2D(230.f, 76.f));
	}
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 8.f)
		[
			!bOwnsGun
				? StaticCastSharedRef<SWidget>(Label(LOCTEXT("NotOwnedGun", "GUN NOT OWNED - SKINS WORK ON PICKED-UP ONES"), 12, CampusWeaponsPage::Steel))
				: SNullWidget::NullWidget
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[Action];
}

#undef LOCTEXT_NAMESPACE
