#include "Player/MovementRules.h"

bool FMovementRules::CanSprint(bool bIsCrouched, const FVector2D& MoveInput)
{
    if (bIsCrouched)
    {
        return false;
    }
    // A small dead zone, so resting fingers on W and a strafe key read as
    // strafing rather than sprinting.
    constexpr float MinForward = 0.1f;
    return MoveInput.Y > MinForward && MoveInput.Y >= FMath::Abs(MoveInput.X);
}
