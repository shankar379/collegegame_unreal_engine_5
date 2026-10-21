// WHAT BRINGS THE PLAYER BACK TOMORROW (UCampusBackend): the login streak, the daily and weekly
// missions, the weekend board, and the claim for each.
//
//   GET  /engagement/home   ONE call on lobby entry for all of it. The raw body is cached in the
//                           save so the next launch paints the panels before the network answers.
//   POST /missions/:id/claim, /me/streak/claim, /periods/:id/claim
//
// EVERY CLAIM ANSWERS WITH THE NEW WALLET, and taking the counters from that response rather than
// adding up locally is what keeps them honest when two devices share an account.
//
// THE COUNTDOWNS DO NOT TRUST THE PHONE'S CLOCK. FCampusEngagement::Now() is the server's time
// plus the elapsed seconds since the response landed, so a player who winds their device forward
// gains nothing and one whose clock is simply wrong is not told a window is shut when it is open.
// The value types that do that arithmetic live here with the call that fills them.
//
// Split out of Campus/CampusBackend.cpp; Campus/CampusBackendInternal.h carries the file map.

#include "Campus/CampusBackend.h"

#include "Campus/CampusBackendInternal.h"
#include "Campus/CampusSave.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

using namespace CampusApi;

// -------------------------------------------------------------------------------------------------
// Engagement value types

FString FCampusReward::Describe() const
{
	TArray<FString> Parts;
	if (Diamonds > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d DIAMONDS"), Diamonds));
	}
	if (RoomCards > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d ROOM CARD%s"), RoomCards, RoomCards == 1 ? TEXT("") : TEXT("S")));
	}
	if (bTitle)
	{
		Parts.Add(TEXT("A TITLE"));
	}
	for (const FString& Item : Items)
	{
		Parts.Add(Item.ToUpper());
	}
	if (Xp > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d XP"), Xp));
	}
	if (PassPoints > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d PASS PTS"), PassPoints));
	}
	return Parts.Num() ? FString::Join(Parts, TEXT(" + ")) : FString();
}

// -------------------------------------------------------------------------------------------------
// CAMPUS PASS (migration 031)

void UCampusBackend::ReadPass(TSharedPtr<FJsonObject> Json)
{
	FCampusPass Out;
	const TSharedPtr<FJsonObject> Season = Obj(Json, TEXT("season"));
	if (!Json.IsValid() || !Season.IsValid())
	{
		Pass = Out; // between seasons: bValid stays false and the screen says so
		return;
	}
	Out.bValid = true;
	Out.SeasonName = Str(Season, TEXT("name"));
	Out.EndsAt = Stamp(Season, TEXT("ends_at"));
	Out.MaxLevel = int32(Num(Season, TEXT("max_level")));
	Out.PointsPerLevel = int32(Num(Season, TEXT("points_per_level")));
	Out.PremiumPrice = int32(Num(Season, TEXT("premium_price_diamonds")));
	Out.Points = int32(Num(Json, TEXT("points")));
	Out.Level = int32(Num(Json, TEXT("level")));
	Out.PointsIntoLevel = int32(Num(Json, TEXT("points_into_level")));
	Out.bPremium = Bool(Json, TEXT("premium"));
	Out.Claimable = int32(Num(Json, TEXT("claimable")));
	const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
	if (Json->TryGetArrayField(TEXT("levels"), Levels))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Levels)
		{
			const TSharedPtr<FJsonObject> Item = Value->AsObject();
			FCampusPassLevel& Row = Out.Levels.AddDefaulted_GetRef();
			Row.Level = int32(Num(Item, TEXT("level")));
			const TSharedPtr<FJsonObject> Free = Obj(Item, TEXT("free"));
			const TSharedPtr<FJsonObject> Prem = Obj(Item, TEXT("premium"));
			Row.Free = ReadReward(Obj(Free, TEXT("reward")));
			Row.FreeState = Str(Free, TEXT("state"));
			Row.Premium = ReadReward(Obj(Prem, TEXT("reward")));
			Row.PremiumState = Str(Prem, TEXT("state"));
		}
	}
	Pass = MoveTemp(Out);
}

void UCampusBackend::LoadPass(FDone Done)
{
	Request(TEXT("GET"), TEXT("/pass"), nullptr, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadPass(Json);
		Done(true, FString());
	});
}

void UCampusBackend::BuyPassPremium(FDone Done)
{
	Request(TEXT("POST"), TEXT("/pass/premium"), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 201)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadWallet(Obj(Json, TEXT("wallet")));
		ReadPass(Obj(Json, TEXT("pass")));
		Done(true, FString());
	});
}

void UCampusBackend::ClaimPass(int32 PassLevel, const FString& Track, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("level"), PassLevel);
	Body->SetStringField(TEXT("track"), Track);
	Request(TEXT("POST"), TEXT("/pass/claim"), Body, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadWallet(Obj(Json, TEXT("wallet")));
		ReadPass(Obj(Json, TEXT("pass")));
		Done(true, FString());
	});
}

int32 FCampusMissionPanel::ClaimableCount() const
{
	int32 Count = 0;
	for (const FCampusMission& Item : Items)
	{
		Count += Item.IsClaimable() ? 1 : 0;
	}
	// the set bonus is one more thing to tap
	Count += bHasBonus && BonusState == TEXT("claimable") ? 1 : 0;
	return Count;
}

FDateTime FCampusEngagement::Now() const
{
	if (ServerTime == FDateTime())
	{
		return FDateTime::UtcNow();
	}
	// The phone's wall clock is never trusted for a countdown (players change it); only the elapsed
	// time since the response landed is taken from the device.
	const double Elapsed = FMath::Max(0.0, FPlatformTime::Seconds() - ReadAt);
	return ServerTime + FTimespan::FromSeconds(Elapsed);
}

int32 FCampusEngagement::SecondsUntil(const FDateTime& When) const
{
	if (When == FDateTime())
	{
		return 0;
	}
	return FMath::Max(0, int32((When - Now()).GetTotalSeconds()));
}

int32 FCampusEngagement::TotalClaimable() const
{
	return Daily.ClaimableCount() + Weekly.ClaimableCount() + WeekendMissions.ClaimableCount();
}

// -------------------------------------------------------------------------------------------------
// Engagement: streak, missions, weekend board

void UCampusBackend::ReadEngagement(TSharedPtr<FJsonObject> Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	FCampusEngagement& Out = Engagement;
	Out.bLoaded = true;
	Out.ServerTime = Stamp(Json, TEXT("server_time"));
	Out.ReadAt = FPlatformTime::Seconds();
	Out.bDailyCardAvailable = Bool(Json, TEXT("daily_card_available"));
	bDailyCardAvailable = Out.bDailyCardAvailable;

	// ---- streak
	const TSharedPtr<FJsonObject> Streak = Obj(Json, TEXT("streak"));
	Out.Streak = FCampusStreak();
	if (Streak.IsValid())
	{
		Out.Streak.CurrentLen = int32(Num(Streak, TEXT("current_len")));
		Out.Streak.BestLen = int32(Num(Streak, TEXT("best_len")));
		Out.Streak.CycleDay = int32(Num(Streak, TEXT("cycle_day")));
		Out.Streak.bClaimable = Bool(Streak, TEXT("claimable"));
		Out.Streak.TodayReward = ReadReward(Obj(Streak, TEXT("today_reward")));
		Out.Streak.ResetsAt = Stamp(Streak, TEXT("resets_at"));
		const TArray<TSharedPtr<FJsonValue>>* Ladder = nullptr;
		if (Streak->TryGetArrayField(TEXT("ladder"), Ladder))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Ladder)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusStreakStep& Step = Out.Streak.Ladder.AddDefaulted_GetRef();
				Step.Day = int32(Num(Item, TEXT("day")));
				Step.Reward = ReadReward(Obj(Item, TEXT("reward")));
				Step.State = Str(Item, TEXT("state"));
			}
		}
	}

	// ---- missions: the same shape three times
	auto ReadPanel = [](const TSharedPtr<FJsonObject>& Panel)
	{
		FCampusMissionPanel Out2;
		if (!Panel.IsValid())
		{
			return Out2;
		}
		Out2.bValid = true;
		Out2.PeriodId = Str(Panel, TEXT("period_id"));
		Out2.EndsAt = Stamp(Panel, TEXT("ends_at"));
		const TSharedPtr<FJsonObject> Bonus = Obj(Panel, TEXT("bonus"));
		if (Bonus.IsValid())
		{
			Out2.bHasBonus = true;
			Out2.BonusNeed = int32(Num(Bonus, TEXT("need")));
			Out2.BonusDone = int32(Num(Bonus, TEXT("done")));
			Out2.BonusState = Str(Bonus, TEXT("state"));
			Out2.BonusReward = ReadReward(Obj(Bonus, TEXT("reward")));
		}
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (Panel->TryGetArrayField(TEXT("items"), Items))
		{
			Out2.Items.Reserve(Items->Num());
			for (const TSharedPtr<FJsonValue>& Value : *Items)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusMission& Mission = Out2.Items.AddDefaulted_GetRef();
				Mission.Id = Str(Item, TEXT("mission_id"));
				Mission.Title = Str(Item, TEXT("title"));
				Mission.Subtitle = Str(Item, TEXT("subtitle"));
				Mission.State = Str(Item, TEXT("state"));
				Mission.Progress = int32(Num(Item, TEXT("progress")));
				Mission.Target = FMath::Max(1, int32(Num(Item, TEXT("target"))));
				Mission.Reward = ReadReward(Obj(Item, TEXT("reward")));
			}
		}
		// The API already sorts claimable first; keep that order rather than re-sorting it here.
		return Out2;
	};
	const TSharedPtr<FJsonObject> Missions = Obj(Json, TEXT("missions"));
	Out.Daily = ReadPanel(Obj(Missions, TEXT("daily")));
	Out.Weekly = ReadPanel(Obj(Missions, TEXT("weekly")));
	Out.WeekendMissions = ReadPanel(Obj(Missions, TEXT("weekend")));

	// ---- weekend board
	Out.Board = FCampusWeekend();
	if (const TSharedPtr<FJsonObject> Weekend = Obj(Json, TEXT("weekend")); Weekend.IsValid())
	{
		FCampusWeekend& Board = Out.Board;
		Board.bValid = true;
		Board.PeriodId = Str(Weekend, TEXT("period_id"));
		Board.Label = Str(Weekend, TEXT("label"));
		Board.State = Str(Weekend, TEXT("state"));
		Board.StartsAt = Stamp(Weekend, TEXT("starts_at"));
		Board.EndsAt = Stamp(Weekend, TEXT("ends_at"));
		// Anything short of 'settled' is provisional, and the board must say so.
		Board.bProvisional = Board.State != TEXT("settled");
		Board.bDegraded = Bool(Weekend, TEXT("degraded"));
		const TArray<TSharedPtr<FJsonValue>>* Top = nullptr;
		if (Weekend->TryGetArrayField(TEXT("top"), Top))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Top)
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				FCampusBoardEntry& Entry = Board.Top.AddDefaulted_GetRef();
				Entry.Rank = int32(Num(Item, TEXT("rank")));
				Entry.Name = Str(Item, TEXT("display_name"));
				Entry.College = Str(Item, TEXT("college"));
				Entry.Points = int32(Num(Item, TEXT("points")));
				Entry.Kills = int32(Num(Item, TEXT("kills")));
				Entry.bMe = Bool(Item, TEXT("is_me"));
			}
		}
		// me is null until the player has played a rated match this weekend: that is the empty state.
		if (const TSharedPtr<FJsonObject> Me = Obj(Weekend, TEXT("me")); Me.IsValid())
		{
			Board.bHasMe = true;
			Board.MyRank = int32(Num(Me, TEXT("rank")));
			Board.MyPoints = int32(Num(Me, TEXT("points")));
			Board.MyRatedMatches = int32(Num(Me, TEXT("rated_matches")));
			Board.ProjectedBracket = Str(Me, TEXT("projected_reward"));
			if (const TSharedPtr<FJsonObject> Next = Obj(Me, TEXT("next_rank")); Next.IsValid())
			{
				Board.bHasNextRank = true;
				Board.NextRank = int32(Num(Next, TEXT("rank")));
				Board.NextGap = int32(Num(Next, TEXT("gap")));
			}
			// projected_reward may arrive as an object with a bracket rather than a bare string
			if (Board.ProjectedBracket.IsEmpty())
			{
				Board.ProjectedBracket = Str(Obj(Me, TEXT("projected_reward")), TEXT("bracket"));
			}
		}
	}

	// ---- last weekend's champion card and unclaimed reward
	Out.LastWeekend = FCampusLastWeekend();
	if (const TSharedPtr<FJsonObject> Last = Obj(Json, TEXT("last_weekend")); Last.IsValid())
	{
		FCampusLastWeekend& Prev = Out.LastWeekend;
		Prev.bValid = true;
		Prev.PeriodId = Str(Last, TEXT("period_id"));
		Prev.ChampionName = Str(Obj(Last, TEXT("champion")), TEXT("display_name"));
		Prev.MyRank = int32(Num(Last, TEXT("my_rank")));
		if (const TSharedPtr<FJsonObject> Unclaimed = Obj(Last, TEXT("unclaimed_reward")); Unclaimed.IsValid())
		{
			Prev.bUnclaimed = true;
			Prev.Bracket = Str(Unclaimed, TEXT("bracket"));
			Prev.Reward = ReadReward(Unclaimed);
		}
	}
}

void UCampusBackend::LoadEngagementFromCache()
{
	const UCampusSave* Profile = Save();
	if (Engagement.bLoaded || !Profile || Profile->EngagementCache.IsEmpty())
	{
		return;
	}
	TSharedPtr<FJsonObject> Json;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Profile->EngagementCache), Json))
	{
		ReadEngagement(Json);
		// A cached response is a stale snapshot: its countdowns must keep running from the server
		// time it carried, which ReadEngagement already anchors against. Nothing else to do.
	}
}

void UCampusBackend::LoadEngagementHome(FDone Done)
{
	Request(TEXT("GET"), TEXT("/engagement/home"), nullptr, true, false, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadEngagement(Json);
		// Cache the raw body so the next launch paints the lobby before the network answers.
		if (UCampusSave* Profile = Save(); Profile && Json.IsValid())
		{
			FString Text;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
			if (FJsonSerializer::Serialize(Json.ToSharedRef(), Writer))
			{
				Profile->EngagementCache = Text;
				Profile->Persist();
			}
		}
		Done(true, FString());
	});
}

/** Every claim answers with the new wallet; taking it from the response is what keeps the counters honest. */
void UCampusBackend::ClaimMission(const FString& PeriodId, const FString& MissionId, FDone Done)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("period_id"), PeriodId);
	bRewardCapReached = false;
	Request(TEXT("POST"), FString::Printf(TEXT("/missions/%s/claim"), *MissionId), Body, true, true,
		[this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status != 200)
			{
				bRewardCapReached = Status == 429 && Str(Obj(Json, TEXT("error")), TEXT("code")) == TEXT("DAILY_REWARD_CAP");
				Done(false, ErrorText(Status, Json));
				return;
			}
			ReadWallet(Obj(Json, TEXT("wallet")));
			Done(true, FString());
		});
}

void UCampusBackend::ClaimStreak(FDone Done)
{
	bRewardCapReached = false;
	Request(TEXT("POST"), TEXT("/me/streak/claim"), nullptr, true, true, [this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
	{
		if (Status != 200)
		{
			bRewardCapReached = Status == 429 && Str(Obj(Json, TEXT("error")), TEXT("code")) == TEXT("DAILY_REWARD_CAP");
			Done(false, ErrorText(Status, Json));
			return;
		}
		ReadWallet(Obj(Json, TEXT("wallet")));
		// Claimed: the popup must not reappear until the ladder next advances.
		Engagement.Streak.bClaimable = false;
		Engagement.Streak.CurrentLen = int32(Num(Json, TEXT("current_len")));
		Engagement.Streak.BestLen = int32(Num(Json, TEXT("best_len")));
		Done(true, FString());
	});
}

void UCampusBackend::ClaimPeriodReward(const FString& PeriodId, FDone Done)
{
	bRewardCapReached = false;
	Request(TEXT("POST"), FString::Printf(TEXT("/rewards/%s/claim"), *PeriodId), nullptr, true, true,
		[this, Done](int32 Status, TSharedPtr<FJsonObject> Json)
		{
			if (Status != 200)
			{
				bRewardCapReached = Status == 429 && Str(Obj(Json, TEXT("error")), TEXT("code")) == TEXT("DAILY_REWARD_CAP");
				Done(false, ErrorText(Status, Json));
				return;
			}
			ReadWallet(Obj(Json, TEXT("wallet")));
			Engagement.LastWeekend.bUnclaimed = false;
			Done(true, FString());
		});
}
