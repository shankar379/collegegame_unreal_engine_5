// Local profile: name, coins, owned items, skills and stats. Lives on the device until the
// backend (Phase 1) takes over; the field names match the planned API so the move is mechanical.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CampusSave.generated.h"

UCLASS()
class UCampusSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** Loads the profile, creating a fresh guest on first launch. */
	static UCampusSave* LoadOrCreate();
	void Persist();

	int32 GetLevel() const { return 1 + Xp / 500; }

	/** Spends coins if there are enough. */
	bool TrySpend(int32 Amount);

	/** Daily reward is available once per calendar day (device time). */
	bool CanClaimDaily() const;
	void ClaimDaily(int32 Amount);

	UPROPERTY()
	FString PlayerName;

	UPROPERTY()
	int32 Coins = 500;

	UPROPERTY()
	int32 Xp = 0;

	UPROPERTY()
	TArray<FName> OwnedItems;

	UPROPERTY()
	FName EquippedOutfit = TEXT("Default");

	UPROPERTY()
	FName EquippedGun = TEXT("Rifle");

	UPROPERTY()
	TMap<FName, int32> SkillLevels;

	UPROPERTY()
	int32 Matches = 0;

	UPROPERTY()
	int32 Wins = 0;

	UPROPERTY()
	int32 Kills = 0;

	UPROPERTY()
	FString LastDailyClaim;
};
