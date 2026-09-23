#pragma once

#include "CoreMinimal.h"

/**
 * Pure movement rules and tuning, with no Unreal types beyond maths. Kept out
 * of the character so they can be unit-tested without a world, the same
 * reason FShipPowerState is separate from UShipSubsystem.
 */
struct DEEPSPACE_API FMovementRules
{
    /** Ground speeds, cm/s. */
    static constexpr float WalkSpeed = 300.0f;
    static constexpr float SprintSpeed = 600.0f;
    static constexpr float CrouchSpeed = 150.0f;

    /**
     * Crouched capsule half-height, cm: 144 cm tall. Sized so the retargeted
     * crouch-walk clip ("Crouched Walking", an upright crouch), which carries
     * the camera to 138 cm, keeps it inside the capsule with the 5 cm margin
     * Tools/check_anim_heights.py requires. Must stay under the ship's crouch
     * clearance in Tools/movement_contract.json;
     * DeepSpace.Player.MovementContract checks.
     */
    static constexpr float CrouchedHalfHeight = 72.0f;

    /**
     * Whether the player may sprint. MoveInput is IA_Move's value: X strafes
     * right, Y moves forward. Sprinting needs the forward component to be
     * positive and to dominate, so strafing and backing up stay at walking
     * pace, and crouching always does.
     */
    static bool CanSprint(bool bIsCrouched, const FVector2D& MoveInput);
};
