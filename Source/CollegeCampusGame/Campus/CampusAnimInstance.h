// Lightweight native animation for the Campus Combat character: a fixed set of ~33 clips
// blended in code, so the game ships without motion matching or an AnimBlueprint.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "CampusAnimInstance.generated.h"

class UAnimSequence;

/** One clip sampled at a time with a blend weight. */
struct FCampusAnimSample
{
	const UAnimSequence* Sequence = nullptr;
	float Time = 0.f;
	float Weight = 0.f;
	bool bLoop = true;
	/** Pelvis travel over one loop to strip (see CampusClips::FClipDef). */
	FVector3f PelvisDrift = FVector3f::ZeroVector;
	/** The clip carries the body's orientation on the ROOT bone (the Mixamo prone clips: the root is rolled ~80
	 *  degrees to lay the body down). The root is pinned after blending, which would throw that away and stand the
	 *  body up toward the sky, so the root's transform is folded into the pelvis first. */
	bool bFoldRootIntoPelvis = false;
};

/** Worker-thread side: turns the sample list prepared on the game thread into the final pose. */
struct FCampusAnimInstanceProxy : public FAnimInstanceProxy
{
	FCampusAnimInstanceProxy() = default;
	explicit FCampusAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual bool Evaluate(FPoseContext& Output) override;

	/** Full-body clips, weights need not be normalized. */
	TArray<FCampusAnimSample> Samples;

	/** Replaces spine_01 and everything above it (reload, equip, rifle hold while crouched). */
	FCampusAnimSample UpperBody;

	/** Additive one-shot on top (fire recoil). */
	FCampusAnimSample Additive;

	/** Camera pitch in degrees, spread over the spine while holding the rifle. */
	/** Turn in place: the feet stay at this yaw offset from the body (deg); the spine turns back to the camera. */
	float RootYaw = 0.f;

	/** Run lean: an additive pose (lean clip minus its base pose) on the body and legs, never the arms. */
	FCampusAnimSample LeanBase;
	FCampusAnimSample LeanSide;

	/** 0 standing .. 1 lying flat (placeholder: the crouch pose tipped forward onto the ground). */
	float ProneAlpha = 0.f;

	/** The deployment: 0..1 belly-to-earth freefall, 0..1 hanging under the canopy. The project owns no
	 *  skydive or parachute clip (there is no FBX and nothing in /Game/CampusAnims), so both poses are
	 *  built in code out of bone rotations - see ApplyDivePose. Driven from the pawn's REPLICATED dive
	 *  byte, so remote divers are posed on every phone too. */
	float FreefallAlpha = 0.f;
	float ChuteAlpha = 0.f;

	float AimPitch = 0.f;
	float AimPitchAlpha = 0.f;
	/** 0..1: the bent-over crouch run (armed, crouched, running): lower spine leans forward, chest partly back. */
	float CrouchRunBend = 0.f;

	/** Foot IK (component space, cm): each foot's ground height relative to the capsule's floor, the pelvis drop
	 *  that lets the lower foot reach, and the blend (0 in the air, traversing, prone). */
	float FootOffset[2] = { 0.f, 0.f };
	float PelvisOffset = 0.f;
	float FootIKAlpha = 0.f;

	/** Prone: the whole body turned toward a sideways crawl (deg, about the root; no spine counter-twist). */
	float ProneYaw = 0.f;

private:
	void SamplePose(const FCampusAnimSample& Sample, FPoseContext& Out) const;
	void ApplyUpperBody(FPoseContext& Output);
	void ApplyAimPitch(FPoseContext& Output) const;
	void ApplyFootIK(FPoseContext& Output) const;
	void ApplyProneYaw(FPoseContext& Output) const;
	void ApplyDivePose(FPoseContext& Output) const;
	void ApplyLean(FPoseContext& Output);
	void ApplyRootYaw(FPoseContext& Output) const;

	/** Compact bone index -> takes the lean (cached per bone count). */
	TArray<bool> LeanMask;
};

UCLASS()
class UCampusAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UCampusAnimInstance();

	void PlayFire();
	void PlayReload();
	void PlayEquip();

	/** Length of the reload clip, so the character can block firing while it plays. */
	float GetReloadDuration() const;

	/** Traversal clip: 0-2 mantle (stand / walk / run), 3-5 climb. The character reads its root motion. */
	const UAnimSequence* GetMantleClip(int32 Kind) const;

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	/** Weighted averages of the moving clips, used to advance the shared gait phase. */
	struct FGaitAccum
	{
		float Weight = 0.f;
		float CycleLength = 0.f;
		float RefSpeed = 0.f;
	};

	void AddLocomotion(TArray<FCampusAnimSample>& Out, FGaitAccum& Gait, int32 FirstClip, bool bHasRunTier, bool bHasSprint, float SetWeight, float Speed, float DirectionDeg) const;
	void AddOneShotPair(TArray<FCampusAnimSample>& Out, int32 UnarmedClip, int32 RifleClip, float Time, float Weight, bool bLoop) const;
	const UAnimSequence* Clip(int32 Index) const;
	const UAnimSequence* DynClip(int32 Index) const;

	/** Game Animation Sample transitions on top of the loops: starts, stops, pivots, sprint turns,
	 *  turn in place, idle breaks, run lean and the slide. Unarmed, standing, on the ground. */
	void UpdateDynamic(float DeltaSeconds, const class ACampusCharacter* Campus, const class UCharacterMovementComponent* Movement,
		float Speed, float DirectionDeg, TArray<FCampusAnimSample>& Samples);

	enum class EDyn : uint8 { None, Start, Stop, Pivot, SprintTurn, Turn, IdleBreak };
	void BeginDyn(EDyn State, int32 Index, float Time = 0.f);
	void EndDyn(float FadeTime);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> DynClips;

	EDyn DynState = EDyn::None;
	int32 DynIndex = INDEX_NONE;
	float DynTime = 0.f;
	float DynWeight = 0.f;
	float DynDistance = 0.f;
	float DynStartDirection = 0.f;
	float PivotCooldown = 0.f;
	// The clip being faded out after a switch.
	int32 FadeIndex = INDEX_NONE;
	float FadeTime = 0.f;
	float FadeWeight = 0.f;
	float FadeRate = 4.f;

	FVector PrevVelocity = FVector::ZeroVector;
	float PrevSpeed = 0.f;
	float VelocityYawRate = 0.f;
	float SprintTurnAccum = 0.f;
	float LeanAlpha = 0.f;

	bool bFeetYawValid = false;
	float FeetYaw = 0.f;
	float RootYawOffset = 0.f;
	float TurnFeetStart = 0.f;

	float StillTime = 0.f;
	float NextIdleBreak = 10.f;

	bool bWasSliding = false;
	float SlideTime = 0.f;
	float SlideWeight = 0.f;
	int32 SlideOutIndex = INDEX_NONE;
	float SlideOutTime = 0.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Clips;

	// Blend state, all ticked on the game thread.
	float CrouchWeight = 0.f;
	float CrouchRunWeight = 0.f;

	// prone (see the prone block in NativeUpdateAnimation)
	bool bWasProne = false;
	bool bWasCrouched = false;
	float ProneTransElapsed = -1.f;   // < 0: no transition playing
	float ProneTransFrom = 0.f;       // get-up clip times
	float ProneTransTo = 0.f;
	float ProneTransDuration = 0.f;
	bool bProneTransDown = false;
	float ProneForwardPhase = 0.f;
	float ProneBackwardPhase = 0.f;
	float ProneYawState = 0.f;
	float FootTrace[2] = { 0.f, 0.f };
	float PelvisTrace = 0.f;
	float FootIKWeight = 0.f;
	float ProneWeight = 0.f;
	float RifleWeight = 0.f;
	float AirWeight = 0.f;
	float LandWeight = 0.f;
	float MovePhase = 0.f;
	float IdleTime = 0.f;
	float AirTime = 0.f;
	float LandTime = 0.f;
	bool bWasInAir = false;
	/** Eased dive weights (0.25 s), so the pose comes in as a move and freefall -> canopy crossfades. */
	float FreefallWeight = 0.f;
	float ChuteWeight = 0.f;
	float TraversalWeight = 0.f;
	int32 LastTraversalKind = INDEX_NONE;
	float LastTraversalTime = 0.f;
	bool bJumped = false;
	int32 JumpKind = 0;
	int32 LandKind = 0;

	// One-shots: clip index and time since start, INDEX_NONE when idle.
	int32 UpperBodyClip = INDEX_NONE;
	float UpperBodyTime = 0.f;
	float FireTime = -1.f;

	/** Aim pitch eased over ~50 ms for remote players: the raw value only moves when their move update lands. */
	float AimPitchSmoothed = 0.f;
};
