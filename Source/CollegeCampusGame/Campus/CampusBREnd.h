// BATTLE ROYALE result screen - "CAMPUS KING".
//
// ONE SCREEN, AND ONLY ONE. There used to be two: SCampusBRHud painted a BOOYAH! / #7 PLACE card over
// the match the moment the BR phase ended, and SCampusMatchWidget built a second "CLAIM N COINS" panel
// on top of it a frame later. They stacked. The BR HUD now stands down completely at the final whistle
// (see SCampusBRHud::Construct) and SCampusMatchWidget::BuildBattleRoyaleResult builds THIS widget, so
// there is exactly one end-of-match surface and the touch controls are gone behind it.
//
// WHAT IS DRAWN OVER WHAT. The 3D line-up is NOT a render target: a SceneCapture2D plus a render
// target is a second scene render and a Vulkan allocation on a phone that has just finished a
// fifty-player match, and it is the first thing to fail in a Shipping build. Instead the match player
// controller stands the squad up on a stage in clear air above the map and points a real camera at it
// (ACampusMatchPlayerController::ShowResultStage) - the same recipe the lobby line-up has always used
// - and this widget is drawn over the live scene, with a warm golden scrim between the two. Every
// number on it, and every figure on the stage, comes from the SAME sorted view (FCampusBREndView), so
// a name plate can never drift off the figure it belongs to.
//
// MOTION. Four layered beats, all transform / opacity only, none of them load-bearing: the title
// punches in past its final size and settles, a gold shine sweeps across it, the crown drops onto it,
// and confetti drifts down the whole screen. The panels fade and slide up on a short stagger. The
// screen is readable with every one of them switched off, which is what makes them safe to cut.

#pragma once

#include "CoreMinimal.h"
#include "Campus/CampusAngular.h"
#include "Misc/DateTime.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class ACampusMatchPlayerController;
class APlayerController;
class UWorld;

/** One figure in the line-up: a squad member, ranked by kills. Rank 0 is the MVP. */
struct FCampusBREndMember
{
	FString Name;
	int32 Kills = 0;
	float Damage = 0.f;
	/** Still standing when the match was called. */
	bool bAlive = false;
	/** This phone's own player, so their plate can be marked and their stats panel filled. */
	bool bLocal = false;
};

/** One row of the MATCH SUMMARY list: everybody this client has a player state for. */
struct FCampusBREndRow
{
	FString Name;
	int32 Kills = 0;
	int32 Placement = 0;
	bool bLocal = false;
};

/** Everything the screen draws, read once off replicated state. Nothing here is per-frame work. */
struct FCampusBREndView
{
	/** False = not a battle royale (or nothing replicated yet); the screen draws nothing. */
	bool bFound = false;
	/** My squad took it. */
	bool bWon = false;
	/** MY SQUAD'S place, out of TotalSquads. Falls back to the combatant placement when the squad
	 *  placement has not replicated - a wrong-looking number beats a blank one. */
	int32 Placement = 0;
	int32 TotalSquads = 1;
	/** 1 / 2 / 4: decides SOLO RESULT vs SQUAD RESULTS and the mode line. */
	int32 SquadSize = 1;
	int32 MatchSeconds = 0;

	FString MapName;
	/** Base name under Content/CampusUI/MapArt, or null when the map is not in the catalog. */
	const TCHAR* MapArt = nullptr;

	// ---- the local player's own card
	int32 Kills = 0;
	int32 Headshots = 0;
	float Damage = 0.f;
	float SurvivalSeconds = 0.f;
	float DistanceCm = 0.f;

	/** The squad, MOST KILLS FIRST. Index 0 is the MVP and stands centre stage. Solo = one entry. */
	TArray<FCampusBREndMember> Squad;
	int32 SquadKills = 0;
	float SquadDamage = 0.f;
	int32 SquadAlive = 0;

	/** Every player state this client can see, placement first. Empty = no summary to show. */
	TArray<FCampusBREndRow> Summary;

	/** Wall clock when the screen was built. Local, on purpose: it is what the player's phone says. */
	FDateTime EndedAt;

	void Read(const APlayerController* Controller);
};

/**
 * WHERE THE WINNING SQUAD STANDS.
 *
 * The same shape as CampusLineUp (the lobby's table), re-cut for a results photo: the MVP centre and
 * a step FORWARD of the rest, the others fanned out to the sides by rank. The stage is built in CLEAR
 * AIR a long way above the map - never on the ground, where a camera would end up inside a wall, a
 * tree or the zone wall depending on where the match happened to end.
 *
 * The camera's FOV is set from the viewport's aspect so the VERTICAL framing is fixed: that is what
 * lets the name plates below be anchored at constant screen fractions on any phone.
 */
namespace CampusVictoryStage
{
	/** The most figures the stage holds (one squad). */
	constexpr int32 MaxFigures = 4;

	/** THE FRAMING, and it is not arbitrary: at 610 cm back, waist high and all but level, a 1.8 m
	 *  figure runs from about 34 % to 72 % down the screen and the MVP (a tenth taller, a step
	 *  forward) from 28 % to 75 %. That is what leaves the title clear of their heads at the top, the
	 *  two side panels clear of their shoulders, and the name plates clear of the buttons along the
	 *  bottom. Move any of these three numbers and the plates move with them - which is fine, because
	 *  ScreenFootFraction derives the plate row from exactly these. */
	constexpr float CameraDistanceCm = 610.f;
	constexpr float CameraHeightCm = 110.f;
	constexpr float CameraPitchDeg = -0.5f;
	/** Half the VERTICAL field of view. Fixed; the horizontal FOV is derived from it per device. */
	constexpr float VerticalHalfDeg = 21.5f;
	/** How far above the player's camera the stage is built. Above every roof, under the plane. */
	constexpr float AltitudeCm = 15000.f;

	/** One spot. Lateral is cm to the player's RIGHT ON SCREEN, Depth is cm TOWARDS the camera. */
	struct FStagePose
	{
		float LateralCm = 0.f;
		float DepthCm = 0.f;
		float YawDeg = 0.f;
		/** The MVP is a little larger, which is most of why they read as the MVP. */
		float Scale = 1.f;
	};

	/** Rank 0 = MVP (centre, forward, larger), then 1, 2, 3 by kills. */
	const FStagePose& Pose(int32 Rank);

	/** Horizontal FOV this device needs for the fixed vertical framing above. */
	float HorizontalFovDeg(float Aspect);

	/** Viewport width / height right now, or 16:9 when there is no viewport to ask. */
	float ViewportAspect();

	/** Where this rank's figure lands across the screen, 0..1. Perspective corrected: a figure further
	 *  back covers fewer pixels, so its plate sits closer to the middle than its lateral offset alone. */
	float ScreenFraction(int32 Rank, float Aspect);

	/** Where that figure's FEET land down the screen, 0..1 - where its name plate hangs. Aspect-free,
	 *  because the vertical framing is fixed. */
	float ScreenFootFraction(int32 Rank);

	/** The yaw a figure in this rank stands at: turned to face the camera, then leaned into the group. */
	float FacingYaw(int32 Rank);
}

/** Gold confetti drifting over the whole screen. Owns its own particles; one draw element each. */
class SCampusBREndConfetti : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusBREndConfetti) : _Celebrate(false) {}
		/** Confetti is for a WIN. A placement screen gets none of it. */
		SLATE_ARGUMENT(bool, Celebrate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	struct FFlake
	{
		/** 0..1 of the width, and pixels down from the top. */
		float X = 0.f;
		float Y = 0.f;
		float Fall = 0.f;
		float Drift = 0.f;
		float Spin = 0.f;
		float Phase = 0.f;
		float Size = 0.f;
		float Tone = 0.f;
	};

	/** Enough to read as a shower, few enough to cost nothing: 44 quads. */
	static constexpr int32 NumFlakes = 44;

	void Seed(float Height);

	TArray<FFlake> Flakes;
	bool bCelebrate = false;
	float Life = 0.f;
	float SeededHeight = 0.f;
};

/** The warm golden backdrop: a vignette round the edges and a glow behind the line-up. */
class SCampusBREndBackdrop : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusBREndBackdrop) : _Warm(true) {}
		SLATE_ARGUMENT(bool, Warm)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	/** A win gets gold; a placement gets the same shape in cold grey, so the screen still reads as
	 *  "this is the end" without congratulating somebody who came 14th. */
	bool bWarm = true;
};

/** The whole result screen. Built by SCampusMatchWidget when a BR match ends; nothing else makes one. */
class SCampusBREnd : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusBREnd) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ACampusMatchPlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** THE WHOLE SCREEN, built once - on the settle tick, never at Construct.
	 *
	 *  The final numbers do not all land in the same frame as the phase: the game state carries the
	 *  duration and the winning squad, but the kills, damage, survival time and squad placement live
	 *  on each PLAYER state and arrive in their own update. Building immediately showed a squad of
	 *  zeroes for a frame or two and, worse, could crown the wrong MVP on the stage. So the screen
	 *  holds on its golden backdrop for SettleSeconds - which reads as a deliberate beat, not a wait -
	 *  and everything after that is built once, from data that has arrived. */
	void BuildScreen();

	TSharedRef<SWidget> BuildIdentity();
	TSharedRef<SWidget> BuildTitle();
	TSharedRef<SWidget> BuildStats();
	TSharedRef<SWidget> BuildClock();
	TSharedRef<SWidget> BuildSquadResults();
	TSharedRef<SWidget> BuildPlates();
	TSharedRef<SWidget> BuildButtons();
	/** SHARE / MATCH SUMMARY / BACK TO LOBBY, under the countdown line BuildButtons puts over them. */
	TSharedRef<SWidget> BuildButtonRow();
	TSharedRef<SWidget> BuildSummary();

	/** One "KILLS   12" row in the left panel. */
	TSharedRef<SWidget> StatRow(CampusAngular::EGlyph Glyph, const FText& Label, const FText& Value);
	/** One figure's plate: rank diamond (or the gold MVP tag), name, KILLS and DAMAGE. */
	TSharedRef<SWidget> Plate(int32 Rank);

	/** The headline: CAMPUS KING on a win, the honest placement otherwise. */
	FText TitleText() const;

	/** One panel's entrance: a short fade and slide-up, started Delay seconds in. */
	struct FEntrance
	{
		TSharedPtr<SWidget> Widget;
		float Delay = 0.f;
		float RisePx = 18.f;
	};
	/** Registers Panel for the staggered entrance and hands it straight back, so a build line stays
	 *  one call. Nothing here is load-bearing: with every entrance removed the screen is identical. */
	TSharedRef<SWidget> Enter(TSharedRef<SWidget> Panel, float Delay, float RisePx = 18.f);

	void Share();
	void BackToLobby();

	/** The player asked for the lowest graphics setting: the ambient loops (confetti, the shine
	 *  sweep) are the first thing to go. The screen never needs them to be read. */
	bool WantsMotion() const;

	TWeakObjectPtr<ACampusMatchPlayerController> Controller;
	FCampusBREndView View;
	/** Screen fractions are computed once, at build, from the viewport that is actually there. */
	float Aspect = 16.f / 9.f;
	/** Seconds this screen has been up; every animation reads it and nothing else. */
	float Life = 0.f;
	/** The MATCH SUMMARY sheet is open over the screen. */
	bool bSummaryOpen = false;
	/** Seconds left on the SHARE confirmation line, or 0. */
	float ToastLeft = 0.f;
	FText Toast;
	/** The screen and the 3D stage are both built on the settle tick, once. */
	bool bSettled = false;
	/** How long the backdrop holds before the results land. Long enough for one net update at a bad
	 *  ping, short enough that nobody reads it as a hang. */
	static constexpr float SettleSeconds = 0.45f;

	/** BACK TO LOBBY BY ITSELF, the Free Fire way: this many seconds after the results land, unless the
	 *  player is reading the summary sheet. */
	static constexpr float AutoLeaveSeconds = 10.f;
	/** Seconds left before the automatic return; < 0 = no automatic return (see bHostingOthers). */
	float AutoLeaveLeft = -1.f;
	/** THIS PHONE IS THE MATCH HOST and other real players are still in it: leaving would end their match,
	 *  so there is no automatic return and BACK TO LOBBY asks twice. Re-checked every second. */
	bool bHostingOthers = false;
	int32 OthersStillPlaying = 0;
	float HostCheckIn = 0.f;
	/** The host tapped BACK TO LOBBY once while others were playing; the second tap leaves. */
	bool bLeaveArmed = false;
	/** Real (not bot) players other than me who are still alive in this match - 0 when I am a client. */
	int32 CountOthersStillPlaying() const;

	TArray<FEntrance> Entrances;
	/** The three title pieces, animated imperatively in Tick (never rebuilt). */
	TSharedPtr<SWidget> TitleBlock;
	TSharedPtr<SWidget> TitleShine;
	TSharedPtr<SWidget> CrownBlock;
};
