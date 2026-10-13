// The invite / friend-request card stack: the one piece of UI that must beat every other screen.
//
// It sits in the lobby's root SOverlay ABOVE the busy scrim and below the toast, so an invite that
// arrives while the Store is open - or while a claim is in flight - is still answerable. Slate
// hit-tests top down, so the card's two buttons win while the scrim still blocks everything beneath.
//
// MOTION IS RENDER TRANSFORM, NEVER LAYOUT. A layout change would re-flow the whole overlay every
// frame of the slide and show up at once on a budget phone. Slide in 250 ms CubicOut from +484 px,
// out 200 ms CubicIn, reflow 150 ms CubicInOut. Nothing allocates per frame: the countdown bar is a
// width lambda over two cached floats, and every string is built once at Construct.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusAvatar.h"
#include "Campus/CampusSocial.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusNoticeUI"

namespace
{
	/** The card starts just off the right edge: its own width plus the gap, so no sliver shows. */
	constexpr float SlideFrom = SCampusNoticeStack::CardWidth + 24.f;
	constexpr float EnterSeconds = 0.25f;
	constexpr float FadeSeconds = 0.12f;
	constexpr float ExitSeconds = 0.20f;
	constexpr float ReflowSeconds = 0.15f;
	/** The bar turns red under this many seconds: colour is never the only signal, the bar is draining too. */
	constexpr float DangerUnder = 5.f;
	/** How much of the name row is kept clear for the floating mode chip, so a long name clips
	 *  instead of running underneath it. The BUTTON row deliberately does not pay this. */
	constexpr float ChipGutter = 84.f;
}

// ---------------------------------------------------------------------------------------------
// One card

const FCampusNotice* SCampusNoticeCard::Notice() const
{
	UCampusSocial* Live = Social.Get();
	return Live ? Live->FindNotice(Key) : nullptr;
}

void SCampusNoticeCard::Construct(const FArguments& InArgs)
{
	Social = InArgs._Social;
	Key = InArgs._Key;
	OnAccept = InArgs._OnAccept;
	OnReject = InArgs._OnReject;

	Slide = Enter.AddCurve(0.f, EnterSeconds, ECurveEaseFunction::CubicOut);
	Fade = Enter.AddCurve(0.f, FadeSeconds, ECurveEaseFunction::Linear);
	ExitSlide = Exit.AddCurve(0.f, ExitSeconds, ECurveEaseFunction::CubicIn);
	Rise = Reflow.AddCurve(0.f, ReflowSeconds, ECurveEaseFunction::CubicInOut);

	const FCampusNotice* Card = Notice();
	const bool bInvite = !Card || Card->Kind == ECampusNoticeKind::Invite;
	const FString Name = Card ? Card->Sender.Name.ToUpper() : FString();
	const int32 Mode = Card ? Card->Mode : 4;
	const int32 Members = Card ? Card->MemberCount : 1;
	Total = Card ? FMath::Max(1.f, Card->TotalSeconds) : 45.f;
	Remaining = Total;

	// Every string is built ONCE, here. The per-frame work on this widget is two floats.
	const FText Line2 = bInvite
		? (Mode == 2 ? LOCTEXT("WantsDuo", "WANTS YOU IN THEIR DUO") : LOCTEXT("WantsSquad", "WANTS YOU IN THEIR SQUAD"))
		: LOCTEXT("SentRequest", "SENT YOU A FRIEND REQUEST");
	const FText Chip = bInvite
		? (Mode == 2 ? LOCTEXT("ChipDuo", "DUO") : LOCTEXT("ChipSquad", "SQUAD"))
		: LOCTEXT("ChipFriend", "FRIEND");
	// The occupancy rides on the SUBTITLE, not in the corner. Stacked under the mode chip it crowded
	// the REJECT button's top edge, and it belongs with the sentence it qualifies anyway:
	// "WANTS YOU IN THEIR SQUAD - 2/4".
	const FText Line2Full = bInvite
		? FText::Format(LOCTEXT("WantsWithCount", "{0}  ·  {1}/{2}"), Line2,
			FText::AsNumber(Members), FText::AsNumber(Mode))
		: Line2;

	FCampusPlayerCard Sender;
	if (Card)
	{
		Sender = Card->Sender;
	}

	auto Answer = [this](bool bYes)
	{
		return FOnClicked::CreateLambda([this, bYes]()
		{
			UCampusSocial* Live = Social.Get();
			FCampusNotice* Mine = Live ? Live->FindNotice(Key) : nullptr;
			// A tap in flight disables both buttons, so nothing is answered twice.
			if (!Mine || Mine->bBusy || !Mine->Exit.IsEmpty())
			{
				return FReply::Handled();
			}
			Mine->bBusy = true;
			const FString Which = Key;
			if (bYes)
			{
				OnAccept.ExecuteIfBound(Which);
			}
			else
			{
				OnReject.ExecuteIfBound(Which);
			}
			return FReply::Handled();
		});
	};

	// 150 x 52 each with a 16 px gap: a fat thumb cannot hit both, and both clear the 48 px floor.
	TSharedRef<SWidget> Buttons = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(150.f).HeightOverride(52.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Wedge(14.f, 10.f))
				.Fill(CampusAngular::Go)
				.Border(FLinearColor(1.f, 1.f, 1.f, 0.28f))
				.BorderWidth(1.f)
				.ContentPadding(FMargin(10.f, 4.f))
				.OnClicked(Answer(true))
				[
					SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("Accept", "ACCEPT")).Font(CampusAngular::Display(18))
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(16.f, 0.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(150.f).HeightOverride(52.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Wedge(14.f, 10.f))
				.Fill(FLinearColor(1.f, 1.f, 1.f, 0.10f))
				.Border(CampusAngular::Hairline)
				.BorderWidth(1.f)
				.ContentPadding(FMargin(10.f, 4.f))
				.OnClicked(Answer(false))
				[
					SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("Reject", "REJECT")).Font(CampusAngular::Display(18))
						.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.82f))
					]
				]
			]
		];

	ChildSlot
	[
		SNew(SBox).WidthOverride(SCampusNoticeStack::CardWidth).HeightOverride(SCampusNoticeStack::CardHeight)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(CampusAngular::InkStrong)
			.Border(CampusAngular::Accent)
			.BorderWidth(2.f)
			.AccentEdge(CampusAngular::Accent)
			.AccentEdgeWidth(5.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().Padding(FMargin(18.f, 12.f, 16.f, 16.f))
				[
					SNew(SHorizontalBox)
					// The sender's PICTURE, with their colour + initial as the fallback behind it (the
					// API returns `color` on every card shape for exactly that reason).
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 2.f, 14.f, 0.f)
					[
						SNew(SBox).WidthOverride(56.f).HeightOverride(56.f)
						[
							SNew(SCampusAvatar)
							.Url(Sender.AvatarUrl)
							.Size(56.f)
							.Shape(FCampusShape::Clipped(12.f))
							.Fill(Sender.Color)
							.Border(FLinearColor(1.f, 1.f, 1.f, 0.35f))
							.BorderWidth(1.f)
							[
								SNew(STextBlock).Text(FText::FromString(Sender.Initial()))
								.Font(CampusAngular::Display(26)).ColorAndOpacity(FLinearColor::White)
							]
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SVerticalBox)
						// The right padding on these two rows ONLY: the mode chip floats over this
						// corner, and the two 150-wide buttons below need the full column to fit.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, ChipGutter, 0.f)
						[
							SNew(STextBlock).Text(FText::FromString(Name)).Font(CampusAngular::Display(22))
							.ColorAndOpacity(FLinearColor::White).Clipping(EWidgetClipping::ClipToBounds)
						]
						// NO gutter on this line: the chip sits in the name's band (y 12-36) and this
						// line starts below it, so the longer "...SQUAD · 2/4" gets the full width.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f, 8.f, 0.f)
						[
							SNew(STextBlock).Text(Line2Full).Font(CampusAngular::Display(13))
							.ColorAndOpacity(CampusAngular::TextMuted).Clipping(EWidgetClipping::ClipToBounds)
						]
						+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 4.f))]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SOverlay)
							// The two buttons, and - once the server has closed this invite - the one
							// word that says why, visible for the 200 ms exit.
							+ SOverlay::Slot()
							[
								SNew(SBox)
								.Visibility_Lambda([this]()
								{
									const FCampusNotice* Live = Notice();
									return Live && !Live->Exit.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
								})
								.IsEnabled_Lambda([this]()
								{
									const FCampusNotice* Live = Notice();
									return Live && !Live->bBusy;
								})
								[
									Buttons
								]
							]
							+ SOverlay::Slot().VAlign(VAlign_Center)
							[
								SNew(SBox).HeightOverride(52.f).VAlign(VAlign_Center)
								.Visibility_Lambda([this]()
								{
									const FCampusNotice* Live = Notice();
									return Live && !Live->Exit.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
								})
								[
									SNew(STextBlock)
									.Text_Lambda([this]()
									{
										const FCampusNotice* Live = Notice();
										return Live ? FText::FromString(Live->Exit) : FText::GetEmpty();
									})
									.Font(CampusAngular::Display(20))
									.ColorAndOpacity(CampusAngular::TextMuted)
								]
							]
						]
					]
				]
				// The mode chip FLOATS over the top-right corner instead of taking a column of its own.
				// With a column it stole ~70 px from the middle, and 150 + 16 + 150 no longer fitted -
				// the two buttons would have been squeezed below the 48 px touch floor. Hit-test
				// invisible, so it can never swallow a tap meant for ACCEPT.
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f, 12.f, 16.f, 0.f))
				[
					SNew(SCampusAngularPanel)
					.Visibility(EVisibility::HitTestInvisible)
					.Shape(FCampusShape::Slanted(10.f))
					.Fill(FLinearColor(0.12f, 0.10f, 0.02f, 0.92f))
					.ContentPadding(FMargin(11.f, 3.f))
					[
						SNew(STextBlock).Text(Chip).Font(CampusAngular::Display(13))
						.ColorAndOpacity(CampusAngular::Accent)
					]
				]
				// The countdown bar: 6 px along the bottom edge, width from two cached floats, so this
				// costs one FOptionalSize per frame and nothing else.
				+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Left)
				[
					SNew(SBox).HeightOverride(6.f).WidthOverride(SCampusNoticeStack::CardWidth)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SCampusAngularPanel).Fill(FLinearColor(1.f, 1.f, 1.f, 0.12f))]
						+ SOverlay::Slot().HAlign(HAlign_Left)
						[
							SNew(SBox)
							.WidthOverride_Lambda([this]()
							{
								const float Fraction = Total > 0.f ? FMath::Clamp(Remaining / Total, 0.f, 1.f) : 0.f;
								return FOptionalSize(SCampusNoticeStack::CardWidth * Fraction);
							})
							[
								SNew(SCampusAngularPanel)
								.Fill_Lambda([this]()
								{
									return Remaining <= DangerUnder ? CampusAngular::Danger : CampusAngular::Accent;
								})
							]
						]
					]
				]
			]
		]
	];

	Enter.Play(AsShared());
	SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(SlideFrom, 0.f))));
	SetRenderOpacity(0.6f);
}

void SCampusNoticeCard::BeginExit()
{
	if (bExiting)
	{
		return;
	}
	bExiting = true;
	Exit.Play(AsShared());
}

bool SCampusNoticeCard::IsGone() const
{
	return bExiting && !Exit.IsPlaying();
}

void SCampusNoticeCard::PlayReflow(float FromOffsetY)
{
	if (FMath::IsNearlyZero(FromOffsetY))
	{
		return;
	}
	ReflowFrom = FromOffsetY;
	Reflow.Play(AsShared());
}

void SCampusNoticeCard::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// The card's own clock, off the SERVER time, so a phone whose clock is a day out still drains the
	// bar correctly - and an ignored invite sends NOTHING, which is what makes it free on a metered tunnel.
	if (const UCampusSocial* Live = Social.Get())
	{
		if (const FCampusNotice* Card = Notice())
		{
			Remaining = FMath::Max(0.f, Live->SecondsUntilPrecise(Card->ExpiresAt));
			if (!Card->Exit.IsEmpty())
			{
				BeginExit();
			}
		}
	}

	float OffsetX = 0.f;
	float Opacity = 1.f;
	if (bExiting)
	{
		const float Alpha = ExitSlide.GetLerp();
		OffsetX = SlideFrom * Alpha;
		// Fade over the LAST 120 ms of the 200 ms slide, so the card is still legible as it leaves.
		Opacity = 1.f - FMath::Clamp((Alpha - 0.4f) / 0.6f, 0.f, 1.f);
	}
	else if (Enter.IsPlaying())
	{
		OffsetX = SlideFrom * (1.f - Slide.GetLerp());
		Opacity = FMath::Lerp(0.6f, 1.f, Fade.GetLerp());
	}

	const float OffsetY = Reflow.IsPlaying() ? ReflowFrom * (1.f - Rise.GetLerp()) : 0.f;
	SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2f(OffsetX, OffsetY))));
	SetRenderOpacity(Opacity);
}

// ---------------------------------------------------------------------------------------------
// The stack

void SCampusNoticeStack::Construct(const FArguments& InArgs)
{
	Social = InArgs._Social;
	OnAccept = InArgs._OnAccept;
	OnReject = InArgs._OnReject;
	OnOpenTray = InArgs._OnOpenTray;

	// SelfHitTestInvisible: the column itself never eats a tap, only the cards' buttons do.
	ChildSlot
	[
		SAssignNew(Column, SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
	];

	if (UCampusSocial* Live = Social.Get())
	{
		ChangedHandle = Live->OnNoticesChanged.AddLambda([this]() { bDirty = true; });
	}
	Reconcile();
}

SCampusNoticeStack::~SCampusNoticeStack()
{
	if (ChangedHandle.IsValid())
	{
		if (UCampusSocial* Live = Social.Get())
		{
			Live->OnNoticesChanged.Remove(ChangedHandle);
		}
		ChangedHandle.Reset();
	}
}

void SCampusNoticeStack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// A card that has finished its exit is dropped from the subsystem, which dirties the stack. This is
	// the only per-frame work here: a walk over at most three pointers.
	for (const TSharedPtr<SCampusNoticeCard>& Card : Cards)
	{
		if (Card.IsValid() && Card->IsGone())
		{
			if (UCampusSocial* Live = Social.Get())
			{
				Live->DropNotice(Card->GetKey());
			}
			bDirty = true;
		}
	}
	if (bDirty)
	{
		bDirty = false;
		Reconcile();
	}
}

void SCampusNoticeStack::Reconcile()
{
	UCampusSocial* Live = Social.Get();
	if (!Column.IsValid())
	{
		return;
	}
	// Cards are drawn on the lobby only. Elsewhere they stay queued in the subsystem and are
	// re-evaluated on lobby entry - exactly as the streak popup already gates on the lobby screen.
	const bool bShow = Live && Live->AreNoticesVisible();
	const TArray<FCampusNotice> Empty;
	const TArray<FCampusNotice>& Notices = bShow ? Live->GetNotices() : Empty;

	TArray<FString> Wanted;
	Wanted.Reserve(MaxVisible);
	for (const FCampusNotice& Notice : Notices)
	{
		if (Wanted.Num() >= MaxVisible)
		{
			break;
		}
		Wanted.Add(Notice.Key);
	}
	const int32 NextOverflow = FMath::Max(0, Notices.Num() - Wanted.Num());

	// Keep the existing widgets for keys that are still wanted (so their animations survive), start an
	// exit on the ones that are not, and only rebuild the column when the visible set really changed.
	bool bChanged = NextOverflow != Overflow || Wanted.Num() != Cards.Num();
	for (int32 Index = 0; Index < Cards.Num() && !bChanged; ++Index)
	{
		bChanged = !Cards[Index].IsValid() || Cards[Index]->GetKey() != Wanted[Index];
	}
	if (!bChanged)
	{
		return;
	}

	TArray<TSharedPtr<SCampusNoticeCard>> Next;
	Next.Reserve(Wanted.Num());
	for (int32 Index = 0; Index < Wanted.Num(); ++Index)
	{
		TSharedPtr<SCampusNoticeCard> Found;
		int32 WasAt = INDEX_NONE;
		for (int32 Old = 0; Old < Cards.Num(); ++Old)
		{
			if (Cards[Old].IsValid() && Cards[Old]->GetKey() == Wanted[Index])
			{
				Found = Cards[Old];
				WasAt = Old;
				break;
			}
		}
		if (!Found.IsValid())
		{
			Found = SNew(SCampusNoticeCard)
				.Social(Social)
				.Key(Wanted[Index])
				.OnAccept(OnAccept)
				.OnReject(OnReject);
		}
		else if (WasAt != Index)
		{
			// It moved up (or down) a slot: animate from where it used to be rather than snapping.
			Found->PlayReflow(float(WasAt - Index) * (CardHeight + CardGap));
		}
		Next.Add(Found);
	}
	Cards = MoveTemp(Next);
	Overflow = NextOverflow;

	Column->ClearChildren();
	for (const TSharedPtr<SCampusNoticeCard>& Card : Cards)
	{
		Column->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, CardGap)
		[
			Card.ToSharedRef()
		];
	}
	if (Overflow > 0)
	{
		// A fourth invite does not shout; it collapses into one chip that opens the whole tray.
		Column->AddSlot().AutoHeight().HAlign(HAlign_Right)
		[
			SNew(SBox).WidthOverride(CardWidth).HeightOverride(48.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Slanted(14.f))
				.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.92f))
				.Border(CampusAngular::Accent)
				.BorderWidth(1.f)
				.ContentPadding(FMargin(16.f, 2.f))
				.OnClicked(FOnClicked::CreateLambda([this]()
				{
					OnOpenTray.ExecuteIfBound();
					return FReply::Handled();
				}))
				[
					SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("MoreInvites", "+{0} MORE"), FText::AsNumber(Overflow)))
						.Font(CampusAngular::Display(16))
						.ColorAndOpacity(CampusAngular::Accent)
					]
				]
			]
		];
	}
}

#undef LOCTEXT_NAMESPACE
