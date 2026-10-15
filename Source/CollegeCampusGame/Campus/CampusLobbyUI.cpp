// Lobby level UI (Slate, no widget assets). Flow like Free Fire / PUBG Mobile:
//   Splash (loading + tips, TAP TO START) -> first launch: Agreement (terms + privacy) -> Login choice
//   (PLAY AS GUEST / LOGIN / REGISTER) -> first launch: Nickname -> Lobby.
// Everything account / money related goes through UCampusBackend (the API); nothing is decided on the phone.
//
// THIS FILE IS THE SHELL, not the screens. What lives here is everything every screen needs:
//
//   Construct / ~    the delegate bindings onto the subsystems that OUTLIVE this widget (the social
//                    channel, the BR queue, the party, the live-config push) and the one place they
//                    all come off again. Get this wrong and a reply lands on a freed lobby.
//   Tick             the single frame hook: one coalesced repaint, the toast and busy timers, the
//                    splash clock, the BR travel retry and the announcement column's dirty flag.
//   Show             ENTERING a screen, which is where anything costing a round trip belongs - a
//                    Build function runs on every repaint and must never start one.
//   Refresh / Busy / Done / Toast / AskConfirm / ShowFloating, and the shared Slate pieces every
//   screen is built out of (Backdrop, Dialog, Field).
//
// THE SCREENS THEMSELVES are one file each, beside this one; Campus/CampusLobbyUIInternal.h carries
// the map of which screen is in which file and the few constants they share.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusAvatar.h"
#include "Campus/CampusBR.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusLobbyUIInternal.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusParty.h"
#include "Campus/CampusPlayBilling.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusSocial.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusLobbyUI"

using namespace CampusStyle;

// ---------------------------------------------------------------------------------------------
// Plumbing

UCampusBackend* SCampusLobbyWidget::Backend() const
{
	return Controller.IsValid() ? Controller->GetBackend() : nullptr;
}

UCampusSave* SCampusLobbyWidget::Profile() const
{
	return Controller.IsValid() ? Controller->GetProfile() : nullptr;
}

UCampusParty* SCampusLobbyWidget::Party() const
{
	return Controller.IsValid() ? Controller->GetParty() : nullptr;
}

UCampusSocial* SCampusLobbyWidget::Social() const
{
	return Controller.IsValid() ? Controller->GetSocial() : nullptr;
}

void SCampusLobbyWidget::SyncParty()
{
	UCampusParty* Squad = Party();
	if (!Squad)
	{
		return;
	}
	// THE GATE. BuildLobby() calls this on every repaint, and it used to call SetCapacity / SetLeader
	// unconditionally. With a real server party that local guess would fight ApplyRoster for ever: the
	// server says who leads and how wide the row is, so when a party exists this does nothing at all.
	// Without one, today's behaviour is unchanged, and AdoptSquad stays the no-party fallback.
	if (const UCampusSocial* Live = Social(); Live && Live->HasParty())
	{
		return;
	}
	// The plate row is as wide as the chosen squad size, and the local player is always the leader.
	Squad->SetCapacity(SelectedTeamSize);
	const UCampusBackend* Api = Backend();
	const UCampusSave* Save = Profile();
	// Level is the SERVER's number (migration 013). There is no local formula any more, so the plate
	// and the profile page can never disagree about it.
	Squad->SetLeader(Api ? Api->PlayerName : TEXT("YOU"), Api ? Api->Level : 1,
		Save ? FMath::Clamp(Save->Wins / 5, 0, 5) : 0, MyAvatarUrl());
}

void SCampusLobbyWidget::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	if (const UCampusSave* Save = Profile())
	{
		// A save written before this screen existed can name a map or mode that was never built;
		// PlayableMode drops that back to Team Clash. The PAIR then has to make sense as well as each
		// half of it: a map the saved mode does not run on - a battle-royale map under Team Clash, or
		// a map this version dropped - lands on that mode's own first map (MapForMode).
		SelectedMode = CampusCatalog::PlayableMode(Save->MatchMode);
		SelectedMap = CampusCatalog::MapForMode(FName(CampusCatalog::Mode(SelectedMode).Id), Save->MatchMap);
		SelectedTeamSize = Save->TeamSize;
	}
	CampusOptions::ApplyGraphics(Profile());
	// The avatar cache borrows this session's API base and bearer; it is the only thing in it that
	// talks HTTP without going through UCampusBackend (the picture body is not JSON).
	CampusAvatar::UseSession(Backend());
	SplashTip = FMath::RandRange(0, UE_ARRAY_COUNT(Tips) - 1);
	// The roster changes from outside the UI (a finished match hands its squad over), so rebuild on
	// the delegate instead of diffing it every frame.
	if (UCampusParty* Squad = Party())
	{
		PartyChangedHandle = Squad->OnChanged.AddLambda([this]()
		{
			if (Current == EScreen::Lobby)
			{
				Refresh();
			}
		});
	}

	// The social channel pushes; the UI never polls. Each delegate repaints at most one screen.
	if (UCampusSocial* Live = Social())
	{
		SocialPartyHandle = Live->OnPartyChanged.AddLambda([this]()
		{
			// THE SQUAD IS GONE (I left, I was removed, or the leader broke it up), so the lobby goes
			// back to the state it had before there was one: SOLO, one figure, my own plate. This
			// delegate only fires on a REAL change, so "no party now" means there was one a moment ago -
			// leaving the chips on SQUAD would show three "+ INVITE" plates nobody asked for.
			const UCampusSocial* Live2 = Social();
			if (Live2 && !Live2->HasParty() && SelectedTeamSize > 1)
			{
				SelectedTeamSize = 1;
				SaveMatchChoice();
				SyncParty();
			}
			if (Current == EScreen::Lobby || Current == EScreen::FriendPick || Current == EScreen::Friends)
			{
				Refresh();
			}
		});
		SocialRosterHandle = Live->OnRosterChanged.AddLambda([this]()
		{
			if (Current == EScreen::Friends || Current == EScreen::FriendPick || Current == EScreen::Summary)
			{
				Refresh();
			}
		});
		SocialStateHandle = Live->OnStateChanged.AddLambda([this]()
		{
			const UCampusSocial* Live2 = Social();
			// The socket just came up, which is exactly when a battle session we were placed into while
			// it was down has to be found: `bs.placed` expires at the countdown, so a late reconnect
			// never replays it. One throttled read answers the question the socket cannot.
			if (Live2 && Live2->IsLive())
			{
				if (UCampusBRQueue* Queue = BRQueue())
				{
					Queue->ColdCheck();
				}
			}
			if (Current == EScreen::Friends || Current == EScreen::FriendPick)
			{
				Refresh();
			}
		});
		SocialNoticeHandle = Live->OnNotice.AddLambda([this](const FString& Message) { Toast(Message); });
		// party.room_ready: the leader is already hosting, so this only ever fires for a member.
		SocialRoomHandle = Live->OnRoomReady.AddLambda([this](const FString& Code)
		{
			if (!Controller.IsValid())
			{
				return;
			}
			// Never teleport out of the lobby with no explanation - that reads as a crash. The busy
			// overlay names what is happening and stays up for the travel.
			Busy(TEXT("Joining your squad's match..."));
			Controller->JoinRoomByCode(Code, [this](const FString& Error)
			{
				bBusy = false;
				Toast(Error.IsEmpty() ? TEXT("Could not join the squad's room.") : Error);
				Refresh();
			});
		});
		// bs.placed / bs.ended / br.result arrive on the same socket, but the BR queue owns the
		// parsing, so the lobby listens to the queue and not to the channel - see Campus/CampusBR.h.
		SocialSummaryHandle = Live->OnSummaryReady.AddLambda([this](const FString& Code)
		{
			// Mid-match this only records the code; the lobby's Tick opens the screen on arrival.
			if (Current == EScreen::Lobby)
			{
				OpenSummary(Code);
			}
		});
	}

	// The BATTLE ROYALE queue. OnChanged is already coalesced to once a second by the subsystem, and
	// it only repaints the overlay - never the lobby behind it.
	if (UCampusBRQueue* Queue = BRQueue())
	{
		BRChangedHandle = Queue->OnChanged.AddLambda([this]()
		{
			if (Current == EScreen::Matchmaking)
			{
				Refresh();
			}
		});
		BRMatchedHandle = Queue->OnMatched.AddLambda([this](const FCampusBRMatch& Match)
		{
			// Fires exactly once per battle session (`bs.placed`, its legacy twin `br.matched` and the
			// fallback read are all deduped on the session id), so this cannot travel twice and tear
			// the level down mid-load. A MEMBER lands here too: the leader queued the team, and this is
			// how the rest of the squad is pulled in.
			EnterBRMatch(Match);
		});
		// bs.ended: the host went quiet and the server settled the session. A player who was still in
		// the lobby (the travel had not finished) is told here; one who was already in the match is
		// brought back by ACampusMatchPlayerController and reads the same line on arrival.
		BREndedHandle = Queue->OnSessionEnded.AddLambda([this](const FString& Why)
		{
			bBusy = false;
			if (UCampusBRQueue* Live2 = BRQueue())
			{
				// We are showing it now, so the lobby after the next travel must not show it again.
				Live2->TakeEndedNotice();
			}
			Toast(Why);
			Show(EScreen::Lobby);
		});
		BRScoredHandle = Queue->OnScored.AddLambda([this](const FCampusBRResult& Scored)
		{
			// The host reported and the XP is paid; the placement is what the player wants to hear.
			Toast(FString::Printf(TEXT("#%d  ·  %d KILLS  ·  +%d XP"),
				Scored.Placement, Scored.Kills, Scored.XpAwarded));
		});
		// A session that ended while this player was in the match level has no lobby to speak to, so
		// the sentence waited in the subsystem for this one.
		if (const FString Notice = Queue->TakeEndedNotice(); !Notice.IsEmpty())
		{
			Toast(Notice);
		}
		// The phone that missed its `bs.placed` frame (app restarted, socket down past the event's
		// expiry) has no other way to learn it was placed. Throttled inside the subsystem.
		Queue->ColdCheck();
	}

	// The published blob can land at any time (lobby entry, or a resume ten minutes later), and the
	// cards it draws must not be rebuilt from inside the network callback - the same rule as every
	// other repaint here. One flag, served by Tick.
	if (UCampusBackend* Api = Backend())
	{
		// AddSP, NOT AddLambda([this]). UCampusBackend is a GameInstanceSubsystem and survives level
		// travel; this widget does not. A raw `this` in the delegate fired into a freed lobby if the
		// handle ever failed to come off - and the only thing that took it off was a destructor that
		// had to find the subsystem again through Controller, which by then may already be gone.
		// AddSP holds a weak reference to the widget, so a stale binding is inert rather than fatal.
		//
		// AND THE SUBSYSTEM IS CACHED, so the destructor never has to go looking for it: the handle
		// comes off through this pointer whatever state the controller is in.
		LiveConfigApi = Api;
		LiveConfigHandle = Api->OnLiveConfigChanged.AddSP(this, &SCampusLobbyWidget::OnLiveConfigPushed);
	}

	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot()
		[
			SAssignNew(Content, SBox).Visibility(EVisibility::SelfHitTestInvisible)
		]
		// "please wait" while an API call runs: blocks taps so nothing is pressed twice
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			// The WASH fades in only once the call is genuinely slow; see BusyScrimDelay. The scrim
			// itself stays Visible for the whole call, so it still blocks the second tap from frame one.
			.BorderBackgroundColor_Lambda([this]()
			{
				return FSlateColor(FLinearColor(0.f, 0.f, 0.f, bBusy && BusyTime >= BusyScrimDelay ? 0.5f : 0.f));
			})
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Visibility_Lambda([this]() { return bBusy ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBox)
				// "PLEASE WAIT" over an invite that answers in two frames is a flash, not information.
				.Visibility_Lambda([this]()
				{
					return bBusy && BusyTime >= BusyScrimDelay ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					CampusStyle::Panel(
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SNew(STextBlock).Text_Lambda([this]()
							{
								const UCampusBackend* Api = Backend();
								// BusyText is already uppercased in Busy(); no per-frame string work here.
							return FText::FromString(Api && Api->IsWaitingForPayment() ? FString(TEXT("WAITING FOR THE PAYMENT TO FINISH...")) : BusyText);
							}).Font(Font(22)).ColorAndOpacity(FLinearColor::White)
						]
						// a payment can always be abandoned (back from the browser, sheet stuck): the server settles it
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 16.f, 0.f, 0.f)
						[
							SNew(SBox).Visibility_Lambda([this]()
							{
								const UCampusBackend* Api = Backend();
								return Api && Api->IsWaitingForPayment() ? EVisibility::Visible : EVisibility::Collapsed;
							})
							[
								Button(LOCTEXT("CancelPay", "CANCEL PAYMENT"), FOnClicked::CreateLambda([this]()
								{
									if (UCampusBackend* Api = Backend())
									{
										BusyText = TEXT("CHECKING WITH RAZORPAY...");
										Api->CancelPendingPayment();
									}
									return FReply::Handled();
								}), EButton::Ghost, 18, FVector2D(280.f, 56.f))
							]
						],
						FMargin(40.f, 24.f), 20.f, true)
				]
			]
		]
		// A yes/no over whatever screen is showing. Above the busy scrim, because the question it asks
		// is usually the thing that is holding the call up.
		+ SOverlay::Slot()
		[
			SAssignNew(ConfirmBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
		]
		// THE NOTICE STACK, above everything except the toast (SOCIAL_PARTY §14.1): an invite that
		// lands while the Store is open, or while a claim is in flight, must still be answerable.
		// Top padding 150 clears the top bar and the logo (which sits at y 86-126); right padding 0,
		// so the card's right edge meets the safe-zone edge and "stops at the right side" reads as
		// intentional rather than as a layout accident.
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Visibility(EVisibility::SelfHitTestInvisible).Padding(FMargin(24.f, 16.f))
			[
				SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(0.f, 150.f, 0.f, 0.f))
				.Visibility(EVisibility::SelfHitTestInvisible)
				[
					SNew(SVerticalBox)
					.Visibility(EVisibility::SelfHitTestInvisible)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SCampusNoticeStack)
						.Social(Social())
						.OnAccept(FOnCampusNoticeAction::CreateLambda([this](FString Key)
						{
							UCampusSocial* Live = Social();
							const FCampusNotice* Card = Live ? Live->FindNotice(Key) : nullptr;
							if (!Card)
							{
								return;
							}
							if (Card->Kind == ECampusNoticeKind::Invite)
							{
								AnswerInvite(Key, true);
								return;
							}
							const FString Who = Card->Sender.PlayerId;
							Live->ResolveNotice(Key, TEXT("ACCEPTED"));
							Live->AcceptFriend(Who, [this](bool, const FString&) { Refresh(); });
						}))
						.OnReject(FOnCampusNoticeAction::CreateLambda([this](FString Key)
						{
							UCampusSocial* Live = Social();
							const FCampusNotice* Card = Live ? Live->FindNotice(Key) : nullptr;
							if (!Card)
							{
								return;
							}
							if (Card->Kind == ECampusNoticeKind::Invite)
							{
								AnswerInvite(Key, false);
								return;
							}
							const FString Who = Card->Sender.PlayerId;
							Live->ResolveNotice(Key, TEXT("DECLINED"));
							Live->DeclineFriend(Who, [this](bool, const FString&) { Refresh(); });
						}))
						.OnOpenTray(FSimpleDelegate::CreateLambda([this]() { Show(EScreen::InviteTray); }))
					]
					// ANNOUNCEMENTS from the admin console (Campus/CampusLiveConfigUI.cpp), in the SAME
					// column and at the same width, directly UNDER the invites: nothing overlaps, the corner
					// stays one surface, and a time-critical invite still sits on top of the news. Lobby
					// only, unlike the invites - an invite must survive the Store, a notice need not.
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
					[
						SAssignNew(AnnounceBox, SBox)
						.Visibility_Lambda([this]()
						{
							return Current == EScreen::Lobby ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
						})
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 96.f, 0.f, 0.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Slanted(22.f))
			.Fill(CampusAngular::InkStrong)
			.Border(CampusAngular::Accent)
			.BorderWidth(2.f)
			.ContentPadding(FMargin(30.f, 12.f))
			.Visibility_Lambda([this]() { return ToastTime > 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(ToastText); }).Font(Font(20)).ColorAndOpacity(FLinearColor::White)
			]
		]
		// THE GATE, topmost: UPDATE REQUIRED or the MAINTENANCE lock. Last in the overlay, so while it
		// is up nothing under it - not the toast, not an invite, not START - can be reached. It is
		// COLLAPSED unless the server says otherwise, and it only exists on the lobby level, which is
		// what guarantees it can never close over a match in progress.
		+ SOverlay::Slot()
		[
			BuildLiveGate()
		]
	];

	const UCampusBackend* Api = Backend();
	SyncParty();
	Show(Api && Api->IsSignedIn() ? EScreen::Lobby : EScreen::Splash);
}

SCampusLobbyWidget::~SCampusLobbyWidget()
{
	if (PartyChangedHandle.IsValid())
	{
		if (UCampusParty* Squad = Party())
		{
			Squad->OnChanged.Remove(PartyChangedHandle);
		}
	}
	// The subsystem outlives this widget (it survives level travel), so every handle comes off here or
	// the next lobby load would fire into a dead `this`.
	if (UCampusSocial* Live = Social())
	{
		Live->OnPartyChanged.Remove(SocialPartyHandle);
		Live->OnRosterChanged.Remove(SocialRosterHandle);
		Live->OnStateChanged.Remove(SocialStateHandle);
		Live->OnNotice.Remove(SocialNoticeHandle);
		Live->OnRoomReady.Remove(SocialRoomHandle);
		Live->OnSummaryReady.Remove(SocialSummaryHandle);
	}
	// Through the CACHED pointer, not through Backend(): that walks Controller -> GameInstance, and a
	// destructor running after the controller has gone would quietly fail to unbind.
	if (UCampusBackend* Api = LiveConfigApi.Get())
	{
		Api->OnLiveConfigChanged.Remove(LiveConfigHandle);
	}
	// Same reason: the queue survives level travel and this widget does not. The QUEUE ITSELF is left
	// running on purpose - a player who taps away mid-queue is still queued, and the overlay picks the
	// state back up when the lobby rebuilds.
	if (UCampusBRQueue* Queue = BRQueue())
	{
		Queue->OnChanged.Remove(BRChangedHandle);
		Queue->OnMatched.Remove(BRMatchedHandle);
		Queue->OnSessionEnded.Remove(BREndedHandle);
		Queue->OnScored.Remove(BRScoredHandle);
	}
}

void SCampusLobbyWidget::ShowFloating(TSharedRef<SWidget> Content2, float Width)
{
	if (!ConfirmBox.IsValid())
	{
		return;
	}
	ConfirmBox->SetVisibility(EVisibility::Visible);
	ConfirmBox->SetContent(
		SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.62f)]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(Width)
			[
				CampusStyle::Panel(Content2, FMargin(32.f, 24.f), 20.f, true)
			]
		]);
}

void SCampusLobbyWidget::AskConfirm(const FText& Title, const FText& Body, const FText& Yes, TFunction<void()> OnYes)
{
	ShowFloating(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Label(Title, 28, FLinearColor::White)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(STextBlock).Text(Body).Font(Font(18, false))
			.ColorAndOpacity(CampusAngular::TextMuted).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 22.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
			[
				Button(LOCTEXT("ConfirmNo", "CANCEL"), FOnClicked::CreateLambda([this]()
				{
					CloseConfirm();
					return FReply::Handled();
				}), EButton::Ghost, 18, FVector2D(180.f, 56.f))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(Yes, FOnClicked::CreateLambda([this, OnYes]()
				{
					CloseConfirm();
					if (OnYes)
					{
						OnYes();
					}
					return FReply::Handled();
				}), EButton::Accent, 18, FVector2D(230.f, 56.f))
			]
		], 680.f);
}

void SCampusLobbyWidget::CloseConfirm()
{
	if (ConfirmBox.IsValid())
	{
		ConfirmBox->SetContent(SNullWidget::NullWidget);
		ConfirmBox->SetVisibility(EVisibility::SelfHitTestInvisible);
	}
}

void SCampusLobbyWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ToastTime = FMath::Max(0.f, ToastTime - InDeltaTime);
	BusyTime = bBusy ? BusyTime + InDeltaTime : 0.f;

	// Every repaint asked for since the last frame, served once, outside event routing. Done first so
	// the rest of this Tick sees a painted screen, and before the Show() calls below - those paint
	// themselves and clear the flag, so a screen change is never followed by a wasted repaint.
	if (bRefreshQueued)
	{
		Show(Current);
	}
	if (bAnnouncementsDirty)
	{
		bAnnouncementsDirty = false;
		RefreshAnnouncements();
	}

	if (Current == EScreen::Splash && !bSplashReady)
	{
		SplashTime += InDeltaTime;
		// a saved session signs back in while the bar fills (like the big games' "checking resources")
		if (!bResuming && SplashTime > 0.3f)
		{
			bResuming = true;
			if (UCampusBackend* Api = Backend(); Api && Api->HasSavedSession())
			{
				Api->ResumeSession([this](bool bOk, const FString& Error)
				{
					if (!bOk && !Error.IsEmpty())
					{
						Toast(Error);
					}
				});
			}
		}
		if (SplashTime > 2.2f)
		{
			bSplashReady = true;
			Refresh(); // shows TAP TO START
		}
	}

	// THE BATTLE SESSION TRAVEL RETRY (see EnterBRMatch). A host that was still loading its level when
	// we dialled it answers on the next try, so the session is dialled again rather than thrown away.
	if (BRRetryIn > 0.f)
	{
		BRRetryIn = FMath::Max(0.f, BRRetryIn - InDeltaTime);
		if (BRRetryIn <= 0.f)
		{
			// The session object has ONE owner. Read it back from the queue instead of holding a copy
			// that may have been sealed, ended or replaced since the travel failed.
			if (UCampusBRQueue* Queue = BRQueue(); Queue && Queue->IsMatched())
			{
				EnterBRMatch(Queue->GetMatch());
			}
		}
	}

	if (UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr)
	{
		if (!Instance->PendingMessage.IsEmpty() && Current != EScreen::Splash)
		{
			Toast(Instance->PendingMessage);
			Instance->PendingMessage.Reset();
		}
	}

	// An account from before the 18+ rule: ask once, then carry on. Only over the lobby, never over
	// a dialog the player opened themselves.
	UCampusBackend* Api = Backend();
	if (Api && Api->NeedsAdultConfirm() && Current == EScreen::Lobby)
	{
		Show(EScreen::AdultConfirm);
		return;
	}

	// The post-match screen's own clock: CONTINUE arms at 1.5 s so nobody blind-taps through it, and
	// it lets go at 12 s - longer than that and the player feels held hostage.
	if (Current == EScreen::Summary)
	{
		SummaryTime += InDeltaTime;
		if (SummaryTime > 12.f)
		{
			SummaryData.Reset();
			Show(EScreen::Lobby);
			return;
		}
	}

	// A match just ended: open PLAYER OF THE MATCH as soon as we are back on the lobby. The room code
	// comes from match.summary_ready when the socket is up, and from the match we just left when it
	// is not - so the screen shows either way.
	if (UCampusSocial* Live = Social(); Live && Current == EScreen::Lobby && !SummaryData.IsValid()
		&& !Live->PendingSummaryRoom().IsEmpty() && !bBusy)
	{
		const FString Code = Live->PendingSummaryRoom();
		// One attempt per code: roomSummary() only exists once the HOST has reported the result, and
		// a retry loop here would poll a metered tunnel.
		Live->ClearPendingSummary();
		OpenSummary(Code);
	}

	// The login streak popup: once per game day, and only on the lobby itself, so it can never land
	// on top of Create / Join Room.
	if (Api && Current == EScreen::Lobby && !bStreakOffered && Api->Engagement.bLoaded && Api->Engagement.Streak.bClaimable)
	{
		UCampusSave* Save = Profile();
		const FString Today = ServerGameDay();
		if (Save && !Today.IsEmpty() && Save->StreakPopupDay != Today)
		{
			bStreakOffered = true;
			Save->StreakPopupDay = Today;
			Save->Persist();
			Show(EScreen::Streak);
		}
	}
}

void SCampusLobbyWidget::Toast(const FString& Message)
{
	ToastText = Message;
	ToastTime = 3.5f;
}

void SCampusLobbyWidget::Busy(const FString& What)
{
	bBusy = true;
	BusyTime = 0.f;
	// Uppercased once here, not in the overlay's per-frame text lambda.
	BusyText = What.ToUpper();
}

void SCampusLobbyWidget::Done(bool bOk, const FString& Error, TFunction<void()> OnOk)
{
	bBusy = false;
	if (!bOk)
	{
		Toast(Error);
	}
	else if (OnOk)
	{
		OnOk();
	}
	Refresh();
}

void SCampusLobbyWidget::Show(EScreen Screen)
{
	// ENTERING a screen is not the same as repainting it. Anything that costs a round trip belongs
	// here, where it happens once, and never in a Build function, which runs on every repaint.
	const bool bEntering = Current != Screen;
	Current = Screen;
	if (bEntering && Screen == EScreen::Store)
	{
		// A fresh visit may find prices the last one could not; the latch inside stops it looping.
		bStorePricesQueried = false;
		StoreQueryPlayPrices();
	}
	if (bEntering && Screen == EScreen::Lobby)
	{
		// GET /me/cosmetics, once per app run (RefreshCosmetics is latched). On the lobby rather than
		// on the wardrobe, because the LINE-UP and the eight decor anchors are what it dresses - the
		// closet is only where it is edited. Covers the resumed-session path that never calls
		// AfterSignIn, and costs nothing on the way back from a match.
		RefreshCosmetics();
		// GET /v1/live-config: maintenance, the minimum build and the announcements. On ENTRY, never
		// in a Build function, and throttled inside the subsystem to once per app run plus one stale
		// resume - the tunnel is metered at 20,000 requests a month.
		RefreshLiveConfig();
	}
	if (bEntering && Screen == EScreen::Event)
	{
		// ONE progress read per visit to the sheet (GET /v1/events/:id/progress), started here and
		// never from BuildEvent: a Build function runs on every repaint, and its own reply repaints.
		OpenEventSheet();
	}
	if (bEntering && Screen == EScreen::Profile && Controller.IsValid())
	{
		// THE CAMERA MOVES TO THE FIGURE on the way IN, exactly like the wardrobe below - and for the
		// same reason it was moved out of BuildWardrobe: the profile page repaints on every tab tap,
		// every like and every reply, and from inside BuildProfile this was a scene change dozens of
		// times over for one visit. The matching FocusProfile(false) is the exception list further down.
		Controller->FocusProfile(true);
	}
	if (bEntering && Screen == EScreen::Wardrobe && Controller.IsValid())
	{
		// THE CAMERA MOVES TO THE FIGURE, and the rest of the line-up hides: the SAME call the profile
		// page makes, so there is still exactly one 3D path in the lobby. HERE and not in
		// BuildWardrobe, because a Build function runs on every repaint - and the closet repaints on
		// every equip tap and every reply, so from there this was a scene change dozens of times over
		// for one visit. The matching FocusProfile(false) is the exception list further down.
		Controller->FocusProfile(true);
	}

	TSharedRef<SWidget> Body = SNullWidget::NullWidget;
	switch (Screen)
	{
	case EScreen::Splash: Body = BuildSplash(); break;
	case EScreen::Agreement: Body = BuildAgreement(); break;
	case EScreen::LoginChoice: Body = BuildLoginChoice(); break;
	case EScreen::EmailLogin: Body = BuildEmailLogin(); break;
	case EScreen::Register: Body = BuildRegister(); break;
	case EScreen::Nickname: Body = BuildNickname(); break;
	case EScreen::Lobby: Body = BuildLobby(); break;
	case EScreen::Store: Body = BuildStoreV2(); break;   // CampusStoreUI.cpp
	case EScreen::Rooms: Body = BuildRooms(); break;
	case EScreen::Account: Body = BuildAccount(); break;
	case EScreen::Event: Body = BuildEvent(); break;
	case EScreen::Daily: Body = BuildDaily(); break;
	case EScreen::Settings: Body = BuildSettings(); break;
	case EScreen::Missions: Body = BuildMissions(); break;
	case EScreen::Streak: Body = BuildStreak(); break;
	case EScreen::AdultConfirm: Body = BuildAdultConfirm(); break;
	case EScreen::CollegePick: Body = BuildCollegePick(); break;
	case EScreen::MapSelect: Body = BuildMapSelect(); break;
	case EScreen::Wardrobe: Body = BuildWardrobe(); break;   // CampusWardrobeUI.cpp
	case EScreen::Decor: Body = BuildDecorScreen(); break;   // CampusWardrobeUI.cpp
	case EScreen::Matchmaking: Body = BuildMatchmaking(); break;
	case EScreen::Friends: Body = BuildFriends(); break;
	case EScreen::FriendPick: Body = BuildFriendPick(); break;
	case EScreen::InviteTray: Body = BuildInviteTray(); break;
	case EScreen::Profile: Body = BuildProfile(); break;
	case EScreen::Summary: Body = BuildSummary(); break;
	}

	// An invite that lands while the Store is open, or while the profile page is up, MUST still be
	// answerable - that is the whole reason the stack sits above the busy scrim. The only screens
	// that suppress it are the ones before there is an account to invite: there they queue in the
	// subsystem and are re-evaluated on the way in, dropping anything whose clock ran out.
	if (UCampusSocial* Live = Social())
	{
		const bool bSignedInScreen = Screen != EScreen::Splash && Screen != EScreen::Agreement
			&& Screen != EScreen::LoginChoice && Screen != EScreen::EmailLogin && Screen != EScreen::Register
			&& Screen != EScreen::Nickname && Screen != EScreen::AdultConfirm;
		Live->SetNoticesVisible(bSignedInScreen);
	}
	// Leaving the profile page gives the 3D scene back to the line-up. THE WARDROBE IS THE SECOND
	// screen that owns the figure: it stands it in the same place with the same camera, so it belongs
	// on this exception with Profile. The DECOR sheet deliberately does NOT - decor is judged against
	// the whole room, so that screen wants the full line-up and the podium camera back.
	if (Screen != EScreen::Profile && Screen != EScreen::Wardrobe && Controller.IsValid())
	{
		Controller->FocusProfile(false);
	}
	// The body was built from the state as it stands right now, so any repaint that was asked for
	// while it was being built has already been served.
	bRefreshQueued = false;
	// The lobby hands back the SAME body every time (BuildLobby repaints it in place), so re-attaching
	// it would throw away a layout for nothing. Every other screen builds a fresh one.
	if (ShownBody.Get() != &Body.Get())
	{
		ShownBody = Body;
		Content->SetContent(Body);
		// MEASURE IT NOW. A repaint is served from Tick, which can land after Slate has already run
		// this frame's layout pass - and a widget that has never been measured has a desired size of
		// zero, so every child of it arranges at its slot's origin. That is exactly what the store
		// looked like: the whole screen piled into the top-left corner. One prepass of the subtree we
		// just built costs nothing and makes a repaint safe to serve from anywhere.
		Body->SlatePrepass();
	}
}

void SCampusLobbyWidget::Refresh()
{
	// COALESCED, AND NEVER FROM INSIDE THE TAP THAT ASKED FOR IT. One mode chip reaches this three
	// times - SyncParty() moves the capacity and UCampusParty::OnChanged fires, the handler then calls
	// Refresh itself, and the party reply arrives on top of that. Repainting there did three full
	// repaints inside one touch and destroyed the chip that was still routing the touch, so the press
	// highlight, the hover state and the whole left menu column visibly jumped on every interaction.
	bRefreshQueued = true;
}

void SCampusLobbyWidget::RefreshEngagement()
{
	UCampusBackend* Api = Backend();
	if (!Api || !Api->IsSignedIn())
	{
		return;
	}
	// Paint from the cached copy first so the lobby is never empty while this is in the air.
	Api->LoadEngagementFromCache();
	Api->LoadEngagementHome([this](bool bOk, const FString& Error)
	{
		if (!bOk)
		{
			// A missing engagement feed must never block the lobby: the panels show their empty
			// state and the rest of the screen works. Only a real error is worth a toast.
			if (!Error.IsEmpty() && Backend() && Backend()->NeedsAdultConfirm())
			{
				Show(EScreen::AdultConfirm);
			}
			return;
		}
		if (Current == EScreen::Lobby || Current == EScreen::Missions)
		{
			Refresh();
		}
	});
}

void SCampusLobbyWidget::AfterClaim(bool bOk, const FString& Error, const FString& OkText)
{
	bClaiming = false;
	bBusy = false;
	const UCampusBackend* Api = Backend();
	if (bOk)
	{
		Toast(OkText);
		// The wallet already came back on the claim response; the panels need the new states.
		RefreshEngagement();
	}
	else if (Api && Api->bRewardCapReached)
	{
		Toast(TEXT("You have hit today's reward limit. This is waiting for you tomorrow."));
	}
	else
	{
		Toast(Error);
	}
	Refresh();
}

FString SCampusLobbyWidget::ServerGameDay() const
{
	const UCampusBackend* Api = Backend();
	if (!Api || !Api->Engagement.bLoaded)
	{
		return FString();
	}
	// The API's game day is IST (UTC+5:30) shifted back 5 hours, so a day rolls over at 5 AM IST.
	const FDateTime Ist = Api->Engagement.Now() + FTimespan::FromMinutes(330) - FTimespan::FromHours(5);
	return Ist.ToString(TEXT("%Y%m%d"));
}

void SCampusLobbyWidget::AfterSignIn()
{
	UCampusSave* Save = Profile();
	const UCampusBackend* Api = Backend();
	// One round trip for streak + missions + the weekend board, and any Play purchase that was
	// paid but never credited (an app killed mid-purchase) is pushed to the server now.
	RefreshEngagement();
	// The social channel, and the ONE read that makes the top bar's level true (migration 013). Both
	// are cheap and both happen once per sign-in, never on a timer.
	if (UCampusSocial* Live = Social())
	{
		Live->Start();
		Live->LoadProfile(FString(), [this](bool bOk, const FString&, const FCampusProfile&)
		{
			if (bOk && (Current == EScreen::Lobby || Current == EScreen::Profile))
			{
				Refresh();
			}
		});
	}
	if (UCampusPlayBilling* Billing = Controller.IsValid() && Controller->GetGameInstance()
		? Controller->GetGameInstance()->GetSubsystem<UCampusPlayBilling>() : nullptr)
	{
		Billing->RestoreUnfinished();
	}
	if (Save && !Save->bNicknameChosen)
	{
		NameText = Api ? Api->PlayerName : FString();
		Show(EScreen::Nickname);
		return;
	}
	if (UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr)
	{
		Instance->bSignedIn = true;
		// ONCE PER APP RUN, AND ONLY WHEN THERE IS SOMETHING TO SHOW. The sheet used to be a hard-coded
		// card that was always worth opening; it is the console's live event now, and opening a "NO
		// EVENT RUNNING" sheet in a player's face on sign-in is worse than not opening it at all. The
		// blob is read from the save in UCampusBackend::Initialize, so this is true on the first frame.
		const bool bShowEvent = !Instance->bEventSeen && Api && Api->LiveEvent() != nullptr;
		Instance->bEventSeen = true;
		Show(bShowEvent ? EScreen::Event : EScreen::Lobby);
		return;
	}
	Show(EScreen::Lobby);
}

void SCampusLobbyWidget::SaveMatchChoice()
{
	if (UCampusSave* Save = Profile())
	{
		Save->MatchMap = FName(CampusCatalog::Map(SelectedMap).Id);
		Save->MatchMode = FName(CampusCatalog::Mode(SelectedMode).Id);
		Save->TeamSize = SelectedTeamSize;
		Save->Persist();
	}
}

// ---------------------------------------------------------------------------------------------
// Shared pieces

TSharedRef<SWidget> SCampusLobbyWidget::Backdrop(float Alpha) const
{
	return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, Alpha));
}

TSharedRef<SWidget> SCampusLobbyWidget::Dialog(const FText& Title, TSharedRef<SWidget> Body, float Width, bool bClose, EScreen CloseTo)
{
	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.6f)]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(Width).MaxDesiredHeight(660.f)
			[
				CampusStyle::Panel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
						[
							// A slanted accent flash to the left of every dialog title.
							SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(9.f)).Fill(Accent)
							[
								SNew(SBox).WidthOverride(16.f).HeightOverride(30.f)
							]
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Label(Title, 30, FLinearColor::White)]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							bClose
								? CampusStyle::IconButton(CampusAngular::EGlyph::Close,
									FOnClicked::CreateLambda([this, CloseTo]() { Show(CloseTo); return FReply::Handled(); }), 62.f)
								: SNullWidget::NullWidget
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 16.f, 0.f, 0.f)[Body],
					FMargin(36.f, 26.f), 22.f, true)
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::Field(const FText& Hint, FString& Target, bool bPassword)
{
	FString* Bound = &Target;
	// A clipped-corner plate behind the text box; the box itself draws no background.
	static FEditableTextBoxStyle FieldStyle = []()
	{
		FEditableTextBoxStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
		static const FSlateNoResource Empty;
		Style.SetBackgroundImageNormal(Empty);
		Style.SetBackgroundImageHovered(Empty);
		Style.SetBackgroundImageFocused(Empty);
		Style.SetBackgroundImageReadOnly(Empty);
		Style.SetForegroundColor(FSlateColor(FLinearColor::White));
		Style.SetFocusedForegroundColor(FSlateColor(FLinearColor::White));
		Style.SetPadding(FMargin(0.f, 6.f));
		return Style;
	}();

	return SNew(SBox).HeightOverride(66.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(14.f))
			.Fill(FLinearColor(0.f, 0.f, 0.f, 0.45f))
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(16.f, 0.f))
			[
				SNew(SEditableTextBox)
				.Style(&FieldStyle)
				.Text(FText::FromString(Target))
				.HintText(Hint)
				.IsPassword(bPassword)
				.Font(Font(22, false))
				.OnTextChanged_Lambda([Bound](const FText& Text) { *Bound = Text.ToString(); })
			]
		];
}

#undef LOCTEXT_NAMESPACE
