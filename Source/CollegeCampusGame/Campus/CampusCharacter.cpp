#include "Campus/CampusCharacter.h"

#include "Camera/CameraComponent.h"
#include "Campus/CampusAnimInstance.h"
#include "Campus/CampusFx.h"
#include "Campus/CampusGameMode.h"
#include "Campus/CampusMatchState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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
#include "Animation/AnimSequence.h"

ACampusCharacter::ACampusCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(30.f, 83.f);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin"));
	USkeletalMeshComponent* MeshComponent = GetMesh();
	MeshComponent->SetSkeletalMeshAsset(MeshFinder.Object);
	// The mesh is 166 cm tall with its feet at the origin. The movement code keeps the capsule hovering
	// ~2.4 cm above the floor (MAX_FLOOR_DIST), so sink the mesh by that gap to plant the feet.
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -83.f - 2.4f), FRotator(0.f, -90.f, 0.f));
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
	// Rotation is set every frame from the control rotation plus the free-look offset (see Tick).
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->SetUsingAbsoluteRotation(true);
	// No lag: the camera follows the finger exactly (three.js applies look once per frame, no smoothing).
	CameraBoom->bEnableCameraLag = false;
	CameraBoom->bEnableCameraRotationLag = false;
	// Fat probe so the camera pulls in before clipping walls (three.js casts five offset rays).
	CameraBoom->bDoCollisionTest = true;
	CameraBoom->ProbeSize = 18.f;

	// The rifle mesh is loaded in BeginPlay, not here: nothing extra loads during engine start-up.
	Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gun"));
	Gun->SetupAttachment(MeshComponent);
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->SetGenerateOverlapEvents(false);
	Gun->SetCastShadow(true);
	Gun->SetVisibility(false);
	Gun->SetUsingAbsoluteLocation(true);
	Gun->SetUsingAbsoluteRotation(true);

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void ACampusCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (Gun && !Gun->GetStaticMesh())
	{
		Gun->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle")));
	}
	// Place the gun after the final pose of every frame, so it never trails the hands.
	GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this, &ACampusCharacter::UpdateGun));
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

	if (bTraversing)
	{
		TickTraversal(DeltaSeconds);
	}

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

	// Free look: released offset swings back the shortest way (three.js: x *= exp(-14 dt), snap < 0.002 rad).
	if (!bFreeLookHeld && !FreeLook.IsZero())
	{
		FreeLook *= FMath::Exp(-14.f * DeltaSeconds);
		if (FreeLook.Size() < FMath::RadiansToDegrees(0.002f))
		{
			FreeLook = FVector2D::ZeroVector;
		}
	}
	const FRotator Control = Controller ? Controller->GetControlRotation() : GetActorRotation();
	const float CameraPitch = FMath::Clamp(FRotator::NormalizeAxis(Control.Pitch) + FreeLook.Y, -MaxPitch, MaxPitch);
	CameraBoom->SetWorldRotation(FRotator(CameraPitch, Control.Yaw + FreeLook.X, 0.f));
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
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACampusCharacter::JumpPressed);
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
	if (!Controller || bTraversing)
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

void ACampusCharacter::SetFreeLookHeld(bool bHeld)
{
	bFreeLookHeld = bHeld;
	if (bHeld)
	{
		// three.js: holding free look cancels ADS and blocks the trigger.
		bFireHeld = false;
		if (bAiming)
		{
			SetAiming(false);
		}
	}
}

void ACampusCharacter::AddFreeLook(float YawDegrees, float PitchDegrees)
{
	// Yaw is unlimited (wrapped); free-look pitch is clamped to +-0.9 rad.
	FreeLook.X = FRotator::NormalizeAxis(FreeLook.X + YawDegrees);
	FreeLook.Y = FMath::Clamp(FreeLook.Y + PitchDegrees, -FMath::RadiansToDegrees(0.9f), FMath::RadiansToDegrees(0.9f));
}

void ACampusCharacter::SetAiming(bool bAim)
{
	if (bAim && IsFreeLooking())
	{
		return;
	}
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
	if (!bArmed || bDead || bTraversing || IsFreeLooking() || Now < NextFireTime || Now < ReloadEndTime)
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

		// Instant local feedback (three.js draws its own marks and tracer the same frame).
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		FHitResult Hit;
		const FVector End = ViewLocation + ViewRotation.Vector() * Range;
		const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, ViewLocation, End, Objects, FCollisionQueryParams(SCENE_QUERY_STAT(CampusFireFx), false, this));
		SpawnShotFx(bHit ? Hit.ImpactPoint : End, bHit ? Hit.ImpactNormal : FVector::UpVector, bHit && !Cast<ACampusCharacter>(Hit.GetActor()));
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
	// Trace world and pawns (character capsules do not block the Visibility channel).
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusFire), false, this);
	FHitResult Hit;
	const FVector End = FVector(Start) + FVector(Direction) * Range;
	const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params);
	ACampusCharacter* Victim = bHit ? Cast<ACampusCharacter>(Hit.GetActor()) : nullptr;
	MulticastFire(bHit ? FVector(Hit.ImpactPoint) : End, bHit ? FVector(Hit.ImpactNormal) : FVector::UpVector, bHit && !Victim);
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
	const bool bWasAlive = Victim->GetHealth() > 0.f;
	Victim->ApplyHit(Damage, GetController());
	ClientConfirmHit(bWasAlive && Victim->GetHealth() <= 0.f);
}

void ACampusCharacter::MulticastFire_Implementation(FVector_NetQuantize End, FVector_NetQuantizeNormal Normal, bool bMark)
{
	// The shooter already played its own anim and effects when it fired.
	if (IsLocallyControlled())
	{
		return;
	}
	if (UCampusAnimInstance* Anim = GetCampusAnim())
	{
		Anim->PlayFire();
	}
	SpawnShotFx(End, Normal, bMark);
}

void ACampusCharacter::ClientConfirmHit_Implementation(bool bKill)
{
	LastHitConfirmTime = GetWorld()->GetTimeSeconds();
	bLastHitKill = bKill;
}

namespace CampusGun
{
	// SM_Rifle: barrel along +Y, 78 cm long, muzzle at +Y 51 cm, grip near the origin.
	constexpr float MuzzleForward = 51.f;
	constexpr float MuzzleUp = 6.f;
	// Grip offset from the right hand bone (pivot sits a little below and behind the knuckles).
	const FVector GripOffset(0.f, -4.f, -3.f);
}

FVector ACampusCharacter::GunPoint() const
{
	if (Gun && Gun->IsVisible())
	{
		const FTransform& GunTransform = Gun->GetComponentTransform();
		return GunTransform.TransformPosition(FVector(0.f, CampusGun::MuzzleForward, CampusGun::MuzzleUp));
	}
	return GetActorLocation() + GetActorForwardVector() * 45.f + GetActorRightVector() * 18.f + FVector(0.f, 0.f, 45.f);
}

void ACampusCharacter::UpdateGun()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!Gun || !MeshComponent)
	{
		return;
	}
	const bool bShow = bArmed && !(IsLocallyControlled() && AimBlend > 0.5f);
	if (Gun->IsVisible() != bShow)
	{
		Gun->SetVisibility(bShow);
	}
	if (!bShow)
	{
		return;
	}

	const FTransform RightHand = MeshComponent->GetSocketTransform(TEXT("hand_r"));
	const FVector LeftHand = MeshComponent->GetSocketLocation(TEXT("hand_l"));
	const FVector Barrel = LeftHand - RightHand.GetLocation();
	const UCampusAnimInstance* Anim = GetCampusAnim();
	const bool bTwoHanded = Barrel.Size() > 15.f && Barrel.Size() < 70.f && !IsReloading();

	FQuat Rotation;
	if (bTwoHanded)
	{
		// Barrel (+Y) toward the support hand, the gun's up (+Z) as close to world up as the barrel allows.
		Rotation = FRotationMatrix::MakeFromYZ(Barrel.GetSafeNormal(), FVector::UpVector).ToQuat();
		GunInHand = RightHand.GetRotation().Inverse() * Rotation;
		bGunInHandValid = true;
	}
	else if (bGunInHandValid)
	{
		// One hand on the gun (reload / equip): keep the last grip relative to the right hand.
		Rotation = RightHand.GetRotation() * GunInHand;
	}
	else
	{
		Rotation = FRotationMatrix::MakeFromYZ(GetActorForwardVector(), FVector::UpVector).ToQuat();
	}
	Gun->SetWorldLocationAndRotation(RightHand.GetLocation() + Rotation.RotateVector(CampusGun::GripOffset), Rotation);
}

void ACampusCharacter::SpawnShotFx(const FVector& End, const FVector& Normal, bool bMark) const
{
	if (UCampusFxSubsystem* Fx = GetWorld()->GetSubsystem<UCampusFxSubsystem>())
	{
		Fx->AddTracer(GunPoint(), End);
		if (bMark)
		{
			Fx->AddImpact(End, Normal);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Traversal: mantle onto ledges 0.45 - 2.3 m high. The clip's own root motion (rise 100 cm, then
// forward onto the top) is stretched to the measured ledge height and distance, so the hands land
// on the edge for any box size. Everyone runs the same deterministic path from the same inputs.

namespace CampusTraversal
{
	constexpr float MinHeight = 45.f;
	constexpr float MaxHeight = 230.f;
	constexpr float WallReach = 110.f;
	constexpr float OntoTop = 35.f;
}

void ACampusCharacter::JumpPressed()
{
	if (bTraversing)
	{
		return;
	}
	if (!TryStartTraversal())
	{
		Jump();
	}
}

bool ACampusCharacter::GetTraversal(int32& OutKind, float& OutTime) const
{
	if (!bTraversing)
	{
		return false;
	}
	OutKind = TraversalKind;
	OutTime = TraversalTime;
	return true;
}

FVector ACampusCharacter::ClipRoot(const UAnimSequence* Clip, float Time)
{
	return Clip ? Clip->ExtractRootTrackTransform(FAnimExtractContext(double(Time)), nullptr).GetTranslation() : FVector::ZeroVector;
}

float ACampusCharacter::RiseEndTime(const UAnimSequence* Clip)
{
	if (!Clip)
	{
		return 0.f;
	}
	const float Length = Clip->GetPlayLength();
	const float TopZ = ClipRoot(Clip, Length).Z;
	for (int32 Step = 0; Step <= 60; ++Step)
	{
		const float Time = Length * Step / 60.f;
		if (ClipRoot(Clip, Time).Z >= TopZ - 1.f)
		{
			return Time;
		}
	}
	return Length;
}

float ACampusCharacter::TraversalEndTime(const UAnimSequence* Clip)
{
	if (!Clip)
	{
		return 0.f;
	}
	const float Length = Clip->GetPlayLength();
	const float TopZ = ClipRoot(Clip, Length).Z;
	for (int32 Step = 0; Step <= 60; ++Step)
	{
		const float Time = Length * Step / 60.f;
		if (ClipRoot(Clip, Time).Z >= TopZ - 1.f)
		{
			return FMath::Min(Time + 0.3f, Length);
		}
	}
	return Length;
}

bool ACampusCharacter::TryStartTraversal()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	const UCampusAnimInstance* Anim = GetCampusAnim();
	if (!Movement || !Movement->IsMovingOnGround() || !Anim || bIsCrouched)
	{
		return false;
	}

	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	FVector Forward = GetActorForwardVector();
	Forward.Z = 0.f;
	Forward.Normalize();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusTraversal), false, this);

	// 1) A wall in front at knee height, roughly facing us.
	FHitResult Wall;
	const FVector KneeStart = Feet + FVector(0.f, 0.f, 40.f);
	if (!GetWorld()->LineTraceSingleByChannel(Wall, KneeStart, KneeStart + Forward * CampusTraversal::WallReach, ECC_Visibility, Params))
	{
		return false;
	}
	FVector WallNormal = Wall.ImpactNormal;
	WallNormal.Z = 0.f;
	if (!WallNormal.Normalize() || FVector::DotProduct(-WallNormal, Forward) < 0.6f)
	{
		return false;
	}

	// 2) The top of it, just past the edge.
	FHitResult Top;
	const FVector Over = Wall.ImpactPoint - WallNormal * 25.f;
	if (!GetWorld()->LineTraceSingleByChannel(Top, FVector(Over.X, Over.Y, Feet.Z + CampusTraversal::MaxHeight + 30.f), FVector(Over.X, Over.Y, Feet.Z + 20.f), ECC_Visibility, Params)
		|| Top.ImpactNormal.Z < 0.7f)
	{
		return false;
	}
	const float Height = Top.ImpactPoint.Z - Feet.Z;
	if (Height < CampusTraversal::MinHeight || Height > CampusTraversal::MaxHeight)
	{
		return false;
	}

	// 3) Room to stand on top.
	const FVector Landing = Top.ImpactPoint - WallNormal * CampusTraversal::OntoTop;
	const FVector StandAt = Landing + FVector(0.f, 0.f, HalfHeight + 2.f);
	if (GetWorld()->OverlapBlockingTestByChannel(StandAt, FQuat::Identity, ECC_Pawn, GetCapsuleComponent()->GetCollisionShape(), Params))
	{
		return false;
	}

	// 4) Pick the clip by approach speed and stretch its root path onto this ledge.
	const float Speed = GetVelocity().Size2D();
	const uint8 Kind = Speed < 60.f ? 0 : (Speed < 320.f ? 1 : 2);
	const UAnimSequence* Clip = Anim->GetMantleClip(Kind);
	if (!Clip)
	{
		return false;
	}
	const FVector Facing = -WallNormal;
	const float Yaw = Facing.Rotation().Yaw;
	// Part 1 target: capsule front touching the wall. Part 2 target: standing on top.
	const float CapsuleRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float ToWall = FMath::Max(0.f, FVector::DotProduct(Wall.ImpactPoint - Feet, Facing) - CapsuleRadius - 2.f);
	const float ToLanding = FMath::Max(ToWall + 10.f, FVector::DotProduct(Landing - Feet, Facing));
	const FVector RootStart = ClipRoot(Clip, 0.f);
	const FVector RootRise = ClipRoot(Clip, RiseEndTime(Clip)) - RootStart;
	const float ScaleZ = RootRise.Z > 1.f ? Height / RootRise.Z : 1.f;
	const float ScaleForward = RootRise.Y > 1.f ? FMath::Clamp(ToWall / RootRise.Y, 0.f, 3.f) : 0.f;
	TraversalOverTop = ToLanding - ToWall;

	if (HasAuthority())
	{
		StartTraversal(Feet, Yaw, ScaleZ, ScaleForward, TraversalOverTop, Kind);
		MulticastStartTraversal(Feet, Yaw, ScaleZ, ScaleForward, TraversalOverTop, Kind);
	}
	else
	{
		StartTraversal(Feet, Yaw, ScaleZ, ScaleForward, TraversalOverTop, Kind);
		ServerStartTraversal(Feet, Yaw, ScaleZ, ScaleForward, TraversalOverTop, Kind);
	}
	return true;
}

void ACampusCharacter::ServerStartTraversal_Implementation(FVector_NetQuantize StartFeet, float Yaw, float ScaleZ, float ScaleForward, float OverTop, uint8 Kind)
{
	// Loose sanity check: the start must be where the server has us.
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (FVector::Dist(FVector(StartFeet), GetActorLocation() - FVector(0.f, 0.f, HalfHeight)) > 150.f || ScaleZ > 3.f)
	{
		return;
	}
	StartTraversal(StartFeet, Yaw, ScaleZ, ScaleForward, OverTop, Kind);
	MulticastStartTraversal(StartFeet, Yaw, ScaleZ, ScaleForward, OverTop, Kind);
}

void ACampusCharacter::MulticastStartTraversal_Implementation(FVector_NetQuantize StartFeet, float Yaw, float ScaleZ, float ScaleForward, float OverTop, uint8 Kind)
{
	if (HasAuthority() || IsLocallyControlled())
	{
		return;
	}
	StartTraversal(StartFeet, Yaw, ScaleZ, ScaleForward, OverTop, Kind);
}

void ACampusCharacter::StartTraversal(const FVector& StartFeet, float Yaw, float ScaleZ, float ScaleForward, float OverTop, uint8 Kind)
{
	const UCampusAnimInstance* Anim = GetCampusAnim();
	const UAnimSequence* Clip = Anim ? Anim->GetMantleClip(Kind) : nullptr;
	if (!Clip)
	{
		return;
	}
	bTraversing = true;
	TraversalKind = Kind;
	TraversalTime = 0.f;
	TraversalEnd = TraversalEndTime(Clip);
	TraversalYaw = Yaw;
	TraversalScaleZ = ScaleZ;
	TraversalScaleForward = ScaleForward;
	TraversalOverTop = OverTop;
	TraversalStartFeet = StartFeet;
	TraversalRootStart = ClipRoot(Clip, 0.f);
	bFireHeld = false;
	if (bSprinting)
	{
		bSprinting = false;
		UpdateMovementSettings();
	}

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->SetMovementMode(MOVE_Flying);
	bUseControllerRotationYaw = false;
	SetActorRotation(FRotator(0.f, Yaw, 0.f));
}

void ACampusCharacter::TickTraversal(float DeltaSeconds)
{
	const UCampusAnimInstance* Anim = GetCampusAnim();
	const UAnimSequence* Clip = Anim ? Anim->GetMantleClip(TraversalKind) : nullptr;
	TraversalTime += DeltaSeconds;
	if (!Clip)
	{
		EndTraversal();
		return;
	}
	const float Time = FMath::Min(TraversalTime, TraversalEnd);
	const FVector Root = ClipRoot(Clip, Time) - TraversalRootStart;
	// Part 1 (until the root finishes rising): forward scaled to stop at the wall face, height scaled
	// to the ledge. Part 2: the rest of the clip's forward travel maps onto the distance over the top.
	const float RiseEnd = RiseEndTime(Clip);
	const FVector RiseRoot = ClipRoot(Clip, RiseEnd) - TraversalRootStart;
	float Forward;
	if (Time <= RiseEnd)
	{
		Forward = Root.Y * TraversalScaleForward;
	}
	else
	{
		const FVector EndRoot = ClipRoot(Clip, TraversalEnd) - TraversalRootStart;
		const float Over = FMath::Max(EndRoot.Y - RiseRoot.Y, 1.f);
		Forward = RiseRoot.Y * TraversalScaleForward + (Root.Y - RiseRoot.Y) / Over * TraversalOverTop;
	}
	// Mesh space: +Y forward, +X to the character's left, +Z up.
	const FVector Local(Forward, -Root.X, FMath::Min(Root.Z, RiseRoot.Z) * TraversalScaleZ);
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Location = TraversalStartFeet + FRotator(0.f, TraversalYaw, 0.f).RotateVector(Local) + FVector(0.f, 0.f, HalfHeight);
	SetActorLocation(Location, false, nullptr, ETeleportType::None);
	if (TraversalTime >= TraversalEnd)
	{
		EndTraversal();
	}
}

void ACampusCharacter::EndTraversal()
{
	bTraversing = false;
	bUseControllerRotationYaw = true;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->StopMovementImmediately();
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
