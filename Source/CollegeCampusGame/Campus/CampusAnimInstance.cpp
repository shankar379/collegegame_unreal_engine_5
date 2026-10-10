#include "Campus/CampusAnimInstance.h"

#include "AnimationRuntime.h"
#include "Animation/AnimSequence.h"
#include "Campus/CampusCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace CampusClips
{
	// Eight directions, clockwise from forward (angle = 45 deg * index): F, FR, R, BR, B, BL, L, FL.
	constexpr int32 NumDirs = 8;

	// Locomotion sets: Idle, then 8 directions per gait tier.
	enum : int32
	{
		Unarmed = 0,       // Idle, Walk x8, Run x8, Sprint F/FR/FL
		Crouch = 20,       // Idle, Move x8
		Rifle = 29,        // Idle, Walk x8, Jog x8
		JumpStart = 46, Fall, Land,
		Fire, Reload, Equip,
		MantleStand, MantleWalk, MantleRun,
		JumpStartWalk, JumpStartRun, JumpStartSprint,
		LandWalk, LandRun, LandSprint,
		Num
	};
	constexpr int32 SprintF = Unarmed + 17, SprintFR = Unarmed + 18, SprintFL = Unarmed + 19;

	struct FClipDef
	{
		const TCHAR* Path;
		/** cm/s the clip travels at, measured from its root (or pelvis) motion; 0 for in-place clips. */
		float RefSpeed;
		/** Pelvis travel over one loop, in mesh space. The Lyra rifle clips were retargeted with their
		 *  forward motion on the pelvis instead of the root, so it is removed per sample (else the body
		 *  slides ahead of the capsule and snaps back every loop). Zero for clips that move the root. */
		FVector3f PelvisDrift = FVector3f::ZeroVector;
		/** Step cycles in the loop and the time the left foot first leads (measured from the foot
		 *  bones). Loops are sampled per step cycle from this point, so every clip in a blend has its
		 *  feet in the same place even when the clips hold different numbers of steps. */
		int32 Cycles = 0;
		float FirstStep = 0.f;
	};

	// Generated from the clips (Tools: animset2.py): speed = root or pelvis travel / length, pelvis drift,
	// step cycles and the time the left foot first leads along the travel direction.
	static const FClipDef Defs[Num] =
	{
		{ TEXT("/Game/CampusAnims/Unarmed/A_Idle.A_Idle"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_F.A_Walk_F"), 200.f, FVector3f::ZeroVector, 4, 0.000f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_FR.A_Walk_FR"), 200.f, FVector3f::ZeroVector, 4, 0.000f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_R.A_Walk_R"), 180.f, FVector3f::ZeroVector, 4, 0.033f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_BR.A_Walk_BR"), 150.f, FVector3f::ZeroVector, 4, 0.067f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_B.A_Walk_B"), 150.f, FVector3f::ZeroVector, 4, 0.067f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_BL.A_Walk_BL"), 150.f, FVector3f::ZeroVector, 4, 0.033f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_L.A_Walk_L"), 180.f, FVector3f::ZeroVector, 4, 0.517f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Walk_FL.A_Walk_FL"), 200.f, FVector3f::ZeroVector, 4, 0.017f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_F.A_Run_F"), 500.f, FVector3f::ZeroVector, 4, 0.600f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_FR.A_Run_FR"), 500.f, FVector3f::ZeroVector, 4, 0.567f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_R.A_Run_R"), 350.f, FVector3f::ZeroVector, 4, 0.537f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_BR.A_Run_BR"), 300.f, FVector3f::ZeroVector, 4, 0.570f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_B.A_Run_B"), 300.f, FVector3f::ZeroVector, 4, 0.604f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_BL.A_Run_BL"), 300.f, FVector3f::ZeroVector, 4, 0.537f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_L.A_Run_L"), 350.f, FVector3f::ZeroVector, 4, 0.571f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Run_FL.A_Run_FL"), 500.f, FVector3f::ZeroVector, 4, 0.567f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Sprint_F.A_Sprint_F"), 700.f, FVector3f::ZeroVector, 4, 0.467f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Sprint_FR.A_Sprint_FR"), 700.f, FVector3f::ZeroVector, 4, 0.467f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Sprint_FL.A_Sprint_FL"), 700.f, FVector3f::ZeroVector, 4, 0.467f },

		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_Idle.A_Crouch_Idle"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_F.A_Crouch_F"), 225.f, FVector3f::ZeroVector, 4, 0.000f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_FR.A_Crouch_FR"), 225.f, FVector3f::ZeroVector, 4, 0.033f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_R.A_Crouch_R"), 200.f, FVector3f::ZeroVector, 4, 0.033f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_BR.A_Crouch_BR"), 180.f, FVector3f::ZeroVector, 4, 0.033f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_B.A_Crouch_B"), 180.f, FVector3f::ZeroVector, 4, 0.067f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_BL.A_Crouch_BL"), 181.f, FVector3f::ZeroVector, 4, 0.033f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_L.A_Crouch_L"), 200.f, FVector3f::ZeroVector, 4, 0.067f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Crouch_FL.A_Crouch_FL"), 225.f, FVector3f::ZeroVector, 4, 0.033f },

		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Idle.A_Rifle_Idle"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_F.A_Rifle_Walk_F"), 295.f, FVector3f(-0.f, 453.f, 0.f), 2, 0.607f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_FR.A_Rifle_Walk_FR"), 295.f, FVector3f(-320.f, 320.f, 0.f), 2, 0.607f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_R.A_Rifle_Walk_R"), 291.f, FVector3f(-437.f, 0.f, 0.f), 2, 0.233f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_BR.A_Rifle_Walk_BR"), 302.f, FVector3f(-242.f, -242.f, 0.f), 2, 0.389f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_B.A_Rifle_Walk_B"), 291.f, FVector3f(-0.f, -349.f, 0.f), 2, 0.400f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_BL.A_Rifle_Walk_BL"), 302.f, FVector3f(242.f, -242.f, 0.f), 2, 0.389f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_L.A_Rifle_Walk_L"), 291.f, FVector3f(359.f, -0.f, 0.f), 2, 0.533f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Walk_FL.A_Rifle_Walk_FL"), 295.f, FVector3f(320.f, 320.f, 0.f), 2, 0.607f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_F.A_Rifle_Run_F"), 604.f, FVector3f(0.f, 927.f, 0.f), 3, 0.135f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_FR.A_Rifle_Run_FR"), 604.f, FVector3f(-1310.f, 1310.f, 0.f), 3, 0.135f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_R.A_Rifle_Run_R"), 582.f, FVector3f(-621.f, 0.f, 0.f), 2, 0.367f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_BR.A_Rifle_Run_BR"), 573.f, FVector3f(-730.f, -730.f, 0.f), 4, 0.118f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_B.A_Rifle_Run_B"), 574.f, FVector3f(0.f, -1033.f, 0.f), 4, 0.118f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_BL.A_Rifle_Run_BL"), 573.f, FVector3f(730.f, -730.f, 0.f), 4, 0.118f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_L.A_Rifle_Run_L"), 573.f, FVector3f(267.f, 0.f, 0.f), 1, 0.133f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Run_FL.A_Rifle_Run_FL"), 604.f, FVector3f(655.f, 655.f, 0.f), 3, 0.135f },

		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Start.A_Jump_Start"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Fall.A_Jump_Fall"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Land.A_Jump_Land"), 0.f },

		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Fire.A_Rifle_Fire"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Reload.A_Rifle_Reload"), 0.f },
		{ TEXT("/Game/CampusAnims/Rifle/A_Rifle_Equip.A_Rifle_Equip"), 0.f },

		// Mantle (hands on the ledge, pull up): root rises 100 cm; the character warps it to the real ledge.
		{ TEXT("/Game/CampusAnims/Traversal/A_Mantle_Stand.A_Mantle_Stand"), 0.f },
		{ TEXT("/Game/CampusAnims/Traversal/A_Mantle_Walk.A_Mantle_Walk"), 0.f },
		{ TEXT("/Game/CampusAnims/Traversal/A_Mantle_Run.A_Mantle_Run"), 0.f },

		// Speed-matched jumps (sample's walk / run / sprint takeoffs and landings).
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Start_Walk.A_Jump_Start_Walk"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Start_Run.A_Jump_Start_Run"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Start_Sprint.A_Jump_Start_Sprint"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Land_Walk.A_Jump_Land_Walk"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Land_Run.A_Jump_Land_Run"), 0.f },
		{ TEXT("/Game/CampusAnims/Unarmed/A_Jump_Land_Sprint.A_Jump_Land_Sprint"), 0.f },
	};

	/** Jump clip timing measured from each clip's root track: takeoff (root starts rising),
	 *  apex (highest root) and touchdown (root back on the ground). Index: stand, walk, run, sprint. */
	struct FJumpTiming { int32 Start; int32 Land; float Takeoff; float Apex; float Impact; };
	static const FJumpTiming Jumps[4] =
	{
		{ JumpStart, Land, 0.36f, 0.81f, 0.52f },
		{ JumpStartWalk, LandWalk, 0.57f, 1.05f, 0.49f },
		{ JumpStartRun, LandRun, 0.23f, 0.70f, 0.50f },
		{ JumpStartSprint, LandSprint, 0.33f, 0.79f, 0.50f },
	};
	inline int32 JumpKindForSpeed(float Speed)
	{
		return Speed < 60.f ? 0 : (Speed < 330.f ? 1 : (Speed < 560.f ? 2 : 3));
	}

	// Gait thresholds (cm/s) between the walk, run and sprint clips of each set, from the speeds above.
	constexpr float UnarmedWalk = 200.f, UnarmedRun = 500.f, UnarmedSprint = 700.f;
	constexpr float RifleWalk = 293.f, RifleRun = 600.f;

	// Landing plays from touchdown for this long, then hands back to locomotion.
	constexpr float LandDuration = 0.4f;
	constexpr float UpperBodyBlendIn = 0.15f;
	constexpr float UpperBodyBlendOut = 0.2f;
}

// ---------------------------------------------------------------------------------------------
// Proxy (worker thread)

void FCampusAnimInstanceProxy::SamplePose(const FCampusAnimSample& Sample, FPoseContext& Out) const
{
	FAnimationPoseData PoseData(Out);
	Sample.Sequence->GetAnimationPose(PoseData, FAnimExtractContext(double(Sample.Time), false, FDeltaTimeRecord(), Sample.bLoop));

	// Remove the clip's travel from the pelvis so the body stays over the capsule.
	if (!Sample.PelvisDrift.IsZero())
	{
		const FBoneContainer& Bones = Out.Pose.GetBoneContainer();
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("pelvis"));
		const float Length = Sample.Sequence->GetPlayLength();
		if (MeshIndex != INDEX_NONE && Length > 0.f)
		{
			const FCompactPoseBoneIndex Pelvis = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			if (Pelvis.IsValid())
			{
				Out.Pose[Pelvis].AddToTranslation(-FVector(Sample.PelvisDrift) * (Sample.Time / Length));
			}
		}
	}
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
	for (const FCompactPoseBoneIndex& Index : Output.Pose.ForEachBoneIndex())
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
	for (const FCompactPoseBoneIndex& Index : Chain)
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

const UAnimSequence* UCampusAnimInstance::GetMantleClip(int32 Kind) const
{
	return Clip(CampusClips::MantleStand + FMath::Clamp(Kind, 0, 2));
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

	// Eight-way: blend the two clips either side of the travel direction (real diagonal clips, so the
	// legs never cross the way a forward + side mix does).
	const float Clockwise = DirectionDeg < 0.f ? DirectionDeg + 360.f : DirectionDeg;
	const float Sector = Clockwise / 45.f;
	const int32 DirA = FMath::FloorToInt(Sector) % CampusClips::NumDirs;
	const int32 DirB = (DirA + 1) % CampusClips::NumDirs;
	const float BlendB = Sector - FMath::FloorToFloat(Sector);
	const int32 Dirs[2] = { DirA, DirB };
	const float DirWeights[2] = { 1.f - BlendB, BlendB };

	const bool bRifleSet = FirstClip == CampusClips::Rifle;
	const float WalkRef = bRifleSet ? CampusClips::RifleWalk : CampusClips::UnarmedWalk;
	const float RunRef = bRifleSet ? CampusClips::RifleRun : CampusClips::UnarmedRun;
	const float RunAlpha = bHasRunTier ? FMath::GetMappedRangeValueClamped(FVector2f(WalkRef, RunRef), FVector2f(0.f, 1.f), Speed) : 0.f;
	const float SprintAlpha = bHasSprint ? FMath::GetMappedRangeValueClamped(FVector2f(CampusClips::UnarmedRun, CampusClips::UnarmedSprint), FVector2f(0.f, 1.f), Speed) : 0.f;

	auto AddMoving = [&](int32 ClipIndex, float Weight)
	{
		const UAnimSequence* Sequence = Clip(ClipIndex);
		if (!Sequence || Weight <= UE_KINDA_SMALL_NUMBER)
		{
			return;
		}
		const CampusClips::FClipDef& Def = CampusClips::Defs[ClipIndex];
		const float Length = Sequence->GetPlayLength();
		const float StepCycle = Length / FMath::Max(Def.Cycles, 1);
		const float Time = FMath::Fmod(Def.FirstStep + MovePhase * StepCycle, Length);
		Out.Add({ Sequence, Time, Weight, true, Def.PelvisDrift });
		Gait.Weight += Weight;
		Gait.CycleLength += Weight * StepCycle;
		Gait.RefSpeed += Weight * Def.RefSpeed;
	};
	// Sprint only has forward clips; side and back sectors fall back to the nearest forward one.
	auto SprintClipFor = [](int32 Dir)
	{
		switch (Dir)
		{
		case 1: case 2: return CampusClips::SprintFR;
		case 6: case 7: return CampusClips::SprintFL;
		default: return CampusClips::SprintF;
		}
	};

	const int32 TierSize = CampusClips::NumDirs;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const float Weight = SetWeight * MoveAlpha * DirWeights[Index];
		if (Weight <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const int32 Dir = Dirs[Index];
		AddMoving(FirstClip + 1 + Dir, Weight * (1.f - RunAlpha));
		if (bHasRunTier)
		{
			const float SprintWeight = Weight * RunAlpha * SprintAlpha;
			AddMoving(FirstClip + 1 + TierSize + Dir, Weight * RunAlpha - SprintWeight);
			if (bHasSprint)
			{
				AddMoving(SprintClipFor(Dir), SprintWeight);
			}
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
			JumpKind = CampusClips::JumpKindForSpeed(Speed);
		}
		AirTime += DeltaSeconds;
	}
	else if (bWasInAir && AirTime > 0.3f)
	{
		LandWeight = 1.f;
		LandTime = 0.f;
		LandKind = CampusClips::JumpKindForSpeed(Speed);
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
		const float PlayRate = FMath::Clamp(Speed / FMath::Max(Gait.RefSpeed / Gait.Weight, 1.f), 0.4f, 1.6f);
		MovePhase = FMath::Fmod(MovePhase + DeltaSeconds * PlayRate / FMath::Max(CycleLength, 0.1f), 1.f);
	}

	// The landing clip matches the landing speed (it keeps the stride), so it plays at full weight.
	const float Land = (1.f - AirWeight) * LandWeight;
	const float GroundScale = (1.f - AirWeight) * (1.f - Land);
	for (FCampusAnimSample& Sample : Samples)
	{
		Sample.Weight *= GroundScale;
	}

	if (AirWeight > UE_KINDA_SMALL_NUMBER)
	{
		// From takeoff (the wind-up already happened on the ground) to the apex, then into the fall loop.
		const CampusClips::FJumpTiming& Jump = CampusClips::Jumps[JumpKind];
		const float Rise = Jump.Apex - Jump.Takeoff;
		const float StartWeight = bJumped ? 1.f - FMath::Clamp((AirTime - Rise) / 0.2f, 0.f, 1.f) : 0.f;
		AddOneShotPair(Samples, Jump.Start, Jump.Start, Jump.Takeoff + FMath::Min(AirTime, Rise + 0.2f), AirWeight * StartWeight, false);
		AddOneShotPair(Samples, Fall, Fall, AirTime, AirWeight * (1.f - StartWeight), true);
	}
	if (Land > UE_KINDA_SMALL_NUMBER)
	{
		const CampusClips::FJumpTiming& Landing = CampusClips::Jumps[LandKind];
		AddOneShotPair(Samples, Landing.Land, Landing.Land, Landing.Impact - 0.04f + LandTime, Land, false);
	}

	// Mantle / climb: one full-body clip driven by the character's traversal clock.
	int32 TraversalKind = INDEX_NONE;
	float TraversalTime = 0.f;
	const bool bTraversing = Campus && Campus->GetTraversal(TraversalKind, TraversalTime);
	Approach(TraversalWeight, bTraversing ? 1.f : 0.f, bTraversing ? 0.1f : 0.25f);
	if (bTraversing)
	{
		LastTraversalKind = TraversalKind;
		LastTraversalTime = TraversalTime;
	}
	if (TraversalWeight > UE_KINDA_SMALL_NUMBER && LastTraversalKind != INDEX_NONE)
	{
		for (FCampusAnimSample& Sample : Samples)
		{
			Sample.Weight *= 1.f - TraversalWeight;
		}
		if (const UAnimSequence* Mantle = Clip(MantleStand + LastTraversalKind))
		{
			Samples.Add({ Mantle, FMath::Min(LastTraversalTime, Mantle->GetPlayLength()), TraversalWeight, false });
		}
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
	// Sets without rifle clips (crouch, air, landing) hold the rifle with the upper body only.
	const float HoldRifle = RifleWeight * FMath::Max3(CrouchWeight, AirWeight, Land);
	if (UpperBodyClip == INDEX_NONE && HoldRifle > 0.01f)
	{
		if (const UAnimSequence* RifleIdle = Clip(CampusClips::Rifle))
		{
			Proxy.UpperBody = { RifleIdle, FMath::Fmod(IdleTime, RifleIdle->GetPlayLength()), HoldRifle, true };
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
