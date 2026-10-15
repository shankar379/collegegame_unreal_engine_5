// EVERYTHING BEFORE THERE IS AN ACCOUNT (Slate, lobby level). One screen per step, in order:
//
//   Splash        loading bar + one rotating tip, TAP TO START. Also the screen a saved session
//                 is resumed behind, so the player sees progress rather than a frozen logo.
//   Agreement     terms + privacy, first launch only. Nothing proceeds until the box is ticked.
//   LoginChoice   PLAY AS GUEST / LOGIN / REGISTER, with the 18+ tick that gates all three.
//   EmailLogin    POST /auth/login
//   Register      POST /auth/register
//   Nickname      the first PATCH /me, first launch only.
//
// Split out of Campus/CampusLobbyUI.cpp; Campus/CampusLobbyUIInternal.h carries the map of which
// screen lives in which file. Every function here is a member of SCampusLobbyWidget and reaches
// the API only through UCampusBackend - nothing about an account is decided on the phone.

#include "Campus/CampusUI.h"

#include "Campus/CampusBackend.h"
#include "Campus/CampusLobbyUIInternal.h"
#include "Campus/CampusSave.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CampusLobbyUI"

using namespace CampusStyle;

// ---------------------------------------------------------------------------------------------
// Splash -> Agreement -> Login

TSharedRef<SWidget> SCampusLobbyWidget::BuildSplash()
{
	TSharedRef<SVerticalBox> Bottom = SNew(SVerticalBox);
	if (bSplashReady)
	{
		Bottom->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[
			CampusStyle::StartButton(LOCTEXT("TapStart", "TAP TO START"), FOnClicked::CreateLambda([this]()
			{
				const UCampusBackend* Api = Backend();
				const UCampusSave* Save = Profile();
				if (Api && Api->IsSignedIn())
				{
					AfterSignIn();
				}
				else
				{
					Show(Save && Save->bAgreedTerms ? EScreen::LoginChoice : EScreen::Agreement);
				}
				return FReply::Handled();
			}), FVector2D(440.f, 88.f))
		];
	}
	else
	{
		Bottom->AddSlot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(SBox).WidthOverride(560.f).HeightOverride(12.f)
			[
				SNew(SProgressBar)
				.Percent_Lambda([this]() { return TOptional<float>(FMath::Clamp(SplashTime / 2.1f, 0.f, 1.f)); })
				.FillColorAndOpacity(Accent)
			]
		];
		Bottom->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("Loading campus... %d%%"), FMath::RoundToInt(FMath::Clamp(SplashTime / 2.1f, 0.f, 1.f) * 100.f))); })
			.Font(Font(18, false))
			.ColorAndOpacity(Muted)
		];
	}

	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(FLinearColor(0.02f, 0.025f, 0.035f, 1.f))
		[
			SNew(SSafeZone).Padding(FMargin(30.f, 24.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[CampusAngular::Logo(84)]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)
					[
						Label(LOCTEXT("Tagline", "YOUR COLLEGE. YOUR SQUAD. LAST ONE STANDING."), 20, Muted)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 18.f)[Bottom]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Label(FText::FromString(Tips[SplashTip]), 18, Muted, false)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
				[
					Label(LOCTEXT("Version", "v0.5  ·  test server"), 14, Muted, false)
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildAgreement()
{
	// DPDP notice before any account exists: what is stored, why, how long, how to delete.
	const FText Notice = LOCTEXT("Notice",
		"Campus Combat is a just-for-fun game: students and colleges play friendly matches against each other. "
		"There are no cash prizes, no betting and no real-money rewards. Diamonds, Room Cards and items are for use "
		"inside the game only, have no cash value and cannot be withdrawn, sold or exchanged for money.\n\n"
		"College names appear only for friendly rivalry on leaderboards. Campus Combat is not run, endorsed or "
		"sponsored by any college or university, and is not affiliated with any other game or publisher.\n\n"
		"Play fair and be respectful: cheating, hacks, abuse or harassment can get an account banned.\n\n"
		"To play we store: a game ID for this phone, your nickname, and (if you add them) email, roll no., branch "
		"and college for leaderboards; your Room Cards, Diamonds, guns and purchases; and match results.\n\n"
		"Why: to keep your progress and run fair matches. Roll numbers are shown masked (21XX..45).\n"
		"How long: while your account exists. Delete it any time in Account > Delete account; it is erased within 7 days.\n"
		"Payments are processed by Razorpay; we never see your card or UPI PIN.");

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SScrollBox) + SScrollBox::Slot()
			[
				SNew(STextBlock).Text(Notice).Font(Font(18, false)).ColorAndOpacity(FLinearColor::White).AutoWrapText(true)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { return bAgreeTicked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAgreeTicked = State == ECheckBoxState::Checked; Refresh(); })
			[
				Label(LOCTEXT("Agree", "I have read and agree to the Terms of Service and Privacy Policy"), 18, FLinearColor::White, false)
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 18.f, 0.f, 0.f)
		[
			Button(LOCTEXT("Accept", "ACCEPT"), FOnClicked::CreateLambda([this]()
			{
				if (UCampusSave* Save = Profile())
				{
					Save->bAgreedTerms = true;
					Save->Persist();
				}
				Show(EScreen::LoginChoice);
				return FReply::Handled();
			}), EButton::Go, 24, FVector2D(320.f, 72.f), bAgreeTicked)
		];
	return Dialog(LOCTEXT("AgreementTitle", "TERMS & PRIVACY"), Body, 900.f, false, EScreen::Agreement);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildLoginChoice()
{
	// 18+ only (migration 009). Nothing on this screen works until the box is ticked, and the tick
	// rides along with every sign-in body. We never ask for or store a date of birth.
	if (const UCampusBackend* Known = Backend(); Known && Known->HasAdultTick())
	{
		bAdultTicked = true;
	}
	const bool bMayProceed = bAdultTicked;

	auto Guest = [this]()
	{
		if (UCampusBackend* Api = Backend())
		{
			Api->SetAdultTick(true);
			Busy(TEXT("Signing in..."));
			Api->SignInGuest([this](bool bOk, const FString& Error)
			{
				bBusy = false;
				if (bOk) { AfterSignIn(); } else { Toast(Error); Refresh(); }
			});
		}
		return FReply::Handled();
	};

	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.55f)]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			CampusStyle::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[CampusAngular::Logo(50)]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 18.f)
				[
					Label(LOCTEXT("ChooseLogin", "CHOOSE HOW TO SIGN IN"), 18, Muted)
				]
				// The 18+ gate. A plain, legible tick with a 60px target - not a tiny checkbox.
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 20.f)
				[
					SNew(SCampusAngularButton)
					.Shape(FCampusShape::Notched(14.f))
					.Fill(bAdultTicked ? FLinearColor(0.08f, 0.26f, 0.14f, 0.92f) : FLinearColor(0.f, 0.f, 0.f, 0.45f))
					.Border(bAdultTicked ? CampusAngular::Go : CampusAngular::Accent)
					.BorderWidth(2.f)
					.ContentPadding(FMargin(18.f, 12.f))
					.OnClicked(FOnClicked::CreateLambda([this]()
					{
						bAdultTicked = !bAdultTicked;
						if (UCampusBackend* Api = Backend())
						{
							Api->SetAdultTick(bAdultTicked);
						}
						Refresh();
						return FReply::Handled();
					}))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
						[
							// Box + tick: the state is a shape, not only a colour.
							SNew(SBox).WidthOverride(28.f).HeightOverride(28.f)
							[
								SNew(SCampusAngularPanel)
								.Shape(FCampusShape::Notched(7.f))
								.Fill(bAdultTicked ? CampusAngular::Go : FLinearColor(0.f, 0.f, 0.f, 0.5f))
								.Border(FLinearColor(1.f, 1.f, 1.f, 0.5f))
								.BorderWidth(1.f)
								[
									SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
									[
										bAdultTicked
											? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(TEXT("X")))
												.Font(CampusAngular::Display(18)).ColorAndOpacity(FLinearColor::White))
											: SNullWidget::NullWidget
									]
								]
							]
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Adult18", "I AM 18 OR OLDER"))
							.Font(CampusAngular::Display(20))
							.ColorAndOpacity(bAdultTicked ? FLinearColor::White : Accent)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 16.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Adult18Hint", "This game is for players aged 18 and over. We do not store your date of birth."))
					.Font(CampusAngular::Body(14))
					.ColorAndOpacity(Muted)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(LOCTEXT("Guest", "PLAY AS GUEST"), FOnClicked::CreateLambda(Guest), EButton::Go, 26, FVector2D(460.f, 84.f), bMayProceed)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 18.f)
				[
					Label(LOCTEXT("GuestWarn", "Guest progress lives on this phone only. Register to keep it on any phone."), 15, CardGold, false)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)
					[
						Button(LOCTEXT("Login", "LOGIN"), FOnClicked::CreateLambda([this]() { Show(EScreen::EmailLogin); return FReply::Handled(); }), EButton::Accent, 22, FVector2D(220.f, 68.f), bMayProceed)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)
					[
						Button(LOCTEXT("Register", "REGISTER"), FOnClicked::CreateLambda([this]() { Show(EScreen::Register); return FReply::Handled(); }), EButton::Dark, 22, FVector2D(220.f, 68.f), bMayProceed)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 14.f, 0.f, 0.f)
				[
					Button(LOCTEXT("Google", "GOOGLE (COMING SOON)"), FOnClicked(), EButton::Ghost, 18, FVector2D(460.f, 56.f), false)
				],
				FMargin(56.f, 40.f), 26.f, true)
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildEmailLogin()
{
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Field(LOCTEXT("Email", "Email"), EmailText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Field(LOCTEXT("Password", "Password"), PasswordText, true)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 20.f, 0.f, 0.f)
		[
			Button(LOCTEXT("LoginGo", "LOGIN"), FOnClicked::CreateLambda([this]()
			{
				if (UCampusBackend* Api = Backend())
				{
					Busy(TEXT("Logging in..."));
					Api->Login(EmailText, PasswordText, [this](bool bOk, const FString& Error)
					{
						bBusy = false;
						PasswordText.Reset();
						if (bOk)
						{
							if (UCampusSave* Save = Profile()) { Save->bNicknameChosen = true; Save->Persist(); }
							AfterSignIn();
						}
						else { Toast(Error); Refresh(); }
					});
				}
				return FReply::Handled();
			}), EButton::Go, 24, FVector2D(320.f, 72.f))
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)
		[
			Button(LOCTEXT("NoAccount", "No account? REGISTER"), FOnClicked::CreateLambda([this]() { Show(EScreen::Register); return FReply::Handled(); }), EButton::Ghost, 18, FVector2D(320.f, 52.f))
		];
	return Dialog(LOCTEXT("LoginTitle", "LOGIN"), Body, 640.f, true, EScreen::LoginChoice);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildRegister()
{
	const UCampusBackend* Api = Backend();
	const bool bUpgrade = Api && Api->IsSignedIn() && Api->bGuest;
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(STextBlock)
			.Text(bUpgrade ? LOCTEXT("UpgradeNote", "Your guest progress (Room Cards, Diamonds, guns) moves to this account.")
				: LOCTEXT("RegNote", "Use any email you can read. Password: at least 8 characters."))
			.Font(Font(16, false)).ColorAndOpacity(Muted).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Field(LOCTEXT("Nick", "Nickname (3-16)"), NameText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Field(LOCTEXT("Email2", "Email"), EmailText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Field(LOCTEXT("Password2", "Password (8+ characters)"), PasswordText, true)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 20.f, 0.f, 0.f)
		[
			Button(bUpgrade ? LOCTEXT("SaveProgress", "SAVE MY PROGRESS") : LOCTEXT("CreateAcc", "CREATE ACCOUNT"), FOnClicked::CreateLambda([this, bUpgrade]()
			{
				if (UCampusBackend* Api = Backend())
				{
					Busy(TEXT("Creating account..."));
					Api->Register(EmailText, PasswordText, NameText, [this, bUpgrade](bool bOk, const FString& Error)
					{
						bBusy = false;
						PasswordText.Reset();
						if (bOk)
						{
							if (UCampusSave* Save = Profile()) { Save->bNicknameChosen = true; Save->Persist(); }
							if (bUpgrade) { Toast(TEXT("Account created. Your progress is saved.")); Show(EScreen::Lobby); }
							else { AfterSignIn(); }
						}
						else { Toast(Error); Refresh(); }
					});
				}
				return FReply::Handled();
			}), EButton::Go, 24, FVector2D(360.f, 72.f))
		];
	return Dialog(bUpgrade ? LOCTEXT("BindTitle", "SAVE PROGRESS") : LOCTEXT("RegTitle", "REGISTER"), Body, 680.f, true,
		bUpgrade ? EScreen::Account : EScreen::LoginChoice);
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildNickname()
{
	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
		[
			SNew(STextBlock).Text(LOCTEXT("NickNote", "This is how other players see you. Student details are optional (for college and branch leaderboards)."))
			.Font(Font(16, false)).ColorAndOpacity(Muted).AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Field(LOCTEXT("Nick2", "Nickname (3-16 letters / digits)"), NameText)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)[Field(LOCTEXT("Roll", "Roll no. (optional)"), RollText)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f, 0.f, 0.f, 0.f)[Field(LOCTEXT("Branch", "Branch e.g. CSE (optional)"), BranchText)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)[Field(LOCTEXT("College", "College (optional)"), CollegeText)]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 20.f, 0.f, 0.f)
		[
			Button(LOCTEXT("Continue", "CONTINUE"), FOnClicked::CreateLambda([this]()
			{
				if (UCampusBackend* Api = Backend())
				{
					Busy(TEXT("Saving..."));
					Api->UpdateProfile(NameText, RollText, BranchText, CollegeText, [this](bool bOk, const FString& Error)
					{
						bBusy = false;
						if (bOk)
						{
							if (UCampusSave* Save = Profile()) { Save->bNicknameChosen = true; Save->Persist(); }
							AfterSignIn();
						}
						else { Toast(Error); Refresh(); }
					});
				}
				return FReply::Handled();
			}), EButton::Go, 24, FVector2D(320.f, 72.f))
		];
	return Dialog(LOCTEXT("NickTitle", "CREATE YOUR PLAYER"), Body, 760.f, false, EScreen::Nickname);
}

#undef LOCTEXT_NAMESPACE
