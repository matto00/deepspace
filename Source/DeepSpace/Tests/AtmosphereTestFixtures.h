#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AtmosphereReference.h"

/**
 * Airs written out by hand for the optics tests (plan conflict 4): test
 * data, not a generator, and allowed to lag procgen's AirFacts, which
 * derives the same numbers from a world's facts. Every air is on an
 * Earth-radius world at 1 g. The mixes are the atmospheres spec's decisions
 * 4 and 5 at 1 g: the gas 0.097 per bar at 550 nm times its cross-section
 * relative to air and its column (28.97 / mu); aerosol and ozone per bar.
 * The pressures are the two extremes the law is held at: 0.05 bar, and each
 * mix's ceiling under MaxNadirTau450, where tau at 450 nm is 0.5 straight
 * down. Nothing outside Tests/ may include this.
 */
namespace AtmosphereTestFixtures
{
    inline constexpr double EarthRadiusCm = 6.3781e8;
    inline constexpr double HomeStarK = 2566.0;   // Baemsekai
    inline constexpr double SunK = 5772.0;
    inline constexpr double LowBar = 0.05;
    inline constexpr double NitrogenOxygenCeilingBar = 1.8013;
    inline constexpr double CarbonDioxideCeilingBar = 1.1710;
    inline constexpr double HydrogenHeliumCeilingBar = 0.8968;

    inline FAirSpec Airless()
    {
        FAirSpec Air;
        Air.RadiusCm = EarthRadiusCm;
        return Air;
    }

    /** Earth's mix at 255 K and 1 g: the gas's scale height 7.463 km. */
    inline FAirSpec NitrogenOxygen(double Bar)
    {
        FAirSpec Air;
        Air.RadiusCm = EarthRadiusCm;
        Air.GasScaleHeightCm = 7.463e5;
        Air.GasTau550 = 0.097 * Bar;
        Air.OzoneTau600 = 0.0415 * Bar;
        Air.AerosolScaleHeightCm = 1.2e5;
        Air.AerosolTau550 = 0.05 * Bar;
        Air.AerosolAngstrom = 1.0;
        Air.AerosolAsymmetry = 0.76;
        Air.AerosolAlbedo450 = 0.95;
        Air.AerosolAlbedo650 = 0.95;
        return Air;
    }

    /** Carbon dioxide at 255 K and 1 g: 4.914 km; iron-oxide dust that eats blue. */
    inline FAirSpec CarbonDioxide(double Bar)
    {
        FAirSpec Air;
        Air.RadiusCm = EarthRadiusCm;
        Air.GasScaleHeightCm = 4.914e5;
        Air.GasTau550 = 0.15328 * Bar;
        Air.AerosolScaleHeightCm = 2.0e5;
        Air.AerosolTau550 = 0.08 * Bar;
        Air.AerosolAngstrom = 0.3;
        Air.AerosolAsymmetry = 0.7;
        Air.AerosolAlbedo450 = 0.85;
        Air.AerosolAlbedo650 = 0.95;
        return Air;
    }

    /** Hydrogen and helium on a cold world at 150 K and 1 g: 55.3 km, X = 115. */
    inline FAirSpec HydrogenHelium(double Bar)
    {
        FAirSpec Air;
        Air.RadiusCm = EarthRadiusCm;
        Air.GasScaleHeightCm = 5.53e6;
        Air.GasTau550 = 0.24436 * Bar;
        Air.AerosolScaleHeightCm = 1.0e5;
        Air.AerosolTau550 = 0.01 * Bar;
        Air.AerosolAngstrom = 1.0;
        Air.AerosolAsymmetry = 0.7;
        Air.AerosolAlbedo450 = 0.99;
        Air.AerosolAlbedo650 = 0.99;
        return Air;
    }

    inline FAirSpec EarthAir()
    {
        return NitrogenOxygen(1.0);
    }

    /** A Jupiter at its disc: 11 Earth radii, 2.63 g, 110 K, the gas's
     *  scale height 15.4 km, 2.36 bar of hydrogen and helium above the level
     *  where tau at 450 nm reaches 0.5. */
    inline FAirSpec Giant()
    {
        FAirSpec Air;
        Air.RadiusCm = 11.0 * EarthRadiusCm;
        Air.GasScaleHeightCm = 1.542e6;
        Air.GasTau550 = 0.2192;
        Air.AerosolScaleHeightCm = 1.0e5;
        Air.AerosolTau550 = 0.00896;
        Air.AerosolAngstrom = 1.0;
        Air.AerosolAsymmetry = 0.7;
        Air.AerosolAlbedo450 = 0.99;
        Air.AerosolAlbedo650 = 0.99;
        return Air;
    }

    struct FNamedAir
    {
        const TCHAR* Name = TEXT("");
        FAirSpec Air;
    };

    /** Every mix at both pressure extremes (decision 1's "each mix and
     *  pressure extreme"). */
    inline TArray<FNamedAir> Extremes()
    {
        return {
            {TEXT("N2/O2 at 0.05 bar"), NitrogenOxygen(LowBar)},
            {TEXT("N2/O2 at its ceiling"), NitrogenOxygen(NitrogenOxygenCeilingBar)},
            {TEXT("CO2 at 0.05 bar"), CarbonDioxide(LowBar)},
            {TEXT("CO2 at its ceiling"), CarbonDioxide(CarbonDioxideCeilingBar)},
            {TEXT("H2/He at 0.05 bar"), HydrogenHelium(LowBar)},
            {TEXT("H2/He at its ceiling"), HydrogenHelium(HydrogenHeliumCeilingBar)}};
    }
}
