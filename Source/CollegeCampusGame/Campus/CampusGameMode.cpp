#include "Campus/CampusGameMode.h"

#include "Campus/CampusCharacter.h"
#include "GameFramework/PlayerController.h"

ACampusGameMode::ACampusGameMode()
{
	DefaultPawnClass = ACampusCharacter::StaticClass();
	PlayerControllerClass = APlayerController::StaticClass();
}
