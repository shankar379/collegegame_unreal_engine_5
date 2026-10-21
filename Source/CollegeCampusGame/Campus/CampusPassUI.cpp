// THE CAMPUS PASS SCREEN (lobby v2, backend migration 031): the season's level track.
//
//   header   season name, time left, LEVEL n with the points bar, and PREMIUM (bought with Diamonds) or
//            PREMIUM ACTIVE
//   track    one column per level, left to right: the FREE reward on top, the PREMIUM reward under it, each
//            with CLAIM / CLAIMED / LOCKED / PREMIUM. A horizontal scroll, opened at the player's level so the
//            next reward is what they see first.
//
// Everything comes from GET /v1/pass (UCampusBackend::Pass); a claim or a purchase answers with the new pass,
// so the screen is redrawn from the server's word and never from a guess.

#include "Campus/CampusUI.h"
#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusLobbyUI"

using namespace CampusStyle;

void SCampusLobbyWidget::OpenPass()
{
	if (UCampusBackend* Api = Backend())
	{
		Busy(TEXT("Opening Campus Pass..."));
		Api->LoadPass([this](bool bOk, const FString& Error) { Done(bOk, Error, [this]() { Show(EScreen::Pass); }); });
	}
}

namespace CampusPassUI
{
	constexpr float ColumnWidth = 168.f;
	constexpr float CellHeight = 132.f;
	const FLinearColor FreeFill(0.03f, 0.05f, 0.07f, 0.92f);
	const FLinearColor PremiumFill(0.12f, 0.09f, 0.02f, 0.94f);
	const FLinearColor Gold(1.f, 0.77f, 0.19f, 1.f);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildPass()
{
	using namespace CampusPassUI;
	const UCampusBackend* Api = Backend();
	if (!Api || !Api->Pass.bValid)
	{
		return Dialog(LOCTEXT("PassTitle", "CAMPUS PASS"),
			Label(LOCTEXT("PassNone", "No season is running right now. The next Campus Pass starts soon."), 18, Muted, false),
			720.f, true, EScreen::Lobby);
	}
	const FCampusPass& P = Api->Pass;

	// ---- the header: season, time left, level + bar, premium
	const int32 DaysLeft = FMath::Max(0, int32((P.EndsAt - FDateTime::UtcNow()).GetTotalDays()));
	TSharedRef<SWidget> PremiumAction = P.bPremium
		? StaticCastSharedRef<SWidget>(Label(LOCTEXT("PremiumOn", "PREMIUM ACTIVE"), 18, Gold))
		: Button(FText::FromString(FString::Printf(TEXT("UNLOCK PREMIUM  %d DIAMONDS"), P.PremiumPrice)), FOnClicked::CreateLambda([this]()
			{
				UCampusBackend* A = Backend();
				if (!A)
				{
					return FReply::Handled();
				}
				if (A->Diamonds < A->Pass.PremiumPrice)
				{
					// short of Diamonds: the only useful thing this tap can do is take them to the top-up
					StoreTab = EStoreTab::Packs;
					Toast(TEXT("Top up Diamonds, then come back for the Premium pass."));
					Show(EScreen::Store);
					return FReply::Handled();
				}
				Busy(TEXT("Unlocking Premium..."));
				A->BuyPassPremium([this](bool bOk, const FString& Error) { Done(bOk, Error, [this]() { Toast(TEXT("Premium pass unlocked!")); Refresh(); }); });
				return FReply::Handled();
			}), EButton::Go, 17, FVector2D(330.f, 54.f));

	TSharedRef<SWidget> Header = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 24.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(P.SeasonName.ToUpper()), 22)]
			+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(FString::Printf(TEXT("ENDS IN %d DAYS"), DaysLeft)), 14, Muted)]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				Label(FText::FromString(FString::Printf(TEXT("LEVEL %d / %d   ·   %d / %d PTS"), P.Level, P.MaxLevel, P.PointsIntoLevel, P.PointsPerLevel)), 16)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 24.f, 0.f)
			[
				SNew(SBox).HeightOverride(8.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[SNew(SCampusAngularPanel).Fill(FLinearColor(1.f, 1.f, 1.f, 0.12f))]
					+ SOverlay::Slot().HAlign(HAlign_Left)
					[
						SNew(SBox).WidthOverride(FMath::Max(6.f, 360.f * P.LevelFraction()))[SNew(SCampusAngularPanel).Fill(Gold)]
					]
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[PremiumAction];

	// ---- one reward cell
	auto Cell = [this](int32 Level, const FCampusReward& Reward, const FString& State, bool bPremiumTrack) -> TSharedRef<SWidget>
	{
		TSharedRef<SWidget> Action = SNullWidget::NullWidget;
		if (State == TEXT("claimable"))
		{
			const FString Track = bPremiumTrack ? TEXT("premium") : TEXT("free");
			Action = Button(LOCTEXT("PassClaim", "CLAIM"), FOnClicked::CreateLambda([this, Level, Track]()
			{
				if (UCampusBackend* A = Backend())
				{
					Busy(TEXT("Claiming..."));
					A->ClaimPass(Level, Track, [this](bool bOk, const FString& Error) { Done(bOk, Error, [this]() { Toast(TEXT("Reward claimed!")); Refresh(); }); });
				}
				return FReply::Handled();
			}), EButton::Go, 15, FVector2D(120.f, 40.f));
		}
		else
		{
			const FText Word = State == TEXT("claimed") ? LOCTEXT("PassClaimed", "CLAIMED")
				: State == TEXT("locked_premium") ? LOCTEXT("PassPremiumLock", "PREMIUM")
				: LOCTEXT("PassLocked", "LOCKED");
			Action = Label(Word, 13, State == TEXT("claimed") ? CampusAngular::Go : Muted);
		}
		return SNew(SBox).WidthOverride(ColumnWidth - 8.f).HeightOverride(CellHeight)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Notched(10.f))
				.Fill(bPremiumTrack ? PremiumFill : FreeFill)
				.Border(State == TEXT("claimable") ? Gold : CampusAngular::Hairline)
				.BorderWidth(State == TEXT("claimable") ? 2.f : 1.f)
				.ContentPadding(FMargin(10.f, 8.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Reward.IsEmpty() ? TEXT("-") : Reward.Describe()))
						.Font(CampusAngular::Body(14)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Action]
				]
			];
	};

	// ---- the track: one column per level
	TSharedRef<SScrollBox> Track = SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarVisibility(EVisibility::Visible);
	for (const FCampusPassLevel& Row : P.Levels)
	{
		const bool bReached = Row.Level <= P.Level;
		Track->AddSlot().Padding(4.f, 0.f)
		[
			SNew(SBox).WidthOverride(ColumnWidth)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 6.f)
				[
					Label(FText::FromString(FString::Printf(TEXT("LV %d"), Row.Level)), 16, bReached ? Gold : Muted)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Cell(Row.Level, Row.Free, Row.FreeState, false)]
				+ SVerticalBox::Slot().AutoHeight()[Cell(Row.Level, Row.Premium, Row.PremiumState, true)]
			]
		];
	}
	// open at the player's level: the next reward is the first thing they see
	const float Offset = FMath::Max(0.f, (P.Level - 2) * (ColumnWidth + 8.f));
	Track->SetScrollOffset(Offset);

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[Header]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 30.f, 0.f, 0.f)[SNew(SBox).HeightOverride(CellHeight).VAlign(VAlign_Center)[Label(LOCTEXT("PassFree", "FREE"), 14, Muted)]]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[SNew(SBox).HeightOverride(CellHeight).VAlign(VAlign_Center)[Label(LOCTEXT("PassPrem", "PREMIUM"), 14, Gold)]]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Track]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			Label(LOCTEXT("PassHow", "Earn pass points from missions and every rated match (10 + 2 per kill + 20 for a win)."), 13, Muted, false)
		];

	return Dialog(LOCTEXT("PassTitle", "CAMPUS PASS"), Body, 1500.f, true, EScreen::Lobby);
}

#undef LOCTEXT_NAMESPACE
