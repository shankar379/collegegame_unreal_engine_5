// The social screens: the friends list, the invite picker behind every "+ INVITE" plate, the stacked
// invite tray, the squad strip under the line-up, and the post-match PLAYER OF THE MATCH screen.
//
// Same language as the rest of the lobby: FCampusShape plates, one accent (#FFB800), bold condensed
// italic uppercase, no texture assets. A player's colour stands in for an avatar, which is why every
// card shape the API returns carries one.
//
// ORDER IS THE SERVER'S. GET /v1/friends sorts by who can answer right now - in a lobby, then in a
// menu, then in a match, then offline, then by name - and this file does not re-sort. Two screens must
// never disagree about who is at the top of the list.

#include "Campus/CampusUI.h"

#include "Algo/Count.h"
#include "Campus/CampusAngular.h"
#include "Campus/CampusAvatar.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusParty.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSocial.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusSocialUI"

using namespace CampusStyle;

namespace
{
	/** Every roster row is this tall, which is also the tap target. */
	constexpr float RowHeight = 64.f;
	/** The lobby's ONLINE strip scrolls past this height instead of growing. Five rows of 52 plus
	 *  their 4 of spacing: tall enough to be a list, short enough that a full roster can never reach
	 *  up into the notice stack that owns the top-right corner. */
	constexpr float StripScrollHeight = 280.f;
	/** The post-match screen holds this long before CONTINUE is tappable, and auto-continues later. */
	constexpr float SummaryArmAt = 1.5f;
	constexpr float SummaryAutoAt = 12.f;

	FText ModeName(int32 Mode)
	{
		return Mode == 2 ? LOCTEXT("Duo", "DUO") : LOCTEXT("Squad", "SQUAD");
	}

	/** "1,830" - thousands separated, which is what makes a stats strip readable at a glance. */
	FText SocialGrouped(int32 Value)
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(true);
		return FText::AsNumber(Value, &Options);
	}
}

// ---------------------------------------------------------------------------------------------
// Shared pieces

TSharedRef<SWidget> SCampusLobbyWidget::Avatar(const FCampusPlayerCard& Card, float Size) const
{
	// THE PICTURE WHEN THERE IS ONE, the colour + initial plate when there is not. Same clipped-corner
	// shape either way, so a row of mixed cards still reads as one row. The URL is captured by value:
	// these rows are rebuilt from server data, never bound to a live card.
	return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
		[
			SNew(SCampusAvatar)
			.Url(Card.AvatarUrl)
			.Size(Size)
			.Shape(FCampusShape::Clipped(Size * 0.22f))
			.Fill(Card.Color)
			.Border(FLinearColor(1.f, 1.f, 1.f, 0.3f))
			.BorderWidth(1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Card.Initial()))
				.Font(CampusAngular::Display(FMath::RoundToInt(Size * 0.46f)))
				.ColorAndOpacity(FLinearColor::White)
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::FriendButton(const FCampusPlayerCard& Card, FVector2D Size)
{
	// The button reads the server's friend_state, with one session-local override: a request sent a
	// moment ago shows REQUESTED before GET /v1/friends has caught up.
	const bool bRequested = OptimisticFriends.Contains(Card.PlayerId);
	const FString State = bRequested ? TEXT("outgoing") : Card.FriendState;
	if (State == TEXT("blocked"))
	{
		// No button at all for a blocked pair, in either direction.
		return SNullWidget::NullWidget;
	}
	if (State == TEXT("friends"))
	{
		return Button(LOCTEXT("AlreadyFriends", "FRIENDS"), FOnClicked(), EButton::Ghost, 16, Size, false);
	}
	if (State == TEXT("outgoing"))
	{
		return Button(LOCTEXT("Requested", "REQUESTED"), FOnClicked(), EButton::Ghost, 16, Size, false);
	}
	const FString Who = Card.PlayerId;
	const FString Name = Card.Name;
	if (State == TEXT("incoming"))
	{
		return Button(LOCTEXT("AcceptFriend", "ACCEPT"), FOnClicked::CreateLambda([this, Who, Name]()
		{
			if (UCampusSocial* Live = Social())
			{
				// WEAK, AND NOT OPTIONAL. UCampusSocial is a GameInstanceSubsystem: it OUTLIVES this widget,
				// it survives level travel, and it holds this callback until the request finishes. A raw
				// `this` was a use-after-free the moment a reply landed on a lobby that had already been torn
				// down - a player tapping PLAY while a friend request is in the air is all it took. Same
				// convention as every UCampusBackend callback (Campus/CampusWardrobeUI.cpp).
				TWeakPtr<SWidget> Weak = AsWeak();
				Live->AcceptFriend(Who, [this, Weak, Name](bool bOk, const FString& Error)
				{
					if (!Weak.IsValid())
					{
						return;
					}
					Toast(bOk ? FString::Printf(TEXT("%s IS NOW YOUR FRIEND."), *Name.ToUpper()) : Error);
					Refresh();
				});
			}
			return FReply::Handled();
		}), EButton::Go, 16, Size);
	}
	return Button(LOCTEXT("AddFriend", "+ ADD FRIEND"), FOnClicked::CreateLambda([this, Who, Name]()
	{
		UCampusSocial* Live = Social();
		if (!Live)
		{
			return FReply::Handled();
		}
		// Optimistic: the button flips to REQUESTED at once and reverts with a toast on failure.
		OptimisticFriends.Add(Who);
		Refresh();
		TWeakPtr<SWidget> Weak = AsWeak();
		Live->RequestFriend(Who, [this, Weak, Who](bool bOk, const FString& Why)
		{
			if (!Weak.IsValid())
			{
				return;
			}
			if (!bOk)
			{
				OptimisticFriends.Remove(Who);
				Toast(Why);
			}
			Refresh();
		});
		return FReply::Handled();
	}), EButton::Go, 16, Size);
}

TSharedRef<SWidget> SCampusLobbyWidget::FriendRequestBadge()
{
	UCampusSocial* Live = Social();
	if (!Live)
	{
		return SNullWidget::NullWidget;
	}
	// Everything that is waiting on an answer: friend requests AND stacked squad invites. One dot,
	// one number, one place to look.
	const int32 Waiting = Live->GetRoster().Incoming.Num()
		+ Algo::CountIf(Live->GetNotices(), [](const FCampusNotice& Card) { return Card.Exit.IsEmpty(); });
	if (Waiting <= 0)
	{
		// Zero footprint when there is nothing waiting. No reserved blank space "just in case".
		return SNullWidget::NullWidget;
	}
	return SNew(SBox).WidthOverride(26.f).HeightOverride(26.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(7.f))
			// RED, not green: this is "something needs you", not "all is well".
			.Fill(CampusAngular::Danger)
			.Border(FLinearColor(1.f, 1.f, 1.f, 0.4f))
			.BorderWidth(1.f)
			[
				SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::AsNumber(Waiting)).Font(CampusAngular::Numeric(15))
					.ColorAndOpacity(FLinearColor::White)
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildSocialStatus()
{
	UCampusSocial* Live = Social();
	if (!Live)
	{
		return SNullWidget::NullWidget;
	}
	const bool bDown = !Live->IsLive();
	const bool bRetryable = Live->GetState() == ECampusSocialState::GaveUp;
	// One honest line, never a modal: the room-code path keeps working with zero social features, and
	// that is exactly why it was not deleted.
	TSharedRef<SWidget> Line = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Wifi).Size(18.f)
				.Color(bDown ? CampusAngular::Danger : CampusAngular::Go)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Live->StatusLabel()).Font(CampusAngular::Display(14))
			.ColorAndOpacity(bDown ? CampusAngular::Danger : CampusAngular::TextMuted)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Live->GetRoster().bDegraded
				? Label(LOCTEXT("PresenceDegraded", "STATUS MAY BE OUT OF DATE"), 12, CampusAngular::TextMuted, false)
				: SNullWidget::NullWidget
		];

	if (!bRetryable)
	{
		return SNew(SBox).HeightOverride(30.f)[Line];
	}
	return SNew(SBox).HeightOverride(48.f)
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Notched(10.f))
			.Fill(FLinearColor(0.16f, 0.03f, 0.03f, 0.6f))
			.Border(CampusAngular::Danger)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(14.f, 4.f))
			.OnClicked(FOnClicked::CreateLambda([this]()
			{
				if (UCampusSocial* Retry = Social())
				{
					Retry->Start();
				}
				Refresh();
				return FReply::Handled();
			}))
			[
				Line
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Friends

void SCampusLobbyWidget::OpenProfile(const FString& PlayerId)
{
	UCampusSocial* Live = Social();
	if (!Live)
	{
		return;
	}
	ProfileTarget = PlayerId;
	ProfileTab = EProfileTab::Profile;
	// Paint the frame first and fill it when the one request lands - the page is never a blank screen.
	ProfileData.Reset();
	bProfileLoading = true;
	Show(EScreen::Profile);
	// Weak, for the reason spelled out in FriendButton at the top of this file.
	TWeakPtr<SWidget> Weak = AsWeak();
	Live->LoadProfile(PlayerId, [this, Weak](bool bOk, const FString& Error, const FCampusProfile& Loaded)
	{
		if (!Weak.IsValid())
		{
			return;
		}
		bProfileLoading = false;
		if (bOk)
		{
			ProfileData = MakeShared<FCampusProfile>(Loaded);
		}
		else
		{
			Toast(Error.IsEmpty() ? TEXT("Could not load that profile.") : Error);
		}
		if (Current == EScreen::Profile)
		{
			Refresh();
		}
	});
}

void SCampusLobbyWidget::SendInvite(const FString& PlayerId)
{
	UCampusSocial* Live = Social();
	if (!Live)
	{
		return;
	}
	Busy(TEXT("Inviting..."));
	// Solo is the absence of a party, so an invite from a Solo lobby opens a Squad - which is what a
	// player who just tapped "+ INVITE" plainly meant.
	const int32 Mode = SelectedTeamSize == 2 ? 2 : 4;
	// Weak, for the reason spelled out in FriendButton at the top of this file.
	TWeakPtr<SWidget> Weak = AsWeak();
	Live->InvitePlayer(PlayerId, Mode, [this, Weak](bool bOk, const FString& Error)
	{
		if (!Weak.IsValid())
		{
			return;
		}
		bBusy = false;
		if (!bOk)
		{
			Toast(Error);
		}
		Refresh();
	});
}

void SCampusLobbyWidget::AnswerInvite(const FString& InviteId, bool bAccept)
{
	UCampusSocial* Live = Social();
	if (!Live)
	{
		return;
	}
	if (!bAccept)
	{
		// The card slides out at once; the request behind it is incidental.
		Live->ResolveNotice(InviteId, TEXT("REJECTED"));
		Live->RejectInvite(InviteId, nullptr);
		return;
	}

	FString SenderName;
	if (const FCampusNotice* Card = Live->FindNotice(InviteId))
	{
		SenderName = Card->Sender.Name.ToUpper();
	}
	// Accepting while already in a squad is a confirm, never a silent auto-leave: quietly abandoning a
	// friend is the kind of thing players remember.
	// RAW `this` IS CORRECT HERE, and it is the only one left in this file: Send is a local helper
	// called synchronously below, not a callback anybody holds. Everything it hands to UCampusSocial
	// carries a weak pointer of its own.
	auto Send = [this, InviteId, SenderName](bool bLeaveCurrent)
	{
		UCampusSocial* Live2 = Social();
		if (!Live2)
		{
			return;
		}
		// Weak, for the reason spelled out in FriendButton at the top of this file.
		TWeakPtr<SWidget> Weak = AsWeak();
		Live2->AcceptInvite(InviteId, bLeaveCurrent,
			[this, Weak, InviteId, SenderName, bLeaveCurrent](bool bOk, const FString& Error, const FString& Code)
		{
			UCampusSocial* Live3 = Social();
			if (!Weak.IsValid() || !Live3)
			{
				return;
			}
			if (bOk)
			{
				Live3->ResolveNotice(InviteId, TEXT("JOINED"));
				Toast(FString::Printf(TEXT("YOU JOINED %s'S SQUAD."), *SenderName));
				Refresh();
				return;
			}
			// Matched on the CODE, never on the message: the server's wording is not a contract.
			if (!bLeaveCurrent && Code == TEXT("IN_OTHER_PARTY"))
			{
				// 409 IN_ANOTHER_PARTY: ask, then re-post with leave_current.
				AskConfirm(LOCTEXT("SwitchSquadTitle", "LEAVE YOUR SQUAD?"),
					FText::Format(LOCTEXT("SwitchSquadBody", "LEAVE YOUR SQUAD AND JOIN {0}?"), FText::FromString(SenderName)),
					LOCTEXT("SwitchSquadYes", "LEAVE AND JOIN"),
					[this, InviteId]()
					{
						if (UCampusSocial* Again = Social())
						{
							TWeakPtr<SWidget> Weak2 = AsWeak();
							Again->AcceptInvite(InviteId, true,
								[this, Weak2, InviteId](bool bOk2, const FString& Error2, const FString&)
							{
								if (!Weak2.IsValid())
								{
									return;
								}
								if (UCampusSocial* Live4 = Social())
								{
									Live4->ResolveNotice(InviteId, bOk2 ? TEXT("JOINED") : TEXT("TOO LATE"));
								}
								if (!bOk2)
								{
									Toast(Error2);
								}
								Refresh();
							});
						}
					});
				if (FCampusNotice* Pending = Live3->FindNotice(InviteId))
				{
					Pending->bBusy = false;
				}
				return;
			}
			// Anything else is terminal for this card: name the reason as it leaves.
			Live3->ResolveNotice(InviteId, TEXT("TOO LATE"));
			Toast(Error);
			Refresh();
		});
	};
	Send(false);
}

// ---------------------------------------------------------------------------------------------
// The friends screen, laid out to the owner's Free Fire reference:
//   left vertical tab rail (active tab = solid accent with dark text), a yellow REQUESTS button in
//   its bottom corner, the "GAME FRIENDS 2 / 100" counter top-right, rows of
//   [square avatar][angular name banner + LV badge][status, centred][action], and - on the ADD tab -
//   a search field pinned along the bottom.
//
// It is a FULL SCREEN, not a Dialog(): the reference's rail runs the whole height, and a centred
// 940-wide panel cannot hold a 240 rail, a row list and a bottom search bar without squeezing all
// three.

TSharedRef<SWidget> SCampusLobbyWidget::BuildNameBanner(const FCampusPlayerCard& Card, float Width)
{
	// The reference's banner: one angular plate carrying the name, with the level pinned to its far
	// end. One shape, so a long name and a short one still line up down the list.
	return SNew(SBox).WidthOverride(Width).HeightOverride(50.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Wedge(12.f, 8.f))
			.Fill(FLinearColor(0.03f, 0.04f, 0.06f, 0.92f))
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.AccentEdge(Card.Color)
			.AccentEdgeWidth(4.f)
			.ContentPadding(FMargin(14.f, 4.f, 10.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(Card.Name.ToUpper()))
					.Font(CampusAngular::Display(20)).ColorAndOpacity(FLinearColor::White)
					.Clipping(EWidgetClipping::ClipToBounds)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Slanted(6.f))
					.Fill(FLinearColor(0.10f, 0.09f, 0.02f, 0.92f))
					.ContentPadding(FMargin(8.f, 1.f))
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("BannerLevel", "LV {0}"), FText::AsNumber(Card.Level)))
						.Font(CampusAngular::Numeric(13)).ColorAndOpacity(CampusAngular::Accent)
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildFriendRow(const FCampusPlayerCard& Card, EFriendRow Kind)
{
	UCampusSocial* Live = Social();
	const FString Who = Card.PlayerId;
	const FDateTime ServerNow = Live ? Live->Now() : FDateTime();
	const bool bRequested = OptimisticFriends.Contains(Who);

	// ---- the trailing action, one per row kind
	TSharedRef<SWidget> Action = SNullWidget::NullWidget;
	switch (Kind)
	{
	case EFriendRow::Add:
	{
		// The reference's "+" square, which turns into a greyed tick once the request is away.
		const bool bDone = bRequested || Card.FriendState == TEXT("outgoing") || Card.FriendState == TEXT("friends");
		const bool bIncoming = Card.FriendState == TEXT("incoming");
		Action = SNew(SBox).WidthOverride(56.f).HeightOverride(56.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Clipped(10.f))
				.Fill(bDone ? FLinearColor(1.f, 1.f, 1.f, 0.07f) : (bIncoming ? CampusAngular::Go : FLinearColor(0.10f, 0.09f, 0.02f, 0.92f)))
				.Border(bDone ? CampusAngular::Hairline : CampusAngular::Accent)
				.BorderWidth(1.f)
				.OnClicked(FOnClicked::CreateLambda([this, Who, bDone, bIncoming]()
				{
					UCampusSocial* Live2 = Social();
					if (!Live2 || bDone)
					{
						return FReply::Handled();
					}
					if (bIncoming)
					{
						TWeakPtr<SWidget> Weak = AsWeak();
						Live2->AcceptFriend(Who, [this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
						return FReply::Handled();
					}
					// Optimistic: the "+" becomes the tick at once and reverts with a toast on failure.
					OptimisticFriends.Add(Who);
					Refresh();
					TWeakPtr<SWidget> Weak = AsWeak();
					Live2->RequestFriend(Who, [this, Weak, Who](bool bOk, const FString& Why)
					{
						if (!Weak.IsValid())
						{
							return;
						}
						if (!bOk)
						{
							OptimisticFriends.Remove(Who);
							Toast(Why);
						}
						Refresh();
					});
					return FReply::Handled();
				}))
				[
					SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SCampusGlyph)
						.Glyph(bDone ? CampusAngular::EGlyph::Tick : CampusAngular::EGlyph::Plus)
						.Size(24.f)
						.Color(bDone ? CampusAngular::TextMuted : CampusAngular::Accent)
					]
				]
			];
		break;
	}
	case EFriendRow::Incoming:
		Action = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(LOCTEXT("AcceptFriend", "ACCEPT"), FOnClicked::CreateLambda([this, Who]()
				{
					if (UCampusSocial* Live2 = Social())
					{
						TWeakPtr<SWidget> Weak = AsWeak();
						Live2->AcceptFriend(Who, [this, Weak](bool bOk, const FString& Error)
						{
							if (!Weak.IsValid())
							{
								return;
							}
							if (!bOk) { Toast(Error); }
							Refresh();
						});
					}
					return FReply::Handled();
				}), EButton::Go, 16, FVector2D(140.f, 50.f))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
			[
				Button(LOCTEXT("Decline", "DECLINE"), FOnClicked::CreateLambda([this, Who]()
				{
					if (UCampusSocial* Live2 = Social())
					{
						TWeakPtr<SWidget> Weak = AsWeak();
						Live2->DeclineFriend(Who, [this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
					}
					return FReply::Handled();
				}), EButton::Ghost, 15, FVector2D(140.f, 50.f))
			];
		break;
	case EFriendRow::Outgoing:
		Action = Button(LOCTEXT("CancelRequest", "CANCEL"), FOnClicked::CreateLambda([this, Who]()
		{
			if (UCampusSocial* Live2 = Social())
			{
				TWeakPtr<SWidget> Weak = AsWeak();
				Live2->RemoveFriend(Who, [this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
			}
			return FReply::Handled();
		}), EButton::Ghost, 15, FVector2D(160.f, 50.f));
		break;
	case EFriendRow::Blocked:
		Action = Button(LOCTEXT("Unblock", "UNBLOCK"), FOnClicked::CreateLambda([this, Who]()
		{
			if (UCampusSocial* Live2 = Social())
			{
				TWeakPtr<SWidget> Weak = AsWeak();
				Live2->UnblockPlayer(Who, [this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
			}
			return FReply::Handled();
		}), EButton::Ghost, 15, FVector2D(160.f, 50.f));
		break;
	default:
	{
		// GAME FRIENDS. The owner's call, with the server as the backstop: a friend in a match cannot
		// be invited at all, so the button says why instead of pretending and then showing a 409.
		const FVector2D Size(168.f, 50.f);
		const bool bLeadOrFree = Live && (!Live->HasParty() || (Live->IsPartyLeader() && Live->GetServerParty().HasRoom()));
		if (!Card.PendingInviteId.IsEmpty())
		{
			Action = Button(LOCTEXT("Invited", "INVITED"), FOnClicked(), EButton::Ghost, 15, Size, false);
		}
		else if (Card.IsInMatch())
		{
			Action = Button(LOCTEXT("InMatchBtn", "IN MATCH"), FOnClicked(), EButton::Ghost, 15, Size, false);
		}
		else if (Card.bInParty)
		{
			Action = Button(LOCTEXT("InSquadBtn", "IN A SQUAD"), FOnClicked(), EButton::Ghost, 14, Size, false);
		}
		else if (Live && Live->HasParty() && !Live->IsPartyLeader())
		{
			Action = Button(LOCTEXT("LeaderInvites", "LEADER INVITES"), FOnClicked(), EButton::Ghost, 13, Size, false);
		}
		else if (Live && Live->HasParty() && !Live->GetServerParty().HasRoom())
		{
			Action = Button(LOCTEXT("SquadFull", "SQUAD FULL"), FOnClicked(), EButton::Ghost, 14, Size, false);
		}
		else if (bLeadOrFree)
		{
			Action = Button(LOCTEXT("InviteBtn", "+ INVITE"), FOnClicked::CreateLambda([this, Who]()
			{
				SendInvite(Who);
				return FReply::Handled();
			}), EButton::Accent, 16, Size);
		}
		break;
	}
	}

	const FText Status = Card.LastSeenLabel(ServerNow);
	const FLinearColor StatusInk = Card.IsOnline() ? CampusAngular::Accent : CampusAngular::TextMuted;
	// Why this stranger is on screen. A suggestion with no reason is just a stranger.
	FText Why;
	if (Card.Reason == TEXT("played_together"))
	{
		Why = LOCTEXT("WhyPlayed", "PLAYED TOGETHER");
	}
	else if (Card.Reason == TEXT("same_college"))
	{
		Why = LOCTEXT("WhyCollege", "SAME COLLEGE");
	}
	else if (Card.MatchesTogether > 0)
	{
		Why = FText::Format(LOCTEXT("WhyMatches", "{0} MATCHES TOGETHER"), FText::AsNumber(Card.MatchesTogether));
	}

	// The whole row left of the action is one tap target to the profile - the reference opens a
	// player card the same way, and it keeps the only destructive control (the action) separate.
	return SNew(SBox).HeightOverride(72.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Notched(8.f))
				// Dark enough to read as a row over the dimmed lobby, light enough that the primitive's
				// hover / press lift is actually visible on it.
				.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.55f))
				.Border(FLinearColor(1.f, 1.f, 1.f, 0.10f))
				.BorderWidth(1.f)
				.ContentPadding(FMargin(10.f, 6.f))
				.OnClicked(FOnClicked::CreateLambda([this, Who]()
				{
					OpenProfile(Who);
					return FReply::Handled();
				}))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
					[
						Avatar(Card, 56.f)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						BuildNameBanner(Card, 440.f)
					]
					// The status column sits in the middle of the row, exactly as the reference does,
					// so the eye reads name -> when -> what can I do, left to right.
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(24.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(Status).Font(CampusAngular::Display(15))
							.ColorAndOpacity(StatusInk).Justification(ETextJustify::Center)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock).Text(Why).Font(CampusAngular::Display(11))
							.ColorAndOpacity(CampusAngular::TextMuted).Justification(ETextJustify::Center)
							.Visibility(Why.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
						]
					]
					// The readable account number, so a player can repeat it to a friend. Hidden when
					// the server has not grown public_id yet rather than showing a raw uuid.
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						Card.PublicId.IsEmpty()
							? SNullWidget::NullWidget
							: StaticCastSharedRef<SWidget>(SNew(STextBlock)
								.Text(FText::FromString(Card.PrettyPublicId()))
								.Font(CampusAngular::Numeric(13)).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.35f)))
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
			[
				Action
			]
		];
}

namespace
{
	/** The empty state that does the recruiting, rather than an apology. */
	TSharedRef<SWidget> EmptyState(const FText& Line, const FText& Hint)
	{
		return SNew(SBox).HeightOverride(240.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SBox).WidthOverride(46.f).HeightOverride(46.f)
					[
						SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Friends).Size(46.f)
						.Color(FLinearColor(1.f, 1.f, 1.f, 0.22f))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(STextBlock).Text(Line).Font(CampusAngular::Display(20))
					.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.7f)).Justification(ETextJustify::Center)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(SBox).MaxDesiredWidth(640.f)
					[
						SNew(STextBlock).Text(Hint).Font(CampusAngular::Body(15))
						.ColorAndOpacity(CampusAngular::TextMuted).Justification(ETextJustify::Center)
						.AutoWrapText(true)
					]
				]
			];
	}
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildFriendTab(EFriendTab Tab, const FText& Label)
{
	// Active = a SOLID accent plate with dark text, straight off the reference. Inactive is bare
	// text on the rail, which is what makes the active one impossible to mistake.
	const bool bSelected = FriendTab == Tab;
	return SNew(SBox).HeightOverride(76.f)
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Tab(14.f, 0.f))
			.Fill(bSelected ? CampusAngular::Accent : FLinearColor(0.f, 0.f, 0.f, 0.f))
			.Border(FLinearColor::Transparent)
			.BorderWidth(0.f)
			.ContentPadding(FMargin(26.f, 4.f, 14.f, 4.f))
			.OnClicked(FOnClicked::CreateLambda([this, Tab]()
			{
				FriendTab = Tab;
				if (UCampusSocial* Live = Social())
				{
					// The ADD tab suggests people before anything is typed; that list is fetched the
					// first time the tab is opened and never polled.
					if (Tab == EFriendTab::Add && !Live->GetRoster().bRecentLoaded)
					{
						TWeakPtr<SWidget> Weak = AsWeak();
						Live->RefreshRecent([this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
					}
					if (Tab != EFriendTab::Add)
					{
						Live->ClearSearch();
					}
				}
				Refresh();
				return FReply::Handled();
			}))
			[
				SNew(SBox).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Label).Font(CampusAngular::Display(21))
					.ColorAndOpacity(bSelected ? FLinearColor(0.06f, 0.05f, 0.02f, 1.f) : FLinearColor(1.f, 1.f, 1.f, 0.72f))
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildFriends()
{
	UCampusSocial* Live = Social();
	const FCampusRoster Blank;
	const FCampusRoster& List = Live ? Live->GetRoster() : Blank;
	const int32 Waiting = List.Incoming.Num();

	// ---- the left rail
	TSharedRef<SVerticalBox> Rail = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[BuildFriendTab(EFriendTab::Friends, LOCTEXT("TabGame", "GAME FRIENDS"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			BuildFriendTab(EFriendTab::Add, LOCTEXT("TabAdd", "ADD"))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
		// The yellow REQUESTS button in the rail's bottom corner, with the count that makes it worth
		// looking at. It is a TAB, not a separate screen - one place, one back button.
		+ SVerticalBox::Slot().AutoHeight().Padding(18.f, 0.f, 0.f, 6.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				Button(LOCTEXT("TabRequests", "REQUESTS"), FOnClicked::CreateLambda([this]()
				{
					FriendTab = EFriendTab::Requests;
					Refresh();
					return FReply::Handled();
				}), FriendTab == EFriendTab::Requests ? EButton::Dark : EButton::Accent, 19, FVector2D(206.f, 64.f))
			]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, -8.f, -8.f, 0.f)
			[
				Waiting > 0
					? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(28.f).HeightOverride(28.f)
						[
							SNew(SCampusAngularPanel)
							.Shape(FCampusShape::Notched(7.f))
							.Fill(CampusAngular::Danger)
							.Border(FLinearColor(1.f, 1.f, 1.f, 0.45f))
							.BorderWidth(1.f)
							[
								SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(FText::AsNumber(Waiting))
									.Font(CampusAngular::Numeric(15)).ColorAndOpacity(FLinearColor::White)
								]
							]
						])
					: SNullWidget::NullWidget
			]
		];

	// ---- the rows for whichever tab is open
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	int32 Count = 0;
	auto Add = [&Rows, &Count, this](const TArray<FCampusPlayerCard>& Cards, EFriendRow Kind)
	{
		for (const FCampusPlayerCard& Card : Cards)
		{
			Rows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[BuildFriendRow(Card, Kind)];
			++Count;
		}
	};

	FText Counter;
	switch (FriendTab)
	{
	case EFriendTab::Add:
		// Search results when there are any, the suggested list before anything is typed.
		if (List.bSearched)
		{
			Add(List.Results, EFriendRow::Add);
			Counter = FText::Format(LOCTEXT("AddResults", "RESULTS FOR “{0}”"), FText::FromString(List.SearchTerm.ToUpper()));
			if (Count == 0)
			{
				Rows->AddSlot().AutoHeight()
				[
					EmptyState(LOCTEXT("NoResults", "NOBODY FOUND"),
						LOCTEXT("NoResultsHint", "Check the ID or the nickname and try again. An ID is the number on a player's profile."))
				];
			}
		}
		else
		{
			Add(List.Recent, EFriendRow::Add);
			Counter = LOCTEXT("AddSuggested", "PLAYERS YOU PLAYED WITH");
			if (Count == 0)
			{
				Rows->AddSlot().AutoHeight()
				[
					EmptyState(LOCTEXT("NoSuggested", "NOBODY TO SUGGEST YET"),
						LOCTEXT("NoSuggestedHint", "Play a match, or search a nickname or ID below."))
				];
			}
		}
		break;
	case EFriendTab::Requests:
		Add(List.Incoming, EFriendRow::Incoming);
		if (List.Outgoing.Num() > 0)
		{
			Rows->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 6.f)
			[
				Label(LOCTEXT("SentHeading", "SENT BY YOU"), 15, CampusAngular::TextMuted)
			];
			Add(List.Outgoing, EFriendRow::Outgoing);
		}
		Counter = FText::Format(LOCTEXT("RequestCount", "REQUESTS  {0}"), FText::AsNumber(Waiting));
		if (Count == 0)
		{
			Rows->AddSlot().AutoHeight()
			[
				EmptyState(LOCTEXT("NoRequests", "NO REQUESTS"),
					LOCTEXT("NoRequestsHint", "Anyone who asks to be your friend shows up here."))
			];
		}
		break;
	default:
		Add(List.Friends, EFriendRow::Game);
		if (List.Blocked.Num() > 0)
		{
			// The only place a block can be undone from: a blocked player is gone from every other list.
			Rows->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 6.f)
			[
				Label(LOCTEXT("BlockedHeading", "BLOCKED"), 15, CampusAngular::TextMuted)
			];
			Add(List.Blocked, EFriendRow::Blocked);
		}
		Counter = FText::Format(LOCTEXT("GameFriendCount", "GAME FRIENDS  {0} / {1}"),
			FText::AsNumber(List.FriendsUsed), FText::AsNumber(List.FriendsMax));
		if (Count == 0)
		{
			Rows->AddSlot().AutoHeight()
			[
				EmptyState(LOCTEXT("NoFriends", "NO FRIENDS YET"),
					LOCTEXT("NoFriendsHint", "Open ADD and search a nickname or ID — or add the people you just played with."))
			];
		}
		break;
	}

	// ---- the bottom search bar, ADD tab only
	TSharedRef<SWidget> Bottom = SNullWidget::NullWidget;
	if (FriendTab == EFriendTab::Add)
	{
		auto RunSearch = [this]()
		{
			UCampusSocial* Live2 = Social();
			if (!Live2)
			{
				return;
			}
			if (SearchText.TrimStartAndEnd().IsEmpty())
			{
				Live2->ClearSearch();
				Refresh();
				return;
			}
			Busy(TEXT("Searching..."));
			TWeakPtr<SWidget> Weak = AsWeak();
			Live2->SearchPlayers(SearchText, [this, Weak](bool bOk, const FString& Error)
			{
				if (!Weak.IsValid())
				{
					return;
				}
				bBusy = false;
				if (!bOk)
				{
					Toast(Error.IsEmpty() ? TEXT("Could not search right now.") : Error);
				}
				Refresh();
			});
		};
		Bottom = SNew(SHorizontalBox)
			// Refresh: re-reads the suggestions and drops the current result set. A WORD, not an icon -
			// there is no glyph in this set that unambiguously means "refresh", and a mystery button
			// next to a search field is worse than one more short label.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
			[
				Button(LOCTEXT("SearchReset", "REFRESH"), FOnClicked::CreateLambda([this]()
				{
					if (UCampusSocial* Live2 = Social())
					{
						SearchText.Reset();
						Live2->ClearSearch();
						TWeakPtr<SWidget> Weak = AsWeak();
						Live2->RefreshRecent([this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
					}
					Refresh();
					return FReply::Handled();
				}), EButton::Ghost, 14, FVector2D(130.f, 64.f))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SBox).MaxDesiredWidth(560.f)
				[
					Field(LOCTEXT("SearchHint", "Input nickname or ID to search"), SearchText)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
			[
				// One request per tap, never per keystroke: a search-as-you-type field on a tunnel
				// metered at 20,000 requests a month would empty it in an afternoon.
				Button(LOCTEXT("SearchBtn", "SEARCH"), FOnClicked::CreateLambda([this, RunSearch]()
				{
					RunSearch();
					return FReply::Handled();
				}), EButton::Accent, 18, FVector2D(170.f, 64.f))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				// Your own number, so "what's your ID?" is answered without leaving the screen.
				Live && !Live->MyPublicId().IsEmpty()
					? Button(FText::Format(LOCTEXT("MyIdBtn", "MY ID  {0}"),
						FText::FromString(Live->MyPublicId())), FOnClicked::CreateLambda([this]()
						{
							if (UCampusSocial* Live2 = Social())
							{
								FPlatformApplicationMisc::ClipboardCopy(*Live2->MyPublicId());
								Toast(TEXT("YOUR ID IS COPIED — SEND IT TO A FRIEND."));
							}
							return FReply::Handled();
						}), EButton::Ghost, 15, FVector2D(300.f, 64.f))
					: SNullWidget::NullWidget
			];
	}

	// 0.72, not 0.88: the reference keeps the lobby visible and blurred behind the list, and a scrim
	// that reads as pure black makes the long empty area under the rows look like a broken screen.
	// The panel-less rows still clear 4.5:1 against this, because they carry their own plate fill.
	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.72f)]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(24.f, 16.f))
			[
				SNew(SVerticalBox)
				// Top bar: the title and the orange X at the far right, as the reference has it.
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 18.f, 0.f)
					[
						Label(LOCTEXT("FriendsTitle", "FRIENDS"), 30, FLinearColor::White)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close, FOnClicked::CreateLambda([this]()
						{
							Show(FriendsReturn);
							return FReply::Handled();
						}), 64.f, CampusAngular::AccentDeep)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 10.f, 8.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[BuildSocialStatus()]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Label(Counter, 15, CampusAngular::TextMuted)
					]
				]
				+ SVerticalBox::Slot().FillHeight(1.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox).WidthOverride(244.f)[Rail]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).Padding(16.f, 0.f, 0.f, 0.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(SScrollBox) + SScrollBox::Slot()[Rows]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)[Bottom]
					]
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildFriendPick()
{
	// The "+ INVITE" plate's picker: the same rows, in a dialog, with nothing but the squad in mind.
	UCampusSocial* Live = Social();
	const FCampusRoster Blank;
	const FCampusRoster& List = Live ? Live->GetRoster() : Blank;

	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	int32 Count = 0;
	for (const FCampusPlayerCard& Card : List.Friends)
	{
		if (Card.FriendState == TEXT("blocked"))
		{
			continue;
		}
		Rows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[BuildFriendRow(Card, EFriendRow::Game)];
		++Count;
	}
	if (Count == 0)
	{
		Rows->AddSlot().AutoHeight()
		[
			EmptyState(LOCTEXT("PickEmpty", "NO FRIENDS YET"),
				LOCTEXT("PickEmptyHint", "Add the people you play with after a match. Until then, a room code still works."))
		];
		Rows->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)
		[
			Button(LOCTEXT("PickAdd", "FIND PLAYERS"), FOnClicked::CreateLambda([this]()
			{
				FriendTab = EFriendTab::Add;
				FriendsReturn = EScreen::Lobby;
				if (UCampusSocial* Live2 = Social(); Live2 && !Live2->GetRoster().bRecentLoaded)
				{
					TWeakPtr<SWidget> Weak = AsWeak();
					Live2->RefreshRecent([this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
				}
				Show(EScreen::Friends);
				return FReply::Handled();
			}), EButton::Go, 17, FVector2D(300.f, 56.f))
		];
	}

	const FCampusServerParty Squad = Live ? Live->GetServerParty() : FCampusServerParty();
	const FText Slots = Squad.bValid
		? FText::Format(LOCTEXT("PickSlots", "{0}  ·  {1} / {2} FILLED"), ModeName(Squad.Mode),
			FText::AsNumber(Squad.Occupancy()), FText::AsNumber(Squad.Mode))
		: FText::Format(LOCTEXT("PickSlotsNew", "{0}  ·  YOUR SQUAD STARTS WITH THIS INVITE"), ModeName(SelectedTeamSize));

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Label(Slots, 17, FLinearColor::White)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)[BuildSocialStatus()]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[Rows]
		];
	return Dialog(LOCTEXT("PickTitle", "INVITE A FRIEND"), Body, 1100.f, true, EScreen::Lobby);
}

// ---------------------------------------------------------------------------------------------
// The lobby's right-edge friends strip (the reference's pre-START invite rail)

TSharedRef<SWidget> SCampusLobbyWidget::BuildLobbyFriends()
{
	UCampusSocial* Live = Social();
	// Solo means playing alone, so the invite rail has nothing to say: zero footprint, not an empty frame.
	if (!Live || SelectedTeamSize <= 1 || !Live->GetRoster().bValid)
	{
		return SNullWidget::NullWidget;
	}
	const TArray<FCampusPlayerCard>& Friends = Live->GetRoster().Friends;
	const int32 Online = Algo::CountIf(Friends, [](const FCampusPlayerCard& Card) { return Card.IsOnline(); });

	// Collapsed by default: the notice stack owns this corner when a card is in flight, and a strip
	// that is always open would spend the player's screen on a list they did not ask for.
	TSharedRef<SWidget> Header = SNew(SBox).HeightOverride(52.f)
		[
			SNew(SCampusAngularButton)
			.Shape(FCampusShape::Notched(10.f))
			.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.86f))
			.Border(bLobbyFriendsOpen ? CampusAngular::Accent : CampusAngular::Hairline)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(12.f, 4.f))
			.OnClicked(FOnClicked::CreateLambda([this]()
			{
				bLobbyFriendsOpen = !bLobbyFriendsOpen;
				if (bLobbyFriendsOpen)
				{
					if (UCampusSocial* Live2 = Social())
					{
						TWeakPtr<SWidget> Weak = AsWeak();
						Live2->RefreshFriends([this, Weak](bool, const FString&)
						{
							if (Weak.IsValid() && Current == EScreen::Lobby) { Refresh(); }
						});
					}
				}
				Refresh();
				return FReply::Handled();
			}))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)
					[
						SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Friends).Size(22.f).Color(CampusAngular::Accent)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("OnlineCount", "ONLINE  {0}"), FText::AsNumber(Online)))
					.Font(CampusAngular::Display(15)).ColorAndOpacity(FLinearColor::White)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
					[
						SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Chevron).Size(16.f)
						.Color(CampusAngular::TextMuted)
					]
				]
			]
		];

	if (!bLobbyFriendsOpen)
	{
		return SNew(SBox).WidthOverride(236.f)[Header];
	}

	// EVERY friend, not the first six: the old cap simply hid people on a full roster, with nothing on
	// screen to say so. The rows go in a scroll box below, capped in height.
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	int32 Shown = 0;
	for (const FCampusPlayerCard& Card : Friends)
	{
		++Shown;
		const FString Who = Card.PlayerId;
		const bool bInvitable = !Card.IsInMatch() && !Card.bInParty && Card.PendingInviteId.IsEmpty()
			&& (!Live->HasParty() || (Live->IsPartyLeader() && Live->GetServerParty().HasRoom()));
		Rows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
		[
			SNew(SBox).HeightOverride(52.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Notched(8.f))
					.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.78f))
					.Border(FLinearColor(1.f, 1.f, 1.f, 0.10f))
					.BorderWidth(1.f)
					.AccentEdge(Card.StatusColor())
					.AccentEdgeWidth(3.f)
					.ContentPadding(FMargin(8.f, 2.f))
					.OnClicked(FOnClicked::CreateLambda([this, Who]()
					{
						OpenProfile(Who);
						return FReply::Handled();
					}))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
						[
							Avatar(Card, 34.f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(FText::FromString(Card.Name.ToUpper()))
								.Font(CampusAngular::Display(15)).ColorAndOpacity(FLinearColor::White)
								.Clipping(EWidgetClipping::ClipToBounds)
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("StripLine", "LV {0}  ·  {1}"),
									FText::AsNumber(Card.Level), Card.StatusLabel()))
								.Font(CampusAngular::Display(11)).ColorAndOpacity(Card.StatusColor())
							]
						]
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox).WidthOverride(48.f).HeightOverride(48.f).IsEnabled(bInvitable)
					[
						SNew(SCampusAngularButton)
						.Shape(FCampusShape::Clipped(8.f))
						.Fill(bInvitable ? FLinearColor(0.10f, 0.09f, 0.02f, 0.92f) : FLinearColor(1.f, 1.f, 1.f, 0.05f))
						.Border(bInvitable ? CampusAngular::Accent : CampusAngular::Hairline)
						.BorderWidth(1.f)
						.OnClicked(FOnClicked::CreateLambda([this, Who]()
						{
							SendInvite(Who);
							return FReply::Handled();
						}))
						[
							SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
							[
								SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Plus).Size(20.f)
								.Color(bInvitable ? CampusAngular::Accent : CampusAngular::TextMuted)
							]
						]
					]
				]
			]
		];
	}
	TSharedRef<SVerticalBox> Strip = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Header];
	if (Shown == 0)
	{
		Strip->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			Label(LOCTEXT("StripEmpty", "NO FRIENDS YET"), 13, CampusAngular::TextMuted)
		];
	}
	else
	{
		Strip->AddSlot().AutoHeight()
		[
			// CAPPED, not fixed: three friends take three rows of space, thirty take the cap and
			// scroll. MaxDesiredHeight rather than HeightOverride, so a short list leaves no hole.
			SNew(SBox).MaxDesiredHeight(StripScrollHeight)
			[
				// The same bare SScrollBox the friends screen and the invite picker already use, which
				// is the form that is proven to drag on the phone. No visible bar: there is no room
				// for one at 236 wide, and on a phone the drag IS the affordance.
				//
				// The TAPS still get through. SScrollBox claims a touch in OnPreviewMouseButtonDown
				// but only keeps it once the finger has travelled past the drag threshold, so a clean
				// tap reaches the row's own button and the "+" invite beside it exactly as before.
				SNew(SScrollBox)
				.ScrollBarVisibility(EVisibility::Collapsed)
				+ SScrollBox::Slot()
				[
					Rows
				]
			]
		];
	}
	Strip->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
	[
		Button(LOCTEXT("StripAll", "ALL FRIENDS"), FOnClicked::CreateLambda([this]()
		{
			FriendTab = EFriendTab::Friends;
			FriendsReturn = EScreen::Lobby;
			Show(EScreen::Friends);
			return FReply::Handled();
		}), EButton::Ghost, 13, FVector2D(236.f, 44.f))
	];
	return SNew(SBox).WidthOverride(236.f)[Strip];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildInviteTray()
{
	UCampusSocial* Live = Social();
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	int32 Count = 0;
	if (Live)
	{
		for (const FCampusNotice& Notice : Live->GetNotices())
		{
			if (!Notice.Exit.IsEmpty())
			{
				continue;
			}
			const FString Key = Notice.Key;
			const bool bInvite = Notice.Kind == ECampusNoticeKind::Invite;
			const int32 Left = Live->SecondsUntil(Notice.ExpiresAt);
			Rows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SBox).HeightOverride(RowHeight)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Notched(10.f))
					.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.72f))
					.Border(CampusAngular::Hairline)
					.BorderWidth(1.f)
					.ContentPadding(FMargin(12.f, 6.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
						[
							Avatar(Notice.Sender, 44.f)
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(FText::FromString(Notice.Sender.Name.ToUpper()))
								.Font(CampusAngular::Display(20)).ColorAndOpacity(FLinearColor::White)
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
								.Text(bInvite
									? FText::Format(LOCTEXT("TrayInvite", "{0}  ·  {1}S LEFT"), ModeName(Notice.Mode), FText::AsNumber(Left))
									: LOCTEXT("TrayFriend", "FRIEND REQUEST"))
								.Font(CampusAngular::Display(13)).ColorAndOpacity(CampusAngular::TextMuted)
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth()
							[
								Button(LOCTEXT("TrayAccept", "ACCEPT"), FOnClicked::CreateLambda([this, Key, bInvite]()
								{
									if (bInvite)
									{
										AnswerInvite(Key, true);
									}
									else if (UCampusSocial* Live2 = Social())
									{
										Live2->ResolveNotice(Key, TEXT("ACCEPTED"));
										TWeakPtr<SWidget> Weak = AsWeak();
										Live2->AcceptFriend(Key, [this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
									}
									Refresh();
									return FReply::Handled();
								}), EButton::Go, 16, FVector2D(130.f, 48.f))
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
							[
								Button(LOCTEXT("TrayReject", "REJECT"), FOnClicked::CreateLambda([this, Key, bInvite]()
								{
									if (bInvite)
									{
										AnswerInvite(Key, false);
									}
									else if (UCampusSocial* Live2 = Social())
									{
										Live2->ResolveNotice(Key, TEXT("DECLINED"));
										TWeakPtr<SWidget> Weak = AsWeak();
										Live2->DeclineFriend(Key, [this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
									}
									Refresh();
									return FReply::Handled();
								}), EButton::Ghost, 16, FVector2D(130.f, 48.f))
							]
						]
					]
				]
			];
			++Count;
		}
	}
	if (Count == 0)
	{
		Rows->AddSlot().AutoHeight()
		[
			EmptyState(LOCTEXT("TrayEmpty", "NOTHING WAITING"), LOCTEXT("TrayEmptyHint", "Invites and friend requests land here."))
		];
	}
	return Dialog(LOCTEXT("TrayTitle", "INVITES"),
		SNew(SScrollBox) + SScrollBox::Slot()[Rows], 820.f, true, EScreen::Lobby);
}

// ---------------------------------------------------------------------------------------------
// The squad strip and the leader's controls

void SCampusLobbyWidget::OpenManageSquad()
{
	UCampusSocial* Live = Social();
	if (!Live || !Live->IsPartyLeader())
	{
		return;
	}
	const FCampusServerParty& Squad = Live->GetServerParty();
	const FString MyId = Live->Me();
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

	// MY OWN PLATE OFFERS "LEAVE SQUAD", NEVER "REMOVE". A leader removing themselves is not a kick, it
	// is a leave - and because the squad is theirs, it DISBANDS. Saying "REMOVE" on my own row would
	// promise a squad that carries on without me, which is not what the server does and not what the
	// owner wants. Keyed on MY ID and not on the crown: if a stale leader flag ever lands on the wrong
	// plate, the one row that must never offer a kick is still mine.
	for (const FCampusPartyMember& Member : Squad.Members)
	{
		if (MyId.IsEmpty() ? !Member.bLeader : Member.PlayerId != MyId)
		{
			continue;
		}
		Rows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SBox).HeightOverride(RowHeight)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						Label(FText::Format(LOCTEXT("ManageYou", "{0}  ·  YOU"), FText::FromString(Member.Name.ToUpper())), 22, CampusAngular::Accent)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						Label(LOCTEXT("ManageYouHint", "LEADER — LEAVING BREAKS UP THE SQUAD"), 13, CampusAngular::TextMuted)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Button(LOCTEXT("ManageLeave", "LEAVE SQUAD"), FOnClicked::CreateLambda([this]()
					{
						AskConfirm(LOCTEXT("DisbandTitle", "LEAVE AND BREAK UP THE SQUAD?"),
							LOCTEXT("DisbandBody", "THE SQUAD IS YOURS, SO IT ENDS WHEN YOU GO. EVERYONE GOES BACK TO PLAYING ON THEIR OWN."),
							LOCTEXT("DisbandYes", "LEAVE SQUAD"),
							[this]()
							{
								if (UCampusSocial* Live2 = Social())
								{
									TWeakPtr<SWidget> Weak = AsWeak();
									Live2->LeaveParty([this, Weak](bool bOk, const FString& Error)
									{
										if (!Weak.IsValid())
										{
											return;
										}
										if (!bOk)
										{
											Toast(Error);
										}
										Refresh();
									});
								}
							});
						return FReply::Handled();
					}), EButton::Ghost, 16, FVector2D(190.f, 50.f))
				]
			]
		];
	}

	for (const FCampusPartyMember& Member : Squad.Members)
	{
		// Everybody who is not me. The crown is not the test: see above.
		if (MyId.IsEmpty() ? Member.bLeader : Member.PlayerId == MyId)
		{
			continue;
		}
		const FString Who = Member.PlayerId;
		const FString Name = Member.Name;
		Rows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SBox).HeightOverride(RowHeight)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					Label(FText::FromString(Name.ToUpper()), 22, FLinearColor::White)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Button(LOCTEXT("RemoveMember", "REMOVE"), FOnClicked::CreateLambda([this, Who, Name]()
					{
						// Kick is a social act, so it is confirmed and never a stray tap.
						AskConfirm(LOCTEXT("RemoveTitle", "REMOVE FROM SQUAD"),
							FText::Format(LOCTEXT("RemoveBody", "REMOVE {0} FROM THE SQUAD?"), FText::FromString(Name.ToUpper())),
							LOCTEXT("RemoveYes", "REMOVE"),
							[this, Who]()
							{
								if (UCampusSocial* Live2 = Social())
								{
									TWeakPtr<SWidget> Weak = AsWeak();
									Live2->KickMember(Who, [this, Weak](bool bOk, const FString& Error)
									{
										if (!Weak.IsValid())
										{
											return;
										}
										if (!bOk)
										{
											Toast(Error);
										}
										Refresh();
									});
								}
							});
						return FReply::Handled();
					}), EButton::Ghost, 16, FVector2D(160.f, 50.f))
				]
			]
		];
	}
	for (const FCampusPartyMember& Member : Squad.Pending)
	{
		const FString InviteId = Member.InviteId;
		const FString Name = Member.Name;
		Rows->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SBox).HeightOverride(RowHeight)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Name.ToUpper()), 22, FLinearColor(1.f, 1.f, 1.f, 0.6f))]
					+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("StillInvited", "INVITED — WAITING"), 13, CampusAngular::TextMuted)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Button(LOCTEXT("CancelInvite", "CANCEL"), FOnClicked::CreateLambda([this, InviteId]()
					{
						if (UCampusSocial* Live2 = Social())
						{
							TWeakPtr<SWidget> Weak = AsWeak();
							Live2->CancelInvite(InviteId, [this, Weak](bool, const FString&) { if (Weak.IsValid()) { Refresh(); } });
						}
						CloseConfirm();
						return FReply::Handled();
					}), EButton::Ghost, 16, FVector2D(160.f, 50.f))
				]
			]
		];
	}

	ShowFloating(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
		[
			Label(LOCTEXT("ManageTitle", "YOUR SQUAD"), 28, FLinearColor::White)
		]
		+ SVerticalBox::Slot().AutoHeight()[Rows]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 10.f, 0.f, 0.f)
		[
			Button(LOCTEXT("ManageDone", "DONE"), FOnClicked::CreateLambda([this]()
			{
				CloseConfirm();
				return FReply::Handled();
			}), EButton::Dark, 17, FVector2D(170.f, 52.f))
		], 680.f);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildPartyBar()
{
	UCampusSocial* Live = Social();
	if (!Live || !Live->HasParty())
	{
		// Zero footprint when there is no squad. No reserved blank space "just in case".
		return SNullWidget::NullWidget;
	}
	const FCampusServerParty& Squad = Live->GetServerParty();
	const bool bLeader = Live->IsPartyLeader();
	const FString LeaderName = Squad.LeaderName().ToUpper();

	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
		[
			SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Crown).Size(18.f).Color(CampusAngular::Accent)
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Label(bLeader
				? FText::Format(LOCTEXT("YouLead", "YOU LEAD  ·  {0} / {1}"), FText::AsNumber(Squad.Occupancy()), FText::AsNumber(Squad.Mode))
				: FText::Format(LOCTEXT("TheyLead", "{0}'S SQUAD  ·  {1} / {2}"), FText::FromString(LeaderName),
					FText::AsNumber(Squad.Occupancy()), FText::AsNumber(Squad.Mode)), 15, CampusAngular::Accent)
		];

	if (bLeader && Squad.Occupancy() > 1)
	{
		Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
		[
			Button(LOCTEXT("ManageSquad", "MANAGE"), FOnClicked::CreateLambda([this]()
			{
				OpenManageSquad();
				return FReply::Handled();
			}), EButton::Ghost, 14, FVector2D(126.f, 48.f))
		];
	}
	Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
	[
		Button(LOCTEXT("LeaveSquad", "LEAVE"), FOnClicked::CreateLambda([this]()
		{
			AskConfirm(LOCTEXT("LeaveTitle", "LEAVE YOUR SQUAD?"),
				LOCTEXT("LeaveBody", "YOU WILL GO BACK TO PLAYING ON YOUR OWN."),
				LOCTEXT("LeaveYes", "LEAVE SQUAD"),
				[this]()
				{
					if (UCampusSocial* Live2 = Social())
					{
						TWeakPtr<SWidget> Weak = AsWeak();
						Live2->LeaveParty([this, Weak](bool bOk, const FString& Error)
						{
							if (!Weak.IsValid())
							{
								return;
							}
							if (!bOk)
							{
								Toast(Error);
							}
							Refresh();
						});
					}
				});
			return FReply::Handled();
		}), EButton::Ghost, 14, FVector2D(118.f, 48.f))
	];

	return SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Slanted(14.f))
		.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.8f))
		.Border(CampusAngular::Hairline)
		.BorderWidth(1.f)
		.ContentPadding(FMargin(16.f, 5.f))
		[
			Bar
		];
}

// ---------------------------------------------------------------------------------------------
// Post-match: PLAYER OF THE MATCH

void SCampusLobbyWidget::OpenSummary(const FString& RoomCode)
{
	UCampusSocial* Live = Social();
	if (!Live || RoomCode.IsEmpty())
	{
		return;
	}
	// Weak, for the reason spelled out in FriendButton at the top of this file.
	TWeakPtr<SWidget> Weak = AsWeak();
	Live->LoadSummary(RoomCode, [this, Weak](bool bOk, const FString& Error, const FCampusSummary& Loaded)
	{
		if (!Weak.IsValid())
		{
			return;
		}
		if (!bOk || !Loaded.bValid || Loaded.Players.Num() == 0)
		{
			// The host may not have reported yet. Say nothing and leave the lobby alone - the local
			// win screen already told the player what happened.
			UE_LOG(LogTemp, Log, TEXT("[CampusSocial] no summary yet: %s"), *Error);
			return;
		}
		SummaryData = MakeShared<FCampusSummary>(Loaded);
		SummaryTime = 0.f;
		if (UCampusSocial* Live2 = Social())
		{
			Live2->ClearPendingSummary();
		}
		Show(EScreen::Summary);
	});
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildSummaryRow(const FCampusSummaryRow& Row)
{
	FCampusPlayerCard Card;
	Card.PlayerId = Row.PlayerId;
	Card.Name = Row.Name;
	Card.Color = Row.Color;
	Card.AvatarUrl = Row.AvatarUrl;
	Card.FriendState = Row.FriendState;
	Card.Level = FMath::Max(1, Row.Level);

	const bool bGone = Row.Name.IsEmpty();
	const FText Name = bGone ? LOCTEXT("PlayerLeft", "PLAYER LEFT") : FText::FromString(Row.Name.ToUpper());
	const FLinearColor Edge = Row.bMe ? CampusAngular::Accent : FLinearColor(1.f, 1.f, 1.f, 0.16f);

	return SNew(SBox).HeightOverride(56.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(8.f))
			.Fill(Row.bMe ? FLinearColor(0.09f, 0.08f, 0.02f, 0.72f) : FLinearColor(0.02f, 0.03f, 0.04f, 0.6f))
			.Border(Edge)
			.BorderWidth(Row.bMe ? 2.f : 1.f)
			.ContentPadding(FMargin(12.f, 4.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
				[
					bGone ? SNullWidget::NullWidget : Avatar(Card, 36.f)
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(Name).Font(CampusAngular::Display(18))
					.ColorAndOpacity(bGone ? CampusAngular::TextMuted : FLinearColor::White)
					// CLIPPED: this is the one slot that gives way when the row runs out of width, and
					// a long name used to paint straight over the stat columns beside it.
					.Clipping(EWidgetClipping::ClipToBounds)
				]
				// Tabular numerals: the three stat columns have to line up down the table.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
				[
					SNew(SBox).WidthOverride(76.f)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("KD", "{0}/{1}"), FText::AsNumber(Row.Kills), FText::AsNumber(Row.Deaths)))
						.Font(CampusAngular::Numeric(17)).ColorAndOpacity(FLinearColor::White)
						.Justification(ETextJustify::Right)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
				[
					SNew(SBox).WidthOverride(52.f)
					[
						SNew(STextBlock).Text(FText::AsNumber(Row.Headshots)).Font(CampusAngular::Numeric(17))
						.ColorAndOpacity(CampusAngular::TextMuted).Justification(ETextJustify::Right)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 12.f, 0.f)
				[
					SNew(SBox).WidthOverride(86.f)
					[
						SNew(STextBlock).Text(SocialGrouped(Row.DamageDealt)).Font(CampusAngular::Numeric(17))
						.ColorAndOpacity(CampusAngular::TextMuted).Justification(ETextJustify::Right)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(158.f)
					[
						bGone || Row.bMe ? SNullWidget::NullWidget : FriendButton(Card, FVector2D(158.f, 46.f))
					]
				]
				// REPORT. This scoreboard is the one player list in the game with no tap through to a
				// profile, so it carries the button itself - and this is also the only screen that can
				// hand the report the ROOM CODE the match was played in, which is what lets a moderator
				// check the claim against match_results.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
				[
					bGone || Row.bMe
						? SNullWidget::NullWidget
						: BuildReportButton(Row.PlayerId, Row.Name,
							SummaryData.IsValid() ? SummaryData->Code : FString(), 44.f)
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildSummary()
{
	if (!SummaryData.IsValid() || SummaryData->Players.Num() == 0)
	{
		// Nothing to show (the host never reported, or the screen was re-entered): the lobby is the
		// honest fallback, not an empty frame.
		return BuildLobby();
	}
	const FCampusSummary& Summary = *SummaryData;
	const FCampusSummaryRow* Mvp = Summary.Mvp();

	FCampusPlayerCard MvpCard;
	if (Mvp)
	{
		MvpCard.PlayerId = Mvp->PlayerId;
		MvpCard.Name = Mvp->Name;
		MvpCard.Color = Mvp->Color;
		MvpCard.AvatarUrl = Mvp->AvatarUrl;
		MvpCard.FriendState = Mvp->FriendState;
		MvpCard.Level = FMath::Max(1, Mvp->Level);
	}

	TSharedRef<SWidget> MvpBlock = Mvp
		? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(18.f))
			.Fill(FLinearColor(0.08f, 0.07f, 0.02f, 0.78f))
			.Border(CampusAngular::Accent)
			.BorderWidth(2.f)
			.AccentEdge(CampusAngular::Accent)
			.AccentEdgeWidth(6.f)
			.ContentPadding(FMargin(22.f, 16.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 20.f, 0.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[Avatar(MvpCard, 96.f)]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, -14.f, 0.f, 0.f)
					[
						SNew(SBox).WidthOverride(30.f).HeightOverride(30.f)
						[
							SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Crown).Size(30.f).Color(CampusAngular::Accent)
						]
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(Mvp->Name.IsEmpty() ? LOCTEXT("PlayerLeft", "PLAYER LEFT") : FText::FromString(Mvp->Name.ToUpper()))
						.Font(CampusAngular::Display(36)).ColorAndOpacity(FLinearColor::White)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("MvpLine", "{0} KILLS  ·  {1} DEATHS  ·  {2} HS  ·  {3} DMG"),
							FText::AsNumber(Mvp->Kills), FText::AsNumber(Mvp->Deaths),
							FText::AsNumber(Mvp->Headshots), SocialGrouped(Mvp->DamageDealt)))
						.Font(CampusAngular::Numeric(18)).ColorAndOpacity(CampusAngular::Accent)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(200.f)
					[
						Mvp->bMe || Mvp->Name.IsEmpty() ? SNullWidget::NullWidget : FriendButton(MvpCard, FVector2D(200.f, 52.f))
					]
				]
			])
		: SNullWidget::NullWidget;

	// Teams side by side, each in the server's row order - the client never re-sorts a scoreboard.
	TSharedRef<SHorizontalBox> TeamColumns = SNew(SHorizontalBox);
	TArray<int32> Order;
	for (const FCampusSummaryRow& Row : Summary.Players)
	{
		Order.AddUnique(Row.Team);
	}
	Order.Sort();
	for (const int32 Team : Order)
	{
		TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
		const bool bWinner = Summary.WinningTeam == Team;
		Column->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Label(FText::Format(LOCTEXT("TeamHeading", "TEAM {0}"), FText::AsNumber(Team + 1)), 18, FLinearColor::White)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
			[
				bWinner
					? StaticCastSharedRef<SWidget>(SNew(SCampusAngularPanel)
						.Shape(FCampusShape::Slanted(8.f)).Fill(CampusAngular::Go).ContentPadding(FMargin(9.f, 2.f))
						[
							SNew(STextBlock).Text(LOCTEXT("Won", "WON")).Font(CampusAngular::Display(12))
							.ColorAndOpacity(FLinearColor::White)
						])
					: SNullWidget::NullWidget
			]
		];
		// Column headers: three numbers with no labels are three numbers nobody can read. The widths
		// match BuildSummaryRow's cells exactly so the headings sit over their own columns.
		Column->AddSlot().AutoHeight().Padding(12.f, 0.f, 12.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)
			[
				SNew(SBox).WidthOverride(76.f)
				[
					SNew(STextBlock).Text(LOCTEXT("ColKd", "K / D")).Font(CampusAngular::Display(11))
					.ColorAndOpacity(CampusAngular::TextMuted).Justification(ETextJustify::Right)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)
			[
				SNew(SBox).WidthOverride(52.f)
				[
					SNew(STextBlock).Text(LOCTEXT("ColHs", "HS")).Font(CampusAngular::Display(11))
					.ColorAndOpacity(CampusAngular::TextMuted).Justification(ETextJustify::Right)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 12.f, 0.f)
			[
				SNew(SBox).WidthOverride(86.f)
				[
					SNew(STextBlock).Text(LOCTEXT("ColDmg", "DAMAGE")).Font(CampusAngular::Display(11))
					.ColorAndOpacity(CampusAngular::TextMuted).Justification(ETextJustify::Right)
				]
			]
			// The friend button (158) plus the report square (6 + 44): the headings have to sit over
			// their own columns, so this spacer is exactly what the row spends to their right.
			+ SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(208.f)]
		];
		for (const FCampusSummaryRow& Row : Summary.Players)
		{
			if (Row.Team != Team)
			{
				continue;
			}
			Column->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[BuildSummaryRow(Row)];
		}
		TeamColumns->AddSlot().FillWidth(1.f).Padding(Team == Order[0] ? FMargin(0.f, 0.f, 10.f, 0.f) : FMargin(10.f, 0.f, 0.f, 0.f))
		[
			Column
		];
	}

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)[MvpBlock]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[TeamColumns]
		];

	if (!Summary.bRated)
	{
		// The player learning this HERE, rather than from a missing leaderboard row, is the whole
		// point of the field existing.
		Body->AddSlot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
				[
					SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Lock).Size(16.f).Color(CampusAngular::TextMuted)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Label(LOCTEXT("Unrated", "UNRATED MATCH — NO POINTS"), 14, CampusAngular::TextMuted)
			]
		];
	}

	Body->AddSlot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			Label(FText::Format(LOCTEXT("SummaryCode", "ROOM {0}"), FText::FromString(Summary.Code)), 14, CampusAngular::TextMuted)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Size(FVector2D(1.f, 1.f))]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			// Tappable at 1.5 s so nobody blind-taps through the screen, and it auto-continues at 12 s:
			// longer than that and players feel held hostage.
			SNew(SBox)
			.IsEnabled_Lambda([this]() { return SummaryTime >= SummaryArmAt; })
			[
				CampusStyle::StartButton(LOCTEXT("Continue", "CONTINUE"),
					FOnClicked::CreateLambda([this]()
					{
						SummaryData.Reset();
						Show(EScreen::Lobby);
						return FReply::Handled();
					}), FVector2D(280.f, 84.f))
			]
		]
	];

	return Dialog(LOCTEXT("SummaryTitle", "PLAYER OF THE MATCH"), Body, 1180.f, false, EScreen::Lobby);
}

#undef LOCTEXT_NAMESPACE
