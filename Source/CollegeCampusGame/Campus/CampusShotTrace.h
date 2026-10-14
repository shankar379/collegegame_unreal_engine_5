// One trace for bullets and bot sight that sees the real steps, not the invisible stair ramps.
//
// The ramps (Scripts/build_collision.py, build_campusmap.py) are ECC_WorldStatic so the capsule walks up
// them, and they ignore the Visibility / Camera channels so "bullets see the real steps". But bullets
// and bot sight are OBJECT-TYPE queries, which never look at channel responses - so the ramps stopped
// bullets anyway. This trace walks every hit along the line and skips any component that ignores
// Visibility, which is exactly the ramps' signature.
#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"

namespace CampusShotTrace
{
	inline bool FirstSolid(const UWorld* World, FHitResult& OutHit, const FVector& From, const FVector& To,
		const FCollisionObjectQueryParams& Objects, const FCollisionQueryParams& Params)
	{
		TArray<FHitResult> Hits;
		World->LineTraceMultiByObjectType(Hits, From, To, Objects, Params);
		for (const FHitResult& Each : Hits)
		{
			const UPrimitiveComponent* Component = Each.GetComponent();
			if (Component && Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Ignore)
			{
				continue;
			}
			OutHit = Each;
			return true;
		}
		return false;
	}
}
