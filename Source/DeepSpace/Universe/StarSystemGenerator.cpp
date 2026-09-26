#include "Universe/StarSystemGenerator.h"

#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"
#include "Universe/SystemNames.h"
#include "Universe/UniverseUnits.h"

#include <cmath>

namespace
{
    /** Each main-sequence class's span of mass (solar masses) and surface
     *  temperature (K), coolest first. Mass and temperature are both placed
     *  log-wise across the span by the same proportion, so a star low in its
     *  class is low in both. */
    struct FClassBand
    {
        double MassMin, MassMax;
        double TemperatureMin, TemperatureMax;
    };

    constexpr FClassBand ClassBands[NumStarClasses] = {
        {0.08, 0.45, 2400.0, 3700.0},       // M
        {0.45, 0.80, 3700.0, 5200.0},       // K
        {0.80, 1.04, 5200.0, 6000.0},       // G
        {1.04, 1.40, 6000.0, 7500.0},       // F
        {1.40, 2.10, 7500.0, 10000.0},      // A
        {2.10, 16.0, 10000.0, 30000.0},     // B
    };

    double LogLerp(double Min, double Max, double Alpha)
    {
        return Min * std::pow(Max / Min, Alpha);
    }

    /** The piecewise main-sequence mass-luminosity relation. */
    double LuminosityOf(double MassSolar)
    {
        if (MassSolar < 0.43)
        {
            return 0.23 * std::pow(MassSolar, 2.3);
        }
        if (MassSolar < 2.0)
        {
            return std::pow(MassSolar, 4.0);
        }
        return 1.4 * std::pow(MassSolar, 3.5);
    }

    /** Planet mass for its region, Earth masses. Mass is a magnitude built
     *  from many multiplied factors -- disc mass, accretion time, migration --
     *  which is log-normal's definition. Every mass is truncated at the
     *  planet-to-star cap: above it the companion is a brown dwarf, which is
     *  a binary, which this generator refuses. The giant chance is part of the
     *  mass quantity and shares its stream. */
    double DrawMass(uint64 PlanetSeed, bool bBeyondFrostLine, const FStar& Star, const FGenPriors& Priors)
    {
        FGenStream Stream(GenSeed::Derive(PlanetSeed, GenSeed::Label("mass")));

        const double CapEarth = GenGuarantees::MaxPlanetToStarMass * Star.MassSolar / UniverseUnits::EarthMassSolar;
        const double RockyMax = FMath::Min(GenGuarantees::RockyMassMax, CapEarth);

        if (!bBeyondFrostLine)
        {
            return Stream.LogNormalBounded(Priors.RockyMassMedianInner, Priors.RockyMassSigma, GenGuarantees::RockyMassMin, RockyMax);
        }

        // Giants are rarer round small stars, which have less disc to build
        // one from.
        const double GiantChance = FMath::Min(Priors.GiantChanceMax, Priors.GiantChancePerSolarMass * Star.MassSolar);
        if (Stream.Chance(GiantChance) && CapEarth >= GenGuarantees::GiantMassMin)
        {
            return Stream.LogNormalBounded(Priors.GiantMassMedian, Priors.GiantMassSigma, GenGuarantees::GiantMassMin,
                FMath::Min(GenGuarantees::GiantMassMax, CapEarth));
        }
        return Stream.LogNormalBounded(Priors.RockyMassMedianOuter, Priors.RockyMassSigma, GenGuarantees::RockyMassMin, RockyMax);
    }

    /** Kind is decided by mass and temperature, and drawn only where both
     *  allow either of two answers (procgen decision 5). */
    bool IsTemperate(double MassEarth, double EquilibriumK)
    {
        return MassEarth < GenGuarantees::GasGiantMinEarthMasses
            && MassEarth >= GenGuarantees::BarrenBelowEarthMasses
            && EquilibriumK <= GenGuarantees::BarrenAboveK
            && EquilibriumK >= GenGuarantees::IceBelowK;
    }

    EPlanetKind KindOf(uint64 PlanetSeed, double MassEarth, double EquilibriumK, const FGenPriors& Priors)
    {
        if (MassEarth >= GenGuarantees::GasGiantMinEarthMasses)
        {
            return EPlanetKind::GasGiant;
        }
        if (MassEarth < GenGuarantees::BarrenBelowEarthMasses || EquilibriumK > GenGuarantees::BarrenAboveK)
        {
            return EPlanetKind::Barren;
        }
        if (EquilibriumK < GenGuarantees::IceBelowK)
        {
            return EPlanetKind::Ice;
        }
        FGenStream Stream(GenSeed::Derive(PlanetSeed, GenSeed::Label("kind")));
        return Stream.Chance(Priors.OceanFraction) ? EPlanetKind::Ocean : EPlanetKind::Terrestrial;
    }
}

FStar FStarSystemGenerator::GenerateStar(uint64 SystemSeed, const FGenPriors& Priors)
{
    const uint64 StarSeed = GenSeed::Derive(SystemSeed, GenSeed::Label("star"));

    // The solar neighbourhood's main sequence as it is: three suns in four
    // are red dwarfs.
    const double Weights[NumStarClasses] = {
        Priors.ClassWeightM, Priors.ClassWeightK, Priors.ClassWeightG,
        Priors.ClassWeightF, Priors.ClassWeightA, Priors.ClassWeightB};
    FGenStream ClassStream(GenSeed::Derive(StarSeed, GenSeed::Label("class")));
    const int32 ClassIndex = ClassStream.Categorical(Weights);

    // Where in its class: a bounded proportion, skewed low because the
    // initial mass function is steep inside a class as well as across them.
    FGenStream BandStream(GenSeed::Derive(StarSeed, GenSeed::Label("band")));
    const double Band = BandStream.Beta(Priors.ClassBandBetaA, Priors.ClassBandBetaB);

    const FClassBand& Span = ClassBands[ClassIndex];

    FStar Star;
    Star.Class = static_cast<EStarClass>(ClassIndex);
    Star.MassSolar = LogLerp(Span.MassMin, Span.MassMax, Band);
    Star.TemperatureK = LogLerp(Span.TemperatureMin, Span.TemperatureMax, Band);
    Star.LuminositySolar = LuminosityOf(Star.MassSolar);
    Star.RadiusSolar = std::pow(Star.MassSolar, 0.8);

    const double RootL = std::sqrt(Star.LuminositySolar);
    Star.HabitableInnerAU = 0.95 * RootL;
    Star.HabitableOuterAU = 1.37 * RootL;
    Star.FrostLineAU = 2.7 * RootL;
    return Star;
}

FString FStarSystemGenerator::GenerateName(uint64 SystemSeed)
{
    return SystemNames::MakeSystemName(GenSeed::Derive(SystemSeed, GenSeed::Label("name")));
}

int32 FStarSystemGenerator::GeneratePlanetCount(uint64 SystemSeed, const FGenPriors& Priors)
{
    // A count of things in an interval is Poisson's whole job.
    FGenStream Stream(GenSeed::Derive(SystemSeed, GenSeed::Label("planets")));
    return Stream.Poisson(Priors.PlanetCountMean, GenGuarantees::MaxPlanets);
}

FStarSystem FStarSystemGenerator::Generate(const FStarSystemStub& Stub, const FGenPriors& Priors)
{
    return GenerateWithPlanetCount(Stub, Priors, GeneratePlanetCount(Stub.Seed, Priors));
}

FStarSystem FStarSystemGenerator::GenerateWithPlanetCount(const FStarSystemStub& Stub, const FGenPriors& Priors, int32 PlanetCount)
{
    const uint64 Seed = Stub.Seed;

    FStarSystem System;
    System.Stub = Stub;
    System.Star = GenerateStar(Seed, Priors);

    const FStar& Star = System.Star;
    const double RootL = std::sqrt(Star.LuminositySolar);
    const double StarRadiusAU = Star.RadiusSolar * UniverseUnits::CmPerSolarRadius / UniverseUnits::CmPerAU;

    double PreviousAU = 0.0;
    double PreviousMassSolar = 0.0;

    System.Planets.Reserve(PlanetCount);
    for (int32 Index = 0; Index < PlanetCount; ++Index)
    {
        const uint64 PlanetSeed = GenSeed::Derive(Seed, GenSeed::Label("planet"), Index);
        FGenStream Spacing(GenSeed::Derive(Seed, GenSeed::Label("spacing"), Index));

        double SemiMajorAxisAU = 0.0;
        double MassEarth = 0.0;

        if (Index == 0)
        {
            // The innermost orbit follows the star's warmth, not its size, and
            // is truncated so that nothing forms inside five stellar radii.
            const double Median = Priors.InnermostMedianFactor * RootL;
            const double Min = FMath::Max(0.5 * Median, GenGuarantees::InnermostMinStellarRadii * StarRadiusAU);
            const double Max = FMath::Max(2.0 * Median, Min);
            SemiMajorAxisAU = Spacing.LogNormalBounded(Median, Priors.InnermostSigma, Min, Max);
            MassEarth = DrawMass(PlanetSeed, SemiMajorAxisAU > Star.FrostLineAU, Star, Priors);
        }
        else
        {
            // Neighbours sit a number of mutual Hill radii apart -- the length
            // on which two planets disturb each other. Observed compact systems
            // sit around twenty; under ten they go unstable within a star's
            // age, so ten is a floor no prior can move.
            const double K = Spacing.LogNormalBounded(Priors.HillSpacingMedian, Priors.HillSpacingSigma,
                GenGuarantees::MinHillSpacing, GenGuarantees::MaxHillSpacing);

            // The closest the new planet could possibly be (spacing ten and no
            // mass of its own) decides which side of the frost line it forms.
            const double H0 = std::cbrt(PreviousMassSolar / (3.0 * Star.MassSolar));
            const double ClosestAU = PreviousAU * (1.0 + 5.0 * H0) / (1.0 - 5.0 * H0);
            MassEarth = DrawMass(PlanetSeed, ClosestAU > Star.FrostLineAU, Star, Priors);

            // Rescale, not clamp: the spacing range is squeezed so that K h
            // stays well under the 2 at which the orbit solution fails, and the
            // heaviest pairs get the tightest range with no pile-up at a bound.
            const double MassSolar = MassEarth * UniverseUnits::EarthMassSolar;
            const double H = std::cbrt((PreviousMassSolar + MassSolar) / (3.0 * Star.MassSolar));
            const double Ceiling = FMath::Min(GenGuarantees::MaxHillSpacing, GenGuarantees::HillSpacingScale / H);
            const double Spread = GenGuarantees::MaxHillSpacing - GenGuarantees::MinHillSpacing;
            const double Rescaled = GenGuarantees::MinHillSpacing + (K - GenGuarantees::MinHillSpacing) * (Ceiling - GenGuarantees::MinHillSpacing) / Spread;

            const double Half = 0.5 * Rescaled * H;
            SemiMajorAxisAU = PreviousAU * (1.0 + Half) / (1.0 - Half);
        }

        FPlanet Planet;
        Planet.Id = FBodyId{Stub.Id, Index, -1};
        Planet.Designation = SystemNames::Designation(Stub.Name, Index);
        Planet.MassEarth = MassEarth;
        Planet.SemiMajorAxisAU = SemiMajorAxisAU;
        Planet.EquilibriumK = 278.6 * std::pow(Star.LuminositySolar, 0.25) / std::sqrt(SemiMajorAxisAU);
        Planet.Kind = KindOf(PlanetSeed, MassEarth, Planet.EquilibriumK, Priors);

        // Crude: one power law for rock, and giants all roughly Jupiter-sized.
        Planet.RadiusEarth = MassEarth < GenGuarantees::GasGiantMinEarthMasses ? std::pow(MassEarth, 0.28) : 11.0;

        // An orbit seen at an unknown epoch has no preferred phase: one of the
        // two places uniform is the distribution that describes the thing.
        FGenStream Phase(GenSeed::Derive(PlanetSeed, GenSeed::Label("phase")));
        Planet.PhaseRad = Phase.Unit() * 2.0 * UE_DOUBLE_PI;

        if (IsTemperate(MassEarth, Planet.EquilibriumK))
        {
            FGenStream Inhabited(GenSeed::Derive(PlanetSeed, GenSeed::Label("inhabited")));
            if (Inhabited.Chance(Priors.InhabitedChance))
            {
                // Settlement size is a magnitude of many multiplied factors,
                // truncated because a log-normal will eventually found a colony
                // of forty billion.
                FGenStream Population(GenSeed::Derive(PlanetSeed, GenSeed::Label("population")));
                Planet.Population = Population.LogNormalBounded(Priors.PopulationMedian, Priors.PopulationSigma,
                    Priors.PopulationMin, Priors.PopulationMax);
                Planet.GivenName = SystemNames::MakeGivenName(GenSeed::Derive(PlanetSeed, GenSeed::Label("givenname")));
            }
        }

        System.Planets.Add(MoveTemp(Planet));
        PreviousAU = SemiMajorAxisAU;
        PreviousMassSolar = MassEarth * UniverseUnits::EarthMassSolar;
    }

    return System;
}
