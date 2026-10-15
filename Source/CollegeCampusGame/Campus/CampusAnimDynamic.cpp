// Game Animation Sample transitions layered over the Campus loops: starts and stops (distance matched),
// pivots, sprint turns, turn in place, idle breaks, run lean and the slide.

#include "Campus/CampusAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Campus/CampusCharacter.h"
#include "Campus/CampusDynamicClips.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace CampusDynTuning
{
	// Eight directions clockwise from forward, matching the clip order in CampusDynamicClips.h.
	constexpr int32 NumDirs = 8;
	constexpr float BlendIn = 0.12f;
	constexpr float EndFade = 0.25f;
	// Above this the walk clips hand over to the run clips.
	constexpr float RunGait = 260.f;
	constexpr float SprintGait = 560.f;
	constexpr float TurnInPlaceAbove = 75.f;
	constexpr float PivotAngle = 130.f;
}

namespace
{
	float CurveAt(const float* Curve, int32 Keys, float Length, float Time)
	{
		if (Keys < 2 || Length <= 0.f)
		{
			return Keys > 0 ? Curve[0] : 0.f;
		}
		const float Position = FMath::Clamp(Time / Length, 0.f, 1.f) * (Keys - 1);
		const int32 Index = FMath::Min(FMath::FloorToInt(Position), Keys - 2);
		return FMath::Lerp(Curve[Index], Curve[Index + 1], Position - Index);
	}

	/** First time the clip's root has travelled Distance (the curve only grows). */
	float TimeAtDistance(const CampusDyn::FDef& Def, float Distance)
	{
		const float* Dist = Def.Dist;
		if (Distance <= Dist[0])
		{
			return 0.f;
		}
		for (int32 Index = 1; Index < Def.Keys; ++Index)
		{
			if (Dist[Index] >= Distance)
			{
				const float Span = Dist[Index] - Dist[Index - 1];
				const float Alpha = Span > UE_KINDA_SMALL_NUMBER ? (Distance - Dist[Index - 1]) / Span : 0.f;
				return (Index - 1 + Alpha) * Def.Length / (Def.Keys - 1);
			}
		}
		return Def.Length;
	}

	float TotalDistance(const CampusDyn::FDef& Def)
	{
		return Def.Dist[Def.Keys - 1];
	}

	int32 DirIndex(float DirectionDeg)
	{
		const float Clockwise = DirectionDeg < 0.f ? DirectionDeg + 360.f : DirectionDeg;
		return FMath::RoundToInt(Clockwise / 45.f) % CampusDynTuning::NumDirs;
	}
}

const UAnimSequence* UCampusAnimInstance::DynClip(int32 Index) const
{
	return DynClips.IsValidIndex(Index) ? DynClips[Index].Get() : nullptr;
}

void UCampusAnimInstance::BeginDyn(EDyn State, int32 Index, float Time)
{
	if (DynState != EDyn::None && DynWeight > 0.01f)
	{
		EndDyn(0.15f);
	}
	DynState = State;
	DynIndex = Index;
	DynTime = FMath::Max(Time, CampusDyn::Defs[Index].Begin);
	DynDistance = 0.f;
	DynWeight = 0.f;
}

void UCampusAnimInstance::EndDyn(float FadeOut)
{
	if (DynState != EDyn::None && DynIndex != INDEX_NONE && DynWeight > 0.01f)
	{
		FadeIndex = DynIndex;
		FadeTime = DynTime;
		FadeWeight = DynWeight;
		FadeRate = 1.f / FMath::Max(FadeOut, 0.05f);
	}
	DynState = EDyn::None;
	DynIndex = INDEX_NONE;
	DynWeight = 0.f;
}

void UCampusAnimInstance::UpdateDynamic(float DeltaSeconds, const ACampusCharacter* Campus, const UCharacterMovementComponent* Movement,
	float Speed, float DirectionDeg, TArray<FCampusAnimSample>& Samples)
{
	using namespace CampusDyn;
	using namespace CampusDynTuning;
	FCampusAnimInstanceProxy& Proxy = GetProxyOnGameThread<FCampusAnimInstanceProxy>();
	Proxy.RootYaw = 0.f;
	Proxy.LeanBase = FCampusAnimSample();
	Proxy.LeanSide = FCampusAnimSample();
	if (!Campus || DeltaSeconds <= 0.f)
	{
		return;
	}

	FVector Velocity = Campus->GetVelocity();
	Velocity.Z = 0.f;
	const float ActorYaw = Campus->GetActorRotation().Yaw;
	const bool bSimulated = Campus->GetLocalRole() == ROLE_SimulatedProxy;
	// Remote copies don't know the input, so they read it from the velocity change.
	FVector Accel = bSimulated ? (Velocity - PrevVelocity) / DeltaSeconds : Movement->GetCurrentAcceleration();
	Accel.Z = 0.f;
	const bool bInput = bSimulated ? Speed > 20.f && Speed >= PrevSpeed - 250.f * DeltaSeconds : Accel.SizeSquared() > 1.f;

	int32 TraversalKind = 0;
	float TraversalTime = 0.f;
	const bool bTraversing = Campus->GetTraversal(TraversalKind, TraversalTime);
	const bool bGround = Movement->IsMovingOnGround() && !bTraversing;
	const bool bSliding = Campus->IsSliding();
	const bool bAllowed = bGround && !Campus->IsArmed() && RifleWeight < 0.05f && !Movement->IsCrouching() && !Campus->IsProne()
		&& UpperBodyClip == INDEX_NONE;

	// Velocity yaw rate drives the lean and the sprint turns.
	float YawRate = 0.f;
	if (Speed > 100.f && PrevSpeed > 100.f)
	{
		// a near-180 flip is a reversal (running backwards), not a turn: reading it as 5000 deg/s pinned the lean
		// at full to a random side. Real turns are capped so a hard corner can't over-bank the body either.
		const float Delta = FRotator::NormalizeAxis(Velocity.Rotation().Yaw - PrevVelocity.Rotation().Yaw);
		if (FMath::Abs(Delta) < 120.f)
		{
			YawRate = FMath::Clamp(Delta / DeltaSeconds, -400.f, 400.f);
		}
	}
	VelocityYawRate = FMath::FInterpTo(VelocityYawRate, YawRate, DeltaSeconds, 10.f);
	PivotCooldown = FMath::Max(0.f, PivotCooldown - DeltaSeconds);

	// Run lean: real lean poses from the sample, scaled by how hard we turn.
	{
		const float Moving = FMath::GetMappedRangeValueClamped(FVector2f(200.f, 450.f), FVector2f(0.f, 1.f), Speed);
		const float Target = bGround && !Movement->IsCrouching() && !bSliding ? FMath::Clamp(VelocityYawRate / 400.f, -0.45f, 0.45f) * Moving : 0.f;
		// Ease in slower than out: a quick flick of the stick shouldn't throw the body over, but it straightens fast.
		LeanAlpha = FMath::FInterpTo(LeanAlpha, Target, DeltaSeconds, FMath::Abs(Target) > FMath::Abs(LeanAlpha) ? 4.f : 7.f);
		if (FMath::Abs(LeanAlpha) > 0.01f)
		{
			const int32 Base = Lean_Run_Base;
			const int32 Side = LeanAlpha > 0.f ? Lean_Run_R : Lean_Run_L;
			if (DynClip(Base) && DynClip(Side))
			{
				Proxy.LeanBase = { DynClip(Base), 0.f, 1.f, false };
				Proxy.LeanSide = { DynClip(Side), 0.f, FMath::Abs(LeanAlpha), false };
			}
		}
	}

	auto Gait = [&](int32 RunFirst, int32 WalkFirst, int32 Dir)
	{
		return (Speed > RunGait || Movement->GetMaxSpeed() > RunGait + 100.f ? RunFirst : WalkFirst) + Dir;
	};

	// --- Transitions ------------------------------------------------------------------------------
	if (!bAllowed)
	{
		if (DynState != EDyn::None)
		{
			EndDyn(0.15f);
		}
	}
	else
	{
		const bool bFree = DynState == EDyn::None || DynState == EDyn::Turn || DynState == EDyn::IdleBreak;
		if (PrevSpeed < 20.f && Speed >= 20.f && bInput && bFree)
		{
			const int32 Dir = DirIndex(DirectionDeg);
			const bool bSprint = Movement->GetMaxSpeed() > SprintGait && Dir == 0;
			BeginDyn(EDyn::Start, bSprint ? int32(Sprint_Start) : Gait(Run_Start_F, Walk_Start_F, Dir));
			DynStartDirection = DirectionDeg;
		}
		else if (!bInput && Speed > 120.f && (DynState == EDyn::None || DynState == EDyn::Start || (DynState == EDyn::Pivot && DynTime > 0.3f)))
		{
			const int32 Dir = DirIndex(DirectionDeg);
			const int32 Index = Speed > SprintGait && Dir == 0 ? int32(Sprint_Stop) : (Speed > RunGait ? Run_Stop_F + Dir : Walk_Stop_F + Dir);
			BeginDyn(EDyn::Stop, Index);
		}
		else if (!bSimulated && bInput && Speed > 200.f && PivotCooldown <= 0.f && (DynState == EDyn::None || DynState == EDyn::Start)
			&& FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Accel.GetSafeNormal(), Velocity.GetSafeNormal()), -1.f, 1.f))) > PivotAngle)
		{
			const float AccelDirection = FRotator::NormalizeAxis(Accel.Rotation().Yaw - ActorYaw);
			BeginDyn(EDyn::Pivot, Gait(Run_Pivot_F, Walk_Pivot_F, DirIndex(AccelDirection)));
			PivotCooldown = 0.5f;
		}

		// Sprint turn clips are off: the body already faces the camera every frame, and on top of that the
		// clips' head-first turn looked like the head being dragged round. The lean carries the turn instead.
		if (false && Speed > SprintGait && bInput)
		{
			SprintTurnAccum = SprintTurnAccum * FMath::Exp(-DeltaSeconds / 0.3f) + YawRate * DeltaSeconds;
			if (FMath::Abs(SprintTurnAccum) > 70.f && DynState == EDyn::None)
			{
				const bool bRight = SprintTurnAccum > 0.f;
				const bool bBig = FMath::Abs(SprintTurnAccum) > 135.f;
				BeginDyn(EDyn::SprintTurn, bBig ? (bRight ? Sprint_Turn_R180 : Sprint_Turn_L180) : (bRight ? Sprint_Turn_R90 : Sprint_Turn_L90));
				SprintTurnAccum = 0.f;
			}
		}
		else
		{
			SprintTurnAccum = 0.f;
		}
	}

	// --- Turn in place: the feet hold their yaw while the camera turns, then step around ---------------
	const bool bIdleTurn = bAllowed && Speed < 5.f && (DynState == EDyn::None || DynState == EDyn::Turn || DynState == EDyn::IdleBreak);
	if (bIdleTurn)
	{
		if (!bFeetYawValid)
		{
			FeetYaw = ActorYaw;
			bFeetYawValid = true;
		}
		if (DynState == EDyn::Turn)
		{
			const FDef& Def = Defs[DynIndex];
			const float End = FMath::Max(FMath::Abs(Def.Yaw[Def.Keys - 1]), 1.f);
			const float Progress = FMath::Clamp(FMath::Abs(CurveAt(Def.Yaw, Def.Keys, Def.Length, DynTime)) / End, 0.f, 1.f);
			FeetYaw = TurnFeetStart + Progress * FRotator::NormalizeAxis(ActorYaw - TurnFeetStart);
		}
		float Offset = FRotator::NormalizeAxis(FeetYaw - ActorYaw);
		if (FMath::Abs(Offset) > 170.f)
		{
			Offset = FMath::Sign(Offset) * 170.f;
			FeetYaw = ActorYaw + Offset;
		}
		if (DynState != EDyn::Turn && FMath::Abs(Offset) > TurnInPlaceAbove)
		{
			// Feet behind the body to the left means the body turned right.
			const bool bRight = Offset < 0.f;
			const float Angle = FMath::Abs(Offset);
			const int32 Step = Angle < 112.f ? 1 : (Angle < 157.f ? 2 : 3);
			BeginDyn(EDyn::Turn, (bRight ? Turn_R045 : Turn_L045) + Step * 2);
			TurnFeetStart = FeetYaw;
		}
		RootYawOffset = Offset;
	}
	else
	{
		RootYawOffset = FMath::FInterpTo(RootYawOffset, 0.f, DeltaSeconds, 10.f);
		FeetYaw = ActorYaw + RootYawOffset;
	}
	// the turn-in-place twist belongs to standing still: gone by the time the run clips have full weight (else the
	// chest and head keep facing the old way for ~0.3 s after running off)
	Proxy.RootYaw = RootYawOffset * (1.f - FMath::GetMappedRangeValueClamped(FVector2f(5.f, 80.f), FVector2f(0.f, 1.f), Speed));

	// --- Idle breaks ------------------------------------------------------------------------------
	if (bAllowed && Speed < 5.f && DynState == EDyn::None && FMath::Abs(RootYawOffset) < 10.f)
	{
		StillTime += DeltaSeconds;
		if (StillTime > NextIdleBreak)
		{
			BeginDyn(EDyn::IdleBreak, IdleBreak_1 + FMath::RandRange(0, 5));
			StillTime = 0.f;
			NextIdleBreak = FMath::FRandRange(9.f, 16.f);
		}
	}
	else if (DynState != EDyn::IdleBreak)
	{
		StillTime = 0.f;
	}
	if (DynState == EDyn::IdleBreak && Speed > 20.f)
	{
		EndDyn(0.15f);
	}

	// --- Advance the active clip -------------------------------------------------------------------
	if (DynState != EDyn::None)
	{
		const FDef& Def = Defs[DynIndex];
		switch (DynState)
		{
		case EDyn::Start:
		{
			// Distance matching: show the frame where the clip has travelled as far as we have.
			DynDistance += Speed * DeltaSeconds;
			const float Target = TimeAtDistance(Def, DynDistance);
			DynTime = FMath::Clamp(Target, DynTime + DeltaSeconds * 0.6f, DynTime + DeltaSeconds * 2.f);
			if (!bInput || FMath::Abs(FRotator::NormalizeAxis(DirectionDeg - DynStartDirection)) > 50.f)
			{
				EndDyn(0.2f);
			}
			break;
		}
		case EDyn::Stop:
		{
			if (bInput)
			{
				EndDyn(0.15f);
				break;
			}
			// Distance matching on the predicted stopping distance (linear braking, see the character).
			const float Braking = FMath::Max(Movement->GetMaxBrakingDeceleration(), 1.f);
			const float Remaining = Speed * Speed / (2.f * Braking);
			const float Target = Speed > 5.f ? TimeAtDistance(Def, FMath::Max(0.f, TotalDistance(Def) - Remaining)) : DynTime + DeltaSeconds;
			// First frame: jump straight to the matching moment of the stop.
			DynTime = DynWeight <= 0.f ? Target : FMath::Clamp(Target, DynTime + DeltaSeconds * 0.5f, DynTime + DeltaSeconds * 2.5f);
			break;
		}
		default:
			DynTime += DeltaSeconds;
			break;
		}
		if (DynState != EDyn::None)
		{
			// a pivot (running backwards) only flavours the body: its clip runs on a fixed clock, longer than the
			// capsule's real turn, so at full weight it dragged the torso / head along on its own timing
			DynWeight = FMath::Min(DynState == EDyn::Pivot ? 0.6f : 1.f, DynWeight + DeltaSeconds / BlendIn);
			if (DynTime >= Def.End - EndFade)
			{
				EndDyn(EndFade);
			}
		}
	}
	if (FadeIndex != INDEX_NONE)
	{
		FadeWeight -= DeltaSeconds * FadeRate;
		FadeTime += DeltaSeconds;
		if (FadeWeight <= 0.f)
		{
			FadeIndex = INDEX_NONE;
		}
	}

	// --- Slide: into, loop while sliding, then the matching way out ---------------------------------
	if (bSliding)
	{
		if (!bWasSliding)
		{
			SlideTime = 0.f;
			SlideOutIndex = INDEX_NONE;
			EndDyn(0.1f);
		}
		SlideTime += DeltaSeconds;
		SlideWeight = FMath::Min(1.f, SlideWeight + DeltaSeconds / 0.1f);
	}
	else if (bWasSliding)
	{
		SlideOutIndex = Speed > 300.f ? int32(Slide_Out_Run) : (Movement->IsCrouching() ? int32(Slide_Out_Crouch) : int32(Slide_Out_Stand));
		SlideOutTime = 0.f;
	}
	bWasSliding = bSliding;

	// --- Write the samples ------------------------------------------------------------------------
	TArray<FCampusAnimSample, TInlineAllocator<3>> Extra;
	if (DynState != EDyn::None && DynClip(DynIndex))
	{
		Extra.Add({ DynClip(DynIndex), FMath::Min(DynTime, Defs[DynIndex].Length), DynWeight, false });
	}
	if (FadeIndex != INDEX_NONE && DynClip(FadeIndex))
	{
		Extra.Add({ DynClip(FadeIndex), FMath::Min(FadeTime, Defs[FadeIndex].Length), FadeWeight, false });
	}
	if (bSliding)
	{
		// The sample's slide-in starts with a run-up: play only the drop into the slide, then the loop.
		const UAnimSequence* Into = DynClip(Slide_Into);
		const float IntoLength = Defs[Slide_Into].End - Defs[Slide_Into].Begin;
		if (Into && SlideTime < IntoLength)
		{
			Extra.Add({ Into, Defs[Slide_Into].Begin + SlideTime, SlideWeight, false });
		}
		else if (const UAnimSequence* Loop = DynClip(Slide_Loop))
		{
			Extra.Add({ Loop, FMath::Fmod(SlideTime - IntoLength, Loop->GetPlayLength()), SlideWeight, true });
		}
	}
	else if (SlideOutIndex != INDEX_NONE)
	{
		SlideOutTime += DeltaSeconds;
		const UAnimSequence* Out = DynClip(SlideOutIndex);
		const float Length = Out ? Defs[SlideOutIndex].End : 0.f;
		if (!Out || SlideOutTime >= Length || !bGround)
		{
			SlideOutIndex = INDEX_NONE;
			SlideWeight = 0.f;
		}
		else
		{
			SlideWeight = FMath::Clamp((Length - SlideOutTime) / 0.3f, 0.f, 1.f);
			Extra.Add({ Out, SlideOutTime, SlideWeight, false });
		}
	}

	float ExtraWeight = 0.f;
	for (const FCampusAnimSample& Sample : Extra)
	{
		ExtraWeight += Sample.Weight;
	}
	if (ExtraWeight > UE_KINDA_SMALL_NUMBER)
	{
		const float Scale = ExtraWeight > 1.f ? 1.f / ExtraWeight : 1.f;
		const float Keep = 1.f - FMath::Min(ExtraWeight, 1.f);
		for (FCampusAnimSample& Sample : Samples)
		{
			Sample.Weight *= Keep;
		}
		for (FCampusAnimSample& Sample : Extra)
		{
			Sample.Weight *= Scale;
			Samples.Add(Sample);
		}
	}

	PrevVelocity = Velocity;
	PrevSpeed = Speed;
}

// ---------------------------------------------------------------------------------------------
// Proxy side

void FCampusAnimInstanceProxy::ApplyLean(FPoseContext& Output)
{
	if (!LeanBase.Sequence || !LeanSide.Sequence || LeanSide.Weight <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	const int32 NumBones = Output.Pose.GetNumBones();
	if (LeanMask.Num() != NumBones)
	{
		// The arms keep their pose (rifle hold, aiming); everything else takes the lean.
		static const TCHAR* const Skip[] = { TEXT("clavicle"), TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand"), TEXT("thumb"), TEXT("index"),
			TEXT("middle"), TEXT("ring"), TEXT("pinky"), TEXT("ik_"), TEXT("weapon"), TEXT("root") };
		// (neck / head take the lean too: the clip's own keys hold the head level while the torso banks; skipping
		// them left the head riding the banked spine uncorrected, so on hard turns the head swung / "fell" first)
		LeanMask.SetNumZeroed(NumBones);
		const FReferenceSkeleton& RefSkeleton = Bones.GetReferenceSkeleton();
		for (const FCompactPoseBoneIndex& Index : Output.Pose.ForEachBoneIndex())
		{
			const FString Name = RefSkeleton.GetBoneName(Bones.MakeMeshPoseIndex(Index).GetInt()).ToString();
			bool bTake = true;
			for (const TCHAR* Part : Skip)
			{
				bTake &= !Name.Contains(Part);
			}
			LeanMask[Index.GetInt()] = bTake;
		}
	}

	// Component-space rotation of a bone, walking its parent chain.
	auto ComponentRotation = [&Output, &Bones](FCompactPoseBoneIndex Index)
	{
		FQuat Rotation = FQuat::Identity;
		for (; Index.IsValid(); Index = Bones.GetParentBoneIndex(Index))
		{
			Rotation = Output.Pose[Index].GetRotation() * Rotation;
		}
		return Rotation;
	};
	auto FindBone = [&Bones](const TCHAR* Name)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return MeshIndex != INDEX_NONE ? Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex)) : FCompactPoseBoneIndex(INDEX_NONE);
	};
	const FCompactPoseBoneIndex Head = FindBone(TEXT("head"));
	const FQuat HeadBefore = Head.IsValid() ? ComponentRotation(Head) : FQuat::Identity;

	FPoseContext BasePose(this);
	FPoseContext SidePose(this);
	SamplePose(LeanBase, BasePose);
	SamplePose(LeanSide, SidePose);
	const float Weight = LeanSide.Weight;
	for (const FCompactPoseBoneIndex& Index : Output.Pose.ForEachBoneIndex())
	{
		if (!LeanMask[Index.GetInt()])
		{
			continue;
		}
		const FTransform& Base = BasePose.Pose[Index];
		const FTransform& Side = SidePose.Pose[Index];
		FTransform& Bone = Output.Pose[Index];
		const FQuat Delta = FQuat::Slerp(FQuat::Identity, Side.GetRotation() * Base.GetRotation().Inverse(), Weight);
		Bone.SetRotation((Delta * Bone.GetRotation()).GetNormalized());
		// Rotation only: the clip's pelvis translation shoved the hips sideways out from under the capsule, which
		// read as the character being dragged toward the turn.
	}

	// Keep the eyes level: runners bank the torso into a turn but hold the head upright. Without this the head
	// rides the banked spine and looks like it is falling over. Most of the correction goes on the neck, the rest
	// on the head, so it bends instead of snapping at one joint.
	if (Head.IsValid())
	{
		const FQuat Correction = FQuat::Slerp(FQuat::Identity, HeadBefore * ComponentRotation(Head).Inverse(), 0.85f);
		const FCompactPoseBoneIndex Neck = Bones.GetParentBoneIndex(Head);
		const TPair<FCompactPoseBoneIndex, float> Joints[] = { { Neck, 0.6f }, { Head, 1.f } };
		FQuat Applied = FQuat::Identity;
		for (const TPair<FCompactPoseBoneIndex, float>& Joint : Joints)
		{
			if (!Joint.Key.IsValid())
			{
				continue;
			}
			// Remaining component-space correction, re-expressed in this joint's parent space.
			const FQuat Step = FQuat::Slerp(FQuat::Identity, Correction * Applied.Inverse(), Joint.Value);
			const FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Joint.Key);
			const FQuat ParentRotation = Parent.IsValid() ? ComponentRotation(Parent) : FQuat::Identity;
			FTransform& Bone = Output.Pose[Joint.Key];
			Bone.SetRotation((ParentRotation.Inverse() * Step * ParentRotation * Bone.GetRotation()).GetNormalized());
			Applied = Step * Applied;
		}
	}
}

void FCampusAnimInstanceProxy::ApplyRootYaw(FPoseContext& Output) const
{
	if (FMath::Abs(RootYaw) < 0.1f)
	{
		return;
	}
	// Turn the spine back by the same amount first, so the chest and arms keep facing the camera.
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
	if (!Chain.IsEmpty())
	{
		const float RadiansPerBone = FMath::DegreesToRadians(-RootYaw / Chain.Num());
		for (const FCompactPoseBoneIndex& Index : Chain)
		{
			FQuat ParentRotation = FQuat::Identity;
			for (FCompactPoseBoneIndex Parent = Bones.GetParentBoneIndex(Index); Parent.IsValid(); Parent = Bones.GetParentBoneIndex(Parent))
			{
				ParentRotation = Output.Pose[Parent].GetRotation() * ParentRotation;
			}
			const FVector LocalAxis = ParentRotation.Inverse().RotateVector(FVector::ZAxisVector);
			FTransform& Bone = Output.Pose[Index];
			Bone.SetRotation((FQuat(LocalAxis, RadiansPerBone) * Bone.GetRotation()).GetNormalized());
		}
	}
	const FCompactPoseBoneIndex Root(0);
	Output.Pose[Root] = Output.Pose[Root] * FTransform(FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(RootYaw)));
}
