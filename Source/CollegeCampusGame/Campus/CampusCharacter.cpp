#include "Campus/CampusCharacter.h"
#include "Campus/CampusMovementComponent.h"

#include "Camera/CameraComponent.h"
#include "Campus/CampusAnimInstance.h"
#include "Campus/CampusZipline.h"
#include "EngineUtils.h"
#include "Campus/CampusFx.h"
#include "Campus/CampusDoor.h"
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
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "UObject/ConstructorHelpers.h"
#include "Animation/AnimSequence.h"

ACampusCharacter::ACampusCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCampusMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// players send 60 updates a second (engine default 100 is capped by the server tick anyway; keep min high so
	// a still player does not fall to 2 Hz and teleport when it moves)
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(30.f);
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
	Movement->JumpZVelocity = 440.f;
	Movement->AirControl = 0.35f;
	Movement->MaxWalkSpeed = JogSpeed;
	Movement->MinAnalogWalkSpeed = 20.f;
	Movement->BrakingDecelerationWalking = StopBraking;
	Movement->BrakingFrictionFactor = 0.f;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->SetCrouchedHalfHeight(55.f);
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
	// Stand on boxes with the feet, not on the capsule's rounded edge: flat-base floor checks, and
	// walk off a ledge once the body is mostly past it.
	Movement->bUseFlatBaseForFloorChecks = true;
	Movement->PerchRadiusThreshold = 20.f;
	Movement->PerchAdditionalHeight = 0.f;
	// three.js player.js: STEP_UP 0.85 m (steps, curbs, the college's 0.77 m base edge) and WALKABLE 0.6
	// (floors up to ~53 degrees, the stair ramps).
	Movement->MaxStepHeight = 85.f;
	Movement->SetWalkableFloorAngle(53.f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = DefaultArmLength;
	CameraBoom->SocketOffset = FVector(0.f, HipSide, 0.f);
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, HipPivotHeight));
	// Rotation is set every frame from the control rotation plus the free-look offset (see Tick).
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->SetUsingAbsoluteRotation(true);
	// No lag: the camera follows the finger exactly (three.js applies look once per frame, no smoothing).
	// a short lag: a collision pull-in (low camera near the floor) or a small correction eases instead of snapping
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 18.f;
	CameraBoom->CameraLagMaxDistance = 60.f;
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
	ShotSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CampusAudio/S_Rifle_Shot.S_Rifle_Shot"));
	RemoteShotSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CampusAudio/S_Rifle_Shot_Remote.S_Rifle_Shot_Remote"));
	ReloadSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CampusAudio/S_Reload.S_Reload"));
	DryFireSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CampusAudio/S_DryFire.S_DryFire"));
	HitSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/CampusAudio/S_Hit.S_Hit"));
	ShotAttenuation = NewObject<USoundAttenuation>(this);
	{
		FSoundAttenuationSettings& Settings = ShotAttenuation->Attenuation;
		Settings.bAttenuate = true;
		Settings.bSpatialize = true;
		Settings.DistanceAlgorithm = EAttenuationDistanceModel::Inverse;
		Settings.AttenuationShape = EAttenuationShape::Sphere;
		Settings.AttenuationShapeExtents = FVector(1040.f, 0.f, 0.f);
		Settings.FalloffDistance = 13000.f - 1040.f;
		Settings.dBAttenuationAtMax = -60.f;
		// Air absorption: farther shots lose their crack (three.js lowers the lowpass with distance).
		Settings.bAttenuateWithLPF = true;
		Settings.LPFRadiusMin = 1040.f;
		Settings.LPFRadiusMax = 13000.f;
		Settings.LPFFrequencyAtMin = 20000.f;
		Settings.LPFFrequencyAtMax = 1500.f;
	}

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
	DOREPLIFETIME_CONDITION(ACampusCharacter, bProne, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(ACampusCharacter, bSliding, COND_SkipOwner);
	DOREPLIFETIME(ACampusCharacter, Health);
}

void ACampusCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		RecordHitbox();
		// fall damage: remember the top of every fall (zipline / vault moves are not falls)
		const bool bFalling = GetCharacterMovement()->IsFalling() && !bTraversing && !Zip;
		if (bFalling)
		{
			FallTopZ = bTrackingFall ? FMath::Max(FallTopZ, GetActorLocation().Z) : GetActorLocation().Z;
			bTrackingFall = true;
		}
		else if (!GetCharacterMovement()->IsFalling())
		{
			bTrackingFall = false;
		}
	}
	if (bTraversing)
	{
		TickTraversal(DeltaSeconds);
	}
	if (Zip)
	{
		TickZip(DeltaSeconds);
	}
	// Fell off the map: dead, like any other death (the death screen offers the way back to the lobby).
	if (HasAuthority() && !bDead && GetActorLocation().Z < KillHeight)
	{
		ApplyHit(Health + 1.f, nullptr);
	}
	if (bSliding)
	{
		SlideTime += DeltaSeconds;
		// owner and server both end it (the server must not keep a 760 cm/s slide if the owner's RPC never comes)
		if ((IsLocallyControlled() || HasAuthority()) && (GetVelocity().Size2D() < 280.f || SlideTime > 1.5f || !GetCharacterMovement()->IsMovingOnGround()))
		{
			SetSliding(false);
			if (bSprinting && IsLocallyControlled())
			{
				UnCrouch();
			}
		}
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
	}
	// The listen-server host's own pawn never sends ServerMove, so the engine never fills RemoteViewPitch for it and
	// every joined phone would see the host aiming level forever. Feed it here (replicated to others by APawn).
	if (HasAuthority() && IsLocallyControlled() && GetNetMode() == NM_ListenServer && Controller)
	{
		SetRemoteViewPitch(Controller->GetControlRotation().Pitch);
	}
	// owner AND server re-check the run rule every frame (the stick can turn mid-sprint)
	if (bRunHeld && (IsLocallyControlled() || HasAuthority()))
	{
		UpdateMovementSettings();
	}

	// ADS moves linearly over 0.18 s; one smoothstep value drives camera, FOV and look speed together
	// so none of them lags a frame behind (three.js updateAds).
	AimLinear = FMath::FInterpConstantTo(AimLinear, bAiming ? 1.f : 0.f, DeltaSeconds, 1.f / 0.18f);
	AimBlend = AimLinear * AimLinear * (3.f - 2.f * AimLinear);
	// Free look while aiming: the character stays in its aiming stance, only the camera leaves the sight for the
	// normal shoulder view so you can look around; releasing the eye drops straight back into the sight.
	FreeLookCamera = FMath::FInterpConstantTo(FreeLookCamera, IsFreeLooking() ? 1.f : 0.f, DeltaSeconds, 1.f / 0.18f);
	const float CamAim = AimBlend * (1.f - FreeLookCamera * FreeLookCamera * (3.f - 2.f * FreeLookCamera));
	CameraAimBlend = CamAim;

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
		const float VerticalFov = BaseVerticalFov / (1.f + (GetZoom() - 1.f) * CamAim);
		const float HorizontalFov = FMath::RadiansToDegrees(2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(VerticalFov * 0.5f)) * Aspect));
		FollowCamera->SetFieldOfView(HorizontalFov);

		// Own body hides once the sight is mostly up (three.js: hidden when e > 0.5).
		GetMesh()->SetOwnerNoSee(CamAim > 0.5f);
	}

	// Recoil: part of each kick recovers by itself (exponential, tau).
	if (IsLocallyControlled() && RecoilToRecover > 0.f)
	{
		const float Step = RecoilToRecover * (1.f - FMath::Exp(-DeltaSeconds / RecoilRecoverTau));
		RecoilToRecover -= Step;
		ApplyLook(0.f, -Step);
	}

	// Camera rig: CAMERA -> AIM_CAMERA lerped by the eased ADS value (dist 0 = at the sight).
	// Stance height glides instead of snapping: it is tracked ABOVE THE FEET, because crouch / prone shrink the
	// capsule instantly (its centre, where the boom is mounted, drops in one frame) while the body takes ~1 s to
	// go down. Fast enough to follow the get-down / get-up animation, slow enough to never jump.
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float StanceAboveFeet = HalfHeight + (bProne ? PronePivotHeight : (bIsCrouched ? CrouchPivotHeight : HipPivotHeight));
	if (CameraAboveFeet < 0.f)
	{
		CameraAboveFeet = StanceAboveFeet; // first frame
	}
	// critically damped spring (~0.35 s): FInterpTo moved fastest on the first frame, so standing up from prone
	// (a 1.2 m rise) shot the camera up before the get-up animation had started moving the body
	{
		constexpr float SmoothTime = 0.35f;
		const float Omega = 2.f / SmoothTime;
		const float X = Omega * DeltaSeconds;
		const float Decay = 1.f / (1.f + X + 0.48f * X * X + 0.235f * X * X * X);
		const float Change = CameraAboveFeet - StanceAboveFeet;
		const float Temp = (CameraAboveFeetSpeed + Omega * Change) * DeltaSeconds;
		CameraAboveFeetSpeed = (CameraAboveFeetSpeed - Omega * Temp) * Decay;
		CameraAboveFeet = StanceAboveFeet + (Change + Temp) * Decay;
	}
	float PivotHeight = CameraAboveFeet - HalfHeight;
	// While aiming, the sight follows the real head height (standing, crouched or any later stance),
	// so the scope never floats above a crouched character.
	if (CamAim > 0.f && GetMesh())
	{
		const float HeadHeight = GetMesh()->GetSocketLocation(TEXT("head")).Z - GetActorLocation().Z + 4.f;
		PivotHeight = FMath::Lerp(PivotHeight, HeadHeight, CamAim);
	}
	CameraBoom->TargetArmLength = FMath::Lerp(DefaultArmLength, AimArmLength, CamAim);
	CameraBoom->SocketOffset = FVector(0.f, FMath::Lerp(HipSide, AimSide, CamAim), FMath::Lerp(0.f, AimUp, CamAim));
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
	if (!Controller || bTraversing || bSliding || Zip)
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
	bRunHeld = true;
	// armed: no standing sprint, but a crouched rifleman runs bent over (UpdateMovementSettings)
	bSprinting = !bArmed;
	UpdateMovementSettings();
	ServerSetSprinting(true);
}

void ACampusCharacter::SprintReleased()
{
	bRunHeld = false;
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
	if (bSliding)
	{
		SetSliding(false);
		return;
	}
	if (!bIsCrouched && !bProne && bSprinting && !bArmed && GetVelocity().Size2D() > 450.f && GetCharacterMovement()->IsMovingOnGround())
	{
		Crouch();
		SetSliding(true);
		return;
	}
	if (bProne)
	{
		// From prone the crouch button kneels up and stays crouched.
		SetProne(false);
		return;
	}
	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		// Running stays on: crouched it becomes a crouch-run.
		Crouch();
	}
}

void ACampusCharacter::SetSliding(bool bNewSliding)
{
	ApplySliding(bNewSliding);
	if (!HasAuthority())
	{
		ServerSetSliding(bNewSliding);
	}
}

void ACampusCharacter::ServerSetSliding_Implementation(bool bNewSliding)
{
	ApplySliding(bNewSliding);
}

void ACampusCharacter::ApplySliding(bool bNewSliding)
{
	bSliding = bNewSliding;
	SlideTime = 0.f;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->GroundFriction = bSliding ? 0.f : 8.f;
	Movement->BrakingDecelerationWalking = bSliding ? SlideBraking : StopBraking;
	UpdateMovementSettings();
}

ACampusZipline* ACampusCharacter::FindZipline(int32& OutEnd) const
{
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	for (TActorIterator<ACampusZipline> It(GetWorld()); It; ++It)
	{
		OutEnd = It->NearEnd(Feet);
		if (OutEnd != INDEX_NONE)
		{
			return *It;
		}
	}
	return nullptr;
}

bool ACampusCharacter::CanUseZipline() const
{
	int32 End = INDEX_NONE;
	return !Zip && !bTraversing && FindZipline(End) != nullptr;
}

void ACampusCharacter::ServerStartZip_Implementation(ACampusZipline* Line, int32 FromEnd)
{
	if (!Line || FromEnd < 0 || FromEnd > 1 || Zip)
	{
		ClientCancelZip();
		return;
	}
	const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (Line->NearEnd(Feet) != FromEnd)
	{
		ClientCancelZip();
		return;
	}
	StartZip(Line, FromEnd);
	MulticastStartZip(Line, FromEnd);
}

void ACampusCharacter::ClientCancelZip_Implementation()
{
	if (!Zip)
	{
		return;
	}
	Zip = nullptr;
	bUseControllerRotationYaw = true;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Walking);
	Movement->StopMovementImmediately();
}

void ACampusCharacter::ClientCancelTraversal_Implementation()
{
	if (bTraversing)
	{
		EndTraversal();
	}
}

void ACampusCharacter::MulticastStartZip_Implementation(ACampusZipline* Line, int32 FromEnd)
{
	if (HasAuthority() || IsLocallyControlled())
	{
		return;
	}
	StartZip(Line, FromEnd);
}

void ACampusCharacter::StartZip(ACampusZipline* Line, int32 FromEnd)
{
	if (!Line)
	{
		return;
	}
	if (bSliding)
	{
		ApplySliding(false);
	}
	if (bIsCrouched)
	{
		UnCrouch();
	}
	Zip = Line;
	ZipDir = FromEnd == 0 ? 1 : -1;
	ZipS = FromEnd == 0 ? 0.f : Line->Length;
	ZipV = 0.f;
	bFireHeld = false;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->StopMovementImmediately();
	Movement->SetMovementMode(MOVE_Flying);
	bUseControllerRotationYaw = false;
}

void ACampusCharacter::TickZip(float DeltaSeconds)
{
	// three.js Zipline.update: accelerate to the line speed, brake smoothly into the far tower.
	const float Left = ZipDir > 0 ? Zip->Length - ZipS : ZipS;
	const float BrakeV = FMath::Sqrt(2.f * ACampusZipline::Accel * FMath::Max(Left - 150.f, 0.f)) + 150.f;
	ZipV = FMath::Min3(ZipV + ACampusZipline::Accel * DeltaSeconds, ACampusZipline::Speed, BrakeV);
	ZipS = FMath::Clamp(ZipS + ZipDir * ZipV * DeltaSeconds, 0.f, Zip->Length);
	const float Alpha = ZipS / FMath::Max(Zip->Length, 1.f);
	const FVector Feet = Zip->PointAt(Alpha) - FVector(0.f, 0.f, ACampusZipline::Hang);
	const FVector Travel = Zip->TangentAt(Alpha) * ZipDir;
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	SetActorLocationAndRotation(Feet + FVector(0.f, 0.f, HalfHeight), FRotator(0.f, Travel.Rotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	if ((ZipDir > 0 && ZipS >= Zip->Length) || (ZipDir < 0 && ZipS <= 0.f))
	{
		const FVector Land = Zip->Landing(ZipDir > 0 ? 1 : 0);
		Zip = nullptr;
		SetActorLocation(Land + FVector(0.f, 0.f, HalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
		bUseControllerRotationYaw = true;
		UCharacterMovementComponent* Movement = GetCharacterMovement();
		Movement->SetMovementMode(MOVE_Walking);
		Movement->StopMovementImmediately();
	}
}

void ACampusCharacter::ToggleProne()
{
	if (bProne)
	{
		SetProne(false);
		UnCrouch();
		return;
	}
	if (bTraversing || !GetCharacterMovement()->IsMovingOnGround())
	{
		return;
	}
	if (bSprinting)
	{
		SprintReleased();
	}
	Crouch();
	SetProne(true);
}

void ACampusCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	// the capsule centre (boom mount) just dropped by the adjust: raise the boom by the same, this frame
	CameraBoom->AddRelativeLocation(FVector(0.f, 0.f, ScaledHalfHeightAdjust));
}

void ACampusCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	// the capsule centre just rose by the adjust: lower the boom by the same, this frame
	CameraBoom->AddRelativeLocation(FVector(0.f, 0.f, -ScaledHalfHeightAdjust));
}

void ACampusCharacter::SetProne(bool bNewProne)
{
	bProne = bNewProne;
	if (UCampusMovementComponent* Movement = Cast<UCampusMovementComponent>(GetCharacterMovement()))
	{
		Movement->bWantsProne = bNewProne;
		Movement->ProneMaxSpeed = ProneSpeed;
	}
	UpdateMovementSettings();
	if (!HasAuthority())
	{
		ServerSetProne(bProne);
	}
}

void ACampusCharacter::ServerSetProne_Implementation(bool bNewProne)
{
	bProne = bNewProne;
	if (UCampusMovementComponent* Movement = Cast<UCampusMovementComponent>(GetCharacterMovement()))
	{
		Movement->ProneMaxSpeed = ProneSpeed; // bWantsProne itself arrives with each move
	}
	UpdateMovementSettings();
}

void ACampusCharacter::ToggleArmed()
{
	bArmed = !bArmed;
	// the run intent survives the swap: a rifle can't sprint standing, fists sprint straight away while run is held
	bSprinting = bRunHeld && !bArmed;
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
		// Free look moves the camera only: the character keeps its stance, aim and sight (no ADS cancel, which
		// popped the camera out and the body back in - the glitch). Only the trigger pauses while looking around.
		bFireHeld = false;
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
	if (bAim && bFreeLookHeld)
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
		if (AmmoReserve <= 0)
		{
			if (DryFireSound && Now >= NextDryFireTime)
			{
				UGameplayStatics::PlaySound2D(this, DryFireSound);
				NextDryFireTime = Now + 0.3f;
			}
			return;
		}
		Reload();
		return;
	}
	NextFireTime = Now + FireInterval;
	--AmmoInMag;
	if (ShotSound)
	{
		UGameplayStatics::PlaySound2D(this, ShotSound);
	}
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
	if (bHeld != bRunHeld)
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
		if (ReloadSound)
		{
			UGameplayStatics::PlaySound2D(this, ReloadSound);
		}
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
	bSprinting = bRunHeld && !bArmed; // same rule as the owner (ToggleArmed)
	if (!bArmed)
	{
		bAiming = false;
	}
	else
	{
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
	bRunHeld = bNewSprinting;
	bSprinting = bNewSprinting && !bArmed;
	UpdateMovementSettings();
}

void ACampusCharacter::ServerReload_Implementation()
{
	// same rule as the phone's Reload(): only when it changes something and not already reloading (stops a looped
	// RPC from making everyone watch an endless reload)
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bArmed || bDead || Now < ServerReloadEndTime || ServerAmmoInMag >= MagazineSize || ServerAmmoReserve <= 0)
	{
		return;
	}
	const UCampusAnimInstance* Anim = GetCampusAnim();
	// a little shorter than the clip: the phone's own clock and the RPC delay never make a legit shot look early
	ServerReloadEndTime = Now + (Anim ? Anim->GetReloadDuration() : 2.f) * 0.8f;
	const int32 Taken = FMath::Min(MagazineSize - ServerAmmoInMag, ServerAmmoReserve);
	ServerAmmoInMag += Taken;
	ServerAmmoReserve -= Taken;
	MulticastUpperBody(true);
}

void ACampusCharacter::ServerFire_Implementation(FVector_NetQuantize Start, FVector_NetQuantizeNormal Direction)
{
	if (bDead || !bArmed)
	{
		return;
	}
	// The server decides whether this shot could have happened: ammo, fire rate, reload, and a muzzle near the body.
	const float Now = GetWorld()->GetTimeSeconds();
	ServerShotCredit = FMath::Min(3.f, ServerShotCredit + (Now - ServerCreditTime) / FMath::Max(FireInterval, 0.01f));
	ServerCreditTime = Now;
	if (ServerShotCredit < 1.f || Now < ServerReloadEndTime || ServerAmmoInMag <= 0
		|| FVector::DistSquared(FVector(Start), GetActorLocation()) > FMath::Square(250.f))
	{
		return;
	}
	ServerShotCredit -= 1.f;
	--ServerAmmoInMag;
	// The world blocks where it is now; players are checked where the shooter saw them (lag compensation).
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CampusFire), false, this);
	FHitResult Hit;
	const FVector End = FVector(Start) + FVector(Direction) * Range;
	const bool bWall = GetWorld()->LineTraceSingleByObjectType(Hit, Start, End, Objects, Params);
	FVector VictimPoint;
	uint8 Zone = 0;
	ACampusCharacter* Victim = RewoundVictim(Start, Direction, bWall ? Hit.Distance : Range, VictimPoint, Zone);
	if (Victim)
	{
		MulticastFire(VictimPoint, -FVector(Direction), false);
	}
	else
	{
		MulticastFire(bWall ? FVector(Hit.ImpactPoint) : End, bWall ? FVector(Hit.ImpactNormal) : FVector::UpVector, bWall);
		// doors break under fire (same falloff as on players)
		if (ACampusDoor* Door = bWall ? Cast<ACampusDoor>(Hit.GetActor()) : nullptr)
		{
			const float DoorFalloff = FMath::GetMappedRangeValueClamped(FVector2D(FalloffStart, FMath::Max(FalloffEnd, FalloffStart + 1.f)),
				FVector2D(1.f, FalloffMinShare), Hit.Distance);
			Door->ApplyDamage(Damage * DoorFalloff);
		}
		return;
	}
	const ACampusPlayerState* MyState = GetPlayerState<ACampusPlayerState>();
	const ACampusPlayerState* VictimState = Victim->GetPlayerState<ACampusPlayerState>();
	if (MyState && VictimState && MyState->Team == VictimState->Team)
	{
		return;
	}
	// zone (1 head, 2 arms / legs) and distance falloff
	const float ZoneScale = Zone == 1 ? HeadMultiplier : (Zone == 2 ? LimbMultiplier : 1.f);
	const float Distance = FVector::Dist(FVector(Start), VictimPoint);
	const float Falloff = FMath::GetMappedRangeValueClamped(FVector2D(FalloffStart, FMath::Max(FalloffEnd, FalloffStart + 1.f)),
		FVector2D(1.f, FalloffMinShare), Distance);
	const float Dealt = Damage * ZoneScale * Falloff;
	const bool bWasAlive = Victim->GetHealth() > 0.f;
	const float Before = Victim->GetHealth();
	Victim->ApplyHit(Dealt, GetController());
	ClientConfirmHit(bWasAlive && Victim->GetHealth() <= 0.f, Zone == 1, int16(FMath::RoundToInt(Before - Victim->GetHealth())));
}

namespace CampusLagComp
{
	/** History kept per player, and the most a shot may be rewound (higher pings get less help, not more). */
	constexpr double KeepSeconds = 0.5;
	constexpr double MaxRewind = 0.3;
	/** Remote players are drawn a little behind their newest update (movement smoothing). */
	constexpr double ViewDelay = 0.03;
}

void ACampusCharacter::RecordHitbox()
{
	const double Now = GetWorld()->GetTimeSeconds();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	FHitboxFrame& Frame = HitboxHistory.AddDefaulted_GetRef();
	Frame.Time = Now;
	Frame.Center = Capsule->GetComponentLocation();
	Frame.HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	Frame.Radius = Capsule->GetScaledCapsuleRadius();
	int32 Old = 0;
	while (Old < HitboxHistory.Num() - 1 && HitboxHistory[Old].Time < Now - CampusLagComp::KeepSeconds)
	{
		++Old;
	}
	if (Old > 0)
	{
		HitboxHistory.RemoveAt(0, Old, EAllowShrinking::No);
	}
}

ACampusCharacter::FHitboxFrame ACampusCharacter::HitboxAt(double Time) const
{
	if (HitboxHistory.IsEmpty())
	{
		FHitboxFrame Now;
		Now.Center = GetCapsuleComponent()->GetComponentLocation();
		Now.HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Now.Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
		return Now;
	}
	if (Time <= HitboxHistory[0].Time)
	{
		return HitboxHistory[0];
	}
	for (int32 Index = HitboxHistory.Num() - 1; Index > 0; --Index)
	{
		const FHitboxFrame& A = HitboxHistory[Index - 1];
		const FHitboxFrame& B = HitboxHistory[Index];
		if (A.Time <= Time)
		{
			const float Alpha = B.Time > A.Time ? float((Time - A.Time) / (B.Time - A.Time)) : 1.f;
			FHitboxFrame Out;
			Out.Time = Time;
			Out.Center = FMath::Lerp(A.Center, B.Center, FMath::Clamp(Alpha, 0.f, 1.f));
			Out.HalfHeight = FMath::Lerp(A.HalfHeight, B.HalfHeight, Alpha);
			Out.Radius = FMath::Lerp(A.Radius, B.Radius, Alpha);
			return Out;
		}
	}
	return HitboxHistory.Last();
}

ACampusCharacter* ACampusCharacter::RewoundVictim(const FVector& Start, const FVector& Direction, float BlockDistance, FVector& OutPoint, uint8& OutZone) const
{
	OutZone = 0;
	FHitboxFrame BestBox;
	// The shot left the phone half a ping ago, and what it aimed at was already half a ping old there.
	double Rewind = CampusLagComp::ViewDelay;
	if (const APlayerState* State = GetPlayerState())
	{
		Rewind += State->GetPingInMilliseconds() * 0.001;
	}
	if (IsLocallyControlled())
	{
		Rewind = 0.0; // the host's own shots see the server's world directly
	}
	const double When = GetWorld()->GetTimeSeconds() - FMath::Min(Rewind, CampusLagComp::MaxRewind);
	const FVector End = Start + Direction * BlockDistance;

	ACampusCharacter* Best = nullptr;
	float BestDistance = BlockDistance;
	for (TActorIterator<ACampusCharacter> It(GetWorld()); It; ++It)
	{
		ACampusCharacter* Other = *It;
		if (Other == this || Other->bDead)
		{
			continue;
		}
		const FHitboxFrame Box = Other->HitboxAt(When);
		const FVector Half(0.f, 0.f, FMath::Max(Box.HalfHeight - Box.Radius, 0.f));
		FVector OnShot, OnAxis;
		FMath::SegmentDistToSegmentSafe(Start, End, Box.Center - Half, Box.Center + Half, OnShot, OnAxis);
		const float Miss = FVector::Dist(OnShot, OnAxis);
		if (Miss > Box.Radius)
		{
			continue;
		}
		// step back from the closest approach to the capsule's surface
		const float Along = FVector::DotProduct(OnShot - Start, Direction) - FMath::Sqrt(FMath::Square(Box.Radius) - FMath::Square(Miss));
		if (Along >= 0.f && Along < BestDistance)
		{
			BestDistance = Along;
			Best = Other;
			BestBox = Box;
		}
	}
	OutPoint = Start + Direction * BestDistance;
	if (Best && !Best->bProne && BestBox.HalfHeight > 0.f)
	{
		// height on the body: the top ~13% of a standing capsule is the head (a crouched one is shorter, its head
		// a larger share); below the hips are the legs
		const float Height = (OutPoint.Z - (BestBox.Center.Z - BestBox.HalfHeight)) / (2.f * BestBox.HalfHeight);
		const float HeadLine = Best->bIsCrouched ? 0.8f : 0.87f;
		OutZone = Height >= HeadLine ? 1 : (Height < 0.42f && !Best->bIsCrouched ? 2 : 0);
	}
	return Best;
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
	if (RemoteShotSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, RemoteShotSound, GunPoint(), 1.f, 1.f, 0.f, ShotAttenuation);
	}
	SpawnShotFx(End, Normal, bMark);
}

void ACampusCharacter::ClientConfirmHit_Implementation(bool bKill, bool bHead, int16 Amount)
{
	LastHitConfirmTime = GetWorld()->GetTimeSeconds();
	bLastHitKill = bKill;
	bLastHitHead = bHead;
	LastHitDamage = Amount;
	if (HitSound)
	{
		// a headshot rings higher, like the "ding" players listen for
		UGameplayStatics::PlaySound2D(this, HitSound, 1.f, bHead ? 1.6f : 1.f);
	}
}

void ACampusCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (!HasAuthority() || !bTrackingFall)
	{
		return;
	}
	bTrackingFall = false;
	const float Drop = FallTopZ - GetActorLocation().Z;
	if (Drop > SafeFall && !bDead)
	{
		// 0 at SafeFall (4 m), rising evenly to everything at DeadlyFall (15.5 m)
		const float Share = FMath::Clamp((Drop - SafeFall) / FMath::Max(DeadlyFall - SafeFall, 1.f), 0.f, 1.f);
		ApplyHit(MaxHealth * Share, nullptr);
	}
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
	const bool bShow = bArmed && !(IsLocallyControlled() && CameraAimBlend > 0.5f);
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
	constexpr float MaxHeight = 275.f;
	// Sample chooser (CHT_TraversalMontages_CMC): mantle up to 150 cm, climb 150 - 275 cm.
	constexpr float ClimbAbove = 150.f;
	constexpr float WallReach = 110.f;
	constexpr float OntoTop = 35.f;
	// Hurdle (sample: 1 m obstacle, thin): up to this high and this deep.
	constexpr float HurdleMaxHeight = 125.f;
	constexpr float HurdleMaxDepth = 70.f;
	// radius of the wall / top probes: wide enough that a railing's rods and gaps read as one solid face
	constexpr float ProbeRadius = 10.f;
	// cos of the widest angle between our heading and the wall's face direction that still vaults (~65 deg)
	constexpr float MinFacingDot = 0.42f;

	/** Per clip (0-2 mantle stand/walk/run, 3-5 climb), measured from the sample's montages:
	 *  HandsTime = end of the first "FrontLedge" motion-warp window (the hands land on the edge),
	 *  HandForward / HandUp = the hands' position relative to the feet at that moment,
	 *  EndTime = when the clip has settled on top (stand) or shortly after the rise (walk / run). */
	struct FAnchor { float HandsTime; float HandForward; float HandUp; float EndTime; };
	static const FAnchor Anchors[6] =
	{
		{ 0.365f, 29.9f, 114.3f, 1.25f },
		{ 0.811f, 31.7f, 68.7f, 1.30f },
		{ 0.568f, 58.9f, 65.9f, 1.05f },
		{ 0.732f, 31.3f, 151.5f, 2.08f },
		{ 1.294f, 32.5f, 179.9f, 2.95f },
		{ 0.885f, 30.8f, 150.3f, 2.10f },
	};
}

void ACampusCharacter::JumpPressed()
{
	if (bTraversing || Zip)
	{
		return;
	}
	int32 ZipEnd = INDEX_NONE;
	if (ACampusZipline* Line = FindZipline(ZipEnd))
	{
		StartZip(Line, ZipEnd);
		if (HasAuthority())
		{
			MulticastStartZip(Line, ZipEnd);
		}
		else
		{
			ServerStartZip(Line, ZipEnd);
		}
		return;
	}
	if (bSliding)
	{
		SetSliding(false);
		UnCrouch();
		return;
	}
	if (bProne)
	{
		// Jump from prone gets up, like the crouch button from crouch.
		ToggleProne();
		return;
	}
	if (bIsCrouched)
	{
		// From crouch the first jump press stands up; the next one jumps (a takeoff matched to the speed then,
		// or a vault / climb if a wall is in front).
		UnCrouch();
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
	return Clip ? Clip->GetPlayLength() : 0.f;
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

	// 1) A wall at knee height in the direction we are heading: where the stick pushes first (running along the
	//    corridor and cutting toward the parapet), else where we face. A small sphere, not a line: the corridor
	//    railings (grill rods 2.5 - 4 cm with gaps between them) let a thin line slip through. Walls up to ~65
	//    degrees off that heading count; we turn square to the wall for the vault.
	FHitResult Wall;
	const FVector KneeStart = Feet + FVector(0.f, 0.f, 45.f);
	const FCollisionShape Probe = FCollisionShape::MakeSphere(CampusTraversal::ProbeRadius);
	FVector Heading = GetLastMovementInputVector();
	Heading.Z = 0.f;
	TArray<FVector, TInlineAllocator<2>> Headings;
	if (Heading.Normalize(0.09f))
	{
		Headings.Add(Heading);
	}
	if (Headings.IsEmpty() || FVector::DotProduct(Headings[0], Forward) < 0.95f)
	{
		Headings.Add(Forward);
	}
	FVector WallNormal = FVector::ZeroVector;
	for (const FVector& Dir : Headings)
	{
		FHitResult Hit;
		if (!GetWorld()->SweepSingleByChannel(Hit, KneeStart, KneeStart + Dir * CampusTraversal::WallReach, FQuat::Identity, ECC_Visibility, Probe, Params)
			|| Hit.bStartPenetrating || Hit.ImpactNormal.Z > 0.7f)
		{
			continue;
		}
		FVector Normal = Hit.ImpactNormal;
		Normal.Z = 0.f;
		if (Normal.Normalize() && FVector::DotProduct(-Normal, Dir) >= CampusTraversal::MinFacingDot)
		{
			Wall = Hit;
			WallNormal = Normal;
			break;
		}
	}
	if (WallNormal.IsZero())
	{
		return false;
	}

	// 2) The top of it. Swept down with the same small sphere just inside the face, so a 3 cm grill or a
	//    15 cm parapet both report their top (a line 12 cm past the face missed thin railings entirely).
	FHitResult Top;
	const FVector Over = Wall.ImpactPoint - WallNormal * (CampusTraversal::ProbeRadius * 0.5f);
	if (!GetWorld()->SweepSingleByChannel(Top, FVector(Over.X, Over.Y, Feet.Z + CampusTraversal::MaxHeight + 30.f), FVector(Over.X, Over.Y, Feet.Z + 20.f), FQuat::Identity, ECC_Visibility, Probe, Params)
		|| Top.ImpactNormal.Z < 0.5f)
	{
		return false;
	}
	const float Height = Top.ImpactPoint.Z - Feet.Z;
	if (Height < CampusTraversal::MinHeight || Height > CampusTraversal::MaxHeight)
	{
		return false;
	}

	const float Speed = GetVelocity().Size2D();
	const uint8 SpeedTier = Speed < 100.f ? 0 : (Speed < 250.f ? 1 : 2);
	const FVector Facing = -WallNormal;
	const float Yaw = Facing.Rotation().Yaw;
	const float WallDistance = FVector::DotProduct(Wall.ImpactPoint - Feet, Facing);

	// 3a) Thin and low (a railing, a low wall, a window sill): hurdle over it like the sample. The far side may
	//    be lower than where we stand (the corridor parapet: the ground beside the base, or a drop from an upper
	//    floor): the hurdle ends at our own feet height and we simply fall from there.
	if (Height <= CampusTraversal::HurdleMaxHeight)
	{
		float Depth = -1.f;
		bool bDropBeyond = false;
		FHitResult Beyond;
		for (float Step = 20.f; Step <= CampusTraversal::HurdleMaxDepth + 20.f; Step += 10.f)
		{
			const FVector At = Wall.ImpactPoint + Facing * Step;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Beyond, FVector(At.X, At.Y, Top.ImpactPoint.Z + 20.f), FVector(At.X, At.Y, Feet.Z - 60.f), ECC_Visibility, Params);
			if (!bHit || Beyond.ImpactPoint.Z < Top.ImpactPoint.Z - 30.f)
			{
				Depth = Step - 5.f;
				bDropBeyond = !bHit || Beyond.ImpactPoint.Z < Feet.Z - 40.f;
				break;
			}
		}
		if (Depth > 0.f && (bDropBeyond || FMath::Abs(Beyond.ImpactPoint.Z - Feet.Z) < 40.f))
		{
			const FVector LandAt = Wall.ImpactPoint + Facing * (Depth + 70.f);
			const FVector LandCapsule(LandAt.X, LandAt.Y, Feet.Z + HalfHeight + 5.f);
			if (!GetWorld()->OverlapBlockingTestByChannel(LandCapsule, FQuat::Identity, ECC_Pawn, GetCapsuleComponent()->GetCollisionShape(), Params))
			{
				// a drop on the far side (corridor parapet, roof wall): the sample's vault, one hand on top and the
				// legs swung over, cut as it clears and handed to the fall; level on both sides: the hurdle
				const uint8 Kind = (bDropBeyond ? 9 : 6) + SpeedTier;
				if (Anim->GetMantleClip(Kind))
				{
					StartTraversal(Feet, Yaw, Height, WallDistance, Depth, Kind);
					if (HasAuthority())
					{
						MulticastStartTraversal(Feet, Yaw, Height, WallDistance, Depth, Kind);
					}
					else
					{
						ServerStartTraversal(Feet, Yaw, Height, WallDistance, Depth, Kind);
					}
					return true;
				}
			}
		}
	}

	// 3) Room to stand on top.
	const FVector Landing = Top.ImpactPoint - WallNormal * CampusTraversal::OntoTop;
	const FVector StandAt = Landing + FVector(0.f, 0.f, HalfHeight + 2.f);
	if (GetWorld()->OverlapBlockingTestByChannel(StandAt, FQuat::Identity, ECC_Pawn, GetCapsuleComponent()->GetCollisionShape(), Params))
	{
		return false;
	}

	// 4) Pick the clip like the sample: by height (mantle / climb) and approach speed (stand / walk / run).
	const uint8 Kind = (Height > CampusTraversal::ClimbAbove ? 3 : 0) + SpeedTier;
	if (!Anim->GetMantleClip(Kind))
	{
		return false;
	}
	const float LandingDistance = FMath::Max(WallDistance + 10.f, FVector::DotProduct(Landing - Feet, Facing));

	StartTraversal(Feet, Yaw, Height, WallDistance, LandingDistance, Kind);
	if (HasAuthority())
	{
		MulticastStartTraversal(Feet, Yaw, Height, WallDistance, LandingDistance, Kind);
	}
	else
	{
		ServerStartTraversal(Feet, Yaw, Height, WallDistance, LandingDistance, Kind);
	}
	return true;
}

void ACampusCharacter::ServerStartTraversal_Implementation(FVector_NetQuantize StartFeet, float Yaw, float Height, float WallDistance, float LandingDistance, uint8 Kind)
{
	// Loose sanity check: the start must be where the server has us, and the ledge in range.
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (FVector::Dist(FVector(StartFeet), GetActorLocation() - FVector(0.f, 0.f, HalfHeight)) > 150.f
		|| Height < CampusTraversal::MinHeight - 5.f || Height > CampusTraversal::MaxHeight + 5.f || Kind > 11)
	{
		ClientCancelTraversal();
		return;
	}
	StartTraversal(StartFeet, Yaw, Height, WallDistance, LandingDistance, Kind);
	MulticastStartTraversal(StartFeet, Yaw, Height, WallDistance, LandingDistance, Kind);
}

void ACampusCharacter::MulticastStartTraversal_Implementation(FVector_NetQuantize StartFeet, float Yaw, float Height, float WallDistance, float LandingDistance, uint8 Kind)
{
	if (HasAuthority() || IsLocallyControlled())
	{
		return;
	}
	StartTraversal(StartFeet, Yaw, Height, WallDistance, LandingDistance, Kind);
}

void ACampusCharacter::StartTraversal(const FVector& StartFeet, float Yaw, float Height, float WallDistance, float LandingDistance, uint8 Kind)
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
	if (Kind >= 6)
	{
		// Hurdle: highest root = over the obstacle; end a little after the feet are back down.
		TraversalApexTime = 0.f;
		float Best = -1.f;
		const float Length = Clip->GetPlayLength();
		for (int32 Step = 0; Step <= 90; ++Step)
		{
			const float Time = Length * Step / 90.f;
			const float Z = ClipRoot(Clip, Time).Z;
			if (Z > Best)
			{
				Best = Z;
				TraversalApexTime = Time;
			}
		}
		TraversalEnd = Length;
		for (int32 Step = 0; Step <= 90; ++Step)
		{
			const float Time = TraversalApexTime + (Length - TraversalApexTime) * Step / 90.f;
			if (ClipRoot(Clip, Time).Z < 1.f)
			{
				TraversalEnd = FMath::Min(Length, Time + 0.15f);
				break;
			}
		}
		const uint8 Tier = (Kind - 6) % 3;
		TraversalExitSpeed = Tier == 0 ? 0.f : (Tier == 1 ? 200.f : JogSpeed);
	}
	else
	{
		TraversalEnd = FMath::Min(CampusTraversal::Anchors[FMath::Min<int32>(Kind, 5)].EndTime, Clip->GetPlayLength());
	}
	TraversalYaw = Yaw;
	TraversalScaleZ = Height;
	TraversalScaleForward = WallDistance;
	TraversalOverTop = LandingDistance;
	TraversalStartFeet = StartFeet;
	TraversalRootStart = ClipRoot(Clip, 0.f);
	bFireHeld = false;

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

	// Motion warping like the sample: two anchors on the clip's own root path.
	//  - Hands time: the root is placed so the hands sit exactly on the real edge (wall face, ledge height).
	//  - End: standing on top of the ledge, a little past the edge.
	// Between anchors the clip's root motion is stretched per axis, so its timing and arcs are kept.
	if (TraversalKind >= 6)
	{
		// Hurdle: the clip's obstacle front is at root Y = 0 and 100 cm high. Stretch the run-up so that
		// front lands on the real wall, scale the height, and keep the clip's own travel after the apex.
		const float Time = FMath::Min(TraversalTime, TraversalEnd);
		const FVector R0 = TraversalRootStart;
		const FVector RA = ClipRoot(Clip, TraversalApexTime);
		const FVector R = ClipRoot(Clip, Time);
		const float WallDistance = TraversalScaleForward;
		float Forward;
		if (Time <= TraversalApexTime && FMath::Abs(RA.Y - R0.Y) > 1.f)
		{
			Forward = (R.Y - R0.Y) / (RA.Y - R0.Y) * (WallDistance + RA.Y);
		}
		else
		{
			Forward = WallDistance + R.Y;
		}
		const FVector Local(Forward, -(R.X - R0.X), R.Z * TraversalScaleZ / 100.f);
		const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		SetActorLocation(TraversalStartFeet + FRotator(0.f, TraversalYaw, 0.f).RotateVector(Local) + FVector(0.f, 0.f, HalfHeight), false, nullptr, ETeleportType::None);
		if (TraversalTime >= TraversalEnd)
		{
			EndTraversal();
		}
		return;
	}
	const CampusTraversal::FAnchor& A = CampusTraversal::Anchors[FMath::Min<int32>(TraversalKind, 5)];
	const float Height = TraversalScaleZ;
	const float WallDistance = TraversalScaleForward;
	const float LandingDistance = TraversalOverTop;
	const float Time = FMath::Min(TraversalTime, TraversalEnd);

	const FVector R0 = TraversalRootStart;
	const FVector RH = ClipRoot(Clip, A.HandsTime);
	const FVector RE = ClipRoot(Clip, TraversalEnd);
	const FVector R = ClipRoot(Clip, Time);
	const float HandsForward = FMath::Max(WallDistance - A.HandForward, -15.f);
	const float HandsUp = FMath::Max(0.f, Height - A.HandUp);

	auto Remap = [](float Value, float From0, float From1, float To0, float To1)
	{
		const float Span = From1 - From0;
		return FMath::Abs(Span) < 1.f ? To1 : To0 + (Value - From0) / Span * (To1 - To0);
	};
	float Forward, Up;
	if (Time <= A.HandsTime)
	{
		Forward = Remap(R.Y, R0.Y, RH.Y, 0.f, HandsForward);
		// Clips that are still on the ground at the grab (stand mantle) ease up over the approach.
		Up = FMath::Abs(RH.Z - R0.Z) < 1.f ? HandsUp * (Time / FMath::Max(A.HandsTime, 0.01f)) : Remap(R.Z, R0.Z, RH.Z, 0.f, HandsUp);
	}
	else
	{
		Forward = Remap(R.Y, RH.Y, RE.Y, HandsForward, LandingDistance);
		Up = Remap(R.Z, RH.Z, RE.Z, HandsUp, Height);
	}
	// Mesh space: +Y forward, +X to the character's left, +Z up.
	const FVector Local(Forward, -(R.X - R0.X), Up);
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
	if (TraversalKind >= 6)
	{
		// Hurdles and vaults keep the run going (off a vault that is straight into the fall).
		Movement->Velocity = FRotator(0.f, TraversalYaw, 0.f).Vector() * TraversalExitSpeed;
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
	// scoreboard: damage actually taken (capped at what was left) and dealt by the shooter (kill plane = nobody)
	const float Taken = FMath::Min(Health, InDamage);
	if (ACampusPlayerState* VictimState = GetPlayerState<ACampusPlayerState>())
	{
		VictimState->DamageTaken += Taken;
	}
	if (ACampusPlayerState* ShooterState = InstigatorController ? InstigatorController->GetPlayerState<ACampusPlayerState>() : nullptr)
	{
		ShooterState->DamageDealt += Taken;
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
	// crouched: unarmed sprint, or the armed bent-over run (not while aiming down the sight)
	const bool bCrouchRun = bRunHeld && (bArmed ? !bAiming : bSprinting);
	Movement->MaxWalkSpeedCrouched = bCrouchRun ? CrouchSprintSpeed : CrouchSpeed;
	// Sprint only counts while pushing forward; strafing or backing up stays at the normal speed.
	// The server replays a client's moves with ITS speed limits, so both must use the same rule from data both have:
	// the movement acceleration (the server gets it with every move), not the local input vector.
	if (bRunHeld)
	{
		const bool bHeading = CanSprintNow() || (bArmed && bSprintHeading); // (updates bSprintHeading)
		if (bSprinting && !CanSprintNow())
		{
			Movement->MaxWalkSpeed = JogSpeed;
		}
		if (bProne || !bHeading)
		{
			Movement->MaxWalkSpeedCrouched = CrouchSpeed;
		}
	}
	if (bProne)
	{
		Movement->MaxWalkSpeedCrouched = ProneSpeed;
	}
	if (bSliding)
	{
		Movement->MaxWalkSpeedCrouched = SprintSpeed + 100.f;
	}
}

bool ACampusCharacter::CanSprintNow() const
{
	// Forward enough to sprint, with a margin so speed can't flicker on the edge (enter above 0.3, leave below 0.1),
	// and a frame without acceleration keeps the current answer instead of dropping the sprint.
	const FVector Accel = GetCharacterMovement()->GetCurrentAcceleration();
	if (!Accel.IsNearlyZero())
	{
		const float Forward = GetActorRotation().UnrotateVector(Accel.GetSafeNormal()).X;
		bSprintHeading = bSprintHeading ? Forward > 0.1f : Forward > 0.3f;
	}
	return !bArmed && !bProne && bSprintHeading;
}

UCampusAnimInstance* ACampusCharacter::GetCampusAnim() const
{
	return Cast<UCampusAnimInstance>(GetMesh()->GetAnimInstance());
}
