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

	/** COSMETICS. Puts the saved / server outfit on the player's OWN figure (slot 0 - the one figure
	 *  whose outfit this phone knows; teammates keep the default body until the API carries theirs),
	 *  and re-dresses the eight lobby anchors from the saved decor. Both are called on lobby entry and
	 *  again whenever the wardrobe or the decor sheet changes something, so the change is instant. */
	void ApplyLobbyOutfit();
	void RefreshLobbyDecor();

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
	virtual void Tick(float DeltaSeconds) override;

private:
	/** Drops the podium cylinder that ships in L_Lobby so the squad stands on the floor. */
	void RemovePodium();

	/** Spawns / moves / hides one character per accepted squad member, leader in the middle.
	 *  Called once at BeginPlay and again whenever the roster or the squad size changes. */
	void RebuildSquad();

	/** The floor height UNDER THAT SLOT, traced fresh every time. Never cached: one height measured at
	 *  the lobby centre is only the right answer for the middle of a Squad line-up. */
	float GroundAt(const FVector& Where) const;

	/** The lobby floor's own height, read off the level once. L_Lobby's floor is an
	 *  /Engine/BasicShapes/Plane, which ships with no collision at all, so GroundAt's ray finds
	 *  nothing to hit and needs an honest answer to fall back on rather than a bare zero. */
	float ReadFloorHeight() const;

	/** Stands one figure on its slot (UCampusParty::SlotPose) with its FEET on the floor - measured
	 *  from the mesh's own bounds, not from an assumption about where the asset's origin sits. */
	void PlaceMember(ASkeletalMeshActor* Actor, int32 SlotIndex) const;

	/** Re-measures every visible figure where it stands. */
	void GroundLineUp() const;

	/** Keeps the yaw the player dragged to when the line-up is rebuilt. */
	float DisplayYaw = 0.f;

	/** Index 0 is the local player; up to UCampusParty::MaxMembers. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ASkeletalMeshActor>> Squad;
	/** The level's floor height; GroundAt's fallback. Read after the podium is gone. */
	float FloorPlaneZ = 0.f;
	/** Seconds since the last rebuild while the line-up keeps re-checking its footing, or -1 when the
	 *  window is shut. A figure placed on its spawn frame only has its reference pose to be measured
	 *  in; this is what comes back once the real pose exists. */
	float RegroundTime = -1.f;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> PodiumCamera;

	/** Spawned on the first profile open and kept: the same rotation and FOV as the podium camera,
	 *  shifted sideways so the figure lands under the left 45 % of the screen. */
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ProfileCamera;
	bool bProfileFocus = false;

	/** The eight decor anchors. Spawned once, after the floor height is known, and rebuilt in place. */
	UPROPERTY(Transient)
	TObjectPtr<class ACampusLobbyDecor> DecorSet;

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

	/** BATTLE ROYALE RESULT SCREEN: stands the squad up as real character meshes on a stage in clear
	 *  air above the map and points a camera at it, so SCampusBREnd can be drawn over a live 3D
	 *  line-up instead of a render target (Campus/CampusBREnd.h explains why not a SceneCapture).
	 *  Idempotent - the widget calls it on its first tick and never again. */
	void ShowResultStage();
	/** Takes the stage down. Called from EndPlay; safe when there is no stage. */
	void ClearResultStage();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	UFUNCTION(Server, Reliable)
	void ServerSetName(const FString& Name);

	void ApplyInputMode(bool bGameplay);

	/** Plants the result line-up's feet on the stage floor, measured from the pose it is actually
	 *  drawn in. Cheap: at most four bounds, and it only writes when the pose disagrees. */
	void GroundResultFigures();

	TSharedPtr<SCampusMatchWidget> Widget;
	TSharedPtr<SCampusTouchControls> TouchControls;
	/** The BATTLE ROYALE overlay. Present in every match and collapsed in the ones that are not BR. */
	TSharedPtr<SCampusBRHud> BRHud;
	/** UCampusBRQueue::OnSessionEnded (bs.ended): the battle session was settled without a result. */
	FDelegateHandle SessionEndedHandle;
	int32 AppliedMode = -1;

	/** THE RESULT STAGE. Display only: no collision, no replication, local to this phone. */
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ResultCamera;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ASkeletalMeshActor>> ResultFigures;

	/** The stage floor the figures stand on (the stage origin's Z). */
	float ResultFloorZ = 0.f;
	/** Seconds since the stage went up while the figures keep re-checking their footing, or -1 when
	 *  that window is shut. Same reasoning as the lobby line-up's. */
	float ResultStageTime = -1.f;

	bool bResultRecorded = false;

	bool bMicOn = false;
	bool bSpeakerOn = true;
	bool bSettingsOpen = false;
	/** Android: the microphone permission is asked the first time the mic is turned on. */
	UFUNCTION()
	void OnMicPermission(const TArray<FString>& Permissions, const TArray<bool>& Granted);
};
