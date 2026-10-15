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

	// THERE IS NO GetLevel() HERE ANY MORE, DELIBERATELY. Level and XP are server state (migration 013,
	// PROFILE_DESIGN §3): the curve is a table in engagement_config and the client never computes a level.
	// The old `1 + Xp / 500` was a number the phone invented - two phones looking at the same player saw
	// different levels, and every squad name plate printed that same local guess for everybody else.
	// Read UCampusBackend::Level / XpIntoLevel / XpForNext instead; the three fields below are only the
	// cache that lets the lobby paint before the first response lands.

	/** Spends coins if there are enough. */
	bool TrySpend(int32 Amount);

	/** Daily reward is available once per calendar day (device time). */
	bool CanClaimDaily() const;
	void ClaimDaily(int32 Amount);

	UPROPERTY()
	FString PlayerName;

	/** The server's last word on my profile picture (`player.avatar_url`), so the lobby badge paints the
	 *  picture on frame one from the disk cache instead of a level number that flips a second later.
	 *  The URL is versioned by the server (?v=<n>), so a stale one here can only ever be a stale
	 *  PICTURE for one launch - never a wrong one, because the bytes are cached under the URL. */
	UPROPERTY()
	FString AvatarUrl;

	UPROPERTY()
	int32 Coins = 500;

	UPROPERTY()
	int32 Xp = 0;

	UPROPERTY()
	TArray<FName> OwnedItems;

	/** COSMETICS: the outfit id from CampusCosmetics. Written the moment the wardrobe equips one, so
	 *  the lobby figure and the next match are right whether the PUT lands or not; the server's answer
	 *  on load overwrites it (GET /me/cosmetics is the truth). An id this build does not know - the
	 *  old "Default" every existing save on a phone still holds - reads as the starter outfit. */
	UPROPERTY()
	FName EquippedOutfit = TEXT("outfit_starter");

	/** The three wardrobe presets, by outfit id. NAME_None = that slot has never been saved. Sized on
	 *  read, not here: an old save file has none and must still load. */
	UPROPERTY()
	TArray<FName> OutfitPresets;

	/** LOBBY DECORATION: anchor id (CampusCosmetics::FAnchor::Id) -> decor item id. An anchor that is
	 *  not in the map is bare, which is how seven of the eight start. */
	UPROPERTY()
	TMap<FName, FName> LobbyDecor;

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

	/** Map chosen on the map-select screen; the id from CampusCatalog::Maps. "Classic" is the college
	 *  room (L_Match), the default. A saved id this version does not have - or one the saved MODE does
	 *  not run on, like an old "RealMap" - is corrected by CampusCatalog::MapForMode when it is read,
	 *  so nothing here can open the wrong level. */
	UPROPERTY()
	FName MatchMap = TEXT("Classic");

	/** Mode chosen on the same screen; the id from CampusCatalog::Modes. */
	UPROPERTY()
	FName MatchMode = TEXT("TeamClash");

	/** "Random map" ticked on the map-select screen. Parked: a mode offers several maps now, but
	 *  nothing rolls one yet (CampusMapSelect's RandomToggle says why). */
	UPROPERTY()
	bool bRandomMap = false;

	/** BATTLE ROYALE: the DAY / NIGHT chip on the mode card. A preference only - it rides along on
	 *  POST /br/queue and the server's `day_night` has the final word on the match that comes back. */
	UPROPERTY()
	bool bBrNight = false;

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

	// ---- live config (GET /v1/live-config, admin console phase A). The last PUBLISHED blob and its
	// ETag, so the lobby knows the minimum build, the live announcements and the maintenance message on
	// the first frame and offline - and so the next fetch costs one conditional request on a tunnel
	// metered at 20,000 a month. MAINTENANCE IS DELIBERATELY NOT TRUSTED FROM HERE: a lock screen is the
	// one thing a stale cache must never invent, so UCampusBackend drops `maintenance.on` when it reads
	// this back and only a live response can raise it.

	/** The strong ETag the server gave the cached blob ("lc-<revision>"), sent back as If-None-Match. */
	UPROPERTY()
	FString LiveConfigEtag;

	/** The last 200 body of GET /v1/live-config, verbatim. */
	UPROPERTY()
	FString LiveConfigCache;

	/** Announcement ids this player has dismissed, so a card never comes back. The server's numeric id
	 *  as TEXT: an old save keeps loading if that type ever widens, and nothing here has to do maths. */
	UPROPERTY()
	TArray<FString> DismissedAnnouncements;

	// ---- social (UCampusSocial, migration 012) and server-side progression (migration 013)

	/** The last social event sequence this phone APPLIED. Persisted so an app restart does not replay
	 *  yesterday's invite cards; reset to the `hello` frame's next_seq after a full re-read. */
	UPROPERTY()
	int64 SocialSeq = 0;

	/** The server's last word on my level and XP, so the top bar and the profile paint on frame one and
	 *  then repaint when GET /me/profile lands. Never computed here - see the note above. */
	UPROPERTY()
	int32 ServerLevel = 1;

	UPROPERTY()
	int32 ServerXpInto = 0;

	UPROPERTY()
	int32 ServerXpForNext = 0;
};
