// AI bot soldiers: ACampusBotController possesses the SAME ACampusCharacter a player gets and shoots
// through the SAME server fire path (ACampusCharacter::ServerFire), so hit registration, lag
// compensation, falloff, hit zones, team damage, the kill feed and the scoreboard are identical.
// A bot is a real APlayerState (bWantsPlayerState), so teams / roster / result table see it like a phone.
//
// No Behavior Tree, no blackboard, no perception component: one cheap state machine (CampusBot.cpp)
// whose update rate steps 20 / 10 / 4 / 2 Hz by distance to the nearest human, because the host is a
// phone. Docs/BATTLE_ROYALE_DESIGN.md caps PHYSICAL bots at MaxPhysicalBots (10) - the rest of a BR
// lobby is simulated as data elsewhere - and the whole level at ~12 animated characters.
// Everything the bot does to its pawn is server-side; bots never exist on a client.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Campus/CampusMatchState.h"
#include "CampusBot.generated.h"

class ACampusCharacter;

/** Bots are meant to be beatable: both levels shoot late and wide compared with a human. */
UENUM()
enum class ECampusBotSkill : uint8
{
	Easy,
	Normal
};

UCLASS()
class ACampusBotController : public AAIController
{
	GENERATED_BODY()

public:
	ACampusBotController();

	/** Hard cap for one world: ten bodies is what a phone host can animate, move and replicate next to
	 *  its own squad (Docs/BATTLE_ROYALE_DESIGN.md). SpawnBots clamps to what is left of it. */
	static constexpr int32 MaxPhysicalBots = 10;

	/** Spawns Count bots and lets them restart into the match themselves.
	 *  TeamIds: 0 red, 1 blue, taken round-robin (empty = balanced against the room).
	 *  Spawns: optional start transforms; when empty the game mode's team pads are used. */
	static void SpawnBots(UWorld* World, int32 Count, const TArray<int32>& TeamIds, const TArray<FTransform>& Spawns);

	/** campus.FillBotsTDM: fills a team-deathmatch room up to its TeamSize per side. No-op when the cvar
	 *  is 0 (default). Called by ACampusMatchGameMode::RequestStart. */
	static void FillTeamDeathmatch(UWorld* World);

	/** Every bot in this world, for the host's own bookkeeping. */
	static int32 CountBots(const UWorld* World);

	/** Post-match table only: "BOT" next to a bot's name. Empty for a human. In-match UI must NOT use
	 *  this - a bot is supposed to read like another player while you are playing against it. */
	static FString PostMatchTag(const APlayerState* State);

	ECampusBotSkill GetSkill() const { return Skill; }
	void SetSkill(ECampusBotSkill InSkill);

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The bot owns its control rotation (AimAt): the base version would point it at the path's focal
	 *  point and flatten the pitch to zero, so a bot could never aim up or down. All this keeps is the
	 *  half that matters - handing the rotation to the pawn, which is what bUseControllerRotationYaw needs. */
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn) override;

private:
	enum class EState : uint8
	{
		Roam,
		Engage,
		Cover
	};

	/** No pawn: back in at the start of a new round (elimination keeps a dead bot out of the current one),
	 *  or straight away outside a round. Mirrors what ACampusMatchGameMode does for player controllers. */
	void TickRespawn(const ACampusGameState* State);
	/** 4 Hz: nearest enemy in the view cone with a clear line, kept for MemorySeconds after it breaks. */
	void TickSenses(ACampusCharacter* Self);
	void TickRoam(ACampusCharacter* Self);
	void TickEngage(ACampusCharacter* Self, float DeltaSeconds);
	void TickCover(ACampusCharacter* Self);
	/** 1 Hz: AI update rate (20/10/4/2 Hz), replication frequency and anim tick, all by distance to the
	 *  nearest human-controlled pawn. This is the whole performance story for a phone host. */
	void TickBudget(ACampusCharacter* Self);

	/** Turns the aim toward Point at the skill's turn rate, plus the drifting human-like error. */
	void AimAt(ACampusCharacter* Self, const FVector& Point, float DeltaSeconds);
	/** One shot down the current aim through the server fire path. */
	void FireOnce(ACampusCharacter* Self);
	/** Server-side magazine: reloads and reports whether the gun can shoot at all right now. */
	bool ManageAmmo(ACampusCharacter* Self);
	/** Walk / strafe through the character's own stick path (per frame, with its own speed rules). */
	void SetStick(ACampusCharacter* Self, const FVector2D& Axis);
	void PickRoamPoint(ACampusCharacter* Self);
	/** A nearby spot with no line to the current target. Nav when there is a mesh, else straight back. */
	bool PickCoverPoint(ACampusCharacter* Self, FVector& OutPoint) const;
	bool MoveToPoint(ACampusCharacter* Self, const FVector& Point);
	bool HasLineOfSight(const ACampusCharacter* Self, const ACampusCharacter* Other) const;
	/** Aim point: chest, with a little lead on a moving target (the lead is deliberately imperfect). */
	FVector AimPointFor(const ACampusCharacter* Target, float Distance) const;

	ACampusPlayerState* GetCampusState() const;

	UPROPERTY()
	ECampusBotSkill Skill = ECampusBotSkill::Normal;

	TWeakObjectPtr<ACampusCharacter> Target;
	EState State = EState::Roam;

	/** Round this bot last spawned into; elimination keeps it out until the next one. */
	int32 SpawnedRound = -1;
	/** When the pawn went missing outside a round, so the game mode's own respawn timer goes first. */
	float PawnLostTime = -1.f;
	float NextSenseTime = 0.f;
	float NextBudgetTime = 0.f;
	float LastSeenTime = -100.f;
	/** When the bot is allowed to start shooting at the target it has just noticed (reaction delay). */
	float ReactionTime = 0.f;
	float NextShotTime = 0.f;
	int32 BurstLeft = 0;
	float NextBurstTime = 0.f;
	float NextRoamTime = 0.f;
	float CoverUntil = 0.f;
	float NextCoverChance = 0.f;
	FVector RoamPoint = FVector::ZeroVector;
	/** Goal of the live path request, so MoveToPoint never re-pathfinds for the same destination. */
	FVector LastMoveGoal = FVector::ZeroVector;
	bool bHasRoamPoint = false;
	/** Drifting aim error in degrees (yaw, pitch), re-rolled every ErrorHoldSeconds. */
	FVector2D AimError = FVector2D::ZeroVector;
	FVector2D AimErrorTarget = FVector2D::ZeroVector;
	float NextErrorTime = 0.f;
	/** Strafe direction while engaging, flipped now and then so bots do not line-dance in step. */
	float StrafeSign = 1.f;
	float NextStrafeTime = 0.f;
};
