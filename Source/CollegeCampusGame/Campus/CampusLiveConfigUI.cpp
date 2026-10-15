// What the admin console changes without a release, as the player sees it (ADMIN_CONSOLE_DESIGN
// "Game integration" 1 and 3). Three surfaces, one source: UCampusBackend's live-config blob.
//
//   THE GATE        a full-screen plate above every lobby screen. UPDATE REQUIRED when this build is
//                   below the published floor - a dead end by design, with the store link when the
//                   console has published one - otherwise the MAINTENANCE lock, with the server's own
//                   sentence and a RETRY that re-reads /v1/live-config. It lives in the LOBBY widget
//                   and nowhere else, which is what stops it ever closing over a live match.
//   THE CARDS       announcements under the invite stack, in the same column, at the same width, so
//                   nothing overlaps and the corner reads as one surface. NOTICE and MAINTENANCE wear
//                   the accent and the danger edge; NEWS wears the gem edge. All dismissible, and a
//                   dismissal is remembered in the save by id.
//
// NOTHING HERE IS REBUILT TO CHANGE STATE. The gate is built once in the lobby's root overlay and
// driven entirely by attributes, so it costs nothing while it is collapsed; the card column is
// re-filled only from the lobby's Tick, never from inside the tap or the network callback that asked
// for it - the same rule Refresh() exists for.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusPlayerControllers.h"
#include "HAL/PlatformProcess.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusLiveConfigUI"

using namespace CampusStyle;

namespace
{
	/** The gate's backdrop: all but opaque, because what is behind it is not usable. */
	const FLinearColor GateWash(0.012f, 0.016f, 0.024f, 0.97f);
	/** Width of the gate plate. Wide enough for a two-line server message at 18 pt. */
	constexpr float GateWidth = 820.f;
	/** At most three cards. A fourth is a wall of text in the corner, not information. */
	constexpr int32 MaxAnnouncementCards = 3;

	/**
	 * A text attribute that re-formats only when its source string actually moves. Slate evaluates an
	 * attribute on EVERY paint, and the gate is the one screen in this game that can stay up for
	 * minutes, so the message must not build an FText sixty times a second for ever.
	 */
	TAttribute<FText> LiveText(TFunction<FString()> Source)
	{
		struct FState
		{
			FString Last;
			FText Cached;
			bool bPrimed = false;
		};
		const TSharedRef<FState> State = MakeShared<FState>();
		return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([State, Source]()
		{
			FString Now = Source();
			if (!State->bPrimed || Now != State->Last)
			{
				State->bPrimed = true;
				State->Last = MoveTemp(Now);
				State->Cached = FText::FromString(State->Last);
			}
			return State->Cached;
		}));
	}

	/** The small glyph + word chip the gate and the cards both wear. The colour is an ATTRIBUTE: the
	 *  gate's chip turns from accent to danger the moment the state does, without being rebuilt. */
	TSharedRef<SWidget> KindChip(CampusAngular::EGlyph Glyph, TAttribute<FText> Label, TAttribute<FLinearColor> Color)
	{
		// STextBlock wants an FSlateColor, which is a DIFFERENT attribute type: a plain FLinearColor
		// converts, a TAttribute<FLinearColor> cannot, so it is forwarded through one getter.
		TAttribute<FSlateColor> TextColor = TAttribute<FSlateColor>::Create(
			TAttribute<FSlateColor>::FGetter::CreateLambda([Color]()
			{
				return FSlateColor(Color.Get(CampusAngular::Accent));
			}));
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(8.f))
			.Fill(FLinearColor(0.f, 0.f, 0.f, 0.55f))
			.Border(Color)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(12.f, 5.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SCampusGlyph).Glyph(Glyph).Size(16.f).Color(Color)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Label).Font(CampusAngular::Display(15)).ColorAndOpacity(TextColor)
				]
			];
	}
}

// -------------------------------------------------------------------------------------------------
// The gate: UPDATE REQUIRED, then MAINTENANCE

TSharedRef<SWidget> SCampusLobbyWidget::BuildLiveGate()
{
	// Read once per paint, in one place, so every lambda below agrees about which state is showing.
	// UPDATE wins: a build the server has retired cannot be helped by waiting for a window to close.
	auto Update = [this]()
	{
		const UCampusBackend* Api = Backend();
		return Api && Api->IsUpdateRequired();
	};
	auto Locked = [this, Update]()
	{
		const UCampusBackend* Api = Backend();
		return !Update() && Api && Api->IsMaintenanceOn();
	};
	auto Edge = [Update]()
	{
		return Update() ? CampusAngular::Danger : CampusAngular::Accent;
	};

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(GateWash)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		// Visible = it also swallows every tap beneath it, which is the whole point of a lock.
		.Visibility_Lambda([Update, Locked]()
		{
			return Update() || Locked() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SSafeZone).Padding(FMargin(24.f, 16.f))
			[
				SNew(SBox).WidthOverride(GateWidth)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Notched(22.f))
					.Fill(CampusAngular::InkStrong)
					.Border_Lambda(Edge)
					.BorderWidth(2.f)
					.AccentEdge_Lambda(Edge)
					.AccentEdgeWidth(6.f)
					.ContentPadding(FMargin(44.f, 34.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
						[
							KindChip(CampusAngular::EGlyph::Lock,
								LiveText([Update]()
								{
									return Update() ? FString(TEXT("VERSION")) : FString(TEXT("SERVER"));
								}),
								TAttribute<FLinearColor>::Create(TAttribute<FLinearColor>::FGetter::CreateLambda(Edge)))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LiveText([Update]()
							{
								return Update() ? FString(TEXT("UPDATE REQUIRED")) : FString(TEXT("UNDER MAINTENANCE"));
							}))
							.Font(CampusAngular::Display(44))
							.ColorAndOpacity(FLinearColor::White)
						]
						// The SERVER's sentence, never a guess of ours: the console writes it, the
						// owner edits it, and this is the one place the player reads it.
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LiveText([this, Update]()
							{
								const UCampusBackend* Api = Backend();
								if (!Api)
								{
									return FString(TEXT("Campus Combat is not available right now."));
								}
								return Update()
									? FString(TEXT("This version of Campus Combat is no longer supported. Install the latest version to keep playing."))
									: Api->MaintenanceText();
							}))
							.Font(CampusAngular::Body(19))
							.ColorAndOpacity(CampusAngular::TextMuted)
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 26.f, 0.f, 0.f)
						[
							SNew(SHorizontalBox)
							// RETRY re-reads /v1/live-config, which is the ONLY route the 503 does not
							// cover - so the lock can always be asked whether it is over. Forced, or
							// the ten-minute throttle would make the button do nothing.
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SBox).WidthOverride(240.f).HeightOverride(64.f)
								.Visibility_Lambda([Update]() { return Update() ? EVisibility::Collapsed : EVisibility::Visible; })
								[
									SNew(SCampusAngularButton)
									.Shape(FCampusShape::Wedge(16.f, 12.f))
									.Fill(CampusAngular::Accent)
									.Border(FLinearColor(1.f, 1.f, 1.f, 0.28f))
									.BorderWidth(1.f)
									.ContentPadding(FMargin(14.f, 4.f))
									.OnClicked(FOnClicked::CreateLambda([this]()
									{
										if (UCampusBackend* Api = Backend())
										{
											Api->FetchLiveConfig(true);
										}
										return FReply::Handled();
									}))
									[
										SNew(STextBlock)
										.Text(LiveText([this]()
										{
											const UCampusBackend* Api = Backend();
											return Api && Api->IsLiveConfigFetching() ? FString(TEXT("CHECKING...")) : FString(TEXT("RETRY"));
										}))
										.Font(CampusAngular::Display(22))
										.ColorAndOpacity(FLinearColor(0.05f, 0.04f, 0.f, 1.f))
									]
								]
							]
							// The store / APK link, only when the console has published one. Without
							// it the screen is text alone, which is still the honest answer.
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SBox).WidthOverride(300.f).HeightOverride(64.f)
								.Visibility_Lambda([this, Update]()
								{
									const UCampusBackend* Api = Backend();
									return Update() && Api && !Api->UpdateUrl().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
								})
								[
									SNew(SCampusAngularButton)
									.Shape(FCampusShape::Wedge(16.f, 12.f))
									.Fill(CampusAngular::Accent)
									.Border(FLinearColor(1.f, 1.f, 1.f, 0.28f))
									.BorderWidth(1.f)
									.ContentPadding(FMargin(14.f, 4.f))
									.OnClicked(FOnClicked::CreateLambda([this]()
									{
										const UCampusBackend* Api = Backend();
										if (Api && !Api->UpdateUrl().IsEmpty())
										{
											FPlatformProcess::LaunchURL(*Api->UpdateUrl(), nullptr, nullptr);
										}
										return FReply::Handled();
									}))
									[
										SNew(STextBlock).Text(LOCTEXT("GetUpdate", "GET THE UPDATE"))
										.Font(CampusAngular::Display(22))
										.ColorAndOpacity(FLinearColor(0.05f, 0.04f, 0.f, 1.f))
									]
								]
							]
							+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
							// The build the player is actually running, so a support message can name
							// it. Numeric, not italic: it is a number in a table.
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
							[
								SNew(STextBlock)
								.Text(LiveText([]()
								{
									return FString::Printf(TEXT("BUILD %d"), UCampusBackend::ClientBuild());
								}))
								.Font(CampusAngular::Numeric(15))
								.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.38f))
							]
						]
					]
				]
			]
		];
}

// -------------------------------------------------------------------------------------------------
// The announcement cards

void SCampusLobbyWidget::RefreshLiveConfig()
{
	// Paint from the blob cached in the save FIRST, so the corner is right on the frame the lobby
	// opens, then ask. The ask is throttled inside the subsystem: once per app run, and again only
	// after a resume older than ten minutes.
	RefreshAnnouncements();
	if (UCampusBackend* Api = Backend())
	{
		Api->FetchLiveConfig(false);
	}
}

void SCampusLobbyWidget::RefreshAnnouncements()
{
	if (!AnnounceBox.IsValid())
	{
		return;
	}
	const UCampusBackend* Api = Backend();
	const TArray<FCampusAnnouncement> Rows = Api ? Api->ActiveAnnouncements() : TArray<FCampusAnnouncement>();
	if (Rows.Num() == 0)
	{
		AnnounceBox->SetContent(SNullWidget::NullWidget);
		return;
	}
	const TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	for (int32 Index = 0; Index < Rows.Num() && Index < MaxAnnouncementCards; ++Index)
	{
		Column->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, SCampusNoticeStack::CardGap)
		[
			BuildAnnouncementCard(Rows[Index])
		];
	}
	AnnounceBox->SetContent(Column);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildAnnouncementCard(const FCampusAnnouncement& Row)
{
	// There is no news LIST in this build, so 'news' is a card too - the design's stated fallback.
	// The kind only changes the edge and the one word on the chip; a kind this build has not heard
	// of still shows, as a plain notice, because the words in it are the point.
	const bool bMaintenance = Row.Kind == TEXT("maintenance");
	const bool bNews = Row.Kind == TEXT("news");
	const FLinearColor Edge = bMaintenance ? CampusAngular::Danger : (bNews ? CampusAngular::Gem : CampusAngular::Accent);
	const FText Kind = bMaintenance
		? LOCTEXT("KindMaintenance", "MAINTENANCE")
		: (bNews ? LOCTEXT("KindNews", "NEWS") : LOCTEXT("KindNotice", "NOTICE"));
	// Every string is built ONCE, here: these cards sit on the lobby for as long as the player does.
	const FText Title = FText::FromString(Row.Title.ToUpper());
	const FText Body = FText::FromString(Row.Body);
	const FString CardId = Row.Id;

	return SNew(SBox).WidthOverride(SCampusNoticeStack::CardWidth)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(CampusAngular::InkStrong)
			.Border(Edge.CopyWithNewOpacity(0.75f))
			.BorderWidth(2.f)
			.AccentEdge(Edge)
			.AccentEdgeWidth(5.f)
			.ContentPadding(FMargin(20.f, 14.f, 14.f, 16.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
					[
						KindChip(bMaintenance ? CampusAngular::EGlyph::Lock : CampusAngular::EGlyph::Mail, Kind, Edge)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(Title).Font(CampusAngular::Display(21))
						.ColorAndOpacity(FLinearColor::White).AutoWrapText(true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
					[
						SNew(STextBlock).Text(Body).Font(CampusAngular::Body(15))
						.ColorAndOpacity(CampusAngular::TextMuted).AutoWrapText(true)
						// Long copy belongs in the console's own screen, not over the lobby.
						.Visibility(Row.Body.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
				]
				// X, top-right, 44 square so a thumb clears the touch floor. The id is remembered in
				// the save, so a dismissed card never comes back on this phone.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(10.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(44.f).HeightOverride(44.f)
					[
						SNew(SCampusAngularButton)
						.Shape(FCampusShape::Notched(10.f))
						.Fill(FLinearColor(1.f, 1.f, 1.f, 0.08f))
						.Border(CampusAngular::Hairline)
						.BorderWidth(1.f)
						.OnClicked(FOnClicked::CreateLambda([this, CardId]()
						{
							if (UCampusBackend* Api = Backend())
							{
								Api->DismissAnnouncement(CardId);
							}
							// NEVER from inside the tap: re-filling the column here would tear down
							// the very button still routing this touch. The lobby's Tick serves it.
							bAnnouncementsDirty = true;
							return FReply::Handled();
						}))
						[
							SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Close).Size(18.f)
							.Color(FLinearColor(1.f, 1.f, 1.f, 0.7f))
						]
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
