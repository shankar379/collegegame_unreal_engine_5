#include "Campus/CampusPlayerControllers.h"

#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Campus/CampusAnimInstance.h"
#include "Campus/CampusBR.h"
#include "Campus/CampusBRHud.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusEOS.h"
#include "Campus/CampusGameInstance.h"
#include "Campus/CampusGameMode.h"
#include "Campus/CampusMatchState.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusParty.h"
#include "Campus/CampusSave.h"
#include "Campus/CampusSocial.h"
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
	/** WHERE THE LINE-UP STANDS now lives in one place, CampusLineUp::SlotPose, because the 3D figures
	 *  and their name plates both have to read it or a plate drifts off its character. The camera
	 *  numbers below have to agree with that table (380 cm out, 70 degrees horizontal FOV). */

	/** The character mesh; every squad member uses it until there is per-player character art. */
	const TCHAR* MeshPath = TEXT("/Game/CampusCharacter/SKM_CampusMannequin.SKM_CampusMannequin");

	/** How long after a rebuild the line-up keeps re-checking its own footing.
	 *
	 *  A figure placed on the frame it was spawned is measured in its REFERENCE pose - the only pose
	 *  it has. A frame later the anim graph has evaluated A_Idle and the real pose may sit higher or
	 *  lower, and nothing used to come back and correct it. Teammates got away with it because adding
	 *  or dropping one fires another rebuild; the player standing ALONE in Solo never got a second
	 *  rebuild, so the ref-pose answer was the one that stuck and only Solo floated. */
	constexpr float RegroundWindow = 0.3f;
}

// ---------------------------------------------------------------------------------------------
// Lobby

ACampusLobbyPlayerController::ACampusLobbyPlayerController()
{
	bShowMouseCursor = true;
	// The line-up re-checks its footing for a moment after every rebuild; see RegroundWindow.
	PrimaryActorTick.bCanEverTick = true;
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

UCampusSocial* ACampusLobbyPlayerController::GetSocial() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusSocial>() : nullptr;
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

float ACampusLobbyPlayerController::ReadFloorHeight() const
{
	// L_Lobby's whole floor is ONE StaticMeshActor ("Floor", /Engine/BasicShapes/Plane, scaled 80).
	// That mesh has no collision primitive, so nothing can be traced against it - its actor height is
	// the only honest answer, and it costs one iteration when the level opens.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.f;
	}
	float Found = 0.f;
	bool bAny = false;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const AStaticMeshActor* Actor = *It;
		if (!Actor || !Actor->GetStaticMeshComponent())
		{
			continue;
		}
		const UStaticMesh* Mesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
		const FString MeshName = Mesh ? Mesh->GetPathName() : FString();
		// The mesh path, not the label: GetActorLabel is editor-only data and does not survive a cook,
		// so on the phone the plane mesh is the only thing left to recognise the floor by.
		if (!MeshName.Contains(TEXT("BasicShapes/Plane")) && !Actor->GetName().Contains(TEXT("Floor")))
		{
			continue;
		}
		// The HIGHEST floor plane that is still at roughly standing height: a skybox plane far above
		// the lobby is not the thing the squad stands on.
		const float Z = Actor->GetActorLocation().Z;
		if (Z < 300.f && (!bAny || Z > Found))
		{
			Found = Z;
			bAny = true;
		}
	}
	return Found;
}

float ACampusLobbyPlayerController::GroundAt(const FVector& Where) const
{
	// Traced AT THE SLOT and never cached: the old code measured once at the lobby centre and reused
	// that one height for all four slots and for the rest of the session, so a floor that is not dead
	// flat - or a first measurement taken before the podium was gone - left the line-up hanging.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FloorPlaneZ;
	}
	FHitResult Hit;
	// From just over head height rather than from 400 up: a ceiling or an overhead prop would
	// otherwise be the first thing the ray meets, and the whole line-up would hang off it.
	const FVector Start(Where.X, Where.Y, FloorPlaneZ + 240.f);
	const FVector End(Where.X, Where.Y, FloorPlaneZ - 500.f);
	FCollisionQueryParams Params(TEXT("CampusLobbyFloor"), false);
	// never land on a squad member (a hidden one still had collision: the line-up stood on invisible teammates)
	for (const TObjectPtr<ASkeletalMeshActor>& Member : Squad)
	{
		if (Member)
		{
			Params.AddIgnoredActor(Member);
		}
	}
	// BY OBJECT TYPE, not by channel. A channel trace returns anything that BLOCKS WorldStatic - a
	// character capsule included, since the Pawn profile blocks it - so a figure standing at the spot
	// being measured could be mistaken for the ground under it. Only world geometry is floor.
	if (World->LineTraceSingleByObjectType(Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		return Hit.ImpactPoint.Z;
	}
	return FloorPlaneZ;
}

void ACampusLobbyPlayerController::PlaceMember(ASkeletalMeshActor* Actor, int32 SlotIndex) const
{
	if (!Actor)
	{
		return;
	}
	const CampusLineUp::FSlotPose& Pose = CampusLineUp::SlotPose(SlotIndex);
	// The camera looks down -X, so ITS right is -Y: a figure to the player's right on screen is -Y.
	// Depth is towards the camera, which is +X.
	const float X = Pose.DepthCm;
	const float Y = -Pose.LateralCm;
	const float Ground = GroundAt(FVector(X, Y, 0.f));
	Actor->SetActorLocation(FVector(X, Y, Ground), false, nullptr, ETeleportType::TeleportPhysics);

	// THEN PLANT THE FEET. Putting the actor's origin on the floor only stands the figure up if the
	// asset's origin is exactly at the soles - the gameplay character does not assume that either, it
	// sinks the mesh inside its capsule by a measured amount (see ACampusCharacter's constructor).
	// Here the measurement comes from the mesh's own bounds in the pose it is actually drawn in, so
	// the feet touch the floor whatever the clip's root does and whatever the asset was authored at.
	USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
	if (!Component || !Component->GetSkeletalMeshAsset())
	{
		return;
	}
	// CalcBounds rather than the cached Bounds: it answers for the pose the component holds right now,
	// and it is the same call the renderer uses. On the frame the actor is spawned that is only the
	// REFERENCE pose - which is why RebuildSquad keeps re-grounding for a moment afterwards.
	const FBoxSphereBounds Posed = Component->CalcBounds(Component->GetComponentTransform());
	const float SoleZ = float(Posed.Origin.Z - Posed.BoxExtent.Z);
	const float Lift = Ground - SoleZ;
	// Half a centimetre of slop is below anything the lobby camera can show; only correct a real gap.
	if (!FMath::IsNearlyZero(Lift, 0.5f))
	{
		Actor->SetActorLocation(FVector(X, Y, Ground + Lift), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ACampusLobbyPlayerController::GroundLineUp() const
{
	// Every visible figure, re-measured where it stands. Cheap: at most four traces and four bounds.
	for (int32 Index = 0; Index < Squad.Num(); ++Index)
	{
		ASkeletalMeshActor* Actor = Squad[Index];
		if (Actor && !Actor->IsHidden())
		{
			PlaceMember(Actor, bProfileFocus ? CampusLineUp::MainSlot : CampusLineUp::SlotForMember(Index));
		}
	}
}

void ACampusLobbyPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (RegroundTime < 0.f)
	{
		return;
	}
	// The window closes on its own; inside it the dead-band in PlaceMember means the figures only
	// actually move on the frame the real pose first disagrees with the reference pose.
	RegroundTime += DeltaSeconds;
	GroundLineUp();
	if (RegroundTime >= CampusLobbyScene::RegroundWindow)
	{
		RegroundTime = -1.f;
	}
}

void ACampusLobbyPlayerController::RebuildSquad()
{
	UWorld* World = GetWorld();
	const UCampusParty* Party = GetParty();
	if (!World || !Party)
	{
		return;
	}
	// The profile page owns the scene while it is open (one figure, its own camera). A party event
	// arriving behind it must not un-hide the line-up under the panel.
	if (bProfileFocus)
	{
		return;
	}
	const int32 Capacity = Party->GetCapacity();
	const TArray<FCampusPartyMember>& Members = Party->GetMembers();

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
			// display only: no collision, so nothing (floor traces, each other) can stack them up
			Actor->SetActorEnableCollision(false);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetSkeletalMeshAsset(Mesh);
			// The mesh faces +X in its own space; the camera looks back along -X.
			Component->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
			Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Component->SetAnimInstanceClass(UCampusAnimInstance::StaticClass());
			Squad[Index] = Actor;
		}
		// MEMBER 0 IS ALWAYS THE LOCAL PLAYER AND ALWAYS LANDS ON MainSlot, so changing the squad size
		// only shows or hides the other three - it never shuffles the player sideways.
		const int32 Slot = CampusLineUp::SlotForMember(Index);
		Actor->SetActorHiddenInGame(false);
		// Everyone keeps the yaw the player dragged to, plus their own slot's turn-in. Set BEFORE the
		// placement, because the placement measures the figure where it is actually standing.
		Actor->SetActorRotation(FRotator(0.f, DisplayYaw + CampusLineUp::SlotFacingYaw(Slot), 0.f));
		// One trace PER SLOT, every rebuild. A mode change moves every figure, so every figure is
		// re-grounded; nothing is carried over from a height measured somewhere else, earlier.
		PlaceMember(Actor, Slot);
	}

	// Open the re-grounding window: the figures just spawned or just moved are measured in whatever
	// pose they hold on THIS frame, which for a newly spawned one is only the reference pose.
	RegroundTime = 0.f;
}

void ACampusLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	RemovePodium();
	// Read the floor AFTER the podium is gone, so its top can never be mistaken for the floor.
	FloorPlaneZ = ReadFloorHeight();

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

	// The social channel follows the screen: one socket, opened once we are signed in, telling the
	// server we are in the lobby so friends see "IN LOBBY" and an invite is not spent on a ghost.
	if (UCampusSocial* Social = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusSocial>() : nullptr)
	{
		Social->SetScreenState(ECampusPresence::Lobby);
		if (const UCampusBackend* Backend = GetBackend(); Backend && Backend->IsSignedIn())
		{
			Social->Start();
		}
	}

	// Warm the EOS login up while the player is reading the lobby, so CREATE and JOIN do not wait on
	// it. Does nothing at all when internet play is not configured, and never blocks or shows an error.
	if (UCampusEOS* Eos = GetEos(); Eos && Eos->IsConfigured())
	{
		Eos->EnsureLogin(nullptr);
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
	for (int32 Index = 0; Index < Squad.Num(); ++Index)
	{
		if (ASkeletalMeshActor* Member = Squad[Index])
		{
			// The whole line-up turns as one, but each figure keeps its own turn-in, or dragging would
			// flatten the group back into a rigid row.
			const int32 Slot = bProfileFocus ? CampusLineUp::MainSlot : CampusLineUp::SlotForMember(Index);
			Member->SetActorRotation(FRotator(0.f, DisplayYaw + CampusLineUp::SlotFacingYaw(Slot), 0.f));
		}
	}
}

void ACampusLobbyPlayerController::FocusProfile(bool bOn)
{
	if (bProfileFocus == bOn)
	{
		return;
	}
	bProfileFocus = bOn;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (bOn)
	{
		// Hide every slot but the first - the same SetActorHiddenInGame mechanism RebuildSquad already
		// uses for unwanted slots, so there is nothing new to get wrong.
		for (int32 Index = 0; Index < Squad.Num(); ++Index)
		{
			if (Squad[Index])
			{
				Squad[Index]->SetActorHiddenInGame(Index != 0);
			}
		}
		if (Squad.IsValidIndex(0) && Squad[0])
		{
			// The player does not move for their own profile page either: they stay on MainSlot and the
			// camera comes to them. Grounded by the same path the line-up uses.
			PlaceMember(Squad[0], CampusLineUp::MainSlot);
			RegroundTime = 0.f;
		}
		if (!ProfileCamera)
		{
			// The player stays on MainSlot, so this camera has to come to THEM - it can no longer
			// assume the figure is standing at the lobby centre. Measured at the figure's own depth:
			// the visible half-width at distance d is d * tan(35), and sliding the camera a quarter of
			// a screen width along its right vector (which is -Y, since it looks down -X) puts the
			// figure about 25 % across and leaves the right half of the screen for the panel.
			const CampusLineUp::FSlotPose& Main = CampusLineUp::SlotPose(CampusLineUp::MainSlot);
			const float Distance = 380.f - Main.DepthCm;
			const float Shift = 0.25f * 2.f * Distance * 0.70021f;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ProfileCamera = World->SpawnActor<ACameraActor>(FVector(380.f, -Main.LateralCm - Shift, 135.f),
				FRotator(-6.f, 180.f, 0.f), Params);
			if (ProfileCamera)
			{
				ProfileCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
				ProfileCamera->GetCameraComponent()->SetFieldOfView(70.f);
			}
		}
		if (ProfileCamera)
		{
			SetViewTargetWithBlend(ProfileCamera, 0.25f);
		}
		return;
	}
	if (PodiumCamera)
	{
		SetViewTargetWithBlend(PodiumCamera, 0.25f);
	}
	// RebuildSquad puts every slot back where it belongs and re-hides the ones the squad size excludes.
	RebuildSquad();
}

UCampusBackend* ACampusLobbyPlayerController::GetBackend() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
}

UCampusEOS* ACampusLobbyPlayerController::GetEos() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusEOS>() : nullptr;
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
	// With a server party the ROOM MUST MATCH THE SQUAD: the API refuses the hand-off with
	// 409 MODE_MISMATCH if team_size and the party mode disagree, and it is far better to create the
	// right room than to create the wrong one and discover it after the Room Card is spent.
	const UCampusSocial* Social = GetSocial();
	const bool bPartyLeader = Social && Social->HasParty() && Social->IsPartyLeader();
	const int32 Team = bPartyLeader ? Social->GetServerParty().Mode : Profile->TeamSize;
	const int32 Rounds = FMath::Clamp(Profile->MatchRounds, 4, 7);
	const int32 Minutes = FMath::Clamp(Profile->RoundMinutes, 3, 10);
	TWeakObjectPtr<ACampusLobbyPlayerController> WeakThis(this);

	// EosPuid empty = host on the LAN exactly as before. Non-empty = this phone is EOS-logged-in, the
	// API lists the room as an EOS room, and the match listens on the EOS net driver instead.
	auto Create = [WeakThis, Map, Team, Rounds, Minutes, OnFailed](const FString& EosPuid)
	{
		ACampusLobbyPlayerController* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		UCampusBackend* Api = Self->GetBackend();
		if (!Api)
		{
			OnFailed(TEXT("Not ready yet."));
			return;
		}
		// the API issues the code (and takes the Room Card) first; then this phone opens the match and hosts it
		Api->CreateRoom(Map, Team, Rounds, Minutes, EosPuid, [WeakThis, Map, Team, Rounds, Minutes, EosPuid, OnFailed](bool bOk, const FString& Error, const FCampusRoom& Room)
		{
			ACampusLobbyPlayerController* Me = WeakThis.Get();
			if (!Me)
			{
				return;
			}
			if (!bOk)
			{
				OnFailed(Error);
				return;
			}
			// With no puid we tell the net driver to use plain IP sockets, so a LAN room is hosted on
			// 7777 as it always was even when EOS is configured but this phone could not log in.
			const TCHAR* Transport = EosPuid.IsEmpty() ? TEXT("?bUseIPSockets") : TEXT("");
			const FString Options = FString::Printf(TEXT("listen%s?Map=%s?Team=%d?Room=%s?Rounds=%d?Minutes=%d"), Transport, *Map, Team, *Room.Code, Rounds, Minutes);

			// THE PARTY HAND-OFF. Tell the API which room this squad is playing, and every member's
			// phone gets party.room_ready and joins by itself - nobody reads six characters aloud.
			// The server does not join anybody: each member calls the existing POST /v1/rooms/join, so
			// the ticket, the max_players check and the admit step stay byte-for-byte what already
			// works today. We travel either way - a failed hand-off must not cost the host a Room
			// Card, and the code still works by hand.
			UCampusSocial* Squad = Me->GetSocial();
			if (Squad && Squad->HasParty() && Squad->IsPartyLeader())
			{
				Squad->HandOffRoom(Room.Code, [WeakThis, Options](bool bOk, const FString& Error)
				{
					ACampusLobbyPlayerController* Host = WeakThis.Get();
					if (!Host)
					{
						return;
					}
					if (!bOk)
					{
						UE_LOG(LogTemp, Warning, TEXT("[CampusSocial] party hand-off failed: %s"), *Error);
					}
					UGameplayStatics::OpenLevel(Host, FName(UCampusGameInstance::MatchMap), true, Options);
				});
				return;
			}
			UGameplayStatics::OpenLevel(Me, FName(UCampusGameInstance::MatchMap), true, Options);
		});
	};

	UCampusEOS* Eos = GetEos();
	if (!Eos || !Eos->IsConfigured())
	{
		Create(FString());
		return;
	}
	// Log in first: the room has to carry the puid the joiners will dial, and the listen server needs
	// that same login to bind its P2P socket. A failure here is not fatal - we host on the LAN.
	TWeakObjectPtr<UCampusEOS> WeakEos(Eos);
	Eos->EnsureLogin([Create, WeakEos](bool bOk)
	{
		Create(bOk && WeakEos.IsValid() ? WeakEos->GetLocalPuid() : FString());
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
		// the ticket rides along as a URL option either way; the host checks it with the API
		// (ACampusMatchGameMode::AdmitWithTicket)
		const FString Ticket = Room.Ticket;
		if (!Room.IsEos())
		{
			Self->ClientTravel(FString::Printf(TEXT("%s:%d?Ticket=%s"), *Room.Address, Room.Port, *Ticket), TRAVEL_Absolute);
			return;
		}
		// An EOS room: we must be logged in ourselves before we can open a P2P socket to the host.
		UCampusEOS* Eos = Self->GetEos();
		if (!Eos || !Eos->IsConfigured())
		{
			OnFailed(TEXT("That room is an internet room; this build can only join rooms on your Wi-Fi."));
			return;
		}
		const FString HostPuid = Room.HostPuid;
		Eos->EnsureLogin([WeakThis, HostPuid, Ticket, OnFailed](bool bLoggedIn)
		{
			ACampusLobbyPlayerController* Me = WeakThis.Get();
			if (!Me)
			{
				return;
			}
			if (!bLoggedIn)
			{
				OnFailed(TEXT("Could not reach the match network. Check your connection and try again."));
				return;
			}
			Me->ClientTravel(FString::Printf(TEXT("%s?Ticket=%s"), *UCampusEOS::ConnectUrlFor(HostPuid), *Ticket), TRAVEL_Absolute);
		});
	});
}

void ACampusLobbyPlayerController::JoinBRMatch(const FCampusBRMatch& Match, TFunction<void(const FString& Error)> OnFailed)
{
	if (!Match.IsValid())
	{
		OnFailed(TEXT("That match is no longer available."));
		return;
	}
	const UCampusBRQueue* Queue = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBRQueue>() : nullptr;
	const bool bHost = Queue && Queue->AmHost();
	const FString Level = CampusBR::LevelForMap(Match.Map);
	const FString Code = Match.Code;
	// The SERVER's day_night is what the match runs on, not the chip in the lobby: two phones in the
	// same match must light the same map.
	const int32 Night = Match.bNight ? 1 : 0;
	TWeakObjectPtr<ACampusLobbyPlayerController> WeakThis(this);

	if (bHost)
	{
		// Mirrors HostRoom: with no puid we tell the net driver to use plain IP sockets, so a LAN
		// match is hosted on 7777 even when EOS is configured but this phone could not log in.
		auto OpenAsListen = [WeakThis, Level, Code, Night](const FString& EosPuid)
		{
			ACampusLobbyPlayerController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			const TCHAR* Transport = EosPuid.IsEmpty() ? TEXT("?bUseIPSockets") : TEXT("");
			const FString Options = FString::Printf(TEXT("listen%s?Mode=BR?Night=%d?Code=%s"), Transport, Night, *Code);
			UGameplayStatics::OpenLevel(Self, FName(*Level), true, Options);
		};
		UCampusEOS* Eos = GetEos();
		if (!Eos || !Eos->IsConfigured() || !Match.IsEos())
		{
			OpenAsListen(FString());
			return;
		}
		// The listen server needs the same login the joiners will dial before it can bind its P2P
		// socket. A failure is not fatal: we host on the LAN instead, exactly as a Custom Room does.
		TWeakObjectPtr<UCampusEOS> WeakEos(Eos);
		Eos->EnsureLogin([OpenAsListen, WeakEos](bool bOk)
		{
			OpenAsListen(bOk && WeakEos.IsValid() ? WeakEos->GetLocalPuid() : FString());
		});
		return;
	}

	// A joiner. The code rides along as a URL option so the host's game mode can check it with the
	// API, the same way a Custom Room's ticket does.
	if (!Match.IsEos())
	{
		if (!Match.HasLan())
		{
			OnFailed(TEXT("That match has no address this build can reach."));
			return;
		}
		ClientTravel(FString::Printf(TEXT("%s:%d?Code=%s"), *Match.Address, Match.Port, *Code), TRAVEL_Absolute);
		return;
	}
	UCampusEOS* Eos = GetEos();
	if (!Eos || !Eos->IsConfigured())
	{
		OnFailed(TEXT("That match is an internet match; this build can only join matches on your Wi-Fi."));
		return;
	}
	const FString HostPuid = Match.HostPuid;
	Eos->EnsureLogin([WeakThis, HostPuid, Code, OnFailed](bool bLoggedIn)
	{
		ACampusLobbyPlayerController* Me = WeakThis.Get();
		if (!Me)
		{
			return;
		}
		if (!bLoggedIn)
		{
			OnFailed(TEXT("Could not reach the match network. Check your connection and try again."));
			return;
		}
		Me->ClientTravel(FString::Printf(TEXT("%s?Code=%s"), *UCampusEOS::ConnectUrlFor(HostPuid), *Code), TRAVEL_Absolute);
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
	// Presence says `match` for the whole match, which is what makes a friend's list read "IN MATCH"
	// and the INVITE button go disabled instead of burning an invite on somebody mid-firefight.
	if (UCampusSocial* Social = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusSocial>() : nullptr)
	{
		Social->SetScreenState(ECampusPresence::Match);
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
		// The BATTLE ROYALE pieces sit between the controls and the match screens: above the stick and
		// the fire button, below the waiting room and the gear panel. In a Team Clash match it finds no
		// BR state and collapses to nothing, so it costs one collapsed widget and no taps.
		BRHud = SNew(SCampusBRHud).Controller(this);
		GEngine->GameViewport->AddViewportWidgetContent(BRHud.ToSharedRef(), 8);
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
		if (BRHud.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(BRHud.ToSharedRef());
		}
	}
	Widget.Reset();
	TouchControls.Reset();
	BRHud.Reset();
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
		// NO LOCAL XP. XP and level are awarded by the result transaction on the server (migration 013,
		// PROFILE_DESIGN §3) and only ever read back from GET /me/profile. A phone that also paid itself
		// 50 XP would disagree with every other phone about this player's level.
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
	// Remember which room we just left, so the lobby can open PLAYER OF THE MATCH even if the
	// match.summary_ready event never arrives (socket down, or the app was backgrounded).
	if (UCampusSocial* Social = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusSocial>() : nullptr)
	{
		if (const ACampusGameState* State = GetWorld() ? GetWorld()->GetGameState<ACampusGameState>() : nullptr)
		{
			Social->NoteFinishedRoom(State->RoomCode);
		}
		Social->SetScreenState(ECampusPresence::Menu);
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
