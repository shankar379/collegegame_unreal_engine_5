// WHAT BRINGS THE PLAYER BACK TOMORROW (Slate, lobby level): the weekend board, the daily and
// weekly missions, the login streak, the 18+ confirm and the college picker.
//
// EVERY COUNTDOWN RUNS OFF THE SERVER CLOCK (UCampusBackend::Engagement::Now), never the phone's:
// a player who moves their device clock forward must not be able to claim tomorrow's reward, and
// one whose clock is simply wrong must not be told a window is shut when it is open.
//
// NOTHING HERE DECIDES WHAT ANYTHING IS WORTH. Every state - claimable, claimed, locked - and
// every amount comes from GET /engagement/home; these screens paint it and send the claim back.
//
// Split out of Campus/CampusLobbyUI.cpp; Campus/CampusLobbyUIInternal.h carries the file map.

#include "Campus/CampusUI.h"

#include "Campus/CampusBackend.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusLobbyUIInternal.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusLobbyUI"

using namespace CampusStyle;

// ---------------------------------------------------------------------------------------------
// Engagement: weekend board, missions, login streak, 18+ confirm, college picker.
// Every countdown runs off the server clock (UCampusBackend::Engagement::Now), never the phone's,
// and is drawn through a text lambda so a ticking clock never rebuilds the screen.

TSharedRef<SWidget> SCampusLobbyWidget::Countdown(const FDateTime& Until, const FText& Prefix, int32 Size, FLinearColor Color) const
{
	TWeakObjectPtr<ACampusLobbyPlayerController> Weak = Controller;

	// Slate evaluates a text attribute on every paint. A clock only changes once a second, so the
	// formatted FText is built once per tick of the clock and simply handed back in between -
	// otherwise a visible countdown would allocate a string 60 times a second, forever.
	struct FClockState
	{
		FDateTime Until;
		FText Caption;
		FText Cached;
		int32 LastSeconds = -1;
	};
	TSharedRef<FClockState> State = MakeShared<FClockState>();
	State->Until = Until;
	State->Caption = Prefix;
	State->Cached = Prefix;

	return SNew(STextBlock)
		.Font(CampusAngular::Display(Size))
		.ColorAndOpacity(Color)
		.Text_Lambda([Weak, State]()
		{
			const UCampusBackend* Api = Weak.IsValid() ? Weak->GetBackend() : nullptr;
			if (!Api || !Api->Engagement.bLoaded)
			{
				return State->Caption;
			}
			const int32 Seconds = Api->Engagement.SecondsUntil(State->Until);
			if (Seconds == State->LastSeconds)
			{
				return State->Cached;
			}
			State->LastSeconds = Seconds;
			const int32 Days = Seconds / 86400;
			const int32 Hours = (Seconds % 86400) / 3600;
			const int32 Minutes = (Seconds % 3600) / 60;
			const FString Clock = Days > 0
				? FString::Printf(TEXT("%dD %02dH %02dM"), Days, Hours, Minutes)
				: FString::Printf(TEXT("%02d:%02d:%02d"), Hours, Minutes, Seconds % 60);
			State->Cached = FText::Format(FTextFormat::FromString(TEXT("{0} {1}")), State->Caption, FText::FromString(Clock));
			return State->Cached;
		});
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildWeekendPanel()
{
	const UCampusBackend* Api = Backend();
	if (!Api || !Api->Engagement.bLoaded || !Api->Engagement.Board.bValid)
	{
		// No weekend configured, or the feed has not landed: render nothing rather than a hole.
		return SNullWidget::NullWidget;
	}
	const FCampusWeekend& Board = Api->Engagement.Board;
	const bool bUpcoming = Board.IsUpcoming();

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// Header: title + the countdown to whichever edge matters.
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Label(LOCTEXT("WeekendTop", "WEEKEND TOP PLAYERS"), 20, Accent)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			// A provisional board says so, plainly: it is the honest face of the anti-cheat hold.
			Board.bProvisional
				? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Slanted(8.f))
					.Fill(FLinearColor(0.35f, 0.26f, 0.02f, 0.9f))
					.ContentPadding(FMargin(9.f, 2.f))
					[
						SNew(STextBlock).Text(LOCTEXT("Provisional", "PROVISIONAL"))
						.Font(CampusAngular::Display(12)).ColorAndOpacity(CardGold)
					])
				: SNullWidget::NullWidget
		]
	];
	Body->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 8.f)
	[
		Countdown(bUpcoming ? Board.StartsAt : Board.EndsAt,
			bUpcoming ? LOCTEXT("StartsIn", "STARTS IN") : LOCTEXT("EndsIn", "ENDS IN"), 16, FLinearColor::White)
	];

	if (Board.bDegraded)
	{
		Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
		[
			SNew(STextBlock).Text(LOCTEXT("BoardDegraded", "Ranks are catching up."))
			.Font(CampusAngular::Body(13)).ColorAndOpacity(Muted)
		];
	}

	// Top 3.
	for (const FCampusBoardEntry& Entry : Board.Top)
	{
		Body->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SBox).WidthOverride(30.f)
				[
					SNew(STextBlock).Text(FText::AsNumber(Entry.Rank))
					.Font(CampusAngular::Numeric(18)).ColorAndOpacity(Entry.Rank == 1 ? Accent : Muted)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(Entry.Name.ToUpper()))
				.Font(CampusAngular::Display(17)).ColorAndOpacity(Entry.bMe ? Accent : FLinearColor::White)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::AsNumber(Entry.Points))
				.Font(CampusAngular::Numeric(17)).ColorAndOpacity(FLinearColor::White)
			]
		];
	}

	// My row: always present, either my standing or the way in.
	Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
	[
		SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Slanted(12.f))
		.Fill(Board.bHasMe ? FLinearColor(0.12f, 0.10f, 0.03f, 0.92f) : FLinearColor(0.f, 0.f, 0.f, 0.35f))
		.Border(Board.bHasMe ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.18f))
		.BorderWidth(1.f)
		.ContentPadding(FMargin(14.f, 7.f))
		[
			Board.bHasMe
				? StaticCastSharedRef<SWidget>(SNew(STextBlock)
					.Text(Board.bHasNextRank
						? FText::Format(LOCTEXT("MyRowGap", "#{0}  ·  {1} PTS BEHIND #{2}"),
							FText::AsNumber(Board.MyRank), FText::AsNumber(Board.NextGap), FText::AsNumber(Board.NextRank))
						: FText::Format(LOCTEXT("MyRowTop", "#{0}  ·  {1} PTS  ·  LEADING"),
							FText::AsNumber(Board.MyRank), FText::AsNumber(Board.MyPoints)))
					.Font(CampusAngular::Display(16))
					.ColorAndOpacity(FLinearColor::White))
				// The empty state names the exact thing that gets you on the board.
				: StaticCastSharedRef<SWidget>(SNew(STextBlock)
					.Text(LOCTEXT("BoardEmpty", "Play a match with 4+ players this weekend to enter the board."))
					.Font(CampusAngular::Body(14))
					.ColorAndOpacity(Muted)
					.AutoWrapText(true))
		]
	];

	return SNew(SBox).WidthOverride(320.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(18.f))
			.Fill(FLinearColor(0.015f, 0.02f, 0.03f, 0.84f))
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.AccentEdge(Accent)
			.AccentEdgeWidth(4.f)
			.ContentPadding(FMargin(22.f, 14.f, 16.f, 14.f))
			[
				Body
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildClaimBanner()
{
	const UCampusBackend* Api = Backend();
	if (!Api || !Api->Engagement.bLoaded || !Api->Engagement.LastWeekend.bUnclaimed)
	{
		return SNullWidget::NullWidget;   // zero footprint when there is nothing to claim
	}
	const FCampusLastWeekend& Last = Api->Engagement.LastWeekend;
	const FString PeriodId = Last.PeriodId;
	const FString Rewards = Last.Reward.Describe();

	return SNew(SCampusAngularButton)
		.Shape(FCampusShape::Wedge(18.f, 12.f))
		.Fill(CampusAngular::Accent)
		.Border(FLinearColor(1.f, 1.f, 1.f, 0.35f))
		.BorderWidth(2.f)
		.ContentPadding(FMargin(22.f, 10.f))
		.OnClicked(FOnClicked::CreateLambda([this, PeriodId]()
		{
			if (UCampusBackend* Api2 = Backend(); Api2 && !bClaiming)
			{
				bClaiming = true;
				Busy(TEXT("Claiming your reward..."));
				Api2->ClaimPeriodReward(PeriodId, [this](bool bOk, const FString& Error)
				{
					AfterClaim(bOk, Error, TEXT("Weekend reward claimed!"));
				});
			}
			return FReply::Handled();
		}))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
			[
				SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)
				[
					SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Crown).Size(22.f).Color(FLinearColor(0.06f, 0.05f, 0.02f, 1.f))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Last.MyRank > 0
						? FText::Format(LOCTEXT("LastRank", "LAST WEEKEND: YOU FINISHED #{0}"), FText::AsNumber(Last.MyRank))
						: LOCTEXT("LastReward", "LAST WEEKEND'S REWARD IS WAITING"))
					.Font(CampusAngular::Display(17)).ColorAndOpacity(FLinearColor(0.06f, 0.05f, 0.02f, 1.f))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("TAP TO CLAIM  %s"), *Rewards)))
					.Font(CampusAngular::Display(13)).ColorAndOpacity(FLinearColor(0.2f, 0.16f, 0.04f, 1.f))
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildMissionCard(const FCampusMission& Mission, const FString& PeriodId)
{
	const bool bClaimable = Mission.IsClaimable();
	const bool bClaimed = Mission.State == TEXT("claimed");
	const bool bExpired = Mission.State == TEXT("expired");
	const FString MissionId = Mission.Id;
	const float Fraction = Mission.Fraction();

	TSharedRef<SWidget> Action = bClaimed
		? StaticCastSharedRef<SWidget>(Label(LOCTEXT("MissionClaimed", "CLAIMED"), 16, CampusAngular::Go))
		: bExpired
			? StaticCastSharedRef<SWidget>(Label(LOCTEXT("MissionExpired", "EXPIRED"), 16, Muted))
			: bClaimable
				? CampusStyle::Button(LOCTEXT("MissionClaim", "CLAIM"), FOnClicked::CreateLambda([this, PeriodId, MissionId]()
					{
						if (UCampusBackend* Api = Backend(); Api && !bClaiming)
						{
							bClaiming = true;
							Busy(TEXT("Claiming..."));
							Api->ClaimMission(PeriodId, MissionId, [this](bool bOk, const FString& Error)
							{
								AfterClaim(bOk, Error, TEXT("Mission reward claimed!"));
							});
						}
						return FReply::Handled();
					}), EButton::Accent, 17, FVector2D(150.f, 52.f), !bClaiming)
				: StaticCastSharedRef<SWidget>(Label(
					FText::FromString(FString::Printf(TEXT("%d / %d"), Mission.Progress, Mission.Target)), 18, FLinearColor::White));

	return CampusStyle::Card(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(Mission.Title.ToUpper()))
				.Font(CampusAngular::Display(20)).ColorAndOpacity(bClaimed || bExpired ? Muted : FLinearColor::White)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(FText::FromString(Mission.Subtitle))
				.Font(CampusAngular::Body(13)).ColorAndOpacity(Muted).AutoWrapText(true)
			]
			// Flat progress bar with the fill leaning like everything else.
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SBox).HeightOverride(8.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SCampusAngularPanel).Fill(FLinearColor(1.f, 1.f, 1.f, 0.12f))]
					+ SOverlay::Slot().HAlign(HAlign_Left)
					[
						SNew(SBox).WidthOverride(FMath::Max(2.f, 380.f * Fraction))
						[
							SNew(SCampusAngularPanel).Fill(bClaimable ? CampusAngular::Go : Accent)
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(FText::FromString(Mission.Reward.Describe()))
				.Font(CampusAngular::Display(14)).ColorAndOpacity(CardGold)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Action],
		bClaimable, FMargin(22.f, 14.f, 18.f, 14.f));
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildMissions()
{
	const UCampusBackend* Api = Backend();
	// A reference needs something that outlives this call: never bind one to a temporary.
	static const FCampusEngagement Empty;
	const FCampusEngagement& Feed = Api ? Api->Engagement : Empty;
	const FCampusMissionPanel& Panel =
		MissionTab == EMissionTab::Daily ? Feed.Daily :
		MissionTab == EMissionTab::Weekly ? Feed.Weekly : Feed.WeekendMissions;

	// MissionTabs, each carrying its own claimable count as a numeric dot.
	TSharedRef<SHorizontalBox> MissionTabs = SNew(SHorizontalBox);
	struct FTabInfo { EMissionTab Tab; const TCHAR* Label; };
	static const FTabInfo TabList[] = {
		{ EMissionTab::Daily, TEXT("DAILY") }, { EMissionTab::Weekly, TEXT("WEEKLY") }, { EMissionTab::Weekend, TEXT("WEEKEND") },
	};
	for (const FTabInfo& Info : TabList)
	{
		const FCampusMissionPanel& Which =
			Info.Tab == EMissionTab::Daily ? Feed.Daily : Info.Tab == EMissionTab::Weekly ? Feed.Weekly : Feed.WeekendMissions;
		const int32 Ready = Which.ClaimableCount();
		const EMissionTab Target = Info.Tab;
		MissionTabs->AddSlot().AutoWidth().Padding(4.f, 0.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				CampusStyle::Pill(FText::FromString(Info.Label), MissionTab == Target,
					FOnClicked::CreateLambda([this, Target]() { MissionTab = Target; Refresh(); return FReply::Handled(); }),
					FVector2D(168.f, 52.f))
			]
			// The dot carries the number, so it is never "there is something" with no idea how much.
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, -6.f, -6.f, 0.f)
			[
				Ready > 0
					? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(26.f).HeightOverride(26.f)
						[
							SNew(SCampusAngularPanel)
							.Shape(FCampusShape::Notched(7.f))
							.Fill(CampusAngular::Go)
							[
								SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(FText::AsNumber(Ready))
									.Font(CampusAngular::Numeric(15)).ColorAndOpacity(FLinearColor::White)
								]
							]
						])
					: SNullWidget::NullWidget
			]
		];
	}

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (!Panel.bValid || Panel.Items.Num() == 0)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 20.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoMissions", "No missions in this period yet. Check back after the next reset."))
			.Font(CampusAngular::Body(16)).ColorAndOpacity(Muted).AutoWrapText(true)
		];
	}
	else
	{
		// The API already sorts claimable first; keep its order.
		for (const FCampusMission& Mission : Panel.Items)
		{
			List->AddSlot().AutoHeight().Padding(0.f, 4.f)[BuildMissionCard(Mission, Panel.PeriodId)];
		}
		if (Panel.bHasBonus)
		{
			const bool bBonusReady = Panel.BonusState == TEXT("claimable");
			const bool bBonusDone = Panel.BonusState == TEXT("claimed");
			List->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
			[
				CampusStyle::Card(
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							Label(FText::Format(LOCTEXT("SetBonus", "SET BONUS  ·  {0} OF {1} DONE"),
								FText::AsNumber(Panel.BonusDone), FText::AsNumber(Panel.BonusNeed)), 19,
								bBonusDone ? Muted : FLinearColor::White)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(FText::FromString(Panel.BonusReward.Describe()))
							.Font(CampusAngular::Display(14)).ColorAndOpacity(CardGold)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						// The bonus pays itself out with the claim that earns it, so this is a status, not a button.
						Label(bBonusDone ? LOCTEXT("BonusPaid", "PAID") : bBonusReady ? LOCTEXT("BonusReady", "READY") : LOCTEXT("BonusLocked", "LOCKED"),
							16, bBonusDone ? CampusAngular::Go : bBonusReady ? Accent : Muted)
					],
					bBonusReady)
			];
		}
	}

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[MissionTabs]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Panel.bValid && Panel.EndsAt != FDateTime()
					? Countdown(Panel.EndsAt, LOCTEXT("ResetsIn", "RESETS IN"), 15, Muted)
					: SNullWidget::NullWidget
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 14.f, 0.f, 0.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[List]
		];

	return Dialog(LOCTEXT("MissionsTitle", "MISSIONS"), Body, 940.f, true, EScreen::Lobby);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildStreak()
{
	const UCampusBackend* Api = Backend();
	const FCampusStreak Streak = Api ? Api->Engagement.Streak : FCampusStreak();

	// The 7-day ladder: past ticked, today lit, future dimmed but legible.
	TSharedRef<SHorizontalBox> Ladder = SNew(SHorizontalBox);
	for (const FCampusStreakStep& Step : Streak.Ladder)
	{
		const bool bDone = Step.State == TEXT("claimed");
		const bool bToday = Step.State == TEXT("claimable");
		Ladder->AddSlot().AutoWidth().Padding(4.f, 0.f)
		[
			SNew(SBox).WidthOverride(104.f)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Notched(12.f))
				.Fill(bToday ? FLinearColor(0.14f, 0.11f, 0.02f, 0.95f)
					: bDone ? FLinearColor(0.05f, 0.14f, 0.08f, 0.9f) : FLinearColor(0.f, 0.f, 0.f, 0.4f))
				.Border(bToday ? Accent : bDone ? CampusAngular::Go : FLinearColor(1.f, 1.f, 1.f, 0.16f))
				.BorderWidth(bToday ? 2.f : 1.f)
				.ContentPadding(FMargin(8.f, 12.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text(FText::Format(LOCTEXT("StreakDay", "DAY {0}"), FText::AsNumber(Step.Day)))
						.Font(CampusAngular::Display(14)).ColorAndOpacity(bToday ? Accent : Muted)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(FText::FromString(Step.Reward.Describe()))
						.Font(CampusAngular::Display(13))
						.ColorAndOpacity(bDone ? CampusAngular::Go : bToday ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.55f))
						.AutoWrapText(true)
						.Justification(ETextJustify::Center)
					]
				]
			]
		];
	}

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(FText::Format(LOCTEXT("StreakLen", "{0} DAY STREAK  ·  BEST {1}"),
				FText::AsNumber(Streak.CurrentLen), FText::AsNumber(Streak.BestLen)), 22, FLinearColor::White)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f)[Ladder]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			CampusStyle::StartButton(
				Streak.bClaimable
					? FText::FromString(FString::Printf(TEXT("CLAIM  %s"), *Streak.TodayReward.Describe()))
					: LOCTEXT("StreakClaimed", "CLAIMED TODAY"),
				FOnClicked::CreateLambda([this]()
				{
					UCampusBackend* Api2 = Backend();
					if (!Api2 || bClaiming || !Api2->Engagement.Streak.bClaimable)
					{
						Show(EScreen::Lobby);
						return FReply::Handled();
					}
					bClaiming = true;
					Busy(TEXT("Claiming..."));
					Api2->ClaimStreak([this](bool bOk, const FString& Error)
					{
						bClaiming = false;
						bBusy = false;
						if (bOk)
						{
							Toast(TEXT("Streak reward claimed!"));
							RefreshEngagement();
						}
						else if (const UCampusBackend* A = Backend(); A && A->bRewardCapReached)
						{
							Toast(TEXT("You have hit today's reward limit. This is waiting for you tomorrow."));
						}
						else
						{
							Toast(Error);
						}
						// Back to the lobby either way: the popup must never trap the player.
						Show(EScreen::Lobby);
					});
					return FReply::Handled();
				}), FVector2D(460.f, 86.f))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
		[
			Button(LOCTEXT("StreakLater", "LATER"),
				FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }), EButton::Ghost, 18, FVector2D(220.f, 56.f))
		];

	return Dialog(LOCTEXT("StreakTitle", "DAILY LOGIN STREAK"), Body, 880.f, true, EScreen::Lobby);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildAdultConfirm()
{
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AdultBody",
				"Campus Combat is for players aged 18 and over. Confirm your age to carry on playing. "
				"We record only that you confirmed; we never ask for or store your date of birth."))
			.Font(CampusAngular::Body(17)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 26.f, 0.f, 0.f)
		[
			CampusStyle::StartButton(LOCTEXT("AdultYes", "I AM 18 OR OLDER"), FOnClicked::CreateLambda([this]()
			{
				if (UCampusBackend* Api = Backend())
				{
					Busy(TEXT("Confirming..."));
					Api->ConfirmAdult([this](bool bOk, const FString& Error)
					{
						bBusy = false;
						if (bOk)
						{
							// Whatever was refused a moment ago can be asked for again now.
							RefreshEngagement();
							Show(EScreen::Lobby);
						}
						else
						{
							Toast(Error);
							Refresh();
						}
					});
				}
				return FReply::Handled();
			}), FVector2D(460.f, 86.f))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)
		[
			Button(LOCTEXT("AdultNo", "SIGN OUT"), FOnClicked::CreateLambda([this]()
			{
				if (UCampusBackend* Api = Backend()) { Api->SetAdultTick(false); Api->Logout(); }
				if (UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr)
				{
					Instance->bSignedIn = false;
				}
				bAdultTicked = false;
				Show(EScreen::LoginChoice);
				return FReply::Handled();
			}), EButton::Ghost, 18, FVector2D(220.f, 56.f))
		];
	// No close button: this one really is a gate.
	return Dialog(LOCTEXT("AdultTitle", "AGE CONFIRMATION"), Body, 760.f, false, EScreen::AdultConfirm);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildCollegePick()
{
	const UCampusBackend* Api = Backend();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

	// "No college" is a first-class choice, not an empty row.
	auto Row = [this](const FString& Id, const FString& Name, const FString& City)
	{
		const bool bSelected = PickedCollegeId == Id;
		return CampusStyle::Card(
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Name.ToUpper()), 19)]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(City)).Font(CampusAngular::Body(13)).ColorAndOpacity(Muted)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				CampusStyle::Pill(bSelected ? LOCTEXT("Picked", "PICKED") : LOCTEXT("Pick", "PICK"), bSelected,
					FOnClicked::CreateLambda([this, Id]() { PickedCollegeId = Id; Refresh(); return FReply::Handled(); }),
					FVector2D(132.f, 46.f))
			],
			bSelected);
	};

	List->AddSlot().AutoHeight().Padding(0.f, 3.f)[Row(FString(), TEXT("No college"), TEXT("Stay off the college boards"))];
	if (Api)
	{
		for (const FCampusCollege& College : Api->Colleges)
		{
			List->AddSlot().AutoHeight().Padding(0.f, 3.f)[Row(College.Id, College.Name, College.City)];
		}
		if (Api->Colleges.Num() == 0)
		{
			List->AddSlot().AutoHeight().Padding(0.f, 16.f)
			[
				SNew(STextBlock).Text(LOCTEXT("NoColleges", "The college list could not be loaded. Try again in a moment."))
				.Font(CampusAngular::Body(15)).ColorAndOpacity(Muted).AutoWrapText(true)
			];
		}
	}

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SScrollBox) + SScrollBox::Slot()[List]]
		// Privacy toggle, default off: nothing about a player's college goes on a public board unasked.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("ShowCollege", "SHOW MY COLLEGE ON LEADERBOARDS"), 18)]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(LOCTEXT("ShowCollegeHint", "Off by default. Your rank is the same either way."))
					.Font(CampusAngular::Body(13)).ColorAndOpacity(Muted)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				CampusStyle::Pill(bShowCollegeTick ? LOCTEXT("On2", "ON") : LOCTEXT("Off2", "OFF"), bShowCollegeTick,
					FOnClicked::CreateLambda([this]() { bShowCollegeTick = !bShowCollegeTick; Refresh(); return FReply::Handled(); }),
					FVector2D(110.f, 48.f))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 18.f, 0.f, 0.f)
		[
			Button(LOCTEXT("SaveCollege", "SAVE"), FOnClicked::CreateLambda([this]()
			{
				if (UCampusBackend* Api2 = Backend())
				{
					Busy(TEXT("Saving..."));
					Api2->SetCollege(PickedCollegeId, bShowCollegeTick, [this](bool bOk, const FString& Error)
					{
						Done(bOk, Error, [this]() { Toast(TEXT("College saved.")); Show(EScreen::Account); });
					});
				}
				return FReply::Handled();
			}), EButton::Go, 22, FVector2D(300.f, 68.f))
		];

	return Dialog(LOCTEXT("CollegeTitle", "YOUR COLLEGE"), Body, 820.f, true, EScreen::Account);
}

#undef LOCTEXT_NAMESPACE
