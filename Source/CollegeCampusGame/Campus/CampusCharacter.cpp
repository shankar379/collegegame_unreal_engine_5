#include "Campus/CampusCharacter.h"

#include "Camera/CameraComponent.h"
#include "Campus/CampusAnimInstance.h"
#include "Campus/CampusGameMode.h"
#include "Campus/CampusMatchState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Camera/PlayerCameraManager.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACampusCharacter::ACampusCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(30.f, 83.f);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin"));
	USkeletalMeshComponent* MeshComponent = GetMesh();
	MeshComponent->SetSkeletalMeshAsset(MeshFinder.Object);
	// The mesh is 166 cm tall with its feet at the origin: half-height 83 puts them on the capsule base.
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -83.f), FRotator(0.f, -90.f, 0.f));
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	MeshComponent->AnimClass = UCampusAnimInstance::StaticClass();
	// Off-screen characters skip pose work; saves CPU on phones in busy rooms.
	MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesAndRefreshBonesWhenPlayingMontages;

	// The body snaps to the camera yaw every frame, armed or not, and movement strafes relative to it
	// (three.js game.js: char.root.rotation.y = p.yaw).
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = false;
	Movement->JumpZVelocity = 600.f;
	Movement->AirControl = 0.35f;
	Movement->MaxWalkSpeed = JogSpeed;
	Movement->MinAnalogWalkSpeed = 20.f;
	Movement->BrakingDecelerationWalking = 2000.f;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->SetCrouchedHalfHeight(55.f);
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
	// Stand on boxes with the feet, not on the capsule's rounded edge: flat-base floor checks, and
	// walk off a ledge once the body is mostly past it.
	Movement->bUseFlatBaseForFloorChecks = true;
	Movement->PerchRadiusThreshold = 20.f;
	Movement->PerchAdditionalHeight = 0.f;
	Movement->MaxStepHeight = 45.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = DefaultArmLength;
	CameraBoom->SocketOffset = FVector(0.f, HipSide, 0.f);
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, HipPivotHeight));
	CameraBoom->bUsePawnControlRotation = true;
	// No lag: the camera follows the finger exactly (three.js applies look once per frame, no smoothing).
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;
	// Fat probe so the camera pulls in before clipping walls (three.js casts five offset rays).
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 18.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void ACampusCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ACampusCharacter, bArmed, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACampusCharacter, bAiming, COND_SkipOwner);
	DOREPLIFETIME(ACampusCharacter, Health);
}

void ACampusCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsLocallyControlled())
	{
		if (!TouchMove.IsNearlyZero())
		{
			ApplyMove(TouchMove);
		}
		if (bFireHeld)
		{
			TryFire();
		}
		if (bSprinting)
		{
			UpdateMovementSettings();
		}
	}

	// ADS moves linearly over 0.18 s; one smoothstep value drives camera, FOV and look speed together
	// so none of them lags a frame behind (three.js updateAds).
	AimLinear = FMath::FInterpConstantTo(AimLinear, bAiming ? 1.f : 0.f, DeltaSeconds, 1.f / 0.18f);
	AimBlend = AimLinear * AimLinear * (3.f - 2.f * AimLinear);

	// FOV: three.js uses a 70 deg VERTICAL FOV, divided by the eased zoom. Unreal's FOV is horizontal,
	// so convert with this viewport's aspect (a 20:9 phone gets a wide view, like the web game).
	if (IsLocallyControlled())
	{
		float Aspect = 16.f / 9.f;
		if (const APlayerController* PC = Cast<APlayerController>(Controller))
		{
			int32 SizeX = 0, SizeY = 0;
			PC->GetViewportSize(SizeX, SizeY);
			if (SizeX > 0 && SizeY > 0)
			{
				Aspect = float(SizeX) / float(SizeY);
			}
		}
		const float VerticalFov = BaseVerticalFov / (1.f + (GetZoom() - 1.f) * AimBlend);
		const float HorizontalFov = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFov * 0.5f)) * Aspect));
		FollowCamera->SetFieldOfView(HorizontalFov);

		// Own body hides once the sight is mostly up (three.js: hidden when e > 0.5).
		GetMesh()->SetOwnerNoSee(AimBlend > 0.5f);
	}

	// Recoil: part of each kick recovers by itself (exponential, tau).
	if (IsLocallyControlled() && RecoilToRecover > 0.f)
	{
		const float Step = RecoilToRecover * (1.f - FMath::Exp(-DeltaSeconds / RecoilRecoverTau));
		RecoilToRecover -= Step;
		ApplyLook(0.f, -Step);
	}

	// Camera rig: CAMERA -> AIM_CAMERA lerped by the eased ADS value (dist 0 = at the sight).
	const float PivotHeight = (bIsCrouched ? CrouchPivotHeight : HipPivotHeight);
	CameraBoom->TargetArmLength = FMath::Lerp(DefaultArmLength, AimArmLength, AimBlend);
	CameraBoom->SocketOffset = FVector(0.f, FMath::Lerp(HipSide, AimSide, AimBlend), FMath::Lerp(0.f, AimUp, AimBlend));
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, PivotHeight));
}

void ACampusCharacter::BuildInput()
{
	if (InputMapping)
	{
		return;
	}

	auto NewAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};
	MoveAction = NewAction(TEXT("IA_CampusMove"), EInputActionValueType::Axis2D);
	LookAction = NewAction(TEXT("IA_CampusLook"), EInputActionValueType::Axis2D);
	LookGamepadAction = NewAction(TEXT("IA_CampusLookGamepad"), EInputActionValueType::Axis2D);
	JumpAction = NewAction(TEXT("IA_CampusJump"), EInputActionValueType::Boolean);
	SprintAction = NewAction(TEXT("IA_CampusSprint"), EInputActionValueType::Boolean);
	CrouchAction = NewAction(TEXT("IA_CampusCrouch"), EInputActionValueType::Boolean);
	EquipAction = NewAction(TEXT("IA_CampusEquip"), EInputActionValueType::Boolean);
	AimAction = NewAction(TEXT("IA_CampusAim"), EInputActionValueType::Boolean);
	FireAction = NewAction(TEXT("IA_CampusFire"), EInputActionValueType::Boolean);
	ReloadAction = NewAction(TEXT("IA_CampusReload"), EInputActionValueType::Boolean);

	InputMapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Campus"));

	// WASD packs into one 2D axis: W/S drive Y (swizzled), A/S are negated.
	auto MapMove = [this](FKey Key, bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& Mapping = InputMapping->MapKey(MoveAction, Key);
		if (bSwizzle)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(InputMapping));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(InputMapping));
		}
	};
	MapMove(EKeys::W, true, false);
	MapMove(EKeys::S, true, true);
	MapMove(EKeys::A, false, true);
	MapMove(EKeys::D, false, false);
	InputMapping->MapKey(MoveAction, EKeys::Gamepad_Left2D).Modifiers.Add(NewObject<UInputModifierDeadZone>(InputMapping));

	InputMapping->MapKey(LookAction, EKeys::Mouse2D);
	InputMapping->MapKey(LookGamepadAction, EKeys::Gamepad_Right2D).Modifiers.Add(NewObject<UInputModifierDeadZone>(InputMapping));

	InputMapping->MapKey(JumpAction, EKeys::SpaceBar);
	InputMapping->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	InputMapping->MapKey(SprintAction, EKeys::LeftShift);
	InputMapping->MapKey(SprintAction, EKeys::Gamepad_LeftShoulder);
	InputMapping->MapKey(CrouchAction, EKeys::C);
	InputMapping->MapKey(CrouchAction, EKeys::Gamepad_FaceButton_Right);
	InputMapping->MapKey(EquipAction, EKeys::One);
	InputMapping->MapKey(EquipAction, EKeys::Gamepad_FaceButton_Top);
	InputMapping->MapKey(AimAction, EKeys::RightMouseButton);
	InputMapping->MapKey(AimAction, EKeys::Gamepad_LeftTrigger);
	InputMapping->MapKey(FireAction, EKeys::LeftMouseButton);
	InputMapping->MapKey(FireAction, EKeys::Gamepad_RightTrigger);
	InputMapping->MapKey(ReloadAction, EKeys::R);
	InputMapping->MapKey(ReloadAction, EKeys::Gamepad_FaceButton_Left);
}

void ACampusCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();

	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController)
	{
		return;
	}
	if (PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->ViewPitchMin = -MaxPitch;
		PlayerController->PlayerCameraManager->ViewPitchMax = MaxPitch;
	}
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
	{
		BuildInput();
		Subsystem->AddMappingContext(InputMapping, 0);
	}
}

void ACampusCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	BuildInput();

	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ACampusCharacter::Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ACampusCharacter::Look);
	Input->BindAction(LookGamepadAction, ETriggerEvent::Triggered, this, &ACampusCharacter::LookGamepad);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ACampusCharacter::SprintPressed);
	Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ACampusCharacter::SprintReleased);
	Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ACampusCharacter::ToggleCrouch);
	Input->BindAction(EquipAction, ETriggerEvent::Started, this, &ACampusCharacter::ToggleArmed);
	Input->BindAction(AimAction, ETriggerEvent::Started, this, &ACampusCharacter::AimPressed);
	Input->BindAction(AimAction, ETriggerEvent::Completed, this, &ACampusCharacter::AimReleased);
	Input->BindAction(FireAction, ETriggerEvent::Started, this, &ACampusCharacter::FirePressed);
	Input->BindAction(FireAction, ETriggerEvent::Completed, this, &ACampusCharacter::FireReleased);
	Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &ACampusCharacter::Reload);
}

void ACampusCharacter::ApplyMove(const FVector2D& Axis)
{
	if (!Controller)
	{
		return;
	}
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

void ACampusCharacter::Move(const FInputActionValue& Value)
{
	ApplyMove(Value.Get<FVector2D>());
}

void ACampusCharacter::Look(const FInputActionValue& Value)
{
	// Mouse up (+Y) looks up.
	const FVector2D Axis = Value.Get<FVector2D>() * MouseDegreesPerPixel * GetLookScale();
	ApplyLook(Axis.X, Axis.Y);
}

void ACampusCharacter::LookGamepad(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	// Stick up (+Y) looks up.
	const float Step = GamepadLookRate * GetWorld()->GetDeltaSeconds() * GetLookScale();
	ApplyLook(Axis.X * Step, Axis.Y * Step);
}

void ACampusCharacter::AddTouchLook(const FVector2D& DegreesDelta)
{
	ApplyLook(DegreesDelta.X, DegreesDelta.Y);
}

void ACampusCharacter::ApplyLook(float YawDegrees, float PitchUpDegrees)
{
	if (!Controller || !IsLocallyControlled())
	{
		return;
	}
	FRotator Rotation = Controller->GetControlRotation();
	Rotation.Yaw = FRotator::NormalizeAxis(Rotation.Yaw + YawDegrees);
	Rotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rotation.Pitch) + PitchUpDegrees, -MaxPitch, MaxPitch);
	Rotation.Roll = 0.f;
	Controller->SetControlRotation(Rotation);
}

void ACampusCharacter::SprintPressed()
{
	if (bArmed)
	{
		return;
	}
	bSprinting = true;
	if (bIsCrouched)
	{
		UnCrouch();
	}
	UpdateMovementSettings();
	ServerSetSprinting(true);
}

void ACampusCharacter::SprintReleased()
{
	bSprinting = false;
	UpdateMovementSettings();
	ServerSetSprinting(false);
}

void ACampusCharacter::ToggleSprint()
{
	if (bSprinting)
	{
		SprintReleased();
	}
	else
	{
		SprintPressed();
	}
}

void ACampusCharacter::ToggleCrouch()
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		if (bSprinting)
		{
			SprintReleased();
		}
		Crouch();
	}
}

void ACampusCharacter::ToggleArmed()
{
	bArmed = !bArmed;
	if (bArmed)
	{
		if (bSprinting)
		{
			SprintReleased();
		}
		if (UCampusAnimInstance* Anim = GetCampusAnim())
		{
			Anim->PlayEquip();
		}
	}
	else
	{
		bAiming = false;
		bFireHeld = false;
	}
	UpdateMovementSettings();
	ServerSetArmed(bArmed);
}

void ACampusCharacter::SetAiming(bool bAim)
{
	if (bAim && !bArmed)
	{
		ToggleArmed();
	}
	bAiming = bAim && bArmed;
	UpdateMovementSettings();
	ServerSetAiming(bAiming);
}

void ACampusCharacter::TryFire()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bArmed || bDead || Now < NextFireTime || Now < ReloadEndTime)
	{
		return;
	}
	if (AmmoInMag <= 0)
	{
		Reload();
		return;
	}
	NextFireTime = Now + FireInterval;
	--AmmoInMag;
	if (UCampusAnimInstance* Anim = GetCampusAnim())
	{
		Anim->PlayFire();
	}

	const float Pose = bIsCrouched ? 0.85f : 1.f;
	const float Kick = RecoilUpDeg * (1.f - 0.5f * AimBlend) * Pose;
	// Kick goes up, then most of it settles back down by itself.
	ApplyLook(FMath::FRandRange(-RecoilSideDeg, RecoilSideDeg), Kick);
	RecoilToRecover += Kick * RecoilRecoverShare;

	FVector ViewLocation;
	FRotator ViewRotation;
	if (Controller)
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
		ServerFire(ViewLocation, ViewRotation.Vector());
	}
	if (AmmoInMag == 0)
	{
		Reload();
	}
}

bool ACampusCharacter::IsReloading() const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() < ReloadEndTime;
}

float ACampusCharacter::GetReloadProgress() const
{
	const float Duration = ReloadEndTime - ReloadStartTime;
	if (!IsReloading() || Duration <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - ReloadStartTime) / Duration, 0.f, 1.f);
}

void ACampusCharacter::SetSprintHeld(bool bHeld)
{
	if (bHeld != bSprinting)
	{
		bHeld ? SprintPressed() : SprintReleased();
	}
}

void ACampusCharacter::Reload()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bArmed || Now < ReloadEndTime || AmmoInMag >= MagazineSize || AmmoReserve <= 0)
	{
		return;
	}
	if (UCampusAnimInstance* Anim = GetCampusAnim())
	{
		Anim->PlayReload();
		ReloadStartTime = Now;
		ReloadEndTime = Now + Anim->GetReloadDuration();
	}
	const int32 Taken = FMath::Min(MagazineSize - AmmoInMag, AmmoReserve);
	AmmoInMag += Taken;
	AmmoReserve -= Taken;
	ServerReload();
}

void ACampusCharacter::ServerSetArmed_Implementation(bool bNewArmed)
{
	bArmed = bNewArmed;
	if (!bArmed)
	{
		bAiming = false;
	}
	else
	{
		bSprinting = false;
		MulticastUpperBody(false);
	}
	UpdateMovementSettings();
}

void ACampusCharacter::ServerSetAiming_Implementation(bool bNewAiming)
{
	bAiming = bNewAiming && bArmed;
	UpdateMovementSettings();
}

void ACampusCharacter::ServerSetSprinting_Implementation(bool bNewSprinting)
{
	bSprinting = bNewSprinting && !bArmed;
	UpdateMovementSettings();
}

void ACampusCharacter::ServerReload_Implementation()
{
	MulticastUpperBody(true);
}

void ACampusCharacter::ServerFire_Implementation(FVector_NetQuantize Start, FVector_NetQuantizeNormal Direction)
{
	if (bDead || !bArmed)
	{
		return;
	}
	MulticastFire();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusFire), false, this);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + FVector(Direction) * Range, ECC_Visibility, Params))
	{
		return;
	}
	ACampusCharacter* Victim = Cast<ACampusCharacter>(Hit.GetActor());
	if (!Victim)
	{
		return;
	}
	const ACampusPlayerState* MyState = GetPlayerState<ACampusPlayerState>();
	const ACampusPlayerState* VictimState = Victim->GetPlayerState<ACampusPlayerState>();
	if (MyState && VictimState && MyState->Team == VictimState->Team)
	{
		return;
	}
	Victim->ApplyHit(Damage, GetController());
}

void ACampusCharacter::MulticastFire_Implementation()
{
	if (!IsLocallyControlled())
	{
		if (UCampusAnimInstance* Anim = GetCampusAnim())
		{
			Anim->PlayFire();
		}
	}
}

void ACampusCharacter::MulticastUpperBody_Implementation(bool bReload)
{
	if (IsLocallyControlled())
	{
		return;
	}
	if (UCampusAnimInstance* Anim = GetCampusAnim())
	{
		bReload ? Anim->PlayReload() : Anim->PlayEquip();
	}
}

void ACampusCharacter::ApplyHit(float InDamage, AController* InstigatorController)
{
	if (!HasAuthority() || bDead)
	{
		return;
	}
	Health = FMath::Max(0.f, Health - InDamage);
	if (Health <= 0.f)
	{
		bDead = true;
		if (ACampusMatchGameMode* GameMode = GetWorld()->GetAuthGameMode<ACampusMatchGameMode>())
		{
			GameMode->OnKilled(GetController(), InstigatorController);
		}
	}
}

void ACampusCharacter::UpdateMovementSettings()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();

	if (bAiming)
	{
		Movement->MaxWalkSpeed = AimWalkSpeed;
	}
	else if (bArmed)
	{
		Movement->MaxWalkSpeed = RifleJogSpeed;
	}
	else
	{
		Movement->MaxWalkSpeed = bSprinting ? SprintSpeed : JogSpeed;
	}
	// Sprint only counts while pushing forward; strafing or backing up stays at jog speed.
	if (bSprinting && IsLocallyControlled() && !CanSprintNow())
	{
		Movement->MaxWalkSpeed = JogSpeed;
	}
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
}

bool ACampusCharacter::CanSprintNow() const
{
	const FVector Local = GetActorRotation().UnrotateVector(GetLastMovementInputVector());
	return !bArmed && !bIsCrouched && Local.X > 0.3f;
}

UCampusAnimInstance* ACampusCharacter::GetCampusAnim() const
{
	return Cast<UCampusAnimInstance>(GetMesh()->GetAnimInstance());
}
