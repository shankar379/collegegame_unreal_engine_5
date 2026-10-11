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
						Label(FText::Format(LOCTEXT("Rules", "{0} rounds  ·  {1} min each  ·  every round is played"), FText::AsNumber(State ? State->Rounds : 5), FText::AsNumber(State ? State->RoundMinutes : 5)), 18, Muted, false)
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
