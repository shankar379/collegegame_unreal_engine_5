#include "Campus/CampusUI.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Campus/CampusAngular.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Scalability.h"
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
	const FLinearColor Accent = CampusAngular::Accent;
	const FLinearColor Go = CampusAngular::Go;
	const FLinearColor Muted = CampusAngular::TextMuted;

	FSlateFontInfo Font(int32 Size, bool bBold)
	{
		// Bold = the Free Fire display face (condensed italic). Not bold = paragraphs, which must
		// stay readable: condensed italic over several lines of terms text is not.
		return bBold ? CampusAngular::Display(Size) : CampusAngular::Body(Size);
	}

	const FSlateBrush* PanelBrush()
	{
		// Radius 0: even the fallback brush has sharp corners now.
		static const FSlateRoundedBoxBrush Brush(CampusAngular::Ink, 0.f, CampusAngular::Hairline, 1.f);
		return &Brush;
	}

	const FSlateBrush* CardBrush(bool bSelected)
	{
		static const FSlateRoundedBoxBrush Normal(FLinearColor(0.04f, 0.05f, 0.07f, 0.88f), 0.f, CampusAngular::Hairline, 1.f);
		static const FSlateRoundedBoxBrush Selected(FLinearColor(0.09f, 0.08f, 0.04f, 0.94f), 0.f, CampusAngular::Accent, 2.f);
		return bSelected ? &Selected : &Normal;
	}

	namespace
	{
		FLinearColor Face(EButton Kind)
		{
			switch (Kind)
			{
			case EButton::Accent: return CampusAngular::Accent;
			case EButton::Go: return CampusAngular::Go;
			case EButton::Ghost: return FLinearColor(1.f, 1.f, 1.f, 0.10f);
			default: return FLinearColor(0.07f, 0.09f, 0.12f, 0.92f);
			}
		}

		/** The accent plate carries dark text, like Free Fire's yellow button. */
		FLinearColor Ink(EButton Kind)
		{
			return Kind == EButton::Accent ? FLinearColor(0.06f, 0.05f, 0.02f, 1.f) : FLinearColor::White;
		}
	}

	TSharedRef<SWidget> Button(const FText& Label, FOnClicked OnClicked, EButton Kind, int32 FontSize, FVector2D MinSize, bool bEnabled)
	{
		// The slant scales with the plate height, so a 46px pill and a 96px button lean the same amount.
		const float Slant = FMath::Clamp(MinSize.Y * 0.26f, 8.f, 26.f);
		return SNew(SBox)
			.MinDesiredWidth(MinSize.X)
			.MinDesiredHeight(MinSize.Y)
			.IsEnabled(bEnabled)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Slanted(Slant))
				.Fill(Face(Kind))
				.Border(Kind == EButton::Dark || Kind == EButton::Ghost ? CampusAngular::Hairline : FLinearColor::Transparent)
				.BorderWidth(1.f)
				.ContentPadding(FMargin(16.f, 6.f))
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(Font(FontSize))
					.ColorAndOpacity(Ink(Kind))
					.Justification(ETextJustify::Center)
				]
			];
	}

	TSharedRef<SWidget> StartButton(const FText& Label, FOnClicked OnClicked, FVector2D MinSize)
	{
		return SNew(SBox).MinDesiredWidth(MinSize.X).MinDesiredHeight(MinSize.Y)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Wedge(MinSize.Y * 0.3f, MinSize.Y * 0.22f))
				.Fill(CampusAngular::Accent)
				.Border(FLinearColor(1.f, 1.f, 1.f, 0.35f))
				.BorderWidth(2.f)
				.ContentPadding(FMargin(26.f, 8.f))
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(CampusAngular::Display(FMath::RoundToInt(MinSize.Y * 0.36f)))
					.ColorAndOpacity(FLinearColor(0.06f, 0.05f, 0.02f, 1.f))
				]
			];
	}

	TSharedRef<SWidget> MenuTab(const FText& Label, CampusAngular::EGlyph Glyph, FOnClicked OnClicked, bool bHighlighted, FVector2D MinSize)
	{
		return SNew(SBox).MinDesiredWidth(MinSize.X).MinDesiredHeight(MinSize.Y)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Tab(MinSize.Y * 0.34f))
				.Fill(bHighlighted ? FLinearColor(0.16f, 0.12f, 0.02f, 0.92f) : FLinearColor(0.02f, 0.03f, 0.04f, 0.78f))
				.Border(bHighlighted ? CampusAngular::Accent : CampusAngular::Hairline)
				.BorderWidth(bHighlighted ? 2.f : 1.f)
				.AccentEdge(bHighlighted ? CampusAngular::Accent : FLinearColor(1.f, 1.f, 1.f, 0.22f))
				.AccentEdgeWidth(5.f)
				.ContentPadding(FMargin(MinSize.Y * 0.42f, 4.f, 16.f, 4.f))
				.OnClicked(OnClicked)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						SNew(SBox).WidthOverride(24.f).HeightOverride(24.f)
						[
							SNew(SCampusGlyph).Glyph(Glyph).Size(24.f).Color(bHighlighted ? CampusAngular::Accent : FLinearColor(1.f, 1.f, 1.f, 0.8f))
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Label).Font(CampusAngular::Display(21)).ColorAndOpacity(FLinearColor::White)
					]
				]
			];
	}

	TSharedRef<SWidget> SelectablePill(const FText& Label, TAttribute<bool> Selected, FOnClicked OnClicked, FVector2D MinSize)
	{
		return SNew(SBox).MinDesiredWidth(MinSize.X).MinDesiredHeight(MinSize.Y)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Slanted(MinSize.Y * 0.3f))
				.Fill_Lambda([Selected]()
				{
					return Selected.Get(false) ? CampusAngular::Accent : FLinearColor(0.04f, 0.05f, 0.07f, 0.85f);
				})
				.Border_Lambda([Selected]()
				{
					return Selected.Get(false) ? FLinearColor::Transparent : CampusAngular::Hairline;
				})
				.BorderWidth(1.f)
				.ContentPadding(FMargin(14.f, 4.f))
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(CampusAngular::Display(FMath::Max(14, FMath::RoundToInt(MinSize.Y * 0.38f))))
					.ColorAndOpacity_Lambda([Selected]()
					{
						return FSlateColor(Selected.Get(false) ? FLinearColor(0.06f, 0.05f, 0.02f, 1.f) : FLinearColor(1.f, 1.f, 1.f, 0.92f));
					})
				]
			];
	}

	TSharedRef<SWidget> Pill(const FText& Label, bool bSelected, FOnClicked OnClicked, FVector2D MinSize)
	{
		return SelectablePill(Label, bSelected, OnClicked, MinSize);
	}

	TSharedRef<SWidget> IconButton(CampusAngular::EGlyph Glyph, FOnClicked OnClicked, float Size, FLinearColor Tint, bool bAccentFill)
	{
		return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Notched(Size * 0.26f))
				.Fill(bAccentFill ? CampusAngular::Accent : FLinearColor(0.02f, 0.03f, 0.04f, 0.82f))
				.Border(bAccentFill ? FLinearColor::Transparent : CampusAngular::Hairline)
				.BorderWidth(1.f)
				.ContentPadding(FMargin(Size * 0.2f))
				.OnClicked(OnClicked)
				[
					SNew(SCampusGlyph)
					.Glyph(Glyph)
					.Size(Size * 0.56f)
					.Color(bAccentFill ? FLinearColor(0.06f, 0.05f, 0.02f, 1.f) : Tint)
				]
			];
	}

	TSharedRef<SWidget> Counter(CampusAngular::EGlyph Glyph, const FLinearColor& Tint, TAttribute<FText> Value, FOnClicked OnPlus)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Slanted(14.f))
				.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.84f))
				.Border(CampusAngular::Hairline)
				.BorderWidth(1.f)
				.ContentPadding(FMargin(20.f, 8.f, 18.f, 8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 9.f, 0.f)
					[
						SNew(SBox).WidthOverride(20.f).HeightOverride(20.f)
						[
							SNew(SCampusGlyph).Glyph(Glyph).Size(20.f).Color(Tint)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Value).Font(CampusAngular::Numeric(22)).ColorAndOpacity(FLinearColor::White)
					]
				]
			];
		if (OnPlus.IsBound())
		{
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(2.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox).WidthOverride(44.f).HeightOverride(46.f)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Slanted(12.f))
					.Fill(CampusAngular::Accent)
					.Border(FLinearColor::Transparent)
					.BorderWidth(0.f)
					.OnClicked(OnPlus)
					[
						SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Plus).Size(18.f).Color(FLinearColor(0.06f, 0.05f, 0.02f, 1.f))
					]
				]
			];
		}
		return Row;
	}

	TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, FMargin ContentPadding, float Cut, bool bStrong)
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Clipped(Cut))
			.Fill(bStrong ? CampusAngular::InkStrong : CampusAngular::Ink)
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.ContentPadding(ContentPadding)
			[
				Content
			];
	}

	TSharedRef<SWidget> Card(TSharedRef<SWidget> Content, bool bSelected, FMargin ContentPadding)
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(bSelected ? FLinearColor(0.09f, 0.08f, 0.04f, 0.94f) : FLinearColor(0.04f, 0.05f, 0.07f, 0.88f))
			.Border(bSelected ? CampusAngular::Accent : CampusAngular::Hairline)
			.BorderWidth(bSelected ? 2.f : 1.f)
			.AccentEdge(bSelected ? CampusAngular::Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bSelected ? 5.f : 0.f)
			.ContentPadding(ContentPadding)
			[
				Content
			];
	}

	TSharedRef<SWidget> Label(const FText& Text, int32 Size, FLinearColor Color, bool bBold)
	{
		return SNew(STextBlock).Text(Text).Font(Font(Size, bBold)).ColorAndOpacity(Color).AutoWrapText(false);
	}
}

// -------------------------------------------------------------------------------------------------
// Options (the gear). One panel, two entry points: the lobby top-right and the in-match HUD.

namespace CampusOptions
{
	void ApplyGraphics(const UCampusSave* Save)
	{
		if (!Save)
		{
			return;
		}
		// 0 smooth / 1 balanced / 2 HD -> engine scalability 1 / 2 / 3 (level 0 is unusable on a phone).
		const int32 Level = FMath::Clamp(Save->GraphicsQuality, 0, 2) + 1;
		Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
		Levels.SetFromSingleQualityLevel(Level);
		Scalability::SetQualityLevels(Levels);
	}

	namespace
	{
		/** One labelled row with its choices on the right. */
		TSharedRef<SWidget> OptionRow(const FText& Title, const FText& Hint, TSharedRef<SWidget> Choices)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 18.f, 0.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[CampusStyle::Label(Title, 21)]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(Hint).Font(CampusAngular::Body(14)).ColorAndOpacity(CampusAngular::TextMuted).AutoWrapText(true)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Choices];
		}

		TSharedRef<SWidget> OptionSwitch(const FText& Title, const FText& Hint, bool bOn, TFunction<void()> Toggle)
		{
			return OptionRow(Title, Hint, CampusStyle::Pill(bOn ? LOCTEXT("On", "ON") : LOCTEXT("Off", "OFF"), bOn,
				FOnClicked::CreateLambda([Toggle]() { Toggle(); return FReply::Handled(); }), FVector2D(110.f, 48.f)));
		}
	}

	TSharedRef<SWidget> Body(UCampusSave* Save, TFunction<void()> OnChanged)
	{
		if (!Save)
		{
			return CampusStyle::Label(LOCTEXT("NoSettings", "SETTINGS ARE NOT READY YET"), 20, CampusAngular::TextMuted);
		}

		TSharedRef<SHorizontalBox> Sensitivity = SNew(SHorizontalBox);
		for (float Choice : { 0.6f, 0.8f, 1.0f, 1.3f, 1.6f })
		{
			const bool bSelected = FMath::IsNearlyEqual(Save->LookSensitivity, Choice, 0.05f);
			Sensitivity->AddSlot().AutoWidth().Padding(3.f, 0.f)
			[
				CampusStyle::Pill(FText::FromString(FString::Printf(TEXT("%.1fx"), Choice)), bSelected,
					FOnClicked::CreateLambda([Save, Choice, OnChanged]()
					{
						Save->LookSensitivity = Choice;
						Save->Persist();
						OnChanged();
						return FReply::Handled();
					}), FVector2D(78.f, 48.f))
			];
		}

		static const TCHAR* QualityNames[] = { TEXT("SMOOTH"), TEXT("BALANCED"), TEXT("HD") };
		TSharedRef<SHorizontalBox> Quality = SNew(SHorizontalBox);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Quality->AddSlot().AutoWidth().Padding(3.f, 0.f)
			[
				CampusStyle::Pill(FText::FromString(QualityNames[Index]), Save->GraphicsQuality == Index,
					FOnClicked::CreateLambda([Save, Index, OnChanged]()
					{
						Save->GraphicsQuality = Index;
						Save->Persist();
						ApplyGraphics(Save);
						OnChanged();
						return FReply::Handled();
					}), FVector2D(132.f, 48.f))
			];
		}

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
			[
				OptionRow(LOCTEXT("SetSens", "LOOK SENSITIVITY"), LOCTEXT("SetSensHint", "How far the view turns for one swipe."), Sensitivity)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
			[
				OptionRow(LOCTEXT("SetGfx", "GRAPHICS"), LOCTEXT("SetGfxHint", "Drop to SMOOTH if the frame rate dips."), Quality)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
			[
				OptionSwitch(LOCTEXT("SetMic", "TEAM MIC"), LOCTEXT("SetMicHint", "Start matches with your microphone open."), Save->bMicDefaultOn,
					[Save, OnChanged]() { Save->bMicDefaultOn = !Save->bMicDefaultOn; Save->Persist(); OnChanged(); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
			[
				OptionSwitch(LOCTEXT("SetSpk", "TEAM SPEAKER"), LOCTEXT("SetSpkHint", "Hear your teammates."), Save->bSpeakerDefaultOn,
					[Save, OnChanged]() { Save->bSpeakerDefaultOn = !Save->bSpeakerDefaultOn; Save->Persist(); OnChanged(); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
			[
				OptionSwitch(LOCTEXT("SetLeftFire", "LEFT FIRE BUTTON"), LOCTEXT("SetLeftFireHint", "A second, smaller fire button for your left thumb."), Save->bLeftFireButton,
					[Save, OnChanged]() { Save->bLeftFireButton = !Save->bLeftFireButton; Save->Persist(); OnChanged(); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
			[
				OptionSwitch(LOCTEXT("SetMap", "MINIMAP"), LOCTEXT("SetMapHint", "The campus map in the HUD's top-left corner."), Save->bShowMinimap,
					[Save, OnChanged]() { Save->bShowMinimap = !Save->bShowMinimap; Save->Persist(); OnChanged(); })
			];
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
	// A TAP IS NOT A DRAG. The finger travels a pixel or two on any tap on open screen, and this used
	// to turn that into a fling: a 2 px move over a 2 ms frame reads as 1000 units/second, which the
	// decay below then spends about 50 degrees of yaw on. A few taps and the line-up was side-on -
	// which is why the player "usually faces left". Nothing rotates until the finger has really moved.
	Travel += FMath::Abs(DeltaX);
	if (Travel < DragThreshold)
	{
		return;
	}
	OnDrag.ExecuteIfBound(DeltaX);
	if (DeltaTime > UE_KINDA_SMALL_NUMBER)
	{
		// Smoothed finger speed, used for the fling when the finger lifts. CLAMPED: a single stuttery
		// frame must not be allowed to define the whole spin.
		const float Measured = FMath::Clamp(DeltaX / DeltaTime, -MaxFlingSpeed, MaxFlingSpeed);
		Velocity = FMath::Lerp(Velocity, Measured, 0.5f);
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
	Travel = 0.f;
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
	Travel = 0.f;
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
	const bool bSettings = Controller.IsValid() && Controller->IsSettingsOpen();

	// Rebuild only when the phase, the room's players, or the gear state change; rebuilding every
	// frame makes it flicker (and allocates a screenful of widgets 60 times a second).
	const FString Signature = Phase == int32(ECampusPhase::Waiting) ? RoomSignature() : FString();
	if (Phase == ShownPhase && Signature == ShownSignature && bSettings == bShownSettings)
	{
		return;
	}
	ShownPhase = Phase;
	ShownSignature = Signature;
	bShownSettings = bSettings;

	// The gear wins over everything: it is the only way out of a match on a phone.
	if (bSettings)
	{
		Content->SetContent(BuildSettings());
		return;
	}
	switch (Phase)
	{
	case int32(ECampusPhase::Waiting): Content->SetContent(BuildWaiting()); break;
	case int32(ECampusPhase::Ended): Content->SetContent(BuildWin()); break;
	default: Content->SetContent(SNullWidget::NullWidget); break;
	}
}

TSharedRef<SWidget> SCampusMatchWidget::BuildSettings()
{
	auto Close = [this]()
	{
		if (Controller.IsValid()) { Controller->SetSettingsOpen(false); }
		return FReply::Handled();
	};
	UCampusSave* Save = Controller.IsValid() ? Controller->GetProfile() : nullptr;
	// The in-match panel applies the two voice switches immediately, so a player can mute mid-fight.
	TWeakObjectPtr<ACampusMatchPlayerController> Weak = Controller;
	TFunction<void()> OnChanged = [this, Weak]()
	{
		if (ACampusMatchPlayerController* PC = Weak.Get())
		{
			if (const UCampusSave* S = PC->GetProfile())
			{
				PC->SetMicOn(S->bMicDefaultOn);
				PC->SetSpeakerOn(S->bSpeakerDefaultOn);
			}
		}
		// Force a rebuild on the next Tick so the pills show their new state.
		bShownSettings = false;
	};

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			// Opaque enough to read over gameplay, and it swallows taps so nothing fires behind it.
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.72f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(900.f)
			[
				CampusStyle::Panel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[CampusStyle::Label(LOCTEXT("MatchSettings", "SETTINGS"), 32, Accent)]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							CampusStyle::IconButton(CampusAngular::EGlyph::Close, FOnClicked::CreateLambda(Close), 58.f)
						]
					]
					+ SVerticalBox::Slot().AutoHeight()[CampusOptions::Body(Save, OnChanged)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 20.f, 0.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Button(LOCTEXT("BackToMatch", "BACK TO MATCH"), FOnClicked::CreateLambda(Close), EButton::Go, 22, FVector2D(280.f, 64.f))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Button(LOCTEXT("QuitMatch", "LEAVE MATCH"), FOnClicked::CreateLambda([this]()
							{
								if (Controller.IsValid())
								{
									Controller->SetSettingsOpen(false);
									Controller->LeaveToLobby();
								}
								return FReply::Handled();
							}), EButton::Ghost, 20, FVector2D(230.f, 64.f))
						]
					],
					FMargin(36.f, 26.f), 22.f, true)
			]
		];
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
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(18.f))
			.Fill(FLinearColor(0.03f, 0.04f, 0.055f, 0.9f))
			.Border(bRed ? FLinearColor(FColor(0xE5, 0x3E, 0x3E)) : FLinearColor(FColor(0x3E, 0x8E, 0xE5)))
			.BorderWidth(2.f)
			.AccentEdge(bRed ? FLinearColor(FColor(0xE5, 0x3E, 0x3E)) : FLinearColor(FColor(0x3E, 0x8E, 0xE5)))
			.AccentEdgeWidth(5.f)
			.ContentPadding(FMargin(24.f, 16.f))
			[
				Names
			]
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
		: Label(FText::Format(LOCTEXT("WaitingHost", "WAITING FOR THE HOST  ·  {0} PLAYERS"), FText::AsNumber(Count)), 20, Muted);

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.45f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			CampusStyle::Panel(
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
				],
				FMargin(40.f, 28.f), 22.f, true)
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
			CampusStyle::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Label(Headline, 56, bWon ? Go : Accent)
				]
				// A slanted accent rule under the headline instead of a soft divider.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 10.f)
				[
					SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(14.f)).Fill(bWon ? Go : Accent)
					[
						SNew(SBox).WidthOverride(260.f).HeightOverride(5.f)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Label(FText::Format(LOCTEXT("Score", "RED {0}  -  {1} BLUE"), FText::AsNumber(State ? State->RedScore : 0), FText::AsNumber(State ? State->BlueScore : 0)), 24)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 24.f)
				[
					Label(FText::Format(LOCTEXT("YourStats", "You: {0} kills  ·  {1} deaths"), FText::AsNumber(Mine ? Mine->Kills : 0), FText::AsNumber(Mine ? Mine->Deaths : 0)), 20, Muted, false)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					StartButton(FText::Format(LOCTEXT("Claim", "CLAIM {0} COINS"), FText::AsNumber(Reward)), FOnClicked::CreateLambda([this]()
					{
						if (Controller.IsValid()) { Controller->ClaimAndLeave(); }
						return FReply::Handled();
					}), FVector2D(420.f, 92.f))
				],
				FMargin(60.f, 36.f), 26.f, true)
		];
}

#undef LOCTEXT_NAMESPACE
