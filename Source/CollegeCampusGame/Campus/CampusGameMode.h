#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CampusGameMode.generated.h"

/** Spawns the lightweight ACampusCharacter with a plain player controller. */
UCLASS()
class ACampusGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACampusGameMode();
};
