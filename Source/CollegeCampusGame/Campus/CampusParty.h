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
};

UCLASS()
class UCampusParty : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** One leader plus three accepted invites. */
	static constexpr int32 MaxMembers = 4;

	/** How far left (negative) or right (positive) of centre a slot sits, in slot widths. */
	static float SlotStep(int32 SlotIndex, int32 Capacity)
	{
		return float(SlotIndex) - float(FMath::Max(Capacity, 1) - 1) * 0.5f;
	}

	/** Slot indices in fill order: closest to the centre first, then right, left, right. The
	 *  leader therefore always stands in the middle, exactly like the Free Fire squad lobby. */
	static void BuildFillOrder(int32 Capacity, TArray<int32>& OutOrder);

	/** The local player. Replaces any existing leader entry. */
	void SetLeader(const FString& Name, int32 Level, int32 Rank = 0);

	/** Adds or updates one member; ignored once the party is full. */
	void AddMember(const FString& Name, int32 Level, bool bAccepted, int32 Rank = 0);

	void RemoveMember(const FString& Name);

	/** Drops everyone but the leader. */
	void ClearGuests();

	/** Replaces the whole roster (for a future API party feed). The leader entry is kept. */
	void ApplyRoster(const TArray<FCampusPartyMember>& InMembers);

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

	TArray<FCampusPartyMember> Members;
	int32 Capacity = 4;
};
