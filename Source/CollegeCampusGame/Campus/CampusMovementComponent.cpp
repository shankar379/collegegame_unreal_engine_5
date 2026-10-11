#include "Campus/CampusMovementComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

namespace CampusStep
{
	/** How far behind the obstacle's face a standable top must reach (stair treads are deeper; the railing's
	 *  bottom band is 15 cm). */
	constexpr float MinDepth = 18.f;
	/** A "top" this close to the floor we stand on is the floor behind a thin obstacle, not a step. */
	constexpr float MinRise = 4.f;
}

bool UCampusMovementComponent::CanStepUp(const FHitResult& Hit) const
{
	if (!Super::CanStepUp(Hit) || !CharacterOwner || !UpdatedComponent)
	{
		return false;
	}
	FVector Into = -Hit.ImpactNormal;
	Into.Z = 0.f;
	if (!Into.Normalize())
	{
		return true;
	}
	const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float FeetZ = UpdatedComponent->GetComponentLocation().Z - HalfHeight;
	const FVector Probe = Hit.ImpactPoint + Into * CampusStep::MinDepth;

	// Look straight down just behind the face: a real step has its own top there, within step height.
	FHitResult Top;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusStepUp), false, CharacterOwner);
	FCollisionResponseParams Responses;
	if (UpdatedPrimitive)
	{
		UpdatedPrimitive->InitSweepCollisionParams(Params, Responses);
	}
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Top,
		FVector(Probe.X, Probe.Y, FeetZ + MaxStepHeight + 5.f), FVector(Probe.X, Probe.Y, FeetZ - 5.f),
		UpdatedPrimitive ? UpdatedPrimitive->GetCollisionObjectType() : ECC_Pawn, Params, Responses);
	if (!bHit)
	{
		return false; // nothing to stand on behind it (a rail over a drop)
	}
	const float Rise = Top.ImpactPoint.Z - FeetZ;
	// something taller than a step right behind the face (the railing's rods), or only the floor behind a thin
	// obstacle: treat it as a wall
	return Rise <= MaxStepHeight && Rise >= CampusStep::MinRise;
}
