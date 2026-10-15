// Where the game finds its backend. Today: this PC on the shared Wi-Fi, standing in for the AWS box.
// Change the values in Config/DefaultGame.ini under [/Script/CollegeCampusGame.CampusNetSettings].

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CampusNetSettings.generated.h"

UCLASS(config=Game, defaultconfig)
class UCampusNetSettings : public UObject
{
	GENERATED_BODY()

public:
	/** LAN IP of the PC running the API and the match server (later: the AWS box's address). */
	UPROPERTY(Config)
	FString ServerHost = TEXT("192.168.1.5");

	/** Node API (accounts, coins, rooms). */
	UPROPERTY(Config)
	int32 ApiPort = 8080;

	/** Unreal match server (UDP). */
	UPROPERTY(Config)
	int32 GamePort = 7777;

	/** Full API base (e.g. an https tunnel / cloud host, ending in /v1). Empty = http://ServerHost:ApiPort/v1 on the LAN. */
	UPROPERTY(Config)
	FString ApiBaseOverride;

	/** Same API as host + path with NO "//" (e.g. my-tunnel.ngrok-free.dev/v1), always served over https.
	 *  Unreal's ini reader cuts unquoted values at "//" (comment), so this key cannot be truncated. */
	UPROPERTY(Config)
	FString ApiHost;

	/** Which matchmaking queue this build stands in. The battle session queue is one ZSET per mode x
	 *  REGION (backend docs/BATTLE_SESSION.md §1), and a region the API has no matchmaker for is a 409
	 *  BS_REGION_UNKNOWN rather than a queue nobody is watching - so this must match BS_REGIONS. */
	UPROPERTY(Config)
	FString MatchRegion = TEXT("IN");

	FString ApiBaseUrl() const
	{
		FString Base = ApiBaseOverride.TrimStartAndEnd();
		Base.RemoveFromStart(TEXT("\""));
		Base.RemoveFromEnd(TEXT("\""));
		// a value cut at "//" reads back as "https:" - rebuild it from the slash-free host instead
		if (!Base.Contains(TEXT("://")) || Base.EndsWith(TEXT("://")))
		{
			const FString Host = ApiHost.TrimStartAndEnd();
			Base = Host.IsEmpty() ? FString() : TEXT("https://") + Host;
		}
		Base.RemoveFromEnd(TEXT("/"));
		return Base.IsEmpty() ? FString::Printf(TEXT("http://%s:%d/v1"), *ServerHost, ApiPort) : Base;
	}

	/** The social WebSocket (UCampusSocial): the SAME host, port and path prefix as the API, with the
	 *  scheme swapped, so the upgrade is one ordinary request to the same origin and rides the ngrok
	 *  tunnel unchanged. Derived from ApiBaseUrl() on purpose - the quote-stripping and the "//"
	 *  truncation defence above must never be re-implemented anywhere else. */
	FString WsBaseUrl() const
	{
		FString Base = ApiBaseUrl();
		if (Base.RemoveFromStart(TEXT("https://")))
		{
			return TEXT("wss://") + Base;
		}
		if (Base.RemoveFromStart(TEXT("http://")))
		{
			return TEXT("ws://") + Base;
		}
		return Base;
	}

	FString GameAddress() const { return FString::Printf(TEXT("%s:%d"), *ServerHost, GamePort); }
};
