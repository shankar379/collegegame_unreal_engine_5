#include "Campus/CampusAnimInstance.h"

#include "AnimationRuntime.h"
#include "BonePose.h"
#include "TwoBoneIK.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimSequence.h"
#include "Campus/CampusCharacter.h"
#include "Campus/CampusDynamicClips.h"
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
		ClimbStand, ClimbWalk, ClimbRun,
		// Prone (Mixamo crawl retargeted to the mannequin, Content/CampusAnims/Prone) + the sample's face-down get-up
		ProneIdle, ProneForward, ProneBackward, ProneGetUp,
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

		// Climb (jump, grab a high ledge, pull up): root rises 250 cm. The sample uses it for 1.5 - 2.75 m.
		{ TEXT("/Game/CampusAnims/Traversal/A_Climb_Stand.A_Climb_Stand"), 0.f },
		{ TEXT("/Game/CampusAnims/Traversal/A_Climb_Walk.A_Climb_Walk"), 0.f },
		{ TEXT("/Game/CampusAnims/Traversal/A_Climb_Run.A_Climb_Run"), 0.f },

		// In place; RefSpeed = how fast the hands / knees sweep the floor at 1x (measured), so the crawl's play
		// rate matches the real prone speed and nothing skates.
		{ TEXT("/Game/CampusAnims/Prone/A_Prone_Prone_Forward_Stop.A_Prone_Prone_Forward_Stop"), 0.f },
		{ TEXT("/Game/CampusAnims/Prone/A_Prone_Prone_Forward.A_Prone_Prone_Forward"), 25.f },
		{ TEXT("/Game/CampusAnims/Prone/A_Prone_Crawl_Backwards_In_Prone.A_Prone_Crawl_Backwards_In_Prone"), 45.f },
		{ TEXT("/Game/Characters/UEFN_Mannequin/Animations/Ragdoll/M_ragdoll_getup_stand_F.M_ragdoll_getup_stand_F"), 0.f },
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

	if (Sample.bFoldRootIntoPelvis)
	{
		// pelvis (component) = pelvis (local) * root; keep that, then give the root its reference pose back
		const FBoneContainer& Bones = Out.Pose.GetBoneContainer();
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(TEXT("pelvis"));
		const FCompactPoseBoneIndex Root(0);
		if (MeshIndex != INDEX_NONE)
		{
			const FCompactPoseBoneIndex Pelvis = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
			if (Pelvis.IsValid() && Bones.GetParentBoneIndex(Pelvis) == Root)
			{
				const FTransform RootRef = Out.Pose.GetRefPose(Root);
				Out.Pose[Pelvis] = Out.Pose[Pelvis] * Out.Pose[Root] * RootRef.Inverse();
				Out.Pose[Root] = RootRef;
			}
		}
	}

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

	ApplyLean(Output);
	ApplyAimPitch(Output);
	ApplyRootYaw(Output);
	ApplyFootIK(Output);
	ApplyProneYaw(Output);
	ApplyDivePose(Output);

	if (ProneAlpha > UE_KINDA_SMALL_NUMBER)
	{
		// Tip the whole body forward about a point just above the feet, then slide it back so the
		// hips sit over the capsule. The mesh faces +Y, so a negative turn about X lays +Z along +Y.
		const FVector Pivot(0.f, 0.f, 20.f);
		const FQuat Tilt(FVector::XAxisVector, FMath::DegreesToRadians(-82.f * ProneAlpha));
		const FTransform Lay(Tilt, Pivot - Tilt.RotateVector(Pivot) + FVector(0.f, -55.f * ProneAlpha, 0.f));
		Output.Pose[Root] = Output.Pose[Root] * Lay;
	}
	return true;
}

void FCampusAnimInstanceProxy::ApplyDivePose(FPoseContext& Output) const
{
	const float Dive = FMath::Clamp(FreefallAlpha + ChuteAlpha, 0.f, 1.f);
	if (Dive < 0.01f)
	{
		return;
	}
	// THE SKYDIVE AND THE CANOPY HANG, IN CODE. The project ships no parachute animation - there is no
	// skydive / freefall / glide clip anywhere in Content and no loose FBX to import - and the default
	// jump-fall loop over a 300 m drop is the thing the owner saw and called wrong: a man who has just
	// hopped off a kerb, legs tucked, for twenty seconds.
	//
	// Component space: the mesh faces +Y, up is +Z, so the character's LEFT is +X. With the engine's
	// rotation convention that makes
	//    about +X : + leans the body BACK, - pitches it FORWARD (the prone tip below uses -82),
	//    about +Y : - raises the LEFT arm, + raises the right,
	//    about +Z : + swings the LEFT arm forward, - the right.
	// Degrees per bone, blended by the two weights, applied over a pull toward the reference pose so
	// the result does not depend on where the fall loop happens to be.
	struct FDiveBone
	{
		const TCHAR* Name;
		int32 Axis;       // 0 = X, 1 = Y, 2 = Z
		float Freefall;   // belly to earth, arms out
		float Chute;      // upright, arms up on the risers
	};
	static const FDiveBone DiveBones[] =
	{
		// The arch: chest up against the wind in freefall, barely any of it in the harness.
		{ TEXT("spine_02"),   0,  10.f,   4.f },
		{ TEXT("spine_03"),   0,   8.f,   2.f },
		// Arms: out to the sides in the star, up and in on the risers under the canopy.
		{ TEXT("upperarm_l"), 1, -55.f, -78.f },
		{ TEXT("upperarm_r"), 1,  55.f,  78.f },
		{ TEXT("upperarm_l"), 2,  12.f,   6.f },
		{ TEXT("upperarm_r"), 2, -12.f,  -6.f },
		// Elbows: forward in the star, folded back in toward the risers while hanging.
		{ TEXT("lowerarm_l"), 2,  45.f,   0.f },
		{ TEXT("lowerarm_r"), 2, -45.f,   0.f },
		{ TEXT("lowerarm_l"), 1,   0.f,  42.f },
		{ TEXT("lowerarm_r"), 1,   0.f, -42.f },
		// Legs: knees bent and spread in the star, hanging slightly forward under the canopy.
		{ TEXT("thigh_l"),    1, -14.f,  -6.f },
		{ TEXT("thigh_r"),    1,  14.f,   6.f },
		{ TEXT("thigh_l"),    0, -10.f, -14.f },
		{ TEXT("thigh_r"),    0, -10.f, -14.f },
		{ TEXT("calf_l"),     0,  50.f,  25.f },
		{ TEXT("calf_r"),     0,  50.f,  25.f },
		// Head up: face-down at 58 degrees, the neck has to come back to look where you are going.
		{ TEXT("neck_01"),    0,  14.f,   0.f },
		{ TEXT("head"),       0,  10.f,   0.f },
	};

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Find = [&Bones](const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
			: Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};

	// Pull every posed bone toward its reference rotation first: the dive is a pose, not an offset on
	// top of whatever the jump clip is doing with the arms this frame.
	for (const FDiveBone& Entry : DiveBones)
	{
		const FCompactPoseBoneIndex Index = Find(Entry.Name);
		if (Index.IsValid())
		{
			FTransform& Bone = Output.Pose[Index];
			Bone.SetRotation(FQuat::Slerp(Bone.GetRotation(), Output.Pose.GetRefPose(Index).GetRotation(), Dive).GetNormalized());
		}
	}

	// Then the pose itself. The axis is a COMPONENT-space axis taken into the bone's own space through
	// its parent chain (the same trick ApplyAimPitch uses for the spine), so none of this depends on
	// which way a particular bone's local axes happen to point.
	static const FVector Axes[3] = { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector };
	for (const FDiveBone& Entry : DiveBones)
	{
		const float Degrees = Entry.Freefall * FreefallAlpha + Entry.Chute * ChuteAlpha;
		const FCompactPoseBoneIndex Index = Find(Entry.Name);
		if (!Index.IsValid() || FMath::Abs(Degrees) < 0.05f)
		{
			continue;
		}
		FQuat ParentRotation = FQuat::Identity;
		for (FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Index); Parent.IsValid(); Parent = Bones.GetParentBoneIndex(Parent))
		{
			ParentRotation = Output.Pose[Parent].GetRotation() * ParentRotation;
		}
		const FVector LocalAxis = ParentRotation.Inverse().RotateVector(Axes[Entry.Axis]);
		FTransform& Bone = Output.Pose[Index];
		Bone.SetRotation((FQuat(LocalAxis, FMath::DegreesToRadians(Degrees)) * Bone.GetRotation()).GetNormalized());
	}

	// Last, the whole body: 58 degrees of nose-down in freefall, a 6 degree recline in the harness,
	// turned about the hips so the diver stays centred in the capsule instead of swinging out of it.
	const float Pitch = -58.f * FreefallAlpha + 6.f * ChuteAlpha;
	if (FMath::Abs(Pitch) > 0.1f)
	{
		const FCompactPoseBoneIndex Root(0);
		const FVector Pivot(0.f, 0.f, 95.f);
		const FQuat Tilt(FVector::XAxisVector, FMath::DegreesToRadians(Pitch));
		const FTransform Lay(Tilt, Pivot - Tilt.RotateVector(Pivot));
		Output.Pose[Root] = Output.Pose[Root] * Lay;
	}
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
	if (FMath::Abs(Pitch) < 0.1f && CrouchRunBend < 0.01f)
	{
		return;
	}
	// Bent-over crouch run: the lower spine leans forward, the chest straightens part of it back, so the body is
	// low and driven forward while the rifle stays near level (degrees per bone, + = toward the ground).
	static const float BendDeg[] = { 12.f, 10.f, 8.f, -6.f, -6.f };

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
	for (int32 Link = 0; Link < Chain.Num(); ++Link)
	{
		const FCompactPoseBoneIndex& Index = Chain[Link];
		// aim pitch: + looks up; the bend tips the torso down, so it goes in with the opposite sign
		const float Radians = RadiansPerBone - FMath::DegreesToRadians(BendDeg[FMath::Min(Link, 4)] * CrouchRunBend);
		FQuat ParentRotation = FQuat::Identity;
		for (FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Index); Parent.IsValid(); Parent = Bones.GetParentBoneIndex(Parent))
		{
			ParentRotation = Output.Pose[Parent].GetRotation() * ParentRotation;
		}
		const FVector LocalAxis = ParentRotation.Inverse().RotateVector(FVector::XAxisVector);
		FTransform& Bone = Output.Pose[Index];
		Bone.SetRotation((FQuat(LocalAxis, Radians) * Bone.GetRotation()).GetNormalized());
	}
}

void FCampusAnimInstanceProxy::ApplyFootIK(FPoseContext& Output) const
{
	if (FootIKAlpha < 0.01f)
	{
		return;
	}
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Find = [&Bones](const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
	};
	const FCompactPoseBoneIndex Pelvis = Find(TEXT("pelvis"));
	const FCompactPoseBoneIndex Legs[2][3] = {
		{ Find(TEXT("thigh_l")), Find(TEXT("calf_l")), Find(TEXT("foot_l")) },
		{ Find(TEXT("thigh_r")), Find(TEXT("calf_r")), Find(TEXT("foot_r")) },
	};
	if (!Pelvis.IsValid())
	{
		return;
	}

	FCSPose<FCompactPose> CS;
	CS.InitPose(Output.Pose);
	TArray<FBoneTransform> Changed;

	// 1) Hips down so the lower foot can reach its step (the capsule stands on the higher one / the stair ramp).
	const float Drop = PelvisOffset * FootIKAlpha;
	if (FMath::Abs(Drop) > 0.05f)
	{
		FTransform Hips = CS.GetComponentSpaceTransform(Pelvis);
		Hips.AddToTranslation(FVector(0.f, 0.f, Drop));
		Changed.Add(FBoneTransform(Pelvis, Hips));
		CS.SafeSetCSBoneTransforms(Changed);
		Changed.Reset();
	}

	// 2) Each leg: two-bone IK puts the foot on its own step; the knee bends forward (the mesh faces +Y).
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FCompactPoseBoneIndex Thigh = Legs[Side][0], Calf = Legs[Side][1], Foot = Legs[Side][2];
		if (!Thigh.IsValid() || !Calf.IsValid() || !Foot.IsValid())
		{
			continue;
		}
		const float Lift = (FootOffset[Side] - PelvisOffset) * FootIKAlpha;
		if (FMath::Abs(Lift) < 0.05f)
		{
			continue;
		}
		FTransform ThighT = CS.GetComponentSpaceTransform(Thigh);
		FTransform CalfT = CS.GetComponentSpaceTransform(Calf);
		FTransform FootT = CS.GetComponentSpaceTransform(Foot);
		const FVector Effector = FootT.GetLocation() + FVector(0.f, 0.f, Lift);
		const FVector KneeHint = CalfT.GetLocation() + FVector(0.f, 60.f, 0.f);
		AnimationCore::SolveTwoBoneIK(ThighT, CalfT, FootT, KneeHint, Effector, false, 1.0, 1.0);
		Changed.Add(FBoneTransform(Thigh, ThighT));
		Changed.Add(FBoneTransform(Calf, CalfT));
		Changed.Add(FBoneTransform(Foot, FootT));
		CS.SafeSetCSBoneTransforms(Changed);
		Changed.Reset();
	}
	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Output.Pose);
}

void FCampusAnimInstanceProxy::ApplyProneYaw(FPoseContext& Output) const
{
	if (FMath::Abs(ProneYaw) < 0.1f)
	{
		return;
	}
	const FCompactPoseBoneIndex Root(0);
	Output.Pose[Root] = Output.Pose[Root] * FTransform(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(ProneYaw)));
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
	DynClips.SetNum(CampusDyn::Num);
	for (int32 Index = 0; Index < CampusDyn::Num; ++Index)
	{
		ConstructorHelpers::FObjectFinder<UAnimSequence> Finder(CampusDyn::Defs[Index].Path);
		DynClips[Index] = Finder.Object;
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
	// 0-2: mantle (stand / walk / run), 3-5: climb, 6-8: hurdle, 9-11: vault (hand on top, drop beyond).
	if (Kind >= 9)
	{
		return DynClip(CampusDyn::Vault_Stand + FMath::Clamp(Kind - 9, 0, 2));
	}
	if (Kind >= 6)
	{
		return DynClip(CampusDyn::Hurdle_Stand + FMath::Clamp(Kind - 6, 0, 2));
	}
	return Kind >= 3 ? Clip(CampusClips::ClimbStand + FMath::Clamp(Kind - 3, 0, 2)) : Clip(CampusClips::MantleStand + FMath::Clamp(Kind, 0, 2));
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
	const bool bInAir = Movement->IsFalling() || (Campus && Campus->IsZipping());

	FVector Velocity = Character->GetVelocity();
	const float VerticalSpeed = Velocity.Z;
	Velocity.Z = 0.f;
	const float Speed = Velocity.Size();
	const float Direction = Speed > 5.f ? FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Character->GetActorRotation().Yaw) : 0.f;

	auto Approach = [DeltaSeconds](float& Value, float Target, float BlendTime)
	{
		Value = FMath::FInterpConstantTo(Value, Target, DeltaSeconds, 1.f / BlendTime);
	};
	// Prone rides on the crouch capsule, which shrinks the instant prone starts: freeze the stand / crouch blend while
	// prone (and while getting down / up) so a crouch dip never mixes into the get-down clip.
	if (!(Campus && Campus->IsProne()) && ProneTransElapsed < 0.f)
	{
		Approach(CrouchWeight, Movement->IsCrouching() ? 1.f : 0.f, 0.2f);
	}
	// bent-over crouch run: grows with speed past the crouch walk (2 m/s) toward the crouch run (3.3 m/s)
	{
		const float RunShare = FMath::GetMappedRangeValueClamped(FVector2f(210.f, 300.f), FVector2f(0.f, 1.f), Velocity.Size2D());
		// (speed tells it apart from the crouch walk, so remote players show it too: bRunHeld is not replicated)
		Approach(CrouchRunWeight, bArmed && Movement->IsCrouching() && !(Campus && Campus->IsProne()) ? RunShare : 0.f, 0.15f);
	}
	Approach(RifleWeight, bArmed ? 1.f : 0.f, 0.2f);
	Approach(ProneWeight, Campus && Campus->IsProne() ? 1.f : 0.f, 0.45f);
	// The drop, from the pawn's replicated dive byte (ACampusCharacter::SetDivePose) - NOT from the
	// drop actor, which only ever reaches the diver's own phone. Both weights ease, so leaving the
	// plane is a move into the star and the canopy opening crossfades the body upright.
	Approach(FreefallWeight, Campus && Campus->IsFreefalling() ? 1.f : 0.f, 0.25f);
	Approach(ChuteWeight, Campus && Campus->IsUnderChute() ? 1.f : 0.f, 0.25f);
	Proxy.FreefallAlpha = FreefallWeight;
	Proxy.ChuteAlpha = ChuteWeight;
	const float DiveShare = FMath::Clamp(FreefallWeight + ChuteWeight, 0.f, 1.f);
	Proxy.ProneAlpha = 0.f; // the old tipped-crouch placeholder; real prone clips below
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
	else if (bWasInAir && AirTime > 0.3f && AirWeight > 0.5f)
	{
		LandWeight = 1.f;
		LandTime = 0.f;
		LandKind = CampusClips::JumpKindForSpeed(Speed);
	}
	// Stepping on or off a step (plinth / pyramid steps, kerbs) leaves the floor for a few frames: that is not a
	// fall, so the air pose only shows for a real jump, the zipline, or a drop that lasts or speeds up.
	const bool bShowAir = bInAir && (bJumped || (Campus && Campus->IsZipping()) || AirTime > 0.2f || VerticalSpeed < -350.f);
	Approach(AirWeight, bShowAir ? 1.f : 0.f, 0.12f);
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
	UpdateDynamic(DeltaSeconds, Campus, Movement, Speed, Direction, Samples);

	// ---- Prone: lying idle + crawl forward / backward (Mixamo clips retargeted onto the mannequin) and the
	// sample's face-down get-up (M_ragdoll_getup_stand_F, measured: lying until ~1.0 s, kneeling ~1.9 s,
	// standing ~2.8 s, stays in place), played backwards to go down and forwards to get up.
	float ProneShare = 0.f;
	{
		// LieTime 0.5: the get-up's frame that matches the Mixamo lying idle (measured: pelvis 11 cm, body flat, facing +Y);
		// at 1.0 the get-up is already pushing up (pelvis 28 cm), which made the body drop and turn at the crossfade
		constexpr float LieTime = 0.5f, KneelTime = 1.9f, StandTime = 2.8f, TransRate = 2.0f, BlendOut = 0.3f;
		const bool bProneNow = Campus && Campus->IsProne();
		if (bProneNow != bWasProne)
		{
			bProneTransDown = bProneNow;
			// going down from where we were (standing or kneeling); getting up to where we end (crouched or not)
			ProneTransFrom = bProneNow ? (bWasCrouched ? KneelTime : StandTime) : LieTime;
			ProneTransTo = bProneNow ? LieTime : (Movement->IsCrouching() ? KneelTime : StandTime);
			ProneTransDuration = FMath::Abs(ProneTransTo - ProneTransFrom) / TransRate;
			ProneTransElapsed = 0.f;
			if (bProneNow)
			{
				ProneYawState = 0.f; // a new get-down starts square to the body, not with the last crawl's turn
			}
		}
		bWasProne = bProneNow;
		bWasCrouched = Movement->IsCrouching() && !bProneNow;

		float TransShare = 0.f;
		float TransFadeOut = 1.f;
		float TransFadeIn = 1.f;
		if (ProneTransElapsed >= 0.f)
		{
			ProneTransElapsed += DeltaSeconds;
			if (ProneTransElapsed >= ProneTransDuration)
			{
				ProneTransElapsed = -1.f;
			}
			else
			{
				const float Alpha = ProneTransElapsed / FMath::Max(ProneTransDuration, 0.01f);
				// quick blend in, then a crossfade into the lying pose (down) or back to locomotion (up)
				TransFadeOut = FMath::Min(1.f, (ProneTransDuration - ProneTransElapsed) / BlendOut);
				TransFadeIn = FMath::Min(1.f, ProneTransElapsed / 0.1f);
				TransShare = TransFadeIn * TransFadeOut;
				if (const UAnimSequence* GetUp = Clip(CampusClips::ProneGetUp))
				{
					Samples.Add({ GetUp, FMath::Lerp(ProneTransFrom, ProneTransTo, Alpha), 0.f, false });
				}
			}
		}
		// the lying / crawling body shows once a down transition has (nearly) finished, until an up starts
		const bool bDownPlaying = ProneTransElapsed >= 0.f && bProneTransDown;
		// going down, the lying body only takes over as the get-down fades out at its end (0 at the start: the old
		// 1 - TransShare was ~1 on the first frames, a snap to the floor and back before the get-down played)
		const bool bUpPlaying = ProneTransElapsed >= 0.f && !bProneTransDown;
		const float LyingShare = bProneNow ? (bDownPlaying ? 1.f - TransFadeOut : 1.f) : (bUpPlaying ? 1.f - TransFadeIn : 0.f);
		ProneShare = FMath::Clamp(LyingShare + TransShare, 0.f, 1.f);

		if (ProneShare > 0.f)
		{
			// everything else (stand / crouch locomotion, turn in place) gives way
			const int32 Last = Samples.Num() - (TransShare > 0.f ? 1 : 0);
			for (int32 Index = 0; Index < Last; ++Index)
			{
				Samples[Index].Weight *= 1.f - ProneShare;
			}
			if (TransShare > 0.f)
			{
				Samples.Last().Weight = TransShare;
			}
			Proxy.RootYaw *= 1.f - ProneShare;
		}

		if (LyingShare > 0.f)
		{
			// sideways crawl: turn the whole body toward the movement (up to 90 deg), backward clip for behind
			const float MoveAlpha = FMath::GetMappedRangeValueClamped(FVector2f(5.f, 25.f), FVector2f(0.f, 1.f), Speed);
			const bool bBackward = FMath::Abs(Direction) > 100.f;
			float YawTarget = bBackward ? FRotator::NormalizeAxis(Direction - 180.f) : Direction;
			YawTarget = FMath::Clamp(YawTarget, -90.f, 90.f) * MoveAlpha;
			ProneYawState = FMath::FInterpTo(ProneYawState, YawTarget, DeltaSeconds, MoveAlpha > 0.5f ? 6.f : 2.f);

			const UAnimSequence* Idle = Clip(CampusClips::ProneIdle);
			const UAnimSequence* Fwd = Clip(CampusClips::ProneForward);
			const UAnimSequence* Back = Clip(CampusClips::ProneBackward);
			if (Idle)
			{
				Samples.Add({ Idle, FMath::Max(0.f, Idle->GetPlayLength() - 0.02f), LyingShare * (1.f - MoveAlpha), false, FVector3f::ZeroVector, true });
			}
			// play rate = real speed / the clip's floor-sweep speed, so hands and knees stay planted
			if (Fwd && !bBackward)
			{
				const float Rate = FMath::Clamp(Speed / CampusClips::Defs[CampusClips::ProneForward].RefSpeed, 0.3f, 2.5f);
				ProneForwardPhase = FMath::Fmod(ProneForwardPhase + DeltaSeconds * Rate / Fwd->GetPlayLength(), 1.f);
				Samples.Add({ Fwd, ProneForwardPhase * Fwd->GetPlayLength(), LyingShare * MoveAlpha, true, FVector3f::ZeroVector, true });
			}
			if (Back && bBackward)
			{
				const float Rate = FMath::Clamp(Speed / CampusClips::Defs[CampusClips::ProneBackward].RefSpeed, 0.3f, 2.5f);
				ProneBackwardPhase = FMath::Fmod(ProneBackwardPhase + DeltaSeconds * Rate / Back->GetPlayLength(), 1.f);
				Samples.Add({ Back, ProneBackwardPhase * Back->GetPlayLength(), LyingShare * MoveAlpha, true, FVector3f::ZeroVector, true });
			}
		}
		else
		{
			ProneYawState = FMath::FInterpTo(ProneYawState, 0.f, DeltaSeconds, 6.f);
		}
		Proxy.ProneYaw = ProneYawState * LyingShare;
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
		if (const UAnimSequence* Mantle = GetMantleClip(LastTraversalKind))
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
	// Sets without rifle clips (crouch, air, landing) hold the rifle with the upper body only - but not
	// mid-drop: a rifle idle on the upper body fights the spread arms and wins (it replaces spine_01
	// and everything above it).
	const float HoldRifle = RifleWeight * FMath::Max3(CrouchWeight, AirWeight, Land) * (1.f - ProneShare) * (1.f - DiveShare);
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

	// Own aim must never lag the crosshair. A remote player's pitch, though, only changes when their ServerMove
	// arrives (on the host, via the server-side ControlRotation) or their RemoteViewPitch replicates (on a client) -
	// and recoil sweeps it ~0.9 deg per shot at ten shots a second. Applied raw it stair-stepped the whole spine
	// chain, which is the "enemy jerks while shooting" everyone was seeing. Ease it over ~50 ms instead.
	// (Not the Approach lambda above: that is a constant degrees/second rate, wrong for a +-70 deg axis.)
	const float TargetAimPitch = FMath::Clamp(FRotator::NormalizeAxis(Character->GetBaseAimRotation().Pitch), -70.f, 70.f);
	if (Character->IsLocallyControlled() || FMath::Abs(TargetAimPitch - AimPitchSmoothed) > 45.f)
	{
		AimPitchSmoothed = TargetAimPitch; // ours, or a respawn / possession jump: take it, do not sweep through it
	}
	else
	{
		AimPitchSmoothed += (TargetAimPitch - AimPitchSmoothed) * (1.f - FMath::Exp(-DeltaSeconds / 0.05f));
	}
	Proxy.AimPitch = AimPitchSmoothed;
	// Not while diving: aim pitch bends the whole spine chain, and the drop's own arch owns the spine.
	Proxy.AimPitchAlpha = RifleWeight * (1.f - UpperAlpha) * (1.f - ProneShare) * (1.f - DiveShare);
	Proxy.CrouchRunBend = CrouchRunWeight * CrouchWeight;

	// Foot IK: trace under each foot onto the real steps. The stair ramps (smooth capsule movement) are on their
	// own collision that Visibility traces ignore, so these land on the treads; on plinth / pyramid steps the
	// capsule perches on an edge and the other foot finds the step below.
	{
		int32 IKTraversalKind = 0;
		float IKTraversalTime = 0.f;
		const bool bIKTraversing = Campus && Campus->GetTraversal(IKTraversalKind, IKTraversalTime);
		const bool bGrounded = Movement->IsMovingOnGround() && !bIKTraversing && !(Campus && (Campus->IsProne() || Campus->IsSliding() || Campus->IsZipping()));
		Approach(FootIKWeight, bGrounded ? 1.f : 0.f, 0.12f);
		const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
		const float Scale = Mesh ? FMath::Max(Mesh->GetComponentScale().Z, 0.01f) : 1.f;
		const float Floor = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		static const FName FootBones[2] = { TEXT("foot_l"), TEXT("foot_r") };
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusFootIK), false, Character);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			float Target = 0.f;
			if (Mesh && bGrounded)
			{
				const FVector At = Mesh->GetSocketLocation(FootBones[Side]);
				FHitResult Hit;
				if (GetWorld()->LineTraceSingleByChannel(Hit, FVector(At.X, At.Y, Floor + 50.f), FVector(At.X, At.Y, Floor - 60.f), ECC_Visibility, Params)
					&& Hit.ImpactNormal.Z > 0.6f)
				{
					Target = FMath::Clamp(Hit.ImpactPoint.Z - Floor, -55.f, 45.f) / Scale;
				}
			}
			FootTrace[Side] = FMath::FInterpTo(FootTrace[Side], Target, DeltaSeconds, 18.f);
		}
		PelvisTrace = FMath::FInterpTo(PelvisTrace, FMath::Min3(FootTrace[0], FootTrace[1], 0.f), DeltaSeconds, 12.f);
		Proxy.FootOffset[0] = FootTrace[0];
		Proxy.FootOffset[1] = FootTrace[1];
		Proxy.PelvisOffset = PelvisTrace;
		Proxy.FootIKAlpha = FootIKWeight;
	}
}
