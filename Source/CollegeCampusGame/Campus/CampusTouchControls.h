// In-game on-screen controls + HUD, ported from the three.js game (src/game/hud.js, hud.css).
// One leaf widget tracks every finger by pointer index, so stick, look, fire and buttons all
// work at the same time. Layout uses the same unit as the web HUD: u = min(1% width, 1.8% height).

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SLeafWidget.h"

class ACampusCharacter;
class APlayerController;

class SCampusTouchControls : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusTouchControls) : _ShowButtons(true) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, Controller)
		/** False on desktop: HUD only, keyboard and mouse drive the character. */
		SLATE_ARGUMENT(bool, ShowButtons)
	SLATE_END_ARGS()

	SCampusTouchControls();
	void Construct(const FArguments& InArgs);

	/** Controls and HUD only show during the match (not in the waiting room or win screen). */
	void SetActive(bool bInActive);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;
	virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& TouchEvent) override;

private:
	enum class EControl : uint8 { None, Fire, FireLeft, Scope, Zoom, Reload, Jump, Crouch, Prone, Sprint, Weapon };
	enum class ERole : uint8 { Look, Joystick, FireDrag, Button };

	struct FFinger
	{
		ERole Role = ERole::Look;
		EControl Control = EControl::None;
		FVector2D Last = FVector2D::ZeroVector;
		FVector2D Offset = FVector2D::ZeroVector; // from the fire button centre
		float HeldTime = 0.f;
	};

	/** Every position and size, recomputed from the widget size (same percentages as hud.css). */
	struct FLayout
	{
		float W = 0.f, H = 0.f, U = 0.f;
		FVector2D JoyHome;
		float JoyR = 0.f;
		FVector2D JoyZoneMin, JoyZoneMax;
		FVector2D Fire, FireLeft, Scope, Zoom, Reload, Jump, Crouch, Prone, Sprint;
		float FireR = 0.f, FireLeftR = 0.f, ScopeR = 0.f, ReloadR = 0.f, JumpR = 0.f, CrouchR = 0.f, ProneR = 0.f, SprintR = 0.f;
		FVector2D WeaponMin, WeaponSize;
		FVector2D ZoomSize;
	};

	FLayout MakeLayout(const FVector2D& Size) const;
	EControl HitTest(const FLayout& L, const FVector2D& Point, bool bAiming) const;
	ACampusCharacter* GetCharacter() const;
	void ApplyLook(const FVector2D& PixelDelta, float Multiplier) const;
	void PressControl(EControl Control);
	void ReleaseControl(EControl Control);
	void ReleaseAll();
	float DegreesPerUnit() const;

	void DrawCircle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Center, float Radius, const FLinearColor& Color) const;
	void DrawRing(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Center, float Radius, const FLinearColor& Color) const;
	void DrawRect(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, const FVector2D& Size, const FLinearColor& Color) const;
	void DrawText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& Text, const FVector2D& Anchor, int32 Size, const FLinearColor& Color, float AlignX = 0.5f, float AlignY = 0.5f) const;
	void DrawPolyline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness) const;
	void DrawIcon(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, EControl Control, const FVector2D& C, float R, const FLinearColor& Color, float Spin) const;
	void DrawHud(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;
	void DrawControls(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;

	TWeakObjectPtr<APlayerController> Controller;
	bool bShowButtons = true;
	bool bActive = false;

	TMap<int32, FFinger> Fingers;
	FLayout CachedLayout;
	int32 JoyFinger = INDEX_NONE;
	FVector2D JoyCenter = FVector2D::ZeroVector;
	FVector2D JoyKnob = FVector2D::ZeroVector;
	FVector2D MoveAxis = FVector2D::ZeroVector;
	bool bAutoRun = false;

	// HUD easing.
	float LagHealth = 100.f;
	float LagDelay = 0.f;
	float LastHealth = 100.f;
	float DamageFlash = 0.f;
	float SpinAngle = 0.f;

	/** Rounded-box brushes need an exact corner radius (the shader does not clamp), so cache one per size. */
	const FSlateBrush* RoundBrush(float Radius, float OutlineWidth = 0.f, const FLinearColor& OutlineColor = FLinearColor::White) const;
	void DrawPill(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, const FVector2D& Size, const FLinearColor& Color) const;

	mutable TMap<uint64, TUniquePtr<FSlateRoundedBoxBrush>> BrushCache;
	const FSlateBrush* WhiteBrush = nullptr;
};
