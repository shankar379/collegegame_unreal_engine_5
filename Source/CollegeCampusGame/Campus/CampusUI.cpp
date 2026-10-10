#include "Campus/CampusUI.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusUI"

namespace CampusStyle
{
	const FLinearColor Accent(FColor(0xF0, 0x8A, 0x24));
	const FLinearColor Go(FColor(0x2E, 0xB8, 0x5C));
	const FLinearColor Muted(1.f, 1.f, 1.f, 0.55f);

	FSlateFontInfo Font(int32 Size, bool bBold)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	const FSlateBrush* PanelBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor(0.035f, 0.045f, 0.06f, 0.88f), 18.f, FLinearColor(1.f, 1.f, 1.f, 0.08f), 1.f);
		return &Brush;
	}

	const FSlateBrush* CardBrush(bool bSelected)
	{
		static const FSlateRoundedBoxBrush Normal(FLinearColor(0.07f, 0.08f, 0.1f, 0.9f), 14.f, FLinearColor(1.f, 1.f, 1.f, 0.12f), 1.f);
		static const FSlateRoundedBoxBrush Selected(FLinearColor(0.07f, 0.08f, 0.1f, 0.95f), 14.f, Accent, 3.f);
		return bSelected ? &Selected : &Normal;
	}

	static const FButtonStyle& ButtonStyle(EButton Kind)
	{
		auto Make = [](const FLinearColor& Color)
		{
			const float Radius = 14.f;
			return FButtonStyle()
				.SetNormal(FSlateRoundedBoxBrush(Color, Radius))
				.SetHovered(FSlateRoundedBoxBrush(Color * FLinearColor(1.15f, 1.15f, 1.15f, 1.f), Radius))
				.SetPressed(FSlateRoundedBoxBrush(Color * FLinearColor(0.8f, 0.8f, 0.8f, 1.f), Radius))
				.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.2f, 0.2f, 0.22f, 0.6f), Radius))
				.SetNormalPadding(FMargin(12.f, 6.f))
				.SetPressedPadding(FMargin(12.f, 7.f, 12.f, 5.f));
		};
		static const FButtonStyle Dark = Make(FLinearColor(0.11f, 0.13f, 0.17f, 0.92f));
		static const FButtonStyle AccentStyle = Make(Accent);
		static const FButtonStyle GoStyle = Make(Go);
		static const FButtonStyle Ghost = Make(FLinearColor(1.f, 1.f, 1.f, 0.08f));
		switch (Kind)
		{
		case EButton::Accent: return AccentStyle;
		case EButton::Go: return GoStyle;
		case EButton::Ghost: return Ghost;
		default: return Dark;
		}
	}

	TSharedRef<SWidget> Button(const FText& Label, FOnClicked OnClicked, EButton Kind, int32 FontSize, FVector2D MinSize, bool bEnabled)
	{
		return SNew(SBox)
			.MinDesiredWidth(MinSize.X)
			.MinDesiredHeight(MinSize.Y)
			[
				SNew(SButton)
				.ButtonStyle(&ButtonStyle(Kind))
				.IsEnabled(bEnabled)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(Font(FontSize))
					.ColorAndOpacity(FLinearColor::White)
					.Justification(ETextJustify::Center)
				]
			];
	}

	TSharedRef<SWidget> Label(const FText& Text, int32 Size, FLinearColor Color, bool bBold)
	{
		return SNew(STextBlock).Text(Text).Font(Font(Size, bBold)).ColorAndOpacity(Color).AutoWrapText(false);
	}
}

using namespace CampusStyle;

// ---------------------------------------------------------------------------------------------
// Drag area

void SCampusDragArea::Construct(const FArguments& InArgs)
{
	OnDrag = InArgs._OnDrag;
}

void SCampusDragArea::Drag(float DeltaX, float DeltaTime)
{
	OnDrag.ExecuteIfBound(DeltaX);
	if (DeltaTime > UE_KINDA_SMALL_NUMBER)
	{
		// Smoothed finger speed, used for the fling when the finger lifts.
		Velocity = FMath::Lerp(Velocity, DeltaX / DeltaTime, 0.5f);
	}
}

void SCampusDragArea::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bDragging || PointerIndex != INDEX_NONE)
	{
		// A finger resting still should not fling on release.
		if (InCurrentTime - LastMoveTime > 0.08)
		{
			Velocity = 0.f;
		}
		return;
	}
	if (FMath::Abs(Velocity) > 5.f)
	{
		OnDrag.ExecuteIfBound(Velocity * InDeltaTime);
		Velocity *= FMath::Exp(-3.5f * InDeltaTime);
	}
	else
	{
		Velocity = 0.f;
	}
}

FReply SCampusDragArea::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bDragging = true;
	Velocity = 0.f;
	LastX = MouseEvent.GetScreenSpacePosition().X;
	LastMoveTime = FSlateApplication::Get().GetCurrentTime();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SCampusDragArea::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SCampusDragArea::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	const float X = MouseEvent.GetScreenSpacePosition().X;
	const double Now = FSlateApplication::Get().GetCurrentTime();
	Drag((X - LastX) / MyGeometry.Scale, float(Now - LastMoveTime));
	LastX = X;
	LastMoveTime = Now;
	return FReply::Handled();
}

FReply SCampusDragArea::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	if (PointerIndex != INDEX_NONE)
	{
		return FReply::Unhandled();
	}
	PointerIndex = TouchEvent.GetPointerIndex();
	Velocity = 0.f;
	LastX = TouchEvent.GetScreenSpacePosition().X;
	LastMoveTime = FSlateApplication::Get().GetCurrentTime();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SCampusDragArea::OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	if (TouchEvent.GetPointerIndex() != PointerIndex)
	{
		return FReply::Unhandled();
	}
	const float X = TouchEvent.GetScreenSpacePosition().X;
	const double Now = FSlateApplication::Get().GetCurrentTime();
	Drag((X - LastX) / MyGeometry.Scale, float(Now - LastMoveTime));
	LastX = X;
	LastMoveTime = Now;
	return FReply::Handled();
}

FReply SCampusDragArea::OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent)
{
	if (TouchEvent.GetPointerIndex() != PointerIndex)
	{
		return FReply::Unhandled();
	}
	PointerIndex = INDEX_NONE;
	return FReply::Handled().ReleaseMouseCapture();
}

// ---------------------------------------------------------------------------------------------
// Lobby

namespace
{
	struct FMapDef
	{
		const TCHAR* Name;
		bool bAvailable;
	};
	const FMapDef Maps[] = { { TEXT("Campus"), true }, { TEXT("Library"), false }, { TEXT("Hostel"), false } };

	struct FShopItem
	{
		const TCHAR* Id;
		const TCHAR* Title;
		int32 Price;
	};
	const FShopItem Outfits[] = { { TEXT("Default"), TEXT("Campus Classic"), 0 }, { TEXT("Hoodie"), TEXT("College Hoodie"), 300 }, { TEXT("Tracksuit"), TEXT("Red Tracksuit"), 500 } };
	const FShopItem Guns[] = { { TEXT("Rifle"), TEXT("AK Rifle"), 0 }, { TEXT("SMG"), TEXT("SMG"), 800 }, { TEXT("Sniper"), TEXT("Sniper"), 1500 } };
	const TCHAR* Skills[] = { TEXT("Health"), TEXT("Reload"), TEXT("Sprint") };
}

void SCampusLobbyWidget::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(Content, SBox)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.f, 90.f, 0.f, 0.f)
		[
			SNew(SBorder)
			.BorderImage(PanelBrush())
			.Padding(FMargin(22.f, 12.f))
			.Visibility_Lambda([this]() { return ToastTime > 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(ToastText); })
				.Font(Font(20))
				.ColorAndOpacity(FLinearColor::White)
			]
		]
	];

	const UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr;
	Show(Instance && Instance->bSignedIn ? EScreen::Lobby : EScreen::Splash);
}

void SCampusLobbyWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ToastTime = FMath::Max(0.f, ToastTime - InDeltaTime);

	if (Current == EScreen::Splash)
	{
		SplashTime += InDeltaTime;
		if (SplashTime > 1.6f)
		{
			Show(EScreen::SignIn);
		}
	}

	// Network failures land back in the lobby; say why once.
	if (UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr)
	{
		if (!Instance->PendingMessage.IsEmpty() && Current != EScreen::Splash)
		{
			Toast(Instance->PendingMessage);
			Instance->PendingMessage.Reset();
		}
	}
}

void SCampusLobbyWidget::Toast(const FString& Message)
{
	ToastText = Message;
	ToastTime = 3.5f;
}

void SCampusLobbyWidget::Show(EScreen Screen)
{
	Current = Screen;
	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	switch (Screen)
	{
	case EScreen::Splash: Body = BuildSplash(); break;
	case EScreen::SignIn: Body = BuildSignIn(); break;
	case EScreen::Event: Body = BuildEvent(); break;
	case EScreen::Lobby: Body = BuildLobby(); break;
	case EScreen::Panel: Body = BuildPanel(); break;
	case EScreen::Rooms: Body = BuildRooms(); break;
	}
	Content->SetContent(Body);
}

void SCampusLobbyWidget::OpenPanel(EPanel Panel)
{
	CurrentPanel = Panel;
	Show(EScreen::Panel);
}

void SCampusLobbyWidget::Refresh()
{
	Show(Current);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildSplash()
{
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.025f, 0.035f, 1.f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				Label(LOCTEXT("Title", "CAMPUS COMBAT"), 64, Accent)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 28.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(420.f).HeightOverride(10.f)
				[
					SNew(SProgressBar)
					.Percent_Lambda([this]() { return TOptional<float>(FMath::Clamp(SplashTime / 1.5f, 0.f, 1.f)); })
					.FillColorAndOpacity(Accent)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 14.f, 0.f, 0.f)
			[
				Label(LOCTEXT("Loading", "Loading..."), 18, Muted, false)
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildSignIn()
{
	const UCampusSave* Profile = Controller.IsValid() ? Controller->GetProfile() : nullptr;
	const FString Name = Profile ? Profile->PlayerName : TEXT("Guest");

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.45f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(PanelBrush())
			.Padding(FMargin(48.f, 36.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Label(LOCTEXT("Title2", "CAMPUS COMBAT"), 44, Accent)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 26.f)
				[
					Label(FText::Format(LOCTEXT("PlayingAs", "You will play as {0}"), FText::FromString(Name)), 18, Muted, false)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(LOCTEXT("Guest", "PLAY AS GUEST"), FOnClicked::CreateLambda([this]()
					{
						if (UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr)
						{
							Instance->bSignedIn = true;
							const bool bShowEvent = !Instance->bEventSeen;
							Instance->bEventSeen = true;
							Show(bShowEvent ? EScreen::Event : EScreen::Lobby);
						}
						return FReply::Handled();
					}), EButton::Go, 26, FVector2D(420.f, 84.f))
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 14.f, 0.f, 0.f)
				[
					Button(LOCTEXT("Google", "Sign in with Google (soon)"), FOnClicked(), EButton::Ghost, 18, FVector2D(420.f, 56.f), false)
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildEvent()
{
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(760.f)
			[
				SNew(SBorder)
				.BorderImage(PanelBrush())
				.Padding(FMargin(36.f, 24.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							Label(LOCTEXT("LiveEvent", "LIVE EVENT"), 18, Accent)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Button(LOCTEXT("Close", "X"), FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }),
								EButton::Ghost, 22, FVector2D(64.f, 64.f))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
					[
						Label(LOCTEXT("EventTitle", "Weekend Double Coins"), 40)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 22.f)
					[
						Label(LOCTEXT("EventBody", "2x coins on every match  ·  event skin  ·  2 days left"), 20, Muted, false)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
					[
						Button(LOCTEXT("JoinEvent", "JOIN EVENT"), FOnClicked::CreateLambda([this]() { SelectedMap = 0; Show(EScreen::Lobby); return FReply::Handled(); }),
							EButton::Accent, 24, FVector2D(300.f, 72.f))
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildTopBar()
{
	const UCampusSave* Profile = Controller.IsValid() ? Controller->GetProfile() : nullptr;
	const FString Name = Profile ? Profile->PlayerName : TEXT("Guest");
	const int32 Level = Profile ? Profile->GetLevel() : 1;
	const int32 Coins = Profile ? Profile->Coins : 0;

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBorder).BorderImage(PanelBrush()).Padding(FMargin(20.f, 10.f))
			[
				Label(FText::FromString(FString::Printf(TEXT("%s  ·  Lv %d"), *Name, Level)), 22)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SSpacer)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBorder).BorderImage(PanelBrush()).Padding(FMargin(20.f, 10.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					Label(LOCTEXT("Coins", "COINS"), 16, Accent)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Label(FText::AsNumber(Coins), 24)
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildMapCard(int32 Index)
{
	const FMapDef& Map = Maps[Index];
	const bool bSelected = SelectedMap == Index;
	return SNew(SBox).WidthOverride(190.f).HeightOverride(96.f)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.IsEnabled(Map.bAvailable)
			.OnClicked_Lambda([this, Index]() { SelectedMap = Index; Refresh(); return FReply::Handled(); })
			[
				SNew(SBorder)
				.BorderImage(CardBrush(bSelected))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						Label(FText::FromString(Map.Name), 22, Map.bAvailable ? FLinearColor::White : Muted)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						Label(Map.bAvailable ? (bSelected ? LOCTEXT("Selected", "selected") : LOCTEXT("Tap", "tap to pick")) : LOCTEXT("Soon", "soon"), 14, Muted, false)
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildLobby()
{
	auto SideButton = [this](const FText& Text, EPanel Panel)
	{
		return Button(Text, FOnClicked::CreateLambda([this, Panel]() { OpenPanel(Panel); return FReply::Handled(); }), EButton::Dark, 20, FVector2D(230.f, 64.f));
	};

	TSharedRef<SHorizontalBox> MapRow = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Maps); ++Index)
	{
		MapRow->AddSlot().AutoWidth().Padding(6.f, 0.f)[BuildMapCard(Index)];
	}

	return SNew(SOverlay)
		// Middle of the screen: drag to turn the character.
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
			SNew(SSafeZone)
			.Padding(FMargin(24.f, 18.f))
			[
				SNew(SOverlay)
				+ SOverlay::Slot().VAlign(VAlign_Top)
				[
					BuildTopBar()
				]
				// Left: dress up and upgrade.
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[SideButton(LOCTEXT("Wardrobe", "Wardrobe"), EPanel::Wardrobe)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[SideButton(LOCTEXT("Upgrade", "Upgrade skills"), EPanel::Upgrade)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[SideButton(LOCTEXT("Loadout", "Loadout"), EPanel::Loadout)]
				]
				// Right: shop, rewards, stats, events.
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[SideButton(LOCTEXT("Store", "Store"), EPanel::Store)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[SideButton(LOCTEXT("GunShop", "Gun shop"), EPanel::GunShop)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[SideButton(LOCTEXT("Daily", "Daily reward"), EPanel::Daily)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[SideButton(LOCTEXT("Stats", "Stats"), EPanel::Stats)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
					[
						Button(LOCTEXT("Events", "Events"), FOnClicked::CreateLambda([this]() { Show(EScreen::Event); return FReply::Handled(); }),
							EButton::Dark, 20, FVector2D(230.f, 64.f))
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 120.f)
				[
					Label(LOCTEXT("DragHint", "drag to turn"), 16, Muted, false)
				]
				// Bottom: map cards, then START (the only green control).
				+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
					[
						MapRow
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Button(LOCTEXT("Start", "START"), FOnClicked::CreateLambda([this]() { Show(EScreen::Rooms); return FReply::Handled(); }),
							EButton::Go, 34, FVector2D(260.f, 96.f))
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::ItemRow(const FString& Title, const FString& Detail, const FString& Action, bool bEnabled, TFunction<void()> OnAction)
{
	return SNew(SBorder)
		.BorderImage(CardBrush(false))
		.Padding(FMargin(18.f, 10.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Title), 22)]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Detail), 16, Muted, false)]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Button(FText::FromString(Action), FOnClicked::CreateLambda([this, OnAction]()
				{
					if (OnAction)
					{
						OnAction();
					}
					Refresh();
					return FReply::Handled();
				}), bEnabled ? EButton::Accent : EButton::Ghost, 18, FVector2D(170.f, 52.f), bEnabled)
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildPanel()
{
	UCampusSave* Profile = Controller.IsValid() ? Controller->GetProfile() : nullptr;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	FText Title;
	auto AddRow = [&List](TSharedRef<SWidget> Row) { List->AddSlot().AutoHeight().Padding(0.f, 5.f)[Row]; };
	auto Buy = [this, Profile](const FShopItem& Item)
	{
		if (Profile && Profile->TrySpend(Item.Price))
		{
			Profile->OwnedItems.AddUnique(Item.Id);
			Profile->Persist();
			Toast(FString::Printf(TEXT("Bought %s"), Item.Title));
		}
		else
		{
			Toast(TEXT("Not enough coins"));
		}
	};

	if (Profile)
	{
		switch (CurrentPanel)
		{
		case EPanel::Wardrobe:
			Title = LOCTEXT("WardrobeTitle", "WARDROBE");
			for (const FShopItem& Item : Outfits)
			{
				const bool bOwned = Profile->OwnedItems.Contains(Item.Id);
				const bool bEquipped = Profile->EquippedOutfit == Item.Id;
				AddRow(ItemRow(Item.Title, bOwned ? TEXT("Owned") : FString::Printf(TEXT("%d coins"), Item.Price),
					bEquipped ? TEXT("EQUIPPED") : (bOwned ? TEXT("EQUIP") : TEXT("BUY")), !bEquipped,
					[Profile, Item, bOwned, Buy]()
					{
						if (bOwned)
						{
							Profile->EquippedOutfit = Item.Id;
							Profile->Persist();
						}
						else
						{
							Buy(Item);
						}
					}));
			}
			break;
		case EPanel::Upgrade:
			Title = LOCTEXT("UpgradeTitle", "UPGRADE SKILLS");
			for (const TCHAR* Skill : Skills)
			{
				const int32 Level = Profile->SkillLevels.FindRef(Skill);
				const int32 Cost = 200 * (Level + 1);
				AddRow(ItemRow(Skill, FString::Printf(TEXT("Level %d / 5"), Level), Level >= 5 ? TEXT("MAX") : FString::Printf(TEXT("%d coins"), Cost), Level < 5,
					[this, Profile, Skill, Level, Cost]()
					{
						if (Profile->TrySpend(Cost))
						{
							Profile->SkillLevels.Add(Skill, Level + 1);
							Profile->Persist();
						}
						else
						{
							Toast(TEXT("Not enough coins"));
						}
					}));
			}
			break;
		case EPanel::Loadout:
			Title = LOCTEXT("LoadoutTitle", "LOADOUT");
			for (const FShopItem& Item : Guns)
			{
				const bool bOwned = Profile->OwnedItems.Contains(Item.Id);
				const bool bEquipped = Profile->EquippedGun == Item.Id;
				AddRow(ItemRow(Item.Title, bOwned ? TEXT("Owned") : TEXT("Buy it in the gun shop"), bEquipped ? TEXT("EQUIPPED") : TEXT("EQUIP"), bOwned && !bEquipped,
					[Profile, Item]() { Profile->EquippedGun = Item.Id; Profile->Persist(); }));
			}
			break;
		case EPanel::GunShop:
			Title = LOCTEXT("GunShopTitle", "GUN SHOP");
			for (const FShopItem& Item : Guns)
			{
				const bool bOwned = Profile->OwnedItems.Contains(Item.Id);
				AddRow(ItemRow(Item.Title, bOwned ? TEXT("Owned") : FString::Printf(TEXT("%d coins"), Item.Price), bOwned ? TEXT("OWNED") : TEXT("BUY"), !bOwned,
					[Item, Buy]() { Buy(Item); }));
			}
			break;
		case EPanel::Store:
			Title = LOCTEXT("StoreTitle", "STORE");
			AddRow(ItemRow(TEXT("1,000 coins"), TEXT("Payments arrive with the store release"), TEXT("SOON"), false, nullptr));
			AddRow(ItemRow(TEXT("Season pass"), TEXT("Payments arrive with the store release"), TEXT("SOON"), false, nullptr));
			break;
		case EPanel::Daily:
		{
			Title = LOCTEXT("DailyTitle", "DAILY REWARD");
			const bool bCan = Profile->CanClaimDaily();
			AddRow(ItemRow(TEXT("100 coins"), bCan ? TEXT("Ready to claim") : TEXT("Come back tomorrow"), bCan ? TEXT("CLAIM") : TEXT("CLAIMED"), bCan,
				[this, Profile]() { Profile->ClaimDaily(100); Toast(TEXT("+100 coins")); }));
			break;
		}
		case EPanel::Stats:
			Title = LOCTEXT("StatsTitle", "STATS");
			AddRow(ItemRow(TEXT("Level"), FString::Printf(TEXT("%d  (%d XP)"), Profile->GetLevel(), Profile->Xp), TEXT(""), false, nullptr));
			AddRow(ItemRow(TEXT("Matches"), FString::Printf(TEXT("%d played  ·  %d won"), Profile->Matches, Profile->Wins), TEXT(""), false, nullptr));
			AddRow(ItemRow(TEXT("Kills"), FString::FromInt(Profile->Kills), TEXT(""), false, nullptr));
			break;
		}
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.5f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(820.f).MaxDesiredHeight(620.f)
			[
				SNew(SBorder)
				.BorderImage(PanelBrush())
				.Padding(FMargin(30.f, 22.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							Label(Title, 28, Accent)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
						[
							Label(FText::Format(LOCTEXT("CoinsShort", "{0} coins"), FText::AsNumber(Profile ? Profile->Coins : 0)), 18, Muted)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Button(LOCTEXT("ClosePanel", "X"), FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }),
								EButton::Ghost, 22, FVector2D(64.f, 64.f))
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f, 0.f, 0.f)
					[
						SNew(SScrollBox) + SScrollBox::Slot()[List]
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildRooms()
{
	const UCampusNetSettings* Net = GetDefault<UCampusNetSettings>();
	auto Option = [](const FText& Title, const FText& Detail, TSharedRef<SWidget> Action)
	{
		return SNew(SBorder)
			.BorderImage(CardBrush(false))
			.Padding(FMargin(22.f, 14.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Label(Title, 24)]
					+ SVerticalBox::Slot().AutoHeight()[Label(Detail, 16, Muted, false)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Action]
			];
	};

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.45f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(0.f, 0.f, 40.f, 0.f)
		[
			SNew(SBox).WidthOverride(760.f)
			[
				SNew(SBorder)
				.BorderImage(PanelBrush())
				.Padding(FMargin(30.f, 22.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							Label(FText::Format(LOCTEXT("MapTitle", "MAP: {0}"), FText::FromString(Maps[SelectedMap].Name)), 28, Accent)
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Button(LOCTEXT("Back", "BACK"), FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }),
								EButton::Ghost, 18, FVector2D(120.f, 56.f))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 6.f)
					[
						Option(LOCTEXT("Create", "Create room"), LOCTEXT("CreateDetail", "Host on this phone  ·  5 min  ·  first to 10 kills"),
							Button(LOCTEXT("CreateBtn", "CREATE"), FOnClicked::CreateLambda([this]()
							{
								if (Controller.IsValid()) { Controller->HostRoom(); }
								return FReply::Handled();
							}), EButton::Accent, 20, FVector2D(170.f, 60.f)))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
					[
						Option(LOCTEXT("Join", "Join room"),
							FText::Format(LOCTEXT("JoinDetail", "Server {0}  (same Wi-Fi)"), FText::FromString(Net->GameAddress())),
							Button(LOCTEXT("JoinBtn", "JOIN"), FOnClicked::CreateLambda([this]()
							{
								if (Controller.IsValid()) { Toast(TEXT("Joining...")); Controller->JoinRoom(); }
								return FReply::Handled();
							}), EButton::Accent, 20, FVector2D(170.f, 60.f)))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
					[
						Option(LOCTEXT("Quick", "Quick match"), LOCTEXT("QuickDetail", "Joins the server room"),
							Button(LOCTEXT("QuickBtn", "PLAY"), FOnClicked::CreateLambda([this]()
							{
								if (Controller.IsValid()) { Toast(TEXT("Joining...")); Controller->JoinRoom(); }
								return FReply::Handled();
							}), EButton::Go, 20, FVector2D(170.f, 60.f)))
					]
				]
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Match: waiting room and win screen (the in-match HUD is SCampusTouchControls)

void SCampusMatchWidget::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	SetVisibility(EVisibility::SelfHitTestInvisible);
	ChildSlot
	[
		SAssignNew(Content, SBox)
		.Visibility(EVisibility::SelfHitTestInvisible)
	];
}

void SCampusMatchWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const int32 Phase = State ? int32(State->Phase) : -1;

	// Rebuild only when the phase or the room's players change; rebuilding every frame makes it flicker.
	const FString Signature = Phase == int32(ECampusPhase::Waiting) ? RoomSignature() : FString();
	if (Phase == ShownPhase && Signature == ShownSignature)
	{
		return;
	}
	ShownPhase = Phase;
	ShownSignature = Signature;

	switch (Phase)
	{
	case int32(ECampusPhase::Waiting): Content->SetContent(BuildWaiting()); break;
	case int32(ECampusPhase::Ended): Content->SetContent(BuildWin()); break;
	default: Content->SetContent(SNullWidget::NullWidget); break;
	}
}

FString SCampusMatchWidget::RoomSignature() const
{
	const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const ACampusPlayerState* Mine = Controller.IsValid() ? Controller->GetPlayerState<ACampusPlayerState>() : nullptr;
	FString Signature = State ? State->RoomCode : FString();
	Signature += Mine && Mine->bIsHost ? TEXT("|H") : TEXT("|-");
	if (State)
	{
		for (const APlayerState* Player : State->PlayerArray)
		{
			if (const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player))
			{
				Signature += FString::Printf(TEXT("|%s:%d:%d"), *Campus->GetPlayerName(), int32(Campus->Team), Campus->bIsHost ? 1 : 0);
			}
		}
	}
	return Signature;
}

TSharedRef<SWidget> SCampusMatchWidget::TeamColumn(uint8 Team)
{
	const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const bool bRed = Team == uint8(ECampusTeam::Red);
	TSharedRef<SVerticalBox> Names = SNew(SVerticalBox);
	Names->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
	[
		Label(bRed ? LOCTEXT("Red", "RED") : LOCTEXT("Blue", "BLUE"), 26, bRed ? FLinearColor(FColor(0xE5, 0x3E, 0x3E)) : FLinearColor(FColor(0x3E, 0x8E, 0xE5)))
	];
	if (State)
	{
		for (const APlayerState* Player : State->PlayerArray)
		{
			const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player);
			if (Campus && uint8(Campus->Team) == Team)
			{
				const bool bMe = Controller.IsValid() && Controller->PlayerState == Campus;
				Names->AddSlot().AutoHeight().Padding(0.f, 3.f)
				[
					Label(FText::FromString(Campus->GetPlayerName() + (Campus->bIsHost ? TEXT("  (host)") : TEXT(""))), 20, bMe ? Accent : FLinearColor::White, bMe)
				];
			}
		}
	}
	return SNew(SBox).WidthOverride(300.f)
		[
			SNew(SBorder).BorderImage(CardBrush(false)).Padding(FMargin(20.f, 16.f))[Names]
		];
}

TSharedRef<SWidget> SCampusMatchWidget::BuildWaiting()
{
	const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const ACampusPlayerState* Mine = Controller.IsValid() ? Controller->GetPlayerState<ACampusPlayerState>() : nullptr;
	const bool bHost = Mine && Mine->bIsHost;
	const int32 Count = State ? State->PlayerArray.Num() : 0;

	TSharedRef<SWidget> StartArea = bHost
		? Button(LOCTEXT("StartMatch", "START MATCH"), FOnClicked::CreateLambda([this]()
			{
				if (Controller.IsValid()) { Controller->ServerRequestStart(); }
				return FReply::Handled();
			}), EButton::Go, 28, FVector2D(320.f, 84.f))
		: Label(FText::Format(LOCTEXT("WaitingHost", "Waiting for the host  ·  {0} players"), FText::AsNumber(Count)), 20, Muted, false);

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.35f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(PanelBrush())
			.Padding(FMargin(36.f, 26.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						Label(FText::Format(LOCTEXT("Room", "ROOM  {0}"), FText::FromString(State ? State->RoomCode : TEXT("-----"))), 30, Accent)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Label(FText::Format(LOCTEXT("Rules", "Campus  ·  5 min  ·  first to {0}"), FText::AsNumber(State ? State->KillsToWin : 10)), 18, Muted, false)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 20.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)[TeamColumn(uint8(ECampusTeam::Red))]
					+ SHorizontalBox::Slot().AutoWidth().Padding(12.f, 0.f, 0.f, 0.f)[TeamColumn(uint8(ECampusTeam::Blue))]
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Button(LOCTEXT("Leave", "LEAVE"), FOnClicked::CreateLambda([this]()
						{
							if (Controller.IsValid()) { Controller->LeaveToLobby(); }
							return FReply::Handled();
						}), EButton::Ghost, 18, FVector2D(140.f, 64.f))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f)
					[
						Button(LOCTEXT("Switch", "SWITCH TEAM"), FOnClicked::CreateLambda([this]()
						{
							if (Controller.IsValid()) { Controller->ServerSwitchTeam(); }
							return FReply::Handled();
						}), EButton::Dark, 18, FVector2D(200.f, 64.f))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
					[
						StartArea
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusMatchWidget::BuildWin()
{
	const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	const ACampusGameState* State = World ? World->GetGameState<ACampusGameState>() : nullptr;
	const ACampusPlayerState* Mine = Controller.IsValid() ? Controller->GetPlayerState<ACampusPlayerState>() : nullptr;
	const int32 Winner = State ? State->Winner : -1;
	const bool bWon = Mine && Winner == int32(Mine->Team);

	FText Headline = Winner < 0 ? LOCTEXT("Draw", "DRAW") : (Winner == 0 ? LOCTEXT("RedWins", "RED WINS") : LOCTEXT("BlueWins", "BLUE WINS"));
	const int32 Reward = Controller.IsValid() ? Controller->ComputeReward() : 0;

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.6f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(PanelBrush())
			.Padding(FMargin(56.f, 34.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Label(Headline, 56, bWon ? Go : Accent)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f)
				[
					Label(FText::Format(LOCTEXT("Score", "RED {0}  -  {1} BLUE"), FText::AsNumber(State ? State->RedScore : 0), FText::AsNumber(State ? State->BlueScore : 0)), 24)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 24.f)
				[
					Label(FText::Format(LOCTEXT("YourStats", "You: {0} kills  ·  {1} deaths"), FText::AsNumber(Mine ? Mine->Kills : 0), FText::AsNumber(Mine ? Mine->Deaths : 0)), 20, Muted, false)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(FText::Format(LOCTEXT("Claim", "CLAIM {0} COINS"), FText::AsNumber(Reward)), FOnClicked::CreateLambda([this]()
					{
						if (Controller.IsValid()) { Controller->ClaimAndLeave(); }
						return FReply::Handled();
					}), EButton::Go, 28, FVector2D(380.f, 88.f))
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
