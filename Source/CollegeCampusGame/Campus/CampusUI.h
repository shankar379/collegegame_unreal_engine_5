// Menu screens built in Slate (no widget assets to cook):
//   Lobby level: Splash -> Sign in -> Event post (optional) -> Lobby -> Create / Join.
//   Match level: Waiting room -> (game HUD lives in SCampusTouchControls) -> Win screen.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class ACampusLobbyPlayerController;
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
	enum class EScreen : uint8 { Splash, SignIn, Event, Lobby, Panel, Rooms };
	enum class EPanel : uint8 { Wardrobe, Upgrade, Loadout, Store, GunShop, Daily, Stats };

	void Show(EScreen Screen);
	void OpenPanel(EPanel Panel);
	void Refresh();
	void Toast(const FString& Message);

	TSharedRef<SWidget> BuildSplash();
	TSharedRef<SWidget> BuildSignIn();
	TSharedRef<SWidget> BuildEvent();
	TSharedRef<SWidget> BuildLobby();
	TSharedRef<SWidget> BuildPanel();
	TSharedRef<SWidget> BuildRooms();
	TSharedRef<SWidget> BuildTopBar();
	TSharedRef<SWidget> BuildMapCard(int32 Index);
	TSharedRef<SWidget> ItemRow(const FString& Title, const FString& Detail, const FString& Action, bool bEnabled, TFunction<void()> OnAction);

	TWeakObjectPtr<ACampusLobbyPlayerController> Controller;
	TSharedPtr<SBox> Content;
	EScreen Current = EScreen::Splash;
	EPanel CurrentPanel = EPanel::Wardrobe;
	int32 SelectedMap = 0;
	float SplashTime = 0.f;
	float ToastTime = 0.f;
	FString ToastText;
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
