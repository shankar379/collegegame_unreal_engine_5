// THE LOBBY ITSELF (Slate, lobby level) - the screen the player spends the most time on, and the
// only one in the game that is repainted IN PLACE rather than rebuilt:
//
//   BuildLobby()   builds the frame ONCE and hands every moving part an SBox of its own.
//   RepaintLobby() re-fills only the boxes whose fingerprint has actually moved (FillRegion plus
//                  one stamp per region), which is why a mode chip can be tapped without the
//                  whole screen flickering and without destroying the button routing the touch.
//
// The pieces: the top bar (wallet, level, avatar), the left menu column, the squad name plates and
// invite plates, the mode chips, the START button, and the small SETTINGS / DAILY / EVENT sheets
// that hang off the menu.
//
// Split out of Campus/CampusLobbyUI.cpp; Campus/CampusLobbyUIInternal.h carries the file map.

#include "Campus/CampusUI.h"

#include "Campus/CampusAvatar.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusLobbyUIInternal.h"
#include "Campus/CampusParty.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusSocial.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusLobbyUI"

using namespace CampusStyle;

namespace
{
	// GAME_PLAN 1.2 - the map and mode lists now live in Campus/CampusMapArt.h (CampusCatalog), so
	// the lobby plate, the map-select screen and the room dialog cannot drift apart.
	struct FTeamDef
	{
		int32 Size;
		const TCHAR* Name;
	};
	// GAME_PLAN 1.3
	const FTeamDef Teams[] = { { 1, TEXT("Solo") }, { 2, TEXT("Duo") }, { 4, TEXT("Squad") } };

	/** The left-hand menu. Every tab lands on a screen that exists. */
	enum class ELobbyTab : uint8 { Store, Missions, Character, Wardrobe, Vault, Weapons, Events };
	struct FTabDef
	{
		ELobbyTab Tab;
		const TCHAR* Label;
		CampusAngular::EGlyph Glyph;
	};
	const FTabDef Tabs[] = {
		{ ELobbyTab::Store,     TEXT("STORE"),     CampusAngular::EGlyph::Coin },
		{ ELobbyTab::Missions,  TEXT("MISSIONS"),  CampusAngular::EGlyph::Chevron },
		{ ELobbyTab::Character, TEXT("CHARACTER"), CampusAngular::EGlyph::Friends },
		// Directly under CHARACTER, because they are the same subject: CHARACTER is who you are,
		// WARDROBE is what you are wearing. The lobby decor sheet hangs off the wardrobe rather than
		// taking a seventh row of its own. A HANGER, not the trophy it used to borrow: the closet's
		// own DECOR rail wears Trophy, and one mark must mean one thing.
		{ ELobbyTab::Wardrobe,  TEXT("WARDROBE"),  CampusAngular::EGlyph::Hanger },
		{ ELobbyTab::Vault,     TEXT("VAULT"),     CampusAngular::EGlyph::Gem },
		{ ELobbyTab::Weapons,   TEXT("WEAPONS"),   CampusAngular::EGlyph::Rank },
		{ ELobbyTab::Events,    TEXT("EVENTS"),    CampusAngular::EGlyph::Crown },
	};
}

// ---------------------------------------------------------------------------------------------
// Lobby

TSharedRef<SWidget> SCampusLobbyWidget::BuildTopBar()
{
	const UCampusBackend* Api = Backend();
	const FString Name = Api && !Api->PlayerName.IsEmpty() ? Api->PlayerName : TEXT("GUEST");
	// Level and XP are the SERVER's (migration 013), cached in the save for an instant first paint.
	// There is no local formula any more: a level the phone invents is a level two players can hold
	// up side by side and disagree about, and the squad plates printed that same guess for everybody.
	const int32 Level = Api ? Api->Level : 1;
	const float LevelProgress = Api ? Api->LevelFraction() : 0.f;

	// Avatar + name + level + XP bar, the whole plate taps through to the profile.
	TSharedRef<SWidget> Identity = SNew(SCampusAngularButton)
		.Shape(FCampusShape::Notched(18.f))
		.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.84f))
		.Border(CampusAngular::Hairline)
		.BorderWidth(1.f)
		.AccentEdge(CampusAngular::Accent)
		.AccentEdgeWidth(5.f)
		.ContentPadding(FMargin(16.f, 8.f, 22.f, 8.f))
		// Tapping your own plate now opens the PROFILE PAGE, not the settings dialog. The settings are
		// still one tap away from it (EDIT), so nothing that worked before was taken away.
		.OnClicked(FOnClicked::CreateLambda([this]() { OpenProfile(FString()); return FReply::Handled(); }))
		[
			SNew(SHorizontalBox)
			// THE PROFILE BADGE. The player's picture (a preset icon, or their own uploaded photo),
			// clipped to the same corner as everything else in this UI. The LEVEL NUMBER is still
			// here - it is the fallback, shown while the picture is loading, when the device is
			// offline, or when talking to a server that predates pictures.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
			[
				SNew(SBox).WidthOverride(54.f).HeightOverride(54.f)
				[
					SNew(SCampusAvatar)
					.Url(MyAvatarUrl())
					.Size(54.f)
					.Shape(FCampusShape::Clipped(12.f))
					.Fill(FLinearColor(0.10f, 0.08f, 0.03f, 0.95f))
					.Border(CampusAngular::Accent)
					.BorderWidth(2.f)
					[
						SNew(STextBlock).Text(FText::AsNumber(Level)).Font(CampusAngular::Display(26)).ColorAndOpacity(Accent)
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Name.ToUpper())).Font(CampusAngular::Display(24)).ColorAndOpacity(FLinearColor::White)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)
					[
						SNew(SCampusAngularPanel)
						.Shape(FCampusShape::Slanted(8.f))
						.Fill(Api && Api->bGuest ? FLinearColor(0.35f, 0.26f, 0.02f, 0.9f) : FLinearColor(0.08f, 0.26f, 0.14f, 0.9f))
						.ContentPadding(FMargin(9.f, 2.f))
						[
							SNew(STextBlock)
							.Text(Api && Api->bGuest ? LOCTEXT("GuestTag", "GUEST") : LOCTEXT("AccTag", "ACCOUNT"))
							.Font(CampusAngular::Display(12))
							.ColorAndOpacity(Api && Api->bGuest ? CardGold : FLinearColor::White)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					// XP to the next level: a flat sliver, no rounded ends.
					SNew(SBox).WidthOverride(160.f).HeightOverride(6.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[SNew(SCampusAngularPanel).Fill(FLinearColor(1.f, 1.f, 1.f, 0.14f))]
						+ SOverlay::Slot().HAlign(HAlign_Left)
						[
							SNew(SBox).WidthOverride(FMath::Max(4.f, 160.f * LevelProgress))
							[
								SNew(SCampusAngularPanel).Fill(Accent)
							]
						]
					]
				]
			]
		];

	return SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)[Identity]
		+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer).Visibility(EVisibility::HitTestInvisible)]
		// Coin (Room Cards) and gem (Diamonds) counters, each with a "+" to the store.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(6.f, 2.f, 0.f, 0.f)
		[
			CampusStyle::Counter(CampusAngular::EGlyph::Coin, CardGold, FText::AsNumber(Api ? Api->RoomCards : 0),
				FOnClicked::CreateLambda([this]() { StoreTab = EStoreTab::Packs; Show(EScreen::Store); return FReply::Handled(); }))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(10.f, 2.f, 0.f, 0.f)
		[
			CampusStyle::Counter(CampusAngular::EGlyph::Gem, Diamond, FText::AsNumber(Api ? Api->Diamonds : 0),
				FOnClicked::CreateLambda([this]() { StoreTab = EStoreTab::Packs; Show(EScreen::Store); return FReply::Handled(); }))
		]
		// Friends, mail, settings gear - the Free Fire corner cluster.
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(18.f, 0.f, 0.f, 0.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				CampusStyle::IconButton(CampusAngular::EGlyph::Friends, FOnClicked::CreateLambda([this]()
				{
					FriendTab = EFriendTab::Friends;
					FriendsReturn = EScreen::Lobby;
					// One read on open, never a poll: the push is a live top-up, not the source.
					if (UCampusSocial* Live = Social())
					{
						Live->RefreshFriends([this](bool, const FString&)
						{
							if (Current == EScreen::Friends)
							{
								Refresh();
							}
						});
					}
					Show(EScreen::Friends);
					return FReply::Handled();
				}), 58.f)
			]
			// A COUNT, not a bare dot: "2 waiting" is actionable, "something is here" is not.
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, -6.f, -6.f, 0.f)
			[
				FriendRequestBadge()
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(8.f, 0.f, 0.f, 0.f)
		[
			CampusStyle::IconButton(CampusAngular::EGlyph::Mail, FOnClicked::CreateLambda([this]()
			{
				Show(EScreen::Event);
				return FReply::Handled();
			}), 58.f)
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(8.f, 0.f, 0.f, 0.f)
		[
			CampusStyle::IconButton(CampusAngular::EGlyph::Gear, FOnClicked::CreateLambda([this]()
			{
				Show(EScreen::Settings);
				return FReply::Handled();
			}), 58.f, Accent)
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildMenuColumn()
{
	const UCampusBackend* Api = Backend();
	const bool bDailyReady = Api && Api->bDailyCardAvailable;

	// Opening the store needs the catalogue, so those two tabs fetch first.
	auto OpenStore = [this](EStoreTab Tab)
	{
		return FOnClicked::CreateLambda([this, Tab]()
		{
			StoreTab = Tab;
			if (UCampusBackend* Api2 = Backend())
			{
				Busy(TEXT("Opening store..."));
				Api2->LoadStore([this](bool bOk, const FString& Error) { Done(bOk, Error, [this]() { Show(EScreen::Store); }); });
			}
			return FReply::Handled();
		});
	};
	auto OpenScreen = [this](EScreen Screen)
	{
		return FOnClicked::CreateLambda([this, Screen]() { Show(Screen); return FReply::Handled(); });
	};

	const int32 MissionsReady = Api ? Api->Engagement.TotalClaimable() : 0;

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible);
	for (const FTabDef& Def : Tabs)
	{
		FOnClicked Action;
		bool bHighlighted = false;
		int32 Badge = 0;
		switch (Def.Tab)
		{
		case ELobbyTab::Store:     Action = OpenStore(EStoreTab::Packs); break;   // STORE lands on DIAMONDS; OFFERS can be empty
		case ELobbyTab::Weapons:   Action = OpenStore(EStoreTab::Guns); break;
		case ELobbyTab::Character: Action = OpenScreen(EScreen::Account); break;
		// Not OpenScreen: the closet asks for the owned list on the way in (once per app run), and
		// that is exactly the kind of round trip Show() exists to keep out of a Build function.
		case ELobbyTab::Wardrobe:
			Action = FOnClicked::CreateLambda([this]() { OpenWardrobe(); return FReply::Handled(); });
			break;
		case ELobbyTab::Vault:     Action = OpenScreen(EScreen::Daily); bHighlighted = bDailyReady; break;
		case ELobbyTab::Events:    Action = OpenScreen(EScreen::Event); break;
		case ELobbyTab::Missions:
			Action = OpenScreen(EScreen::Missions);
			Badge = MissionsReady;
			bHighlighted = MissionsReady > 0;
			break;
		}
		Column->AddSlot().AutoHeight().Padding(0.f, 5.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[CampusStyle::MenuTab(FText::FromString(Def.Label), Def.Glyph, Action, bHighlighted)]
			// A count, not a bare dot: "3 waiting" is actionable, "something is here" is not.
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, -7.f, -7.f, 0.f)
			[
				Badge > 0
					? StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(28.f).HeightOverride(28.f)
						[
							SNew(SCampusAngularPanel)
							.Shape(FCampusShape::Notched(7.f))
							.Fill(CampusAngular::Go)
							.Border(FLinearColor(1.f, 1.f, 1.f, 0.4f))
							.BorderWidth(1.f)
							[
								SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(FText::AsNumber(Badge))
									.Font(CampusAngular::Numeric(16)).ColorAndOpacity(FLinearColor::White)
								]
							]
						])
					: SNullWidget::NullWidget
			]
		];
	}
	return Column;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildNamePlate(const FCampusPartyMember& Member)
{
	const FLinearColor Edge = Member.bLeader ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.3f);
	TSharedRef<SHorizontalBox> Badges = SNew(SHorizontalBox);
	// Small rank pip, then the "friend" mark that says this plate is somebody you played with.
	if (Member.Rank > 0)
	{
		Badges->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Rank).Size(16.f).Color(Accent)
			]
		];
	}
	if (Member.bLeader)
	{
		Badges->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Crown).Size(16.f).Color(Accent)
			]
		];
	}
	else
	{
		Badges->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
		[
			SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
			[
				SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Friends).Size(16.f).Color(FLinearColor(1.f, 1.f, 1.f, 0.7f))
			]
		];
	}

	// A member of a REAL party is somebody you can look up: the plate taps through to their profile.
	// A plate from the AdoptSquad fallback has no player id, so it stays a plain panel.
	TSharedRef<SWidget> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			// CAPPED. The plate is anchored at a screen FRACTION so it sits under its own 3D
			// character; its WIDTH, though, grows with the name, and at Squad (4) the right-hand
			// plate is the one that would eventually reach the mode plate / START column. Clipping
			// at 150 makes that impossible at any name length without moving the plate off its
			// character - which is the one thing this row may never do.
			SNew(SBox).MaxDesiredWidth(150.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Member.Name.ToUpper()))
				.Font(CampusAngular::Display(19))
				.ColorAndOpacity(Member.bAccepted ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.5f))
				.Clipping(EWidgetClipping::ClipToBounds)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Badges]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Member.bAccepted
					? FText::Format(LOCTEXT("PlateLevel", "LV {0}"), FText::AsNumber(Member.Level))
					: LOCTEXT("PlateWaiting", "INVITED"))
				.Font(CampusAngular::Display(13))
				.ColorAndOpacity(CampusAngular::TextMuted)
			]
			// The presence dot: a squadmate who is still in a match is worth seeing before START.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
			[
				Member.Presence == TEXT("match")
					? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("PlateInMatch", "· IN MATCH"))
						.Font(CampusAngular::Display(11)).ColorAndOpacity(Accent))
					: SNullWidget::NullWidget
			]
		];

	// THE PICTURE, ONLY WHEN THE PAYLOAD CARRIED ONE. A plate from the AdoptSquad fallback has no
	// avatar_url (all it has is a replicated display name), and it then looks exactly as it did before
	// pictures existed - no empty square where a face is never coming.
	TSharedRef<SWidget> Body = Column;
	if (!Member.AvatarUrl.IsEmpty())
	{
		const FString Initial = Member.Name.IsEmpty() ? FString(TEXT("?")) : Member.Name.Left(1).ToUpper();
		Body = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)
			[
				SNew(SCampusAvatar)
				.Url(Member.AvatarUrl)
				.Size(36.f)
				.Shape(FCampusShape::Clipped(8.f))
				.Fill(FLinearColor(0.08f, 0.09f, 0.11f, 0.95f))
				.Border(FLinearColor(1.f, 1.f, 1.f, 0.28f))
				.BorderWidth(1.f)
				// An invited-but-not-accepted plate is drawn dimmed; its picture dims with it.
				.Tint(Member.bAccepted ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.5f))
				[
					SNew(STextBlock).Text(FText::FromString(Initial)).Font(CampusAngular::Display(16))
					.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.75f))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Column];
	}

	const FLinearColor Fill = Member.bAccepted
		? FLinearColor(0.015f, 0.02f, 0.03f, 0.88f)
		: FLinearColor(0.015f, 0.02f, 0.03f, 0.45f);
	if (Member.PlayerId.IsEmpty() || !Member.bAccepted)
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Wedge(14.f, 10.f))
			.Fill(Fill)
			.Border(Edge)
			.BorderWidth(Member.bLeader ? 2.f : 1.f)
			.AccentEdge(Edge)
			.AccentEdgeWidth(4.f)
			.ContentPadding(FMargin(18.f, 7.f, 14.f, 7.f))
			[
				Body
			];
	}
	const FString Who = Member.PlayerId;
	return SNew(SCampusAngularButton)
		.Shape(FCampusShape::Wedge(14.f, 10.f))
		.Fill(Fill)
		.Border(Edge)
		.BorderWidth(Member.bLeader ? 2.f : 1.f)
		.AccentEdge(Edge)
		.AccentEdgeWidth(4.f)
		.ContentPadding(FMargin(18.f, 7.f, 14.f, 7.f))
		.OnClicked(FOnClicked::CreateLambda([this, Who]()
		{
			OpenProfile(Who);
			return FReply::Handled();
		}))
		[
			Body
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildInvitePlate()
{
	// An empty slot: tap to invite. This used to open the room-code screen because the API had no
	// invite channel; now it opens the friend picker, and one tap there puts a card on their phone.
	return SNew(SCampusAngularButton)
		.Shape(FCampusShape::Wedge(14.f, 10.f))
		.Fill(FLinearColor(0.f, 0.f, 0.f, 0.35f))
		.Border(FLinearColor(1.f, 1.f, 1.f, 0.22f))
		.BorderWidth(1.f)
		.ContentPadding(FMargin(14.f, 10.f))
		.OnClicked(FOnClicked::CreateLambda([this]()
		{
			UCampusSocial* Live = Social();
			if (!Live)
			{
				// No social subsystem at all (a build without it): the room code still works.
				Show(EScreen::Rooms);
				return FReply::Handled();
			}
			// A member cannot invite - the server would answer 409 NOT_PARTY_LEADER - so say who can.
			if (Live->HasParty() && !Live->IsPartyLeader())
			{
				Toast(FString::Printf(TEXT("%s LEADS THIS SQUAD. ONLY THEY CAN INVITE."),
					*Live->GetServerParty().LeaderName().ToUpper()));
				return FReply::Handled();
			}
			Live->RefreshFriends([this](bool, const FString&)
			{
				if (Current == EScreen::FriendPick)
				{
					Refresh();
				}
			});
			Show(EScreen::FriendPick);
			return FReply::Handled();
		}))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)
				[
					SNew(SCampusGlyph).Glyph(CampusAngular::EGlyph::Plus).Size(22.f).Color(Accent)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(STextBlock).Text(LOCTEXT("InviteSlot", "INVITE")).Font(CampusAngular::Display(14)).ColorAndOpacity(CampusAngular::TextMuted)
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildSquadRow()
{
	UCampusParty* Squad = Party();
	if (!Squad)
	{
		return SNullWidget::NullWidget;
	}
	const TArray<FCampusPartyMember>& Members = Squad->GetMembers();
	const int32 Capacity = Squad->GetCapacity();
	// Solo: no row at all. Zero footprint beats an empty frame.
	if (Capacity <= 1)
	{
		return SNullWidget::NullWidget;
	}

	// Anchor each plate at the screen fraction of the SLOT its character stands in. Both sides read
	// CampusLineUp::SlotScreenFraction, so a plate cannot drift off its figure - and because the
	// player always holds MainSlot, their own plate does not move when the squad size changes.
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas).Visibility(EVisibility::SelfHitTestInvisible);
	for (int32 Index = 0; Index < Capacity; ++Index)
	{
		const float Fraction = CampusLineUp::SlotScreenFraction(CampusLineUp::SlotForMember(Index));
		TSharedRef<SWidget> Plate = Members.IsValidIndex(Index) ? BuildNamePlate(Members[Index]) : BuildInvitePlate();
		Canvas->AddSlot()
			.Anchors(FAnchors(Fraction, 1.f))
			.Alignment(FVector2D(0.5f, 1.f))
			.AutoSize(true)
			[
				Plate
			];
	}
	// A fixed height means adding or losing a squad member never shifts the rest of the lobby.
	return SNew(SBox).HeightOverride(78.f)[Canvas];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildModeSelect()
{
	UCampusSocial* Live = Social();
	const bool bInParty = Live && Live->HasParty();
	const bool bLeads = !bInParty || Live->IsPartyLeader();

	// A MEMBER GETS ONE READ-ONLY PLATE, NOT THREE DEAD PILLS. Three greyed-out chips are three tap
	// targets that do nothing, and the caption explaining them has nowhere to go that does not land on
	// the squad plates. One plate says the same thing in a third of the width.
	if (bInParty && !bLeads)
	{
		return SNew(SBox).HeightOverride(46.f).HAlign(HAlign_Right).Visibility(EVisibility::SelfHitTestInvisible)
			[
				SNew(SBox).HeightOverride(46.f)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Slanted(12.f))
					.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.8f))
					.Border(CampusAngular::Hairline)
					.BorderWidth(1.f)
					.ContentPadding(FMargin(18.f, 4.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							// A lambda, so party.mode_changed relabels the plate without rebuilding it.
							.Text_Lambda([this]()
							{
								return ShownTeamSize() == 2 ? LOCTEXT("ModeDuo", "DUO") : LOCTEXT("ModeSquad", "SQUAD");
							})
							.Font(CampusAngular::Display(18)).ColorAndOpacity(Accent)
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 2.f, 0.f, 0.f)
						[
							SNew(STextBlock).Text(LOCTEXT("LeaderPicks", "LEADER PICKS"))
							.Font(CampusAngular::Display(12)).ColorAndOpacity(CampusAngular::TextMuted)
						]
					]
				]
			];
	}

	TSharedRef<SHorizontalBox> TeamRow = SNew(SHorizontalBox);
	for (const FTeamDef& Team : Teams)
	{
		const int32 Size = Team.Size;
		TeamRow->AddSlot().AutoWidth().Padding(4.f, 0.f)
		[
			SNew(SBox).IsEnabled(bLeads)
			[
				// THE LIT CHIP IS AN ATTRIBUTE, not a build-time colour. Tapping SOLO / DUO / SQUAD now
				// restyles the three chips through these lambdas and rebuilds nothing at all - which is
				// why START, the map plate and the ONLINE strip no longer flinch when the mode changes.
				CampusStyle::SelectablePill(FText::FromString(FString(Team.Name).ToUpper()),
					TAttribute<bool>::CreateLambda([this, Size]() { return ShownTeamSize() == Size; }),
					FOnClicked::CreateLambda([this, Size]()
					{
						UCampusSocial* Live2 = Social();
						if (Live2 && Live2->HasParty())
						{
							if (Size <= 1)
							{
								// SOLO is not a mode value: it is the ABSENCE of a party. So it is a
								// confirm-and-leave, never a silent disband behind the player's back.
								AskConfirm(LOCTEXT("SoloTitle", "LEAVE YOUR SQUAD?"),
									LOCTEXT("SoloBody", "SOLO MEANS PLAYING ON YOUR OWN. YOUR SQUAD WILL BE DISBANDED."),
									LOCTEXT("SoloYes", "GO SOLO"),
									[this]()
									{
										if (UCampusSocial* Live3 = Social())
										{
											Live3->LeaveParty([this](bool bOk, const FString& Error)
											{
												if (!bOk)
												{
													Toast(Error);
												}
												SelectedTeamSize = 1;
												SaveMatchChoice();
												SyncParty();
												Refresh();
											});
										}
									});
								return FReply::Handled();
							}
							Live2->SetPartyMode(Size, [this](bool bOk, const FString& Error)
							{
								if (!bOk)
								{
									// 409 PARTY_TOO_LARGE: a silent kick-to-fit is the worst possible
									// behaviour, so the leader is told and kicks explicitly instead.
									Toast(Error);
								}
								Refresh();
							});
							return FReply::Handled();
						}
						SelectedTeamSize = Size;
						SaveMatchChoice();
						// The squad row and the 3D line-up follow the chosen size straight away.
						SyncParty();
						Refresh();
						return FReply::Handled();
					}), FVector2D(136.f, 46.f))
			]
		];
	}

	// The map / mode plate ("MAPS") is its own region in BuildLobby now, so a mode tap cannot touch it.
	return SNew(SBox).HAlign(HAlign_Right).Visibility(EVisibility::SelfHitTestInvisible)[TeamRow];
}

int32 SCampusLobbyWidget::ShownTeamSize() const
{
	// With a server party the SIZE OF THE ROW IS THE SERVER'S: the chips show the party's mode, the
	// leader's tap goes through PATCH /v1/parties/me, and SetCapacity only follows party.mode_changed.
	const UCampusSocial* Live = Social();
	return Live && Live->HasParty() ? Live->GetServerParty().Mode : SelectedTeamSize;
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildStartButton()
{
	UCampusSocial* Live = Social();
	if (Live && Live->HasParty() && !Live->IsPartyLeader())
	{
		// A member cannot start their own room out from under the squad: they would spend a Room Card
		// and leave everyone else standing in the lobby.
		//
		// BATTLE SESSION: one ticket is one TEAM and it is the leader's (backend docs/BATTLE_SESSION.md
		// §5), so the plate says whose press it is. The member does nothing and is pulled in by
		// `bs.placed` the moment the matchmaker places the squad.
		const FString LeaderName = Live->GetServerParty().LeaderName().ToUpper();
		const FText Waiting = LeaderName.IsEmpty()
			? LOCTEXT("LeaderStarts", "LEADER STARTS")
			: FText::Format(LOCTEXT("WaitingForLeader", "WAITING FOR {0}"), FText::FromString(LeaderName));
		return SNew(SBox).IsEnabled(false).MinDesiredWidth(300.f).MinDesiredHeight(96.f)
			[
				SNew(SCampusAngularPanel)
				.Shape(FCampusShape::Wedge(28.f, 20.f))
				.Fill(FLinearColor(1.f, 1.f, 1.f, 0.08f))
				.Border(CampusAngular::Hairline)
				.BorderWidth(1.f)
				.ContentPadding(FMargin(26.f, 8.f))
				[
					SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Waiting)
						.Font(CampusAngular::Display(24))
						.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.6f))
						.Justification(ETextJustify::Center)
					]
				]
			];
	}
	// BATTLE ROYALE has no Room Card, no code and no sheet to pick settings in: START *is* the queue.
	// Every other mode still opens the Custom Room / Quick Match sheet.
	return CampusStyle::StartButton(LOCTEXT("Start", "START"),
		FOnClicked::CreateLambda([this]()
		{
			// START IS ALWAYS THE 50-PLAYER BATTLE SESSION (solo / duo / squad). Red/blue Rooms are a
			// Room Card thing and open only from their own ROOMS button, never from here.
			StartBattleRoyale();
			return FReply::Handled();
		}), FVector2D(300.f, 96.f));
}

void SCampusLobbyWidget::FillRegion(const TSharedPtr<SBox>& Slot, uint32& Stamp, uint32 Now,
	TFunctionRef<TSharedRef<SWidget>()> Build)
{
	if (!Slot.IsValid())
	{
		return;
	}
	// 0 is reserved for "never filled", so a region whose data happens to hash to 0 still paints once.
	const uint32 Next = Now == 0 ? 1u : Now;
	if (Stamp == Next)
	{
		return;
	}
	Stamp = Next;
	const TSharedRef<SWidget> Fresh = Build();
	Slot->SetContent(Fresh);
	// Measured now, for the same reason Show() measures a new body: a region re-filled from Tick can
	// miss this frame's layout pass, and an unmeasured widget arranges its children at zero size.
	Fresh->SlatePrepass();
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildLobby()
{
	SyncParty();

	// BUILT ONCE. The overlay, the safe zone and - above all - the drag area live for as long as the
	// lobby does: re-creating them on every tap dropped the touch that was still being routed and
	// re-laid out the whole screen, which is the flicker the phone showed on every interaction.
	if (!LobbyBody.IsValid())
	{
		SAssignNew(LobbyBody, SBox).Visibility(EVisibility::SelfHitTestInvisible)
		[
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SOverlay::Slot()
			[
				SNew(SCampusDragArea).OnDrag_Lambda([this](float DeltaX) { if (Controller.IsValid()) { Controller->RotateDisplay(-DeltaX * 0.4f); } })
			]
			+ SOverlay::Slot()
			[
				SNew(SSafeZone).Visibility(EVisibility::SelfHitTestInvisible).Padding(FMargin(24.f, 16.f))
				[
					SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
					// Top bar: avatar + name + level, coin and gem counters, friends / mail / gear.
					+ SOverlay::Slot().VAlign(VAlign_Top)
					[
						SAssignNew(TopBarBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
					]
					// The logo sits under the corner icon cluster, right-aligned.
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 86.f, 4.f, 0.f)
					[
						SNew(SBox).Visibility(EVisibility::HitTestInvisible)[CampusAngular::Logo(40)]
					]
					// The right-edge friends strip: the reference's pre-START invite rail, sitting directly
					// above the mode chips. Anchored to the BOTTOM rather than the top on purpose - the
					// notice stack owns the top-right corner, and a permanent list under transient cards
					// is the one arrangement that would annoy twice.
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 276.f) // clears START + mode plate + SOLO/DUO/SQUAD pills
					[
						SAssignNew(FriendsStripBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
					]
					// Slanted menu tabs down the left, with the weekend board under them.
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(0.f, 20.f, 0.f, 0.f)
					[
						SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(MenuColumnBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
						[
							SAssignNew(WeekendBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
						]
					]
					// A settled weekend's unclaimed reward: top-centre, under the top bar, impossible to miss.
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 96.f, 0.f, 0.f)
					[
						SAssignNew(ClaimBannerBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
					]
					// Squad name plates, under the characters.
					+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 108.f)
					[
						SAssignNew(SquadRowBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
					]
					// The squad strip and the drag hint share ONE column anchored to the bottom, so they
					// can never land on top of each other however tall the strip gets - and the whole
					// column clears the plate row above it (which ends 108 up).
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 14.f)
					[
						SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
						[
							SAssignNew(PartyBarBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
						[
							Label(LOCTEXT("DragHint", "DRAG TO TURN"), 14, FLinearColor(1.f, 1.f, 1.f, 0.35f))
						]
					]
					// Bottom right: mode chips above the yellow slanted START.
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
					[
						SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
						// The chips, the map plate and START are three separate regions on purpose: a
						// mode tap only ever has business with the first of them.
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 8.f)
						[
							SAssignNew(ModeSelectBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 12.f)
						[
							SAssignNew(ModePlateBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
						[
							SAssignNew(StartBox, SBox).Visibility(EVisibility::SelfHitTestInvisible)
						]
					]
				]
			]
		];
	}

	RepaintLobby();
	return LobbyBody.ToSharedRef();
}

void SCampusLobbyWidget::RepaintLobby()
{
	const UCampusBackend* Api = Backend();
	UCampusSocial* Live = Social();

	// THE TOP BAR, THE LEFT MENU COLUMN, THE WEEKEND BOARD AND THE CLAIM BANNER are the parts of the
	// lobby an interaction must never touch. None of them reads the mode, the friends strip or the
	// party, so each is fingerprinted from its OWN inputs and left alone until those move. Choosing a
	// mode, opening ONLINE or sending an invite now repaints neither the tabs nor the board.
	uint32 Now = GetTypeHash(FString::Printf(TEXT("%s|%d|%d|%d|%d|%d"),
		Api ? *Api->PlayerName : TEXT(""),
		Api ? Api->Level : 0,
		Api ? FMath::RoundToInt(Api->LevelFraction() * 1000.f) : 0,
		Api && Api->bGuest ? 1 : 0,
		Api ? Api->RoomCards : 0,
		Api ? Api->Diamonds : 0));
	// The friends icon carries a count of everything waiting on an answer, so the live notice stack
	// and the incoming-request list are part of the top bar's fingerprint too.
	Now = HashCombine(Now, Live ? uint32(Live->GetRoster().Incoming.Num() * 31 + Live->GetNotices().Num()) : 0u);
	// THE PROFILE PICTURE: the URL *and* where its download stands, so the badge re-fills exactly once
	// - when the picture lands (or fails) - and never because some other player's avatar arrived.
	Now = HashCombine(Now, CampusAvatar::Fingerprint(MyAvatarUrl()));
	FillRegion(TopBarBox, TopBarStamp, Now, [this]() { return BuildTopBar(); });

	// BuildMenuColumn reads exactly two things: the free Room Card and the claimable mission count.
	FillRegion(MenuColumnBox, MenuColumnStamp,
		GetTypeHash(FString::Printf(TEXT("%d|%d"),
			Api && Api->bDailyCardAvailable ? 1 : 0,
			Api ? Api->Engagement.TotalClaimable() : 0)),
		[this]() { return BuildMenuColumn(); });

	// The board's own fingerprint. Its COUNTDOWN is already a text lambda, so a ticking clock has
	// never needed a repaint and must not get one now.
	uint32 Board = 0;
	if (Api && Api->Engagement.bLoaded && Api->Engagement.Board.bValid)
	{
		const FCampusWeekend& Weekend = Api->Engagement.Board;
		Board = GetTypeHash(FString::Printf(TEXT("%s|%s|%lld|%lld|%d|%d|%d|%d|%d|%d|%d|%d"),
			*Weekend.PeriodId, *Weekend.State, Weekend.StartsAt.GetTicks(), Weekend.EndsAt.GetTicks(),
			Weekend.bProvisional ? 1 : 0, Weekend.bDegraded ? 1 : 0, Weekend.bHasMe ? 1 : 0,
			Weekend.bHasNextRank ? 1 : 0, Weekend.MyRank, Weekend.MyPoints, Weekend.NextRank, Weekend.NextGap));
		for (const FCampusBoardEntry& Entry : Weekend.Top)
		{
			Board = HashCombine(Board, GetTypeHash(FString::Printf(TEXT("%d|%s|%d|%d"),
				Entry.Rank, *Entry.Name, Entry.Points, Entry.bMe ? 1 : 0)));
		}
	}
	FillRegion(WeekendBox, WeekendStamp, Board, [this]() { return BuildWeekendPanel(); });

	FillRegion(ClaimBannerBox, ClaimBannerStamp,
		GetTypeHash(FString::Printf(TEXT("%d|%s|%d|%s"),
			Api && Api->Engagement.bLoaded && Api->Engagement.LastWeekend.bUnclaimed ? 1 : 0,
			Api ? *Api->Engagement.LastWeekend.PeriodId : TEXT(""),
			Api ? Api->Engagement.LastWeekend.MyRank : 0,
			Api ? *Api->Engagement.LastWeekend.Reward.Describe() : TEXT(""))),
		[this]() { return BuildClaimBanner(); });

	// ---- The regions a mode tap, the ONLINE strip or an invite can legitimately change. These were
	// re-filled unconditionally, which is why START, the map plate and the friends list still flinched
	// when a mode chip was tapped. Each one now carries its own fingerprint too.

	const bool bInParty = Live && Live->HasParty();
	const bool bLeads = !bInParty || Live->IsPartyLeader();
	// A named empty, not a temporary bound to a reference: the fingerprints below only read it when
	// bInParty says there is a party, but the reference has to be valid either way.
	static const FCampusServerParty NoParty;
	const FCampusServerParty& ServerSquad = Live ? Live->GetServerParty() : NoParty;

	// THE CHIPS: only the leader / member layout is structural. WHICH chip is lit is an attribute
	// (SelectablePill), so tapping SOLO / DUO / SQUAD rebuilds nothing here at all.
	FillRegion(ModeSelectBox, ModeSelectStamp,
		GetTypeHash(FString::Printf(TEXT("%d|%d"), bInParty ? 1 : 0, bLeads ? 1 : 0)),
		[this]() { return BuildModeSelect(); });

	// The map / mode plate reads the chosen mode and map, and nothing else.
	FillRegion(ModePlateBox, ModePlateStamp,
		GetTypeHash(FString::Printf(TEXT("%d|%d"), SelectedMode, SelectedMap)),
		[this]() { return BuildModePlate(); });

	FillRegion(StartBox, StartStamp,
		GetTypeHash(FString::Printf(TEXT("%d|%d|%s|%d"), bInParty ? 1 : 0, bLeads ? 1 : 0,
			bInParty ? *ServerSquad.LeaderName() : TEXT(""), IsBattleRoyaleSelected() ? 1 : 0)),
		[this]() { return BuildStartButton(); });

	// The plate row: the squad size, then every field a name plate or an INVITE plate can draw.
	uint32 Row = 0;
	if (const UCampusParty* Local = Party())
	{
		Row = GetTypeHash(FString::Printf(TEXT("cap%d"), Local->GetCapacity()));
		for (const FCampusPartyMember& Member : Local->GetMembers())
		{
			Row = HashCombine(Row, GetTypeHash(FString::Printf(TEXT("%s|%s|%d|%d|%d|%d|%s|%s"),
				*Member.PlayerId, *Member.Name, Member.Level, Member.Rank, Member.bLeader ? 1 : 0,
				Member.bAccepted ? 1 : 0, *Member.Presence, *Member.InviteId)));
			// Same rule for the plate thumbnails: one re-fill when a squadmate's picture arrives.
			Row = HashCombine(Row, CampusAvatar::Fingerprint(Member.AvatarUrl));
		}
	}
	FillRegion(SquadRowBox, SquadRowStamp, Row, [this]() { return BuildSquadRow(); });

	FillRegion(PartyBarBox, PartyBarStamp,
		GetTypeHash(FString::Printf(TEXT("%d|%d|%s|%s|%d|%d|%s"), bInParty ? 1 : 0, bLeads ? 1 : 0,
			bInParty ? *ServerSquad.LeaderName() : TEXT(""), bInParty ? *ServerSquad.State : TEXT(""),
			bInParty ? ServerSquad.Occupancy() : 0, bInParty ? ServerSquad.Mode : 0,
			bInParty ? *ServerSquad.RoomCode : TEXT(""))),
		[this]() { return BuildPartyBar(); });

	// The ONLINE strip: whether it is there at all, whether it is open, and every field its rows draw.
	// A DUO -> SQUAD tap touches none of these, so the list holds still.
	uint32 Strip = GetTypeHash(FString::Printf(TEXT("%d|%d|%d|%d|%d"),
		SelectedTeamSize <= 1 ? 1 : 0, bLobbyFriendsOpen ? 1 : 0,
		bInParty ? 1 : 0, bLeads ? 1 : 0, bInParty && ServerSquad.HasRoom() ? 1 : 0));
	if (Live && Live->GetRoster().bValid)
	{
		Strip = HashCombine(Strip, 0x5EEDu);
		for (const FCampusPlayerCard& Card : Live->GetRoster().Friends)
		{
			Strip = HashCombine(Strip, GetTypeHash(FString::Printf(TEXT("%s|%s|%s|%d|%s|%d"),
				*Card.PlayerId, *Card.Name, *Card.Presence, Card.bInParty ? 1 : 0,
				*Card.PendingInviteId, Card.Level)));
		}
	}
	FillRegion(FriendsStripBox, FriendsStripStamp, Strip, [this]() { return BuildLobbyFriends(); });
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildSettings()
{
	UCampusSave* Save = Profile();
	return Dialog(LOCTEXT("SettingsTitle", "SETTINGS"),
		CampusOptions::Body(Save, [this]() { Refresh(); }), 900.f, true, EScreen::Lobby);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildDaily()
{
	const UCampusBackend* Api = Backend();
	const bool bCan = Api && Api->bDailyCardAvailable;
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("DailyCard", "1 free Room Card every day"), 26)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 22.f)
		[
			Label(LOCTEXT("DailyDetail", "A Room Card creates a Custom Room for you and your friends. Resets at 5 AM."), 17, Muted, false)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			Button(bCan ? LOCTEXT("Claim", "CLAIM") : LOCTEXT("Claimed", "CLAIMED - COME BACK TOMORROW"), FOnClicked::CreateLambda([this]()
			{
				if (UCampusBackend* Api2 = Backend())
				{
					Busy(TEXT("Claiming..."));
					Api2->ClaimDailyCard([this](bool bOk, const FString& Error) { Done(bOk, Error, [this]() { Toast(TEXT("+1 Room Card")); }); });
				}
				return FReply::Handled();
			}), bCan ? EButton::Go : EButton::Ghost, 22, FVector2D(420.f, 72.f), bCan)
		];
	return Dialog(LOCTEXT("DailyTitle", "DAILY REWARD"), Body, 720.f, true, EScreen::Lobby);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildEvent()
{
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("EventName", "Founders' Week"), 40)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 22.f)
		[
			Label(LOCTEXT("EventBody", "First 30 players get the Founder badge  ·  daily free Room Card  ·  Combo Offer ₹100"), 19, Muted, false)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
		[
			Button(LOCTEXT("PlayNow", "PLAY NOW"), FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }), EButton::Accent, 24, FVector2D(300.f, 72.f))
		];
	return Dialog(LOCTEXT("LiveEvent", "LIVE EVENT"), Body, 780.f, true, EScreen::Lobby);
}

#undef LOCTEXT_NAMESPACE
