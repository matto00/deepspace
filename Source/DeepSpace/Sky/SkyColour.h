#pragma once

#include "CoreMinimal.h"

/**
 * The one blackbody in the project (plan conflict 6). A star's colour is a
 * presentation concern, so it lives on the sky's side of the seam and
 * procgen's subsystem has no StarColour of its own.
 */
namespace SkyColour
{
    /**
     * The colour of a blackbody at this temperature, in linear sRGB, scaled
     * so its brightest channel is 1: a chromaticity, never a brightness. How
     * bright a star is comes from its luminosity and distance, and mixing the
     * two here would make a hot star brighter twice.
     *
     * Hot stars are blue-white and cool ones orange-red, against a D65 white,
     * so a Sun-like star reads faintly warm -- as the Sun does in a photograph
     * white-balanced for daylight. Clamped to the 1,000-15,000 K the
     * approximation covers; nothing drawn here is cooler, and above 15,000 K
     * the colour has stopped changing to the eye.
     */
    DEEPSPACE_API FLinearColor Blackbody(double TemperatureK);
}
