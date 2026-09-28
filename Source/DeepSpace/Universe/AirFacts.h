#pragma once

#include "CoreMinimal.h"

/**
 * What a world's air is, as procgen's facts (atmospheres decision 2): the
 * two drawn facts -- the mix and the surface pressure -- live on FPlanet;
 * everything else is derived here, pure, from them and the world's mass,
 * radius and equilibrium temperature. Looks are the sky's
 * (PlanetAir::SpecOf turns these into optics); nothing here knows a colour.
 */

/** Which air a world holds. None for barren and ice worlds (ruled
 *  airless); one of the three for terrestrial and ocean worlds, drawn; and
 *  hydrogen and helium for every giant. A mix names a whole air: its
 *  aerosol and absorber follow it (decision 5). */
enum class EAirMix : uint8 { None, NitrogenOxygen, CarbonDioxide, HydrogenHelium };

/**
 * The air's guarantees (decisions 2 and 4): numbers no ini line may move,
 * because an invariant rests on each. This reopens GenPriors.h's namespace
 * of the same name.
 */
namespace GenGuarantees
{
    /** The top of an air is far hotter than its surface -- Earth's
     *  thermosphere runs near 1,000 K over a 255 K equilibrium -- and it is
     *  the top that loses gas: the exobase is taken at this many times
     *  EquilibriumK. It decides which worlds may hold hydrogen. */
    inline constexpr double ExobaseFactor = 4.0;

    /** Escape speed over the molecules' root-mean-square speed at the
     *  exobase: at or above RetainedAbove a gas is held over a star's age;
     *  at or below LostBelow it is gone; a smoothstep between. */
    inline constexpr double RetainedAbove = 6.0;
    inline constexpr double LostBelow = 4.0;

    /** No air's total extinction straight down at 450 nm (Rayleigh plus
     *  aerosol, not absorption) exceeds this (ruling 8): every airy world's
     *  surface stays legible from orbit. Bounds the fact, as a per-world
     *  pressure ceiling, never the rendering. */
    inline constexpr double MaxNadirTau450 = 0.5;
}

/** One mix's constants: what it is made of, and the aerosol and absorber
 *  that come with it (decision 5). Every aerosol number is a first estimate
 *  for the reference, the corpus and the playtest to move. */
struct FAirMixFacts
{
    double MeanMolecularWeight = 0.0;

    /** Rayleigh cross-section per molecule relative to Earth's air. */
    double RayleighPerAir = 0.0;

    /** Aerosol extinction straight down at 550 nm, per bar at 1 g; like the
     *  gas, it goes with the column, P / g. */
    double AerosolTau550PerBar = 0.0;
    double AerosolScaleHeightKm = 0.0;
    double AerosolAngstrom = 1.0;
    double AerosolAsymmetry = 0.0;
    double AerosolAlbedo450 = 1.0;
    double AerosolAlbedo650 = 1.0;

    /** The ozone-like absorber at its Chappuis peak, 600 nm, per bar at 1 g.
     *  0 for a mix with none. */
    double OzoneTau600PerBar = 0.0;
};

namespace AirFacts
{
    inline constexpr double BoltzmannJPerK = 1.380649e-23;
    inline constexpr double AtomicMassKg = 1.66053906660e-27;
    inline constexpr double StandardGravityMS2 = 9.80665;

    /** Earth's air: its Rayleigh depth straight down at 550 nm per bar, and
     *  its mean molecular weight -- what every other mix is measured
     *  against. */
    inline constexpr double EarthRayleighTau550PerBar = 0.097;
    inline constexpr double EarthAirMolecularWeight = 28.97;

    /** The mixes a world can draw: EAirMix's after None, in order. */
    inline constexpr int32 NumMixes = 3;

    DEEPSPACE_API const FAirMixFacts& Facts(EAirMix Mix);

    /** The corpus's word for a mix. */
    DEEPSPACE_API const TCHAR* Name(EAirMix Mix);

    /** k T / (mu m_u g), km, with T the equilibrium temperature (no
     *  greenhouse: *Deliberate fakes*). 0 for None or a non-positive input. */
    DEEPSPACE_API double ScaleHeightKm(EAirMix Mix, double TemperatureK, double GravityEarth);

    /** Molecules above the surface relative to Earth's:
     *  (P / 1 bar) (1 / g) (28.97 / mu). */
    DEEPSPACE_API double ColumnRelativeToEarth(EAirMix Mix, double PressureBar, double GravityEarth);

    /** The gas's Rayleigh depth straight down at 550 nm: Earth's per bar,
     *  times the column, times the mix's cross-section. */
    DEEPSPACE_API double RayleighTau550(EAirMix Mix, double PressureBar, double GravityEarth);

    /** The aerosol's extinction straight down at 550 nm: per bar at 1 g,
     *  times P / g. */
    DEEPSPACE_API double AerosolTau550(EAirMix Mix, double PressureBar, double GravityEarth);

    /** The absorber's depth straight down at 600 nm: per bar at 1 g, times
     *  P / g. */
    DEEPSPACE_API double OzoneTau600(EAirMix Mix, double PressureBar, double GravityEarth);

    /** Rayleigh plus aerosol extinction straight down at 450 nm, the bluest
     *  channel's centre and the one that hazes first: what MaxNadirTau450
     *  bounds. */
    DEEPSPACE_API double NadirTau450(EAirMix Mix, double PressureBar, double GravityEarth);

    /** Escape speed over the molecules' root-mean-square speed at the
     *  exobase (ExobaseFactor x EquilibriumK); 0 for None. */
    DEEPSPACE_API double JeansLambda(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK);

    /** 0 at or below LostBelow, 1 at or above RetainedAbove, a smoothstep
     *  between. */
    DEEPSPACE_API double Retention(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK);

    /** The most of this mix a world of this gravity may hold, bar: where
     *  NadirTau450 reaches MaxNadirTau450, times the world's retention of
     *  the mix (a world that barely holds its gas holds less of it). */
    DEEPSPACE_API double PressureCeilingBar(EAirMix Mix, double GravityEarth, double RetentionOfMix);

    /** A drawn pressure under a ceiling, bent rather than clipped:
     *  1 / (Drawn^-4 + Ceiling^-4)^(1/4), so no pile of worlds stacks at the
     *  cap. 0 when either is not positive. */
    DEEPSPACE_API double SmoothCeiling(double DrawnBar, double CeilingBar);

    /** A giant's "surface": where the air above it reaches MaxNadirTau450,
     *  bar -- its hydrogen and helium's ceiling at full retention. */
    DEEPSPACE_API double GiantDiscPressureBar(double GravityEarth);
}
