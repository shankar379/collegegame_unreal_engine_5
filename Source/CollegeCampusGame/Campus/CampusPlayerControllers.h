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
class SCampusBRHud;
class SCampusLobbyWidget;
class SCampusMatchWidget;
class SCampusTouchControls;
class UCampusParty;
class UCampusSave;
class UCampusBackend;
class UCampusSocial;
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
	/** Friends, squads, invites, presence and the profile page's data (migration 012 / 013). */
	UCampusSocial* GetSocial() const;

	/** Turns the whole squad together (drag in the empty middle of the screen). */
	void RotateDisplay(float DeltaYaw);

	/** The profile page: one figure, moved to about 25 % across the screen so the angular panel can
	 *  have the right half. Reuses the lobby's own mannequin and RotateDisplay - there is no second
	 *  3D path to keep in step. Call with false to put the line-up and the podium camera back. */
	void FocusProfile(bool bOn);

	UCampusBackend* GetBackend() const;

	/** Internet play (EOS P2P). Null-safe: when it is not configured the room stays on the LAN. */
	UCampusEOS* GetEos() const;

	/** Custom Room: the API issues a code (1 Room Card), then this phone hosts the match (listen server). */
	void HostRoom(TFunction<void(const FString& Error)> OnFailed);

	/** Resolves a friend's room code through the API and travels to the host phone. */
	void JoinRoomByCode(const FString& Code, TFunction<void(const FString& Error)> OnFailed);

	/** BATTLE ROYALE: the queue has handed us a match (UCampusBRQueue). Either this phone is the
	 *  listed host, in which case it OPENS the map as a listen server with
	 *      ?listen?Mode=BR?Night=0|1?Code=...
	 *  or it travels to the host over the same EOS / LAN path JoinRoomByCode uses - minus the room
	 *  lookup, because the queue response already carries the connect object. */
	void JoinBRMatch(const struct FCampusBRMatch& Match, TFunction<void(const FString& Error)> OnFailed);

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

	/** Spawned on the first profile open and kept: the same rotation and FOV as the podium camera,
	 *  shifted sideways so the figure lands under the left 45 % of the screen. */
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ProfileCamera;
	bool bProfileFocus = false;

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
	/** The BATTLE ROYALE overlay. Present in every match and collapsed in the ones that are not BR. */
	TSharedPtr<SCampusBRHud> BRHud;
	int32 AppliedMode = -1;

	bool bResultRecorded = false;

	bool bMicOn = false;
	bool bSpeakerOn = true;
	bool bSettingsOpen = false;
	/** Android: the microphone permission is asked the first time the mic is turned on. */
	UFUNCTION()
	void OnMicPermission(const TArray<FString>& Permissions, const TArray<bool>& Granted);
};
