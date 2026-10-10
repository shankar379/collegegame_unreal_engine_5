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
	float AimPitch = 0.f;
	float AimPitchAlpha = 0.f;

private:
	void SamplePose(const FCampusAnimSample& Sample, FPoseContext& Out) const;
	void ApplyUpperBody(FPoseContext& Output);
	void ApplyAimPitch(FPoseContext& Output) const;
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

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Clips;

	// Blend state, all ticked on the game thread.
	float CrouchWeight = 0.f;
	float RifleWeight = 0.f;
	float AirWeight = 0.f;
	float LandWeight = 0.f;
	float MovePhase = 0.f;
	float IdleTime = 0.f;
	float AirTime = 0.f;
	float LandTime = 0.f;
	bool bWasInAir = false;
	bool bJumped = false;

	// One-shots: clip index and time since start, INDEX_NONE when idle.
	int32 UpperBodyClip = INDEX_NONE;
	float UpperBodyTime = 0.f;
	float FireTime = -1.f;
};
