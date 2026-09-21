#pragma once

#include "CoreMinimal.h"
#include "Posture.generated.h"

/**
 * What the player's body is doing, decided in C++ by the character. The
 * animation blueprint only reads it -- its single "Blend Poses by EPosture"
 * node picks a pose per value -- so the decision never lives in Content/.
 */
UENUM(BlueprintType)
enum class EPosture : uint8
{
    Standing,
    Crouched,
    Seated,
    Falling,
};
