// BATTLE ROYALE matchmaking, client side. One game-instance subsystem so the queue survives the
// lobby widget being rebuilt (and a level travel) and there is exactly one place that knows whether
// this phone is queued.
//
//   POST   /v1/br/queue      {mode: solo|duo|squad}   -> join
//   DELETE /v1/br/queue                               -> cancel
//   GET    /v1/br/queue/me                            -> {state, eta_s, match{...}}
//   ws     "br.matched"                               -> the same match object, pushed
//
// THE SOCKET IS THE PRIMARY CHANNEL. The API lives behind a tunnel metered at 20,000 requests a
// MONTH (see CampusSocial.h), so GET /br/queue/me runs at 5 s ONLY while the socket is not live -
// it is the fallback, not the design. With the socket up the whole queue costs two requests: the
// join and the cancel.
//
// Nothing here travels or opens a level: UCampusBRQueue reports, and ACampusLobbyPlayerController::
// JoinBRMatch does the moving. One owner for the queue, one owner for travel.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CampusBR.generated.h"

class FJsonObject;
class UCampusBackend;
class UCampusSocial;

/** The `match` object, from the queue poll or the br.matched event - the same shape either way. */
struct FCampusBRMatch
{
	FString Code;
	/** "eos" (P2P, works over the internet) or "lan" (Address:Port on this Wi-Fi). */
	FString ConnectType;
	FString HostPuid;
	FString Address;
	int32 Port = 7777;
	/** Whose phone hosts the listen server. Equal to my player id = I am the host. */
	FString HostPlayerId;
	FString TeamId;
	/** Catalog map id (CampusCatalog::Maps), e.g. "VarsityValley". */
	FString Map;
	/** "day" | "night" from the server, which has the final word on the lighting. */
	bool bNight = false;
	int32 Humans = 0;
	int32 MaxHumans = 0;
	int32 TotalSlots = 50;
	/** Server clock; only used for the "STARTING" beat, never for gameplay timing. */
	FDateTime StartsAt = FDateTime();

	bool IsValid() const { return !Code.IsEmpty(); }
	bool IsEos() const { return ConnectType == TEXT("eos") && !HostPuid.IsEmpty(); }
	bool HasLan() const { return !Address.IsEmpty(); }
};

/** Where this phone is in the queue. Idle is also where a cancel and a failure land. */
enum class ECampusBRState : uint8 { Idle, Joining, Queued, Matched, Leaving };

/** A match object arrived (socket or poll). Declared outside the class, like the social delegates,
 *  so UHT never has to parse it. */
DECLARE_MULTICAST_DELEGATE_OneParam(FCampusBRMatched, const FCampusBRMatch&);

namespace CampusBR
{
	/** Catalog map id -> level to open. Unknown ids fall back to the campus, so a server that names a
	 *  map this build does not have still starts a match instead of a black screen. */
	const TCHAR* LevelForMap(const FString& MapId);

	/** 1 -> "solo", 2 -> "duo", anything else -> "squad". The API's three queue names. */
	const TCHAR* ModeForTeamSize(int32 TeamSize);

	/** "SOLO" / "DUO" / "SQUAD" for the overlay's subtitle. */
	FText TeamSizeLabel(int32 TeamSize);
}

UCLASS()
class UCampusBRQueue : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FDone = TFunction<void(bool bOk, const FString& Error)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** POST /v1/br/queue. TeamSize is the lobby's 1 / 2 / 4. */
	void Join(int32 TeamSize, bool bNightWanted, FDone Done);
	/** DELETE /v1/br/queue. Safe to call when not queued; it just resets. */
	void Leave(FDone Done = nullptr);
	/** GET /v1/br/queue/me. Called by the fallback timer and once on entering the overlay. */
	void Refresh(FDone Done = nullptr);

	ECampusBRState GetState() const { return State; }
	bool IsBusy() const { return State == ECampusBRState::Joining || State == ECampusBRState::Leaving; }
	bool IsQueued() const { return State == ECampusBRState::Queued || State == ECampusBRState::Joining; }
	bool IsMatched() const { return State == ECampusBRState::Matched; }
	const FCampusBRMatch& GetMatch() const { return Match; }
	/** Seconds since JOIN, counted locally so the timer runs on with the socket down. */
	float GetElapsed() const { return Elapsed; }
	/** The server's estimate, 0 when it did not send one. */
	int32 GetEta() const { return EtaSeconds; }
	int32 GetFound() const { return Found; }
	int32 GetTotalSlots() const { return TotalSlots; }
	/** The queue the player joined, for the overlay's subtitle. */
	int32 GetTeamSize() const { return TeamSize; }
	bool WantsNight() const { return bNight; }
	/** True when this phone hosts the match the server just handed us. */
	bool AmHost() const;
	/** The last refusal worth printing, or empty. */
	const FString& GetError() const { return LastError; }

	/** Repaint: state, timer bucket or found-count changed. */
	FSimpleMulticastDelegate OnChanged;
	/** A match object arrived (socket or poll). The lobby travels; nothing here does. */
	FCampusBRMatched OnMatched;

private:
	UCampusBackend* Api() const;
	UCampusSocial* Live() const;

	/** Reads {state, eta_s, match} - the body of GET /br/queue/me and of POST /br/queue alike. */
	void ReadQueue(const TSharedPtr<FJsonObject>& Json);
	/** Reads one `match` object and, if it is new, fires OnMatched exactly once. */
	void TakeMatch(const TSharedPtr<FJsonObject>& Json);
	bool Tick(float DeltaTime);
	void StartTimer();
	void StopTimer();

	ECampusBRState State = ECampusBRState::Idle;
	FCampusBRMatch Match;
	int32 TeamSize = 1;
	bool bNight = false;
	int32 EtaSeconds = 0;
	int32 Found = 0;
	int32 TotalSlots = 50;
	float Elapsed = 0.f;
	/** Seconds until the next fallback poll; only counted down while the socket is dead. */
	float PollIn = 0.f;
	/** The last whole second broadcast, so OnChanged fires at 1 Hz and not per frame. */
	int32 ShownSecond = -1;
	FString LastError;
	FDelegateHandle MatchedHandle;
	FTSTicker::FDelegateHandle TickHandle;
};
