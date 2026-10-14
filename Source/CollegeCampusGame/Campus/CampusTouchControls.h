// In-game on-screen controls + HUD, ported from the three.js game (src/game/hud.js, hud.css).
// One leaf widget tracks every finger by pointer index, so stick, look, fire and buttons all
// work at the same time. Layout uses the same unit as the web HUD: u = min(1% width, 1.8% height).

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SLeafWidget.h"

class ACampusBRGameState;
class ACampusCharacter;
class ACampusGameState;
class ACampusPlayerState;
class UTexture2D;
class UWorld;
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
	enum class EControl : uint8 { None, Fire, FireLeft, Scope, Zoom, Reload, Jump, Crouch, Prone, Sprint, Weapon, Eye, Mic, Speaker, Settings,
		/** ENTER / EXIT a car; its own button, never an overloaded Jump. */
		Vehicle,
		/** SWITCH SEAT, only while seated. */
		SeatSwitch,
		/** The second weapon plate: tap to hold that gun (Weapon is the first plate). */
		Weapon2,
		/** PICK UP: appears when a ground-loot box is in reach (battle royale). */
		Pickup };
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
		FVector2D Vehicle, SeatSwitch, Pickup;
		float PickupR = 0.f;
		float VoiceR = 0.f;
		float FireR = 0.f, FireLeftR = 0.f, ScopeR = 0.f, ReloadR = 0.f, JumpR = 0.f, CrouchR = 0.f, ProneR = 0.f, SprintR = 0.f, EyeR = 0.f;
		float VehicleR = 0.f, SeatSwitchR = 0.f;
		FVector2D WeaponMin, WeaponSize;
		FVector2D ZoomSize;
		/** Settings gear, HUD top-right corner (Free Fire puts it there). */
		FVector2D Gear;
		float GearR = 0.f;
		/** Minimap, top-left corner. */
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
	/** While seated: car HP under the player's bar, 4 wheel pips, the seat chip, and the BAIL OUT warning. */
	void DrawVehicleHud(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;
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
	/** Car HP trail bar, the same easing as LagHealth. */
	float LagCarHealth = -1.f;

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
	/** BATTLE ROYALE: a green chevron and name over each of YOUR OWN SQUAD, and nothing at all over
	 *  anybody else - every other combatant is an enemy, and tagging fifty people tags nobody.
	 *  Draws nothing in solo, where a player has no squadmates, and nothing in a room. */
	void DrawSquadMarkers(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;
	/** Battle royale overlay: the circle on the minimap, its clock, the alive count, the drop prompts
	 *  and BOOYAH. Returns immediately in TDM, which has no BR game state. */
	void DrawBattleRoyaleHud(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;
	/** Connection strength: Wi-Fi bars tinted by ping, with the number beside them. */
	void DrawConnection(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;

	mutable TMap<uint64, TUniquePtr<FSlateRoundedBoxBrush>> BrushCache;

	/** The three.js HUD icons (src/game/assets + icons.js), imported as UI textures. */
	void LoadIcons();
	const FSlateBrush* Icon(FName Name) const;

	// ---- maps (three.js src/game/maps.js) and the scoreboard

	/** HOW MUCH OF THE PLAY SQUARE A MAP IS SHOWING, as a window in CampusBRMap UV.
	 *
	 *  The full-screen map shows all of it (MinUV 0, SpanUV 1) and so did the minimap, which is why the
	 *  minimap looked frozen: 800 m of Varsity Valley squeezed into ~330 px is 0.4 px per metre, so a
	 *  sprinting player's dot crawled under two pixels a second and read as a stuck marker. The minimap
	 *  now shows a small window that FOLLOWS the player, and every single thing drawn on a map - the
	 *  picture, the safe circles, the flight line, the arrows - goes through Of(), so the two maps can
	 *  never disagree about where a world point lands. */
	struct FMapView
	{
		/** Top-left of the window, in CampusBRMap UV. */
		FVector2D MinUV = FVector2D::ZeroVector;
		/** Width of the window, as a share of the play square. 1 = the whole map. */
		double SpanUV = 1.0;
		/** World point -> 0..1 across the DRAWN map. Outside 0..1 is off the window. */
		FVector2D Of(const UWorld* World, const FVector& Where) const;
	};

	/** The window a map widget should draw: all of the square for the full map (and for the two small
	 *  campus maps), a square that follows Eye for the minimap on the big BR map. */
	FMapView MapWindow(const UWorld* World, const FVector& Eye, bool bBig) const;
	/** The picture of THIS level, or null where there is none - the valley's and Campus Grounds' own
	 *  top-down plans (loose PNGs under Content/CampusUI/MapArt) or the college room's cooked
	 *  T_Icon_campusmap. Never one level's picture under another level's coordinates. */
	const FSlateBrush* MapPicture(const UWorld* World) const;
	/** WHERE THE LOCAL PLAYER IS AND WHICH WAY THEY ARE LOOKING, this frame, for the map marker: the
	 *  pawn when there is one (on foot, driving, in the plane, under the chute - all of those keep the
	 *  pawn) and otherwise whatever the camera is looking through, which is the spectated squadmate
	 *  after a knockdown. False only when there is nothing at all to mark. */
	bool LocalMapMarker(FVector& OutWhere, float& OutYawDegrees) const;

	/** The campus picture with you (yellow arrow) and your teammates; bBig = full-screen map with grid + names. */
	void DrawMap(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, float Size, bool bBig, float U) const;
	/** BATTLE ROYALE: the safe circle on a map square, CURRENT BLUE and NEXT WHITE. One function for
	 *  the minimap and the big map, so the two can never drift apart; draws nothing outside a battle
	 *  session or before the schedule opens. */
	void DrawZoneCircles(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, float Size,
		const FMapView& View) const;
	/** BATTLE ROYALE: the plane's flight path across the picture, PUBG / Free Fire style - the line,
	 *  a START and an END marker, chevrons along it for the direction, and the aircraft's own icon
	 *  where it is right now. Drawn on BOTH maps because DrawMap draws both; dims once the plane has
	 *  flown the line out, and draws nothing at all outside a battle session. */
	void DrawFlightRoute(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Min, float Size,
		bool bBig, float Scale, const FMapView& View) const;
	/** Tap the timer. Picks the board that belongs to the game type and draws nothing else. */
	void DrawScoreboard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L) const;
	/** ROOM: both teams side by side, kills / deaths / damage dealt / taken for this room's rounds. */
	void DrawTeamBoard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L,
		const ACampusGameState* State, const ACampusPlayerState* Mine) const;
	/** BATTLE ROYALE: every squad this client can see, with alive status and kills. No RED, no BLUE. */
	void DrawSquadBoard(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FLayout& L,
		const ACampusBRGameState* State, const ACampusPlayerState* Mine) const;
	bool bBigMap = false;
	bool bScoreboard = false;
	void DrawImage(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FSlateBrush* Brush, const FVector2D& Center, const FVector2D& Size, const FLinearColor& Tint, float Angle = 0.f) const;
	/** three.js .on / :active state: a soft orange (#f0a020) glow behind the icon. */
	void DrawGlow(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G, const FVector2D& Center, float Radius) const;

	TMap<FName, FSlateBrush> IconBrushes;
	TArray<TStrongObjectPtr<UTexture2D>> IconTextures;
	const FSlateBrush* WhiteBrush = nullptr;
};
