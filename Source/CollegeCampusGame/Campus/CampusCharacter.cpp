#include "Campus/CampusCharacter.h"

#include "Camera/CameraComponent.h"
#include "Campus/CampusAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "UObject/ConstructorHelpers.h"

ACampusCharacter::ACampusCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(35.f, 90.f);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	USkeletalMeshComponent* MeshComponent = GetMesh();
	MeshComponent->SetSkeletalMeshAsset(MeshFinder.Object);
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -90.f), FRotator(0.f, -90.f, 0.f));
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	MeshComponent->AnimClass = UCampusAnimInstance::StaticClass();

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 600.f, 0.f);
	Movement->JumpZVelocity = 500.f;
	Movement->AirControl = 0.35f;
	Movement->MaxWalkSpeed = JogSpeed;
	Movement->MinAnalogWalkSpeed = 20.f;
	Movement->BrakingDecelerationWalking = 2000.f;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->SetCrouchedHalfHeight(60.f);
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = DefaultArmLength;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 40.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 15.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void ACampusCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Over-the-shoulder camera with the rifle, closer while aiming.
	const float TargetLength = bAiming ? AimArmLength : DefaultArmLength;
	const float TargetSide = bArmed ? 55.f : 0.f;
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, TargetLength, DeltaSeconds, 10.f);
	CameraBoom->SocketOffset.Y = FMath::FInterpTo(CameraBoom->SocketOffset.Y, TargetSide, DeltaSeconds, 10.f);
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
	Input->BindAction(FireAction, ETriggerEvent::Triggered, this, &ACampusCharacter::Fire);
	Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &ACampusCharacter::Reload);
}

void ACampusCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (!Controller)
	{
		return;
	}
	const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axis.X);
}

void ACampusCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ACampusCharacter::LookGamepad(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	const float Step = GamepadLookRate * GetWorld()->GetDeltaSeconds();
	AddControllerYawInput(Axis.X * Step);
	AddControllerPitchInput(Axis.Y * Step);
}

void ACampusCharacter::SprintPressed()
{
	bSprinting = true;
	if (bIsCrouched)
	{
		UnCrouch();
	}
	UpdateMovementSettings();
}

void ACampusCharacter::SprintReleased()
{
	bSprinting = false;
	UpdateMovementSettings();
}

void ACampusCharacter::ToggleCrouch()
{
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		bSprinting = false;
		Crouch();
	}
	UpdateMovementSettings();
}

void ACampusCharacter::ToggleArmed()
{
	bArmed = !bArmed;
	if (bArmed)
	{
		if (UCampusAnimInstance* Anim = GetCampusAnim())
		{
			Anim->PlayEquip();
		}
	}
	else
	{
		bAiming = false;
	}
	UpdateMovementSettings();
}

void ACampusCharacter::AimPressed()
{
	if (!bArmed)
	{
		ToggleArmed();
	}
	bAiming = true;
	bSprinting = false;
	UpdateMovementSettings();
}

void ACampusCharacter::AimReleased()
{
	bAiming = false;
	UpdateMovementSettings();
}

void ACampusCharacter::Fire()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bArmed || Now < NextFireTime || Now < ReloadEndTime)
	{
		return;
	}
	NextFireTime = Now + FireInterval;
	if (UCampusAnimInstance* Anim = GetCampusAnim())
	{
		Anim->PlayFire();
	}
}

void ACampusCharacter::Reload()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bArmed || Now < ReloadEndTime)
	{
		return;
	}
	if (UCampusAnimInstance* Anim = GetCampusAnim())
	{
		Anim->PlayReload();
		ReloadEndTime = Now + Anim->GetReloadDuration();
	}
}

void ACampusCharacter::UpdateMovementSettings()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const bool bStrafe = bArmed || bAiming;
	Movement->bOrientRotationToMovement = !bStrafe;
	Movement->bUseControllerDesiredRotation = bStrafe;

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
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
}

UCampusAnimInstance* ACampusCharacter::GetCampusAnim() const
{
	return Cast<UCampusAnimInstance>(GetMesh()->GetAnimInstance());
}
