// THE TWO SCREENS THE MENU OPENS ON THE PLAYER'S OWN BEHALF (Slate, lobby level).
//
//   Rooms    Quick Match (free, joins the server room on this Wi-Fi) / Create (costs one Room
//            Card) / Join by code. The map, mode and round settings it shows are the ones the
//            lobby already chose - CampusCatalog is the single source for all three.
//   Account  who the player is signed in as, the college they appear on the board for, EDIT
//            PROFILE, and LOG OUT - with the guest warning, because a guest who logs out
//            without registering first has no way back to that account.
//
// Split out of Campus/CampusLobbyUI.cpp; Campus/CampusLobbyUIInternal.h carries the file map.

#include "Campus/CampusUI.h"

#include "Campus/CampusBackend.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusSocial.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusLobbyUI"

using namespace CampusStyle;

namespace
{
	const FLinearColor Danger = CampusAngular::Danger;
}

// ---------------------------------------------------------------------------------------------
// Rooms: Quick Match / Create (1 Room Card) / Join by code

TSharedRef<SWidget> SCampusLobbyWidget::BuildRooms()
{
	const UCampusBackend* Api = Backend();
	auto Option = [](const FText& Title, const FText& Detail, TSharedRef<SWidget> Action)
	{
		return SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(FLinearColor(0.03f, 0.04f, 0.06f, 0.9f))
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(26.f, 14.f, 20.f, 14.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Label(Title, 24)]
					+ SVerticalBox::Slot().AutoHeight()[Label(Detail, 15, Muted, false)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Action]
			];
	};
	const FString Mode = FString::Printf(TEXT("%s  ·  %s  ·  %s"),
		*FString(CampusCatalog::Mode(SelectedMode).Name).ToUpper(),
		CampusCatalog::Map(SelectedMap).Name,
		SelectedTeamSize == 1 ? TEXT("Solo") : (SelectedTeamSize == 2 ? TEXT("Duo") : TEXT("Squad")));
	const int32 Cards = Api ? Api->RoomCards : 0;
	// The mode picked on the map-select screen decides which route this dialog pushes: Quick Match
	// gets the green PLAY, Team Clash gets the accent CREATE. The other button still works.
	const bool bQuick = FName(CampusCatalog::Mode(SelectedMode).Id) == FName(TEXT("QuickMatch"));

	// Custom Room options (three.js lobby): rounds per match and minutes per round, one tap each, remembered
	UCampusSave* Save = Profile();
	const int32 PickedRounds = Save ? Save->MatchRounds : 5;
	const int32 PickedMinutes = Save ? Save->RoundMinutes : 5;
	TSharedRef<SHorizontalBox> RoundRow = SNew(SHorizontalBox);
	RoundRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 10.f, 0.f)[Label(LOCTEXT("RoundsLabel", "Rounds"), 17, Muted)];
	for (int32 Choice : { 4, 5, 6, 7 })
	{
		RoundRow->AddSlot().AutoWidth().Padding(3.f, 0.f)
		[
			Button(FText::AsNumber(Choice), FOnClicked::CreateLambda([this, Choice]()
			{
				if (UCampusSave* S = Profile()) { S->MatchRounds = Choice; S->Persist(); }
				Refresh();
				return FReply::Handled();
			}), PickedRounds == Choice ? EButton::Accent : EButton::Dark, 18, FVector2D(60.f, 46.f))
		];
	}
	RoundRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(22.f, 0.f, 10.f, 0.f)[Label(LOCTEXT("TimeLabel", "Round time"), 17, Muted)];
	for (int32 Choice : { 3, 5, 7, 10 })
	{
		RoundRow->AddSlot().AutoWidth().Padding(3.f, 0.f)
		[
			Button(FText::FromString(FString::Printf(TEXT("%dm"), Choice)), FOnClicked::CreateLambda([this, Choice]()
			{
				if (UCampusSave* S = Profile()) { S->RoundMinutes = Choice; S->Persist(); }
				Refresh();
				return FReply::Handled();
			}), PickedMinutes == Choice ? EButton::Accent : EButton::Dark, 18, FVector2D(64.f, 46.f))
		];
	}

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Label(FText::FromString(Mode), 20, FLinearColor::White)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f)
		[
			Option(LOCTEXT("Quick", "Quick Match"), LOCTEXT("QuickDetail", "Free  ·  joins the server room on this Wi-Fi"),
				Button(LOCTEXT("PlayBtn", "PLAY"), FOnClicked::CreateLambda([this]()
				{
					if (Controller.IsValid()) { Toast(TEXT("Joining...")); Controller->JoinRoom(); }
					return FReply::Handled();
				}), bQuick ? EButton::Go : EButton::Dark, 20, FVector2D(160.f, 58.f)))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)[RoundRow]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f)
		[
			Option(LOCTEXT("Create", "Create Custom Room"),
				FText::FromString(FString::Printf(TEXT("%d rounds x %d min, all played  ·  1 Room Card (you have %d)"), PickedRounds, PickedMinutes, Cards)),
				Button(LOCTEXT("CreateBtn", "CREATE"), FOnClicked::CreateLambda([this]()
				{
					if (Controller.IsValid())
					{
						// Say what is actually happening: a leader's squad is pulled in behind the
						// room, and that second round trip is worth naming on the busy overlay.
						const UCampusSocial* Live = Social();
						Busy(Live && Live->HasParty() && Live->IsPartyLeader()
							? TEXT("Pulling your squad in...")
							: TEXT("Creating room..."));
						Controller->HostRoom([this](const FString& Error) { Done(false, Error); });
					}
					return FReply::Handled();
				}), bQuick ? EButton::Dark : EButton::Accent, 20, FVector2D(160.f, 58.f), Cards > 0))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(FLinearColor(0.03f, 0.04f, 0.06f, 0.9f))
			.Border(CampusAngular::Hairline)
			.BorderWidth(1.f)
			.ContentPadding(FMargin(26.f, 14.f, 20.f, 14.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 12.f, 0.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Label(LOCTEXT("Join", "Join a friend"), 24)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)[Field(LOCTEXT("CodeHint", "6-letter room code"), CodeText)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					Button(LOCTEXT("JoinBtn", "JOIN"), FOnClicked::CreateLambda([this]()
					{
						if (Controller.IsValid())
						{
							Busy(TEXT("Finding room..."));
							Controller->JoinRoomByCode(CodeText, [this](const FString& Error) { Done(false, Error); });
						}
						return FReply::Handled();
					}), EButton::Accent, 20, FVector2D(160.f, 58.f))
				]
			]
		];
	return Dialog(LOCTEXT("RoomsTitle", "PLAY"), Body, 820.f, true, EScreen::Lobby);
}

// ---------------------------------------------------------------------------------------------
// Account

TSharedRef<SWidget> SCampusLobbyWidget::BuildAccount()
{
	const UCampusBackend* Api = Backend();
	const bool bGuest = !Api || Api->bGuest;
	auto Line = [](const FString& Key, const FString& Value)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 12.f, 0.f)[Label(FText::FromString(Key), 18, Muted, false)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[Label(FText::FromString(Value.IsEmpty() ? TEXT("-") : Value), 18)];
	};
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
		// THE PICTURE BLOCK, first: the one and only place a profile picture is changed (reached from
		// the profile page's EDIT). Campus/CampusProfileUI.cpp owns it.
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)[BuildAvatarChooser()]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
		[
			SNew(SBox).HeightOverride(1.f)[SNew(SCampusAngularPanel).Fill(CampusAngular::Hairline)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("Nickname"), Api ? Api->PlayerName : FString())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("Account"), bGuest ? TEXT("Guest (this phone only)") : Api->Email)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("Roll no."), Api ? Api->RollNo : FString())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("Branch / College"), Api ? FString::Printf(TEXT("%s  %s"), *Api->Branch, *Api->College) : FString())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)
		[
			Line(TEXT("Leaderboard college"), Api && !Api->CollegeId.IsEmpty()
				? FString::Printf(TEXT("%s  ·  %s"), *Api->CollegeId, Api->bShowCollege ? TEXT("shown on boards") : TEXT("hidden on boards"))
				: FString(TEXT("Not set")))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				Button(LOCTEXT("EditProfile", "EDIT PROFILE"), FOnClicked::CreateLambda([this]()
				{
					if (const UCampusBackend* A = Backend()) { NameText = A->PlayerName; RollText = A->RollNo; BranchText = A->Branch; CollegeText = A->College; }
					Show(EScreen::Nickname);
					return FReply::Handled();
				}), EButton::Dark, 18, FVector2D(220.f, 56.f))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				Button(LOCTEXT("PickCollege", "COLLEGE"), FOnClicked::CreateLambda([this]()
				{
					if (UCampusBackend* A = Backend())
					{
						// Seed the picker from what the server currently holds, then fetch the list.
						PickedCollegeId = A->CollegeId;
						bShowCollegeTick = A->bShowCollege;
						Busy(TEXT("Loading colleges..."));
						A->LoadColleges([this](bool bOk, const FString& Error)
						{
							Done(bOk, Error, [this]() { Show(EScreen::CollegePick); });
						});
					}
					return FReply::Handled();
				}), EButton::Dark, 18, FVector2D(190.f, 56.f))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
			[
				bGuest
					? Button(LOCTEXT("Bind", "SAVE PROGRESS (REGISTER)"), FOnClicked::CreateLambda([this]() { Show(EScreen::Register); return FReply::Handled(); }), EButton::Go, 18, FVector2D(320.f, 56.f))
					: SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				Button(LOCTEXT("Logout", "LOG OUT"), FOnClicked::CreateLambda([this]()
				{
					if (UCampusBackend* A = Backend()) { A->Logout(); }
					if (UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr) { Instance->bSignedIn = false; }
					Show(EScreen::LoginChoice);
					return FReply::Handled();
				}), EButton::Ghost, 18, FVector2D(160.f, 56.f))
			]
		];
	if (bGuest)
	{
		Body->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
		[
			Label(LOCTEXT("GuestLogoutWarn", "Guest: logging out or reinstalling loses this account unless you register first."), 15, Danger, false)
		];
	}
	// Named ACCOUNT, not PROFILE: the profile PAGE is the new full-screen identity sheet, and two
	// screens with the same title is how a player stops trusting either of them.
	// SCROLLED since the picture block landed on top of it: on a 1080p phone in landscape the sheet is
	// now taller than the safe area, and a dialog whose LOG OUT cannot be reached is a bug report.
	return Dialog(LOCTEXT("AccountTitle", "ACCOUNT"),
		StaticCastSharedRef<SWidget>(SNew(SBox).MaxDesiredHeight(560.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()[Body]
		]), 860.f, true, EScreen::Lobby);
}

#undef LOCTEXT_NAMESPACE
