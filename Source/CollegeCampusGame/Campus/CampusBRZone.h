// Battle royale play area: one replicated circle that waits, then shrinks, five times over, and
// burns anyone standing outside it. Clients never run the schedule - they read the start / end
// transform and the server's phase clock and interpolate, so the HUD ring and the server's damage
// check always agree about where the edge is.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CampusBRZone.generated.h"

/** The square the BR play area covers, in Unreal world cm.
 *  This is the SAME square the minimap picture covers (SCampusTouchControls / CampusMapArea in
 *  CampusTouchControls.cpp: the college bounds + 15 m, campus origin (15710, 8222.6) in L_Match),
 *  spelled out here because the zone, the plane and the HUD ring all need it and only one of the
 *  three lives in the HUD file. Change both together or the ring stops matching the damage. */
namespace CampusBRMap
{
	constexpr float MinX = -12210.f + 15710.f;
	constexpr float MinY = -19549.45f + 8222.6f;
	constexpr float Span = 22710.f;

	inline FVector2D Center() { return FVector2D(MinX + Span * 0.5f, MinY + Span * 0.5f); }
	/** Half the map square: the widest the first circle can be. */
	inline float HalfSpan() { return Span * 0.5f; }
	/** Normalised position on the map picture (0..1 across, 0..1 down), matching CampusMapArea::ToUV. */
	inline FVector2D ToUV(const FVector& World)
	{
		return FVector2D((World.X - MinX) / Span, (World.Y - MinY) / Span);
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
	/** Metres this location is outside the circle (0 when safe). */
	float MetresOutside(const FVector& Where) const;

private:
	static const FCampusBRZonePhase& PhaseAt(int32 Index);
	/** Server: begin the wait of the next phase (or finish the schedule). */
	void OpenPhase(int32 Index);
	/** Server: one second of burn on everyone outside. */
	void BurnOutside();
	/** A circle of RadiusShare that stays inside the current one. */
	void PickNextCircle(float NewRadius);

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
