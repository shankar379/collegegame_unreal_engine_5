#include "Campus/CampusParty.h"

#include "Campus/CampusMatchState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

namespace CampusLineUp
{
	/** The lobby camera: 380 cm out, 70 degrees horizontal FOV. Shared with the lobby controller, which
	 *  spawns that camera, and with the plate row, which has to land under the right figure. */
	constexpr float CameraDistanceCm = 380.f;
	/** tan(35 deg): half the horizontal FOV. Visible half-width at distance d is d * this. */
	constexpr float HalfFovTan = 0.70021f;
	/** 77 cm apart. Wide enough that nobody overlaps, tight enough that four fit between the menu
	 *  column and the START button. */
	constexpr float SpacingCm = 77.f;

	const FSlotPose& SlotPose(int32 SlotIndex)
	{
		// Laterals are centred on the GROUP, which leaves the player half a step left of the screen
		// centre - exactly the "centre or slightly left of centre" the line-up is drawn around.
		// YAW SIGN. The lobby camera looks down -X and its right is -Y, so screen-left is +Y - and a
		// figure faces +X at yaw 0, which UE turns towards +Y as the yaw rises. A POSITIVE yaw
		// therefore turns a figure towards the LEFT OF THE SCREEN. The one on the left of the group
		// leans in with a negative yaw; the two on the right lean in with a positive one.
		static const FSlotPose Poses[NumSlots] = {
			// 0: the one teammate on the player's left, well back, leaning in to its right.
			{ -1.5f * SpacingCm, -18.f, -6.f },
			// 1: THE PLAYER. Forward of everyone and square to the camera. Never moves, never leans.
			{ -0.5f * SpacingCm,  22.f,  0.f },
			// 2: first teammate to the player's right, leaning in to its left.
			{  0.5f * SpacingCm, -14.f,  5.f },
			// 3: second teammate to the player's right, the furthest back of the four.
			{  1.5f * SpacingCm, -26.f,  9.f },
		};
		return Poses[FMath::Clamp(SlotIndex, 0, NumSlots - 1)];
	}

	int32 SlotForMember(int32 MemberIndex)
	{
		// Member 0 is always the local player, so it always lands on MainSlot.
		static const int32 Order[NumSlots] = { MainSlot, 2, 0, 3 };
		return Order[FMath::Clamp(MemberIndex, 0, NumSlots - 1)];
	}

	float SlotScreenFraction(int32 SlotIndex)
	{
		const FSlotPose& Pose = SlotPose(SlotIndex);
		// How far the camera is from THIS figure, not from the centre of the row: a figure standing
		// further back is further away, so the same lateral offset covers fewer pixels.
		const float Distance = FMath::Max(50.f, CameraDistanceCm - Pose.DepthCm);
		return 0.5f + Pose.LateralCm / (2.f * Distance * HalfFovTan);
	}

	float SlotFacingYaw(int32 SlotIndex)
	{
		const FSlotPose& Pose = SlotPose(SlotIndex);
		// The figure stands at world (DepthCm, -LateralCm) and the camera at (CameraDistanceCm, 0), so
		// the bearing from the figure to the camera is atan2(LateralCm, CameraDistanceCm - DepthCm).
		// At yaw 0 a figure faces +X, straight down the lobby axis, which only points AT the camera
		// for something standing dead centre.
		const float Distance = FMath::Max(50.f, CameraDistanceCm - Pose.DepthCm);
		const float TowardsCamera = FMath::RadiansToDegrees(FMath::Atan2(Pose.LateralCm, Distance));
		return TowardsCamera + Pose.YawDeg;
	}
}

void UCampusParty::Changed()
{
	OnChanged.Broadcast();
}

int32 UCampusParty::NumAccepted() const
{
	int32 Count = 0;
	for (const FCampusPartyMember& Member : Members)
	{
		Count += Member.bAccepted ? 1 : 0;
	}
	return Count;
}

void UCampusParty::SetCapacity(int32 InCapacity)
{
	const int32 Clamped = FMath::Clamp(InCapacity, 1, MaxMembers);
	if (Capacity == Clamped)
	{
		return;
	}
	Capacity = Clamped;
	// A smaller squad drops the members who no longer fit, leader first in the list and so kept.
	if (Members.Num() > Capacity)
	{
		Members.SetNum(Capacity);
	}
	Changed();
}

void UCampusParty::SetLeader(const FString& Name, int32 Level, int32 Rank)
{
	FCampusPartyMember Leader;
	Leader.Name = Name.IsEmpty() ? TEXT("YOU") : Name;
	Leader.Level = FMath::Max(Level, 1);
	Leader.Rank = Rank;
	Leader.bLeader = true;
	Leader.bAccepted = true;

	// Nothing to do if the leader already reads exactly like this. The lobby calls SetLeader on
	// every rebuild and rebuilds on OnChanged, so an unconditional broadcast here would recurse
	// forever.
	if (Members.Num() > 0 && Members[0].bLeader && Members[0].Name == Leader.Name
		&& Members[0].Level == Leader.Level && Members[0].Rank == Leader.Rank)
	{
		return;
	}

	for (int32 Index = Members.Num() - 1; Index >= 0; --Index)
	{
		if (Members[Index].bLeader || Members[Index].Name == Leader.Name)
		{
			Members.RemoveAt(Index);
		}
	}
	Members.Insert(Leader, 0);
	if (Members.Num() > Capacity)
	{
		Members.SetNum(Capacity);
	}
	Changed();
}

void UCampusParty::AddMember(const FString& Name, int32 Level, bool bAccepted, int32 Rank)
{
	if (Name.IsEmpty())
	{
		return;
	}
	for (FCampusPartyMember& Existing : Members)
	{
		if (Existing.Name == Name)
		{
			// Only broadcast on a real change; see the note in SetLeader.
			if (Existing.Level == FMath::Max(Level, 1) && Existing.bAccepted == bAccepted && Existing.Rank == Rank)
			{
				return;
			}
			Existing.Level = FMath::Max(Level, 1);
			Existing.bAccepted = bAccepted;
			Existing.Rank = Rank;
			Changed();
			return;
		}
	}
	if (Members.Num() >= Capacity)
	{
		return;
	}
	FCampusPartyMember Member;
	Member.Name = Name;
	Member.Level = FMath::Max(Level, 1);
	Member.Rank = Rank;
	Member.bAccepted = bAccepted;
	Members.Add(Member);
	Changed();
}

void UCampusParty::RemoveMember(const FString& Name)
{
	const int32 Removed = Members.RemoveAll([&Name](const FCampusPartyMember& Member)
	{
		return !Member.bLeader && Member.Name == Name;
	});
	if (Removed > 0)
	{
		Changed();
	}
}

void UCampusParty::ClearGuests()
{
	const int32 Removed = Members.RemoveAll([](const FCampusPartyMember& Member) { return !Member.bLeader; });
	if (Removed > 0)
	{
		Changed();
	}
}

void UCampusParty::HoistLocalPlayer(TArray<FCampusPartyMember>& List) const
{
	if (LocalPlayerId.IsEmpty())
	{
		return;
	}
	const int32 Mine = List.IndexOfByPredicate([this](const FCampusPartyMember& Member)
	{
		return Member.PlayerId == LocalPlayerId;
	});
	if (Mine <= 0)
	{
		// Already first, or not in this roster at all. "Not in it" is not repaired here: UCampusSocial
		// treats a party without me as no party at all, so it never reaches this function.
		return;
	}
	FCampusPartyMember MyPlate = List[Mine];
	List.RemoveAt(Mine, 1, EAllowShrinking::No);
	List.Insert(MoveTemp(MyPlate), 0);
}

void UCampusParty::KeepOnlyLocalPlayer()
{
	int32 Keep = LocalPlayerId.IsEmpty() ? INDEX_NONE : Members.IndexOfByPredicate([this](const FCampusPartyMember& Member)
	{
		return Member.PlayerId == LocalPlayerId;
	});
	if (Keep == INDEX_NONE)
	{
		// No server id to match on (the local / AdoptSquad path): the leader entry IS the local player.
		Keep = Members.IndexOfByPredicate([](const FCampusPartyMember& Member) { return Member.bLeader; });
	}
	if (Keep == INDEX_NONE)
	{
		// Nothing in here is identifiably me. The squad is still gone, so empty it and let the lobby's
		// SyncParty() build my plate from the profile - which is where it comes from on a cold start too.
		if (Members.Num() > 0)
		{
			Members.Reset();
			Changed();
		}
		return;
	}
	// Already exactly "me, alone, leading": say nothing. Every broadcast repaints the lobby.
	if (Members.Num() == 1 && Keep == 0 && Members[0].bLeader && Members[0].bAccepted)
	{
		return;
	}
	FCampusPartyMember Mine = Members[Keep];
	Mine.bLeader = true;
	Mine.bAccepted = true;
	Mine.InviteId.Reset();
	Members.Reset();
	Members.Add(MoveTemp(Mine));
	Changed();
}

void UCampusParty::ApplyRoster(const TArray<FCampusPartyMember>& InMembers, bool bAuthoritative)
{
	TArray<FCampusPartyMember> Next;
	Next.Reserve(FMath::Max(Capacity, InMembers.Num()));
	if (bAuthoritative)
	{
		// The server's roster, in the server's own order (joined_at) with one correction: ME FIRST.
		// Nothing local is merged in - the party IS the server's answer, including who holds the crown -
		// but member 0 owns MainSlot, and in somebody else's squad the server lists me second or third.
		// Hoisting BEFORE the capacity clamp, so a narrow row can never be the thing that drops me.
		Next = InMembers;
		HoistLocalPlayer(Next);
		if (Next.Num() > Capacity)
		{
			Next.SetNum(Capacity);
		}
	}
	else
	{
		for (const FCampusPartyMember& Member : Members)
		{
			if (Member.bLeader)
			{
				Next.Add(Member);
				break;
			}
		}
		for (const FCampusPartyMember& Member : InMembers)
		{
			if (Member.bLeader || Next.Num() >= Capacity)
			{
				continue;
			}
			Next.Add(Member);
		}
	}

	// Only broadcast on a real change: every party event repaints the lobby, and an unconditional
	// broadcast from a 30-second presence frame would rebuild the whole screen for nothing.
	if (Next.Num() == Members.Num())
	{
		bool bSame = true;
		for (int32 Index = 0; Index < Next.Num() && bSame; ++Index)
		{
			const FCampusPartyMember& A = Next[Index];
			const FCampusPartyMember& B = Members[Index];
			bSame = A.Is(B) && A.Name == B.Name && A.Level == B.Level && A.bLeader == B.bLeader
				&& A.bAccepted == B.bAccepted && A.Presence == B.Presence && A.InviteId == B.InviteId;
		}
		if (bSame)
		{
			return;
		}
	}
	Members = MoveTemp(Next);
	Changed();
}

void UCampusParty::AdoptSquad(const AGameStateBase* State, const FString& MyName, uint8 MyTeam)
{
	if (!State)
	{
		return;
	}
	ClearGuests();
	for (const APlayerState* Player : State->PlayerArray)
	{
		const ACampusPlayerState* Campus = Cast<ACampusPlayerState>(Player);
		if (!Campus || uint8(Campus->Team) != MyTeam)
		{
			continue;
		}
		const FString Name = Campus->GetPlayerName();
		if (Name.IsEmpty() || Name == MyName || Members.Num() >= Capacity)
		{
			continue;
		}
		FCampusPartyMember Member;
		Member.Name = Name;
		// Level is not replicated in a match; show the squad rank the kills earned instead.
		Member.Level = 1;
		Member.Rank = FMath::Clamp(Campus->Kills / 3, 0, 5);
		Member.bAccepted = true;
		Members.Add(Member);
	}
	Changed();
}
