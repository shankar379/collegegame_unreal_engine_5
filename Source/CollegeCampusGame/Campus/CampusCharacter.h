// Campus Combat player character: the orange UEFN mannequin on CharacterMovement,
// with unarmed, crouch and rifle stances driven by UCampusAnimInstance.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CampusCharacter.generated.h"

class UCameraComponent;
class UCampusAnimInstance;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
struct FInputActionValue;

UCLASS()
class ACampusCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACampusCharacter();

	bool IsArmed() const { return bArmed; }
	bool IsAiming() const { return bAiming; }

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	/** Builds the input actions and mapping in code; keys match the touch interface's gamepad keys. */
	void BuildInput();

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void LookGamepad(const FInputActionValue& Value);
	void SprintPressed();
	void SprintReleased();
	void ToggleCrouch();
	void ToggleArmed();
	void AimPressed();
	void AimReleased();
	void Fire();
	void Reload();

	void UpdateMovementSettings();
	UCampusAnimInstance* GetCampusAnim() const;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> InputMapping;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookGamepadAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> EquipAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AimAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditAnywhere, Category="Movement")
	float JogSpeed = 400.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float SprintSpeed = 600.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float RifleJogSpeed = 375.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float AimWalkSpeed = 200.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float CrouchSpeed = 180.f;

	/** Degrees per second at full right-stick deflection. */
	UPROPERTY(EditAnywhere, Category="Camera")
	float GamepadLookRate = 150.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float DefaultArmLength = 300.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float AimArmLength = 160.f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float FireInterval = 0.12f;

	bool bArmed = false;
	bool bAiming = false;
	bool bSprinting = false;
	float ReloadEndTime = 0.f;
	float NextFireTime = 0.f;
};
