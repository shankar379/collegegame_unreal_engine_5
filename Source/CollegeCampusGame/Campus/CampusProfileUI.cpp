// The profile page (docs/PROFILE_DESIGN.md): the full-screen identity sheet reached from the lobby's
// top-left plate, and the public version reached from the friends list and the post-match screen.
//
// Left half: the lobby mannequin, isolated and drag-to-rotate - the same actor and the same
// RotateDisplay() the lobby already uses, so there is no second 3D path to keep in step.
// Right half: ONE angular panel, three tabs, an SScrollBox. Switching tabs costs NO request: the one
// GET paints every tab, which is both the ngrok reflex and simply the right shape.
//
// EVERY NUMBER HERE IS THE SERVER'S. There is no client-side level formula any more (CampusSave.h),
// because a level the phone invents is a level two players can hold up side by side and disagree about.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSocial.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusProfileUI"

using namespace CampusStyle;

namespace
{
	/** The 3D figure owns the left 45 %; a drag there must never fight the panel's scroll. */
	constexpr float SceneShare = 0.45f;
	constexpr float PanelShare = 0.55f;
	constexpr float PanelMaxWidth = 980.f;
	/** A stats cell this wide lets all six fit one row on a 1080p phone in landscape. */
	constexpr float StatCellWidth = 112.f;
	constexpr float BadgeSize = 128.f;

	FText Grouped(int32 Value)
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(true);
		return FText::AsNumber(Value, &Options);
	}

	/** "1.31" / "33%" - two decimals for a ratio, whole percent for a rate. */
	FText Ratio(float Value)
	{
		FNumberFormattingOptions Options;
		Options.SetMinimumFractionalDigits(2);
		Options.SetMaximumFractionalDigits(2);
		return FText::AsNumber(Value, &Options);
	}

	FText Percent(float Fraction)
	{
		return FText::Format(LOCTEXT("PercentFmt", "{0}%"), FText::AsNumber(FMath::RoundToInt(Fraction * 100.f)));
	}

	FText ShortDate(const FDateTime& When)
	{
		return When == FDateTime() ? FText::GetEmpty() : FText::FromString(When.ToString(TEXT("%d %b")));
	}
}

// ---------------------------------------------------------------------------------------------
// Pieces

TSharedRef<SWidget> SCampusLobbyWidget::StatCell(CampusAngular::EGlyph Glyph, const FText& Value, const FText& Caption) const
{
	return SNew(SBox).WidthOverride(StatCellWidth).HeightOverride(92.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(28.f).HeightOverride(28.f)
				[
					SNew(SCampusGlyph).Glyph(Glyph).Size(28.f).Color(FLinearColor(1.f, 1.f, 1.f, 0.55f))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
			[
				// Tabular numerals so a column of stats lines up however the digits fall.
				SNew(STextBlock).Text(Value).Font(CampusAngular::Numeric(26)).ColorAndOpacity(FLinearColor::White)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text(Caption).Font(CampusAngular::Display(11)).ColorAndOpacity(CampusAngular::TextMuted)
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildStatsStrip()
{
	if (!ProfileData.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	const FCampusProfile& Page = *ProfileData;
	TSharedRef<SWrapBox> Strip = SNew(SWrapBox).UseAllottedSize(true);
	auto Add = [&Strip, this](CampusAngular::EGlyph Glyph, const FText& Value, const FText& Caption)
	{
		Strip->AddSlot().Padding(0.f, 0.f, 6.f, 6.f)[StatCell(Glyph, Value, Caption)];
	};
	Add(CampusAngular::EGlyph::Clock, Grouped(Page.Matches), LOCTEXT("StatMatches", "MATCHES"));
	Add(CampusAngular::EGlyph::Trophy, Grouped(Page.Wins), LOCTEXT("StatWins", "WINS"));
	Add(CampusAngular::EGlyph::Skull, Grouped(Page.Kills), LOCTEXT("StatKills", "KILLS"));
	Add(CampusAngular::EGlyph::Chevron, Ratio(Page.Kd), LOCTEXT("StatKd", "K/D"));
	Add(CampusAngular::EGlyph::Crown, Grouped(Page.Mvps), LOCTEXT("StatMvp", "MVP"));
	Add(CampusAngular::EGlyph::Target, Percent(Page.HeadshotRate), LOCTEXT("StatHs", "HS%"));
	return Strip;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildRankBlock()
{
	if (!ProfileData.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	const FCampusProfile& Page = *ProfileData;
	UCampusSocial* Live = Social();

	// There are NO ranked tiers in this game, and inventing one is the easiest way to make this page
	// worthless - a tier earned in private rooms against your own friends is farmable. So the block
	// shows the three things that are true: the equipped title, the best weekend ever, and this
	// weekend's live standing WITH its provisional flag.
	TSharedRef<SVerticalBox> Block = SNew(SVerticalBox);
	Block->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Crown).Size(22.f)
				.Color(Page.TitleName.IsEmpty() ? CampusAngular::TextMuted : CampusAngular::Accent)
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			Label(Page.TitleName.IsEmpty()
				? LOCTEXT("NoTitle", "NO TITLE YET — WIN A WEEKEND")
				: FText::FromString(Page.TitleName.ToUpper()), 22,
				Page.TitleName.IsEmpty() ? CampusAngular::TextMuted : CampusAngular::Accent)
		]
	];

	if (!Page.bHasRank)
	{
		// board_opt_out: a player who left the public boards should not find their rank on their
		// public profile. Their own view still shows it.
		Block->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			Label(LOCTEXT("NotOnBoards", "NOT ON PUBLIC BOARDS"), 15, CampusAngular::TextMuted)
		];
		return Block;
	}

	TSharedRef<SWidget> Best = Page.bHasBestWeekend
		? StaticCastSharedRef<SWidget>(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("BestWeekend", "BEST WEEKEND"), 12, CampusAngular::TextMuted)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("BestRank", "#{0}  ·  {1}"), FText::AsNumber(Page.BestWeekendRank),
					FText::FromString(Page.BestWeekendLabel.ToUpper())))
				.Font(CampusAngular::Numeric(22)).ColorAndOpacity(FLinearColor::White)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Label(FText::FromString(Page.BestWeekendBracket.ToUpper()), 12, CampusAngular::TextMuted)
			])
		: StaticCastSharedRef<SWidget>(SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("BestWeekend", "BEST WEEKEND"), 12, CampusAngular::TextMuted)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(SBox).MaxDesiredWidth(260.f)
				[
					SNew(STextBlock).Text(LOCTEXT("NeverRanked", "PLAY A RATED MATCH THIS WEEKEND TO GET ON THE BOARD"))
					.Font(CampusAngular::Body(13)).ColorAndOpacity(CampusAngular::TextMuted).AutoWrapText(true)
				]
			]);

	TSharedRef<SVerticalBox> Nowadays = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("ThisWeekend", "THIS WEEKEND"), 12, CampusAngular::TextMuted)];
	if (Page.bHasThisWeekend)
	{
		Nowadays->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(Page.WeekendRank > 0
				? FText::Format(LOCTEXT("NowRank", "{0} PTS  ·  #{1}"), Grouped(Page.WeekendPoints), FText::AsNumber(Page.WeekendRank))
				: FText::Format(LOCTEXT("NowPoints", "{0} PTS"), Grouped(Page.WeekendPoints)))
			.Font(CampusAngular::Numeric(22)).ColorAndOpacity(CampusAngular::Accent)
		];
		TSharedRef<SHorizontalBox> Flags = SNew(SHorizontalBox);
		if (Page.bWeekendProvisional)
		{
			// Eventual consistency and the anti-cheat review hold are IN the contract, not hidden.
			Flags->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Slanted(7.f))
				.Fill(FLinearColor(0.16f, 0.13f, 0.02f, 0.85f))
				.ContentPadding(FMargin(8.f, 2.f))
				[
					SNew(STextBlock).Text(LOCTEXT("Provisional", "PROVISIONAL")).Font(CampusAngular::Display(11))
					.ColorAndOpacity(CampusAngular::Accent)
				]
			];
		}
		if (Live && Page.WeekendEndsAt != FDateTime())
		{
			Flags->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				Countdown(Page.WeekendEndsAt, LOCTEXT("EndsIn", "ENDS "), 12, CampusAngular::TextMuted)
			];
		}
		Nowadays->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)[Flags];
	}
	else
	{
		Nowadays->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
		[
			Label(LOCTEXT("NoWeekend", "NO WEEKEND RUNNING"), 16, CampusAngular::TextMuted)
		];
	}

	Block->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f)[Best]
		+ SHorizontalBox::Slot().AutoWidth().Padding(16.f, 0.f)
		[
			SNew(SBox).WidthOverride(1.f)[SNew(SCampusAngularPanel).Fill(CampusAngular::Hairline)]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[Nowadays]
	];
	return Block;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildProfileHeader()
{
	if (!ProfileData.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	const FCampusProfile& Page = *ProfileData;
	FCampusPlayerCard Card;
	Card.PlayerId = Page.PlayerId;
	Card.Name = Page.Name;
	Card.Color = Page.Color;

	const bool bMax = Page.XpForNext <= 0;
	const float Fraction = bMax ? 1.f : FMath::Clamp(float(Page.XpInto) / float(Page.XpForNext), 0.f, 1.f);
	const int32 Likes = Page.Likes + (OptimisticLikes.Contains(Page.PlayerId) ? 1 : 0);
	// The readable account number is what a player reads out to a friend, so it is what the page
	// shows and what COPY puts on the clipboard. Older servers have no public_id: fall back to the
	// uuid's short form, and copy the whole uuid, exactly as before.
	const bool bHasPublic = !Page.PublicId.IsEmpty();
	FString Shown;
	if (bHasPublic)
	{
		for (int32 Index = 0; Index < Page.PublicId.Len(); ++Index)
		{
			if (Index > 0 && (Page.PublicId.Len() - Index) % 3 == 0)
			{
				Shown.AppendChar(TEXT(' '));
			}
			Shown.AppendChar(Page.PublicId[Index]);
		}
	}
	else
	{
		Shown = Page.ShortId();
	}
	const FString FullId = bHasPublic ? Page.PublicId : Page.PlayerId;

	FString College;
	if (!Page.CollegeName.IsEmpty())
	{
		College = Page.CollegeName.ToUpper();
		if (!Page.Branch.IsEmpty())
		{
			College += TEXT("  ·  ") + Page.Branch.ToUpper();
		}
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 0.f, 18.f, 0.f)
		[
			Avatar(Card, 92.f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Page.Name.ToUpper()))
					.Font(CampusAngular::Display(32)).ColorAndOpacity(FLinearColor::White)
				]
				// Likes are vanity with no gameplay effect, on purpose: the moment a like pays
				// anything it becomes a farming target that needs a whole anti-abuse apparatus.
				// In a plate with its own caption, so a bare number across a 980 px panel is never
				// a mystery - the heart alone was not enough at this distance from the name.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Slanted(10.f))
					.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.72f))
					.Border(CampusAngular::Hairline)
					.BorderWidth(1.f)
					.ContentPadding(FMargin(14.f, 5.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)
							[
								SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Heart).Size(22.f).Color(CampusAngular::Danger)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(Grouped(Likes)).Font(CampusAngular::Numeric(20))
							.ColorAndOpacity(FLinearColor::White)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 2.f, 0.f, 0.f)
						[
							SNew(STextBlock).Text(LOCTEXT("LikesCaption", "LIKES")).Font(CampusAngular::Display(11))
							.ColorAndOpacity(CampusAngular::TextMuted)
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Slanted(10.f))
					.Fill(FLinearColor(0.12f, 0.10f, 0.02f, 0.92f))
					.ContentPadding(FMargin(11.f, 2.f))
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("LevelChip", "LV {0}"), FText::AsNumber(Page.Level)))
						.Font(CampusAngular::Display(18)).ColorAndOpacity(CampusAngular::Accent)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					// The same two-layer sliver the top bar uses: a flat track, an accent fill.
					SNew(SBox).WidthOverride(260.f).HeightOverride(8.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SCampusAngularPanel).Fill(FLinearColor(1.f, 1.f, 1.f, 0.14f))]
						+ SOverlay::Slot().HAlign(HAlign_Left)
						[
							SNew(SBox).WidthOverride(FMath::Max(4.f, 260.f * Fraction))
							[
								SNew(SCampusAngularPanel).Fill(CampusAngular::Accent)
							]
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text(bMax
						? LOCTEXT("MaxLevel", "MAX")
						: FText::Format(LOCTEXT("XpInto", "{0} / {1}"), Grouped(Page.XpInto), Grouped(Page.XpForNext)))
					.Font(CampusAngular::Numeric(13)).ColorAndOpacity(CampusAngular::TextMuted)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("IdLine", "ID  {0}"), FText::FromString(Shown)))
					.Font(CampusAngular::Numeric(16)).ColorAndOpacity(CampusAngular::TextMuted)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					// The short form is what the page shows; the clipboard gets the WHOLE uuid, because
					// the id is for support and abuse reports, not for typing at a friend. A word rather
					// than an icon, because there is no glyph that unambiguously means "copy".
					Button(LOCTEXT("CopyId", "COPY"), FOnClicked::CreateLambda([this, FullId]()
					{
						FPlatformApplicationMisc::ClipboardCopy(*FullId);
						Toast(TEXT("ID COPIED — SEND IT TO A FRIEND."));
						return FReply::Handled();
					}), EButton::Ghost, 13, FVector2D(96.f, 48.f))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
				[
					College.IsEmpty()
						? SNullWidget::NullWidget
						: Label(FText::FromString(College), 15, FLinearColor(1.f, 1.f, 1.f, 0.72f))
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildProfileTab()
{
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[BuildRankBlock()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 14.f)
		[
			SNew(SBox).HeightOverride(1.f)[SNew(SCampusAngularPanel).Fill(CampusAngular::Hairline)]
		]
		+ SVerticalBox::Slot().AutoHeight()[BuildStatsStrip()];

	// The last three matches, from the SAME response - no second request, and it fills what would
	// otherwise be half a screen of nothing between the stats and the action row.
	if (ProfileData.IsValid() && ProfileData->Recent.Num() > 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.f, 18.f, 0.f, 10.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Label(LOCTEXT("RecentMatches", "RECENT MATCHES"), 18, FLinearColor::White)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(LOCTEXT("SeeAll", "SEE ALL"), FOnClicked::CreateLambda([this]()
				{
					ProfileTab = EProfileTab::History;
					Refresh();
					return FReply::Handled();
				}), EButton::Ghost, 14, FVector2D(140.f, 48.f))
			]
		];
		const int32 Shown = FMath::Min(3, ProfileData->Recent.Num());
		for (int32 Index = 0; Index < Shown; ++Index)
		{
			Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[BuildHistoryRow(ProfileData->Recent[Index])];
		}
	}
	return Body;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildMilestone(const FCampusMilestone& Badge)
{
	const FLinearColor Border = Badge.bEarned ? CampusAngular::Accent : CampusAngular::Hairline;
	const FLinearColor Ink = Badge.bEarned ? CampusAngular::Accent : FLinearColor(1.f, 1.f, 1.f, 0.25f);
	const FText Footer = Badge.bEarned
		? (Badge.AchievedAt != FDateTime() ? ShortDate(Badge.AchievedAt) : LOCTEXT("EarnedEarlier", "EARNED EARLIER"))
		: (Badge.Target > 0
			? FText::Format(LOCTEXT("BadgeProgress", "{0} / {1}"), FText::AsNumber(Badge.Progress), FText::AsNumber(Badge.Target))
			: FText::GetEmpty());

	TSharedRef<SOverlay> Plate = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Clipped(14.f))
			.Fill(Badge.bEarned ? FLinearColor(0.07f, 0.06f, 0.02f, 0.82f) : FLinearColor(0.015f, 0.02f, 0.03f, 0.6f))
			.Border(Border)
			.BorderWidth(Badge.bEarned ? 2.f : 1.f)
			.ContentPadding(FMargin(8.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(46.f).HeightOverride(46.f)
					[
						SNew(SCampusGlyph).Glyph(CampusAngular::GlyphFromName(Badge.Glyph)).Size(46.f).Color(Ink)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Badge.Name.ToUpper())).Font(CampusAngular::Display(12))
					.ColorAndOpacity(Badge.bEarned ? FLinearColor::White : CampusAngular::TextMuted)
					.Justification(ETextJustify::Center).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(Footer).Font(CampusAngular::Numeric(11))
					.ColorAndOpacity(CampusAngular::TextMuted)
				]
			]
		];
	if (Badge.bEarned && Badge.Tier >= 3)
	{
		// A tier-3 badge earns a second inset border and nothing else: one extra stroke, not a new colour.
		Plate->AddSlot().Padding(FMargin(5.f))
		[
			SNew(SCampusAngularPanel).Shape(FCampusShape::Clipped(10.f))
			.Fill(FLinearColor::Transparent).Border(CampusAngular::Accent).BorderWidth(1.f)
		];
	}
	return SNew(SBox).WidthOverride(BadgeSize).HeightOverride(BadgeSize)[Plate];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildShowcaseTab()
{
	if (!ProfileData.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	const FCampusProfile& Page = *ProfileData;
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// The badge wall. The owner asked for Free Fire's GALLERY; a real photo gallery needs object
	// storage, transcoding, a moderation queue and a DPDP obligation over user content, so this tab is
	// what we already own and already grant. An empty photo gallery is worse than a full trophy wall.
	Body->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Label(LOCTEXT("Milestones", "MILESTONES"), 20, FLinearColor::White)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
		[
			Label(FText::Format(LOCTEXT("MilestoneCount", "{0} / {1}"),
				FText::AsNumber(Page.MilestonesEarned), FText::AsNumber(Page.MilestonesTotal)), 15, CampusAngular::Accent)
		]
	];
	if (Page.Milestones.Num() == 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			Label(LOCTEXT("NoMilestones", "NO BADGES YET — PLAY A MATCH"), 15, CampusAngular::TextMuted)
		];
	}
	else
	{
		TSharedRef<SWrapBox> Wall = SNew(SWrapBox).UseAllottedSize(true);
		for (const FCampusMilestone& Badge : Page.Milestones)
		{
			Wall->AddSlot().Padding(0.f, 0.f, 10.f, 10.f)[BuildMilestone(Badge)];
		}
		Body->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)[Wall];
	}

	// Titles: owned, with the equipped one marked. Tapping one equips it (my own page only).
	Body->AddSlot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)
	[
		Label(LOCTEXT("Titles", "TITLES"), 20, FLinearColor::White)
	];
	if (Page.Titles.Num() == 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			Label(LOCTEXT("NoTitles", "NO TITLES YET — WIN A WEEKEND"), 15, CampusAngular::TextMuted)
		];
	}
	else
	{
		TSharedRef<SWrapBox> Row = SNew(SWrapBox).UseAllottedSize(true);
		for (const FCampusOwnedItem& Item : Page.Titles)
		{
			const FString ItemId = Item.ItemId;
			const bool bEquipped = Item.bEquipped;
			Row->AddSlot().Padding(0.f, 0.f, 10.f, 10.f)
			[
				SNew(SBox).HeightOverride(56.f).MinDesiredWidth(200.f)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Slanted(12.f))
					.Fill(bEquipped ? FLinearColor(0.10f, 0.09f, 0.02f, 0.92f) : FLinearColor(0.02f, 0.03f, 0.04f, 0.72f))
					.Border(bEquipped ? CampusAngular::Accent : CampusAngular::Hairline)
					.BorderWidth(bEquipped ? 2.f : 1.f)
					.ContentPadding(FMargin(16.f, 4.f))
					.OnClicked(FOnClicked::CreateLambda([this, ItemId, bEquipped]()
					{
						if (!ProfileData.IsValid() || !ProfileData->bMine)
						{
							return FReply::Handled();
						}
						if (UCampusSocial* Live = Social())
						{
							// Tapping the equipped title clears it, which is how a player takes a
							// title OFF without hunting for a separate control.
							Live->EquipTitle(bEquipped ? FString() : ItemId, [this](bool bOk, const FString& Error)
							{
								if (!bOk)
								{
									Toast(Error);
								}
								OpenProfile(FString());
							});
						}
						return FReply::Handled();
					}))
					[
						SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(Item.Name.ToUpper()))
							.Font(CampusAngular::Display(16))
							.ColorAndOpacity(bEquipped ? CampusAngular::Accent : FLinearColor(1.f, 1.f, 1.f, 0.82f))
						]
					]
				]
			];
		}
		Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[Row];
	}

	// The equipped gun card: the same four numbers the Store already shows, so it is a reuse.
	if (Page.bHasGun)
	{
		Body->AddSlot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)
		[
			Label(LOCTEXT("EquippedGun", "EQUIPPED WEAPON"), 20, FLinearColor::White)
		];
		TSharedRef<SWrapBox> Stats = SNew(SWrapBox).UseAllottedSize(true);
		Stats->AddSlot().Padding(0.f, 0.f, 6.f, 6.f)[StatCell(CampusAngular::EGlyph::Skull, FText::AsNumber(FMath::RoundToInt(Page.GunDamage)), LOCTEXT("GunDamage", "DAMAGE"))];
		Stats->AddSlot().Padding(0.f, 0.f, 6.f, 6.f)[StatCell(CampusAngular::EGlyph::Clock, Ratio(Page.GunFireRate), LOCTEXT("GunRate", "FIRE RATE"))];
		Stats->AddSlot().Padding(0.f, 0.f, 6.f, 6.f)[StatCell(CampusAngular::EGlyph::Target, Percent(Page.GunAccuracy), LOCTEXT("GunAcc", "ACCURACY"))];
		Stats->AddSlot().Padding(0.f, 0.f, 6.f, 6.f)[StatCell(CampusAngular::EGlyph::Coin, FText::AsNumber(Page.GunMagazine), LOCTEXT("GunMag", "MAGAZINE"))];
		Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(14.f))
			.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.7f))
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(18.f, 14.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						Label(FText::FromString(Page.GunName.ToUpper()), 24, FLinearColor::White)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Label(FText::Format(LOCTEXT("GunLevel", "LV {0} / {1}"), FText::AsNumber(Page.GunLevel),
							FText::AsNumber(Page.GunMaxLevel)), 15, CampusAngular::Accent)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[Stats]
			]
		];
	}
	return Body;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildHistoryRow(const FCampusHistoryRow& Row)
{
	// A redacted room code is not tappable and MUST NOT look tappable: the code is a join credential,
	// so a stranger's history row carries none.
	const bool bOpenable = !Row.RoomCode.IsEmpty();
	const FLinearColor Edge = Row.bWon ? CampusAngular::Go : CampusAngular::Danger;
	const FString Code = Row.RoomCode;

	// Named Line, not Content: SCampusLobbyWidget already has a `Content` member, and a local
	// that hides it is a warning this project treats as an error (and rightly so).
	TSharedRef<SWidget> Line = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Row.bWon ? LOCTEXT("HistoryWon", "WON") : LOCTEXT("HistoryLost", "LOST"))
					.Font(CampusAngular::Display(18)).ColorAndOpacity(Edge)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(FText::FromString(Row.Map.ToUpper())).Font(CampusAngular::Display(16))
					.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.82f))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					Row.bMvp
						? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
							[
								SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Crown).Size(16.f).Color(CampusAngular::Accent)
							])
						: SNullWidget::NullWidget
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(ShortDate(Row.At)).Font(CampusAngular::Numeric(12))
					.ColorAndOpacity(CampusAngular::TextMuted)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					Row.bRated
						? SNullWidget::NullWidget
						: StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
							.Shape(FCampusShape::Slanted(6.f))
							.Fill(FLinearColor(1.f, 1.f, 1.f, 0.08f))
							.ContentPadding(FMargin(7.f, 1.f))
							[
								SNew(STextBlock).Text(LOCTEXT("UnratedChip", "UNRATED")).Font(CampusAngular::Display(10))
								.ColorAndOpacity(CampusAngular::TextMuted)
							])
				]
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f)
		[
			SNew(SBox).WidthOverride(72.f)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("HistoryKd", "{0}/{1}"), FText::AsNumber(Row.Kills), FText::AsNumber(Row.Deaths)))
				.Font(CampusAngular::Numeric(18)).ColorAndOpacity(FLinearColor::White).Justification(ETextJustify::Right)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f)
		[
			SNew(SBox).WidthOverride(56.f)
			[
				SNew(STextBlock).Text(FText::Format(LOCTEXT("HistoryHs", "{0} HS"), FText::AsNumber(Row.Headshots)))
				.Font(CampusAngular::Numeric(14)).ColorAndOpacity(CampusAngular::TextMuted).Justification(ETextJustify::Right)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(84.f)
			[
				SNew(STextBlock).Text(FText::Format(LOCTEXT("HistoryXp", "+{0} XP"), FText::AsNumber(Row.Xp)))
				.Font(CampusAngular::Numeric(14)).ColorAndOpacity(CampusAngular::Accent).Justification(ETextJustify::Right)
			]
		];

	if (!bOpenable)
	{
		return SNew(SBox).HeightOverride(72.f)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Notched(10.f))
				.Fill(FLinearColor(0.015f, 0.02f, 0.03f, 0.55f))
				.Border(CampusAngular::Hairline)
				.BorderWidth(1.f)
				.AccentEdge(Edge)
				.AccentEdgeWidth(4.f)
				.ContentPadding(FMargin(14.f, 6.f))
				[
					Line
				]
			];
	}
	return SNew(SBox).HeightOverride(72.f)
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Notched(10.f))
			.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.7f))
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.AccentEdge(Edge)
			.AccentEdgeWidth(4.f)
			.ContentPadding(FMargin(14.f, 6.f))
			.OnClicked(FOnClicked::CreateLambda([this, Code]()
			{
				OpenSummary(Code);
				return FReply::Handled();
			}))
			[
				Line
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildHistoryTab()
{
	if (!ProfileData.IsValid())
	{
		return SNullWidget::NullWidget;
	}
	const FCampusProfile& Page = *ProfileData;
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	if (Page.Recent.Num() == 0)
	{
		Body->AddSlot().AutoHeight()
		[
			Label(LOCTEXT("NoHistory", "NO MATCHES YET"), 17, CampusAngular::TextMuted)
		];
		return Body;
	}
	for (const FCampusHistoryRow& Row : Page.Recent)
	{
		Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[BuildHistoryRow(Row)];
	}
	if (Page.NextBefore != FDateTime())
	{
		// LOAD MORE, never infinite scroll: a scroll-triggered fetch on a metered tunnel is how you
		// discover your quota is gone.
		const FDateTime Before = Page.NextBefore;
		const FString Who = ProfileTarget;
		Body->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SBox).IsEnabled(!bHistoryLoading)
			[
				Button(bHistoryLoading ? LOCTEXT("Loading", "LOADING...") : LOCTEXT("LoadMore", "LOAD MORE"),
					FOnClicked::CreateLambda([this, Before, Who]()
					{
						UCampusSocial* Live = Social();
						if (!Live || bHistoryLoading)
						{
							return FReply::Handled();
						}
						bHistoryLoading = true;
						Refresh();
						Live->LoadHistory(Who, Before, [this](bool bOk, const FString& Error, const FCampusProfile& Page2)
						{
							bHistoryLoading = false;
							if (bOk && ProfileData.IsValid())
							{
								ProfileData->Recent.Append(Page2.Recent);
								ProfileData->NextBefore = Page2.NextBefore;
							}
							else if (!bOk)
							{
								Toast(Error);
							}
							Refresh();
						});
						return FReply::Handled();
					}), EButton::Dark, 17, FVector2D(240.f, 56.f))
			]
		];
	}
	return Body;
}

// ---------------------------------------------------------------------------------------------
// The page

void SCampusLobbyWidget::CloseProfile()
{
	ProfileData.Reset();
	ProfileTarget.Reset();
	if (Controller.IsValid())
	{
		Controller->FocusProfile(false);
	}
	Show(EScreen::Lobby);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildProfile()
{
	// The camera moves and the rest of the line-up hides, so the figure stands at about 25 % across
	// the screen with the panel beside it. Nothing in the lobby's own geometry is touched.
	if (Controller.IsValid())
	{
		Controller->FocusProfile(true);
	}

	const bool bMine = !ProfileData.IsValid() || ProfileData->bMine;

	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	struct FTabDef { EProfileTab Tab; FText Label; };
	const FTabDef Defs[] = {
		{ EProfileTab::Showcase, LOCTEXT("TabShowcase", "SHOWCASE") },
		{ EProfileTab::Profile, LOCTEXT("TabProfile", "PROFILE") },
		{ EProfileTab::History, LOCTEXT("TabHistory", "HISTORY") },
	};
	for (const FTabDef& Def : Defs)
	{
		const EProfileTab Which = Def.Tab;
		const bool bSelected = ProfileTab == Which;
		Tabs->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SBox).WidthOverride(190.f).HeightOverride(60.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Tab(18.f, 8.f))
				.Fill(bSelected ? FLinearColor(0.09f, 0.08f, 0.03f, 0.95f) : FLinearColor(0.02f, 0.03f, 0.04f, 0.8f))
				.Border(bSelected ? CampusAngular::Accent : CampusAngular::Hairline)
				.BorderWidth(bSelected ? 2.f : 1.f)
				.AccentEdge(bSelected ? CampusAngular::Accent : FLinearColor::Transparent)
				.AccentEdgeWidth(bSelected ? 5.f : 0.f)
				.ContentPadding(FMargin(14.f, 4.f))
				.OnClicked(FOnClicked::CreateLambda([this, Which]()
				{
					// Switching tabs makes NO request: the one GET already paints every tab.
					ProfileTab = Which;
					Refresh();
					return FReply::Handled();
				}))
				[
					SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Def.Label).Font(CampusAngular::Display(18))
						.ColorAndOpacity(bSelected ? CampusAngular::Accent : FLinearColor(1.f, 1.f, 1.f, 0.72f))
					]
				]
			]
		];
	}

	TSharedRef<SWidget> TabBody = SNullWidget::NullWidget;
	if (!ProfileData.IsValid())
	{
		// Loading and error both get a sentence. A blank screen on first load is a bug.
		TabBody = SNew(SBox).HeightOverride(220.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				Label(bProfileLoading ? LOCTEXT("ProfileLoading", "LOADING PROFILE...")
					: LOCTEXT("ProfileFailed", "COULD NOT LOAD THIS PROFILE"), 20, CampusAngular::TextMuted)
			];
	}
	else
	{
		switch (ProfileTab)
		{
		case EProfileTab::Showcase: TabBody = BuildShowcaseTab(); break;
		case EProfileTab::History: TabBody = BuildHistoryTab(); break;
		default: TabBody = BuildProfileTab(); break;
		}
	}

	// The action row: only for somebody else's page, as the design says.
	TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
	if (ProfileData.IsValid() && !bMine)
	{
		FCampusPlayerCard Card;
		Card.PlayerId = ProfileData->PlayerId;
		Card.Name = ProfileData->Name;
		Card.Color = ProfileData->Color;
		Card.FriendState = ProfileData->FriendState;
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)[FriendButton(Card, FVector2D(200.f, 52.f))];

		const bool bLiked = !ProfileData->bCanLike || OptimisticLikes.Contains(ProfileData->PlayerId);
		const FString Who = ProfileData->PlayerId;
		const FString Name = ProfileData->Name;
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(SBox).IsEnabled(!bLiked)
			[
				Button(bLiked ? LOCTEXT("Liked", "LIKED") : LOCTEXT("Like", "LIKE"),
					FOnClicked::CreateLambda([this, Who, Name]()
					{
						UCampusSocial* Live = Social();
						if (!Live)
						{
							return FReply::Handled();
						}
						// Optimistic: the count ticks up at once and reverts with a toast that names
						// the reason (one like per player per day).
						OptimisticLikes.Add(Who);
						Refresh();
						Live->LikePlayer(Who, [this, Who, Name](bool bOk, const FString& Error)
						{
							if (!bOk)
							{
								OptimisticLikes.Remove(Who);
								Toast(Error.IsEmpty()
									? FString::Printf(TEXT("YOU ALREADY LIKED %s TODAY."), *Name.ToUpper())
									: Error);
							}
							Refresh();
						});
						return FReply::Handled();
					}), bLiked ? EButton::Ghost : EButton::Dark, 17, FVector2D(150.f, 52.f))
			]
		];
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
		[
			Button(LOCTEXT("Block", "BLOCK"), FOnClicked::CreateLambda([this, Who, Name]()
			{
				AskConfirm(LOCTEXT("BlockTitle", "BLOCK PLAYER"),
					FText::Format(LOCTEXT("BlockBody", "BLOCK {0}? THEY CANNOT INVITE YOU OR SEE YOU AGAIN."),
						FText::FromString(Name.ToUpper())),
					LOCTEXT("BlockYes", "BLOCK"),
					[this, Who]()
					{
						if (UCampusSocial* Live = Social())
						{
							Live->BlockPlayer(Who, [this](bool bOk, const FString& Error)
							{
								if (!bOk)
								{
									Toast(Error);
								}
								CloseProfile();
							});
						}
					});
				return FReply::Handled();
			}), EButton::Ghost, 17, FVector2D(150.f, 52.f))
		];
	}
	else if (ProfileData.IsValid())
	{
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
		[
			Button(LOCTEXT("EditProfileBtn", "EDIT"), FOnClicked::CreateLambda([this]()
			{
				Show(EScreen::Account);
				return FReply::Handled();
			}), EButton::Dark, 17, FVector2D(150.f, 52.f))
		];
	}
	if (ProfileData.IsValid())
	{
		// SHARE is a clipboard text card. UE ships no share intent, and a URL would be a dead link
		// until there is a public web origin - which is worse than no link.
		Actions->AddSlot().AutoWidth()
		[
			Button(LOCTEXT("Share", "SHARE"), FOnClicked::CreateLambda([this]()
			{
				if (ProfileData.IsValid())
				{
					FPlatformApplicationMisc::ClipboardCopy(*ProfileData->ShareText());
					Toast(TEXT("COPIED — PASTE IT ANYWHERE."));
				}
				return FReply::Handled();
			}), EButton::Ghost, 17, FVector2D(150.f, 52.f))
		];
	}

	TSharedRef<SWidget> Panel = SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Notched(20.f))
		// InkStrong is opaque, which is exactly why it exists: this panel must stay readable over the
		// 3D scene showing through on the left.
		.Fill(CampusAngular::InkStrong)
		.Border(CampusAngular::Hairline)
		.BorderWidth(1.f)
		.ContentPadding(FMargin(26.f, 20.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Tabs]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 14.f)[BuildProfileHeader()]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
			[
				SNew(SBox).HeightOverride(1.f)[SNew(SCampusAngularPanel).Fill(CampusAngular::Hairline)]
			]
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SScrollBox) + SScrollBox::Slot()[TabBody]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)[Actions]
		];

	// No backdrop: this page is full-screen rather than a Dialog(), because the left half has to show
	// the 3D scene. A 0.6 scrim over it would defeat the whole point of putting the figure there.
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(24.f, 16.f))
			[
				SNew(SHorizontalBox)
				// The left 45 %: the 3D figure, drag to rotate, exactly as the lobby does it.
				+ SHorizontalBox::Slot().FillWidth(SceneShare)
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
						Label(LOCTEXT("ProfileDrag", "DRAG TO TURN"), 13, FLinearColor(1.f, 1.f, 1.f, 0.35f))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(PanelShare)
				[
					SNew(SVerticalBox)
					// The close X sits OUTSIDE the panel, so it can never be scrolled away.
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 8.f)
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close, FOnClicked::CreateLambda([this]()
						{
							CloseProfile();
							return FReply::Handled();
						}), 56.f)
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SBox).MaxDesiredWidth(PanelMaxWidth)[Panel]
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
