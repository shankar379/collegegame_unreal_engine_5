#include "Campus/CampusPlayerControllers.h"

#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Campus/CampusAnimInstance.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusGameMode.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusParty.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusTouchControls.h"
#include "Campus/CampusUI.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/VoiceInterface.h"
#include "OnlineSubsystemUtils.h"
#if PLATFORM_ANDROID
#include "AndroidPermissionFunctionLibrary.h"
#include "AndroidPermissionCallbackProxy.h"
#endif

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

namespace CampusLobbyScene
{
	/** Where the squad stands, in cm either side of the lobby centre, per slot step.
	 *
	 *  This has to agree with the lobby UI's SquadSlotFraction (0.16 of the viewport width) or a
	 *  name plate would drift off its character. The camera sits 380 cm in front of the line-up at
	 *  70 degrees horizontal FOV, so the visible half-width at that distance is
	 *      380 * tan(35 deg) = 266 cm  ==  half the screen,
	 *  and one slot step of 0.16 screen widths is 0.16 * 2 * 266 = 85 cm.
	 *  The camera looks along -X, so its right vector is -Y: a slot to the player's right is -Y. */
	constexpr float SlotSpacing = 85.f;

	/** The character mesh; every squad member uses it until there is per-player character art. */
	const TCHAR* MeshPath = TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin");
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

UCampusParty* ACampusLobbyPlayerController::GetParty() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusParty>() : nullptr;
}

void ACampusLobbyPlayerController::RemovePodium()
{
	// L_Lobby ships a /Engine/BasicShapes/Cylinder static mesh actor (material MI_Podium) under the
	// character. Free Fire's lobby has no pedestal, so it goes - done here rather than by editing
	// the .umap so the level asset stays untouched and the change is reviewable in code.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (!Actor || !Actor->GetStaticMeshComponent())
		{
			continue;
		}
		const UStaticMesh* Mesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
		const FString MeshName = Mesh ? Mesh->GetPathName() : FString();
		const bool bPodiumMesh = MeshName.Contains(TEXT("BasicShapes/Cylinder"));
		const bool bPodiumName = Actor->GetName().Contains(TEXT("Podium"));
		if (bPodiumMesh || bPodiumName)
		{
			Actor->Destroy();
		}
	}
}

float ACampusLobbyPlayerController::GroundAt(const FVector& Where) const
{
	// The podium is already gone by now, so this finds the lobby floor plane. Falls back to Z = 0.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}
	FHitResult Hit;
	const FVector Start(Where.X, Where.Y, 400.f);
	const FVector End(Where.X, Where.Y, -400.f);
	FCollisionQueryParams Params(TEXT("CampusLobbyFloor"), false);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return Hit.ImpactPoint.Z;
	}
	return 0.f;
}

void ACampusLobbyPlayerController::RebuildSquad()
{
	UWorld* World = GetWorld();
	const UCampusParty* Party = GetParty();
	if (!World || !Party)
	{
		return;
	}
	const int32 Capacity = Party->GetCapacity();
	const TArray<FCampusPartyMember>& Members = Party->GetMembers();

	TArray<int32> Order;
	UCampusParty::BuildFillOrder(Capacity, Order);

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, CampusLobbyScene::MeshPath);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// One actor per member, re-used across rebuilds: changing squad size never respawns the leader.
	for (int32 Index = 0; Index < UCampusParty::MaxMembers; ++Index)
	{
		const bool bWanted = Members.IsValidIndex(Index) && Members[Index].bAccepted && Index < Capacity;
		if (!Squad.IsValidIndex(Index))
		{
			Squad.SetNum(UCampusParty::MaxMembers);
		}
		ASkeletalMeshActor* Actor = Squad[Index];
		if (!bWanted)
		{
			if (Actor)
			{
				Actor->SetActorHiddenInGame(true);
			}
			continue;
		}
		if (!Actor)
		{
			Actor = World->SpawnActor<ASkeletalMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Actor)
			{
				continue;
			}
			USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetSkeletalMeshAsset(Mesh);
			// The mesh faces +X in its own space; the camera looks back along -X.
			Component->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
			Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Component->SetAnimInstanceClass(UCampusAnimInstance::StaticClass());
			Squad[Index] = Actor;
		}
		const int32 Slot = Order.IsValidIndex(Index) ? Order[Index] : Index;
		const float Y = -UCampusParty::SlotStep(Slot, Capacity) * CampusLobbyScene::SlotSpacing;
		const FVector Where(0.f, Y, 0.f);
		Actor->SetActorHiddenInGame(false);
		Actor->SetActorLocation(FVector(0.f, Y, GroundAt(Where)));
		// Everyone keeps the yaw the player dragged to, so the line-up turns as one.
		Actor->SetActorRotation(FRotator(0.f, DisplayYaw, 0.f));
	}
}

void ACampusLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	RemovePodium();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PodiumCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(380.f, 0.f, 135.f), FRotator(-6.f, 180.f, 0.f), Params);
	if (PodiumCamera)
	{
		PodiumCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
		PodiumCamera->GetCameraComponent()->SetFieldOfView(70.f);
		SetViewTarget(PodiumCamera);
	}

	// The squad roster lives on the game instance, so it is already filled in when we come back
	// from a match. The UI's SyncParty() adds the local player as leader on its first build.
	if (UCampusParty* Party = GetParty())
	{
		PartyChangedHandle = Party->OnChanged.AddUObject(this, &ACampusLobbyPlayerController::RebuildSquad);
	}

	// back in the lobby after hosting: that room is over (an unplayed one refunds its Room Card)
	if (UCampusBackend* Backend = GetBackend())
	{
		Backend->CloseHostedRoom();
	}

	Widget = SNew(SCampusLobbyWidget).Controller(this);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(), 10);
	}
	RebuildSquad();
	FInputModeUIOnly Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void ACampusLobbyPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PartyChangedHandle.IsValid())
	{
		if (UCampusParty* Party = GetParty())
		{
			Party->OnChanged.Remove(PartyChangedHandle);
		}
		PartyChangedHandle.Reset();
	}
	if (Widget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACampusLobbyPlayerController::RotateDisplay(float DeltaYaw)
{
	DisplayYaw = FRotator::NormalizeAxis(DisplayYaw + DeltaYaw);
	for (ASkeletalMeshActor* Member : Squad)
	{
		if (Member)
		{
			Member->SetActorRotation(FRotator(0.f, DisplayYaw, 0.f));
		}
	}
}

UCampusBackend* ACampusLobbyPlayerController::GetBackend() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
}

void ACampusLobbyPlayerController::HostRoom(TFunction<void(const FString& Error)> OnFailed)
{
	const UCampusSave* Profile = GetProfile();
	UCampusBackend* Backend = GetBackend();
	if (!Profile || !Backend)
	{
		OnFailed(TEXT("Not ready yet."));
		return;
	}
	const FString Map = Profile->MatchMap.ToString();
	const int32 Team = Profile->TeamSize;
	const int32 Rounds = FMath::Clamp(Profile->MatchRounds, 4, 7);
	const int32 Minutes = FMath::Clamp(Profile->RoundMinutes, 3, 10);
	// the API issues the code (and takes the Room Card) first; then this phone opens the match and hosts it
	TWeakObjectPtr<ACampusLobbyPlayerController> WeakThis(this);
	Backend->CreateRoom(Map, Team, Rounds, Minutes, [WeakThis, Map, Team, Rounds, Minutes, OnFailed](bool bOk, const FString& Error, const FCampusRoom& Room)
	{
		ACampusLobbyPlayerController* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		if (!bOk)
		{
			OnFailed(Error);
			return;
		}
		const FString Options = FString::Printf(TEXT("listen?Map=%s?Team=%d?Room=%s?Rounds=%d?Minutes=%d"), *Map, Team, *Room.Code, Rounds, Minutes);
		UGameplayStatics::OpenLevel(Self, FName(UCampusGameInstance::MatchMap), true, Options);
	});
}

void ACampusLobbyPlayerController::JoinRoomByCode(const FString& Code, TFunction<void(const FString& Error)> OnFailed)
{
	UCampusBackend* Backend = GetBackend();
	if (!Backend)
	{
		OnFailed(TEXT("Not ready yet."));
		return;
	}
	TWeakObjectPtr<ACampusLobbyPlayerController> WeakThis(this);
	Backend->JoinRoom(Code, [WeakThis, OnFailed](bool bOk, const FString& Error, const FCampusRoom& Room)
	{
		if (ACampusLobbyPlayerController* Self = WeakThis.Get())
		{
			if (!bOk)
			{
				OnFailed(Error);
				return;
			}
			// the ticket rides along as a URL option; the host checks it with the API (ACampusMatchGameMode::AdmitWithTicket)
			Self->ClientTravel(FString::Printf(TEXT("%s:%d?Ticket=%s"), *Room.Address, Room.Port, *Room.Ticket), TRAVEL_Absolute);
		}
	});
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
		// The saved settings decide how a match starts (gear panel in the lobby or the HUD).
		CampusOptions::ApplyGraphics(Profile);
		bSpeakerOn = Profile->bSpeakerDefaultOn;
		SetSpeakerOn(Profile->bSpeakerDefaultOn);
		if (Profile->bMicDefaultOn)
		{
			SetMicOn(true);
		}
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
	// With the gear panel open the HUD stands down: held fingers are released and the taps go to
	// the panel instead of the controls underneath it.
	ApplyInputMode(State && State->Phase == ECampusPhase::Playing && !bSettingsOpen);
	if (State && State->Phase == ECampusPhase::Ended)
	{
		RecordResult();
	}
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
	RecordResult();
	LeaveToLobby();
}

void ACampusMatchPlayerController::RecordResult()
{
	// once per match, and as soon as the match ends (PlayerTick): if the host leaves first, everyone else is
	// dropped back to the lobby, and their result must already be saved by then
	if (bResultRecorded)
	{
		return;
	}
	bResultRecorded = true;
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
}

float ACampusMatchPlayerController::GetLookSensitivity() const
{
	const UCampusSave* Profile = GetProfile();
	return Profile ? FMath::Clamp(Profile->LookSensitivity, 0.4f, 2.5f) : 1.f;
}

void ACampusMatchPlayerController::LeaveToLobby()
{
	// Hand the squad we just played with to the lobby, so those players are standing side by side
	// when it loads. This is the only real "who is in my party" data the game has today.
	if (UCampusParty* Party = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusParty>() : nullptr)
	{
		const ACampusPlayerState* Mine = GetPlayerState<ACampusPlayerState>();
		if (const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr; State && Mine)
		{
			Party->AdoptSquad(State, Mine->GetPlayerName(), uint8(Mine->Team));
		}
	}
	bSettingsOpen = false;
	UGameplayStatics::OpenLevel(this, FName(UCampusGameInstance::LobbyMap));
}

// ---------------------------------------------------------------------------------------------
// Team voice

namespace CampusVoice
{
	const TCHAR* MicPermission = TEXT("android.permission.RECORD_AUDIO");
}

void ACampusMatchPlayerController::SetMicOn(bool bOn)
{
#if PLATFORM_ANDROID
	if (bOn && !UAndroidPermissionFunctionLibrary::CheckPermission(CampusVoice::MicPermission))
	{
		if (UAndroidPermissionCallbackProxy* Ask = UAndroidPermissionFunctionLibrary::AcquirePermissions({ CampusVoice::MicPermission }))
		{
			Ask->OnPermissionsGrantedDynamicDelegate.AddDynamic(this, &ACampusMatchPlayerController::OnMicPermission);
		}
		return; // turns on once granted
	}
#endif
	bMicOn = bOn;
	// push-to-talk session: only transmits between StartTalking and StopTalking
	if (bOn)
	{
		StartTalking();
	}
	else
	{
		StopTalking();
	}
}

void ACampusMatchPlayerController::OnMicPermission(const TArray<FString>& Permissions, const TArray<bool>& Granted)
{
	if (Granted.Num() > 0 && Granted[0])
	{
		SetMicOn(true);
	}
}

void ACampusMatchPlayerController::SetSpeakerOn(bool bOn)
{
	bSpeakerOn = bOn;
	IOnlineVoicePtr Voice = Online::GetVoiceInterface(GetWorld());
	const AGameStateBase* State = GetWorld()->GetGameState();
	if (!Voice.IsValid() || !State)
	{
		return;
	}
	for (const APlayerState* Other : State->PlayerArray)
	{
		if (Other && Other != PlayerState && Other->GetUniqueId().IsValid())
		{
			if (bOn)
			{
				Voice->UnmuteRemoteTalker(0, *Other->GetUniqueId(), false);
			}
			else
			{
				Voice->MuteRemoteTalker(0, *Other->GetUniqueId(), false);
			}
		}
	}
}
