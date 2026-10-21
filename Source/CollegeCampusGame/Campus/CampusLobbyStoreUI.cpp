// THE OLD TEXT STORE (Slate, lobby level) - the plain text-card version EScreen::Store used to be
// served by, before BuildStoreV2 in Campus/CampusStoreUI.cpp replaced it.
//
// *** NOTHING IN THIS FILE IS REACHABLE. *** Splitting it out of CampusLobbyUI.cpp is what made that
// visible: Show() routes EScreen::Store to BuildStoreV2, the new store brought its own StoreBilling()
// and its own card builders, and every function below is now called only by another function below.
// Confirmed across the whole module:
//
//   BuildStore()      no caller anywhere
//   BuildGunCard()    BuildStore only          BuildOfferCard()  BuildStore only
//   MoneyButton()     BuildGunCard / BuildOfferCard only
//   Billing()         BuildStore / MoneyButton only (the live store uses StoreBilling instead)
//
// IT IS LEFT STANDING DELIBERATELY, for now. The split it arrived in was a no-behaviour-change move,
// and deleting a screen is a decision for whoever owns the store - these 300-odd lines, plus the five
// declarations in Campus/CampusUI.h, are the whole of it and they come out in one cut. The reason to
// want them gone is not the line count: it is that two stores in the tree means the next person to
// fix a price or a purchase path has a 50 % chance of fixing the one nobody can open.
//
// Until then, what it still documents is worth keeping straight: REAL MONEY GOES THROUGH GOOGLE PLAY
// wherever Play Billing is usable and falls back to Razorpay otherwise, and neither path is trusted
// on the device. The server verifies the purchase token or the payment signature and answers with the
// new wallet, and that answer is the only thing the counters are ever read from.
//
// Split out of Campus/CampusLobbyUI.cpp; Campus/CampusLobbyUIInternal.h carries the file map.

#include "Campus/CampusUI.h"

#include "Campus/CampusBackend.h"
#include "Campus/CampusLobbyUIInternal.h"
#include "Campus/CampusPlayBilling.h"
#include "Campus/CampusPlayerControllers.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusLobbyUI"

using namespace CampusStyle;

namespace
{
	FString Money(float Inr)
	{
		return FString::Printf(TEXT("₹%s"), *FString::SanitizeFloat(Inr, 0));
	}
}

// ---------------------------------------------------------------------------------------------
// Store: text cards until there are icons.
// Real money goes through Google Play where Play Billing is usable, and falls back to Razorpay
// everywhere else. Play's own localized price string is the only price ever shown for a Play
// product - never a rupee figure we worked out ourselves.

UCampusPlayBilling* SCampusLobbyWidget::Billing() const
{
	UGameInstance* Instance = Controller.IsValid() ? Controller->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UCampusPlayBilling>() : nullptr;
}

TSharedRef<SWidget> SCampusLobbyWidget::MoneyButton(const FString& ItemId, const FString& ItemName, float PriceInr, FVector2D Size)
{
	const UCampusBackend* Api = Backend();
	UCampusPlayBilling* Play = Billing();
	const FString Product = Api ? Api->GooglePlayProductFor(ItemId) : FString();

	if (Play && Play->IsAvailable() && !Product.IsEmpty())
	{
		// Play knows this product: use its price text, and keep the button out of action while a
		// purchase is already in flight.
		const FCampusPlayProduct* Known = Play->FindProduct(Product);
		const FText Price = Known && !Known->PriceText.IsEmpty()
			? FText::FromString(Known->PriceText)
			: LOCTEXT("PlayPrice", "BUY");
		const bool bEnabled = !Play->IsPurchasing();
		return Button(Price, FOnClicked::CreateLambda([this, Product, ItemName]()
		{
			UCampusPlayBilling* Play2 = Billing();
			if (!Play2 || Play2->IsPurchasing())
			{
				return FReply::Handled();
			}
			Busy(TEXT("Opening Google Play..."));
			Play2->Purchase(Product, [this, ItemName](ECampusPurchaseResult Result, const FString& Error)
			{
				bBusy = false;
				switch (Result)
				{
				case ECampusPurchaseResult::Credited:
					// The backend credited the wallet as part of the verify; re-read it so the
					// counters and the owned flags are right without guessing.
					if (UCampusBackend* Api2 = Backend())
					{
						Api2->LoadStore([this, ItemName](bool, const FString&)
						{
							Toast(ItemName + TEXT(" is yours!"));
							Refresh();
						});
					}
					return;
				case ECampusPurchaseResult::Pending:
					Toast(TEXT("Payment in progress. It lands as soon as Google confirms it."));
					break;
				case ECampusPurchaseResult::Cancelled:
					break;   // the player backed out; say nothing
				case ECampusPurchaseResult::Unavailable:
					Toast(TEXT("Google Play is not available on this device."));
					break;
				default:
					Toast(Error.IsEmpty() ? TEXT("The payment did not go through.") : Error);
					break;
				}
				Refresh();
			});
			return FReply::Handled();
		}), EButton::Go, 16, Size, bEnabled);
	}

	// Razorpay fallback: unchanged behaviour.
	return Button(FText::FromString(Money(PriceInr)), FOnClicked::CreateLambda([this, ItemId, ItemName]()
	{
		if (UCampusBackend* Api2 = Backend())
		{
			Busy(TEXT("Opening payment..."));
			Api2->BuyWithMoney(ItemId, [this, ItemName](bool bOk, const FString& Error)
			{
				Done(bOk, Error, [this, ItemName]() { Toast(ItemName + TEXT(" is yours!")); });
			});
		}
		return FReply::Handled();
	}), EButton::Go, 16, Size);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildGunCard(int32 Index)
{
	UCampusBackend* Api = Backend();
	const FCampusGun Gun = Api->Guns[Index];
	const bool bEquipped = Api->EquippedGun == Gun.Id;

	TSharedRef<SVerticalBox> Perks = SNew(SVerticalBox);
	for (const FString& Perk : Gun.Perks)
	{
		Perks->AddSlot().AutoHeight()[Label(FText::FromString(TEXT("• ") + Perk), 15, FLinearColor::White, false)];
	}
	const FString Stats = FString::Printf(TEXT("DMG %s   RATE %sx   ACC %d%%   MAG %d"),
		*FString::SanitizeFloat(Gun.Damage, 0), *FString::SanitizeFloat(Gun.FireRate, 1), FMath::RoundToInt(Gun.Accuracy), Gun.Magazine);

	auto Act = [this](TFunction<void(UCampusBackend*, UCampusBackend::FDone)> Call, const FString& What, const FString& OkText)
	{
		return [this, Call, What, OkText]()
		{
			if (UCampusBackend* Api2 = Backend())
			{
				Busy(What);
				Call(Api2, [this, OkText](bool bOk, const FString& Error) { Done(bOk, Error, [this, OkText]() { Toast(OkText); }); });
			}
			return FReply::Handled();
		};
	};
	const FString Id = Gun.Id;

	TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
	if (!Gun.bOwned)
	{
		if (Gun.PriceDiamonds > 0)
		{
			Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				Button(FText::FromString(FString::Printf(TEXT("%d DIAMONDS"), Gun.PriceDiamonds)), FOnClicked::CreateLambda(Act(
					[Id](UCampusBackend* A, UCampusBackend::FDone D) { A->BuyGun(Id, D); }, TEXT("Buying..."), Gun.Name + TEXT(" is yours!"))), EButton::Accent, 16, FVector2D(170.f, 50.f))
			];
		}
	}
	else
	{
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			Button(bEquipped ? LOCTEXT("Equipped", "EQUIPPED") : LOCTEXT("Equip", "EQUIP"), FOnClicked::CreateLambda(Act(
				[Id](UCampusBackend* A, UCampusBackend::FDone D) { A->EquipGun(Id, D); }, TEXT("Equipping..."), Gun.Name + TEXT(" equipped"))),
				bEquipped ? EButton::Ghost : EButton::Go, 16, FVector2D(120.f, 50.f), !bEquipped)
		];
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			Button(Gun.NextUpgradeDiamonds > 0 ? FText::FromString(FString::Printf(TEXT("UPGRADE  %d"), Gun.NextUpgradeDiamonds)) : LOCTEXT("Max", "MAX LEVEL"),
				FOnClicked::CreateLambda(Act([Id](UCampusBackend* A, UCampusBackend::FDone D) { A->UpgradeGun(Id, D); }, TEXT("Upgrading..."), Gun.Name + TEXT(" upgraded!"))),
				Gun.NextUpgradeDiamonds > 0 ? EButton::Accent : EButton::Ghost, 16, FVector2D(170.f, 50.f), Gun.NextUpgradeDiamonds > 0)
		];
		if (!Gun.bStarter)
		{
			Actions->AddSlot().AutoWidth()
			[
				Button(FText::FromString(FString::Printf(TEXT("SELL +%d"), Gun.SellDiamonds)), FOnClicked::CreateLambda(Act(
					[Id](UCampusBackend* A, UCampusBackend::FDone D) { A->SellGun(Id, D); }, TEXT("Selling..."), FString::Printf(TEXT("Sold for %d Diamonds"), Gun.SellDiamonds))),
					EButton::Dark, 16, FVector2D(130.f, 50.f))
			];
		}
	}

	FString Title = Gun.Name;
	if (Gun.bOwned)
	{
		Title += FString::Printf(TEXT("   Lv %d/%d"), Gun.Level, Gun.MaxLevel);
	}
	return SNew(SBox).WidthOverride(420.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(bEquipped ? FLinearColor(0.10f, 0.08f, 0.03f, 0.94f) : FLinearColor(0.03f, 0.04f, 0.06f, 0.9f))
			.Border(bEquipped ? Accent : CampusAngular::Hairline)
			.BorderWidth(bEquipped ? 2.f : 1.f)
			.AccentEdge(bEquipped ? Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bEquipped ? 5.f : 0.f)
			.ContentPadding(FMargin(22.f, 12.f, 18.f, 12.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[Label(FText::FromString(Title), 24)]
					+ SHorizontalBox::Slot().AutoWidth()[Label(FText::FromString(Gun.Badge), 14, Accent)]
				]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Gun.Description), 14, Muted, false)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[Perks]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Stats), 14, Diamond)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[Actions]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildOfferCard(const FCampusOffer& Offer)
{
	TSharedRef<SVerticalBox> Perks = SNew(SVerticalBox);
	for (const FString& Perk : Offer.Perks)
	{
		Perks->AddSlot().AutoHeight()[Label(FText::FromString(TEXT("• ") + Perk), 16, FLinearColor::White, false)];
	}
	const FString Id = Offer.Id;
	return SNew(SBox).WidthOverride(330.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(Offer.Badge.IsEmpty() ? FLinearColor(0.03f, 0.04f, 0.06f, 0.9f) : FLinearColor(0.10f, 0.08f, 0.03f, 0.94f))
			.Border(Offer.Badge.IsEmpty() ? CampusAngular::Hairline : Accent)
			.BorderWidth(Offer.Badge.IsEmpty() ? 1.f : 2.f)
			.AccentEdge(Offer.Badge.IsEmpty() ? FLinearColor::Transparent : Accent)
			.AccentEdgeWidth(Offer.Badge.IsEmpty() ? 0.f : 5.f)
			.ContentPadding(FMargin(22.f, 12.f, 18.f, 12.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Offer.Badge), 14, Accent)]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Offer.Name), 24)]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Offer.Description), 14, Muted, false)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)[Perks]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					MoneyButton(Id, Offer.Name, Offer.PriceInr, FVector2D(290.f, 56.f))
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildStore()
{
	UCampusBackend* Api = Backend();
	auto Tab = [this](const FText& Text, EStoreTab Which)
	{
		return CampusStyle::Pill(Text, StoreTab == Which,
			FOnClicked::CreateLambda([this, Which]() { StoreTab = Which; Refresh(); return FReply::Handled(); }), FVector2D(186.f, 52.f));
	};

	// Ask Play for its localized prices once per store visit, then redraw. Products Play has never
	// heard of simply do not come back, which is why the button draws from FindProduct, not from
	// the ids we asked for.
	UCampusPlayBilling* Play = Billing();
	if (Play && Play->IsAvailable() && Api && Play->GetProducts().Num() == 0)
	{
		TArray<FString> Wanted;
		for (const FCampusOffer& Offer : Api->Offers) { Wanted.AddUnique(Api->GooglePlayProductFor(Offer.Id)); }
		for (const FCampusOffer& Pack : Api->Packs) { Wanted.AddUnique(Api->GooglePlayProductFor(Pack.Id)); }
		for (const FCampusGun& Gun : Api->Guns) { Wanted.AddUnique(Api->GooglePlayProductFor(Gun.Id)); }
		Wanted.Remove(FString());
		if (Wanted.Num() > 0)
		{
			Play->QueryProducts(Wanted, [this](bool bOk, const FString&, const TArray<FCampusPlayProduct>&)
			{
				if (bOk && Current == EScreen::Store)
				{
					Refresh();
				}
			});
		}
	}

	TSharedRef<SScrollBox> Row = SNew(SScrollBox).Orientation(Orient_Horizontal);
	if (Api)
	{
		if (StoreTab == EStoreTab::Guns)
		{
			for (int32 Index = 0; Index < Api->Guns.Num(); ++Index)
			{
				Row->AddSlot().Padding(6.f, 0.f)[BuildGunCard(Index)];
			}
		}
		else
		{
			for (const FCampusOffer& Offer : StoreTab == EStoreTab::Offers ? Api->Offers : Api->Packs)
			{
				Row->AddSlot().Padding(6.f, 0.f)[BuildOfferCard(Offer)];
			}
		}
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.72f)]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(24.f, 18.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
					[
						SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(10.f)).Fill(Accent)
						[
							SNew(SBox).WidthOverride(16.f).HeightOverride(34.f)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 20.f, 0.f)[Label(LOCTEXT("StoreTitle", "STORE"), 34, FLinearColor::White)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[Tab(LOCTEXT("TabGuns", "GUNS"), EStoreTab::Guns)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[Tab(LOCTEXT("TabOffers", "OFFERS"), EStoreTab::Offers)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[Tab(LOCTEXT("TabPacks", "DIAMONDS & CARDS"), EStoreTab::Packs)]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
						[
							CampusStyle::Counter(CampusAngular::EGlyph::Coin, CardGold, FText::AsNumber(Api ? Api->RoomCards : 0))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							CampusStyle::Counter(CampusAngular::EGlyph::Gem, Diamond, FText::AsNumber(Api ? Api->Diamonds : 0))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close,
							FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }), 64.f)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					Play && Play->IsAvailable()
						? StaticCastSharedRef<SWidget>(Label(LOCTEXT("PayPlay", "Pay with Google Play."), 15, Muted, false))
						: StaticCastSharedRef<SWidget>(Label(Api && Api->bPaymentsEnabled
							? LOCTEXT("PayOn", "Pay with UPI or card (Razorpay).")
							: LOCTEXT("PayOff", "Real-money payments switch on when Razorpay is connected. Diamond purchases work now."), 15, Muted, false))
				]
				// A UPI collect request Play is still waiting on: say so, so nobody buys twice.
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					Play && Play->GetPendingCount() > 0
						? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
							.Shape(FCampusShape::Slanted(12.f))
							.Fill(FLinearColor(0.35f, 0.26f, 0.02f, 0.9f))
							.ContentPadding(FMargin(16.f, 7.f))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PayPending", "PAYMENT IN PROGRESS  ·  IT LANDS AS SOON AS GOOGLE CONFIRMS IT"))
								.Font(CampusAngular::Display(14)).ColorAndOpacity(CardGold)
							])
						: SNullWidget::NullWidget
				]
				+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)[Row]
			]
		];
}

#undef LOCTEXT_NAMESPACE
