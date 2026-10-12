#include "Campus/CampusParty.h"

#include "Campus/CampusMatchState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

void UCampusParty::BuildFillOrder(int32 Capacity, TArray<int32>& OutOrder)
{
	const int32 Count = FMath::Clamp(Capacity, 1, MaxMembers);
	OutOrder.Reset(Count);
	// Walk outwards from the middle, right side first: 4 slots -> 1, 2, 0, 3.
	const int32 Middle = (Count - 1) / 2;
	OutOrder.Add(Middle);
	for (int32 Step = 1; OutOrder.Num() < Count; ++Step)
	{
		if (Middle + Step < Count)
		{
			OutOrder.Add(Middle + Step);
		}
		if (OutOrder.Num() < Count && Middle - Step >= 0)
		{
			OutOrder.Add(Middle - Step);
		}
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

void UCampusParty::ApplyRoster(const TArray<FCampusPartyMember>& InMembers)
{
	FCampusPartyMember Leader;
	bool bHadLeader = false;
	for (const FCampusPartyMember& Member : Members)
	{
		if (Member.bLeader)
		{
			Leader = Member;
			bHadLeader = true;
			break;
		}
	}
	Members.Reset(Capacity);
	if (bHadLeader)
	{
		Members.Add(Leader);
	}
	for (const FCampusPartyMember& Member : InMembers)
	{
		if (Member.bLeader || Members.Num() >= Capacity)
		{
			continue;
		}
		Members.Add(Member);
	}
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
