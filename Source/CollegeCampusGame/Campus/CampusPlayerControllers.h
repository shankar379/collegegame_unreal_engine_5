// One controller per level.
// Lobby: UI only — shows the character on a podium, drag to turn, menus, START.
// Match: game controls — waiting room, then on-screen controls + HUD, then the win screen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CampusPlayerControllers.generated.h"

class ACameraActor;
class ASkeletalMeshActor;
class SCampusLobbyWidget;
class SCampusMatchWidget;
class SCampusTouchControls;
class UCampusSave;
class UCampusGameInstance;

UCLASS()
class ACampusLobbyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACampusLobbyPlayerController();

	UCampusGameInstance* GetCampusInstance() const;
	UCampusSave* GetProfile() const;

	/** Turns the podium character (drag in the empty middle of the screen). */
	void RotateDisplay(float DeltaYaw);

	/** Hosts a room on this device (listen server, LAN). */
	void HostRoom();

	/** Joins the room server from CampusNetSettings (the PC on the Wi-Fi today, AWS later). */
	void JoinRoom();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<ASkeletalMeshActor> Display;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> PodiumCamera;

	TSharedPtr<SCampusLobbyWidget> Widget;
};

UCLASS()
class ACampusMatchPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UCampusSave* GetProfile() const;

	UFUNCTION(Server, Reliable)
	void ServerRequestStart();

	UFUNCTION(Server, Reliable)
	void ServerSwitchTeam();

	/** Adds this match's reward to the profile and goes back to the lobby. */
	void ClaimAndLeave();

	/** Leaves without a reward (waiting room "Leave"). */
	void LeaveToLobby();

	/** Coins this player earns for the finished match. */
	int32 ComputeReward() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	UFUNCTION(Server, Reliable)
	void ServerSetName(const FString& Name);

	void ApplyInputMode(bool bGameplay);

	TSharedPtr<SCampusMatchWidget> Widget;
	TSharedPtr<SCampusTouchControls> TouchControls;
	int32 AppliedMode = -1;
};
