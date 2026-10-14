// Campus Combat player character: the orange UEFN mannequin on CharacterMovement,
// with unarmed, crouch and rifle stances driven by UCampusAnimInstance.
// Keyboard / mouse / gamepad come through Enhanced Input; phones use the Touch* calls
// from the on-screen controls. The server owns health and hits.

#pragma once

#include "CoreMinimal.h"
#include "Campus/CampusLoot.h"
#include "Campus/CampusVehicle.h"
#include "Campus/CampusWeapons.h"
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

	/** AI bot soldiers (Campus/CampusBot.h) drive this pawn through its OWN server-side functions -
	 *  ServerFire, ServerReload, ServerSetArmed - so a bot's bullet takes the identical path to a
	 *  phone's. Server-only, and the alternative was five new public methods on this header. */
	friend class ACampusBotController;

public:
	ACampusCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** "This pawn is the one a person on THIS machine looks through." IsLocallyControlled() is not that
	 *  test any more: on a listen-server host an AI-possessed bot is locally controlled too, and the
	 *  camera rig, the own-shot FX skip and the FOV maths are all for the human's pawn alone. */
	bool IsLocalPlayerPawn() const;

	bool IsArmed() const { return bArmed; }
	bool IsAiming() const { return bAiming; }
	bool IsSprinting() const { return bSprinting; }
	/** Armed, crouched and running: the bent-over run (CrouchSprintSpeed, crouch clips sped up + torso bend). */
	bool IsCrouchRunning() const { return bRunHeld && bArmed && bIsCrouched && !bAiming && !bProne && !bSliding; }
	bool IsProne() const { return bProne; }
	bool IsSliding() const { return bSliding; }
	bool IsZipping() const { return Zip != nullptr; }

	/** The deployment pose, REPLICATED TO EVERY MACHINE: 0 none, 1 freefall, 2 under the canopy.
	 *  ACampusBRDrop keeps the dive's real state, but that actor is owner-relevant - it never reaches
	 *  the other forty-nine phones, and the skydive pose has to. One byte on the pawn everybody
	 *  already sees is the cheapest thing that fixes that, so the pose is driven from here. */
	static constexpr uint8 DiveNone = 0;
	static constexpr uint8 DiveFreefall = 1;
	static constexpr uint8 DiveChute = 2;
	uint8 GetDivePose() const { return DivePose; }
	bool IsFreefalling() const { return DivePose == DiveFreefall; }
	bool IsUnderChute() const { return DivePose == DiveChute; }
	bool IsDiving() const { return DivePose != DiveNone; }
	/** Server: ACampusBRDrop sets this as the dive moves through its states. */
	void SetDivePose(uint8 Pose);

	/** Standing at a zipline tower: the Jump button rides it. */
	bool CanUseZipline() const;
	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }
	int32 GetAmmoInMag() const { return AmmoInMag; }
	int32 GetAmmoReserve() const { return AmmoReserve; }
	bool IsReloading() const;
	/** 0..1 while reloading, for the reload ring. */
	float GetReloadProgress() const;
	/** Current scope zoom (1x, 2x, 4x), used while aiming. Capped at 2x from a moving car. */
	float GetZoom() const;
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

	// ---- vehicles (Docs/VEHICLES_DESIGN.md Section 6.2). The character is never re-possessed: it is
	// attached to a seat component and keeps every other system exactly as it was on foot.
	ACampusVehicle* GetVehicle() const { return Vehicle; }
	ECampusSeat GetSeat() const { return Seat; }
	bool IsSeated() const { return Vehicle != nullptr; }
	bool IsDriving() const { return Vehicle != nullptr && Seat == ECampusSeat::Driver; }
	/** A free seat within ACampusVehicle::UseDistance, for the touch button. Mirrors CanUseZipline(). */
	ACampusVehicle* FindNearbyVehicle(ECampusSeat& OutSeat) const;
	/** ENTER / EXIT button: take the nearest free seat, or get out if already seated. */
	void VehiclePressed();
	/** SWITCH SEAT: move to the next free seat in the same car. */
	void VehicleLongPressed();
	/** While driving, the Fire slot becomes BRAKE (held). */
	void SetBrakeHeld(bool bHeld) { bBrakeHeld = bHeld; }
	bool IsBrakeHeld() const { return bBrakeHeld; }
	/** Seat bookkeeping. Called by ACampusVehicle (authority) and by OnRep on every other machine. */
	void SetSeated(ACampusVehicle* InVehicle, ECampusSeat InSeat);

	/** 0.10 s while seated (the car's playout buffer draws this mesh), 0.08 s on foot.
	 *  Read PER CANDIDATE inside RewoundVictim: get this wrong and drive-by hit registration breaks. */
	double ViewDelaySeconds() const;

	/** True while climbing; the anim instance plays the mantle clip at this time. Kind: 0 stand, 1 walk, 2 run. */
	bool GetTraversal(int32& OutKind, float& OutTime) const;

	// ---- weapons (CampusWeapons.h). Two slots, one of them current. Equipping COPIES the gun's row into the
	// combat fields below (Damage, FireInterval, MagazineSize, recoil, falloff, ...), so the whole existing
	// fire / reload / lag-comp path keeps working and simply shoots whatever is in hand.
	FName GetWeaponId(int32 Slot) const;
	FName GetCurrentWeaponId() const { return GetWeaponId(CurrentSlot); }
	int32 GetCurrentSlot() const { return CurrentSlot; }
	const FCampusWeaponDef& GetCurrentWeapon() const;
	bool HasWeapon(int32 Slot) const { return !GetWeaponId(Slot).IsNone(); }
	/** Rounds left in the slot we are not holding (the HUD's second plate). */
	int32 GetSlotAmmo(int32 Slot) const;
	/** Tap the other slot plate: the owner switches at once and the server confirms. */
	void SwitchWeapon(int32 Slot);
	void NextWeapon() { SwitchWeapon(CurrentSlot == 0 ? 1 : 0); }
	/** Server: put this gun in a slot. Slot < 0 fills an empty slot, else replaces the one in hand. */
	void GiveWeapon(FName WeaponId, int32 Slot = -1);
	/** Owner: equip whatever gun the player's backend loadout says is equipped (/v1/me -> EquippedGun). */
	void ApplyBackendLoadout();

	// ---- battle-royale body gear
	float GetArmor() const { return Armor; }
	bool HasHelmet() const { return bHelmet; }
	/** A helmet cuts headshot damage; the shooter's ServerFire reads this off the victim. */
	float HeadDamageShare() const { return bHelmet ? 0.65f : 1.f; }
	/** True while a medkit is still healing (the HUD shows the heal). */
	bool IsHealing() const { return MedkitRemaining > 0.f; }

	// ---- ground loot (CampusLoot.h)
	/** The pickup the PICK UP button would take (nearest one in reach), or nullptr. Mirrors FindNearbyVehicle(). */
	ACampusPickup* FindNearbyPickup() const;
	/** PICK UP button: ask the server for the nearest pickup. */
	void PickupPressed();
	/** Server only: hand this over. False when it cannot be used (helmet already on, nothing to top up). */
	bool ReceivePickup(ECampusPickupKind Kind, FName WeaponId, int32 Amount);

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
	void ServerRequestSeat(ACampusVehicle* Car, ECampusSeat WantedSeat);

	UFUNCTION(Server, Reliable)
	void ServerRequestExit();

	UFUNCTION(Server, Reliable)
	void ServerRequestSwitchSeat();

	/** 20 Hz, only when a value moves by more than 8/255 or every 250 ms. int8 keeps it at 4 bytes. */
	UFUNCTION(Server, Unreliable)
	void ServerDriveInput(int8 Throttle, int8 Steer, bool bHandbrake);

	UFUNCTION()
	void OnRep_Vehicle();

	/** Movement, collision, capsule and trigger state for sitting in / getting out of a car. */
	void ApplySeatedState();
	void SendDriveInput();
	/** Keep the camera inside the seat's firing cone, so it never shows an angle you cannot shoot. */
	void ClampLookToSeat();
	/** Server: is this direction inside the seat's aperture? A driver never passes. */
	bool SeatAllowsFire(const FVector& Direction) const;
	/** The instant a shot should be judged at, for a target drawn ViewDelay behind its own updates. */
	double ShotRewindTime(double ViewDelay) const;
	/** Server: a refused vehicle RPC. Ten in ten seconds and the connection goes. */
	void CountVehicleViolation();

	UFUNCTION(Server, Reliable)
	void ServerReload();

	// ---- weapons / loot plumbing
	UFUNCTION(Server, Reliable)
	void ServerSwitchWeapon(uint8 Slot);

	/** The owner's backend loadout: which gun and what upgrade level it is.
	 *  INTERIM TRUST GAP: the level arrives from the owning client. The host only clamps it (and the mod cap
	 *  bounds the damage); it cannot yet verify ownership. The real fix is the admit response / travel options
	 *  carrying the API's own answer - see the report. campus.GunStatsEnabled 0 removes mods entirely. */
	UFUNCTION(Server, Reliable)
	void ServerSetLoadout(FName WeaponId, uint8 Level);

	UFUNCTION(Server, Reliable)
	void ServerTakePickup(ACampusPickup* Pickup);

	UFUNCTION()
	void OnRep_Weapons();

	/** Copies the current slot's row into the combat fields, and rescales / tints the rifle mesh for it. */
	void ApplyWeapon();
	/** Server: the medkit's health, paid out over MedkitSeconds. */
	void TickMedkit(float DeltaSeconds);
	/** Server: adds this frame's honest travel to ACampusPlayerState::DistanceCm (result screen). */
	void TickDistance(float DeltaSeconds);
	/** A shot leaves the muzzle inside the gun's cone (tight while aiming). Owner side; the server
	 *  judges the direction it is sent, as it always has. */
	FVector SpreadDirection(const FVector& Aim) const;

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

	/** Unreliable on purpose: a cosmetic reload / equip animation is not worth a reliable bunch out of a phone
	 *  host during a firefight, and a dropped one costs nothing (the ammo itself is server state). */
	UFUNCTION(NetMulticast, Unreliable)
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

	/** The drop camera. The canopy is a 7 m dome 3.3 m over the shoulders: at the 320 cm walking boom
	 *  it sits DIRECTLY ABOVE THE CAMERA, outside a 62 deg vertical frustum - the chute was there all
	 *  along and nobody could ever see it. 900 cm puts the whole canopy in frame (its far rim lands at
	 *  ~12 deg, its near rim at ~26 deg), and the freefall pulls back too so the dive reads. */
	UPROPERTY(EditAnywhere, Category="Camera")
	float FreefallArmLength = 620.f;

	UPROPERTY(EditAnywhere, Category="Camera")
	float ChuteArmLength = 900.f;

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

	/** The dive pose (see SetDivePose): 0 none, 1 freefall, 2 under the canopy. Unconditional, owner
	 *  included - the owner's own phone draws the pose and pulls its camera back off the same byte. */
	UPROPERTY(Replicated)
	uint8 DivePose = 0;

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
	/** Server: where this pawn stood last frame, for the DISTANCE TRAVELED stat. */
	FVector DistanceMark = FVector::ZeroVector;
	bool bHasDistanceMark = false;
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
	/** 0..1 eased: the drop camera is out (freefall or canopy). Eased over ~0.5 s so the pull-back at
	 *  the moment the chute opens is a move, not a cut. */
	float DiveCamera = 0.f;
	/** The dive boom's own length (cm), eased between the freefall and the canopy values. */
	float DiveArm = 0.f;
	/** Camera pivot height above the feet, eased toward the stance's (cm); < 0 until the first frame. */
	float CameraAboveFeet = -1.f;
	/** Speed of the stance height glide (critically damped spring: starts and ends gently). */
	float CameraAboveFeetSpeed = 0.f;
	/** Whether this character's camera rig is being ticked (only the locally controlled one needs it).
	 *  Starts true: the constructor leaves the boom and camera ticking. */
	bool bCameraRigActive = true;

	/** The capsule changes size inside the movement tick, after Tick placed the boom: keep the boom where it was
	 *  in the world for that frame (else it pops by the height change for one frame). */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
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
	// ---- vehicle state. Unconditional replication, NOT COND_SkipOwner: every client needs to know who
	// is in which seat to draw them and to aim at them.
	UPROPERTY(ReplicatedUsing=OnRep_Vehicle)
	TObjectPtr<ACampusVehicle> Vehicle = nullptr;

	/** Same notify as Vehicle: a SEAT SWITCH leaves Vehicle untouched, so without this no other machine
	 *  (nor the owning client) would re-run ApplySeatedState and the driver's bUseControllerRotationYaw,
	 *  seat chip and firing aperture would stay on the seat they left. */
	UPROPERTY(ReplicatedUsing=OnRep_Vehicle)
	ECampusSeat Seat = ECampusSeat::None;

	/** Driver intent this frame, from the move stick / WASD: Y throttle, X steer. */
	FVector2D DriveAxis = FVector2D::ZeroVector;
	bool bBrakeHeld = false;
	/** We crouched this character to seat it, so we know to stand it back up on the way out. */
	bool bCrouchedForSeat = false;
	float NextDriveSendTime = 0.f;
	float LastDriveSendTime = -1.f;
	int8 LastDriveThrottle = 0;
	int8 LastDriveSteer = 0;
	bool bLastDriveHandbrake = false;
	float SeatRequestTime = -10.f;
	// Server-side only: the drive-input rate window and the refused-RPC counter.
	float DriveWindowStart = 0.f;
	int32 DriveInputsThisSecond = 0;
	float ViolationWindowStart = 0.f;
	int32 VehicleViolations = 0;

	// ---- weapon state. The ids replicate (everyone draws the right gun and the HUD reads both plates);
	// the stats do not: every machine looks the id up in the same CampusWeapons table.
	UPROPERTY(ReplicatedUsing=OnRep_Weapons)
	TArray<FName> WeaponSlots;

	UPROPERTY(ReplicatedUsing=OnRep_Weapons)
	uint8 CurrentSlot = 0;

	/** Store upgrade level per slot (0 / 1 = base). Ground loot is always 0: a floor gun has no bonuses. */
	UPROPERTY(ReplicatedUsing=OnRep_Weapons)
	TArray<uint8> WeaponLevels;

	/** The current gun's row with its store mods already folded in (ApplyWeapon rebuilds it). */
	FCampusWeaponDef CurrentDef;

	/** Vest points left; absorbs ArmorAbsorbShare of every hit until they are gone. */
	UPROPERTY(Replicated)
	float Armor = 0.f;

	UPROPERTY(Replicated)
	bool bHelmet = false;

	/** Ammo of the slot that is not in hand: [slot] mag / reserve. The owner predicts its own, the
	 *  server keeps the real one, exactly like AmmoInMag vs ServerAmmoInMag. */
	int32 StashMag[2] = { 0, 0 };
	int32 StashReserve[2] = { 0, 0 };
	int32 ServerStashMag[2] = { 0, 0 };
	int32 ServerStashReserve[2] = { 0, 0 };

	/** Server: health a medkit still owes, and the rate it pays at. */
	float MedkitRemaining = 0.f;
	float MedkitRate = 0.f;

	// Copied from the current gun's row by ApplyWeapon (the rest lands in the Combat fields above).
	float ReloadSeconds = 2.1f;
	float SpreadDeg = 2.5f;
	float AdsSpreadShare = 0.25f;
	float WeaponMaxZoom = 4.f;
	float ShotPitch = 1.f;

	TArray<FHitboxFrame> HitboxHistory;
	void RecordHitbox();
	/** The capsule as it was at Time (interpolated between recorded frames). */
	FHitboxFrame HitboxAt(double Time) const;
	/** The enemy this shot hit as the shooter saw the world (rewound by its ping), nearer than BlockDistance. */
	ACampusCharacter* RewoundVictim(const FVector& Start, const FVector& Direction, float BlockDistance, FVector& OutPoint, uint8& OutZone) const;
	float ServerReloadEndTime = 0.f;
};
