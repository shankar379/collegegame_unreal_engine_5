// Menu screens built in Slate (no widget assets to cook):
//   Lobby level: Splash -> Sign in -> Event post (optional) -> Lobby -> Create / Join / Settings.
//   Match level: Waiting room -> (game HUD lives in SCampusTouchControls) -> Settings -> Win screen.
//
// Look: Garena Free Fire. Sharp edges only - every panel, tab, counter and button is an
// FCampusShape (slanted parallelogram or clipped corner box, see CampusAngular.h), bold condensed
// italic uppercase type, dark translucent fills, one accent (#FFB800) with white text.

#pragma once

#include "CoreMinimal.h"
#include "Campus/CampusAngular.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class ACampusLobbyPlayerController;
class UCampusBackend;
class UCampusParty;
class UCampusPlayBilling;
class UCampusSave;
struct FCampusOffer;
struct FCampusMission;
struct FCampusPartyMember;
struct FCampusPlayProduct;
class ACampusMatchPlayerController;
class SBox;

/** Shared look: colours, fonts, angular buttons, angular panels. */
namespace CampusStyle
{
	enum class EButton : uint8 { Dark, Accent, Go, Ghost };

	/** Display type (bold condensed italic) when bBold, readable body type otherwise. */
	FSlateFontInfo Font(int32 Size, bool bBold = true);

	/** Sharp-cornered fallback brushes for the few places that still want a plain SBorder. */
	const FSlateBrush* PanelBrush();
	const FSlateBrush* CardBrush(bool bSelected);

	TSharedRef<SWidget> Button(const FText& Label, FOnClicked OnClicked, EButton Kind = EButton::Dark,
		int32 FontSize = 22, FVector2D MinSize = FVector2D(220.f, 64.f), bool bEnabled = true);

	/** Free Fire's yellow slanted call to action (bottom-right START). */
	TSharedRef<SWidget> StartButton(const FText& Label, FOnClicked OnClicked, FVector2D MinSize = FVector2D(300.f, 96.f));

	/** Slanted dark menu tab (left-hand STORE / CHARACTER / VAULT / WEAPONS column). */
	TSharedRef<SWidget> MenuTab(const FText& Label, CampusAngular::EGlyph Glyph, FOnClicked OnClicked,
		bool bHighlighted = false, FVector2D MinSize = FVector2D(248.f, 62.f));

	/** Small slanted selector pill (team size, map, rounds, store tabs). */
	TSharedRef<SWidget> Pill(const FText& Label, bool bSelected, FOnClicked OnClicked, FVector2D MinSize = FVector2D(140.f, 48.f));

	/** Square clipped-corner icon button: friends, mail, settings gear, close. */
	TSharedRef<SWidget> IconButton(CampusAngular::EGlyph Glyph, FOnClicked OnClicked, float Size = 60.f,
		FLinearColor Tint = FLinearColor::White, bool bAccentFill = false);

	/** Slanted currency counter with an optional "+" cap, like the coin / gem chips. */
	TSharedRef<SWidget> Counter(CampusAngular::EGlyph Glyph, const FLinearColor& Tint, TAttribute<FText> Value, FOnClicked OnPlus = FOnClicked());

	/** Clipped-corner dark panel. */
	TSharedRef<SWidget> Panel(TSharedRef<SWidget> Content, FMargin ContentPadding = FMargin(30.f, 22.f), float Cut = 18.f, bool bStrong = false);

	/** Card plate; the selected one gets an accent border and left edge. */
	TSharedRef<SWidget> Card(TSharedRef<SWidget> Content, bool bSelected, FMargin ContentPadding = FMargin(18.f, 12.f));

	TSharedRef<SWidget> Label(const FText& Text, int32 Size, FLinearColor Color = FLinearColor::White, bool bBold = true);

	extern const FLinearColor Accent;
	extern const FLinearColor Go;
	extern const FLinearColor Muted;
}

/** The one options panel, shared by the lobby gear and the in-match gear. */
namespace CampusOptions
{
	/** Builds the rows; OnChanged is called after every tap so the caller can rebuild and persist. */
	TSharedRef<SWidget> Body(UCampusSave* Save, TFunction<void()> OnChanged);

	/** Pushes the saved graphics choice into the engine's scalability levels. */
	void ApplyGraphics(const UCampusSave* Save);
}

/** Empty area that reports horizontal drags (mouse or one finger). Used to turn the squad. */
class SCampusDragArea : public SLeafWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnDrag, float);

	SLATE_BEGIN_ARGS(SCampusDragArea) {}
		SLATE_EVENT(FOnDrag, OnDrag)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override { return LayerId; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void Drag(float DeltaX, float DeltaTime);

	FOnDrag OnDrag;
	bool bDragging = false;
	int32 PointerIndex = INDEX_NONE;
	float LastX = 0.f;
	double LastMoveTime = 0.0;
	/** Units per second at release; keeps the character spinning like a lobby turntable, then eases out. */
	float Velocity = 0.f;
};

class SCampusLobbyWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusLobbyWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACampusLobbyPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SCampusLobbyWidget();
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	// Free Fire / PUBG style flow: Splash -> (first time) Agreement -> Login choice -> Email login / Register ->
	// (first time) Nickname -> Lobby. The lobby opens the Store, Rooms (quick / create / join by code), Account.
	enum class EScreen : uint8 { Splash, Agreement, LoginChoice, EmailLogin, Register, Nickname, Lobby, Store, Rooms,
		Account, Event, Daily, Settings, Missions, Streak, AdultConfirm, CollegePick, MapSelect };
	enum class EStoreTab : uint8 { Guns, Offers, Packs };
	enum class EMissionTab : uint8 { Daily, Weekly, Weekend };

	void Show(EScreen Screen);
	void Refresh();
	void Toast(const FString& Message);
	/** Runs an API call with a "please wait" overlay; the reply re-enables the screen. */
	void Busy(const FString& What);
	void Done(bool bOk, const FString& Error, TFunction<void()> OnOk = nullptr);
	/** Where to go once signed in: the nickname screen the first time, else the lobby. */
	void AfterSignIn();
	void SaveMatchChoice();

	UCampusBackend* Backend() const;
	UCampusSave* Profile() const;
	UCampusParty* Party() const;
	/** Pushes the local player into the squad roster and keeps the plate row the right width. */
	void SyncParty();

	TSharedRef<SWidget> Backdrop(float Alpha) const;
	TSharedRef<SWidget> Dialog(const FText& Title, TSharedRef<SWidget> Body, float Width, bool bClose, EScreen CloseTo);
	TSharedRef<SWidget> Field(const FText& Hint, FString& Target, bool bPassword = false);

	TSharedRef<SWidget> BuildSplash();
	TSharedRef<SWidget> BuildAgreement();
	TSharedRef<SWidget> BuildLoginChoice();
	TSharedRef<SWidget> BuildEmailLogin();
	TSharedRef<SWidget> BuildRegister();
	TSharedRef<SWidget> BuildNickname();
	TSharedRef<SWidget> BuildLobby();
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildMenuColumn();
	TSharedRef<SWidget> BuildSquadRow();
	TSharedRef<SWidget> BuildNamePlate(const FCampusPartyMember& Member);
	TSharedRef<SWidget> BuildInvitePlate();
	TSharedRef<SWidget> BuildModeSelect();

	// ---- map / mode select (Campus/CampusMapSelect.cpp). Opened from the lobby plate above START.
	/** The lobby plate next to START: the chosen mode over the chosen map, tap to change. */
	TSharedRef<SWidget> BuildModePlate();
	/** The full screen: map banners on the left, mode cards on the right, CONFIRM bottom-right. */
	TSharedRef<SWidget> BuildMapSelect();
	/** One left-hand map banner. */
	TSharedRef<SWidget> BuildMapBanner(int32 Index);
	/** One right-hand mode card, with its green "?" badge and any lock / time row. */
	TSharedRef<SWidget> BuildModeCard(int32 Index);
	/** Opens the map-select screen with the saved choice loaded as the pending one. */
	void OpenMapSelect();
	/** CONFIRM: writes the pending choice to the profile and goes back to the lobby. */
	void ConfirmMapSelect();
	TSharedRef<SWidget> BuildStore();
	TSharedRef<SWidget> BuildGunCard(int32 Index);
	TSharedRef<SWidget> BuildOfferCard(const FCampusOffer& Offer);
	/** The real-money button: Google Play where it is available, Razorpay everywhere else. */
	TSharedRef<SWidget> MoneyButton(const FString& ItemId, const FString& ItemName, float PriceInr, FVector2D Size);
	/** Play Billing for this game instance, or null off Android / when IAP is not packaged. */
	UCampusPlayBilling* Billing() const;
	TSharedRef<SWidget> BuildRooms();
	TSharedRef<SWidget> BuildAccount();
	TSharedRef<SWidget> BuildDaily();
	TSharedRef<SWidget> BuildEvent();
	TSharedRef<SWidget> BuildSettings();

	// ---- engagement
	/** The weekend board panel under the menu tabs: countdown, top 3, my row, empty state. */
	TSharedRef<SWidget> BuildWeekendPanel();
	/** The banner for a settled weekend's unclaimed reward. */
	TSharedRef<SWidget> BuildClaimBanner();
	TSharedRef<SWidget> BuildMissions();
	TSharedRef<SWidget> BuildMissionCard(const FCampusMission& Mission, const FString& PeriodId);
	TSharedRef<SWidget> BuildStreak();
	TSharedRef<SWidget> BuildAdultConfirm();
	TSharedRef<SWidget> BuildCollegePick();
	/** One live "2D 04:11:08" countdown, driven off the server clock by a text lambda (no rebuild). */
	TSharedRef<SWidget> Countdown(const FDateTime& Until, const FText& Prefix, int32 Size, FLinearColor Color) const;
	/** Refreshes GET /engagement/home and repaints when it lands. */
	void RefreshEngagement();
	/** Shared tail of every claim: wallet from the response, toast, repaint, reward-cap message. */
	void AfterClaim(bool bOk, const FString& Error, const FString& OkText);
	/** The IST game day (YYYYMMDD) by the server clock, for the once-a-day streak popup. */
	FString ServerGameDay() const;

	TWeakObjectPtr<ACampusLobbyPlayerController> Controller;
	TSharedPtr<SBox> Content;
	EScreen Current = EScreen::Splash;
	EStoreTab StoreTab = EStoreTab::Guns;
	/** The confirmed choice, mirrored from UCampusSave; indices into CampusCatalog. */
	int32 SelectedMap = 0;
	int32 SelectedMode = 0;
	int32 SelectedTeamSize = 4;
	/** What the map-select screen is showing before CONFIRM; closing with X throws it away. */
	int32 PendingMap = 0;
	int32 PendingMode = 0;
	bool bPendingRandomMap = false;
	/** Mode whose "?" rules sheet is open, or INDEX_NONE. */
	int32 HelpMode = INDEX_NONE;
	float SplashTime = 0.f;
	int32 SplashTip = 0;
	bool bSplashReady = false;
	bool bResuming = false;
	bool bAgreeTicked = false;
	float ToastTime = 0.f;
	FString ToastText;
	bool bBusy = false;
	FString BusyText;
	FDelegateHandle PartyChangedHandle;

	// ---- engagement / 18+ state
	EMissionTab MissionTab = EMissionTab::Daily;
	/** The "I am 18 or older" tick on the sign-in screen; nothing proceeds without it. */
	bool bAdultTicked = false;
	/** The streak popup has been offered this session, so it never re-opens behind the player. */
	bool bStreakOffered = false;
	/** A claim is in flight: every CLAIM button is disabled so nothing is double-tapped. */
	bool bClaiming = false;
	/** Profile screen: the college chosen but not yet saved, and the board-privacy tick. */
	FString PickedCollegeId;
	bool bShowCollegeTick = false;
	FString CollegeFilter;

	// form fields
	FString EmailText;
	FString PasswordText;
	FString NameText;
	FString RollText;
	FString BranchText;
	FString CollegeText;
	FString CodeText;
};

class SCampusMatchWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusMatchWidget) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACampusMatchPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<SWidget> BuildWaiting();
	TSharedRef<SWidget> BuildWin();
	TSharedRef<SWidget> BuildSettings();
	TSharedRef<SWidget> TeamColumn(uint8 Team);

	TWeakObjectPtr<ACampusMatchPlayerController> Controller;
	TSharedPtr<SBox> Content;
	FString RoomSignature() const;

	int32 ShownPhase = -1;
	FString ShownSignature;
	bool bShownSettings = false;
};
