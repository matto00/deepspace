#pragma once

#include "CoreMinimal.h"

/**
 * The background stars: a galaxy seen from inside it, not an even spiral
 * (sky decision 4). The real sky clumps, and the clumping is the galaxy --
 * the one thing in the view that says there is a structure far larger than
 * anywhere the ship will ever go.
 *
 * Drawn on procgen's FGenStream and nothing else, so the sky has no second
 * RNG and no second seeding scheme (ADR 0006). The galactic plane is the
 * universe XY plane and its centre lies along +X, until procgen has a galaxy
 * with an orientation of its own.
 */
struct DEEPSPACE_API FSkyStar
{
    /** Unit, universe axes. */
    FVector Direction = FVector::ForwardVector;

    /** 1 = the faintest drawn, up to SkyStarfield::MaxFlux. */
    double Flux = 1.0;

    double TemperatureK = 5000.0;
};

namespace SkyStarfield
{
    /** The naked-eye range, about 6.5 magnitudes: the brightest background
     *  star is this many times the faintest. */
    inline constexpr double MaxFlux = 400.0;

    inline constexpr double MinTemperatureK = 2800.0;
    inline constexpr double MaxTemperatureK = 15000.0;

    /**
     * Count stars from Seed. Star i depends on Seed and i alone -- each of
     * its quantities has its own stream -- so asking for more stars appends
     * to the sky rather than reshuffling it.
     */
    DEEPSPACE_API TArray<FSkyStar> Generate(uint64 Seed, int32 Count);
}
