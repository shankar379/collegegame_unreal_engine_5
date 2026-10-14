// Battle royale play area: one replicated circle that waits, then shrinks, five times over, and
// burns anyone standing outside it. Clients never run the schedule - they read the start / end
// transform and the server's phase clock and interpolate, so the HUD ring and the server's damage
// check always agree about where the edge is.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusBRZone.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/** The square the BR play area covers, in Unreal world cm - PER LEVEL.
 *
 *  THIS USED TO BE ONE HARDCODED SQUARE, and it was the L_Match college square: 227 m centred on
 *  (14855, 28). Battle royale does not play there. CampusBR::LevelForMap sends a battle session to
 *  /Game/Maps/L_VarsityValley, which Scripts/varsity_layout.py builds as 800 m x 800 m CENTRED ON
 *  THE WORLD ORIGIN (HALF = 40000). The consequence on a phone was the whole bug report: the first
 *  circle opened 107 m wide over one corner of an 800 m map, so a player standing anywhere else was
 *  genuinely outside it and genuinely burning - while the minimap, which drew that same circle onto
 *  the college picture, put it dead centre and nearly full width and so said "you are inside".
 *
 *  So the square is resolved from the LEVEL, identically on the host and on every client (both ask
 *  the same question of the same level name), and the zone, the plane, the waiting slab and the two
 *  maps all read it through here. One square per level, stated once. */
namespace CampusBRMap
{
	/** Which built level a square belongs to. The map picture and the place labels are keyed on this,
	 *  and nothing else may be: a bool was enough while there were two levels and is a lie now. */
	enum class ELevel : uint8 { College, CampusGrounds, Valley };

	struct FSquare
	{
		/** Middle of the play square in world cm. */
		FVector2D Middle = FVector2D::ZeroVector;
		/** Width of the square in cm; the first circle is half of this, less a margin. */
		float Span = 0.f;
		ELevel Level = ELevel::Valley;
	};

	/** The square of this level. Cheap: resolved once per world and cached. */
	const FSquare& For(const UWorld* World);

	inline FVector2D Center(const UWorld* World) { return For(World).Middle; }
	inline float Span(const UWorld* World) { return For(World).Span; }
	/** Half the map square: the widest the first circle can be. */
	inline float HalfSpan(const UWorld* World) { return For(World).Span * 0.5f; }
	/** Normalised position on the map picture (0..1 across, 0..1 down). */
	inline FVector2D ToUV(const UWorld* World, const FVector& Where)
	{
		const FSquare& Square = For(World);
		const float Wide = FMath::Max(Square.Span, 1.f);
		return FVector2D((Where.X - (Square.Middle.X - Wide * 0.5f)) / Wide,
			(Where.Y - (Square.Middle.Y - Wide * 0.5f)) / Wide);
	}
}

/** Server world time on every machine: the game state's synchronised clock on a client, plain world
 *  time on the host. Every BR timestamp is written from the host's world time and read through this,
 *  the same contract ACampusGameState::EndTime already uses. */
float CampusBRServerTime(const UWorld* World);

/** One entry of the shrink schedule. RadiusShare is of the FIRST circle, not of the previous one,
 *  so the table reads as the sequence of sizes the players actually see. */
struct FCampusBRZonePhase
{
	float WaitSeconds;
	float ShrinkSeconds;
	float RadiusShare;
	float DamagePerSecond;
};

UCLASS()
class ACampusBRZone : public AActor
{
	GENERATED_BODY()

public:
	ACampusBRZone();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** The zone of this world, or null outside a BR match. Cheaper than a TActorIterator per HUD frame. */
	static ACampusBRZone* Get(const UWorld* World);

	/** Server: open the first circle over the whole map and start the schedule. */
	void Begin();

	/** Server: stop damaging and stop shrinking (the match ended). */
	void Freeze();

	// ---- read by the HUD on every machine
	/** Circle the players must be inside right now (interpolated while shrinking). */
	FVector2D GetCenter() const;
	float GetRadius() const;
	/** The circle being shrunk toward; equals the current one when not announced yet. */
	FVector2D GetNextCenter() const { return FVector2D(CenterTo.X, CenterTo.Y); }
	float GetNextRadius() const { return RadiusTo; }
	bool IsShrinking() const { return bShrinking; }
	/** Seconds until the circle starts shrinking, or until it finishes once it has started. */
	float SecondsToEvent() const;
	int32 GetPhase() const { return PhaseIndex; }
	int32 GetPhaseCount() const;
	float GetDamagePerSecond() const { return DamagePerSecond; }
	/** True once the circle can actually hurt you: the schedule has opened AND this phase burns.
	 *  False in the waiting area, in the plane, through the freefall and for the whole of the first
	 *  circle's wait - which is the rule the three.js game plays by (zone.js: dps 0 while 'open'). */
	bool IsHot() const { return PhaseIndex >= 0 && DamagePerSecond > 0.f; }
	/** Metres this location is outside the CURRENT circle (0 when safe). 2D: height never counts, so
	 *  the 40 m waiting slab and the plane are "inside" exactly like the ground under them. */
	float MetresOutside(const FVector& Where) const;

private:
	static const FCampusBRZonePhase& PhaseAt(int32 Index);
	/** Server: begin the wait of the next phase (or finish the schedule). */
	void OpenPhase(int32 Index);
	/** Server: one second of burn on everyone outside. */
	void BurnOutside();
	/** A circle of RadiusShare that stays inside the current one. */
	void PickNextCircle(float NewRadius);
	/** Every machine: move / resize / fade the blue wall to the circle as it stands this frame. */
	void UpdateWall();

	/** THE WALL, every machine: a ring of thin translucent unlit blue panels standing at the circle's
	 *  edge, which is the three.js game's look (game.js showZone: a 60 m see-through glowing cylinder
	 *  at #3aa0ff, fainter while waiting, stronger while closing).
	 *
	 *  One instanced component, NOT a cylinder mesh: the engine cylinder has caps, and a 200 m-wide
	 *  translucent cap overhead washes the whole sky blue. The ring is built ONCE at a 1 m radius and
	 *  then moved and scaled as a whole, so a shrink costs two transform calls a frame and never
	 *  touches the instance buffer - which is what keeps it free on a phone. */
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Wall;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WallPaint;

	UPROPERTY(Replicated)
	FVector_NetQuantize CenterFrom = FVector_NetQuantize();

	UPROPERTY(Replicated)
	FVector_NetQuantize CenterTo = FVector_NetQuantize();

	UPROPERTY(Replicated)
	float RadiusFrom = 0.f;

	UPROPERTY(Replicated)
	float RadiusTo = 0.f;

	/** Server world time this phase's wait / shrink started and ends. */
	UPROPERTY(Replicated)
	float EventStartTime = 0.f;

	UPROPERTY(Replicated)
	float EventEndTime = 0.f;

	UPROPERTY(Replicated)
	bool bShrinking = false;

	UPROPERTY(Replicated)
	int32 PhaseIndex = -1;

	UPROPERTY(Replicated)
	float DamagePerSecond = 0.f;

	bool bRunning = false;
	float BurnAccumulator = 0.f;
	FTimerHandle PhaseTimer;
};
