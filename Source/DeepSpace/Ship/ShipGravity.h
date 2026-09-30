#pragma once

#include "CoreMinimal.h"
#include "Universe/UniversePosition.h"

/**
 * One body's pull, as the flight law needs it (landing decision 4): where it
 * is, its GM, and its radius. Built once a frame by UShipSubsystem from the
 * system here, beside the floor spheres; none in transit.
 */
struct DEEPSPACE_API FGravityWell
{
    FUniversePosition Centre;

    /** GM, cm^3/s^2: FSkyBody::GravParam. */
    double Mu = 0.0;

    /** Mean radius, cm. Inside it the pull falls linearly to nothing at the
     *  centre, a uniform sphere, so a point the ground's hard stop has not
     *  yet lifted never divides by nearly zero. */
    double RadiusCm = 0.0;
};

namespace ShipFlight
{
    /** One Earth gravity, cm/s^2 (the standard, 9.80665 m/s^2): what "g" means
     *  wherever this slice says "per g" -- the hold's watts, the climb top. */
    inline constexpr double StandardGravityCmS2 = 980.665;

    /**
     * The pull at Position, cm/s^2, universe axes: the inverse-square sum over
     * every well (ruling 4 -- the star and every world). Every separation goes
     * through FUniversePosition's operator-, so it keeps its digits at any
     * range. Pure. The flight state reports it and never integrates it: the
     * boosters hold every lever against it (decision 4).
     */
    DEEPSPACE_API FVector GravityAt(TConstArrayView<FGravityWell> Wells, const FUniversePosition& Position);
}
