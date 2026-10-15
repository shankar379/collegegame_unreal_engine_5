// REPORTING A PLAYER, as the player sees it (PHASE_B_CONTRACT B3).
//
//   THE BUTTON   a square "!" on a player card. It is on the PROFILE PAGE, which is what the party
//                plates, the friends rows, the ONLINE strip and the invite tray all tap through to,
//                and on the POST-MATCH SCOREBOARD, which is the one player list with no profile tap
//                of its own. Never on my own row.
//   THE SHEET    the five reasons a moderator can actually act on, one tap each, floating over
//                whatever screen asked for it (ShowFloating, so it works over the profile page and
//                over the scoreboard alike).
//
// ONE TAP, ONE ROW, NO VERDICT. The server writes one row and tells us it was received; nothing is
// counted on the device and nothing is hidden from anybody. So the only thing this screen has to get
// right is what it says afterwards:
//
//   sent          "THANKS - OUR TEAM WILL LOOK AT IT." and the button reads SENT for the session.
//   already sent  the server's 409 is NOT a failure the player caused: they reported the same person
//                 this morning, and the sentence says so. The button still goes to SENT, because it
//                 is the truth from the player's side.
//   rate limited  "wait a minute", never "error".
//
// NO FREE TEXT. The five reasons are a vocabulary a moderation queue can be filtered and counted by,
// which is worth more than a thousand unique sentences nobody reads - the server's own reasoning in
// api/src/routes/reports.js, and the reason there is no text box here to abuse either.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusPlayerControllers.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusReportUI"

using namespace CampusStyle;

namespace
{
	/** The five reasons, in the order a player is most likely to want them. The WIRE VALUES are the
	 *  server's enum (cheating | abuse | name | afk | other) and are never localised. */
	struct FReportReason
	{
		const TCHAR* Wire;
		const TCHAR* Label;
		const TCHAR* Hint;
	};
	const FReportReason ReportReasons[] = {
		{ TEXT("cheating"), TEXT("CHEATING"),       TEXT("Aimbot, shooting through walls, impossible movement.") },
		{ TEXT("abuse"),    TEXT("ABUSIVE"),        TEXT("Insults, threats or hate in chat or voice.") },
		{ TEXT("name"),     TEXT("OFFENSIVE NAME"), TEXT("The name itself breaks the rules.") },
		{ TEXT("afk"),      TEXT("NOT PLAYING"),    TEXT("Stood still all match, or left their squad to lose.") },
		{ TEXT("other"),    TEXT("SOMETHING ELSE"), TEXT("Our team will read the match and decide.") },
	};
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildReportButton(const FString& PlayerId, const FString& PlayerName,
	const FString& RoomCode, float Size)
{
	const UCampusBackend* Api = Backend();
	if (PlayerId.IsEmpty() || (Api && PlayerId == Api->PlayerId))
	{
		// Nobody reports themselves, and a row with no player id behind it (an erased account on an
		// old scoreboard) has nothing to report. Zero footprint, not a dead button.
		return SNullWidget::NullWidget;
	}
	const bool bSent = ReportedPlayers.Contains(PlayerId);
	const FLinearColor Ink = bSent ? CampusAngular::TextMuted : CampusAngular::Danger;

	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Clipped(Size * 0.2f))
			.Fill(bSent ? FLinearColor(1.f, 1.f, 1.f, 0.06f) : FLinearColor(0.14f, 0.03f, 0.03f, 0.72f))
			.Border(Ink.CopyWithNewOpacity(bSent ? 0.4f : 0.9f))
			.BorderWidth(1.f)
			.ContentPadding(FMargin(0.f))
			// SCampusAngularButton is PreciseTap, so a swipe that starts on this square scrolls the
			// scoreboard it sits in and only a clean tap opens the sheet.
			.OnClicked(FOnClicked::CreateLambda([this, PlayerId, PlayerName, RoomCode, bSent]()
			{
				if (bSent)
				{
					Toast(TEXT("ALREADY REPORTED. OUR TEAM WILL LOOK AT IT."));
					return FReply::Handled();
				}
				OpenReportPlayer(PlayerId, PlayerName, RoomCode);
				return FReply::Handled();
			}))
			[
				SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					// A MARK, NOT A COLOUR: "!" before the report and a tick after it, so the state
					// reads for a player who cannot tell the red plate from the grey one.
					bSent
						? StaticCastSharedRef<SWidget>(SNew(SCampusGlyph)
							.Glyph(CampusAngular::EGlyph::Tick).Size(Size * 0.42f).Color(Ink))
						: StaticCastSharedRef<SWidget>(SNew(STextBlock)
							.Text(LOCTEXT("ReportMark", "!"))
							.Font(CampusAngular::Display(FMath::RoundToInt(Size * 0.56f)))
							.ColorAndOpacity(Ink))
				]
			]
		];
}

void SCampusLobbyWidget::OpenReportPlayer(const FString& PlayerId, const FString& PlayerName, const FString& RoomCode)
{
	if (PlayerId.IsEmpty())
	{
		return;
	}
	const FString Who = PlayerName.IsEmpty() ? FString(TEXT("THIS PLAYER")) : PlayerName.ToUpper();

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			Label(FText::Format(LOCTEXT("ReportTitle", "REPORT {0}"), FText::FromString(Who)), 28, FLinearColor::White)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ReportBody", "Pick what happened. Our team reads the report with the match it came from - nothing happens to them automatically, and they are never told who reported them."))
			.Font(CampusAngular::Body(16)).ColorAndOpacity(CampusAngular::TextMuted).AutoWrapText(true)
		];

	for (const FReportReason& Reason : ReportReasons)
	{
		const FString Wire(Reason.Wire);
		Body->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			SNew(SBox).HeightOverride(62.f).IsEnabled(!bReportInFlight)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Notched(12.f))
				.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.72f))
				.Border(CampusAngular::Hairline)
				.BorderWidth(1.f)
				.AccentEdge(CampusAngular::Danger)
				.AccentEdgeWidth(4.f)
				.ContentPadding(FMargin(18.f, 6.f))
				.OnClicked(FOnClicked::CreateLambda([this, PlayerId, PlayerName, Wire, RoomCode]()
				{
					SendPlayerReport(PlayerId, PlayerName, Wire, RoomCode);
					return FReply::Handled();
				}))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Reason.Label))
						.Font(CampusAngular::Display(20)).ColorAndOpacity(FLinearColor::White)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Reason.Hint))
						.Font(CampusAngular::Body(13)).ColorAndOpacity(CampusAngular::TextMuted)
					]
				]
			]
		];
	}

	Body->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 16.f, 0.f, 0.f)
	[
		Button(LOCTEXT("ReportCancel", "CANCEL"), FOnClicked::CreateLambda([this]()
		{
			CloseConfirm();
			return FReply::Handled();
		}), EButton::Ghost, 17, FVector2D(180.f, 54.f))
	];

	ShowFloating(Body, 720.f);
}

void SCampusLobbyWidget::SendPlayerReport(const FString& PlayerId, const FString& PlayerName,
	const FString& Reason, const FString& RoomCode)
{
	UCampusBackend* Api = Backend();
	if (!Api || bReportInFlight || PlayerId.IsEmpty())
	{
		return;
	}
	// The sheet closes on the tap: the request is one row on the server and the answer is a toast, so
	// holding a modal open over it would be a wait the player has no reason to sit through.
	CloseConfirm();
	bReportInFlight = true;
	Busy(TEXT("Sending report..."));
	const FString Who = PlayerName.IsEmpty() ? FString(TEXT("THAT PLAYER")) : PlayerName.ToUpper();
	// WEAK, AND NOT OPTIONAL: UCampusBackend outlives this widget and holds the callback until the
	// request finishes, so a raw `this` is a use-after-free the moment the lobby is torn down under it.
	TWeakPtr<SWidget> Weak = AsWeak();
	Api->ReportPlayer(PlayerId, Reason, RoomCode,
		[this, Weak, PlayerId, Who](bool bOk, bool bAlreadySent, const FString& Message)
	{
		if (!Weak.IsValid())
		{
			return;
		}
		bReportInFlight = false;
		bBusy = false;
		if (bOk)
		{
			// SESSION-LOCAL, because the server's rule is one report per target per IST game day and
			// this is only the local echo of it: the next launch asks the server again rather than
			// trusting a flag in a save file. A 409 lands here too - the row exists either way, so
			// the button is right to read SENT and only the sentence differs.
			ReportedPlayers.Add(PlayerId);
			Toast(bAlreadySent ? Message : FString::Printf(TEXT("%s REPORTED. OUR TEAM WILL LOOK AT IT."), *Who));
		}
		else
		{
			Toast(Message);
		}
		Refresh();
	});
}

#undef LOCTEXT_NAMESPACE
