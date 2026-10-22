// LIVE EVENTS (UCampusBackend): blob key `events`, plus the two player routes behind the lobby
// banner (PHASE_B_CONTRACT B2, routes/adminevents.js).
//
//   `events` : { horizon_days, events: [ { id, label, state, starts_at, ends_at, banner_key,
//                modifiers{xp_multiplier, points_multiplier}, rewards[] } ] }
//   GET  /v1/events/:id/progress              the window, the rewards, this player's progress
//   POST /v1/events/:id/claim {reward_index}   Idempotency-Key, once per player per reward
//
// THE CLIENT FILTERS BY ITS OWN CLOCK, and that is a correctness decision made on the server's side
// of the contract: the blob is cached by the phone and by a CDN, so it carries every scheduled event
// with its own window instead of a list that was true at the moment it was assembled. An event eight
// days out would otherwise never reach a device until somebody happened to touch the console again.
//
// THE CLIENT NEVER DECIDES ELIGIBILITY. `claimable` on a progress row is the server's own answer,
// computed with the same function the claim uses, so the button is never enabled for a claim the API
// would refuse - and the claim is still gated by `reward_claims`' primary key, which is what makes two
// phones tapping at once one payout and one 409.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Dom/JsonObject.h"

using namespace CampusApi;

// -------------------------------------------------------------------------------------------------
// Value types

namespace
{
	/** "diamonds" -> "50 DIAMONDS", "item" -> "GOLD HELMET": the one place a reward becomes words. */
	FString DescribeReward(const FString& Kind, const FString& ItemId, int32 Amount)
	{
		if (Kind == TEXT("diamonds"))
		{
			return FString::Printf(TEXT("%d DIAMONDS"), Amount);
		}
		if (Kind == TEXT("room_cards"))
		{
			return FString::Printf(TEXT("%d ROOM CARD%s"), Amount, Amount == 1 ? TEXT("") : TEXT("S"));
		}
		if (Kind == TEXT("item") && !ItemId.IsEmpty())
		{
			// The catalogue id is all the blob carries, so it is shown as words rather than as a
			// mystery: "gold_helmet" reads as GOLD HELMET and needs no second request to name it.
			return ItemId.Replace(TEXT("_"), TEXT(" ")).ToUpper();
		}
		return FString();
	}
}

FString FCampusEventReward::Describe() const
{
	return DescribeReward(Kind, ItemId, Amount);
}

FString FCampusEventRewardProgress::Describe() const
{
	return DescribeReward(Kind, ItemId, Amount);
}

// -------------------------------------------------------------------------------------------------
// The published blob

void UCampusBackend::ReadEventsKey(const TSharedPtr<FJsonObject>& Key, FCampusEvents& Out)
{
	Out = FCampusEvents();
	if (!Key.IsValid())
	{
		return;
	}
	if (const int32 Horizon = int32(Num(Key, TEXT("horizon_days"))); Horizon > 0)
	{
		Out.HorizonDays = Horizon;
	}
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Key->TryGetArrayField(TEXT("events"), Rows))
	{
		return;
	}
	Out.Events.Reserve(Rows->Num());
	for (const TSharedPtr<FJsonValue>& Value : *Rows)
	{
		const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
		const int32 Id = int32(Num(Row, TEXT("id")));
		if (!Row.IsValid() || Id <= 0)
		{
			continue;   // nothing to ask progress for: not showable
		}
		FCampusEvent& Event = Out.Events.AddDefaulted_GetRef();
		Event.Id = Id;
		Event.Label = Str(Row, TEXT("label"));
		Event.State = Str(Row, TEXT("state"));
		Event.StartsAt = Stamp(Row, TEXT("starts_at"));
		Event.EndsAt = Stamp(Row, TEXT("ends_at"));
		// THE KEY, NOT THE URL. `banner_url` is in the blob too, but the version is in the `content`
		// key - so the key is what the picture cache can hold immutably, and the url cannot be.
		Event.BannerKey = Str(Row, TEXT("banner_key"));
		// migration 034: the festival dressing. Each absent on an older server, which reads as "none".
		Event.Theme = Str(Row, TEXT("theme")).ToLower();
		Event.Tagline = Str(Row, TEXT("tagline"));
		Event.AnnounceAt = Stamp(Row, TEXT("announce_at"));
		{
			// "YYYY-MM-DD": a calendar date, not an instant - parsed as that day, never shifted by a zone.
			const FString Day = Str(Row, TEXT("festival_day"));
			int32 Year = 0, Month = 0, Date = 0;
			TArray<FString> Parts;
			if (Day.ParseIntoArray(Parts, TEXT("-")) == 3)
			{
				Year = FCString::Atoi(*Parts[0]);
				Month = FCString::Atoi(*Parts[1]);
				Date = FCString::Atoi(*Parts[2]);
			}
			if (FDateTime::Validate(Year, Month, Date, 0, 0, 0, 0))
			{
				Event.FestivalDay = FDateTime(Year, Month, Date);
			}
		}
		const TSharedPtr<FJsonObject> Modifiers = Obj(Row, TEXT("modifiers"));
		// Absent means 1.0 by the admin schema's own account; a published 0 would be a trap.
		if (const double Xp = Num(Modifiers, TEXT("xp_multiplier")); Xp > 0.0)
		{
			Event.XpMultiplier = float(Xp);
		}
		if (const double Points = Num(Modifiers, TEXT("points_multiplier")); Points > 0.0)
		{
			Event.PointsMultiplier = float(Points);
		}
		const TArray<TSharedPtr<FJsonValue>>* Rewards = nullptr;
		if (Row->TryGetArrayField(TEXT("rewards"), Rewards))
		{
			Event.Rewards.Reserve(Rewards->Num());
			for (const TSharedPtr<FJsonValue>& Each : *Rewards)
			{
				const TSharedPtr<FJsonObject> Item = Each.IsValid() ? Each->AsObject() : nullptr;
				if (!Item.IsValid())
				{
					continue;
				}
				FCampusEventReward& Reward = Event.Rewards.AddDefaulted_GetRef();
				Reward.Kind = Str(Item, TEXT("kind"));
				Reward.ItemId = Str(Item, TEXT("item_id"));
				Reward.Amount = int32(Num(Item, TEXT("amount")));
				const TSharedPtr<FJsonObject> Condition = Obj(Item, TEXT("condition"));
				Reward.Mode = Str(Condition, TEXT("mode"));
				Reward.Matches = int32(Num(Condition, TEXT("matches")));
			}
		}
	}
	Out.bLoaded = true;
}

const FCampusEvent* UCampusBackend::LiveEvent() const
{
	// The SERVER clock (Engagement::Now falls back to the device's when no response has landed), for
	// the same reason every countdown in this game uses it: a player who winds their phone forward
	// must not be shown an event that is not running.
	const FDateTime Now = Engagement.Now();
	for (const FCampusEvent& Event : LiveConfig.Events.Events)
	{
		if (Event.IsLiveAt(Now))
		{
			return &Event;
		}
	}
	return nullptr;
}

TArray<FCampusEvent> UCampusBackend::VisibleEvents() const
{
	TArray<FCampusEvent> Out;
	const FDateTime Now = Engagement.Now();
	const FDateTime Horizon = Now + FTimespan::FromDays(FMath::Max(1, LiveConfig.Events.HorizonDays));
	for (const FCampusEvent& Event : LiveConfig.Events.Events)
	{
		if (Event.EndsAt != FDateTime() && Event.EndsAt <= Now)
		{
			continue;   // over: the blob keeps it until the console next publishes, the lobby does not
		}
		// Further out than the horizon is hidden - unless the console ANNOUNCED it early (migration 034:
		// a festival is listed as upcoming from announce_at, weeks ahead).
		const bool bAnnounced = Event.AnnounceAt != FDateTime() && Event.AnnounceAt <= Now;
		if (Event.StartsAt > Horizon && !bAnnounced)
		{
			continue;
		}
		Out.Add(Event);
	}
	// Live first, then by start: the one thing a player can act on right now belongs at the top.
	Out.Sort([Now](const FCampusEvent& A, const FCampusEvent& B)
	{
		const bool bLiveA = A.IsLiveAt(Now);
		const bool bLiveB = B.IsLiveAt(Now);
		return bLiveA != bLiveB ? bLiveA : A.StartsAt < B.StartsAt;
	});
	return Out;
}

const FCampusEvent* UCampusBackend::FindEvent(int32 EventId) const
{
	return LiveConfig.Events.Events.FindByPredicate([EventId](const FCampusEvent& Row) { return Row.Id == EventId; });
}

// -------------------------------------------------------------------------------------------------
// The player routes

void UCampusBackend::LoadEventProgress(int32 EventId, FEventProgressDone Done)
{
	if (EventId <= 0)
	{
		if (Done) { Done(false, TEXT("That event is not available."), FCampusEventProgress()); }
		return;
	}
	Request(TEXT("GET"), FString::Printf(TEXT("/events/%d/progress"), EventId), nullptr, true, false,
		[this, EventId, Done](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (!Done)
			{
				return;
			}
			if (Status != 200)
			{
				Done(false, ErrorText(Status, Json), FCampusEventProgress());
				return;
			}
			FCampusEventProgress Out;
			Out.bValid = true;
			Out.EventId = EventId;
			const TSharedPtr<FJsonObject> Event = Obj(Json, TEXT("event"));
			Out.Label = Str(Event, TEXT("label"));
			Out.State = Str(Event, TEXT("state"));
			Out.BannerKey = Str(Event, TEXT("banner_key"));
			Out.EndsAt = Stamp(Event, TEXT("ends_at"));
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			if (Json.IsValid() && Json->TryGetArrayField(TEXT("rewards"), Rows))
			{
				Out.Rewards.Reserve(Rows->Num());
				for (const TSharedPtr<FJsonValue>& Value : *Rows)
				{
					const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
					if (!Row.IsValid())
					{
						continue;
					}
					FCampusEventRewardProgress& Reward = Out.Rewards.AddDefaulted_GetRef();
					Reward.Index = int32(Num(Row, TEXT("index")));
					Reward.Kind = Str(Row, TEXT("kind"));
					Reward.ItemId = Str(Row, TEXT("item_id"));
					Reward.Amount = int32(Num(Row, TEXT("amount")));
					Reward.Progress = int32(Num(Row, TEXT("progress")));
					Reward.Target = int32(Num(Row, TEXT("target")));
					Reward.bClaimed = Bool(Row, TEXT("claimed"));
					Reward.bClaimable = Bool(Row, TEXT("claimable"));
					Reward.Mode = Str(Obj(Row, TEXT("condition")), TEXT("mode"));
				}
			}
			Done(true, FString(), Out);
		});
}

void UCampusBackend::ClaimEventReward(int32 EventId, int32 RewardIndex, FDone Done)
{
	if (EventId <= 0 || RewardIndex < 0)
	{
		if (Done) { Done(false, TEXT("That reward is not available.")); }
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("reward_index"), RewardIndex);
	bRewardCapReached = false;
	// IDEMPOTENT: one key per claim attempt, so a retry across a dropped tunnel replays the stored
	// answer instead of being refused by the claim row it already wrote.
	Request(TEXT("POST"), FString::Printf(TEXT("/events/%d/claim"), EventId), Body, true, true,
		[this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status != 200 && Status != 201)
			{
				if (Done) { Done(false, ErrorText(Status, Json)); }
				return;
			}
			// The wallet comes back on the response; taking the counters from it rather than adding up
			// locally is what keeps them honest when two devices share one account.
			ReadWallet(Obj(Json, TEXT("wallet")));
			if (Done) { Done(true, FString()); }
		});
}
