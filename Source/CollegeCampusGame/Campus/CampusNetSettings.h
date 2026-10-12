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

	FString ApiBaseUrl() const
	{
		return ApiBaseOverride.IsEmpty() ? FString::Printf(TEXT("http://%s:%d/v1"), *ServerHost, ApiPort) : ApiBaseOverride;
	}
	FString GameAddress() const { return FString::Printf(TEXT("%s:%d"), *ServerHost, GamePort); }
};
