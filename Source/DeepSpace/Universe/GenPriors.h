#pragma once

#include "CoreMinimal.h"

/**
 * Every tunable number the generator draws with: the priors (procgen
 * decision 14). The defaults here are the only code defaults there are;
 * UProcGenPriorsConfig initialises its ini mirror from FGenPriors{} and copies
 * back with ToPriors(), so a tune is an edit to Config/DefaultGame.ini and
 * ds.Universe.ReloadPriors in a running session, never a rebuild. The ini is
 * the one source of the numbers played: these are only what a line missing
 * from it falls back to, and a tune never needs writing back here.
 *
 * A plain struct, not a USTRUCT: generated reflection code has no business in
 * the pure layer. Natural units throughout (procgen decision 7).
 *
 * Changing any of these re-rolls the universe, and nothing catches it -- on
 * purpose, until something persists. The known-value tests use means written
 * into the test, so a prior tune never turns them red.
 */
struct FGenPriors
{
    // -- the star ---------------------------------------------------------------

    /**
     * Main-sequence class weights, M K G F A B: the solar neighbourhood as it
     * is. Three suns in four are dim red dwarfs, which is what *most worlds are
     * empty* looks like from the inside (the developer's ruling, 2026-09-25:
     * honest weights). Relative, so they need not sum to one.
     */
    double ClassWeightM = 0.76;
    double ClassWeightK = 0.12;
    double ClassWeightG = 0.076;
    double ClassWeightF = 0.030;
    double ClassWeightA = 0.006;
    double ClassWeightB = 0.0013;

    /** Where in its class a star sits: Beta(A, B), skewed low because the
     *  initial mass function is steep -- small stars outnumber large ones
     *  inside every class as well as across them. */
    double ClassBandBetaA = 1.2;
    double ClassBandBetaB = 2.0;

    // -- planets ----------------------------------------------------------------

    /** Poisson mean of the planet count. A count of things in an interval is
     *  Poisson's whole job. */
    double PlanetCountMean = 4.0;

    /** Innermost orbit: log-normal with median InnermostMedianFactor * sqrt(L)
     *  AU, so it follows the star's warmth rather than its size. */
    double InnermostMedianFactor = 0.2;
    double InnermostSigma = 0.35;

    /** Spacing between neighbours, in mutual Hill radii: log-normal around
     *  twenty, where observed compact multi-planet systems sit. The floor of
     *  ten and the ceiling of sixty are guarantees, not priors. */
    double HillSpacingMedian = 20.0;
    double HillSpacingSigma = 0.4;

    /** Rocky masses, Earth masses: log-normal, because mass is a magnitude
     *  built from many multiplied factors. Beyond the frost line there is more
     *  solid material to build from, so the median is higher. */
    double RockyMassMedianInner = 1.0;
    double RockyMassMedianOuter = 2.0;
    double RockyMassSigma = 0.9;

    /** Beyond the frost line a giant forms with Chance(min(GiantChanceMax,
     *  GiantChancePerSolarMass * M_star)): giants are rarer round small stars,
     *  which have less disc to build them from. */
    double GiantChanceMax = 0.5;
    double GiantChancePerSolarMass = 0.3;
    double GiantMassMedian = 100.0;
    double GiantMassSigma = 1.0;

    /** Of temperate worlds, the fraction that are oceans rather than dry land.
     *  The only place a kind is drawn: everywhere else physics decides it. */
    double OceanFraction = 0.4;

    /** Of temperate worlds, the chance anybody lives there. About one system
     *  in sixteen, which is rare enough to be an event and common enough that
     *  a playtest meets one (the developer's ruling, 2026-09-25). */
    double InhabitedChance = 0.08;

    /** Settlement size, people: log-normal and truncated, because an
     *  untruncated log-normal eventually founds a colony of forty billion. */
    double PopulationMedian = 5.0e4;
    double PopulationSigma = 2.0;
    double PopulationMin = 200.0;
    double PopulationMax = 5.0e8;

    // -- the ground (landing decision 3) -----------------------------------------

    /** What a crust can hold up goes as 1/g: Earth's 8.8 km at 1 g, Mars's
     *  22 km at 0.38 g. The ceiling of a world's relief is Strength / g, km,
     *  g in Earth g; weather takes a factor off a terrestrial world's. */
    double ReliefStrengthRockKm = 9.0;
    double ReliefStrengthIceKm = 9.0;
    double ReliefTerrestrialFactor = 0.7;

    /** How much of its ceiling a world reaches, a bounded proportion, which
     *  is Beta's whole job. Barren and ice skew high -- old, unrelaxed crust,
     *  Beta(5, 2), mean 0.71 -- and terrestrial lower, Beta(3, 2), mean 0.6. */
    double ReliefBetaA = 5.0;
    double ReliefBetaB = 2.0;
    double ReliefTerrestrialBetaA = 3.0;
    double ReliefTerrestrialBetaB = 2.0;

    // -- the air (atmospheres decision 2) ----------------------------------------

    /** A temperate world's surface pressure, bar: log-normal, because a
     *  pressure is a product of many factors -- outgassing, loss,
     *  sequestration. Bounded above in code, not here: by the world's
     *  retention of its gas and the nadir-haze guarantee
     *  (AirFacts::PressureCeilingBar), bent under that ceiling rather than
     *  clipped, so no pile of worlds stacks at the cap. */
    double AirPressureMedianTerrestrialBar = 0.8;
    double AirPressureMedianOceanBar = 1.0;
    double AirPressureSigma = 0.9;

    /** Which gas a temperate world holds, before retention: relative
     *  weights, each multiplied by how well the world holds that gas, so a
     *  world that cannot keep hydrogen never draws it and no draw is thrown
     *  away (ADR 0008). They set the colour of much of the universe
     *  (sign-off item 19): the corpus's sky_zenith_rgb is what they are
     *  judged by. */
    double AirMixWeightNitrogenOxygen = 0.55;
    double AirMixWeightCarbonDioxide = 0.40;
    double AirMixWeightHydrogenHelium = 0.05;

    // -- the galaxy -------------------------------------------------------------

    /** Poisson mean of systems per 2^62 cm sector: the solar neighbourhood,
     *  nearest neighbours about 3.4 ly apart and about eighteen systems within
     *  ten. */
    double SystemsPerSector = 0.5;
};

/**
 * Every field of FGenPriors, once. UProcGenPriorsConfig copies through this
 * list in both directions and the priors test walks it, so a prior added to
 * the struct and not to the list fails to compile (the static_assert below)
 * rather than silently never reaching the ini.
 */
#define DS_GEN_PRIORS(X) \
    X(ClassWeightM) X(ClassWeightK) X(ClassWeightG) X(ClassWeightF) X(ClassWeightA) X(ClassWeightB) \
    X(ClassBandBetaA) X(ClassBandBetaB) \
    X(PlanetCountMean) \
    X(InnermostMedianFactor) X(InnermostSigma) \
    X(HillSpacingMedian) X(HillSpacingSigma) \
    X(RockyMassMedianInner) X(RockyMassMedianOuter) X(RockyMassSigma) \
    X(GiantChanceMax) X(GiantChancePerSolarMass) X(GiantMassMedian) X(GiantMassSigma) \
    X(OceanFraction) \
    X(InhabitedChance) \
    X(PopulationMedian) X(PopulationSigma) X(PopulationMin) X(PopulationMax) \
    X(ReliefStrengthRockKm) X(ReliefStrengthIceKm) X(ReliefTerrestrialFactor) \
    X(ReliefBetaA) X(ReliefBetaB) X(ReliefTerrestrialBetaA) X(ReliefTerrestrialBetaB) \
    X(AirPressureMedianTerrestrialBar) X(AirPressureMedianOceanBar) X(AirPressureSigma) \
    X(AirMixWeightNitrogenOxygen) X(AirMixWeightCarbonDioxide) X(AirMixWeightHydrogenHelium) \
    X(SystemsPerSector)

#define DS_GEN_PRIORS_COUNT_ONE(Name) + 1
inline constexpr int32 NumGenPriors = 0 DS_GEN_PRIORS(DS_GEN_PRIORS_COUNT_ONE);
#undef DS_GEN_PRIORS_COUNT_ONE

static_assert(sizeof(FGenPriors) == NumGenPriors * sizeof(double),
    "FGenPriors has a field DS_GEN_PRIORS does not list: add it there, to "
    "UProcGenPriorsConfig and to Config/DefaultGame.ini, or it can never be tuned");

/**
 * The numbers an ini edit must not be able to move, because an invariant
 * rests on each (procgen decision 14). The generator reads them from here and
 * the tests assert against the same names, so the property and the guard
 * cannot quietly disagree.
 */
namespace GenGuarantees
{
    /** Poisson caps. The planet cap bites once in five thousand systems at the
     *  default mean; the sector cap is far out in the tail at 0.5. */
    inline constexpr int32 MaxPlanets = 12;
    inline constexpr int32 MaxSystemsPerSector = 8;

    /** Below about ten mutual Hill radii, systems of small planets go unstable
     *  within the age of a star. Ten is the floor, not a tunable. */
    inline constexpr double MinHillSpacing = 10.0;
    inline constexpr double MaxHillSpacing = 60.0;

    /** The spacing range is rescaled to [10, min(60, HillSpacingScale / h)],
     *  keeping K h well under the 2 at which the orbit solution fails. The mass
     *  cap guarantees HillSpacingScale / h >= 11.9, so the floor survives. */
    inline constexpr double HillSpacingScale = 1.5;

    /** No planet is drawn inside this many stellar radii of its star. */
    inline constexpr double InnermostMinStellarRadii = 5.0;

    /** The heaviest planet, as a fraction of its star: about three Jupiters
     *  per solar mass. Above it is a brown-dwarf companion, which is a
     *  binary, which is a fake this generator refuses rather than draws. */
    inline constexpr double MaxPlanetToStarMass = 3.0e-3;

    /** Mass bounds, Earth masses. A rocky world stops short of the giant
     *  threshold, so a drawn mass can never contradict its derived kind. */
    inline constexpr double RockyMassMin = 0.05;
    inline constexpr double RockyMassMax = 14.9;
    inline constexpr double GiantMassMin = 15.0;
    inline constexpr double GiantMassMax = 3000.0;

    /** Kind thresholds. Kind is derived, never drawn, so an ocean at 800 K
     *  cannot be expressed (procgen decision 5). */
    inline constexpr double GasGiantMinEarthMasses = 15.0;
    inline constexpr double BarrenBelowEarthMasses = 0.3;
    inline constexpr double BarrenAboveK = 320.0;
    inline constexpr double IceBelowK = 180.0;

    /** No world's relief rises higher than this, km, whatever the ini says
     *  (landing ruling 6), nor above this fraction of its radius. The drive's
     *  floor stands 10 km above the highest peak (landing decision 10), so
     *  this is what that floor's height rests on. */
    inline constexpr double MaxReliefKm = 10.0;
    inline constexpr double MaxReliefRadiusFraction = 0.005;

    /** The top of an air is far hotter than its surface -- Earth's
     *  thermosphere runs near 1,000 K over a 255 K equilibrium -- and it is
     *  the top that loses gas: the exobase is taken at this many times
     *  EquilibriumK (atmospheres decision 2). It decides which worlds may
     *  hold hydrogen. */
    inline constexpr double ExobaseFactor = 4.0;

    /** Escape speed over the molecules' root-mean-square speed at the
     *  exobase: at or above RetainedAbove a gas is held over a star's age;
     *  at or below LostBelow it is gone; a smoothstep between. */
    inline constexpr double RetainedAbove = 6.0;
    inline constexpr double LostBelow = 4.0;

    /** No air's total extinction straight down at 450 nm (Rayleigh plus
     *  aerosol, not absorption) exceeds this (atmospheres ruling 8, decision
     *  4): every airy world's surface stays legible from orbit. Bounds the
     *  fact, as a per-world pressure ceiling, never the rendering.
     *  0.32, not decision 4's first 0.5: atmosphere plan ruling 2 (at 0.5 every
     *  mix at its ceiling kept 0.34 of its contrast). */
    inline constexpr double MaxNadirTau450 = 0.32;
}

/**
 * The domain each prior must lie in for the sampler it feeds, and the check
 * that stands between an ini line and the generator (procgen decision 14).
 * Before the priors were ini lines, GenStream's check()s were programmer
 * asserts; now a typo would reach them, and a check() is a crash. So a set of
 * priors is refused whole, here, before any of it is drawn with.
 *
 * Bounds are what the samplers and the arithmetic after them need, not what
 * is plausible: a tune may make a strange universe, never a broken one.
 */
namespace GenPriorDomain
{
    /** Below about a tenth, Beta(a, b) is two spikes at 0 and 1, and both of
     *  its gamma draws underflow to zero together often enough to give 0/0 --
     *  a star with no mass. At a tenth that needs a uniform draw under 1e-30,
     *  which never comes. */
    inline constexpr double MinBetaShape = 0.1;

    /** The innermost orbit's median, AU per sqrt(L). The default is 0.2; at
     *  a hundred a sunlike star's first world is three times further out than
     *  Neptune. The ceiling is not taste: it is what keeps eleven Hill-spaced
     *  orbits beyond it finite numbers. */
    inline constexpr double MaxInnermostMedianFactor = 100.0;

    /** Past this a world's pressure spans a factor of twelve either way at
     *  one sigma, and the ceiling is doing all the work the prior should. */
    inline constexpr double MaxAirPressureSigma = 2.5;

    /** Every way these priors fall outside the domain, one line each, naming
     *  the ini line, its value and what it must be. Empty when the generator
     *  can take them. */
    TArray<FString> Refusals(const FGenPriors& Priors);
}
