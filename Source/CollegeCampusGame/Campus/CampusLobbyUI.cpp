// Lobby level UI (Slate, no widget assets). Flow like Free Fire / PUBG Mobile:
//   Splash (loading + tips, TAP TO START) -> first launch: Agreement (terms + privacy) -> Login choice
//   (PLAY AS GUEST / LOGIN / REGISTER) -> first launch: Nickname -> Lobby.
// Everything account / money related goes through UCampusBackend (the API); nothing is decided on the phone.

#include "Campus/CampusUI.h"

#include "Campus/CampusAngular.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusParty.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusSave.h"
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

namespace
{
	struct FMapDef
	{
		const TCHAR* Id;
		const TCHAR* Name;
		const TCHAR* Detail;
	};
	// GAME_PLAN 1.2
	const FMapDef Maps[] = {
		{ TEXT("Classic"), TEXT("Classic"), TEXT("College  ·  4 rounds  ·  first to 3") },
		{ TEXT("RealMap"), TEXT("Real Map"), TEXT("Battle royale  ·  up to 50") },
	};
	struct FTeamDef
	{
		int32 Size;
		const TCHAR* Name;
	};
	// GAME_PLAN 1.3
	const FTeamDef Teams[] = { { 1, TEXT("Solo") }, { 2, TEXT("Duo") }, { 4, TEXT("Squad") } };

	const TCHAR* Tips[] = {
		TEXT("Tip: run at the corridor wall and jump to vault down."),
		TEXT("Tip: hold the eye to look around without moving your aim."),
		TEXT("Tip: claim a free Room Card every day in the lobby."),
		TEXT("Tip: crouch + push the stick to the edge for a fast low run."),
		TEXT("Tip: upgrade a gun to raise its damage and accuracy."),
	};

	const FLinearColor Diamond = CampusAngular::Gem;
	const FLinearColor CardGold = CampusAngular::CoinGold;
	const FLinearColor Danger = CampusAngular::Danger;

	FString Money(float Inr)
	{
		return FString::Printf(TEXT("₹%s"), *FString::SanitizeFloat(Inr, 0));
	}

	/** One slot in the lobby squad row, as a fraction of the viewport width either side of centre.
	 *  ACampusLobbyPlayerController uses the same step to place the 3D characters, so a plate is
	 *  always under its own character whatever the screen size. */
	constexpr float SquadSlotFraction = 0.16f;

	/** The left-hand menu. Every tab lands on a screen that exists. */
	enum class ELobbyTab : uint8 { Store, Character, Vault, Weapons, Events };
	struct FTabDef
	{
		ELobbyTab Tab;
		const TCHAR* Label;
		CampusAngular::EGlyph Glyph;
	};
	const FTabDef Tabs[] = {
		{ ELobbyTab::Store,     TEXT("STORE"),     CampusAngular::EGlyph::Coin },
		{ ELobbyTab::Character, TEXT("CHARACTER"), CampusAngular::EGlyph::Friends },
		{ ELobbyTab::Vault,     TEXT("VAULT"),     CampusAngular::EGlyph::Gem },
		{ ELobbyTab::Weapons,   TEXT("WEAPONS"),   CampusAngular::EGlyph::Rank },
		{ ELobbyTab::Events,    TEXT("EVENTS"),    CampusAngular::EGlyph::Crown },
	};
}

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

void SCampusLobbyWidget::SyncParty()
{
	UCampusParty* Squad = Party();
	if (!Squad)
	{
		return;
	}
	// The plate row is as wide as the chosen squad size, and the local player is always the leader.
	Squad->SetCapacity(SelectedTeamSize);
	const UCampusBackend* Api = Backend();
	const UCampusSave* Save = Profile();
	Squad->SetLeader(Api ? Api->PlayerName : TEXT("YOU"), Save ? Save->GetLevel() : 1,
		Save ? FMath::Clamp(Save->Wins / 5, 0, 5) : 0);
}

void SCampusLobbyWidget::Construct(const FArguments& InArgs)
{
	Controller = InArgs._Controller;
	if (const UCampusSave* Save = Profile())
	{
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Maps); ++Index)
		{
			SelectedMap = Save->MatchMap == FName(Maps[Index].Id) ? Index : SelectedMap;
		}
		SelectedTeamSize = Save->TeamSize;
	}
	CampusOptions::ApplyGraphics(Profile());
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
			.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.5f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Visibility_Lambda([this]() { return bBusy ? EVisibility::Visible : EVisibility::Collapsed; })
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
}

void SCampusLobbyWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	ToastTime = FMath::Max(0.f, ToastTime - InDeltaTime);

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

	if (UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr)
	{
		if (!Instance->PendingMessage.IsEmpty() && Current != EScreen::Splash)
		{
			Toast(Instance->PendingMessage);
			Instance->PendingMessage.Reset();
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
	Current = Screen;
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
	case EScreen::Store: Body = BuildStore(); break;
	case EScreen::Rooms: Body = BuildRooms(); break;
	case EScreen::Account: Body = BuildAccount(); break;
	case EScreen::Event: Body = BuildEvent(); break;
	case EScreen::Daily: Body = BuildDaily(); break;
	case EScreen::Settings: Body = BuildSettings(); break;
	}
	Content->SetContent(Body);
}

void SCampusLobbyWidget::Refresh()
{
	Show(Current);
}

void SCampusLobbyWidget::AfterSignIn()
{
	UCampusSave* Save = Profile();
	const UCampusBackend* Api = Backend();
	if (Save && !Save->bNicknameChosen)
	{
		NameText = Api ? Api->PlayerName : FString();
		Show(EScreen::Nickname);
		return;
	}
	if (UCampusGameInstance* Instance = Controller.IsValid() ? Controller->GetCampusInstance() : nullptr)
	{
		Instance->bSignedIn = true;
		const bool bShowEvent = !Instance->bEventSeen;
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
		Save->MatchMap = FName(Maps[SelectedMap].Id);
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
	auto Guest = [this]()
	{
		if (UCampusBackend* Api = Backend())
		{
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
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 12.f, 0.f, 26.f)
				[
					Label(LOCTEXT("ChooseLogin", "CHOOSE HOW TO SIGN IN"), 18, Muted)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					Button(LOCTEXT("Guest", "PLAY AS GUEST"), FOnClicked::CreateLambda(Guest), EButton::Go, 26, FVector2D(460.f, 84.f))
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
						Button(LOCTEXT("Login", "LOGIN"), FOnClicked::CreateLambda([this]() { Show(EScreen::EmailLogin); return FReply::Handled(); }), EButton::Accent, 22, FVector2D(220.f, 68.f))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f)
					[
						Button(LOCTEXT("Register", "REGISTER"), FOnClicked::CreateLambda([this]() { Show(EScreen::Register); return FReply::Handled(); }), EButton::Dark, 22, FVector2D(220.f, 68.f))
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

// ---------------------------------------------------------------------------------------------
// Lobby

TSharedRef<SWidget> SCampusLobbyWidget::BuildTopBar()
{
	const UCampusBackend* Api = Backend();
	const UCampusSave* Save = Profile();
	const FString Name = Api && !Api->PlayerName.IsEmpty() ? Api->PlayerName : TEXT("GUEST");
	const int32 Level = Save ? Save->GetLevel() : 1;
	// XP inside the current level (GetLevel() is 1 + Xp / 500).
	const float LevelProgress = Save ? float(Save->Xp % 500) / 500.f : 0.f;

	// Avatar + name + level + XP bar, the whole plate taps through to the profile.
	TSharedRef<SWidget> Identity = SNew(SCampusAngularButton)
		.Shape(FCampusShape::Notched(18.f))
		.Fill(FLinearColor(0.02f, 0.03f, 0.04f, 0.84f))
		.Border(CampusAngular::Hairline)
		.BorderWidth(1.f)
		.AccentEdge(CampusAngular::Accent)
		.AccentEdgeWidth(5.f)
		.ContentPadding(FMargin(16.f, 8.f, 22.f, 8.f))
		.OnClicked(FOnClicked::CreateLambda([this]() { Show(EScreen::Account); return FReply::Handled(); }))
		[
			SNew(SHorizontalBox)
			// Avatar: a clipped-corner plate with the level number, until there are portraits.
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
			[
				SNew(SBox).WidthOverride(54.f).HeightOverride(54.f)
				[
					SNew(SCampusAngularPanel)
					.Shape(FCampusShape::Clipped(12.f))
					.Fill(FLinearColor(0.10f, 0.08f, 0.03f, 0.95f))
					.Border(CampusAngular::Accent)
					.BorderWidth(2.f)
					[
						SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::AsNumber(Level)).Font(CampusAngular::Display(26)).ColorAndOpacity(Accent)
						]
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
			CampusStyle::IconButton(CampusAngular::EGlyph::Friends, FOnClicked::CreateLambda([this]()
			{
				// No friends list in the API yet; the room code is the real invite channel.
				Toast(TEXT("Friends list is coming. Invite with a room code for now."));
				Show(EScreen::Rooms);
				return FReply::Handled();
			}), 58.f)
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

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible);
	for (const FTabDef& Def : Tabs)
	{
		FOnClicked Action;
		bool bHighlighted = false;
		switch (Def.Tab)
		{
		case ELobbyTab::Store:     Action = OpenStore(EStoreTab::Offers); break;
		case ELobbyTab::Weapons:   Action = OpenStore(EStoreTab::Guns); break;
		case ELobbyTab::Character: Action = OpenScreen(EScreen::Account); break;
		case ELobbyTab::Vault:     Action = OpenScreen(EScreen::Daily); bHighlighted = bDailyReady; break;
		case ELobbyTab::Events:    Action = OpenScreen(EScreen::Event); break;
		}
		Column->AddSlot().AutoHeight().Padding(0.f, 5.f)
		[
			CampusStyle::MenuTab(FText::FromString(Def.Label), Def.Glyph, Action, bHighlighted)
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

	return SNew(SCampusAngularPanel)
		.Shape(FCampusShape::Wedge(14.f, 10.f))
		.Fill(Member.bAccepted ? FLinearColor(0.015f, 0.02f, 0.03f, 0.88f) : FLinearColor(0.015f, 0.02f, 0.03f, 0.45f))
		.Border(Edge)
		.BorderWidth(Member.bLeader ? 2.f : 1.f)
		.AccentEdge(Edge)
		.AccentEdgeWidth(4.f)
		.ContentPadding(FMargin(18.f, 7.f, 14.f, 7.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Member.Name.ToUpper()))
				.Font(CampusAngular::Display(19))
				.ColorAndOpacity(Member.bAccepted ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.5f))
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
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildInvitePlate()
{
	// An empty slot: tap to invite. Room codes are the only invite channel the API has today.
	return SNew(SCampusAngularButton)
		.Shape(FCampusShape::Wedge(14.f, 10.f))
		.Fill(FLinearColor(0.f, 0.f, 0.f, 0.35f))
		.Border(FLinearColor(1.f, 1.f, 1.f, 0.22f))
		.BorderWidth(1.f)
		.ContentPadding(FMargin(14.f, 10.f))
		.OnClicked(FOnClicked::CreateLambda([this]() { Show(EScreen::Rooms); return FReply::Handled(); }))
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

	TArray<int32> Order;
	UCampusParty::BuildFillOrder(Capacity, Order);

	// Anchor each plate at the same screen fraction the controller used for its character, so a
	// plate sits under its own character at any resolution.
	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas).Visibility(EVisibility::SelfHitTestInvisible);
	for (int32 Index = 0; Index < Capacity; ++Index)
	{
		const int32 Slot = Order.IsValidIndex(Index) ? Order[Index] : Index;
		const float Fraction = 0.5f + UCampusParty::SlotStep(Slot, Capacity) * SquadSlotFraction;
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
	TSharedRef<SHorizontalBox> MapRow = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Maps); ++Index)
	{
		const bool bSelected = SelectedMap == Index;
		MapRow->AddSlot().AutoWidth().Padding(4.f, 0.f)
		[
			SNew(SBox).WidthOverride(226.f).HeightOverride(80.f)
			[
				SNew(SCampusAngularButton)
				.Shape(FCampusShape::Wedge(22.f, 14.f))
				.Fill(bSelected ? FLinearColor(0.12f, 0.10f, 0.03f, 0.94f) : FLinearColor(0.02f, 0.03f, 0.04f, 0.82f))
				.Border(bSelected ? Accent : CampusAngular::Hairline)
				.BorderWidth(bSelected ? 2.f : 1.f)
				.AccentEdge(bSelected ? Accent : FLinearColor::Transparent)
				.AccentEdgeWidth(bSelected ? 5.f : 0.f)
				.ContentPadding(FMargin(26.f, 6.f, 18.f, 6.f))
				.OnClicked(FOnClicked::CreateLambda([this, Index]() { SelectedMap = Index; SaveMatchChoice(); Refresh(); return FReply::Handled(); }))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(FString(Maps[Index].Name).ToUpper()))
						.Font(CampusAngular::Display(23)).ColorAndOpacity(bSelected ? Accent : FLinearColor::White)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Maps[Index].Detail))
						.Font(CampusAngular::Body(12)).ColorAndOpacity(bSelected ? FLinearColor(1.f, 1.f, 1.f, 0.85f) : Muted)
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
			CampusStyle::Pill(FText::FromString(FString(Team.Name).ToUpper()), SelectedTeamSize == Size,
				FOnClicked::CreateLambda([this, Size]()
				{
					SelectedTeamSize = Size;
					SaveMatchChoice();
					// The squad row and the 3D line-up follow the chosen size straight away.
					SyncParty();
					Refresh();
					return FReply::Handled();
				}), FVector2D(136.f, 46.f))
		];
	}
	return SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 8.f)[TeamRow]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[MapRow];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildLobby()
{
	SyncParty();

	return SNew(SOverlay)
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
				+ SOverlay::Slot().VAlign(VAlign_Top)[BuildTopBar()]
				// The logo sits under the corner icon cluster, right-aligned.
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 86.f, 4.f, 0.f)
				[
					SNew(SBox).Visibility(EVisibility::HitTestInvisible)[CampusAngular::Logo(40)]
				]
				// Slanted menu tabs down the left.
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(0.f, 30.f, 0.f, 0.f)
				[
					BuildMenuColumn()
				]
				// Squad name plates, under the characters.
				+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 108.f)
				[
					BuildSquadRow()
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0.f, 0.f, 0.f, 86.f)
				[
					Label(LOCTEXT("DragHint", "DRAG TO TURN"), 14, FLinearColor(1.f, 1.f, 1.f, 0.35f))
				]
				// Bottom right: mode chips above the yellow slanted START.
				+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
				[
					SNew(SVerticalBox).Visibility(EVisibility::SelfHitTestInvisible)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 0.f, 0.f, 12.f)[BuildModeSelect()]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
					[
						CampusStyle::StartButton(LOCTEXT("Start", "START"),
							FOnClicked::CreateLambda([this]() { Show(EScreen::Rooms); return FReply::Handled(); }), FVector2D(300.f, 96.f))
					]
				]
			]
		];
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

// ---------------------------------------------------------------------------------------------
// Store: text cards until there are icons

TSharedRef<SWidget> SCampusLobbyWidget::BuildGunCard(int32 Index)
{
	UCampusBackend* Api = Backend();
	const FCampusGun Gun = Api->Guns[Index];
	const bool bEquipped = Api->EquippedGun == Gun.Id;

	TSharedRef<SVerticalBox> Perks = SNew(SVerticalBox);
	for (const FString& Perk : Gun.Perks)
	{
		Perks->AddSlot().AutoHeight()[Label(FText::FromString(TEXT("• ") + Perk), 15, FLinearColor::White, false)];
	}
	const FString Stats = FString::Printf(TEXT("DMG %s   RATE %sx   ACC %d%%   MAG %d"),
		*FString::SanitizeFloat(Gun.Damage, 0), *FString::SanitizeFloat(Gun.FireRate, 1), FMath::RoundToInt(Gun.Accuracy), Gun.Magazine);

	auto Act = [this](TFunction<void(UCampusBackend*, UCampusBackend::FDone)> Call, const FString& What, const FString& OkText)
	{
		return [this, Call, What, OkText]()
		{
			if (UCampusBackend* Api2 = Backend())
			{
				Busy(What);
				Call(Api2, [this, OkText](bool bOk, const FString& Error) { Done(bOk, Error, [this, OkText]() { Toast(OkText); }); });
			}
			return FReply::Handled();
		};
	};
	const FString Id = Gun.Id;

	TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox);
	if (!Gun.bOwned)
	{
		if (Gun.PriceDiamonds > 0)
		{
			Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
			[
				Button(FText::FromString(FString::Printf(TEXT("%d DIAMONDS"), Gun.PriceDiamonds)), FOnClicked::CreateLambda(Act(
					[Id](UCampusBackend* A, UCampusBackend::FDone D) { A->BuyGun(Id, D); }, TEXT("Buying..."), Gun.Name + TEXT(" is yours!"))), EButton::Accent, 16, FVector2D(170.f, 50.f))
			];
		}
		if (Gun.PriceInr > 0.f)
		{
			Actions->AddSlot().AutoWidth()
			[
				Button(FText::FromString(Money(Gun.PriceInr)), FOnClicked::CreateLambda(Act(
					[Id](UCampusBackend* A, UCampusBackend::FDone D) { A->BuyWithMoney(Id, D); }, TEXT("Opening payment..."), Gun.Name + TEXT(" is yours!"))), EButton::Go, 16, FVector2D(110.f, 50.f))
			];
		}
	}
	else
	{
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			Button(bEquipped ? LOCTEXT("Equipped", "EQUIPPED") : LOCTEXT("Equip", "EQUIP"), FOnClicked::CreateLambda(Act(
				[Id](UCampusBackend* A, UCampusBackend::FDone D) { A->EquipGun(Id, D); }, TEXT("Equipping..."), Gun.Name + TEXT(" equipped"))),
				bEquipped ? EButton::Ghost : EButton::Go, 16, FVector2D(120.f, 50.f), !bEquipped)
		];
		Actions->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
		[
			Button(Gun.NextUpgradeDiamonds > 0 ? FText::FromString(FString::Printf(TEXT("UPGRADE  %d"), Gun.NextUpgradeDiamonds)) : LOCTEXT("Max", "MAX LEVEL"),
				FOnClicked::CreateLambda(Act([Id](UCampusBackend* A, UCampusBackend::FDone D) { A->UpgradeGun(Id, D); }, TEXT("Upgrading..."), Gun.Name + TEXT(" upgraded!"))),
				Gun.NextUpgradeDiamonds > 0 ? EButton::Accent : EButton::Ghost, 16, FVector2D(170.f, 50.f), Gun.NextUpgradeDiamonds > 0)
		];
		if (!Gun.bStarter)
		{
			Actions->AddSlot().AutoWidth()
			[
				Button(FText::FromString(FString::Printf(TEXT("SELL +%d"), Gun.SellDiamonds)), FOnClicked::CreateLambda(Act(
					[Id](UCampusBackend* A, UCampusBackend::FDone D) { A->SellGun(Id, D); }, TEXT("Selling..."), FString::Printf(TEXT("Sold for %d Diamonds"), Gun.SellDiamonds))),
					EButton::Dark, 16, FVector2D(130.f, 50.f))
			];
		}
	}

	FString Title = Gun.Name;
	if (Gun.bOwned)
	{
		Title += FString::Printf(TEXT("   Lv %d/%d"), Gun.Level, Gun.MaxLevel);
	}
	return SNew(SBox).WidthOverride(420.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(bEquipped ? FLinearColor(0.10f, 0.08f, 0.03f, 0.94f) : FLinearColor(0.03f, 0.04f, 0.06f, 0.9f))
			.Border(bEquipped ? Accent : CampusAngular::Hairline)
			.BorderWidth(bEquipped ? 2.f : 1.f)
			.AccentEdge(bEquipped ? Accent : FLinearColor::Transparent)
			.AccentEdgeWidth(bEquipped ? 5.f : 0.f)
			.ContentPadding(FMargin(22.f, 12.f, 18.f, 12.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)[Label(FText::FromString(Title), 24)]
					+ SHorizontalBox::Slot().AutoWidth()[Label(FText::FromString(Gun.Badge), 14, Accent)]
				]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Gun.Description), 14, Muted, false)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[Perks]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Stats), 14, Diamond)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[Actions]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildOfferCard(const FCampusOffer& Offer)
{
	TSharedRef<SVerticalBox> Perks = SNew(SVerticalBox);
	for (const FString& Perk : Offer.Perks)
	{
		Perks->AddSlot().AutoHeight()[Label(FText::FromString(TEXT("• ") + Perk), 16, FLinearColor::White, false)];
	}
	const FString Id = Offer.Id;
	return SNew(SBox).WidthOverride(330.f)
		[
			SNew(SCampusAngularPanel)
			.Shape(FCampusShape::Notched(16.f))
			.Fill(Offer.Badge.IsEmpty() ? FLinearColor(0.03f, 0.04f, 0.06f, 0.9f) : FLinearColor(0.10f, 0.08f, 0.03f, 0.94f))
			.Border(Offer.Badge.IsEmpty() ? CampusAngular::Hairline : Accent)
			.BorderWidth(Offer.Badge.IsEmpty() ? 1.f : 2.f)
			.AccentEdge(Offer.Badge.IsEmpty() ? FLinearColor::Transparent : Accent)
			.AccentEdgeWidth(Offer.Badge.IsEmpty() ? 0.f : 5.f)
			.ContentPadding(FMargin(22.f, 12.f, 18.f, 12.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Offer.Badge), 14, Accent)]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Offer.Name), 24)]
				+ SVerticalBox::Slot().AutoHeight()[Label(FText::FromString(Offer.Description), 14, Muted, false)]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)[Perks]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					Button(FText::FromString(TEXT("BUY  ") + Money(Offer.PriceInr)), FOnClicked::CreateLambda([this, Id]()
					{
						if (UCampusBackend* Api = Backend())
						{
							Busy(TEXT("Opening payment..."));
							Api->BuyWithMoney(Id, [this](bool bOk, const FString& Error) { Done(bOk, Error, [this]() { Toast(TEXT("Payment received! Added to your account.")); }); });
						}
						return FReply::Handled();
					}), EButton::Go, 18, FVector2D(290.f, 56.f))
				]
			]
		];
}

TSharedRef<SWidget> SCampusLobbyWidget::BuildStore()
{
	UCampusBackend* Api = Backend();
	auto Tab = [this](const FText& Text, EStoreTab Which)
	{
		return CampusStyle::Pill(Text, StoreTab == Which,
			FOnClicked::CreateLambda([this, Which]() { StoreTab = Which; Refresh(); return FReply::Handled(); }), FVector2D(186.f, 52.f));
	};

	TSharedRef<SScrollBox> Row = SNew(SScrollBox).Orientation(Orient_Horizontal);
	if (Api)
	{
		if (StoreTab == EStoreTab::Guns)
		{
			for (int32 Index = 0; Index < Api->Guns.Num(); ++Index)
			{
				Row->AddSlot().Padding(6.f, 0.f)[BuildGunCard(Index)];
			}
		}
		else
		{
			for (const FCampusOffer& Offer : StoreTab == EStoreTab::Offers ? Api->Offers : Api->Packs)
			{
				Row->AddSlot().Padding(6.f, 0.f)[BuildOfferCard(Offer)];
			}
		}
	}

	return SNew(SOverlay)
		+ SOverlay::Slot()[Backdrop(0.72f)]
		+ SOverlay::Slot()
		[
			SNew(SSafeZone).Padding(FMargin(24.f, 18.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 14.f, 0.f)
					[
						SNew(SCampusAngularPanel).Shape(FCampusShape::Slanted(10.f)).Fill(Accent)
						[
							SNew(SBox).WidthOverride(16.f).HeightOverride(34.f)
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 20.f, 0.f)[Label(LOCTEXT("StoreTitle", "STORE"), 34, FLinearColor::White)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[Tab(LOCTEXT("TabGuns", "GUNS"), EStoreTab::Guns)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[Tab(LOCTEXT("TabOffers", "OFFERS"), EStoreTab::Offers)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)[Tab(LOCTEXT("TabPacks", "DIAMONDS & CARDS"), EStoreTab::Packs)]
					+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(SSpacer)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
						[
							CampusStyle::Counter(CampusAngular::EGlyph::Coin, CardGold, FText::AsNumber(Api ? Api->RoomCards : 0))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							CampusStyle::Counter(CampusAngular::EGlyph::Gem, Diamond, FText::AsNumber(Api ? Api->Diamonds : 0))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						CampusStyle::IconButton(CampusAngular::EGlyph::Close,
							FOnClicked::CreateLambda([this]() { Show(EScreen::Lobby); return FReply::Handled(); }), 64.f)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					Label(Api && Api->bPaymentsEnabled ? LOCTEXT("PayOn", "Pay with UPI or card (Razorpay).")
						: LOCTEXT("PayOff", "Real-money payments switch on when Razorpay is connected. Diamond purchases work now."), 15, Muted, false)
				]
				+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Center).Padding(0.f, 12.f, 0.f, 0.f)[Row]
			]
		];
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
	const FString Mode = FString::Printf(TEXT("%s  ·  %s"), Maps[SelectedMap].Name,
		SelectedTeamSize == 1 ? TEXT("Solo") : (SelectedTeamSize == 2 ? TEXT("Duo") : TEXT("Squad")));
	const int32 Cards = Api ? Api->RoomCards : 0;

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
				}), EButton::Go, 20, FVector2D(160.f, 58.f)))
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
						Busy(TEXT("Creating room..."));
						Controller->HostRoom([this](const FString& Error) { Done(false, Error); });
					}
					return FReply::Handled();
				}), EButton::Accent, 20, FVector2D(160.f, 58.f), Cards > 0))
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
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("Nickname"), Api ? Api->PlayerName : FString())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("Account"), bGuest ? TEXT("Guest (this phone only)") : Api->Email)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("Roll no."), Api ? Api->RollNo : FString())]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f)[Line(TEXT("Branch / College"), Api ? FString::Printf(TEXT("%s  %s"), *Api->Branch, *Api->College) : FString())]
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
	return Dialog(LOCTEXT("AccountTitle", "PROFILE"), Body, 860.f, true, EScreen::Lobby);
}

#undef LOCTEXT_NAMESPACE
