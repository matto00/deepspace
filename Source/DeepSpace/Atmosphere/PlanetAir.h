#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Universe/StarSystem.h"

/**
 * A world's air as the optics take it, from procgen's facts: the one
 * adapter across the seam, as FSkySystem::FromSystem is for everything else
 * the sky draws. Pure.
 */
namespace PlanetAir
{
    /** The world's air as an FAirSpec: its radius, and -- if it has air --
     *  its gas's scale height and Rayleigh and ozone depths, and its mix's
     *  aerosol, all from AirFacts. Airless (HasAir() false) for EAirMix::None
     *  or no pressure. */
    DEEPSPACE_API FAirSpec SpecOf(const FPlanet& Planet);

    /** The noon zenith from the ground (AtmosphereLaw::NoonSun: straight up,
     *  the sun 45 degrees high) under a star of this temperature, through
     *  the shipped law: linear sRGB in the pi convention, for the star at
     *  unit luminance. Zero for an airless world. The corpus's
     *  sky_zenith_rgb. */
    DEEPSPACE_API FVector3d NoonZenith(const FPlanet& Planet, double StarTemperatureK);

    /** HSV saturation, 1 - min / max, with negative channels taken as none;
     *  0 for black. The corpus's sky_zenith_saturation. */
    DEEPSPACE_API double Saturation(const FVector3d& Colour);
}
