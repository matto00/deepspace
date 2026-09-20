#include "Core/DeepSpaceGameMode.h"

#include "Player/DeepSpaceCharacter.h"

ADeepSpaceGameMode::ADeepSpaceGameMode()
{
    DefaultPawnClass = ADeepSpaceCharacter::StaticClass();
}
