#include "Campus/CampusGameInstance.h"

#include "Campus/CampusSave.h"
#include "Engine/Engine.h"

const TCHAR* UCampusGameInstance::LobbyMap = TEXT("/Game/Maps/L_Lobby");
const TCHAR* UCampusGameInstance::MatchMap = TEXT("/Game/Maps/L_Match");

void UCampusGameInstance::Init()
{
	Super::Init();
	GEngine->OnNetworkFailure().AddUObject(this, &UCampusGameInstance::HandleNetworkFailure);
	GEngine->OnTravelFailure().AddUObject(this, &UCampusGameInstance::HandleTravelFailure);
}

UCampusSave* UCampusGameInstance::GetProfile()
{
	if (!Profile)
	{
		Profile = UCampusSave::LoadOrCreate();
	}
	return Profile;
}

void UCampusGameInstance::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	PendingMessage = FailureType == ENetworkFailure::ConnectionLost
		? TEXT("Lost connection to the room.")
		: TEXT("Could not reach the server. Same Wi-Fi? Server running?");
}

void UCampusGameInstance::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
{
	PendingMessage = TEXT("Could not join the room.");
}
