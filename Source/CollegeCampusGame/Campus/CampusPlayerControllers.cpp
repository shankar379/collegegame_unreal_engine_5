#include "Campus/CampusPlayerControllers.h"

#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Campus/CampusAnimInstance.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusGameMode.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusTouchControls.h"
#include "Campus/CampusUI.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	bool IsTouchDevice()
	{
#if PLATFORM_ANDROID || PLATFORM_IOS
		return true;
#else
		return false;
#endif
	}
}

// ---------------------------------------------------------------------------------------------
// Lobby

ACampusLobbyPlayerController::ACampusLobbyPlayerController()
{
	bShowMouseCursor = true;
}

UCampusGameInstance* ACampusLobbyPlayerController::GetCampusInstance() const
{
	return GetGameInstance<UCampusGameInstance>();
}

UCampusSave* ACampusLobbyPlayerController::GetProfile() const
{
	UCampusGameInstance* Instance = GetCampusInstance();
	return Instance ? Instance->GetProfile() : nullptr;
}

void ACampusLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	// The character on the podium (top of the podium is at Z = 30) facing the camera on +X.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Display = GetWorld()->SpawnActor<ASkeletalMeshActor>(FVector(0.f, 0.f, 30.f), FRotator(0.f, 0.f, 0.f), Params);
	if (Display)
	{
		USkeletalMeshComponent* Mesh = Display->GetSkeletalMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin")));
		Mesh->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Mesh->SetAnimInstanceClass(UCampusAnimInstance::StaticClass());
	}

	PodiumCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(380.f, 0.f, 135.f), FRotator(-6.f, 180.f, 0.f), Params);
	if (PodiumCamera)
	{
		PodiumCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
		PodiumCamera->GetCameraComponent()->SetFieldOfView(70.f);
		SetViewTarget(PodiumCamera);
	}

	Widget = SNew(SCampusLobbyWidget).Controller(this);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(), 10);
	}
	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void ACampusLobbyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Widget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACampusLobbyPlayerController::RotateDisplay(float DeltaYaw)
{
	if (Display)
	{
		Display->AddActorWorldRotation(FRotator(0.f, DeltaYaw, 0.f));
	}
}

void ACampusLobbyPlayerController::HostRoom()
{
	UGameplayStatics::OpenLevel(this, FName(UCampusGameInstance::MatchMap), true, TEXT("listen"));
}

void ACampusLobbyPlayerController::JoinRoom()
{
	const FString Address = GetDefault<UCampusNetSettings>()->GameAddress();
	ClientTravel(Address, TRAVEL_Absolute);
}

// ---------------------------------------------------------------------------------------------
// Match

UCampusSave* ACampusMatchPlayerController::GetProfile() const
{
	UCampusGameInstance* Instance = GetGameInstance<UCampusGameInstance>();
	return Instance ? Instance->GetProfile() : nullptr;
}

void ACampusMatchPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	if (const UCampusSave* Profile = GetProfile())
	{
		ServerSetName(Profile->PlayerName);
	}

	if (GEngine && GEngine->GameViewport)
	{
		TouchControls = SNew(SCampusTouchControls).Controller(this).ShowButtons(IsTouchDevice());
		GEngine->GameViewport->AddViewportWidgetContent(TouchControls.ToSharedRef(), 5);
		Widget = SNew(SCampusMatchWidget).Controller(this);
		GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(), 10);
	}
}

void ACampusMatchPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GEngine && GEngine->GameViewport)
	{
		if (Widget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
		}
		if (TouchControls.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(TouchControls.ToSharedRef());
		}
	}
	Widget.Reset();
	TouchControls.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACampusMatchPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController())
	{
		return;
	}
	const ACampusGameState* State = GetWorld()->GetGameState<ACampusGameState>();
	ApplyInputMode(State && State->Phase == ECampusPhase::Playing);
}

void ACampusMatchPlayerController::ApplyInputMode(bool bGameplay)
{
	if (AppliedMode == int32(bGameplay))
	{
		return;
	}
	AppliedMode = int32(bGameplay);

	if (!bGameplay)
	{
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		bShowMouseCursor = true;
		SetIgnoreMoveInput(true);
	}
	else if (IsTouchDevice())
	{
		// On-screen controls are Slate widgets, so the UI must keep receiving touches.
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		bShowMouseCursor = false;
		SetIgnoreMoveInput(false);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		SetIgnoreMoveInput(false);
	}
	if (TouchControls.IsValid())
	{
		TouchControls->SetActive(bGameplay);
	}
}

void ACampusMatchPlayerController::ServerSetName_Implementation(const FString& Name)
{
	if (PlayerState && !Name.IsEmpty())
	{
		PlayerState->SetPlayerName(Name.Left(16));
	}
}

void ACampusMatchPlayerController::ServerRequestStart_Implementation()
{
	if (ACampusMatchGameMode* GameMode = GetWorld()->GetAuthGameMode<ACampusMatchGameMode>())
	{
		GameMode->RequestStart(this);
	}
}

void ACampusMatchPlayerController::ServerSwitchTeam_Implementation()
{
	if (ACampusMatchGameMode* GameMode = GetWorld()->GetAuthGameMode<ACampusMatchGameMode>())
	{
		GameMode->SwitchTeam(this);
	}
}

int32 ACampusMatchPlayerController::ComputeReward() const
{
	const ACampusGameState* State = GetWorld()->GetGameState<ACampusGameState>();
	const ACampusPlayerState* Mine = GetPlayerState<ACampusPlayerState>();
	if (!State || !Mine)
	{
		return 0;
	}
	const int32 Base = State->Winner < 0 ? 60 : (State->Winner == int32(Mine->Team) ? 100 : 40);
	return Base + 10 * Mine->Kills;
}

void ACampusMatchPlayerController::ClaimAndLeave()
{
	if (UCampusSave* Profile = GetProfile())
	{
		const ACampusGameState* State = GetWorld()->GetGameState<ACampusGameState>();
		const ACampusPlayerState* Mine = GetPlayerState<ACampusPlayerState>();
		Profile->Coins += ComputeReward();
		Profile->Xp += 50;
		Profile->Matches += 1;
		Profile->Wins += (State && Mine && State->Winner == int32(Mine->Team)) ? 1 : 0;
		Profile->Kills += Mine ? Mine->Kills : 0;
		Profile->Persist();
	}
	LeaveToLobby();
}

void ACampusMatchPlayerController::LeaveToLobby()
{
	UGameplayStatics::OpenLevel(this, FName(UCampusGameInstance::LobbyMap));
}
