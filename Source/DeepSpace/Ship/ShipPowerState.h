#pragma once

#include "CoreMinimal.h"

/**
 * Pure power arithmetic for a ship.
 *
 * Deliberately not a UObject and not tied to a UWorld, so it can be unit-tested
 * headlessly. UShipSubsystem owns an instance of this and exposes it to
 * gameplay. This is the layer that will accumulate the most complexity as ship
 * systems grow, which is exactly why it is the layer that is trivially testable.
 */
struct DEEPSPACE_API FShipPowerState
{
public:
    void SetReactorOutput(float Watts);
    float GetReactorOutput() const;

    /** Returns false if ModuleId already draws power. */
    bool AddDraw(FName ModuleId, float Watts);

    /** Returns false if ModuleId was not drawing power. */
    bool RemoveDraw(FName ModuleId);

    float GetTotalDraw() const;

    /** Reactor output minus total draw. Negative when overloaded. */
    float GetHeadroom() const;

    bool IsOverloaded() const;

private:
    float ReactorOutput = 0.0f;
    TMap<FName, float> Draws;
};
