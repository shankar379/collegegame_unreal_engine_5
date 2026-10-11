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

	/** Prone, sent with every move (compressed flag) so the server limits speed on the same move as the owner. */
	bool bWantsProne = false;
	float ProneMaxSpeed = 45.f;

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};

class FSavedMove_Campus : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;
	bool bSavedWantsProne = false;

	virtual void Clear() override;
	virtual uint8 GetCompressedFlags() const override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void PrepMoveFor(ACharacter* Character) override;
};

class FNetworkPredictionData_Client_Campus : public FNetworkPredictionData_Client_Character
{
public:
	using FNetworkPredictionData_Client_Character::FNetworkPredictionData_Client_Character;
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FSavedMove_Campus()); }
};
