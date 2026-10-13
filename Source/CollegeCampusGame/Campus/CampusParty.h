// The lobby squad (Free Fire's side-by-side party row).
//
// Where the data comes from today: the API (UCampusBackend) has no party / invite endpoints, and the
// only real "join a friend" channel in the game is the room code. So the roster is built from the two
// real sources that do exist:
//   * the local player, from UCampusBackend's cached profile (always the leader, always present);
//   * the squad the player just played with, adopted from the match's replicated PlayerArray when the
//     match hands back to the lobby (AdoptSquad) - these are the players who really were in the room.
// Empty slots are shown by the lobby as "+" invite plates that open the room-code screen.
//
// Living on the game instance means the roster survives level travel, which is what makes the
// "come back from a match and your squad is still standing there" behaviour work.
//
// When the API grows a party endpoint (GET /api/me/party -> members + accepted flags), only
// ApplyRoster below needs to be called from UCampusBackend; the lobby does not change at all.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CampusParty.generated.h"

class AGameStateBase;

/**
 * WHERE THE LOBBY LINE-UP STANDS. Deliberately outside UCampusParty: it is view geometry, not party
 * state, and BOTH the 3D figures (ACampusLobbyPlayerController) and their name plates (the lobby UI)
 * have to read the same numbers or a plate drifts off its character.
 */
namespace CampusLineUp
{
	/** ONE SPOT.
	 *
	 *  Lateral is cm to the player's RIGHT ON SCREEN (the lobby camera looks down -X, so its right is
	 *  -Y). Depth is cm TOWARDS the camera. Yaw turns the figure, on top of the yaw the player has
	 *  dragged the whole line-up to. */
	struct FSlotPose
	{
		float LateralCm = 0.f;
		float DepthCm = 0.f;
		float YawDeg = 0.f;
	};

	/** FOUR FIXED SPOTS, LEFT TO RIGHT, THE SAME IN SOLO, DUO AND SQUAD:
	 *
	 *      0: teammate   1: THE PLAYER   2: teammate   3: teammate
	 *
	 *  The player never moves between modes - MainSlot is their spot whether they stand alone or in a
	 *  full squad, so switching mode only shows or hides the other three. The player also stands a
	 *  little FORWARD of the rest, and the other three sit at slightly different depths and yaws, so
	 *  the group reads as a photo rather than as four figures on a rail.
	 *
	 *  The numbers: the lobby camera is 380 cm out at 70 degrees horizontal FOV, so the visible
	 *  half-width at the line-up is 380 * tan(35) = 266 cm. 77 cm of lateral spacing puts the four
	 *  spots between roughly 29 % and 70 % across the screen - clear of the menu column and the
	 *  weekend board on the left, and of the ONLINE strip, the mode chips and START on the right. */
	constexpr int32 NumSlots = 4;
	constexpr int32 MainSlot = 1;
	const FSlotPose& SlotPose(int32 SlotIndex);

	/** WHICH SLOT EACH MEMBER TAKES: the player, then the near RIGHT, then the left, then the far
	 *  right - { 1, 2, 0, 3 }. Right before left on purpose: the left of the screen is where the menu
	 *  column lives, so a Duo pair reads better on the open side. */
	int32 SlotForMember(int32 MemberIndex);

	/** Where that slot's name plate belongs, as a fraction of the viewport width. Perspective
	 *  corrected: a figure standing further back is further from the camera, so it sits closer to the
	 *  middle of the screen than its lateral offset alone would suggest. */
	float SlotScreenFraction(int32 SlotIndex);

	/** THE YAW A FIGURE IN THIS SLOT SHOULD STAND AT, before the turntable drag is added.
	 *
	 *  Aimed at the camera first - a figure 115 cm off to one side is NOT facing the camera when it
	 *  faces straight down the lobby axis - and then turned by the slot's own small amount so the
	 *  outer three lean in towards the group. The player's slot adds nothing, so the player faces the
	 *  camera square on. */
	// The mannequin mesh faces +Y at yaw 0 (seen on the phone: side-on, facing screen-left). The lobby
	// camera looks down -X, so every figure needs -90 to face it. Added to the actor yaw, the only rotation.
	inline constexpr float MeshYawOffset = -90.f;

	float SlotFacingYaw(int32 SlotIndex);
}

/** One plate in the lobby squad row. */
struct FCampusPartyMember
{
	FString Name;
	int32 Level = 1;
	/** 0 = unranked, 1..5 = chevron pips on the name plate. */
	int32 Rank = 0;
	bool bLeader = false;
	/** False = invited but has not accepted yet; drawn dimmed, no character spawned. */
	bool bAccepted = true;

	/** The server's player id, set for every member of a real party (migration 012). Identity is this,
	 *  never Name: two students may pick the same nickname, and kick needs an id anyway. Empty on the
	 *  AdoptSquad path, where all the lobby has is a replicated display name. */
	FString PlayerId;

	/** menu | lobby | match | offline, from the party view. Empty on the AdoptSquad path. */
	FString Presence;

	/** The pending invite behind an INVITED plate, so the leader can cancel it. */
	FString InviteId;

	/** Same plate, by id when there is one and by name otherwise. */
	bool Is(const FCampusPartyMember& Other) const
	{
		return !PlayerId.IsEmpty() && !Other.PlayerId.IsEmpty() ? PlayerId == Other.PlayerId : Name == Other.Name;
	}
};

UCLASS()
class UCampusParty : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** One leader plus three accepted invites. */
	static constexpr int32 MaxMembers = 4;

	/** The local player. Replaces any existing leader entry. */
	void SetLeader(const FString& Name, int32 Level, int32 Rank = 0);

	/** Adds or updates one member; ignored once the party is full. */
	void AddMember(const FString& Name, int32 Level, bool bAccepted, int32 Rank = 0);

	void RemoveMember(const FString& Name);

	/** Drops everyone but the leader. */
	void ClearGuests();

	/** GOING SOLO: keeps MY plate and nothing else, with the crown back on it.
	 *
	 *  The local player is ALWAYS in the line-up, so the squad going away (I left, I was removed, the
	 *  leader broke it up) must not empty the roster: an empty one blinks the podium to nobody until the
	 *  lobby's next SyncParty() rebuilds my plate, and that can be a whole screen away. Keeps the plate
	 *  the server last sent for me - the right one even when the squad I just left was somebody
	 *  else's, where ClearGuests() would have left THEIR plate standing. */
	void KeepOnlyLocalPlayer();

	/** Replaces the whole roster from the API party feed (GET /v1/parties/me and every party.* event).
	 *
	 *  bAuthoritative = the list is the server's and already contains ME, with the server's own
	 *  leader flag - so it replaces the roster wholesale. Without it the local leader entry is kept,
	 *  which is what the pre-party AdoptSquad path still wants. Keeping the local entry while a server
	 *  party exists would show the player twice and put the crown on the wrong plate.
	 *
	 *  THE LOCAL PLAYER IS MOVED TO INDEX 0 (see SetLocalPlayerId): member 0 owns MainSlot, and the
	 *  server's order is joined_at - so in somebody else's squad I arrive second or third. */
	void ApplyRoster(const TArray<FCampusPartyMember>& InMembers, bool bAuthoritative = false);

	/** My own server player id, from UCampusSocial. THE MainSlot INVARIANT depends on it: member 0 is
	 *  the local player, their figure stands on MainSlot and their plate is anchored under it, so a
	 *  roster that puts somebody else first would draw a teammate's name over the player's own
	 *  character. Empty on the AdoptSquad / solo path, where member 0 is the local leader by
	 *  construction. */
	void SetLocalPlayerId(const FString& InPlayerId) { LocalPlayerId = InPlayerId; }
	const FString& GetLocalPlayerId() const { return LocalPlayerId; }

	/** Takes the squad from a finished match: everyone on the local player's team, in join order. */
	void AdoptSquad(const AGameStateBase* State, const FString& MyName, uint8 MyTeam);

	const TArray<FCampusPartyMember>& GetMembers() const { return Members; }

	/** Members with bAccepted - the ones that get a character in the lobby. */
	int32 NumAccepted() const;

	/** Visible plates: follows the lobby's Solo / Duo / Squad choice, clamped to MaxMembers. */
	int32 GetCapacity() const { return Capacity; }
	void SetCapacity(int32 InCapacity);

	/** Raised when the roster changes, so the lobby rebuilds once instead of polling every frame. */
	FSimpleMulticastDelegate OnChanged;

private:
	void Changed();

	/** Moves the local player to index 0, keeping everybody else's relative order. No-op when the id is
	 *  unknown or already first. */
	void HoistLocalPlayer(TArray<FCampusPartyMember>& List) const;

	TArray<FCampusPartyMember> Members;
	int32 Capacity = 4;
	FString LocalPlayerId;
};
