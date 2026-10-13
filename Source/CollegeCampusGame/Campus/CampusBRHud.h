// BATTLE ROYALE in-match HUD (Slate), drawn over SCampusTouchControls and under SCampusMatchWidget.
//
// THE GAME LOGIC IS NOT MINE. ACampusBRGameMode / ACampusBRZone / ACampusBRPlane are built by
// another pass and their replicated state is still moving, so this widget READS WHAT IT CAN FIND and
// draws only what it found:
//
//   * every number is looked up by NAME through reflection (FCampusBRView::Read), trying a short
//     list of plausible names per field;
//   * the phase is read as the ENUMERATOR'S NAME, not its integer, so re-ordering the enum cannot
//     silently point the HUD at the wrong phase;
//   * anything missing simply does not draw. A HUD that disappears is survivable; a HUD that invents
//     "ALIVE 50" over an empty match is not.
//
// So this compiles and runs against today's build (nothing found -> nothing drawn) and lights up
// piece by piece as the BR state lands, with no edit here.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class APlayerController;
class SBox;

/** One frame of everything the HUD could read. Each piece has its own bHas flag: absence is normal. */
struct FCampusBRView
{
	/** waiting | countdown | plane | parachute | playing | ended | unknown */
	FString Phase;

	bool bHasAlive = false;
	int32 Alive = 0;
	int32 Kills = 0;

	bool bHasFill = false;
	int32 Humans = 0;
	int32 MaxHumans = 0;
	int32 TotalSlots = 50;
	/** Still gathering on the waiting platform. The fill banner and the countdown belong to THIS and
	 *  not to "we have a player count", which is true for the whole match. */
	bool bWaiting = false;

	/** Seconds until the waiting room ends. */
	bool bHasCountdown = false;
	float Countdown = 0.f;

	/** Seconds until the circle starts closing (or finishes closing). */
	bool bHasZoneClock = false;
	float ZoneClock = 0.f;
	/** The circle is closing NOW, so the clock means "closing in", not "shrinking in". */
	bool bZoneClosing = false;

	/** World-space circle this player should be inside, and where they are. */
	bool bHasZone = false;
	FVector ZoneCenter = FVector::ZeroVector;
	float ZoneRadius = 0.f;
	FVector Me = FVector::ZeroVector;
	/** Yaw the player is facing, so the arrow points relative to the camera and not to north. */
	float MeYaw = 0.f;
	/** True once the player is outside the circle: the arrow and the timer go red. */
	bool bOutside = false;
	float DistanceToEdge = 0.f;

	bool bInPlane = false;
	/** Dropped, chute not open yet. */
	bool bFreefall = false;
	bool bParachuting = false;
	/** My ACampusBRDrop, when there is one: the object the JUMP button calls ServerJump on. */
	TWeakObjectPtr<AActor> Drop;

	bool bHasPlacement = false;
	int32 Placement = 0;

	bool bEnded = false;
	/** My squad took it: BOOYAH! instead of a placement line. */
	bool bWon = false;

	/** Reads one frame off the world. Safe to call with no BR actors at all. */
	void Read(const APlayerController* Controller);
	/** Any BR state found at all - the HUD draws nothing when this is false. */
	bool IsBattleRoyale() const { return bFound; }

	bool bFound = false;
};

/** The circle + the arrow to it. A leaf widget because an arrow and a ring are two draw elements and
 *  no amount of Slate boxes will make them. Reads the parent's cached view; never the world. */
class SCampusBRZoneDial : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusBRZoneDial) {}
		/** Owned by the parent HUD, which outlives this child. */
		SLATE_ARGUMENT(const FCampusBRView*, View)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(132.f, 132.f); }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	const FCampusBRView* View = nullptr;
};

class SCampusBRHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCampusBRHud) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, Controller)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** The plane's JUMP button. Calls a no-argument UFUNCTION on the pawn or the controller, whichever
	 *  has one - "BRJump", "JumpFromPlane", "ServerBRJump", "Dive" - and says so when there is none. */
	void BRJump();
	void ReturnToLobby();

	TWeakObjectPtr<APlayerController> Controller;
	FCampusBRView View;
	/** Reflection is a hash lookup per field; four times a second is plenty for a HUD. */
	float ReadIn = 0.f;
	/** Set when BRJump found nothing to call, so the button can say it instead of doing nothing. */
	bool bJumpUnavailable = false;
};
