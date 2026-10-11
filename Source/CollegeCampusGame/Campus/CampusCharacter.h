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
class USoundBase;
class USoundAttenuation;
struct FInputActionValue;

UCLASS()
class ACampusCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ACampusCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsArmed() const { return bArmed; }
	bool IsAiming() const { return bAiming; }
	bool IsSprinting() const { return bSprinting; }
	/** Armed, crouched and running: the bent-over run (CrouchSprintSpeed, crouch clips sped up + torso bend). */
	bool IsCrouchRunning() const { return bRunHeld && bArmed && bIsCrouched && !bAiming && !bProne && !bSliding; }
	bool IsProne() const { return bProne; }
	bool IsSliding() const { return bSliding; }
	bool IsZipping() const { return Zip != nullptr; }
	/** Standing at a zipline tower: the Jump button rides it. */
	bool CanUseZipline() const;
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
	float GetAimBlend() const { return CameraAimBlend; }
	/** Look speed multiplier while zoomed: 1 / (1 + (zoom - 1) * aim). */
	float GetLookScale() const { return 1.f / (1.f + (GetZoom() - 1.f) * CameraAimBlend); }
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
	/** Lie down / get up. Prone rides on the crouch capsule; only speed, camera and pose differ. */
	void ToggleProne();
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
	bool WasLastHitAHeadshot() const { return bLastHitHead; }
	int32 GetLastHitDamage() const { return LastHitDamage; }
	virtual void Landed(const FHitResult& Hit) override;

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
	void ServerSetProne(bool bNewProne);

	UFUNCTION(Server, Reliable)
	void ServerSetSliding(bool bNewSliding);

	UFUNCTION(Server, Reliable)
	void ServerStartZip(class ACampusZipline* Line, int32 FromEnd);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartZip(class ACampusZipline* Line, int32 FromEnd);

	/** The server refused a zip / traversal the owner already started locally: undo it there (else the owner keeps
	 *  flying along the cable / up the wall while the server corrects it every frame). */
	UFUNCTION(Client, Reliable)
	void ClientCancelZip();

	UFUNCTION(Client, Reliable)
	void ClientCancelTraversal();

	class ACampusZipline* FindZipline(int32& OutEnd) const;
	void StartZip(class ACampusZipline* Line, int32 FromEnd);
	void TickZip(float DeltaSeconds);
	/** Sprint + crouch: knee slide that bleeds speed, then crouch (or stand up if still sprinting). */
	void SetSliding(bool bNewSliding);
	void ApplySliding(bool bNewSliding);
	void SetProne(bool bNewProne);

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
	void ClientConfirmHit(bool bKill, bool bHead, int16 Amount);

	bool TryStartTraversal();
	void StartTraversal(const FVector& StartFeet, float Yaw, float Height, float WallDistance, float LandingDistance, uint8 Kind);
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
	void ServerStartTraversal(FVector_NetQuantize StartFeet, float Yaw, float Height, float WallDistance, float LandingDistance, uint8 Kind);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartTraversal(FVector_NetQuantize StartFeet, float Yaw, float Height, float WallDistance, float LandingDistance, uint8 Kind);

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

	/** Crouch + run button: stays crouched, moves faster (the crouch clips speed up to match). */
	UPROPERTY(EditAnywhere, Category="Movement")
	float CrouchSprintSpeed = 330.f;

	UPROPERTY(EditAnywhere, Category="Movement")
	float ProneSpeed = 45.f; // the crawl clip sweeps the floor at 25 cm/s: 1.8x, hands and knees stay planted

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

	UPROPERTY(EditAnywhere, Category="Camera")
	float PronePivotHeight = 35.f;

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

	/** Hit zones (from where the shot meets the body): head counts more, arms and legs less. */
	UPROPERTY(EditAnywhere, Category="Combat")
	float HeadMultiplier = 2.f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float LimbMultiplier = 0.75f;

	/** Rifle falloff (PUBG / Valorant pattern): full damage to 30 m, down to 70% at 60 m. */
	UPROPERTY(EditAnywhere, Category="Combat")
	float FalloffStart = 3000.f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float FalloffEnd = 6000.f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float FalloffMinShare = 0.7f;

	/** Falls shorter than this are free; DeadlyFall and more kill. */
	UPROPERTY(EditAnywhere, Category="Combat")
	float SafeFall = 400.f;

	UPROPERTY(EditAnywhere, Category="Combat")
	float DeadlyFall = 1550.f;

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
	bool bProne = false;

	/** Below this world height (cm) the player is killed: an invisible floor under the whole map. */
	UPROPERTY(EditAnywhere, Category="Combat")
	float KillHeight = -1500.f;

	UPROPERTY(Replicated)
	bool bSliding = false;

	// Zipline ride (three.js zipline.js), run on every machine from the start event.
	UPROPERTY(Transient)
	TObjectPtr<class ACampusZipline> Zip;
	int32 ZipDir = 1;
	float ZipS = 0.f;
	float ZipV = 0.f;
	float SlideTime = 0.f;

	/** Slide: no ground friction, only this deceleration (sprint 660 -> 280 cm/s in about a second). */
	UPROPERTY(EditAnywhere, Category="Movement")
	float SlideBraking = 380.f;

	/** Stopping without input: linear braking, so the stop clips can be distance matched. */
	UPROPERTY(EditAnywhere, Category="Movement")
	float StopBraking = 1400.f;

	UPROPERTY(Replicated)
	float Health = 100.f;

	bool bSprinting = false;
	/** Run input held (stick at the rim / sprint button), whatever we carry. Unarmed it sprints; armed and
	 *  crouched it is the bent-over crouch run (the rifle never sprints standing). */
	bool bRunHeld = false;
	/** Moving forward enough to run (with a margin, see CanSprintNow). */
	mutable bool bSprintHeading = false;

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
	/** Hurdle: when the root is highest (over the obstacle) and the speed to keep running at afterwards. */
	float TraversalApexTime = 0.f;
	float TraversalExitSpeed = 0.f;

	/** Gun rotation relative to the right hand, captured while both hands hold it (used during reload). */
	FQuat GunInHand = FQuat::Identity;
	bool bGunInHandValid = false;

	/** Camera-only offset from the control rotation, degrees (X yaw, Y pitch). */
	FVector2D FreeLook = FVector2D::ZeroVector;
	bool bFreeLookHeld = false;

	// Weapon sounds rendered from the three.js recipes (sounds.js): own shot in-head, others' in 3D.
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ShotSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> RemoteShotSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ReloadSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> DryFireSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> ShotAttenuation;

	float NextDryFireTime = 0.f;
	float LastHitConfirmTime = -100.f;
	bool bLastHitKill = false;
	bool bLastHitHead = false;
	int32 LastHitDamage = 0;
	/** Server: highest point of the current fall (fall damage on landing). */
	float FallTopZ = 0.f;
	bool bTrackingFall = false;
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
	/** 0..1 eased: free look has pulled the camera out of the sight (aim pose kept). */
	float FreeLookCamera = 0.f;
	/** AimBlend as the camera sees it (0 while free looking). */
	float CameraAimBlend = 0.f;
	/** Camera pivot height above the feet, eased toward the stance's (cm); < 0 until the first frame. */
	float CameraAboveFeet = -1.f;
	static constexpr float ZoomLevels[3] = { 1.f, 2.f, 4.f };
	int32 ZoomIndex = 1;
	float NextFireTime = 0.f;

	// Server's own count of this player's gun (never trusts the phone): ammo, a fire-rate budget that refills at the
	// gun's rate (a few shots of slack for Wi-Fi bunching), and the reload window.
	int32 ServerAmmoInMag = 30;
	int32 ServerAmmoReserve = 120;
	float ServerShotCredit = 3.f;
	float ServerCreditTime = 0.f;

	/** Server-only lag compensation: where this capsule was over the last half second. */
	struct FHitboxFrame
	{
		double Time = 0.0;
		FVector Center = FVector::ZeroVector;
		float HalfHeight = 0.f;
		float Radius = 0.f;
	};
	TArray<FHitboxFrame> HitboxHistory;
	void RecordHitbox();
	/** The capsule as it was at Time (interpolated between recorded frames). */
	FHitboxFrame HitboxAt(double Time) const;
	/** The enemy this shot hit as the shooter saw the world (rewound by its ping), nearer than BlockDistance. */
	ACampusCharacter* RewoundVictim(const FVector& Start, const FVector& Direction, float BlockDistance, FVector& OutPoint, uint8& OutZone) const;
	float ServerReloadEndTime = 0.f;
};
