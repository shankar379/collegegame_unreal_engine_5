#include "Campus/CampusAnimInstance.h"

#include "AnimationRuntime.h"
#include "Animation/AnimSequence.h"
#include "Campus/CampusCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace CampusClips
{
	// Locomotion sets are laid out as Idle, Walk F/B/L/R, Run F/B/L/R, Sprint.
	enum : int32
	{
		Unarmed = 0,
		Crouch = 10,       // Idle, F/B/L/R (single tier)
		Rifle = 15,        // Idle, Walk F/B/L/R, Jog F/B/L/R
		JumpStart = 24, Fall, Land,
		RifleJumpStart, RifleFall, RifleLand,
		Fire, Reload, Equip,
		Num
	};

	struct FClipDef
	{
		const TCHAR* Path;
		float RefSpeed; // cm/s the clip was authored at, 0 for clips that do not move
	};

	static const FClipDef Defs[Num] =
	{
		{ TEXT("/Game/CampusAnims/Unarmed/A_Idle.A_Idle"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_F.A_Walk_F"), 160.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_B.A_Walk_B"), 160.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_L.A_Walk_L"), 160.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_R.A_Walk_R"), 160.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_F.A_Run_F"), 375.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_B.A_Run_B"), 375.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_L.A_Run_L"), 375.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_R.A_Run_R"), 375.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Sprint_F.A_Sprint_F"), 600.f },

		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_Idle.A_Crouch_Idle"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_F.A_Crouch_F"), 150.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_B.A_Crouch_B"), 150.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_L.A_Crouch_L"), 150.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_R.A_Crouch_R"), 150.f },

		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Idle.A_Rifle_Idle"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_F.A_Rifle_Walk_F"), 170.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_B.A_Rifle_Walk_B"), 170.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_L.A_Rifle_Walk_L"), 170.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_R.A_Rifle_Walk_R"), 170.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_F.A_Rifle_Run_F"), 375.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_B.A_Rifle_Run_B"), 375.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_L.A_Rifle_Run_L"), 375.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_R.A_Rifle_Run_R"), 375.f },

		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Start.A_Jump_Start"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Fall.A_Jump_Fall"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Land.A_Jump_Land"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Jump_Start.A_Rifle_Jump_Start"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Jump_Fall.A_Rifle_Jump_Fall"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Jump_Land.A_Rifle_Jump_Land"), 0.f },

		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Fire.A_Rifle_Fire"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Reload.A_Rifle_Reload"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Equip.A_Rifle_Equip"), 0.f },
	};

	// Seconds of the jump start clip used before crossfading into the fall loop.
	constexpr float JumpStartWindow = 0.4f;
	constexpr float LandDuration = 0.45f;
	constexpr float UpperBodyBlendIn = 0.15f;
	constexpr float UpperBodyBlendOut = 0.2f;
}

// ---------------------------------------------------------------------------------------------
// Proxy (worker thread)

void FCampusAnimInstanceProxy::SamplePose(const FCampusAnimSample& Sample, FPoseContext& Out) const
{
	FAnimationPoseData PoseData(Out);
	Sample.Sequence->GetAnimationPose(PoseData, FAnimExtractContext(double(Sample.Time), false, FDeltaTimeRecord(), Sample.bLoop));
}

bool FCampusAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	float TotalWeight = 0.f;
	for (const FCampusAnimSample& Sample : Samples)
	{
		if (Sample.Sequence && Sample.Weight > UE_KINDA_SMALL_NUMBER)
		{
			TotalWeight += Sample.Weight;
		}
	}
	if (TotalWeight <= UE_KINDA_SMALL_NUMBER)
	{
		Output.ResetToRefPose();
		return true;
	}

	TIndirectArray<FPoseContext> Poses;
	TArray<const FCompactPose*, TInlineAllocator<16>> PosePtrs;
	TArray<const FBlendedCurve*, TInlineAllocator<16>> CurvePtrs;
	TArray<const UE::Anim::FStackAttributeContainer*, TInlineAllocator<16>> AttributePtrs;
	TArray<float, TInlineAllocator<16>> Weights;
	for (const FCampusAnimSample& Sample : Samples)
	{
		if (!Sample.Sequence || Sample.Weight <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		FPoseContext* Pose = new FPoseContext(this);
		SamplePose(Sample, *Pose);
		Poses.Add(Pose);
		PosePtrs.Add(&Pose->Pose);
		CurvePtrs.Add(&Pose->Curve);
		AttributePtrs.Add(&Pose->CustomAttributes);
		Weights.Add(Sample.Weight / TotalWeight);
	}

	FAnimationPoseData OutData(Output);
	FAnimationRuntime::BlendPosesTogetherIndirect(PosePtrs, CurvePtrs, AttributePtrs, Weights, OutData);

	// The source clips carry root motion; the capsule drives movement, so pin the root in place.
	const FCompactPoseBoneIndex Root(0);
	Output.Pose[Root] = Output.Pose.GetRefPose(Root);

	ApplyUpperBody(Output);

	if (Additive.Sequence && Additive.Weight > UE_KINDA_SMALL_NUMBER)
	{
		FPoseContext AdditivePose(this, true);
		SamplePose(Additive, AdditivePose);
		const FAnimationPoseData AdditiveData(AdditivePose);
		FAnimationRuntime::AccumulateAdditivePose(OutData, AdditiveData, Additive.Weight, Additive.Sequence->GetAdditiveAnimType());
		Output.Pose.NormalizeRotations();
	}

	ApplyAimPitch(Output);
	return true;
}

void FCampusAnimInstanceProxy::ApplyUpperBody(FPoseContext& Output)
{
	if (!UpperBody.Sequence || UpperBody.Weight <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const int32 SpineMeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("spine_01"));
	if (SpineMeshIndex == INDEX_NONE)
	{
		return;
	}
	const FCompactPoseBoneIndex Spine = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(SpineMeshIndex));
	if (!Spine.IsValid())
	{
		return;
	}

	FPoseContext UpperPose(this);
	SamplePose(UpperBody, UpperPose);

	// Compact bones are ordered parents-first, so one pass marks spine_01 and all its children.
	TArray<bool, TInlineAllocator<128>> IsUpper;
	IsUpper.SetNumZeroed(Output.Pose.GetNumBones());
	for (const FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
	{
		const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Index);
		const bool bUpper = Index == Spine || (Parent.IsValid() && IsUpper[Parent.GetInt()]);
		IsUpper[Index.GetInt()] = bUpper;
		if (bUpper)
		{
			Output.Pose[Index].BlendWith(UpperPose.Pose[Index], UpperBody.Weight);
		}
	}
}

void FCampusAnimInstanceProxy::ApplyAimPitch(FPoseContext& Output) const
{
	const float Pitch = AimPitch * AimPitchAlpha;
	if (FMath::Abs(Pitch) < 0.1f)
	{
		return;
	}

	static const FName SpineBones[] = { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") };
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();

	TArray<FCompactPoseBoneIndex, TInlineAllocator<5>> Chain;
	for (const FName& Name : SpineBones)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Name);
		if (MeshIndex != INDEX_NONE)
		{
			const FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			if (Index.IsValid())
			{
				Chain.Add(Index);
			}
		}
	}
	if (Chain.IsEmpty())
	{
		return;
	}

	// The mesh faces +Y in component space, so +X is the pitch axis.
	const float RadiansPerBone = FMath::DegreesToRadians(Pitch / Chain.Num());
	for (const FCompactPoseBoneIndex Index : Chain)
	{
		FQuat ParentRotation = FQuat::Identity;
		for (FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Index); Parent.IsValid(); Parent = Bones.GetParentBoneIndex(Parent))
		{
			ParentRotation = Output.Pose[Parent].GetRotation() * ParentRotation;
		}
		const FVector LocalAxis = ParentRotation.Inverse().RotateVector(FVector::XAxisVector);
		FTransform& Bone = Output.Pose[Index];
		Bone.SetRotation((FQuat(LocalAxis, RadiansPerBone) * Bone.GetRotation()).GetNormalized());
	}
}

// ---------------------------------------------------------------------------------------------
// Anim instance (game thread)

UCampusAnimInstance::UCampusAnimInstance()
{
	Clips.SetNum(CampusClips::Num);
	for (int32 Index = 0; Index < CampusClips::Num; ++Index)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequence> Finder(CampusClips::Defs[Index].Path);
		Clips[Index] = Finder.Object;
	}
}

FAnimInstanceProxy* UCampusAnimInstance::CreateAnimInstanceProxy()
{
	return new FCampusAnimInstanceProxy(this);
}

void UCampusAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	delete static_cast<FCampusAnimInstanceProxy*>(InProxy);
}

const UAnimSequence* UCampusAnimInstance::Clip(int32 Index) const
{
	return Clips.IsValidIndex(Index) ? Clips[Index].Get() : nullptr;
}

void UCampusAnimInstance::PlayFire()
{
	FireTime = 0.f;
}

void UCampusAnimInstance::PlayReload()
{
	UpperBodyClip = CampusClips::Reload;
	UpperBodyTime = 0.f;
}

void UCampusAnimInstance::PlayEquip()
{
	UpperBodyClip = CampusClips::Equip;
	UpperBodyTime = 0.f;
}

float UCampusAnimInstance::GetReloadDuration() const
{
	const UAnimSequence* Reload = Clip(CampusClips::Reload);
	return Reload ? Reload->GetPlayLength() : 0.f;
}

void UCampusAnimInstance::AddLocomotion(TArray<FCampusAnimSample>& Out, FGaitAccum& Gait, int32 FirstClip, bool bHasRunTier, bool bHasSprint,
	float SetWeight, float Speed, float DirectionDeg) const
{
	if (SetWeight <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float MoveAlpha = FMath::GetMappedRangeValueClamped(FVector2f(10.f, 80.f), FVector2f(0.f, 1.f), Speed);
	if (const UAnimSequence* Idle = Clip(FirstClip))
	{
		Out.Add({ Idle, FMath::Fmod(IdleTime, Idle->GetPlayLength()), SetWeight * (1.f - MoveAlpha), true });
	}
	if (MoveAlpha <= 0.f)
	{
		return;
	}

	// Four-way direction split: forward, back, left, right.
	const float Radians = FMath::DegreesToRadians(DirectionDeg);
	float DirWeights[4] = { FMath::Max(0.f, FMath::Cos(Radians)), FMath::Max(0.f, -FMath::Cos(Radians)), FMath::Max(0.f, -FMath::Sin(Radians)), FMath::Max(0.f, FMath::Sin(Radians)) };
	const float DirTotal = DirWeights[0] + DirWeights[1] + DirWeights[2] + DirWeights[3];

	const float RunAlpha = bHasRunTier ? FMath::GetMappedRangeValueClamped(FVector2f(180.f, 380.f), FVector2f(0.f, 1.f), Speed) : 0.f;
	const float SprintAlpha = bHasSprint ? FMath::GetMappedRangeValueClamped(FVector2f(380.f, 600.f), FVector2f(0.f, 1.f), Speed) : 0.f;

	auto AddMoving = [&](int32 ClipIndex, float Weight)
	{
		const UAnimSequence* Sequence = Clip(ClipIndex);
		if (!Sequence || Weight <= UE_KINDA_SMALL_NUMBER)
		{
			return;
		}
		const float Length = Sequence->GetPlayLength();
		Out.Add({ Sequence, MovePhase * Length, Weight, true });
		Gait.Weight += Weight;
		Gait.CycleLength += Weight * Length;
		Gait.RefSpeed += Weight * CampusClips::Defs[ClipIndex].RefSpeed;
	};

	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		const float Weight = SetWeight * MoveAlpha * DirWeights[Dir] / DirTotal;
		if (Weight <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		AddMoving(FirstClip + 1 + Dir, Weight * (1.f - RunAlpha));
		if (bHasRunTier)
		{
			const float SprintWeight = Dir == 0 ? Weight * RunAlpha * SprintAlpha : 0.f;
			AddMoving(FirstClip + 5 + Dir, Weight * RunAlpha - SprintWeight);
			AddMoving(FirstClip + 9, SprintWeight);
		}
	}
}

void UCampusAnimInstance::AddOneShotPair(TArray<FCampusAnimSample>& Out, int32 UnarmedClip, int32 RifleClip, float Time, float Weight, bool bLoop) const
{
	const float RifleShare = RifleWeight * (1.f - CrouchWeight);
	const int32 Indices[2] = { UnarmedClip, RifleClip };
	const float Weights[2] = { Weight * (1.f - RifleShare), Weight * RifleShare };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const UAnimSequence* Sequence = Clip(Indices[Index]);
		if (Sequence && Weights[Index] > UE_KINDA_SMALL_NUMBER)
		{
			const float Length = Sequence->GetPlayLength();
			Out.Add({ Sequence, bLoop ? FMath::Fmod(Time, Length) : FMath::Min(Time, Length), Weights[Index], bLoop });
		}
	}
}

void UCampusAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	using namespace CampusClips;

	FCampusAnimInstanceProxy& Proxy = GetProxyOnGameThread<FCampusAnimInstanceProxy>();
	TArray<FCampusAnimSample>& Samples = Proxy.Samples;
	Samples.Reset();
	Proxy.UpperBody = FCampusAnimSample();
	Proxy.Additive = FCampusAnimSample();
	Proxy.AimPitchAlpha = 0.f;
	IdleTime = FMath::Fmod(IdleTime + DeltaSeconds, 3600.f);

	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		// Editor previews and spectators: just breathe.
		if (const UAnimSequence* Idle = Clip(Unarmed))
		{
			Samples.Add({ Idle, FMath::Fmod(IdleTime, Idle->GetPlayLength()), 1.f, true });
		}
		return;
	}

	const ACampusCharacter* Campus = Cast<ACampusCharacter>(Character);
	const bool bArmed = Campus && Campus->IsArmed();
	const bool bInAir = Movement->IsFalling();

	FVector Velocity = Character->GetVelocity();
	const float VerticalSpeed = Velocity.Z;
	Velocity.Z = 0.f;
	const float Speed = Velocity.Size();
	const float Direction = Speed > 5.f ? FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Character->GetActorRotation().Yaw) : 0.f;

	auto Approach = [DeltaSeconds](float& Value, float Target, float BlendTime)
	{
		Value = FMath::FInterpConstantTo(Value, Target, DeltaSeconds, 1.f / BlendTime);
	};
	Approach(CrouchWeight, Movement->IsCrouching() ? 1.f : 0.f, 0.2f);
	Approach(RifleWeight, bArmed ? 1.f : 0.f, 0.2f);
	Approach(AirWeight, bInAir ? 1.f : 0.f, 0.12f);

	if (bInAir)
	{
		if (!bWasInAir)
		{
			AirTime = 0.f;
			bJumped = VerticalSpeed > 50.f;
		}
		AirTime += DeltaSeconds;
	}
	else if (bWasInAir && AirTime > 0.3f)
	{
		LandWeight = 1.f;
		LandTime = 0.f;
	}
	bWasInAir = bInAir;
	if (LandWeight > 0.f)
	{
		LandTime += DeltaSeconds;
		LandWeight = FMath::Max(0.f, 1.f - LandTime / LandDuration);
	}

	// Ground locomotion: crouch, unarmed and rifle sets crossfade by their weights.
	FGaitAccum Gait;
	AddLocomotion(Samples, Gait, CampusClips::Unarmed, true, true, (1.f - CrouchWeight) * (1.f - RifleWeight), Speed, Direction);
	AddLocomotion(Samples, Gait, CampusClips::Crouch, false, false, CrouchWeight, Speed, Direction);
	AddLocomotion(Samples, Gait, CampusClips::Rifle, true, false, (1.f - CrouchWeight) * RifleWeight, Speed, Direction);
	if (Gait.Weight > UE_KINDA_SMALL_NUMBER)
	{
		const float CycleLength = Gait.CycleLength / Gait.Weight;
		const float PlayRate = FMath::Clamp(Speed / FMath::Max(Gait.RefSpeed / Gait.Weight, 1.f), 0.7f, 1.5f);
		MovePhase = FMath::Fmod(MovePhase + DeltaSeconds * PlayRate / FMath::Max(CycleLength, 0.1f), 1.f);
	}

	// Landing matters most when standing still; while moving, mostly keep the stride.
	const float MoveAlpha = FMath::GetMappedRangeValueClamped(FVector2f(10.f, 80.f), FVector2f(0.f, 1.f), Speed);
	const float Land = (1.f - AirWeight) * LandWeight * (1.f - 0.7f * MoveAlpha);
	const float GroundScale = (1.f - AirWeight) * (1.f - Land);
	for (FCampusAnimSample& Sample : Samples)
	{
		Sample.Weight *= GroundScale;
	}

	if (AirWeight > UE_KINDA_SMALL_NUMBER)
	{
		const float StartWeight = bJumped ? 1.f - FMath::Clamp((AirTime - JumpStartWindow) / 0.2f, 0.f, 1.f) : 0.f;
		AddOneShotPair(Samples, JumpStart, RifleJumpStart, AirTime, AirWeight * StartWeight, false);
		AddOneShotPair(Samples, Fall, RifleFall, AirTime, AirWeight * (1.f - StartWeight), true);
	}
	if (Land > UE_KINDA_SMALL_NUMBER)
	{
		AddOneShotPair(Samples, CampusClips::Land, RifleLand, LandTime, Land, false);
	}

	// Upper-body one-shot (reload / equip), else hold the rifle while crouched.
	float UpperAlpha = 0.f;
	if (UpperBodyClip != INDEX_NONE)
	{
		const UAnimSequence* Sequence = Clip(UpperBodyClip);
		UpperBodyTime += DeltaSeconds;
		const float Length = Sequence ? Sequence->GetPlayLength() : 0.f;
		if (UpperBodyTime >= Length)
		{
			UpperBodyClip = INDEX_NONE;
		}
		else
		{
			UpperAlpha = FMath::Clamp(FMath::Min(UpperBodyTime / UpperBodyBlendIn, (Length - UpperBodyTime) / UpperBodyBlendOut), 0.f, 1.f);
			Proxy.UpperBody = { Sequence, UpperBodyTime, UpperAlpha, false };
		}
	}
	if (UpperBodyClip == INDEX_NONE && CrouchWeight * RifleWeight > 0.01f)
	{
		if (const UAnimSequence* RifleIdle = Clip(CampusClips::Rifle))
		{
			Proxy.UpperBody = { RifleIdle, FMath::Fmod(IdleTime, RifleIdle->GetPlayLength()), CrouchWeight * RifleWeight, true };
		}
	}

	if (FireTime >= 0.f)
	{
		const UAnimSequence* FireClip = Clip(Fire);
		FireTime += DeltaSeconds;
		if (!FireClip || FireTime >= FireClip->GetPlayLength())
		{
			FireTime = -1.f;
		}
		else
		{
			Proxy.Additive = { FireClip, FireTime, RifleWeight, false };
		}
	}

	Proxy.AimPitch = FMath::Clamp(FRotator::NormalizeAxis(Character->GetBaseAimRotation().Pitch), -70.f, 70.f);
	Proxy.AimPitchAlpha = RifleWeight * (1.f - UpperAlpha);
}
