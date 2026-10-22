// LIVE EVENTS, as the player sees them (PHASE_B_CONTRACT B2). Two surfaces, one source - the
// published `events` blob plus one progress read:
//
//   THE BANNER   top-centre of the lobby, under the weekend claim banner: the console's uploaded
//                picture, the event's name, the time left on the server clock and the multipliers it
//                is running. Zero footprint when nothing is on, because an empty frame in the middle
//                of the lobby is worse than no frame at all. Tapping it opens the sheet.
//   THE SHEET    every reward with the SERVER's progress against it and a CLAIM button that is only
//                live when the server says the claim would be accepted.
//
// THE CLIENT DECIDES "RUNNING NOW", THE SERVER DECIDES EVERYTHING ELSE. The blob is cached by the
// phone, so it carries each event's window and the client filters with the clock it already has
// (UCampusBackend::VisibleEvents). Progress and eligibility are the server's answer, computed with the
// same function the claim uses - so this screen never works out whether a reward is earned, which is
// the one thing a client must never be trusted with when money is at the end of it.
//
// ONE READ PER VISIT, NOT PER REPAINT. The progress GET is started by Show() through OpenEventSheet
// and latched (bEventProgressAsked), for the reason bStorePricesQueried exists: a Build function that
// starts a request its own reply repaints is a request loop on a tunnel metered at 20,000 a month.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusFestiveArt.h"
#include "Campus/CampusLobbyUIInternal.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusPlayerControllers.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusEventsUI"

using namespace CampusStyle;

namespace
{
	/** The reward progress bar's drawn width inside the sheet. */
	constexpr float EventBarWidth = 420.f;

	/** "x2 XP" / "x1.5 POINTS": the multiplier chip, which is the whole reason a player cares that an
	 *  event is on. One decimal, because 1.5 is a real value the console can publish. */
	FString MultiplierText(float Value, const TCHAR* What)
	{
		const FString Number = FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value))
			? FString::Printf(TEXT("%d"), FMath::RoundToInt(Value))
			: FString::Printf(TEXT("%.1f"), Value);
		return FString::Printf(TEXT("x%s %s"), *Number, What);
	}

	/** The small dark chip the banner and the sheet both wear. */
	TSharedRef<SWidget> EventChip(CampusAngular::EGlyph Glyph, const FText& Label, const FLinearColor& Color)
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(8.f))
			.Fill(FLinearColor(0.f, 0.f, 0.f, 0.62f))
			.Border(Color.CopyWithNewOpacity(0.55f))
			.BorderWidth(1.f)
			.ContentPadding(FMargin(11.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 7.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(Glyph).Size(15.f).Color(Color)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Label).Font(CampusAngular::Display(14)).ColorAndOpacity(Color)
				]
			];
	}

	/** "SUN 8 NOV": the festival day the way a poster prints it. Empty when the console set none. */
	FText FestivalDayText(const FCampusEvent& Event)
	{
		if (Event.FestivalDay == FDateTime())
		{
			return FText::GetEmpty();
		}
		static const TCHAR* Days[] = { TEXT("MON"), TEXT("TUE"), TEXT("WED"), TEXT("THU"), TEXT("FRI"), TEXT("SAT"), TEXT("SUN") };
		static const TCHAR* Months[] = { TEXT("JAN"), TEXT("FEB"), TEXT("MAR"), TEXT("APR"), TEXT("MAY"), TEXT("JUN"),
			TEXT("JUL"), TEXT("AUG"), TEXT("SEP"), TEXT("OCT"), TEXT("NOV"), TEXT("DEC") };
		const int32 Day = int32(Event.FestivalDay.GetDayOfWeek());
		const int32 Month = FMath::Clamp(Event.FestivalDay.GetMonth(), 1, 12) - 1;
		return FText::FromString(FString::Printf(TEXT("%s %d %s"), Days[FMath::Clamp(Day, 0, 6)], Event.FestivalDay.GetDay(), Months[Month]));
	}

	/** This event is drawn by the game (a theme and no uploaded banner): SCampusFestiveArt. */
	bool IsFestive(const FCampusEvent& Event)
	{
		return Event.BannerKey.IsEmpty() && !Event.Theme.IsEmpty();
	}

	/** The accent an event's frame wears: the festival's own colour, else the game's. */
	FLinearColor EventAccent(const FCampusEvent& Event)
	{
		return Event.Theme.IsEmpty() ? CampusAngular::Accent : SCampusFestiveArt::AccentFor(Event.Theme);
	}

	/** A festival plate: the drawn art underneath, a light scrim, the frame and the click on top. */
	TSharedRef<SWidget> FestivePlate(const FCampusEvent& Event, float Intensity, bool bStrong, FMargin Padding,
		FOnClicked OnClicked, TSharedRef<SWidget> Content)
	{
		const FLinearColor Frame = EventAccent(Event);
		return SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox).Clipping(EWidgetClipping::ClipToBounds)
				[
					SNew(SCampusFestiveArt).Theme(Event.Theme).Intensity(Intensity)
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Notched(16.f))
				.Fill(FLinearColor(0.f, 0.f, 0.f, 0.18f))
				.Border(bStrong ? Frame : Frame.CopyWithNewOpacity(0.55f))
				.BorderWidth(bStrong ? 2.f : 1.f)
				.AccentEdge(Frame)
				.AccentEdgeWidth(bStrong ? 5.f : 3.f)
				.ContentPadding(Padding)
				.OnClicked(OnClicked)
				[
					Content
				]
			];
	}

	/** Every multiplier the event is running, as chips. Empty when it runs none. */
	TSharedRef<SWidget> EventMultipliers(const FCampusEvent& Event)
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		if (Event.XpMultiplier != 1.f)
		{
			Row->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				EventChip(CampusAngular::EGlyph::Rank,
					FText::FromString(MultiplierText(Event.XpMultiplier, TEXT("XP"))), CampusAngular::Accent)
			];
		}
		if (Event.PointsMultiplier != 1.f)
		{
			Row->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				EventChip(CampusAngular::EGlyph::Trophy,
					FText::FromString(MultiplierText(Event.PointsMultiplier, TEXT("POINTS"))), CampusAngular::Gem)
			];
		}
		return Row;
	}
}

// -------------------------------------------------------------------------------------------------
// The sheet

void SCampusLobbyWidget::OpenEventSheet()
{
	const UCampusBackend* Api = Backend();
	const TArray<FCampusEvent> Shown = Api ? Api->VisibleEvents() : TArray<FCampusEvent>();
	// The banner names the event it was tapped on; the menu tab names nothing, so the first visible
	// one (live first, by VisibleEvents' own order) is what opens.
	const bool bKnown = EventShownId > 0
		&& Shown.ContainsByPredicate([this](const FCampusEvent& Row) { return Row.Id == EventShownId; });
	if (!bKnown)
	{
		EventShownId = Shown.Num() > 0 ? Shown[0].Id : 0;
	}
	EventProgress.Reset();
	bEventProgressAsked = false;
	EventClaimIndex = INDEX_NONE;
	RefreshEventProgress();
}

void SCampusLobbyWidget::RefreshEventProgress()
{
	UCampusBackend* Api = Backend();
	if (bEventProgressAsked || EventShownId <= 0 || !Api || !Api->IsSignedIn())
	{
		return;
	}
	bEventProgressAsked = true;
	bEventProgressLoading = true;
	const int32 Asked = EventShownId;
	// WEAK, AND NOT OPTIONAL. UCampusBackend is a GameInstanceSubsystem: it outlives this widget and
	// holds this callback until the request finishes, so a raw `this` is a use-after-free the moment
	// the player taps PLAY while a progress read is in the air.
	TWeakPtr<SWidget> Weak = AsWeak();
	Api->LoadEventProgress(Asked, [this, Weak, Asked](bool bOk, const FString& Error, const FCampusEventProgress& Loaded)
	{
		if (!Weak.IsValid())
		{
			return;
		}
		bEventProgressLoading = false;
		if (Asked != EventShownId)
		{
			return;   // the player moved to another event while this was in the air
		}
		if (bOk)
		{
			EventProgress = MakeShared<FCampusEventProgress>(Loaded);
		}
		else if (!Error.IsEmpty())
		{
			Toast(Error);
		}
		if (Current == EScreen::Event)
		{
			Refresh();
		}
	});
}

void SCampusLobbyWidget::ClaimEventReward(int32 RewardIndex)
{
	UCampusBackend* Api = Backend();
	if (!Api || EventShownId <= 0 || RewardIndex < 0 || EventClaimIndex != INDEX_NONE)
	{
		return;
	}
	EventClaimIndex = RewardIndex;
	Busy(TEXT("Claiming..."));
	const int32 Asked = EventShownId;
	TWeakPtr<SWidget> Weak = AsWeak();
	Api->ClaimEventReward(Asked, RewardIndex, [this, Weak](bool bOk, const FString& Error)
	{
		if (!Weak.IsValid())
		{
			return;
		}
		EventClaimIndex = INDEX_NONE;
		bBusy = false;
		if (!bOk)
		{
			Toast(Error);
			Refresh();
			return;
		}
		Toast(TEXT("EVENT REWARD CLAIMED!"));
		// THE SERVER'S WORD ON WHAT IS LEFT. The wallet already came back on the claim response (and
		// the top bar reads it from the subsystem); the rows have to be re-read, because `claimed` and
		// `claimable` are the server's answer and not something this screen may edit.
		bEventProgressAsked = false;
		RefreshEventProgress();
		// The top bar's wallet counters moved, so its region has to be re-filled.
		TopBarStamp = 0;
		Refresh();
	});
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildEventRewardRow(const FCampusEventRewardProgress& Reward)
{
	const int32 Index = Reward.Index;
	// Named for the ROW, not the screen: SCampusLobbyWidget::bClaiming is the engagement screens'
	// own latch, and a local that hides it is a warning this project treats as an error.
	const bool bRowClaiming = EventClaimIndex == Index;
	const FString Gives = Reward.Describe();
	// What has to be true to get it, in words. `condition.mode` is a MAP wire name on the server side
	// (routes/adminevents.js says so in as many words), so this reads it as the map it is.
	FText Needs;
	if (Reward.Target <= 0)
	{
		Needs = LOCTEXT("EventRewardFree", "JUST FOR TAKING PART");
	}
	else if (Reward.Mode.IsEmpty())
	{
		Needs = FText::Format(LOCTEXT("EventRewardMatches", "{0} RATED MATCHES"), FText::AsNumber(Reward.Target));
	}
	else
	{
		Needs = FText::Format(LOCTEXT("EventRewardOnMap", "{0} RATED MATCHES ON {1}"),
			FText::AsNumber(Reward.Target), FText::FromString(Reward.Mode.ToUpper()));
	}

	TSharedRef<SWidget> Action = SNullWidget::NullWidget;
	if (Reward.bClaimed)
	{
		Action = Label(LOCTEXT("EventClaimed", "CLAIMED"), 16, CampusAngular::Go);
	}
	else if (Reward.bClaimable)
	{
		Action = Button(bRowClaiming ? LOCTEXT("EventClaiming", "...") : LOCTEXT("EventClaim", "CLAIM"),
			FOnClicked::CreateLambda([this, Index]()
			{
				ClaimEventReward(Index);
				return FReply::Handled();
			}), EButton::Go, 17, FVector2D(150.f, 52.f), EventClaimIndex == INDEX_NONE);
	}
	else
	{
		// The honest count, not a dead button: the server decides claimable, so a row that is not
		// there yet says how far there is to go.
		Action = Label(FText::FromString(FString::Printf(TEXT("%d / %d"),
			FMath::Min(Reward.Progress, Reward.Target), Reward.Target)), 18, FLinearColor::White);
	}

	return CampusStyle::Card(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Gives.IsEmpty() ? TEXT("REWARD") : Gives))
				.Font(CampusAngular::Display(20))
				.ColorAndOpacity(Reward.bClaimed ? Muted : CardGold)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(Needs).Font(CampusAngular::Body(13)).ColorAndOpacity(Muted)
			]
			// The same flat leaning bar the mission cards use, so progress means one thing in this game.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SBox).HeightOverride(8.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SCampusAngularPanel).Fill(FLinearColor(1.f, 1.f, 1.f, 0.12f))]
					+ SOverlay::Slot().HAlign(HAlign_Left)
					[
						SNew(SBox).WidthOverride(FMath::Max(2.f, EventBarWidth * Reward.Fraction()))
						[
							SNew(SCampusAngularPanel).Fill(Reward.bClaimable ? CampusAngular::Go : Accent)
						]
					]
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Action],
		Reward.bClaimable, FMargin(22.f, 14.f, 18.f, 14.f));
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildEvent()
{
	// FREE FIRE'S EVENTS SCREEN: the events down the left (LIVE / SOON), the selected one as a tall poster
	// in the middle (its art, the name, the dates, Event Details and Event Time), and its rewards on the
	// right, each with what it takes and where the player stands.
	const UCampusBackend* Api = Backend();
	const TArray<FCampusEvent> Shown = Api ? Api->VisibleEvents() : TArray<FCampusEvent>();
	const FCampusEvent* Event = Shown.FindByPredicate([this](const FCampusEvent& Row) { return Row.Id == EventShownId; });

	if (!Event)
	{
		// The empty state says what to expect rather than apologising, and it is the ONLY thing this
		// screen shows when the console is running nothing - no half-filled card, no spinner.
		TSharedRef<SWidget> Empty = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				Label(LOCTEXT("NoEvent", "NO EVENT RUNNING"), 26, FLinearColor::White)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoEventHint", "Events bring extra XP, extra weekend points and rewards you claim here. The next one shows up on this screen and on the lobby banner."))
				.Font(CampusAngular::Body(17)).ColorAndOpacity(Muted).AutoWrapText(true)
			];
		return Dialog(LOCTEXT("EventsTitle", "EVENTS"), Empty, 780.f, true, EScreen::Lobby);
	}

	const FDateTime Now = Api ? Api->Engagement.Now() : FDateTime::UtcNow();
	const bool bLive = Event->IsLiveAt(Now);
	const FLinearColor Theme = EventAccent(*Event);

	// ---- LEFT: every live and upcoming event, one tab each
	TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox);
	for (const FCampusEvent& Row : Shown)
	{
		const int32 RowId = Row.Id;
		const bool bOn = RowId == EventShownId;
		const bool bRowLive = Row.IsLiveAt(Now);
		const FLinearColor RowColor = EventAccent(Row);
		Rail->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(SBox).WidthOverride(250.f).MinDesiredHeight(70.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Tab(12.f, 12.f))
				.Fill(bOn ? FLinearColor(RowColor.R * 0.30f, RowColor.G * 0.18f, RowColor.B * 0.10f, 0.96f) : FLinearColor(0.02f, 0.03f, 0.04f, 0.88f))
				.Border(bOn ? RowColor : CampusAngular::Hairline)
				.BorderWidth(bOn ? 2.f : 1.f)
				.AccentEdge(bOn ? RowColor : FLinearColor::Transparent)
				.AccentEdgeWidth(bOn ? 5.f : 0.f)
				.ContentPadding(FMargin(16.f, 8.f, 10.f, 8.f))
				.OnClicked(FOnClicked::CreateLambda([this, RowId]()
				{
					if (EventShownId != RowId)
					{
						EventShownId = RowId;
						EventProgress.Reset();
						bEventProgressAsked = false;
						RefreshEventProgress();
						Refresh();
					}
					return FReply::Handled();
				}))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Row.Label.ToUpper()))
						.Font(CampusAngular::Display(18))
						.ColorAndOpacity(bOn ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.82f))
						.AutoWrapText(true)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(SCampusAngularPanel)
						.Shape(FCampusShape::Slanted(6.f))
						.Fill(bRowLive ? RowColor : FLinearColor(0.f, 0.f, 0.f, 0.6f))
						.Border(RowColor)
						.BorderWidth(1.f)
						.ContentPadding(FMargin(7.f, 1.f))
						[
							SNew(STextBlock)
							.Text(bRowLive ? LOCTEXT("TabLive", "LIVE") : LOCTEXT("TabSoon", "SOON"))
							.Font(CampusAngular::Display(11))
							.ColorAndOpacity(bRowLive ? FLinearColor::Black : RowColor)
						]
					]
				]
			]
		];
	}

	// ---- CENTRE: the poster
	static const TCHAR* Months[] = { TEXT("JAN"), TEXT("FEB"), TEXT("MAR"), TEXT("APR"), TEXT("MAY"), TEXT("JUN"),
		TEXT("JUL"), TEXT("AUG"), TEXT("SEP"), TEXT("OCT"), TEXT("NOV"), TEXT("DEC") };
	// The window in the phone's own time zone, ending on the last minute it is open.
	const FTimespan Zone = FDateTime::Now() - FDateTime::UtcNow();
	const FDateTime LocalStart = Event->StartsAt + Zone;
	const FDateTime LocalLast = Event->EndsAt + Zone - FTimespan::FromMinutes(1.0);
	const FText Range = LocalStart.GetMonth() == LocalLast.GetMonth()
		? FText::FromString(FString::Printf(TEXT("%d - %d %s"), LocalStart.GetDay(), LocalLast.GetDay(), Months[LocalLast.GetMonth() - 1]))
		: FText::FromString(FString::Printf(TEXT("%d %s - %d %s"), LocalStart.GetDay(), Months[LocalStart.GetMonth() - 1],
			LocalLast.GetDay(), Months[LocalLast.GetMonth() - 1]));
	const FText Window = FText::FromString(FString::Printf(TEXT("%s  -  %s"),
		*LocalStart.ToString(TEXT("%Y-%m-%d %H:%M")), *LocalLast.ToString(TEXT("%Y-%m-%d %H:%M"))));
	const FText Details = Event->Tagline.IsEmpty() ? LOCTEXT("EventDetailsDefault", "Play rated matches during the event and claim the rewards.")
		: FText::FromString(Event->Tagline);
	const FText Day = FestivalDayText(*Event);

	TSharedRef<SWidget> PosterText = SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Event->Label.ToUpper()))
			.Font(CampusAngular::Display(44))
			.ColorAndOpacity(FLinearColor::White)
			.ShadowOffset(FVector2D(3.f, 3.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(8.f)).Fill(FLinearColor::White).ContentPadding(FMargin(12.f, 2.f))
				[
					SNew(STextBlock).Text(Range).Font(CampusAngular::Display(15)).ColorAndOpacity(FLinearColor::Black)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Day.IsEmpty() ? SNullWidget::NullWidget : EventChip(CampusAngular::EGlyph::Crown, Day, Theme)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(8.f))
			.Fill(FLinearColor(0.f, 0.f, 0.f, 0.62f))
			.AccentEdge(Theme)
			.AccentEdgeWidth(3.f)
			.ContentPadding(FMargin(12.f, 8.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("EventDetails", "Event Details"), 15, FLinearColor::White)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)
				[
					SNew(STextBlock).Text(Details).Font(CampusAngular::Body(14)).ColorAndOpacity(Theme).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("EventTime", "Event Time"), 15, FLinearColor::White)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(Window).Font(CampusAngular::Numeric(14)).ColorAndOpacity(Theme)
				]
			]
		];
	TSharedRef<SWidget> Poster = SNew(SBox).WidthOverride(430.f)
		[
			IsFestive(*Event)
				? FestivePlate(*Event, 1.f, true, FMargin(20.f, 16.f), FOnClicked::CreateLambda([]() { return FReply::Handled(); }), PosterText)
				: StaticCastSharedRef<SWidget>(SNew(SCampusArtPlate)
					.Shape(FCampusShape::Notched(16.f))
					.RemoteKey(Event->BannerKey)
					.FocusY(0.4f)
					.ScrimStrength(0.45f)
					.Border(Theme)
					.BorderWidth(2.f)
					.ContentPadding(FMargin(20.f, 16.f))
					[
						PosterText
					])
		];

	// ---- RIGHT: the clock, the boosts, and the rewards
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	if (EventProgress.IsValid() && EventProgress->EventId == EventShownId && EventProgress->Rewards.Num() > 0)
	{
		for (const FCampusEventRewardProgress& Reward : EventProgress->Rewards)
		{
			Rows->AddSlot().AutoHeight().Padding(0.f, 4.f)[BuildEventRewardRow(Reward)];
		}
	}
	else if (!bLive && Event->Rewards.Num() == 0)
	{
		// An announced festival whose rewards are still being planned: say they are coming, not "none".
		Rows->AddSlot().AutoHeight()[Label(LOCTEXT("EventRewardsSoon", "REWARDS WILL BE REVEALED BEFORE IT STARTS."), 17, Muted)];
	}
	else if (bEventProgressLoading)
	{
		Rows->AddSlot().AutoHeight()[Label(LOCTEXT("EventLoading", "LOADING YOUR PROGRESS..."), 17, Muted)];
	}
	else if (EventProgress.IsValid())
	{
		Rows->AddSlot().AutoHeight()[Label(LOCTEXT("EventNoRewards", "THIS EVENT HAS NO REWARDS TO CLAIM."), 17, Muted)];
	}
	else
	{
		// The read failed (or there is no session). RETRY is the only way it is asked again - the
		// latch is what stops a failed read repainting itself into another one.
		Rows->AddSlot().AutoHeight()[Label(LOCTEXT("EventNoProgress", "COULD NOT READ YOUR PROGRESS."), 17, Muted)];
		Rows->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(LOCTEXT("EventRetry", "RETRY"), FOnClicked::CreateLambda([this]()
				{
					bEventProgressAsked = false;
					RefreshEventProgress();
					Refresh();
					return FReply::Handled();
				}), EButton::Dark, 17, FVector2D(190.f, 52.f))
			]
		];
	}
	TSharedRef<SWidget> Right = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
			[
				Countdown(bLive ? Event->EndsAt : Event->StartsAt,
					bLive ? LOCTEXT("EventEndsIn", "ENDS IN") : LOCTEXT("EventStartsIn", "STARTS IN"), 18, Theme)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[EventMultipliers(*Event)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Label(bLive ? LOCTEXT("EventRated", "RATED MATCHES COUNT") : LOCTEXT("EventNotStarted", "NOT STARTED YET"), 13, Muted)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			Label(LOCTEXT("EventRewards", "REWARDS"), 20, FLinearColor::White)
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[Rows]
		];

	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.84f)]
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
						SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(10.f)).Fill(Theme)
						[
							SNew(SBox).WidthOverride(14.f).HeightOverride(32.f)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Label(LOCTEXT("EventsTitle", "EVENTS"), 32, FLinearColor::White)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close,
							FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }), 62.f)
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 18.f, 0.f)
					[
						SNew(SScrollBox) + SScrollBox::Slot()[Rail]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 18.f, 0.f)[Poster]
					+ SHorizontalBox::Slot().FillWidth(1.f)[Right]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
