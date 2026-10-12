// Internet play. Matches are still hosted on a player's phone, but over Epic Online Services P2P
// (relayed when neither phone can be reached directly) instead of a LAN address, so two players on
// mobile data or on different Wi-Fi can play together.
//
// What this subsystem owns: one EOS Connect login per app run, with a *device id* credential - no
// Epic account, no sign-in screen, nothing for the player to do. The login gives this phone a
// ProductUserId ("puid", 32 hex characters). The host sends its puid to the API with the room; a
// joiner travels to "[EOS:<host puid>]" and the engine's EOS net driver (NetDriverEOS, configured
// as the GameNetDriver in DefaultEngine.ini) carries the match traffic over P2P.
//
// Everything degrades to the old LAN path: if the EOS keys in DefaultEngine.ini are blank, or the
// login fails, IsReady() stays false, the room is created without a puid and the match is hosted
// and joined by ip:port exactly as before. Voice is untouched - it rides the net connection, which
// is still a UIpConnection subclass (UNetConnectionEOS), and still resolves through
// OnlineSubsystemNull. Google Play billing is untouched too: it is reached by name.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CampusEOS.generated.h"

class IEOSPlatformHandle;

UCLASS()
class UCampusEOS : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** bOk = this phone has a ProductUserId and can host or join an EOS room. */
	using FDone = TFunction<void(bool bOk)>;

	virtual void Deinitialize() override;

	/** The owner filled in the EOS artifact keys (DefaultEngine.ini) and this build can use them.
	 *  False means "play on the LAN": no EOS call is attempted at all. */
	bool IsConfigured() const;

	/** Logged in, with a ProductUserId in hand. */
	bool IsReady() const { return Phase == EPhase::Ready && !LocalPuid.IsEmpty(); }

	/** This phone's EOS ProductUserId (32 hex). Empty until IsReady(). Sent to the API as eos_puid. */
	const FString& GetLocalPuid() const { return LocalPuid; }

	/** Device-id Connect login, once per app run. Safe to call at any time and from anywhere:
	 *  fires Done straight away when already logged in, queues it while a login is in flight, and
	 *  retries after a failure (the player may have had no network a minute ago). Done may be null
	 *  to just warm the login up. Never blocks and never fails the caller's flow - on failure the
	 *  caller is expected to fall back to the LAN path or say so. */
	void EnsureLogin(FDone Done);

	/** The travel URL for a host's ProductUserId: "[EOS:<puid>]".
	 *  The brackets are not decoration - FURL only keeps a colon inside a host when it is bracketed
	 *  (the IPv6 rule), and without them "EOS:<puid>" is parsed as protocol "EOS". */
	static FString ConnectUrlFor(const FString& HostPuid);

	/** Why the last login attempt failed; empty when it did not. */
	const FString& GetLastError() const { return LastError; }

private:
	enum class EPhase : uint8
	{
		Idle,
		LoggingIn,
		Ready,
		Failed
	};

	/** Brings up the EOS platform (through the v2 online services) and registers a device id for it. */
	void StartLogin();
	/** EOS_Connect_Login with the device-id credential. */
	void ConnectLogin();
	/** Reads the logged-in ProductUserId out of the Connect interface. */
	void ReadLocalPuid();
	void Finish(bool bOk, const FString& Error);

	EPhase Phase = EPhase::Idle;
	FString LocalPuid;
	FString LastError;

	/** Callers waiting on the login in flight. */
	TArray<FDone> Waiting;

	/** Kept alive for as long as we need the Connect interface; the online services hold one too. */
	TSharedPtr<IEOSPlatformHandle, ESPMode::ThreadSafe> Platform;
};
