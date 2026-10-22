#include "Campus/CampusBRGameMode.h"

#include "Campus/CampusBR.h"
#include "Campus/CampusBRPlane.h"
#include "Campus/CampusBRZone.h"
#include "Campus/CampusBackend.h"
#include "Campus/CampusCharacter.h"
#include "Campus/CampusDayNight.h"
#include "Campus/CampusLoot.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusPlayerControllers.h"
#include "Campus/CampusZipline.h"
#include "AIController.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/GameSession.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

namespace CampusBRRoster
{
	/** Virtual combatants think at 2 Hz. Fifty rows at 2 Hz is nothing; fifty pawns is a dead phone. */
	constexpr float VirtualInterval = 0.5f;
	/** Promotion bubble (design §3.3). On this 227 m campus that is "anywhere", which is exactly right:
	 *  the ten physical slots should always be full while humans are alive. */
	constexpr float PromotionRadius = 30000.f;
	/** Virtual drift toward the circle, cm/s (a plausible jog). */
	constexpr float DriftSpeed = 420.f;

	/** Attrition curve (design §3.3): seconds into the match -> alive count the players should see. */
	struct FPoint { float Seconds; float Alive; };
	const FPoint Curve[] = {
		{ 0.f, 50.f }, { 90.f, 34.f }, { 180.f, 24.f }, { 300.f, 15.f }, { 420.f, 8.f }, { 540.f, 4.f },
	};

	const TCHAR* BotNames[] = {
		TEXT("Arjun"), TEXT("Rohit"), TEXT("Kiran"), TEXT("Deepak"), TEXT("Sahil"), TEXT("Varun"),
		TEXT("Nikhil"), TEXT("Aditya"), TEXT("Manish"), TEXT("Pranav"), TEXT("Rahul"), TEXT("Sandeep"),
		TEXT("Yash"), TEXT("Imran"), TEXT("Vivek"), TEXT("Harsh"), TEXT("Gaurav"), TEXT("Tarun"),
	};

	TWeakObjectPtr<UObject> BotFactory;

	/** ONE sentence for both doors - PreLogin's refusal and the kick - so a late joiner cannot be told
	 *  two different stories about the same closed session. */
	FText SealRefusal()
	{
		return NSLOCTEXT("CampusBR", "BrSessionSealed",
			"That battle session has already started. Press START for the next one.");
	}
}

void ACampusBRGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACampusBRGameState, BRPhase);
	DOREPLIFETIME(ACampusBRGameState, BRPhaseEndTime);
	DOREPLIFETIME(ACampusBRGameState, AliveCombatants);
	DOREPLIFETIME(ACampusBRGameState, TotalCombatants);
	DOREPLIFETIME(ACampusBRGameState, AliveSquads);
	DOREPLIFETIME(ACampusBRGameState, TotalSquads);
	DOREPLIFETIME(ACampusBRGameState, MatchSeconds);
	DOREPLIFETIME(ACampusBRGameState, WinningSquad);
	DOREPLIFETIME(ACampusBRGameState, HumansJoined);
	DOREPLIFETIME(ACampusBRGameState, Zone);
	DOREPLIFETIME(ACampusBRGameState, HumanCap);
	DOREPLIFETIME(ACampusBRGameState, SessionId);
}

ACampusBRGameMode::ACampusBRGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = ACampusCharacter::StaticClass();
	PlayerControllerClass = ACampusMatchPlayerController::StaticClass();
	PlayerStateClass = ACampusPlayerState::StaticClass();
	GameStateClass = ACampusBRGameState::StaticClass();
}

FString ACampusBRGameMode::HostOptionsFor(const FString& MapChoice)
{
	// "?Mode=BR" is what the match reads; "?game=" is what the engine needs to pick the class at all
	// (UWorld::GetGameModeForURL), and a game mode cannot select itself.
	return MapChoice == TEXT("BR")
		? FString(TEXT("?Mode=BR?game=/Script/CollegeCampusGame.CampusBRGameMode"))
		: FString();
}

void ACampusBRGameMode::SetBotFactory(UObject* Factory)
{
	CampusBRRoster::BotFactory = Factory;
}

ACampusBRGameState* ACampusBRGameMode::GetBRState() const
{
	return GetGameState<ACampusBRGameState>();
}

void ACampusBRGameMode::InitGameState()
{
	Super::InitGameState();
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	FString Code = UGameplayStatics::ParseOption(OptionsString, TEXT("Code")).ToUpper();
	if (Code.IsEmpty())
	{
		Code = UGameplayStatics::ParseOption(OptionsString, TEXT("Room")).ToUpper();
	}
	if (Code.IsEmpty())
	{
		static const TCHAR Letters[] = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Code.AppendChar(Letters[FMath::RandRange(0, UE_ARRAY_COUNT(Letters) - 2)]);
		}
	}
	State->RoomCode = Code;
	// THE LEVEL SAYS WHICH MAP THIS IS, because the level is what the allocator's map id already chose
	// (CampusBR::LevelForMap) and it cannot be wrong about itself. "RealMap" stays the answer for a
	// level the catalogue does not know, which is the legacy id every reader already handles.
	const FName MapId = CampusCatalog::MapIdForLevel(GetWorld()->GetMapName());
	State->MatchMap = MapId.IsNone() ? FName(TEXT("RealMap")) : MapId;
	// BS-SQUAD-IN-000731, carried in by the host's travel URL. The HUD prints it; nothing keys off it.
	State->SessionId = UGameplayStatics::ParseOption(OptionsString, TEXT("Session"));

	const int32 TeamOption = UGameplayStatics::GetIntOption(OptionsString, TEXT("Team"), 4);
	SquadSize = TeamOption == 1 || TeamOption == 2 ? TeamOption : 4;
	MaxHumans = FMath::Clamp(UGameplayStatics::GetIntOption(OptionsString, TEXT("Humans"), MaxHumans), 1, 16);
	// The SERVER's fill cap ("?Fill=", from the session's fill_s), which is all the safety net needs.
	// Only as a deadline to measure the missing seal against - this mode never counts down on it.
	const int32 FillOption = UGameplayStatics::GetIntOption(OptionsString, TEXT("Fill"), 0);
	if (FillOption > 0)
	{
		WaitingSeconds = float(FMath::Clamp(FillOption, 5, 120));
	}
	State->TeamSize = SquadSize;
	State->HumanCap = MaxHumans;
	State->MaxPlayers = MaxHumans;
	State->TotalCombatants = MaxCombatants;
	State->AliveCombatants = MaxCombatants;
	// The SESSION's shape, so the HUD's SQUADS counter does not read "1" at the one moment the first
	// player to arrive is looking at it. RefreshCounts takes over the instant the roster is sealed.
	State->AliveSquads = FMath::Max(1, MaxCombatants / FMath::Max(1, SquadSize));
	State->TotalSquads = State->AliveSquads;
	if (GameSession)
	{
		GameSession->MaxPlayers = MaxHumans;
	}

	WaitingArea = GetWorld()->SpawnActor<ACampusBRWaitingArea>(ACampusBRWaitingArea::StaticClass(),
		FTransform(ACampusBRWaitingArea::Place(GetWorld())));
	ZoneActor = GetWorld()->SpawnActor<ACampusBRZone>(ACampusBRZone::StaticClass(), FTransform::Identity);
	State->Zone = ZoneActor;
	// The sky, on every machine: applies the day / night preset, retires any sky dome the level still
	// has baked in, and places the atmosphere if the level has none. A no-op when the build script
	// already put one in the level.
	ACampusDayNight::EnsureFor(GetWorld());
	// The ziplines this level carries (ACampusZipline::SpawnForLevel's own table), beside the zone and
	// the sky: one pass, on the host, before anybody can be standing at a gantry.
	ACampusZipline::SpawnForLevel(GetWorld());
	State->Phase = ECampusPhase::Waiting;
	EnterPhase(ECampusBRPhase::Waiting);

	// THE BACKEND OWNS THE SEAL (Docs/BATTLE_SESSION_DESIGN.md §4). The queue subsystem hands it over
	// whichever door it came through - `bs.sealed` or the heartbeat response - and this one binding is
	// the whole wire between the two. A client binds it too and simply never gets one for a session it
	// does not host, so there is no authority test to get wrong here.
	if (UCampusBRQueue* Queue = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBRQueue>() : nullptr)
	{
		SealedHandle = Queue->OnSessionSealed.AddLambda([this](int32 Humans, int32 Bots, float Countdown)
		{
			SealSession(Humans, Bots, Countdown);
		});
		// The seal can be OLDER THAN THIS LEVEL: a host that was slow to load, or a player rejoining a
		// session that has already been backfilled. Act on what we already hold rather than waiting for
		// an event that has been and gone.
		if (Queue->IsSealed())
		{
			const FCampusBRMatch& Session = Queue->GetMatch();
			SealSession(Session.Humans, Session.Bots, float(Session.CountdownSeconds));
		}
	}
	// THE SAFETY NET. A seal that never arrives (the socket dropped, the beat 500ed) must not leave a
	// player standing on a slab forever, so the mode seals itself a grace period past the fill cap.
	if (!bSealed)
	{
		GetWorldTimerManager().SetTimer(SealSafetyTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			SealLocally();
		}), FMath::Max(1.f, WaitingSeconds + SealGraceSeconds), false);
	}
}

void ACampusBRGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	// The queue subsystem outlives this level, so its delegate must not outlive this mode.
	if (UCampusBRQueue* Queue = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBRQueue>() : nullptr)
	{
		Queue->OnSessionSealed.Remove(SealedHandle);
	}
	SealedHandle.Reset();
	Super::EndPlay(Reason);
}

void ACampusBRGameMode::EnterPhase(ECampusBRPhase NewPhase)
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	State->BRPhase = NewPhase;
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	const float Now = float(GetWorld()->GetTimeSeconds());

	switch (NewPhase)
	{
	case ECampusBRPhase::Waiting:
		// NO CLOCK OF ITS OWN. The waiting area fills while the BACKEND decides the seal (full, or the
		// fill cap running out), and the only timer on this phase is the safety net armed in
		// InitGameState. The stamp IS that deadline, so the HUD is never shown a promise the mode is not
		// the one making.
		State->BRPhaseEndTime = Now + WaitingSeconds + SealGraceSeconds;
		break;

	case ECampusBRPhase::Countdown:
		FillWithBots();
		State->BRPhaseEndTime = Now + CountdownSeconds;
		GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			EnterPhase(ECampusBRPhase::Flight);
		}), CountdownSeconds, false);
		break;

	case ECampusBRPhase::Flight:
		StartFlight();
		break;

	case ECampusBRPhase::Playing:
		BeginPlaying();
		break;

	case ECampusBRPhase::Ended:
		break;
	}

	// THE HOST'S BEAT (backend docs/BATTLE_SESSION.md §4), and the whole hook for it: the match phase IS
	// the battle session's state, so there is one seam and no second state machine. The beat was already
	// started by the LOBBY (the server deletes an unbeaten session at ten seconds, long before a level
	// loads); this claims it, so from here the beat lives and dies with the match. Stops at Ended, where
	// the result route closes the session instead. No-op off a session-hosted level.
	if (UCampusBRHostBeat* Beat = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBRHostBeat>() : nullptr)
	{
		static const auto BeatFor = [](ECampusBRPhase Phase)
		{
			switch (Phase)
			{
			case ECampusBRPhase::Waiting:   return ECampusBRBeat::Filling;
			case ECampusBRPhase::Flight:    return ECampusBRBeat::InFlight;
			case ECampusBRPhase::Playing:   return ECampusBRBeat::InProgress;
			case ECampusBRPhase::Ended:     return ECampusBRBeat::Ended;
			default:                        return ECampusBRBeat::Countdown;
			}
		};
		TWeakObjectPtr<ACampusBRGameMode> WeakThis(this);
		Beat->Report(this, State->RoomCode, BeatFor(NewPhase), [WeakThis]()
		{
			const ACampusBRGameState* Live = WeakThis.IsValid() ? WeakThis->GetBRState() : nullptr;
			return Live ? Live->AliveCombatants : 0;
		});
	}
}

// ---------------------------------------------------------------------------------------------
// Joining

void ACampusBRGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
	FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	// SEALED MEANS SEALED. The roster is fixed, the bots are in it and the plane is on its way, so a
	// late dial is refused HERE - before a connection, a slot or a pawn exists - with a sentence the
	// joiner can read instead of a loading screen that never ends.
	if (bSealed && ErrorMessage.IsEmpty())
	{
		ErrorMessage = CampusBRRoster::SealRefusal().ToString();
	}
}

FString ACampusBRGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId,
	const FString& Options, const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	// "?Squad=" is the SESSION's team id for this phone and the only thing that can tell a party of
	// two from two strangers who queued one after the other. It has to be read HERE: PostLogin seats
	// the player, and by then the joining URL is gone and join order is all that is left.
	if (ACampusPlayerState* PlayerState = NewPlayerController
		? NewPlayerController->GetPlayerState<ACampusPlayerState>() : nullptr)
	{
		PlayerState->SquadKey = UGameplayStatics::ParseOption(Options, TEXT("Squad"));
		if (PlayerState->SquadKey.IsEmpty() && NewPlayerController->IsLocalPlayerController())
		{
			// The LISTEN HOST's own controller is seated from the LEVEL's URL, not from a join URL.
			// Local only, and deliberately so: handing a joiner who arrived without a team id the
			// host's key would make a stranger the host's squadmate.
			PlayerState->SquadKey = UGameplayStatics::ParseOption(OptionsString, TEXT("Squad"));
		}
	}
	return Error;
}

void ACampusBRGameMode::PostLogin(APlayerController* NewPlayer)
{
	ACampusBRGameState* State = GetBRState();
	// The second door, for a connection that was already past PreLogin when the seal landed. The player
	// is told why rather than being dropped into a match that has left the platform - which to them
	// looks like the game losing them.
	if (bSealed && !NewPlayer->IsLocalPlayerController())
	{
		Super::PostLogin(NewPlayer);
		if (GameSession)
		{
			GameSession->KickPlayer(NewPlayer, CampusBRRoster::SealRefusal());
		}
		return;
	}
	ACampusPlayerState* PlayerState = NewPlayer->GetPlayerState<ACampusPlayerState>();
	FCampusBRCombatant& Slot = Combatants.AddDefaulted_GetRef();
	Slot.bHuman = true;
	Slot.bPhysical = true;
	Slot.Controller = NewPlayer;
	// THE SESSION DECIDES THE SQUAD, not the order phones dialled in. Join order would seat two
	// strangers in one squad in every duo and squad match.
	Slot.Squad = SquadForKey(PlayerState ? PlayerState->SquadKey : FString());
	Slot.Name = NewPlayer->PlayerState ? NewPlayer->PlayerState->GetPlayerName() : TEXT("Player");

	if (PlayerState)
	{
		// Team is left at its default and NEVER read in BR: there are no two colours here. Squad is
		// the identity, and it is replicated so a client can mark its own squadmates and nobody else.
		PlayerState->Squad = Slot.Squad;
		PlayerState->Placement = 0;
		PlayerState->bIsHost = NewPlayer->IsLocalPlayerController();
		// THE HOST'S OWN ACCOUNT, exactly as ACampusMatchGameMode::AdmitWithTicket does for a room:
		// it is this phone's signed-in id, so there is nothing to verify. Without it ReportResult
		// reports every combatant - the player included - as a bot, and the backend awards nobody.
		// A REMOTE joiner still has no account here: a battle session travels with the session code
		// and no per-phone ticket, so there is nothing to admit it with yet.
		if (NewPlayer->IsLocalPlayerController())
		{
			if (const UCampusBackend* Backend = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr)
			{
				PlayerState->AccountId = Backend->PlayerId;
			}
		}
	}

	Super::PostLogin(NewPlayer);

	if (State)
	{
		const int32 Humans = HumansPresent();
		State->HumansJoined = Humans;
		// A FULL HOUSE IS NOT A CLOCK RUNNING OUT, it is a fact: nobody else CAN join a host that is at
		// its connection cap, so there is nothing left to wait for. It goes through the same door the
		// backend uses, and SealSession is idempotent if the server's own seal is already on its way.
		if (Humans >= MaxHumans)
		{
			SealSession(Humans, 0, CountdownSeconds);
		}
	}
	RefreshCounts();

	// SOLO TESTING: the host arriving is enough. Bots take the other 49 slots and the countdown starts.
	if (bInstantBotFill && !bSealed && NewPlayer->IsLocalPlayerController())
	{
		FTimerHandle InstantFill;
		GetWorldTimerManager().SetTimer(InstantFill, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			const int32 Present = HumansPresent();
			SealSession(Present, FMath::Max(0, MaxCombatants - Present), CountdownSeconds);
		}), FMath::Max(0.1f, InstantFillDelaySeconds), false);
	}
}

int32 ACampusBRGameMode::HumansPresent() const
{
	int32 Count = 0;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		Count += Each.bHuman && Each.Controller.IsValid() ? 1 : 0;
	}
	return Count;
}

void ACampusBRGameMode::SealSession(int32 Humans, int32 Bots, float InCountdownSeconds)
{
	const ACampusBRGameState* State = GetBRState();
	// The seal arrives twice by design (the socket AND the heartbeat response, and the lobby may have
	// held one from before this level): the second one is a confirmation, not a second match.
	if (!State || bSealed || State->BRPhase != ECampusBRPhase::Waiting)
	{
		return;
	}
	bSealed = true;
	GetWorldTimerManager().ClearTimer(SealSafetyTimer);

	// THE ROSTER IS WHO IS PRESENT PLUS BOTS. The server's human count is what it PLACED; a placed
	// player who never arrived must not hold a slot that nothing will ever fill, and the bots the
	// server asked for fill the rest of the fifty.
	const int32 Present = HumansPresent();
	const int32 Ceiling = FMath::Max(MaxCombatants, Present);
	MaxCombatants = Bots > 0 ? FMath::Clamp(Present + Bots, Present, Ceiling) : Ceiling;
	if (InCountdownSeconds > 0.f)
	{
		CountdownSeconds = FMath::Clamp(InCountdownSeconds, 1.f, 60.f);
	}
	// Close the door at the engine's own level too, so a late dial is refused by the session rather
	// than queued behind a match that has already started.
	if (GameSession)
	{
		GameSession->MaxPlayers = FMath::Max(1, Present);
	}
	UE_LOG(LogTemp, Log, TEXT("[CampusBR] sealed: %d present (%d placed), %d bots, %.0f s countdown"),
		Present, Humans, MaxCombatants - Present, CountdownSeconds);
	EnterPhase(ECampusBRPhase::Countdown);
}

void ACampusBRGameMode::SealLocally()
{
	if (bSealed)
	{
		return;
	}
	// The one case this exists for: a seal that was sent and never arrived. Sealing with what is here
	// beats stranding a player on a platform, so the solo tester always reaches the plane.
	UE_LOG(LogTemp, Warning, TEXT("[CampusBR] no seal after %.0f s - sealing locally"),
		WaitingSeconds + SealGraceSeconds);
	SealSession(HumansPresent(), 0, CountdownSeconds);
}

void ACampusBRGameMode::Logout(AController* Exiting)
{
	const int32 Index = IndexOf(Exiting);
	if (Index != INDEX_NONE && Combatants[Index].bAlive)
	{
		Eliminate(Index, TEXT("left"));
	}
	Super::Logout(Exiting);
}

int32 ACampusBRGameMode::SeatsTaken(int32 Squad) const
{
	int32 Taken = 0;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		Taken += Each.Squad == Squad ? 1 : 0;
	}
	return Taken;
}

int32 ACampusBRGameMode::SquadForKey(const FString& Key)
{
	// SOLO IS ALWAYS A TEAM OF ONE. Not "a squad with one seat taken" - a fresh number per player, so
	// no default and no missing team id can ever put two solo players on the same side.
	if (SquadSize <= 1 || Key.IsEmpty())
	{
		return NextSquadNumber++;
	}
	if (const int32* Found = SquadByKey.Find(Key))
	{
		// Only while that squad still has a seat: a team id carrying more phones than this mode seats
		// is a server fault, and splitting it beats a squad of five.
		if (SeatsTaken(*Found) < SquadSize)
		{
			return *Found;
		}
	}
	const int32 Fresh = NextSquadNumber++;
	SquadByKey.Add(Key, Fresh);
	return Fresh;
}

int32 ACampusBRGameMode::IndexOf(const AController* Controller) const
{
	for (int32 Index = 0; Index < Combatants.Num(); ++Index)
	{
		if (Combatants[Index].Controller.Get() == Controller)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 ACampusBRGameMode::SquadOf(const AController* Controller) const
{
	if (!Controller)
	{
		return 0;
	}
	// The PLAYER STATE first, and not as a fallback: this mode writes it the moment a combatant is
	// seated and a squad never changes afterwards, so it is the same number the roster holds - in
	// constant time instead of a scan of fifty, which matters because every bot asks this about every
	// candidate it can see, four times a second.
	if (const int32 Squad = CampusBR::GetSquadId(Controller->PlayerState); Squad != 0)
	{
		return Squad;
	}
	const int32 Index = IndexOf(Controller);
	return Index == INDEX_NONE ? 0 : Combatants[Index].Squad;
}

bool ACampusBRGameMode::AreFriendly(const AController* First, const AController* Second) const
{
	const ACampusBRGameState* State = GetBRState();
	// Before the drop nobody can hurt anybody: the waiting platform is a lobby, not a map.
	if (State && State->BRPhase != ECampusBRPhase::Playing && State->BRPhase != ECampusBRPhase::Flight)
	{
		return true;
	}
	if (!First || !Second || First == Second)
	{
		return true;
	}
	const int32 Mine = SquadOf(First);
	const int32 Theirs = SquadOf(Second);
	return Mine != 0 && Mine == Theirs;
}

void ACampusBRGameMode::RestartPlayer(AController* NewPlayer)
{
	const ACampusBRGameState* State = GetBRState();
	if (!NewPlayer || NewPlayer->IsPendingKillPending() || !State)
	{
		return;
	}
	// BR has no respawn. Before the drop everyone stands on the platform; after it, a dead player
	// spectates (see Eliminate) and never gets a pawn back.
	if (State->BRPhase != ECampusBRPhase::Waiting && State->BRPhase != ECampusBRPhase::Countdown)
	{
		return;
	}
	// A RANDOM FREE SPOT, not the Nth slot. Players arrive while the session is still filling and leave
	// again, so an index-per-player stacked two of them on the same square metre the moment anybody
	// dropped out - and fifty in a lattice looked like a parade, not a lobby.
	const FTransform Where = WaitingArea ? WaitingArea->FreeSlot()
		: FTransform(ACampusBRWaitingArea::Place(GetWorld()) + FVector(0.f, 0.f, 200.f));
	RestartPlayerAtTransform(NewPlayer, Where);
}

void ACampusBRGameMode::RequestStart(APlayerController* Requester)
{
	const ACampusBRGameState* State = GetBRState();
	const ACampusPlayerState* PlayerState = Requester ? Requester->GetPlayerState<ACampusPlayerState>() : nullptr;
	if (State && PlayerState && PlayerState->bIsHost && State->BRPhase == ECampusBRPhase::Waiting)
	{
		// The host's own START is a local seal, through the one door everything else uses.
		SealSession(HumansPresent(), 0, CountdownSeconds);
	}
}

// ---------------------------------------------------------------------------------------------
// Filling the roster

void ACampusBRGameMode::FillWithBots()
{
	// Every remaining slot starts VIRTUAL. Physical pawns are only ever made by promotion, so the
	// host never pays for ten AI pawns during the countdown, when it is busiest.
	//
	// A BOT SQUAD IS ALL BOTS. The seal hands this mode a bot COUNT and nothing that names a bot as a
	// particular player's teammate filler, so no bot is ever seated on a human's squad: in duo and
	// squad every bot is an enemy, and in solo every bot is its own squad of one.
	const int32 Seats = FMath::Max(1, SquadSize);
	const FVector2D Middle = CampusBRMap::Center(GetWorld());
	int32 Next = 0;
	int32 BotSquad = 0;
	int32 Filled = 0;
	while (Combatants.Num() < MaxCombatants)
	{
		if (BotSquad == 0 || Filled >= Seats)
		{
			BotSquad = NextSquadNumber++;
			Filled = 0;
		}
		FCampusBRCombatant& Slot = Combatants.AddDefaulted_GetRef();
		Slot.Squad = BotSquad;
		++Filled;
		Slot.Name = FString::Printf(TEXT("%s%02d"),
			CampusBRRoster::BotNames[Next % int32(UE_ARRAY_COUNT(CampusBRRoster::BotNames))], Next + 1);
		Slot.Where = FVector(Middle.X, Middle.Y, 0.f);
		++Next;
	}
	RefreshCounts();
}

FVector ACampusBRGameMode::GroundedSpawn(const FVector& Where) const
{
	// Straight down from well above the tallest roof, against the world only - a bot must not be
	// stood on another pawn's head. Capsule half height is 83 cm; 95 leaves it a clean 12 cm drop.
	constexpr float StartAbove = 20000.f;
	constexpr float Reach = 60000.f;
	constexpr float FeetClearance = 95.f;
	const FVector Top(Where.X, Where.Y, StartAbove);
	FHitResult Hit;
	FCollisionQueryParams Params(TEXT("CampusBRPromote"), false);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, Top, Top - FVector(0.f, 0.f, Reach), Objects, Params))
	{
		return Hit.Location + FVector(0.f, 0.f, FeetClearance);
	}
	// Nothing under that map position at all (a hole, or streaming has not caught up): the old
	// behaviour, which at least spawns something that can be shot.
	return Where + FVector(0.f, 0.f, 120.f);
}

int32 ACampusBRGameMode::PhysicalBotsAlive() const
{
	int32 Count = 0;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		Count += !Each.bHuman && Each.bPhysical && Each.bAlive ? 1 : 0;
	}
	return Count;
}

bool ACampusBRGameMode::VirtualAllowed() const
{
	// No ghosts in the endgame: in the last two circles every combatant left must be real or dead.
	return !ZoneActor || ZoneActor->GetPhase() < ZoneActor->GetPhaseCount() - 2;
}

bool ACampusBRGameMode::Promote(int32 Index)
{
	if (!Combatants.IsValidIndex(Index) || Combatants[Index].bPhysical || !Combatants[Index].bAlive)
	{
		return false;
	}
	FCampusBRCombatant& Slot = Combatants[Index];
	const TArray<int32> Squads = { Slot.Squad };
	// TickVirtual drifts these rows in two dimensions and writes Z = 0, so the raw value is a
	// point at sea level: on this campus that is either inside the terrain (the bot spawns buried
	// and stands there for the rest of the match) or well above it (it spawns falling, and a bot
	// has no drop to suppress its fall damage). Put it on the ground instead.
	const FVector Ground = GroundedSpawn(Slot.Where);
	const TArray<FVector> Where = { Ground };
	TArray<AController*> Made;

	if (UObject* Factory = CampusBRRoster::BotFactory.Get())
	{
		if (ICampusBRBotFactory* Bots = Cast<ICampusBRBotFactory>(Factory))
		{
			Bots->SpawnBots(1, Squads, Where, Made);
		}
	}
	if (Made.Num() == 0)
	{
		// No bot module in this build yet: a plain ACampusCharacter under an AIController found by
		// name. It stands and can be shot, which is still better than a ghost in the endgame.
		UClass* ControllerClass = FindObject<UClass>(nullptr, TEXT("/Script/CollegeCampusGame.CampusBotController"));
		if (!ControllerClass)
		{
			ControllerClass = AAIController::StaticClass();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ACampusCharacter* Pawn = GetWorld()->SpawnActor<ACampusCharacter>(ACampusCharacter::StaticClass(),
			FTransform(Ground), Params);
		AController* Bot = Pawn ? GetWorld()->SpawnActor<AController>(ControllerClass, FTransform::Identity, Params) : nullptr;
		if (!Pawn || !Bot)
		{
			if (Pawn)
			{
				Pawn->Destroy();
			}
			return false;
		}
		// A bot needs a player state so the scoreboard, the kill feed and the squad HUD see it at all.
		// ONLY IF IT HAS NONE: ACampusBotController asks for one itself (bWantsPlayerState, spawned in
		// AAIController::PostInitializeComponents), and a second InitPlayerState leaves the first one
		// registered on the game state for ever - a nameless, pawnless row replicated to every phone,
		// ten of them per match.
		if (!Bot->PlayerState)
		{
			Bot->InitPlayerState();
		}
		if (ACampusPlayerState* BotState = Bot->GetPlayerState<ACampusPlayerState>())
		{
			BotState->SetPlayerName(Slot.Name);
		}
		Bot->Possess(Pawn);
		// 50 actors on a phone: a bot nobody can see must not cost bandwidth or a tick.
		Pawn->SetNetUpdateFrequency(10.f);
		Pawn->SetMinNetUpdateFrequency(2.f);
		Pawn->SetNetCullDistanceSquared(4.0e8f); // 200 m, per the design's net budget table
		Made.Add(Bot);
	}

	Slot.bPhysical = true;
	Slot.Controller = Made[0];
	// A bot that appears in the first minute after the plane (promotion only runs while Playing, so
	// MatchStartTime is set) has not had time to find anything yet: it starts empty-handed and has to
	// pick a gun up off the ground like everybody else (ACampusBotController::TickLoot). Later ones
	// arrive armed - the virtual bot they were "looted" off-screen.
	if (ACampusCharacter* BotPawn = Made[0] ? Cast<ACampusCharacter>(Made[0]->GetPawn()) : nullptr)
	{
		if (MatchStartTime > 0.f && GetWorld()->GetTimeSeconds() - MatchStartTime < BotsLootWindowSeconds)
		{
			BotPawn->StripWeapons();
		}
	}
	// THE SQUAD, on the bot's own player state: the bot's target picker, the client's squad markers
	// and the scoreboard all read it from there, and a bot left on squad 0 would be friendly with
	// nobody AND marked as nobody's teammate - which is exactly right, but only by accident.
	if (ACampusPlayerState* BotState = Made[0] ? Made[0]->GetPlayerState<ACampusPlayerState>() : nullptr)
	{
		BotState->Squad = Slot.Squad;
		BotState->Placement = 0;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Flight and play

void ACampusBRGameMode::StartFlight()
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	State->Phase = ECampusPhase::Playing; // the HUD and the touch controls key off this
	// The waiting slab has done its job. It is 40 m up, 40 m square and BlockAll, so leaving it in the
	// world leaves a mid-air floor a diver can auto-chute onto and be stranded on until the zone burns.
	if (WaitingArea)
	{
		WaitingArea->Destroy();
		WaitingArea = nullptr;
	}
	// GROUND LOOT, laid out as the plane takes off and not before: it is a battle-royale thing (so the
	// level's spawners stay dormant in a Team Clash room on the same map) and nothing can be reached
	// from the waiting slab, while every box bobs and replicates for as long as it exists.
	ACampusLootSpawner::FillAll(GetWorld());
	Plane = GetWorld()->SpawnActor<ACampusBRPlane>(ACampusBRPlane::StaticClass(), FTransform::Identity);
	if (!Plane)
	{
		EnterPhase(ECampusBRPhase::Playing);
		return;
	}
	Plane->Launch();
	// Only humans ride. A promoted bot appears on the ground where its virtual row had drifted to,
	// which costs nothing and nobody can tell the difference from below.
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (!Each.bHuman)
		{
			continue;
		}
		if (ACampusCharacter* Pawn = Each.Controller.IsValid() ? Cast<ACampusCharacter>(Each.Controller->GetPawn()) : nullptr)
		{
			Plane->Board(Pawn);
		}
	}
	// The circle opens as the plane leaves the map, which is the design's 0:45 mark.
	GetWorldTimerManager().SetTimer(PhaseTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		EnterPhase(ECampusBRPhase::Playing);
	}), 45.f, false);
}

void ACampusBRGameMode::BeginPlaying()
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	State->Phase = ECampusPhase::Playing;
	MatchStartTime = float(GetWorld()->GetTimeSeconds());
	if (ZoneActor)
	{
		ZoneActor->Begin();
	}
	State->MulticastFeed(TEXT("The play area is closing in"));
}

void ACampusBRGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ACampusBRGameState* State = GetBRState();
	if (!State || State->BRPhase != ECampusBRPhase::Playing)
	{
		return;
	}
	const float Now = float(GetWorld()->GetTimeSeconds());
	if (Now < NextVirtualTime)
	{
		return;
	}
	NextVirtualTime = Now + CampusBRRoster::VirtualInterval;
	TickVirtual();
}

int32 ACampusBRGameMode::TargetAlive(float SecondsIntoMatch) const
{
	const auto& Curve = CampusBRRoster::Curve;
	const int32 Count = int32(UE_ARRAY_COUNT(CampusBRRoster::Curve));
	if (SecondsIntoMatch <= Curve[0].Seconds)
	{
		return MaxCombatants;
	}
	for (int32 Index = 1; Index < Count; ++Index)
	{
		if (SecondsIntoMatch <= Curve[Index].Seconds)
		{
			const float Alpha = (SecondsIntoMatch - Curve[Index - 1].Seconds)
				/ FMath::Max(Curve[Index].Seconds - Curve[Index - 1].Seconds, 1.f);
			const float Alive = FMath::Lerp(Curve[Index - 1].Alive, Curve[Index].Alive, Alpha);
			// The curve is written for fifty; a smaller match scales with it.
			return FMath::CeilToInt(Alive * MaxCombatants / 50.f);
		}
	}
	return 1;
}

void ACampusBRGameMode::TickVirtual()
{
	const FVector2D Middle = ZoneActor ? ZoneActor->GetCenter() : CampusBRMap::Center(GetWorld());
	const float Drift = CampusBRRoster::DriftSpeed * CampusBRRoster::VirtualInterval;
	const bool bGhostsAllowed = VirtualAllowed();

	// Gather the humans once: the promotion test is the only per-combatant distance check we do.
	TArray<FVector> Humans;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bHuman && Each.bAlive && Each.Controller.IsValid())
		{
			if (const APawn* Pawn = Each.Controller->GetPawn())
			{
				Humans.Add(Pawn->GetActorLocation());
			}
		}
	}

	for (int32 Index = 0; Index < Combatants.Num(); ++Index)
	{
		FCampusBRCombatant& Slot = Combatants[Index];
		if (!Slot.bAlive || Slot.bPhysical)
		{
			continue;
		}
		const FVector2D Flat(Slot.Where.X, Slot.Where.Y);
		const FVector2D Toward = (Middle - Flat).GetSafeNormal();
		Slot.Where = FVector(Flat + Toward * Drift, 0.f);

		bool bNear = !bGhostsAllowed; // endgame: everyone left must become real (or die below)
		for (const FVector& Human : Humans)
		{
			bNear |= FVector2D::DistSquared(FVector2D(Human.X, Human.Y), Flat)
				< FMath::Square(CampusBRRoster::PromotionRadius);
		}
		if (bNear && PhysicalBotsAlive() < MaxPhysicalBots)
		{
			Promote(Index);
		}
	}

	// Attrition: bring the ALIVE counter down the curve the players expect. Only virtual rows are
	// ever taken this way - a real pawn has to actually be killed.
	const int32 Target = TargetAlive(float(GetWorld()->GetTimeSeconds()) - MatchStartTime);
	const ACampusBRGameState* State = GetBRState();
	if (State && State->AliveCombatants > Target)
	{
		for (int32 Index = 0; Index < Combatants.Num(); ++Index)
		{
			if (Combatants[Index].bAlive && !Combatants[Index].bPhysical)
			{
				KillVirtual(Index);
				break; // one a tick: deaths should trickle into the feed, not arrive in a block
			}
		}
	}
}

void ACampusBRGameMode::KillVirtual(int32 Index)
{
	if (!Combatants.IsValidIndex(Index))
	{
		return;
	}
	// A plausible killer, so the feed reads like a world that is alive. The fallback is the ZONE only
	// when the zone could have done it (it is burning and this ghost is standing outside the circle);
	// otherwise nobody is named, because naming the zone for a death it did not cause is the same lie
	// the real elimination path used to tell.
	FString Killer;
	if (ZoneActor && ZoneActor->IsHot() && ZoneActor->MetresOutside(Combatants[Index].Where) > 0.f)
	{
		Killer = TEXT("the play zone");
	}
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive && Each.Squad != Combatants[Index].Squad && !Each.bHuman)
		{
			Killer = Each.Name;
			break;
		}
	}
	Eliminate(Index, Killer);
}

// ---------------------------------------------------------------------------------------------
// Dying and winning

void ACampusBRGameMode::OnKilled(AController* Victim, AController* Killer)
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	// A fall off the waiting platform is not an elimination: put the player back on the slab.
	if (State->BRPhase == ECampusBRPhase::Waiting || State->BRPhase == ECampusBRPhase::Countdown)
	{
		if (Victim)
		{
			if (APawn* Pawn = Victim->GetPawn())
			{
				Pawn->Destroy();
			}
			RestartPlayer(Victim);
		}
		return;
	}
	if (ACampusPlayerState* VictimState = Victim ? Victim->GetPlayerState<ACampusPlayerState>() : nullptr)
	{
		++VictimState->Deaths;
	}
	ACampusPlayerState* KillerState = Killer ? Killer->GetPlayerState<ACampusPlayerState>() : nullptr;
	// KILL CREDIT KEYS OFF THE SQUAD. Friendly fire is already off upstream, so this is a belt on the
	// braces - but a credited squadmate kill is exactly the kind of thing that becomes an exploit.
	// A KILLER-LESS DEATH CREDITS NOBODY, which is the whole of the zone rule: ACampusBRZone passes a
	// null instigator, so KillerState is null and no counter moves.
	if (KillerState && Killer != Victim && !AreFriendly(Killer, Victim))
	{
		++KillerState->Kills;
	}

	// WHO TO NAME IN THE FEED. It used to be "the zone" for every death with no killer, which made the
	// zone the author of every fall off a 40 m slab, every 100 m drop with no chute and every trip
	// below the kill plane. Ask the circle instead: it is only the zone if the zone could have done it.
	FString Cause;
	if (KillerState)
	{
		Cause = KillerState->GetPlayerName();
	}
	else if (const APawn* Body = Victim ? Victim->GetPawn() : nullptr)
	{
		if (ZoneActor && ZoneActor->IsHot() && ZoneActor->MetresOutside(Body->GetActorLocation()) > 0.f)
		{
			Cause = TEXT("the play zone");
		}
	}

	const int32 Index = IndexOf(Victim);
	if (Index != INDEX_NONE)
	{
		Eliminate(Index, Cause);
		return;
	}
	// NOT ON THE ROSTER and still holding a pawn. This used to fall straight out of the bottom of the
	// function: the pawn kept standing in the world at zero health, un-killable (ApplyHit returns on
	// bDead), blocking shots and a doorway, while ALIVE never came down and the last-squad-standing
	// test could never fire. Whoever it is, it is dead - take the pawn.
	if (APawn* Orphan = Victim ? Victim->GetPawn() : nullptr)
	{
		Orphan->Destroy();
	}
}

void ACampusBRGameMode::Eliminate(int32 Index, const FString& KillerName)
{
	ACampusBRGameState* State = GetBRState();
	if (!Combatants.IsValidIndex(Index) || !Combatants[Index].bAlive || !State)
	{
		return;
	}
	FCampusBRCombatant& Slot = Combatants[Index];
	Slot.bAlive = false;
	Slot.Placement = FMath::Max(1, State->AliveCombatants); // the place they finished in
	// An EMPTY killer name means "nobody did this": a fall, the kill plane, a vehicle wreck. The feed
	// has to say so rather than hand the body to whichever name was convenient.
	State->MulticastFeed(KillerName.IsEmpty()
		? FString::Printf(TEXT("%s  was eliminated"), *Slot.Name)
		: FString::Printf(TEXT("%s  >  %s"), *KillerName, *Slot.Name));

	AController* Controller = Slot.Controller.Get();
	// The placement goes on the player state so the BR result screen can show "#7" without a second
	// channel. Non-zero also IS "eliminated" to the client's squad list, which has no other way to
	// tell a dead squadmate from one whose pawn is simply not relevant to it yet.
	if (ACampusPlayerState* DeadState = Controller ? Controller->GetPlayerState<ACampusPlayerState>() : nullptr)
	{
		DeadState->Placement = Slot.Placement;
		// How long they lasted, for the result screen's SURVIVAL TIME. Measured from the whistle, not
		// from the drop: everyone in the match shares that start, so two squadmates can compare.
		DeadState->SurvivalSeconds = FMath::Max(0.f, float(GetWorld()->GetTimeSeconds()) - MatchStartTime);
	}
	if (Controller)
	{
		if (APawn* Pawn = Controller->GetPawn())
		{
			// Dead squadmates who were watching this pawn move on to the next one still up, before the
			// pawn goes: a destroyed view target leaves their camera frozen on an empty spot.
			for (const FCampusBRCombatant& Each : Combatants)
			{
				APlayerController* Watcher = Cast<APlayerController>(Each.Controller.Get());
				if (!Each.bAlive && Each.Squad == Slot.Squad && Watcher && Watcher != Controller
					&& Watcher->GetViewTarget() == Pawn)
				{
					if (APawn* Next = LivingSquadmatePawn(Slot.Squad, Controller))
					{
						Watcher->SetViewTargetWithBlend(Next, 1.f);
					}
				}
			}
			Pawn->Destroy();
		}
		// Spectate a squadmate who is still up; with nobody left the win screen takes over and the
		// existing "Leave" button walks back to the lobby.
		if (APlayerController* Player = Cast<APlayerController>(Controller))
		{
			if (APawn* Mate = LivingSquadmatePawn(Slot.Squad, Controller))
			{
				Player->SetViewTargetWithBlend(Mate, 1.f);
			}
		}
	}
	RefreshCounts();
	// THE SQUAD'S OWN PLACE, written only when this elimination was the squad's LAST one. Counted in
	// teams, not in bodies: with AliveSquads teams still standing after the wipe, this squad came in
	// one behind them. Every member gets it, dead or disconnected, because "#3 / 13" has to read the
	// same on all four phones.
	if (!HasLivingMember(Slot.Squad))
	{
		const int32 SquadPlace = FMath::Max(1, State->AliveSquads + 1);
		for (const FCampusBRCombatant& Each : Combatants)
		{
			if (Each.Squad != Slot.Squad)
			{
				continue;
			}
			if (ACampusPlayerState* MateState = Each.Controller.IsValid()
				? Each.Controller->GetPlayerState<ACampusPlayerState>() : nullptr)
			{
				MateState->SquadPlacement = SquadPlace;
			}
		}
	}

	if (State->AliveSquads <= 1)
	{
		EndMatch();
	}
}

bool ACampusBRGameMode::HasLivingMember(int32 Squad) const
{
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive && Each.Squad == Squad)
		{
			return true;
		}
	}
	return false;
}

void ACampusBRGameMode::SpectateNextMate(APlayerController* Player)
{
	const int32 Index = IndexOf(Player);
	if (!Combatants.IsValidIndex(Index) || Combatants[Index].bAlive)
	{
		return;
	}
	const int32 Squad = Combatants[Index].Squad;
	TArray<APawn*> Mates;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive && Each.Squad == Squad && Each.Controller.IsValid())
		{
			if (APawn* Pawn = Each.Controller->GetPawn())
			{
				Mates.Add(Pawn);
			}
		}
	}
	if (Mates.Num() > 0)
	{
		// Not found (-1) lands on the first mate; otherwise the one after the current view.
		const int32 Current = Mates.IndexOfByKey(Player->GetViewTarget());
		Player->SetViewTargetWithBlend(Mates[(Current + 1) % Mates.Num()], 0.5f);
	}
}

APawn* ACampusBRGameMode::LivingSquadmatePawn(int32 Squad, const AController* Except) const
{
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive && Each.Squad == Squad && Each.Controller.IsValid() && Each.Controller.Get() != Except)
		{
			if (APawn* Pawn = Each.Controller->GetPawn())
			{
				return Pawn;
			}
		}
	}
	return nullptr;
}

void ACampusBRGameMode::RefreshCounts()
{
	ACampusBRGameState* State = GetBRState();
	if (!State)
	{
		return;
	}
	TSet<int32> Squads;
	int32 Alive = 0;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive)
		{
			++Alive;
			Squads.Add(Each.Squad);
		}
	}
	State->AliveCombatants = Alive;
	// Same reasoning as TotalCombatants below: while the platform is still FILLING, the roster holds
	// only the humans who have walked in, so the squad count is the SESSION's shape. Once the roster
	// is sealed every combatant is on it and the count is the plain truth.
	State->AliveSquads = bSealed ? Squads.Num()
		: FMath::Max(Squads.Num(), FMath::Max(Combatants.Num(), MaxCombatants) / FMath::Max(1, SquadSize));
	// THE SESSION'S SIZE, not the roster's length. While the platform is filling the roster is just the
	// humans who have walked in, and a HUD reading "PLAYERS 1 / 1" off it told the first player to
	// arrive that they were the whole match. The bots take the rest of the slots at the seal.
	State->TotalCombatants = FMath::Max(Combatants.Num(), MaxCombatants);
	// The denominator of "#3 / N" is fixed for the whole match, so it is only ever raised: counting
	// DISTINCT squads on a sealed roster, and never dropping below the shape the session advertised.
	if (bSealed)
	{
		TSet<int32> AllSquads;
		for (const FCampusBRCombatant& Each : Combatants)
		{
			AllSquads.Add(Each.Squad);
		}
		State->TotalSquads = FMath::Max(State->TotalSquads, AllSquads.Num());
	}
	State->TotalSquads = FMath::Max3(State->TotalSquads, State->AliveSquads,
		State->TotalCombatants / FMath::Max(1, SquadSize));
}

void ACampusBRGameMode::EndMatch()
{
	ACampusBRGameState* State = GetBRState();
	if (!State || State->BRPhase == ECampusBRPhase::Ended)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PhaseTimer);
	if (ZoneActor)
	{
		ZoneActor->Freeze();
	}
	// Through EnterPhase rather than straight onto the field: it does exactly this plus clear the phase
	// timer, and it is where the host's heartbeat is stopped (the result below ends the session).
	EnterPhase(ECampusBRPhase::Ended);
	State->Phase = ECampusPhase::Ended;

	// The whole match in whole seconds, for MATCH DURATION on the result screen. Measured once, here,
	// because the host's world clock is the only one every phone in the session agrees about.
	const float Ran = FMath::Max(0.f, float(GetWorld()->GetTimeSeconds()) - MatchStartTime);
	State->MatchSeconds = FMath::RoundToInt(Ran);

	int32 WinningSquad = -1;
	for (FCampusBRCombatant& Each : Combatants)
	{
		if (Each.bAlive)
		{
			WinningSquad = Each.Squad;
			Each.Placement = 1;
			if (ACampusPlayerState* Won = Each.Controller.IsValid()
				? Each.Controller->GetPlayerState<ACampusPlayerState>() : nullptr)
			{
				Won->Placement = 1;
				Won->SquadPlacement = 1;
				// They never died, so nothing wrote their survival time: they lasted the whole match.
				Won->SurvivalSeconds = Ran;
			}
		}
	}
	State->WinningSquad = WinningSquad;
	// Winner is a RED/BLUE field and means nothing here; it is set only because the shared match
	// widget uses "Winner >= 0" as "the match is over". The BR result screen reads WinningSquad and
	// the player's own Placement, never this.
	State->Winner = 0;
	State->MulticastFeed(TEXT("BOOYAH!"));
	ReportResult();
}

void ACampusBRGameMode::ReportResult()
{
	const ACampusBRGameState* State = GetBRState();
	UCampusBackend* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
	if (!State || !Backend || State->RoomCode.IsEmpty())
	{
		return;
	}
	// Contract: { placements: [ { player_id | bot, team, kills, bot_kills, placement, damage } ], duration_s }
	// `damage` (backend migration 035) pays a little rank point in Ranked BR; older servers ignore it.
	FString Rows;
	for (const FCampusBRCombatant& Each : Combatants)
	{
		const ACampusPlayerState* PlayerState = Each.Controller.IsValid()
			? Each.Controller->GetPlayerState<ACampusPlayerState>() : nullptr;
		// A human with no admitted account (editor play, LAN) is reported as a bot: the backend keys
		// placements on the uuid, and inventing one would corrupt somebody else's record.
		const bool bAccount = Each.bHuman && PlayerState && !PlayerState->AccountId.IsEmpty();
		const FString Who = bAccount
			? FString::Printf(TEXT("\"player_id\":\"%s\""), *PlayerState->AccountId)
			: FString(TEXT("\"bot\":true"));
		Rows += FString::Printf(TEXT("%s{%s,\"team\":%d,\"kills\":%d,\"bot_kills\":%d,\"placement\":%d,\"damage\":%d}"),
			Rows.IsEmpty() ? TEXT("") : TEXT(","), *Who, Each.Squad,
			PlayerState ? PlayerState->Kills : 0, 0, Each.Placement,
			PlayerState ? FMath::Clamp(FMath::RoundToInt(float(PlayerState->DamageDealt)), 0, 1000000) : 0);
	}
	const int32 Duration = FMath::Max(0, FMath::RoundToInt(float(GetWorld()->GetTimeSeconds()) - MatchStartTime));
	const FString Body = FString::Printf(TEXT("{\"placements\":[%s],\"duration_s\":%d}"), *Rows, Duration);

	// Fire and forget. The endpoint is still being built; a 404 must never hold up the win screen.
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http = FHttpModule::Get().CreateRequest();
	Http->SetURL(FString::Printf(TEXT("%s/br/%s/result"), *GetDefault<UCampusNetSettings>()->ApiBaseUrl(), *State->RoomCode));
	Http->SetVerb(TEXT("POST"));
	Http->SetTimeout(10.f);
	Http->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Http->SetHeader(TEXT("ngrok-skip-browser-warning"), TEXT("1"));
	// One result per room, whatever the network does on the way: the room code IS the natural key.
	Http->SetHeader(TEXT("Idempotency-Key"), FString::Printf(TEXT("br-result-%s"), *State->RoomCode));
	if (!Backend->GetAccessToken().IsEmpty())
	{
		Http->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Backend->GetAccessToken());
	}
	Http->SetContentAsString(Body);
	Http->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		UE_LOG(LogTemp, Log, TEXT("[CampusBR] result POST -> %d"), bConnected && Response ? Response->GetResponseCode() : 0);
	});
	Http->ProcessRequest();
}
