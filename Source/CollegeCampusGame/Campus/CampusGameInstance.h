// Survives level travel: remembers that the player is past the splash, holds the profile,
// and turns network failures into a message the lobby can show.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "CampusGameInstance.generated.h"

class UCampusSave;

UCLASS()
class UCampusGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	UCampusSave* GetProfile();

	/** Lobby map, used for "back to lobby" and as the fallback after a failed join. */
	static const TCHAR* LobbyMap;
	static const TCHAR* MatchMap;

	bool bSignedIn = false;
	bool bEventSeen = false;

	/** Shown once by the lobby, then cleared. */
	FString PendingMessage;

private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	UPROPERTY(Transient)
	TObjectPtr<UCampusSave> Profile;
};
