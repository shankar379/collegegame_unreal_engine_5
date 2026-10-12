// One controller per level.
// Lobby: UI only — the squad stands side by side on the lobby floor (no podium), drag to turn,
//        menus, START.
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
class UCampusParty;
class UCampusSave;
class UCampusBackend;
class UCampusEOS;
class UCampusGameInstance;

UCLASS()
class ACampusLobbyPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACampusLobbyPlayerController();

	UCampusGameInstance* GetCampusInstance() const;
	UCampusSave* GetProfile() const;
	UCampusParty* GetParty() const;

	/** Turns the whole squad together (drag in the empty middle of the screen). */
	void RotateDisplay(float DeltaYaw);

	UCampusBackend* GetBackend() const;

	/** Internet play (EOS P2P). Null-safe: when it is not configured the room stays on the LAN. */
	UCampusEOS* GetEos() const;

	/** Custom Room: the API issues a code (1 Room Card), then this phone hosts the match (listen server). */
	void HostRoom(TFunction<void(const FString& Error)> OnFailed);

	/** Resolves a friend's room code through the API and travels to the host phone. */
	void JoinRoomByCode(const FString& Code, TFunction<void(const FString& Error)> OnFailed);

	/** Quick Match: joins the room server from CampusNetSettings (the PC on the Wi-Fi today, AWS later). */
	void JoinRoom();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Drops the podium cylinder that ships in L_Lobby so the squad stands on the floor. */
	void RemovePodium();

	/** Spawns / moves / hides one character per accepted squad member, leader in the middle.
	 *  Called once at BeginPlay and again whenever the roster or the squad size changes. */
	void RebuildSquad();

	/** The floor height under a slot, traced once per rebuild (the podium may be gone by then). */
	float GroundAt(const FVector& Where) const;

	/** Keeps the yaw the player dragged to when the line-up is rebuilt. */
	float DisplayYaw = 0.f;

	/** Index 0 is the local player; up to UCampusParty::MaxMembers. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ASkeletalMeshActor>> Squad;
	/** Lobby floor height under the line-up, measured once (traces never see the squad). */
	float FloorZ = 0.f;
	bool bFloorZKnown = false;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> PodiumCamera;

	TSharedPtr<SCampusLobbyWidget> Widget;
	FDelegateHandle PartyChangedHandle;
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

	/** Adds this match's reward to the profile (if not yet) and goes back to the lobby. */
	void ClaimAndLeave();

	/** Saves this match's reward once; runs by itself the moment the match ends. */
	void RecordResult();

	/** Leaves without a reward (waiting room "Leave"). */
	void LeaveToLobby();

	/** Coins this player earns for the finished match. */
	int32 ComputeReward() const;

	/** Team voice (Free Fire style toggles): mic sends to teammates; speaker plays teammates. */
	void SetMicOn(bool bOn);
	void SetSpeakerOn(bool bOn);
	bool IsMicOn() const { return bMicOn; }
	bool IsSpeakerOn() const { return bSpeakerOn; }

	/** The in-match settings gear (HUD top-right). SCampusMatchWidget draws the panel. */
	void SetSettingsOpen(bool bOpen) { bSettingsOpen = bOpen; }
	void ToggleSettings() { bSettingsOpen = !bSettingsOpen; }
	bool IsSettingsOpen() const { return bSettingsOpen; }

	/** Look speed from the saved settings; the touch HUD multiplies its deltas by this. */
	float GetLookSensitivity() const;

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

	bool bResultRecorded = false;

	bool bMicOn = false;
	bool bSpeakerOn = true;
	bool bSettingsOpen = false;
	/** Android: the microphone permission is asked the first time the mic is turned on. */
	UFUNCTION()
	void OnMicPermission(const TArray<FString>& Permissions, const TArray<bool>& Granted);
};
