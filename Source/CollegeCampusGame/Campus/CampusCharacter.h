// Campus Combat player character: the orange UEFN mannequin on CharacterMovement,
// with unarmed, crouch and rifle stances driven by UCampusAnimInstance.
// Keyboard / mouse / gamepad come through Enhanced Input; phones use the Touch* calls
// from the on-screen controls. The server owns health and hits.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CampusCharacter.generated.h"

class UCameraComponent;
class UCampusAnimInstance;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

UCLASS()
class ACampusCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACampusCharacter();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsArmed() const { return bArmed; }
	bool IsAiming() const { return bAiming; }
	bool IsSprinting() const { return bSprinting; }
	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }
	int32 GetAmmoInMag() const { return AmmoInMag; }
	int32 GetAmmoReserve() const { return AmmoReserve; }
	bool IsReloading() const;
	/** 0..1 while reloading, for the reload ring. */
	float GetReloadProgress() const;
	/** Current scope zoom (1x, 2x, 4x), used while aiming. */
	float GetZoom() const { return ZoomLevels[ZoomIndex]; }
	/** 0 hip .. 1 fully aimed (eases in over the ADS time). */
	float GetAimBlend() const { return AimBlend; }
	/** Look speed multiplier while zoomed: 1 / (1 + (zoom - 1) * aim). */
	float GetLookScale() const { return 1.f / (1.f + (GetZoom() - 1.f) * AimBlend); }
	void CycleZoom() { ZoomIndex = (ZoomIndex + 1) % UE_ARRAY_COUNT(ZoomLevels); }
	void SetSprintHeld(bool bHeld);

	// On-screen controls.
	void SetTouchMove(const FVector2D& Axis) { TouchMove = Axis; }
	void AddTouchLook(const FVector2D& DegreesDelta);
	void SetFireHeld(bool bHeld) { bFireHeld = bHeld; }
	void SetAiming(bool bAim);

	/** Free look (three.js eye button): orbit the camera only; body, aim and movement keep the real yaw. */
	void SetFreeLookHeld(bool bHeld);
	void AddFreeLook(float YawDegrees, float PitchDegrees);
	bool IsFreeLooking() const { return bFreeLookHeld || !FreeLook.IsNearlyZero(0.1f); }
	void ToggleSprint();
	void ToggleCrouch();
	void ToggleArmed();
	void Reload();

	/** Server: apply a hit from another player. */
	void ApplyHit(float Damage, AController* InstigatorController);

	/** Jump button: climbs onto a ledge in front when there is one (mantle), otherwise jumps. */
	void JumpPressed();

	/** True while climbing; the anim instance plays the mantle clip at this time. Kind: 0 stand, 1 walk, 2 run. */
	bool GetTraversal(int32& OutKind, float& OutTime) const;

	/** World time of the last hit the server confirmed for this shooter (HUD hit-marker). */
	float GetLastHitConfirmTime() const { return LastHitConfirmTime; }
	bool WasLastHitAKill() const { return bLastHitKill; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PawnClientRestart() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	/** Builds the input actions and mapping in code. */
	void BuildInput();

	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void LookGamepad(const FInputActionValue& Value);
	void SprintPressed();
	void SprintReleased();
	void AimPressed() { SetAiming(true); }
	void AimReleased() { SetAiming(false); }
	void FirePressed() { bFireHeld = true; }
	void FireReleased() { bFireHeld = false; }

	void ApplyMove(const FVector2D& Axis);
	void TryFire();
	void UpdateMovementSettings();
	bool CanSprintNow() const;
	/** Turns the view: positive pitch looks up. Every look source (touch, mouse, stick, recoil) uses this. */
	void ApplyLook(float YawDegrees, float PitchUpDegrees);
	UCampusAnimInstance* GetCampusAnim() const;

	UFUNCTION(Server, Reliable)
	void ServerSetArmed(bool bNewArmed);

	UFUNCTION(Server, Reliable)
	void ServerSetAiming(bool bNewAiming);

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	UFUNCTION(Server, Unreliable)
	void ServerFire(FVector_NetQuantize Start, FVector_NetQuantizeNormal Direction);

	UFUNCTION(Server, Reliable)
	void ServerReload();

	/** Remote players see the shot: anim, tracer, and the mark where it hit the world. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFire(FVector_NetQuantize End, FVector_NetQuantizeNormal Normal, bool bMark);

	UFUNCTION(Client, Unreliable)
	void ClientConfirmHit(bool bKill);

	bool TryStartTraversal();
	void StartTraversal(const FVector& StartFeet, float Yaw, float ScaleZ, float ScaleForward, float OverTop, uint8 Kind);
	void TickTraversal(float DeltaSeconds);
	void EndTraversal();
	/** Mantle clip timing: when the root has finished rising, plus a short settle. */
	static float TraversalEndTime(const class UAnimSequence* Clip);
	/** When the clip's root has finished rising (hands done pulling up). */
	static float RiseEndTime(const class UAnimSequence* Clip);
	static FVector ClipRoot(const class UAnimSequence* Clip, float Time);
	/** Where shots start for the tracer: the rifle muzzle. */
	FVector GunPoint() const;

	/** After the pose is final: grip in the right hand, barrel toward the left (support) hand. */
	void UpdateGun();
	void SpawnShotFx(const FVector& End, const FVector& Normal, bool bMark) const;

	UFUNCTION(Server, Reliable)
	void ServerStartTraversal(FVector_NetQuantize StartFeet, float Yaw, float ScaleZ, float ScaleForward, float OverTop, uint8 Kind);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartTraversal(FVector_NetQuantize StartFeet, float Yaw, float ScaleZ, float ScaleForward, float OverTop, uint8 Kind);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastUpperBody(bool bReload);

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** The rifle model (Unreal template SM_Rifle), placed between the hands every frame. */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Gun;

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

	// Speeds match the three.js game (player.js): stand 4.2 m/s, sprint 6.6, crouch 2.0, aiming x0.55.
	UPROPERTY(EditAnywhere, Category="Movement")
	float JogSpeed = 420.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float SprintSpeed = 660.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float RifleJogSpeed = 420.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float AimWalkSpeed = 231.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float CrouchSpeed = 200.f;

	/** Degrees per second at full right-stick deflection. */
	UPROPERTY(EditAnywhere, Category="Camera")
	float GamepadLookRate = 150.f;

	// Camera rig from the three.js game (CAMERA dist 3.0 / side 0.6 / up 0.4 at a 1.12 m eye;
	// AIM_CAMERA dist 0 / side 0.06 / up 0.02), scaled to this 165 cm eye.
	UPROPERTY(EditAnywhere, Category="Camera")
	float DefaultArmLength = 320.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float AimArmLength = 0.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float HipSide = 70.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float AimSide = 9.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float AimUp = 3.f;

	/** Boom pivot above the capsule centre: eye (165 - 90) + up (59). */
	UPROPERTY(EditAnywhere, Category="Camera")
	float HipPivotHeight = 120.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float CrouchPivotHeight = 75.f;

	/** three.js BASE_FOV: vertical degrees. */
	UPROPERTY(EditAnywhere, Category="Camera")
	float BaseVerticalFov = 62.f;

	/** three.js MAX_PITCH 1.35 rad. */
	UPROPERTY(EditAnywhere, Category="Camera")
	float MaxPitch = 77.3f;

	/** three.js MOUSE_LOOK 0.0022 rad per pixel. */
	UPROPERTY(EditAnywhere, Category="Camera")
	float MouseDegreesPerPixel = 0.126f;

	// AK from weapons.js: 600 rpm, 30 / 120, recoil 0.016 rad up, +-0.006 rad side, 70% recovers (tau 0.22 s).
	UPROPERTY(EditAnywhere, Category="Combat")
	float FireInterval = 0.1f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float RecoilUpDeg = 0.92f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float RecoilSideDeg = 0.34f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float RecoilRecoverShare = 0.7f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float RecoilRecoverTau = 0.22f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float Damage = 25.f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float Range = 10000.f;

	UPROPERTY(EditAnywhere, Category="Combat")
	int32 MagazineSize = 30;

	UPROPERTY(EditAnywhere, Category="Combat")
	float MaxHealth = 100.f;

	UPROPERTY(Replicated)
	bool bArmed = false;

	UPROPERTY(Replicated)
	bool bAiming = false;

	UPROPERTY(Replicated)
	float Health = 100.f;

	bool bSprinting = false;

	// Traversal (mantle) state, identical on every machine that runs it.
	bool bTraversing = false;
	uint8 TraversalKind = 0;
	float TraversalTime = 0.f;
	float TraversalEnd = 0.f;
	float TraversalYaw = 0.f;
	float TraversalScaleZ = 1.f;
	float TraversalScaleForward = 1.f;
	/** Distance from the wall face to the standing spot on top (part 2 of the climb). */
	float TraversalOverTop = 0.f;
	FVector TraversalStartFeet = FVector::ZeroVector;
	FVector TraversalRootStart = FVector::ZeroVector;

	/** Gun rotation relative to the right hand, captured while both hands hold it (used during reload). */
	FQuat GunInHand = FQuat::Identity;
	bool bGunInHandValid = false;

	/** Camera-only offset from the control rotation, degrees (X yaw, Y pitch). */
	FVector2D FreeLook = FVector2D::ZeroVector;
	bool bFreeLookHeld = false;

	float LastHitConfirmTime = -100.f;
	bool bLastHitKill = false;
	bool bFireHeld = false;
	bool bDead = false;
	FVector2D TouchMove = FVector2D::ZeroVector;
	int32 AmmoInMag = 30;
	int32 AmmoReserve = 120;
	float ReloadStartTime = 0.f;
	float ReloadEndTime = 0.f;
	float RecoilToRecover = 0.f;
	float AimLinear = 0.f;
	float AimBlend = 0.f;
	static constexpr float ZoomLevels[3] = { 1.f, 2.f, 4.f };
	int32 ZoomIndex = 1;
	float NextFireTime = 0.f;
};
