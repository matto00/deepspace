#pragma once

#include "CoreMinimal.h"

/**
 * The natural units the generator thinks in, and their size in centimetres.
 *
 * The plan of a system is written in AU, solar and Earth masses and radii,
 * and Kelvin, because that is what every source the priors come from uses,
 * and 1.495978707e13 is unreadable in a test, a corpus report or a log
 * (procgen decision 7). Centimetres appear only at the FUniversePosition
 * boundary, and every conversion goes through these.
 *
 * IAU nominal values where the IAU has one: these are definitions, not
 * measurements, so they will not move under us.
 */
namespace UniverseUnits
{
    inline constexpr double CmPerKm = 1.0e5;

    /** The astronomical unit, exact by IAU 2012 definition. */
    inline constexpr double CmPerAU = 1.495978707e13;

    /** The Julian light year: c times 365.25 days of 86,400 s. */
    inline constexpr double CmPerLightYear = 9.4607304725808e17;

    /** IAU 2015 nominal solar radius. */
    inline constexpr double CmPerSolarRadius = 6.957e10;

    /** IAU 2015 nominal equatorial Earth radius. */
    inline constexpr double CmPerEarthRadius = 6.3781e8;

    /** Earth's mass in solar masses (5.9722e24 kg over 1.98847e30 kg), for
     *  the Hill radius, where a planet's mass meets its star's. */
    inline constexpr double EarthMassSolar = 3.0034147e-6;

    /** IAU 2015 nominal solar effective temperature. */
    inline constexpr double SolarTemperatureK = 5772.0;

    /** IAU 2015 nominal solar mass parameter GM, cm^3/s^2 (1.3271244e20
     *  m^3/s^2). A definition, like the radii above: the product is known far
     *  better than G or the mass alone (landing decision 4). */
    inline constexpr double GMSunCm3PerS2 = 1.3271244e26;

    /** IAU 2015 nominal terrestrial mass parameter GM, cm^3/s^2 (3.986004e14
     *  m^3/s^2). Its ratio to the Sun's agrees with EarthMassSolar to 2.5e-5.
     *  Over CmPerEarthRadius squared it is 979.840 cm/s^2. */
    inline constexpr double GMEarthCm3PerS2 = 3.986004e20;
}
