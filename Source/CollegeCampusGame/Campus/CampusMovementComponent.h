// Character movement with one campus rule: no automatic step-up onto ledges too shallow to stand on.
// MaxStepHeight is high (85 cm, so the 0.6 m building base can be walked onto), which made a runner hitting the
// corridor railing step onto its 30 cm / 15 cm-deep bottom band, hit the rods, drop, step up again: a bounce loop.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CampusMovementComponent.generated.h"

UCLASS()
class UCampusMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual bool CanStepUp(const FHitResult& Hit) const override;
};
