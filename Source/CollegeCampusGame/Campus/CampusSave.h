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

	/** Lobby choice: "Classic" (college, 4 rounds) or "RealMap" (up to 50 players). */
	UPROPERTY()
	FName MatchMap = TEXT("Classic");

	/** Lobby choice: 1 solo, 2 duo, 4 squad. */
	UPROPERTY()
	int32 TeamSize = 4;

	/** Custom Room choices (remembered): rounds per match (4-7, all played) and minutes per round. */
	UPROPERTY()
	int32 MatchRounds = 5;

	UPROPERTY()
	int32 RoundMinutes = 5;

	// ---- backend session (UCampusBackend). The device id + secret ARE the guest account: lose them and the
	// guest is gone, which is why the lobby keeps suggesting "Register to keep your progress".
	UPROPERTY()
	FString DeviceId;

	UPROPERTY()
	FString DeviceSecret;

	/** Opaque, single-use, rotated on every refresh. Empty = signed out. */
	UPROPERTY()
	FString RefreshToken;

	/** Terms + privacy notice accepted (shown once, before any account exists). */
	UPROPERTY()
	bool bAgreedTerms = false;

	/** First-time nickname chosen (guests get one generated until then). */
	UPROPERTY()
	bool bNicknameChosen = false;

	// ---- settings (the gear in the lobby top-right and in the match HUD top-right). Added fields
	// default sensibly, so an old save file loads with no migration.

	/** Touch look speed multiplier, 0.6 .. 1.6. */
	UPROPERTY()
	float LookSensitivity = 1.f;

	/** 0 smooth (lowest), 1 balanced, 2 HD. Pushed into the engine scalability levels. */
	UPROPERTY()
	int32 GraphicsQuality = 1;

	/** Team voice: whether the mic / speaker start on when a match loads. */
	UPROPERTY()
	bool bMicDefaultOn = false;

	UPROPERTY()
	bool bSpeakerDefaultOn = true;

	/** Second, smaller fire button on the left of the HUD (two-thumb shooting). */
	UPROPERTY()
	bool bLeftFireButton = true;

	UPROPERTY()
	bool bShowMinimap = true;

	// ---- 18+ gate and engagement (streak / missions / weekend board)

	/** The "I am 18 or older" tick. Sent with every sign-in; the server stores the confirmation, not an age. */
	UPROPERTY()
	bool bAdultConfirmed = false;

	/** The last GET /engagement/home body, so the lobby paints before the network answers. */
	UPROPERTY()
	FString EngagementCache;

	/** IST game day (YYYYMMDD) the streak popup was last shown, so it opens once a day and never nags. */
	UPROPERTY()
	FString StreakPopupDay;
};
