// Menu screens built in Slate (no widget assets to cook):
//   Lobby level: Splash -> Sign in -> Event post (optional) -> Lobby -> Create / Join.
//   Match level: Waiting room -> (game HUD lives in SCampusTouchControls) -> Win screen.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class ACampusLobbyPlayerController;
class UCampusBackend;
class UCampusSave;
struct FCampusOffer;
class ACampusMatchPlayerController;
class SBox;

/** Shared look: colours, fonts, buttons, panels. */
namespace CampusStyle
{
	enum class EButton : uint8 { Dark, Accent, Go, Ghost };

	FSlateFontInfo Font(int32 Size, bool bBold = true);
	const FSlateBrush* PanelBrush();
	const FSlateBrush* CardBrush(bool bSelected);
	TSharedRef<SWidget> Button(const FText& Label, FOnClicked OnClicked, EButton Kind = EButton::Dark,
		int32 FontSize = 22, FVector2D MinSize = FVector2D(220.f, 64.f), bool bEnabled = true);
	TSharedRef<SWidget> Label(const FText& Text, int32 Size, FLinearColor Color = FLinearColor::White, bool bBold = true);

	extern const FLinearColor Accent;
	extern const FLinearColor Go;
	extern const FLinearColor Muted;
}

/** Empty area that reports horizontal drags (mouse or one finger). Used to turn the podium character. */
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
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	// Free Fire / PUBG style flow: Splash -> (first time) Agreement -> Login choice -> Email login / Register ->
	// (first time) Nickname -> Lobby. The lobby opens the Store, Rooms (quick / create / join by code), Account.
	enum class EScreen : uint8 { Splash, Agreement, LoginChoice, EmailLogin, Register, Nickname, Lobby, Store, Rooms, Account, Event, Daily };
	enum class EStoreTab : uint8 { Guns, Offers, Packs };

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
	TSharedRef<SWidget> BuildModeSelect();
	TSharedRef<SWidget> BuildStore();
	TSharedRef<SWidget> BuildGunCard(int32 Index);
	TSharedRef<SWidget> BuildOfferCard(const FCampusOffer& Offer);
	TSharedRef<SWidget> BuildRooms();
	TSharedRef<SWidget> BuildAccount();
	TSharedRef<SWidget> BuildDaily();
	TSharedRef<SWidget> BuildEvent();

	TWeakObjectPtr<ACampusLobbyPlayerController> Controller;
	TSharedPtr<SBox> Content;
	EScreen Current = EScreen::Splash;
	EStoreTab StoreTab = EStoreTab::Guns;
	int32 SelectedMap = 0;
	int32 SelectedTeamSize = 4;
	float SplashTime = 0.f;
	int32 SplashTip = 0;
	bool bSplashReady = false;
	bool bResuming = false;
	bool bAgreeTicked = false;
	float ToastTime = 0.f;
	FString ToastText;
	bool bBusy = false;
	FString BusyText;

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
	TSharedRef<SWidget> TeamColumn(uint8 Team);

	TWeakObjectPtr<ACampusMatchPlayerController> Controller;
	TSharedPtr<SBox> Content;
	FString RoomSignature() const;

	int32 ShownPhase = -1;
	FString ShownSignature;
};
