#include "Campus/CampusBR.h"

#include "Campus/CampusBackend.h"
#include "Campus/CampusEOS.h"
#include "Campus/CampusMapArt.h"
#include "Campus/CampusNetSettings.h"
#include "Campus/CampusSocial.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "CampusBR"

namespace
{
	// Unity builds put several .cpp files in one translation unit, so every local helper here is
	// prefixed - a bare Str()/Num() would collide with the ones in CampusSocial.cpp.
	FString BrStr(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Out;
		return Json.IsValid() && Json->TryGetStringField(Field, Out) ? Out : FString();
	}

	double BrNum(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		double Out = 0.0;
		return Json.IsValid() && Json->TryGetNumberField(Field, Out) ? Out : 0.0;
	}

	TSharedPtr<FJsonObject> BrObj(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Out = nullptr;
		return Json.IsValid() && Json->TryGetObjectField(Field, Out) && Out ? *Out : nullptr;
	}

	/** The error code out of {error:{code, message, details}}, or empty. */
	FString BrErrorCode(const TSharedPtr<FJsonObject>& Json)
	{
		return BrStr(BrObj(Json, TEXT("error")), TEXT("code"));
	}

	/** The queue is per mode x REGION. One place to change for SEA, and an unconfigured region is a
	 *  409 BS_REGION_UNKNOWN rather than a queue nobody's matchmaker is watching. */
	FString BrRegion()
	{
		const FString Configured = GetDefault<UCampusNetSettings>()->MatchRegion.TrimStartAndEnd().ToUpper();
		return Configured.IsEmpty() ? FString(TEXT("IN")) : Configured;
	}

	/** The fallback poll interval. Only ever used while the social socket is down - see CampusBR.h. */
	constexpr float PollSeconds = 5.f;

	/** The beat, per the backend contract: every 5 s while the session is still FILLING (the server
	 *  deletes a session nobody has beaten within 10 s as empty), every 20 s once it is sealed. */
	constexpr float FillingBeatSeconds = 5.f;
	constexpr float BeatSeconds = 20.f;

	/** How long the beat may run with no game mode to bound it: the travel and the level load. A travel
	 *  that never lands stops beating instead of holding a session open for a player who is not coming. */
	constexpr float LobbyGraceSeconds = 120.f;

	/** A cold read of /br/queue/me is cheap but the tunnel is metered: once a minute at most. */
	constexpr double ColdCheckSeconds = 60.0;

	/** THE ONLY TWO STATES A HOST MAY ANNOUNCE. "COUNTDOWN" is REJECTED with a 400 - the seal is the
	 *  server's decision, not the host's - and the session is already FILLING when the beat starts, so
	 *  both of those beat BARE ("I am still here", which is the whole job of most beats). */
	bool BrAnnounceable(ECampusBRBeat Beat)
	{
		return Beat == ECampusBRBeat::InFlight || Beat == ECampusBRBeat::InProgress;
	}

	const TCHAR* BeatName(ECampusBRBeat Beat)
	{
		return Beat == ECampusBRBeat::InProgress ? TEXT("IN_PROGRESS") : TEXT("IN_FLIGHT");
	}

	/** The session states that mean SEALED on the wire (BATTLE_SESSION_DESIGN §3): the empty slots
	 *  became bots, or the countdown is running. Read alongside the explicit `sealed` flag. */
	bool BrIsSealedState(const FString& State)
	{
		return State.Equals(TEXT("BACKFILLED"), ESearchCase::IgnoreCase)
			|| State.Equals(TEXT("COUNTDOWN"), ESearchCase::IgnoreCase);
	}

	bool BrBool(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		bool Out = false;
		return Json.IsValid() && Json->TryGetBoolField(Field, Out) ? Out : false;
	}
}

namespace
{
	/** THE API'S OWN NAMES FOR THE MAPS, against the client catalogue's ids. The valley is "real" to the
	 *  backend and always has been (routes/rooms.js MAPS.real, migration 015's br_matches.map) while the
	 *  catalogue here calls it "VarsityValley", so one of the two has to translate and this is the file
	 *  that talks to the API. The backend's list is api/src/lib/battlesession/maps.js - the two are one
	 *  list in two languages and have to be changed together. */
	struct FBrMapName
	{
		const TCHAR* Api;
		const TCHAR* Catalog;
	};
	const FBrMapName BrMapNames[] = {
		{ TEXT("real"), TEXT("VarsityValley") },
		{ TEXT("campus"), TEXT("CampusMap") },
		{ TEXT("classic"), TEXT("Classic") },
	};
}

namespace CampusBR
{
	const TCHAR* ApiMapFor(FName CatalogId)
	{
		for (const FBrMapName& Each : BrMapNames)
		{
			if (CatalogId == FName(Each.Catalog))
			{
				return Each.Api;
			}
		}
		// A map the API has no name for (a "coming soon" plate). NOT a default: what to do about it is
		// the caller's to decide - a battle session falls back to the valley, a 4v4 room to the college.
		return nullptr;
	}

	FName CatalogMapFor(const FString& MapId)
	{
		// Already a catalogue id (the game state's own field, or a server that speaks our language).
		if (CampusCatalog::FindMap(FName(MapId)) != INDEX_NONE)
		{
			return FName(MapId);
		}
		for (const FBrMapName& Each : BrMapNames)
		{
			if (MapId.Equals(Each.Api, ESearchCase::IgnoreCase))
			{
				return FName(Each.Catalog);
			}
		}
		return NAME_None;
	}

	const TCHAR* LevelForMap(const FString& MapId)
	{
		// WHAT THE SERVER SENDS IS AN API NAME ("real", "campus"), and the game state replicates the
		// valley's as "RealMap"; the catalogue here calls the same levels "VarsityValley" and
		// "CampusMap". An empty id means the valley too - it is where a battle session runs unless the
		// server names another battle-royale map.
		//
		// So the valley is the DEFAULT and only a map that resolves to a catalogue row is spelled out: an
		// id nobody here recognised used to fall through to L_Match, which opened a fifty-player battle
		// session on the 4v4 room map. The level itself is CampusCatalog's to state (FMapEntry::Level) -
		// this function is only the BR default on top of it.
		const int32 Mode = CampusCatalog::FindMode(FName(TEXT("BattleRoyale")));
		const FName Catalogued = CatalogMapFor(MapId);
		// Only a map BATTLE ROYALE actually offers may be opened by this path; "Classic" stays allowed
		// because a room handle can still name it and the 4v4 level is a real level.
		const bool bAllowed = !Catalogued.IsNone()
			&& (Catalogued == FName(TEXT("Classic")) || (Mode != INDEX_NONE && CampusCatalog::ModeHasMap(Mode, Catalogued)));
		const TCHAR* Level = bAllowed ? CampusCatalog::LevelFor(Catalogued) : nullptr;
		return Level ? Level : TEXT("/Game/Maps/L_VarsityValley");
	}

	const TCHAR* ModeForTeamSize(int32 TeamSize)
	{
		if (TeamSize <= 1)
		{
			return TEXT("solo");
		}
		return TeamSize == 2 ? TEXT("duo") : TEXT("squad");
	}

	int32 TeamSizeForMode(const FString& Mode)
	{
		if (Mode.Equals(TEXT("solo"), ESearchCase::IgnoreCase))
		{
			return 1;
		}
		return Mode.Equals(TEXT("duo"), ESearchCase::IgnoreCase) ? 2 : 4;
	}

	FText TeamSizeLabel(int32 TeamSize)
	{
		if (TeamSize <= 1)
		{
			return LOCTEXT("BrSolo", "SOLO");
		}
		return TeamSize == 2 ? LOCTEXT("BrDuo", "DUO") : LOCTEXT("BrSquad", "SQUAD");
	}
}

void UCampusBRQueue::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The push side. UCampusSocial dedupes and orders the frames; all we do is read the objects. Both
	// `bs.placed` and the legacy `br.matched` land on OnBRPlaced with the SAME payload, so TakeMatch's
	// session-id dedupe is what makes sure a player is sent into a session exactly once.
	if (UCampusSocial* Social = Live())
	{
		PlacedHandle = Social->OnBRPlaced.AddLambda([this](const TSharedPtr<FJsonObject>& Payload)
		{
			// The session arrives bare, or under "battle_session" / "match" depending on the producer.
			TSharedPtr<FJsonObject> Inner = BrObj(Payload, TEXT("battle_session"));
			if (!Inner.IsValid())
			{
				Inner = BrObj(Payload, TEXT("match"));
			}
			TakeMatch(Inner.IsValid() ? Inner : Payload);
		});
		// THE SEAL'S FIRST DOOR. The second is the heartbeat response (UCampusBRHostBeat::TakeBeatState);
		// both end in TakeSeal, which acts on one session exactly once.
		SealedHandle = Social->OnBRSealed.AddLambda([this](const TSharedPtr<FJsonObject>& Payload)
		{
			TakeSeal(Payload);
		});
		LeftHandle = Social->OnBRLeft.AddLambda([this](const TSharedPtr<FJsonObject>& Payload)
		{
			TakeLeft(Payload);
		});
		EndedHandle = Social->OnBRSessionEnded.AddLambda([this](const TSharedPtr<FJsonObject>& Payload)
		{
			TakeEnded(Payload);
		});
		ResultHandle = Social->OnBRResult.AddLambda([this](const TSharedPtr<FJsonObject>& Payload)
		{
			TakeResult(Payload);
		});
	}
}

void UCampusBRQueue::Deinitialize()
{
	StopTimer();
	if (UCampusSocial* Social = Live())
	{
		Social->OnBRPlaced.Remove(PlacedHandle);
		Social->OnBRSealed.Remove(SealedHandle);
		Social->OnBRLeft.Remove(LeftHandle);
		Social->OnBRSessionEnded.Remove(EndedHandle);
		Social->OnBRResult.Remove(ResultHandle);
	}
	PlacedHandle.Reset();
	SealedHandle.Reset();
	LeftHandle.Reset();
	EndedHandle.Reset();
	ResultHandle.Reset();
	Super::Deinitialize();
}

UCampusBackend* UCampusBRQueue::Api() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
}

UCampusSocial* UCampusBRQueue::Live() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusSocial>() : nullptr;
}

bool UCampusBRQueue::AmHost() const
{
	const UCampusBackend* Backend = Api();
	// No host id at all (an older server, or an address-only match) = whoever is listed first hosts, and
	// that is not us: travelling to a listen server that nobody opened is the one unrecoverable case.
	//
	// A DEDICATED SESSION HAS NO LOCAL HOST. The backend still names a player as host_player_id there
	// (its result route authorises on it - docs/BATTLE_SESSION.md §3), so the connect type has the last
	// word: on AWS the game server is already running and every phone travels to its address.
	if (Match.IsDedicated())
	{
		return false;
	}
	return Backend && !Match.HostPlayerId.IsEmpty() && Match.HostPlayerId == Backend->PlayerId;
}

bool UCampusBRQueue::MustWaitForLeader() const
{
	const UCampusSocial* Social = Live();
	return Social && Social->HasParty() && !Social->IsPartyLeader();
}

int32 UCampusBRQueue::GetCountdown() const
{
	if (State != ECampusBRState::Matched || Match.CountdownSeconds <= 0)
	{
		return 0;
	}
	// The server's number, walked down by the local clock between reads: a countdown that jumps back
	// up is the one thing that makes a lobby feel broken.
	const double Since = FPlatformTime::Seconds() - CountdownReadAt;
	return FMath::Max(0, Match.CountdownSeconds - int32(Since));
}

void UCampusBRQueue::Join(int32 InTeamSize, bool bNightWanted, FName MapId, FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		LastError = TEXT("Sign in first.");
		if (Done)
		{
			Done(false, LastError);
		}
		return;
	}
	// ONE TICKET IS ONE TEAM. A member who somehow reaches START must not cut a second ticket: the
	// server would refuse it with NOT_PARTY_LEADER anyway, and spending the request to be told so is
	// worse than saying it here.
	if (MustWaitForLeader())
	{
		LastError = TEXT("Only the squad leader can start matchmaking.");
		if (Done)
		{
			Done(false, LastError);
		}
		return;
	}
	TeamSize = FMath::Clamp(InTeamSize, 1, 4);
	bNight = bNightWanted;
	// Resolved HERE and not at the request, so a retry and the overlay both read the one answer. A map
	// the API has no name for queues for the valley - a battle session is always somewhere.
	const TCHAR* ApiMap = CampusBR::ApiMapFor(MapId);
	WantedMap = ApiMap ? ApiMap : TEXT("real");
	Match = FCampusBRMatch();
	LastError.Reset();
	EtaSeconds = 0;
	Found = 0;
	Elapsed = 0.f;
	ShownSecond = -1;
	State = ECampusBRState::Joining;
	OnChanged.Broadcast();
	StartTimer();

	TWeakObjectPtr<UCampusBRQueue> WeakThis(this);
	auto Post = [WeakThis, Done](const FString& EosPuid)
	{
		UCampusBRQueue* Self = WeakThis.Get();
		UCampusBackend* Api = Self ? Self->Api() : nullptr;
		if (!Self || !Api)
		{
			return;
		}
		TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("mode"), CampusBR::ModeForTeamSize(Self->TeamSize));
		// One queue per mode x REGION x MAP. Both sent explicitly rather than left to the server's
		// defaults, so the phone and the matchmaker cannot disagree about which queue this team is
		// standing in - and two players who picked different maps are never put in one session.
		Body->SetStringField(TEXT("region"), BrRegion());
		Body->SetStringField(TEXT("map"), Self->WantedMap);
		// The matchmaker needs the puid UP FRONT: if it picks this phone as the host, the session object
		// it hands every other player has to carry the identity they will dial. Empty = this phone can
		// play but can never host (the phone allocator needs one reachable listen server).
		if (!EosPuid.IsEmpty())
		{
			Body->SetStringField(TEXT("eos_puid"), EosPuid);
		}
		// The server owns the lighting; this is only the player's preference riding along, and a
		// server that ignores the field simply sends day_night back and the HUD follows it.
		Body->SetStringField(TEXT("day_night"), Self->bNight ? TEXT("night") : TEXT("day"));

		Api->CallApi(TEXT("POST"), TEXT("/br/queue"), Body, true,
			[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
		{
			UCampusBRQueue* Me = WeakThis.Get();
			if (!Me)
			{
				return;
			}
			if (!bOk)
			{
				// 409 NOT_PARTY_LEADER / PARTY_TOO_BIG_FOR_MODE / PARTY_IN_SOLO_QUEUE /
				// BS_ALREADY_QUEUED / BS_REGION_UNKNOWN and 503 BR_UNAVAILABLE all land here as one
				// sentence the overlay prints and leaves.
				const FString Code = BrErrorCode(Json);
				Me->Reset();
				Me->LastError = Error;
				// BR_ALREADY_IN_MATCH means the server is holding a LIVE session with us in it - the
				// travel failed, or the app was killed on the loading screen. The honest answer is not
				// an error, it is to go and get that session and walk in.
				if (Code == TEXT("BR_ALREADY_IN_MATCH"))
				{
					Me->ColdCheck(true);
				}
				Me->OnChanged.Broadcast();
				if (Done)
				{
					Done(false, Error);
				}
				return;
			}
			// 201 with a body, or 204 with none: either way we are queued until told otherwise.
			Me->State = ECampusBRState::Queued;
			Me->ReadQueue(Json);
			if (Done)
			{
				Done(true, FString());
			}
		});
	};

	// Log in to EOS first when it is configured, so the puid is real by the time the ticket is cut.
	// A failure is not fatal - the ticket simply goes up without one and the match stays on the LAN.
	UCampusEOS* Eos = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusEOS>() : nullptr;
	if (!Eos || !Eos->IsConfigured())
	{
		Post(FString());
		return;
	}
	TWeakObjectPtr<UCampusEOS> WeakEos(Eos);
	Eos->EnsureLogin([Post, WeakEos](bool bOk)
	{
		Post(bOk && WeakEos.IsValid() ? WeakEos->GetLocalPuid() : FString());
	});
}

void UCampusBRQueue::Leave(FDone Done)
{
	UCampusBackend* Backend = Api();
	// Only the leader's ticket exists, and deleting it takes the WHOLE TEAM out of the queue - that is
	// the server's contract, not an extra call. A member has nothing to delete and is refused with
	// NOT_PARTY_LEADER, so they only reset locally.
	//
	// IT ALSO CANCELS A SESSION THAT IS STILL FILLING: the backend closes it and requeues everyone else
	// (they get `bs.ended` with requeued). Once the session is SEALED there is nothing to cancel - the
	// server answers 409 BS_SESSION_STARTED - so the call is not even made.
	const bool bWasQueued = (IsQueued() || (IsMatched() && !IsSealed())) && !MustWaitForLeader();
	// The UI must be free the instant CANCEL is tapped: the local state goes back now and the
	// DELETE is best-effort. A queue entry the server still holds is dropped by its own TTL.
	Reset();
	OnChanged.Broadcast();

	if (!Backend || !bWasQueued)
	{
		if (Done)
		{
			Done(true, FString());
		}
		return;
	}
	Backend->CallApi(TEXT("DELETE"), TEXT("/br/queue"), nullptr, true,
		[Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject>)
	{
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusBRQueue::Reset()
{
	State = ECampusBRState::Idle;
	Match = FCampusBRMatch();
	Elapsed = 0.f;
	EtaSeconds = 0;
	Found = 0;
	ShownSecond = -1;
	PlacedKey.Reset();
	SealedKey.Reset();
	CountdownReadAt = 0.0;
	StopTimer();
}

void UCampusBRQueue::AllowRetry()
{
	// Keep the session we hold; only forget that it was already handed over, so the next read (or the
	// next START) can offer it again. The session itself is still ours until the server ends it.
	PlacedKey.Reset();
}

void UCampusBRQueue::ColdCheck(bool bForce)
{
	UCampusBackend* Backend = Api();
	if (!Backend || !Backend->IsSignedIn())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bForce && LastColdCheckAt > 0.0 && Now - LastColdCheckAt < ColdCheckSeconds)
	{
		return;
	}
	LastColdCheckAt = Now;
	// Deliberately NOT routed through Refresh's "am I queued?" gate: the whole point is the phone that
	// does not know it was placed. A 404 NOT_QUEUED is the common answer and costs one request a minute.
	TWeakObjectPtr<UCampusBRQueue> WeakThis(this);
	Backend->CallApi(TEXT("GET"), TEXT("/br/queue/me"), nullptr, false,
		[WeakThis](bool bOk, const FString&, TSharedPtr<FJsonObject> Json)
	{
		UCampusBRQueue* Self = WeakThis.Get();
		if (Self && bOk)
		{
			Self->ReadQueue(Json);
		}
	});
}

void UCampusBRQueue::Refresh(FDone Done)
{
	UCampusBackend* Backend = Api();
	if (!Backend || State == ECampusBRState::Idle)
	{
		if (Done)
		{
			Done(false, FString());
		}
		return;
	}
	TWeakObjectPtr<UCampusBRQueue> WeakThis(this);
	Backend->CallApi(TEXT("GET"), TEXT("/br/queue/me"), nullptr, false,
		[WeakThis, Done](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusBRQueue* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		// A failed poll is NOT a failed queue: the fallback is there because the network is unreliable,
		// so it stays queued and tries again in five seconds. Only the server saying "not queued"
		// takes the player out of the overlay.
		if (bOk)
		{
			Self->ReadQueue(Json);
		}
		else if (BrErrorCode(Json).Contains(TEXT("NOT_QUEUED")))
		{
			// A 404 here is the one failure that IS an answer: there is no ticket for us. It is how a
			// requeue that did not take is found out, and a timer counting up against nothing is worse
			// than being sent back to the lobby.
			Self->LastError = TEXT("Matchmaking stopped. Press START to try again.");
			Self->EndedNotice = Self->LastError;
			Self->Reset();
			Self->OnChanged.Broadcast();
			Self->OnSessionEnded.Broadcast(Self->LastError);
		}
		if (Done)
		{
			Done(bOk, Error);
		}
	});
}

void UCampusBRQueue::ReadQueue(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		OnChanged.Broadcast();
		return;
	}
	// POST answers with {ticket:{state, eta_s, max_humans, total_slots, ...}}; GET /br/queue/me
	// answers with the same fields at the top level. Read the ticket when it is there, the body
	// otherwise, so one function covers both.
	const TSharedPtr<FJsonObject> Ticket = BrObj(Json, TEXT("ticket"));
	const TSharedPtr<FJsonObject>& Source = Ticket.IsValid() ? Ticket : Json;

	const FString Reported = BrStr(Source, TEXT("state"));
	// The queue's own mode is the authority on the team size, which matters for the phone that did NOT
	// press START: a member's overlay has no local choice to read.
	const FString ReportedMode = BrStr(Source, TEXT("mode"));
	if (!ReportedMode.IsEmpty())
	{
		TeamSize = CampusBR::TeamSizeForMode(ReportedMode);
	}
	const double Eta = BrNum(Source, TEXT("eta_s"));
	if (Eta > 0.0)
	{
		EtaSeconds = int32(Eta);
	}
	// The queue already knows how big a match is; showing 50 before the first player is found is the
	// honest shape of the thing.
	const int32 Slots = int32(BrNum(Source, TEXT("total_slots")));
	if (Slots > 0)
	{
		TotalSlots = Slots;
	}
	const int32 Party = int32(BrNum(Source, TEXT("party_size")));
	if (Party > 0)
	{
		// Until the server reports a lobby fill, "players found" is at least my own squad.
		Found = FMath::Max(Found, Party);
	}
	// `battle_session` is the name the design gives the object and `match` is the one the shipped
	// client reads; they are the SAME object, so prefer the new name and fall back to the old one.
	TSharedPtr<FJsonObject> Inner = BrObj(Json, TEXT("battle_session"));
	if (!Inner.IsValid())
	{
		Inner = BrObj(Json, TEXT("match"));
	}
	if (Inner.IsValid())
	{
		TakeMatch(Inner);
		return;
	}
	if (Reported == TEXT("queued"))
	{
		State = ECampusBRState::Queued;
		// A member who learns from this read that their leader queued them should count with everyone
		// else: the timer is what the overlay draws, and it is not running on their phone yet.
		StartTimer();
	}
	else if (!Reported.IsEmpty() && Reported != TEXT("matched"))
	{
		// idle / cancelled / expired: the server has no entry for us, so neither do we.
		State = ECampusBRState::Idle;
		StopTimer();
	}
	// Some servers report the lobby fill on the queue itself; take it when it is there.
	const int32 Humans = int32(BrNum(Json, TEXT("humans")));
	if (Humans > 0)
	{
		Found = Humans;
	}
	OnChanged.Broadcast();
}

void UCampusBRQueue::TakeMatch(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	FCampusBRMatch Next;
	Next.Code = BrStr(Json, TEXT("code"));
	Next.SessionId = BrStr(Json, TEXT("session_id"));
	Next.Mode = BrStr(Json, TEXT("mode"));
	Next.Region = BrStr(Json, TEXT("region"));
	Next.State = BrStr(Json, TEXT("state"));
	// The server's own flag first; an older body that only carries the state is still right about it.
	Next.bSealed = BrBool(Json, TEXT("sealed")) || BrIsSealedState(Next.State);
	Next.HostPlayerId = BrStr(Json, TEXT("host_player_id"));
	Next.TeamId = BrStr(Json, TEXT("team_id"));
	Next.Map = BrStr(Json, TEXT("map"));
	Next.bNight = BrStr(Json, TEXT("day_night")).Equals(TEXT("night"), ESearchCase::IgnoreCase);
	Next.Humans = int32(BrNum(Json, TEXT("humans")));
	Next.MaxHumans = int32(BrNum(Json, TEXT("max_humans")));
	const int32 Slots = int32(BrNum(Json, TEXT("total_slots")));
	Next.TotalSlots = Slots > 0 ? Slots : 50;
	// The server does the subtraction (it owns the roster); an older one that does not send `bots` is
	// still right about the two numbers it does send.
	const int32 ReportedBots = int32(BrNum(Json, TEXT("bots")));
	Next.Bots = ReportedBots > 0 ? ReportedBots : FMath::Max(0, Next.TotalSlots - Next.Humans);
	Next.CountdownSeconds = int32(BrNum(Json, TEXT("countdown_s")));
	// Seconds until the FILL CAP. The host carries it into the level, where it is all the safety net
	// needs to know when a seal has gone missing.
	Next.FillSeconds = int32(BrNum(Json, TEXT("fill_s")));
	FDateTime Starts;
	if (FDateTime::ParseIso8601(*BrStr(Json, TEXT("starts_at")), Starts))
	{
		Next.StartsAt = Starts;
	}
	if (const TSharedPtr<FJsonObject> Connect = BrObj(Json, TEXT("connect")))
	{
		// "eos" = the host phone's P2P relay identity; "dedicated" = an address to dial, which is the
		// AWS path and the only thing that changes when the allocator is flipped.
		Next.ConnectType = BrStr(Connect, TEXT("type"));
		Next.HostPuid = BrStr(Connect, TEXT("puid"));
		Next.Address = BrStr(Connect, TEXT("address"));
		const int32 Port = int32(BrNum(Connect, TEXT("port")));
		Next.Port = Port > 0 ? Port : 7777;
	}
	if (!Next.IsValid())
	{
		return;
	}
	// THE DEDUPE, keyed on the SESSION ID. The server emits `bs.placed` and `br.matched` with the same
	// payload, the fallback read delivers it a third time, and travelling twice tears the level down
	// mid-load - so the lobby hears about one session exactly once.
	const bool bNew = Next.Key() != PlacedKey;
	Match = Next;
	CountdownReadAt = FPlatformTime::Seconds();
	Found = FMath::Max(Found, Next.Humans);
	TotalSlots = Next.TotalSlots;
	if (!Next.Mode.IsEmpty())
	{
		TeamSize = CampusBR::TeamSizeForMode(Next.Mode);
	}
	State = ECampusBRState::Matched;
	StopTimer();
	OnChanged.Broadcast();
	if (bNew)
	{
		PlacedKey = Next.Key();
		// THE HOST BEATS FROM THE LOBBY, NOW - not when the level finishes loading. The server deletes a
		// session nobody has beaten inside ten seconds as empty, and a level load is longer than that.
		// This is the first moment this phone can know it hosts (the POST reply or `bs.placed`), so it is
		// where the beat starts.
		if (AmHost())
		{
			if (UCampusBRHostBeat* Beat = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UCampusBRHostBeat>() : nullptr)
			{
				Beat->Begin(Match.Handle());
			}
		}
		OnMatched.Broadcast(Match);
	}
}

void UCampusBRQueue::TakeSeal(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	// Bare, or wrapped the way a placement is. One shape, whichever producer sent it.
	TSharedPtr<FJsonObject> Inner = BrObj(Json, TEXT("battle_session"));
	if (!Inner.IsValid())
	{
		Inner = BrObj(Json, TEXT("match"));
	}
	const TSharedPtr<FJsonObject>& Source = Inner.IsValid() ? Inner : Json;

	const FString Sealed = BrStr(Source, TEXT("session_id"));
	const FString Code = BrStr(Source, TEXT("code"));
	// ONLY THE SESSION THIS PHONE IS IN. A late seal for a session we already finished must never close
	// the one we are standing in now (the same rule TakeEnded follows).
	const bool bOther = (!Sealed.IsEmpty() && !Match.SessionId.IsEmpty() && Sealed != Match.SessionId)
		|| (!Code.IsEmpty() && !Match.Code.IsEmpty() && Code != Match.Code);
	if (bOther)
	{
		return;
	}
	// THE DEDUPE, as for a placement: `bs.sealed` and the heartbeat response carry the same decision.
	FString Key = Sealed.IsEmpty() ? Code : Sealed;
	if (Key.IsEmpty())
	{
		Key = Match.Key();
	}
	if (!Key.IsEmpty() && Key == SealedKey)
	{
		return;
	}
	SealedKey = Key;

	const int32 Humans = int32(BrNum(Source, TEXT("humans")));
	int32 Bots = int32(BrNum(Source, TEXT("bots")));
	const int32 Slots = int32(BrNum(Source, TEXT("total_slots")));
	// The server owns the subtraction, but one that sends only the two counts is still right about them.
	if (Bots <= 0 && Slots > 0 && Humans > 0)
	{
		Bots = FMath::Max(0, Slots - Humans);
	}
	float Countdown = float(BrNum(Source, TEXT("countdown_s")));
	// `starts_at` is the authority when the countdown field is absent: measured against the server's own
	// clock when it sent one, so a phone with a wrong wall clock cannot shorten or stretch the drop.
	if (Countdown <= 0.f)
	{
		FDateTime Starts;
		FDateTime ServerNow;
		if (FDateTime::ParseIso8601(*BrStr(Source, TEXT("starts_at")), Starts))
		{
			if (!FDateTime::ParseIso8601(*BrStr(Source, TEXT("server_time")), ServerNow))
			{
				ServerNow = FDateTime::UtcNow();
			}
			Countdown = FMath::Max(0.f, float((Starts - ServerNow).GetTotalSeconds()));
		}
	}

	// Mirror it into the session we hold, so a player still on the matchmaking screen (the travel is
	// slower than the socket) sees the real fill and the real countdown rather than the placement's.
	Match.bSealed = true;
	Match.State = TEXT("COUNTDOWN");
	Match.FillSeconds = 0;
	if (Humans > 0)
	{
		Match.Humans = Humans;
		Found = FMath::Max(Found, Humans);
	}
	if (Bots > 0)
	{
		Match.Bots = Bots;
	}
	if (Countdown > 0.f)
	{
		Match.CountdownSeconds = int32(Countdown);
		CountdownReadAt = FPlatformTime::Seconds();
	}
	UE_LOG(LogTemp, Log, TEXT("[CampusBR] session sealed (%s): %d humans, %d bots, %.0f s"),
		*BrStr(Source, TEXT("reason")), Humans, Bots, Countdown);
	OnChanged.Broadcast();
	OnSessionSealed.Broadcast(Humans, Bots, Countdown);
}

void UCampusBRQueue::TakeLeft(const TSharedPtr<FJsonObject>& Json)
{
	const FString Left = BrStr(Json, TEXT("session_id"));
	const FString Code = BrStr(Json, TEXT("code"));
	// Only the session we are in, for the same reason as TakeEnded.
	const bool bMine = Match.IsValid()
		&& ((!Left.IsEmpty() && Left == Match.SessionId) || (!Code.IsEmpty() && Code == Match.Code));
	if (!bMine)
	{
		return;
	}
	// The leader cancelled while the session was still filling. The members are out of a session that no
	// longer exists, so they go back to the lobby with the reason - not to a platform nobody will seal.
	LastError = TEXT("The squad leader cancelled the match.");
	EndedNotice = LastError;
	Reset();
	OnChanged.Broadcast();
	OnSessionEnded.Broadcast(LastError);
}

void UCampusBRQueue::TakeEnded(const TSharedPtr<FJsonObject>& Json)
{
	const FString Ended = BrStr(Json, TEXT("session_id"));
	const FString Code = BrStr(Json, TEXT("code"));
	// Only the session we are actually in: a late sweep for a session we already finished must not
	// throw a player out of the one they are in now.
	const bool bMine = Match.IsValid()
		&& ((!Ended.IsEmpty() && Ended == Match.SessionId) || (!Code.IsEmpty() && Code == Match.Code));
	if (!bMine)
	{
		return;
	}
	const FString Reason = BrStr(Json, TEXT("reason"));
	// REQUEUED: the server put our ticket straight back in the queue, so this is not the end of
	// anything the player has to be told about - it is a search that is still running. Throwing them out
	// to the lobby here would make a recovery look like a failure.
	if (BrBool(Json, TEXT("requeued")))
	{
		UE_LOG(LogTemp, Log, TEXT("[CampusBR] session ended (%s) and the ticket was requeued"), *Reason);
		Match = FCampusBRMatch();
		PlacedKey.Reset();
		SealedKey.Reset();
		CountdownReadAt = 0.0;
		Found = 0;
		State = ECampusBRState::Queued;
		// The overlay's timer is what tells the player the search is alive; it stopped when we matched.
		StartTimer();
		OnChanged.Broadcast();
		return;
	}
	// Two reasons, one sentence: a player does not care whether the host went quiet or the session
	// went stale, only that nobody is running it any more.
	LastError = Reason == TEXT("stale")
		? TEXT("Match ended - the session timed out.")
		: TEXT("Host disconnected - match ended.");
	// Held for the LOBBY: a player who is in the match level right now is about to travel, and the
	// widget that can show this sentence does not exist yet.
	EndedNotice = LastError;
	Reset();
	OnChanged.Broadcast();
	OnSessionEnded.Broadcast(LastError);
}

FString UCampusBRQueue::TakeEndedNotice()
{
	FString Notice = MoveTemp(EndedNotice);
	EndedNotice.Reset();
	return Notice;
}

void UCampusBRQueue::TakeResult(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	FCampusBRResult Scored;
	Scored.SessionId = BrStr(Json, TEXT("session_id"));
	Scored.Mode = BrStr(Json, TEXT("mode"));
	Scored.Placement = int32(BrNum(Json, TEXT("placement")));
	Scored.Kills = int32(BrNum(Json, TEXT("kills")));
	Scored.XpAwarded = int32(BrNum(Json, TEXT("xp_awarded")));
	Json->TryGetBoolField(TEXT("rated"), Scored.bRated);
	// The session is over and paid for: the queue goes back to idle so the next START is a clean one.
	Reset();
	OnChanged.Broadcast();
	OnScored.Broadcast(Scored);
}

void UCampusBRQueue::StartTimer()
{
	if (TickHandle.IsValid())
	{
		return;
	}
	PollIn = PollSeconds;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UCampusBRQueue::Tick), 0.f);
}

void UCampusBRQueue::StopTimer()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

bool UCampusBRQueue::Tick(float DeltaTime)
{
	if (State != ECampusBRState::Queued && State != ECampusBRState::Joining)
	{
		return true;
	}
	Elapsed += DeltaTime;
	const int32 Second = int32(Elapsed);
	if (Second != ShownSecond)
	{
		ShownSecond = Second;
		// One repaint a second, which is what a counting-up timer actually needs.
		OnChanged.Broadcast();
	}

	// THE FALLBACK, AND ONLY THE FALLBACK. With the socket live the server pushes `bs.placed` and this
	// never spends a request (see the metered-tunnel note in CampusBR.h).
	const UCampusSocial* Social = Live();
	const bool bSocketUp = Social && Social->IsLive();
	if (bSocketUp)
	{
		PollIn = PollSeconds;
		return true;
	}
	PollIn -= DeltaTime;
	if (PollIn <= 0.f)
	{
		PollIn = PollSeconds;
		Refresh(nullptr);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The host's heartbeat (POST /v1/br/:handle/heartbeat)

void UCampusBRHostBeat::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

UCampusBackend* UCampusBRHostBeat::Api() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBackend>() : nullptr;
}

void UCampusBRHostBeat::Begin(const FString& InHandle)
{
	// THE LOBBY'S CALL, the instant this phone learns it hosts. No owner yet - there is no game mode and
	// no level - so the beat is bounded by LobbyGraceSeconds until Report claims it.
	Start(InHandle);
}

void UCampusBRHostBeat::Start(const FString& InHandle)
{
	const UCampusBackend* Backend = Api();
	// No handle means this is not a battle session (editor play, a custom room), and no credential means
	// a DEDICATED server is running the match - it carries no player token yet (docs/BATTLE_SESSION.md
	// §3), so there is nothing to authorise a beat with.
	if (InHandle.IsEmpty() || !Backend || !Backend->IsSignedIn())
	{
		return;
	}
	Handle = InHandle;
	if (TickHandle.IsValid())
	{
		return;
	}
	// The session is already FILLING on the server, so the first beat is BARE: announcing a state it is
	// already in is a 409 BS_BAD_STATE.
	Announced = ECampusBRBeat::Filling;
	Wanted = ECampusBRBeat::Filling;
	UnownedFor = 0.f;
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UCampusBRHostBeat::Tick), 0.f);
	// IMMEDIATELY, not in five seconds: the server deletes a session nobody has beaten within ten.
	Send();
}

void UCampusBRHostBeat::Report(AActor* InOwner, const FString& InHandle, ECampusBRBeat Beat, FAliveCount InAlive)
{
	if (Beat == ECampusBRBeat::Ended)
	{
		// The result route ends the session; a beat announcing ENDED is not even a legal transition.
		Stop();
		return;
	}
	if (!InOwner || InOwner->GetNetMode() == NM_Client)
	{
		return;
	}
	Start(InHandle);
	if (!TickHandle.IsValid())
	{
		return;
	}
	// The game mode claims the beat: from here on ITS life is the beat's life.
	Owner = InOwner;
	bOwned = true;
	if (InAlive)
	{
		Alive = MoveTemp(InAlive);
	}
	const ECampusBRBeat Previous = Wanted;
	Wanted = Beat;
	// A transition the players can see must not wait for the next scheduled beat - but only the two
	// states a host may actually announce are worth a request of their own. FILLING -> COUNTDOWN is the
	// server's own move and is never sent.
	if (Wanted != Previous && BrAnnounceable(Wanted))
	{
		Send();
	}
}

void UCampusBRHostBeat::Stop()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
	Handle.Reset();
	Owner.Reset();
	Alive = nullptr;
	bOwned = false;
	bSending = false;
	UnownedFor = 0.f;
}

void UCampusBRHostBeat::Send()
{
	UCampusBackend* Backend = Api();
	if (Handle.IsEmpty() || !Backend)
	{
		return;
	}
	// One beat in flight at a time. A state change that lands on top of one must not wait the full
	// twenty seconds, though: the plane has already left the map by then.
	if (bSending)
	{
		NextBeatIn = FMath::Min(NextBeatIn, 1.f);
		return;
	}
	bSending = true;
	// Five seconds while the session is filling, twenty once it is sealed and the match is running.
	// Five seconds throughout: a phone that seals LOCALLY (solo testing, a lost bs.sealed) is still
	// FILLING on the server, whose host_quiet clock is far shorter than the old twenty-second beat.
	NextBeatIn = FillingBeatSeconds;
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	// The STATE is sent only when it is a move AND a move a host is allowed to announce: "I am still
	// here" is the whole job of every other beat. COUNTDOWN is never sent - it is the server's to make.
	const bool bMoving = Wanted != Announced && BrAnnounceable(Wanted);
	if (bMoving)
	{
		Body->SetStringField(TEXT("state"), BeatName(Wanted));
	}
	if (Alive)
	{
		Body->SetNumberField(TEXT("alive"), FMath::Clamp(Alive(), 0, 100));
	}
	const ECampusBRBeat Sent = Wanted;
	TWeakObjectPtr<UCampusBRHostBeat> WeakThis(this);
	// NOT idempotency-keyed: a heartbeat is a fact about now, and replaying an old one is a lie.
	Backend->CallApi(TEXT("POST"), FString::Printf(TEXT("/br/%s/heartbeat"), *Handle), Body, false,
		[WeakThis, Sent, bMoving](bool bOk, const FString& Error, TSharedPtr<FJsonObject> Json)
	{
		UCampusBRHostBeat* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		Self->bSending = false;
		if (bOk)
		{
			Self->Announced = Sent;
			// THE SEAL'S SECOND DOOR: the server can answer the beat with the sealed state and the fill
			// numbers instead of (or as well as) pushing `bs.sealed`.
			Self->TakeBeatState(Json);
			return;
		}
		const FString Code = BrErrorCode(Json);
		// BS_ENDED: the result landed, or the sweep settled it. The host is told the truth and stops.
		// BR_MATCH_NOT_FOUND: this phone does not host that session - beating again cannot fix it.
		if (Code == TEXT("BS_ENDED") || Code == TEXT("BR_MATCH_NOT_FOUND"))
		{
			UE_LOG(LogTemp, Log, TEXT("[CampusBR] heartbeat stopped: %s"), *Error);
			Self->Stop();
			return;
		}
		// BS_BAD_STATE: the server disagrees about the move (a sweep, or a phase we missed). Stop
		// re-announcing it and keep beating, because being quiet is what actually ends the session.
		if (bMoving && Code == TEXT("BS_BAD_STATE"))
		{
			Self->Announced = Sent;
		}
	});
}

void UCampusBRHostBeat::TakeBeatState(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid())
	{
		return;
	}
	TSharedPtr<FJsonObject> Session = BrObj(Json, TEXT("battle_session"));
	if (!Session.IsValid())
	{
		Session = BrObj(Json, TEXT("match"));
	}
	const TSharedPtr<FJsonObject>& Source = Session.IsValid() ? Session : Json;
	// THE BEAT ITSELF CAN SEAL THE SESSION: the fill cap runs out on the server, and this response is
	// how a host whose socket is down finds out. `sealed` is the flag; the state is the older way of
	// saying the same thing. Nothing to disambiguate - a host never announces COUNTDOWN.
	if (!BrBool(Source, TEXT("sealed")) && !BrIsSealedState(BrStr(Source, TEXT("state"))))
	{
		return;
	}
	// One owner for the parsing and the dedupe: the queue. This function only decides whether the body
	// is a seal at all.
	if (UCampusBRQueue* Queue = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCampusBRQueue>() : nullptr)
	{
		Queue->TakeSeal(Source);
	}
}

bool UCampusBRHostBeat::Tick(float DeltaTime)
{
	// The match is gone (the player quit, the level tore down): a beat for a session nobody is in would
	// keep it alive in the matchmaker's eyes, which is exactly what the sweep exists to prevent. Before
	// a game mode has claimed the beat there is no actor to watch, so the travel gets a grace period and
	// no more - a travel that never lands must not hold a session open for a player who is not coming.
	UnownedFor = bOwned ? 0.f : UnownedFor + DeltaTime;
	if (Handle.IsEmpty() || (bOwned && !Owner.IsValid()) || UnownedFor > LobbyGraceSeconds)
	{
		// Returning false is what removes this ticker, so drop the handle FIRST: Stop() must not try to
		// remove the delegate it is currently running inside.
		TickHandle.Reset();
		Stop();
		return false;
	}
	NextBeatIn -= DeltaTime;
	if (NextBeatIn <= 0.f)
	{
		Send();
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
