// BATTLE ROYALE matchmaking overlay (Slate, lobby level). Free Fire's queue screen:
//
//        MATCHING...                      the one line that says what is happening
//        VARSITY VALLEY  ·  SQUAD  ·  NIGHT
//           0:14                          counting UP, because nobody can promise a down-count
//        PLAYERS FOUND  7 / 50            honest: it is what the server has told us
//        [          CANCEL          ]     one big target, nothing else to mis-tap
//
// It is a SCREEN and not a dialog: a queue the player can tap behind is a queue they will leave by
// accident. START is gone while this is up, and CANCEL is the only way out.
//
// Everything on it is a text lambda off UCampusBRQueue, and the queue broadcasts once a second - so
// the overlay is built once per state change, not once per frame.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBR.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusSocial.h"
#include "Engine/GameInstance.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusBRUI"

using namespace CampusStyle;

namespace
{
	/** Unity-safe local names (CampusMapSelect.cpp has its own SelectTitle / SelectShadow). */
	const FLinearColor BrUiShadow(0.f, 0.f, 0.f, 0.85f);

	/** "0:14" - minutes and seconds, so a long queue does not read as a four-digit number. */
	FString BrClock(float Seconds)
	{
		const int32 Whole = FMath::Max(0, int32(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
	}

	TSharedRef<SWidget> BrLine(TAttribute<FText> Text, int32 Size, const FLinearColor& Color, bool bBold = true)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(bBold ? CampusAngular::Display(Size) : CampusAngular::Body(Size))
			.ColorAndOpacity(Color)
			.ShadowOffset(FVector2D(2.f, 2.f))
			.ShadowColorAndOpacity(BrUiShadow)
			.Justification(ETextJustify::Center);
	}
}

// -------------------------------------------------------------------------------------------------
// Plumbing

UCampusBRQueue* SCampusLobbyWidget::BRQueue() const
{
	if (!Controller.IsValid() || !Controller->GetGameInstance())
	{
		return nullptr;
	}
	return Controller->GetGameInstance()->GetSubsystem<UCampusBRQueue>();
}

bool SCampusLobbyWidget::IsBattleRoyaleSelected() const
{
	return FName(CampusCatalog::Mode(SelectedMode).Id) == FName(TEXT("BattleRoyale"));
}

// -------------------------------------------------------------------------------------------------
// Join / cancel / enter

void SCampusLobbyWidget::StartBattleRoyale()
{
	UCampusBRQueue* Queue = BRQueue();
	if (!Queue)
	{
		Toast(TEXT("Matchmaking is not available in this build."));
		return;
	}
	const UCampusBackend* Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		Toast(TEXT("Sign in to play Battle Royale."));
		return;
	}
	// ONE TICKET IS ONE TEAM, and it is the LEADER's: a member who reaches this (a stale button, a
	// leader change mid-tap) is told whose press it is instead of cutting a second ticket.
	const UCampusSocial* Live = Social();
	if (Live && Live->HasParty() && !Live->IsPartyLeader())
	{
		Toast(FString::Printf(TEXT("%s STARTS FOR THE SQUAD."), *Live->GetServerParty().LeaderName().ToUpper()));
		return;
	}
	// The squad decides the queue: with a server party it is the party's mode, exactly as HostRoom
	// does for a Custom Room, so four players never queue as four solos.
	const bool bPartyLeader = Live && Live->HasParty() && Live->IsPartyLeader();
	const int32 Team = bPartyLeader ? Live->GetServerParty().Mode : SelectedTeamSize;
	const UCampusSave* Save = Profile();
	const bool bNight = Save && Save->bBrNight;

	// Show the overlay FIRST: the request may take a second on a tunnelled API, and a START that
	// looks dead for a second is a START the player taps twice.
	Show(EScreen::Matchmaking);
	Queue->Join(Team, bNight, [this](bool bOk, const FString& Error)
	{
		if (!bOk)
		{
			Toast(Error.IsEmpty() ? TEXT("Could not join the queue.") : Error);
			Show(EScreen::Lobby);
			return;
		}
		Refresh();
	});
}

void SCampusLobbyWidget::CancelMatchmaking()
{
	if (UCampusBRQueue* Queue = BRQueue())
	{
		Queue->Leave(nullptr);
	}
	Show(EScreen::Lobby);
}

void SCampusLobbyWidget::EnterBRMatch(const FCampusBRMatch& Match)
{
	if (!Controller.IsValid() || !Match.IsValid())
	{
		return;
	}
	// Never teleport out of the lobby with no explanation - that reads as a crash. The same busy
	// overlay the squad hand-off uses names what is happening and stays up for the travel.
	Busy(BRQueue() && BRQueue()->AmHost() ? TEXT("Starting the match...") : TEXT("Joining the match..."));
	Refresh();
	Controller->JoinBRMatch(Match, [this](const FString& Error)
	{
		bBusy = false;
		// The travel failed but the SESSION IS STILL OURS until the server ends it, so let it be
		// offered again - otherwise the player is stuck in the lobby, unable to queue (the server
		// answers BR_ALREADY_IN_MATCH) and unable to walk into the session they are a member of.
		if (UCampusBRQueue* Queue = BRQueue())
		{
			Queue->AllowRetry();
		}
		Toast(Error.IsEmpty() ? TEXT("Could not join the match.") : Error);
		Show(EScreen::Lobby);
	});
}

// -------------------------------------------------------------------------------------------------
// The screen

TSharedRef<SWidget> SCampusLobbyWidget::BuildMatchmaking()
{
	UCampusBRQueue* Queue = BRQueue();
	const int32 MapIndex = CampusCatalog::FindMap(FName(CampusCatalog::Mode(SelectedMode).MapId));
	const FString MapName = MapIndex != INDEX_NONE
		? FString(CampusCatalog::Map(MapIndex).Name).ToUpper() : FString(TEXT("BATTLE ROYALE"));
	const UCampusSave* Save = Profile();
	const bool bNight = Save && Save->bBrNight;
	const int32 Team = Queue ? Queue->GetTeamSize() : SelectedTeamSize;

	const FText Subtitle = FText::Format(LOCTEXT("BrSub", "{0}  ·  {1}  ·  {2}"),
		FText::FromString(MapName),
		CampusBR::TeamSizeLabel(Team),
		bNight ? LOCTEXT("BrNightTag", "NIGHT") : LOCTEXT("BrDayTag", "DAY"));

	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.88f)]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(26.f, 16.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
				// The state line. "MATCHED" is a beat, not a screen: EnterBRMatch is already travelling.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
					[
						SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(9.f)).Fill(Accent)
						[
							SNew(SBox).WidthOverride(16.f).HeightOverride(38.f)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						BrLine(TAttribute<FText>::CreateLambda([this]()
						{
							const UCampusBRQueue* Live = BRQueue();
							if (!Live)
							{
								return LOCTEXT("BrOff", "MATCHMAKING UNAVAILABLE");
							}
							switch (Live->GetState())
							{
							case ECampusBRState::Matched: return LOCTEXT("BrMatched", "MATCH FOUND");
							case ECampusBRState::Joining: return LOCTEXT("BrJoining", "JOINING THE QUEUE...");
							case ECampusBRState::Idle:    return LOCTEXT("BrIdle", "NOT IN THE QUEUE");
							default:                      return LOCTEXT("BrMatching", "MATCHING...");
							}
						}), 46, FLinearColor::White)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 0.f)
				[
					BrLine(Subtitle, 20, Accent)
				]
				// THE SESSION ID (BS-SQUAD-IN-000206). It appears the moment the team is placed and is
				// the one string a player can read out - to a friend, or to us when they report a bug.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						const UCampusBRQueue* Live = BRQueue();
						return Live && !Live->GetSessionId().IsEmpty()
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						BrLine(TAttribute<FText>::CreateLambda([this]()
						{
							const UCampusBRQueue* Live = BRQueue();
							return FText::FromString(Live ? Live->GetSessionId() : FString());
						}), 15, CampusAngular::TextMuted, false)
					]
				]
				// The timer counts UP. A down-count would be a promise the queue cannot keep; the
				// server's estimate goes under it, clearly labelled as an estimate.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 22.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						const UCampusBRQueue* Live = BRQueue();
						return FText::FromString(BrClock(Live ? Live->GetElapsed() : 0.f));
					})
					.Font(CampusAngular::Numeric(76))
					.ColorAndOpacity(FLinearColor::White)
					.ShadowOffset(FVector2D(3.f, 3.f))
					.ShadowColorAndOpacity(BrUiShadow)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						// The estimate is for a WAIT. Once the session is formed the countdown below is a
						// real promise, and two numbers where one is now meaningless is worse than one.
						const UCampusBRQueue* Live = BRQueue();
						return Live && Live->GetEta() > 0 && !Live->IsMatched()
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						BrLine(TAttribute<FText>::CreateLambda([this]()
						{
							const UCampusBRQueue* Live = BRQueue();
							return FText::Format(LOCTEXT("BrEta", "USUALLY ABOUT {0}"),
								FText::FromString(BrClock(float(Live ? Live->GetEta() : 0))));
						}), 16, CampusAngular::TextMuted)
					]
				]
				// Players found. Zero is a real answer, so the row is always here rather than
				// appearing halfway through and shifting the button under the player's thumb.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 24.f, 0.f, 0.f)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Notched(10.f))
					.Fill(FLinearColor(0.f, 0.f, 0.f, 0.62f))
					.Border(CampusAngular::Hairline)
					.BorderWidth(1.f)
					.ContentPadding(FMargin(22.f, 10.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
						[
							SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Friends).Size(20.f).Color(Accent)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text_Lambda([this]()
							{
								const UCampusBRQueue* Live = BRQueue();
								const int32 Found = Live ? Live->GetFound() : 0;
								const int32 Slots = Live ? Live->GetTotalSlots() : 50;
								// ONCE THE SESSION IS FORMED THE FILL IS A FACT, not a search: the
								// server has told us how many humans it packed and how many of the
								// fifty slots the match will spawn as bots.
								const int32 Bots = Live ? Live->GetBots() : 0;
								if (Live && Live->IsMatched() && Bots > 0)
								{
									return FText::Format(LOCTEXT("BrFill", "PLAYERS {0}   ·   BOTS {1}   ·   {2} SLOTS"),
										FText::AsNumber(Found), FText::AsNumber(Bots), FText::AsNumber(Slots));
								}
								return FText::Format(LOCTEXT("BrFound", "PLAYERS FOUND   {0} / {1}"),
									FText::AsNumber(Found), FText::AsNumber(Slots));
							})
							.Font(CampusAngular::Numeric(22))
							.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.88f))
						]
					]
				]
				// THE COUNTDOWN, off the server's clock (the session is in COUNTDOWN the instant it is
				// formed). This is the only down-count on the screen that the server actually promised.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						const UCampusBRQueue* Live = BRQueue();
						return Live && Live->IsMatched() && Live->GetCountdown() > 0
							? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						BrLine(TAttribute<FText>::CreateLambda([this]()
						{
							const UCampusBRQueue* Live = BRQueue();
							return FText::Format(LOCTEXT("BrDrop", "DROPPING IN {0}"),
								FText::AsNumber(Live ? Live->GetCountdown() : 0));
						}), 18, Accent)
					]
				]
				// The socket is how a match arrives; when it is down we poll every five seconds and
				// the player is told, because a silent slow queue looks broken.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						const UCampusSocial* Live = Social();
						const bool bDown = !Live || !Live->IsLive();
						return bDown ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
						[
							SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Wifi).Size(17.f)
							.Color(CampusAngular::Danger)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							BrLine(LOCTEXT("BrPolling", "CONNECTION IS WEAK - CHECKING EVERY 5 SECONDS"), 14,
								CampusAngular::TextMuted)
						]
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer)]
				// ONE target, 420 x 96, centred: the only thing on this screen you can press.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 18.f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]()
					{
						// Once a session is formed, cancelling is a lie - the travel is already running.
						// And a MEMBER has nothing to cancel: the ticket is the leader's and the server
						// refuses them by design, so offering the button would be a second lie.
						const UCampusBRQueue* Live = BRQueue();
						return Live && (Live->IsMatched() || Live->MustWaitForLeader())
							? EVisibility::Hidden : EVisibility::Visible;
					})
					[
						CampusStyle::Button(LOCTEXT("BrCancel", "CANCEL"),
							FOnClicked::CreateLambda([this]() { CancelMatchmaking(); return FReply::Handled(); }),
							EButton::Ghost, 30, FVector2D(420.f, 96.f))
					]
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
