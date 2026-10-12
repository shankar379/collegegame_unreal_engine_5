// In-game on-screen controls + HUD, ported from the three.js game (src/game/hud.js, hud.css).
// One leaf widget tracks every finger by pointer index, so stick, look, fire and buttons all
// work at the same time. Layout uses the same unit as the web HUD: u = min(1% width, 1.8% height).

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class ACampusCharacter;
class UTexture2D;
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
	enum class EControl : uint8 { None, Fire, FireLeft, Scope, Zoom, Reload, Jump, Crouch, Prone, Sprint, Weapon, Eye, Mic, Speaker, Settings };
	enum class ERole : uint8 { Look, Joystick, FireDrag, Button, FreeLook };

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
		FVector2D Fire, FireLeft, Scope, Zoom, Reload, Jump, Crouch, Prone, Sprint, Eye, Mic, Speaker;
		float VoiceR = 0.f;
		float FireR = 0.f, FireLeftR = 0.f, ScopeR = 0.f, ReloadR = 0.f, JumpR = 0.f, CrouchR = 0.f, ProneR = 0.f, SprintR = 0.f, EyeR = 0.f;
		FVector2D WeaponMin, WeaponSize;
		FVector2D ZoomSize;
		/** Settings gear, HUD top-right corner (Free Fire puts it there). */
		FVector2D Gear;
		float GearR = 0.f;
		/** Team roster panel, top-left; the minimap sits under it. */
		FVector2D TeamMin, TeamSize;
		FVector2D MapMin;
		float MapSize = 0.f;
		/** ALIVE / KILL chips, left of the gear. */
		FVector2D ChipRight;
		float ChipTop = 0.f;
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
	/** bTabular: upright bold condensed instead of italic, for numbers that sit in a column
	 *  (the scoreboard). Italic numerals in a table never line up. */
	void DrawText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FString& Text, const FVector2D& Anchor, int32 Size, const FLinearColor& Color, float AlignX = 0.5f, float AlignY = 0.5f, bool bTabular = false) const;
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
	/** Joystick held forward at the rim; after 2 s the run locks on. */
	bool bAtRunRim = false;
	float RunRimTime = 0.f;

	// HUD easing.
	float LagHealth = 100.f;
	float LagDelay = 0.f;
	float LastHealth = 100.f;
	float DamageFlash = 0.f;
	float SpinAngle = 0.f;

	// Settings cached in Tick: reading the save game in OnPaint / per look event would hit the
	// game instance thousands of times a second for values that change on a menu tap.
	float LookSensitivity = 1.f;
	bool bShowMinimap = true;
	bool bLeftFireButton = true;

	/** Rounded-box brushes need an exact corner radius (the shader does not clamp), so cache one per size. */
	const FSlateBrush* RoundBrush(float Radius, float OutlineWidth = 0.f, const FLinearColor& OutlineColor = FLinearColor::White) const;
	void DrawPill(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, const FVector2D& Size, const FLinearColor& Color) const;

	// ---- angular HUD chrome (Free Fire look). Same FCampusShape vocabulary as the Slate menus,
	// so a HUD chip and a lobby tab lean by the same amount.
	/** Dark translucent parallelogram chip; Slant is in px of top-edge shift. */
	void DrawSlantedPanel(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, const FVector2D& Size,
		float Slant, const FLinearColor& Fill, const FLinearColor& Border = FLinearColor::Transparent, float BorderWidth = 0.f) const;
	/** Clipped-corner box (all four corners cut by Cut px). */
	void DrawCutCornerBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, const FVector2D& Size,
		float Cut, const FLinearColor& Fill, const FLinearColor& Border = FLinearColor::Transparent, float BorderWidth = 0.f) const;
	/** Chip with only the top-right and bottom-left corners cut: the standard HUD plate. */
	void DrawNotchedBox(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, const FVector2D& Size,
		float Cut, const FLinearColor& Fill, const FLinearColor& Border = FLinearColor::Transparent, float BorderWidth = 0.f) const;
	/** The teammate list, top-left: name + flat health bar per player on your team. */
	void DrawTeamList(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;
	/** Connection strength: Wi-Fi bars tinted by ping, with the number beside them. */
	void DrawConnection(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;

	mutable TMap<uint64, TUniquePtr<FSlateRoundedBoxBrush>> BrushCache;

	/** The three.js HUD icons (src/game/assets + icons.js), imported as UI textures. */
	void LoadIcons();
	const FSlateBrush* Icon(FName Name) const;

	// ---- maps (three.js src/game/maps.js) and the scoreboard
	/** The campus picture with you (yellow arrow) and your teammates; bBig = full-screen map with grid + names. */
	void DrawMap(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, float Size, bool bBig, float U) const;
	/** Tap the timer: both teams side by side, kills / deaths / damage dealt / taken for this room's rounds. */
	void DrawScoreboard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;
	bool bBigMap = false;
	bool bScoreboard = false;
	void DrawImage(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FSlateBrush* Brush, const FVector2D& Center, const FVector2D& Size, const FLinearColor& Tint, float Angle = 0.f) const;
	/** three.js .on / :active state: a soft orange (#f0a020) glow behind the icon. */
	void DrawGlow(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Center, float Radius) const;

	TMap<FName, FSlateBrush> IconBrushes;
	TArray<TStrongObjectPtr<UTexture2D>> IconTextures;
	const FSlateBrush* WhiteBrush = nullptr;
};
