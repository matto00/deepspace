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

    /**
     * A giant's day, hours, from its own `day` stream.
     *
     * A giant keeps the spin it accreted with, and what that comes to is a
     * product of many factors -- the angular momentum the disc fed it, how
     * much its magnetic field braked it, how far it contracted since -- which
     * is log-normal's definition. Centred on 12 h with a log spread of 0.35:
     * Jupiter's 9.9 h and Saturn's 10.7 h sit just under the median, the ice
     * giants' 16 and 17 h a sigma above it, and the young giants measured
     * round other stars (beta Pictoris b, 8 h) inside it. Bounded at 5 h,
     * well short of the ~3 h at which a Jupiter would fling itself apart,
     * and at 30 h, beyond which a giant has been braked by something -- a
     * close star's tides -- this generator does not model.
     */
    constexpr double GiantDayMedianHours = 12.0;
    constexpr double GiantDaySigma = 0.35;
    constexpr double GiantDayMinHours = 5.0;
    constexpr double GiantDayMaxHours = 30.0;

    double DrawGiantDay(uint64 PlanetSeed)
    {
        FGenStream Stream(GenSeed::Derive(PlanetSeed, GenSeed::Label("day")));
        return Stream.LogNormalBounded(GiantDayMedianHours, GiantDaySigma, GiantDayMinHours, GiantDayMaxHours);
    }

    /**
     * A solid world's highest relief, km, from its own `relief` stream
     * (landing decision 3). What a crust holds up goes as 1/g, so the ceiling
     * is derived -- a strength over the surface gravity in Earth g, weathered
     * lower on a terrestrial world -- and capped by the guarantees. Only how
     * much of it a world reaches is drawn, and a bounded proportion is Beta's
     * whole job. Oceans and giants have no ground: 0, and nothing drawn.
     */
    double DrawRelief(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)
    {
        if (Planet.Kind == EPlanetKind::Ocean || Planet.Kind == EPlanetKind::GasGiant)
        {
            return 0.0;
        }
        const bool bWeathered = Planet.Kind == EPlanetKind::Terrestrial;
        const double Strength = Planet.Kind == EPlanetKind::Ice ? Priors.ReliefStrengthIceKm
            : bWeathered ? Priors.ReliefStrengthRockKm * Priors.ReliefTerrestrialFactor
            : Priors.ReliefStrengthRockKm;
        const double RadiusKm = Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius / UniverseUnits::CmPerKm;
        const double Ceiling = FMath::Min3(Strength / Planet.SurfaceGravityEarth(), GenGuarantees::MaxReliefKm,
            GenGuarantees::MaxReliefRadiusFraction * RadiusKm);
        FGenStream Stream(GenSeed::Derive(PlanetSeed, GenSeed::Label("relief")));
        const double Share = bWeathered ? Stream.Beta(Priors.ReliefTerrestrialBetaA, Priors.ReliefTerrestrialBetaB)
                                        : Stream.Beta(Priors.ReliefBetaA, Priors.ReliefBetaB);
        return Ceiling * Share;
    }

    /**
     * A world's air, from its own two streams (atmospheres decision 2).
     * Giants draw nothing: hydrogen and helium at their disc. Barren and ice
     * worlds draw nothing: airless. A temperate world's mix is categorical
     * over the ini's weights, each multiplied by how well the world holds
     * that gas, so what it cannot hold is never drawn and nothing drawn is
     * thrown away (ADR 0008); if the weights leave it nothing it can hold,
     * it is airless and draws nothing. Its pressure is log-normal by kind --
     * a product of many factors -- bent under the world's ceiling by the
     * smooth ceiling, so the corpus shows no pile at the cap.
     */
    FAirDraw DrawAir(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)
    {
        FAirDraw Air;
        const double Gravity = Planet.SurfaceGravityEarth();
        if (Planet.Kind == EPlanetKind::GasGiant)
        {
            Air.Mix = EAirMix::HydrogenHelium;
            Air.CeilingBar = AirFacts::GiantDiscPressureBar(Gravity);
            Air.DrawnBar = Air.CeilingBar;
            Air.PressureBar = Air.CeilingBar;
            return Air;
        }
        if (Planet.Kind != EPlanetKind::Terrestrial && Planet.Kind != EPlanetKind::Ocean)
        {
            return Air;
        }
        const EAirMix Mixes[AirFacts::NumMixes] = {EAirMix::NitrogenOxygen, EAirMix::CarbonDioxide, EAirMix::HydrogenHelium};
        const double Weights[AirFacts::NumMixes] = {Priors.AirMixWeightNitrogenOxygen, Priors.AirMixWeightCarbonDioxide, Priors.AirMixWeightHydrogenHelium};
        double Retained[AirFacts::NumMixes];
        double Shaped[AirFacts::NumMixes];
        double Total = 0.0;
        for (int32 Index = 0; Index < AirFacts::NumMixes; ++Index)
        {
            Retained[Index] = AirFacts::Retention(Mixes[Index], Planet.MassEarth, Planet.RadiusEarth, Planet.EquilibriumK);
            Shaped[Index] = Weights[Index] * Retained[Index];
            Total += Shaped[Index];
        }
        if (!(Total > 0.0))
        {
            return Air;
        }
        FGenStream MixStream(GenSeed::Derive(PlanetSeed, GenSeed::Label("air.mix")));
        const int32 Chosen = MixStream.Categorical(MakeArrayView(Shaped, AirFacts::NumMixes));
        Air.Mix = Mixes[Chosen];
        const double Median = Planet.Kind == EPlanetKind::Ocean ? Priors.AirPressureMedianOceanBar : Priors.AirPressureMedianTerrestrialBar;
        FGenStream PressureStream(GenSeed::Derive(PlanetSeed, GenSeed::Label("air.pressure")));
        Air.DrawnBar = PressureStream.LogNormal(Median, Priors.AirPressureSigma);
        Air.CeilingBar = AirFacts::PressureCeilingBar(Air.Mix, Gravity, Retained[Chosen]);
        Air.PressureBar = AirFacts::SmoothCeiling(Air.DrawnBar, Air.CeilingBar);
        return Air;
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

FStarSystem FStarSystemGenerator::GenerateWithPlanetCount(const FStarSystemStub& Stub, const FGenPriors& Priors, int32 PlanetCount, EAirDraws AirDraws)
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
        const uint64 PlanetSeed = FStarSystemGenerator::PlanetSeed(Seed, Index);
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

        // Its own stream too: a world's relief moves nothing else about it.
        Planet.ReliefKm = DrawRelief(PlanetSeed, Planet, Priors);

        // Its own two streams: a world's air moves nothing else about it,
        // which the draws-off comparison holds.
        if (AirDraws == EAirDraws::On)
        {
            const FAirDraw Air = DrawAir(PlanetSeed, Planet, Priors);
            Planet.AirMix = Air.Mix;
            Planet.SurfacePressureBar = Air.PressureBar;
        }

        // An orbit seen at an unknown epoch has no preferred phase: one of the
        // two places uniform is the distribution that describes the thing.
        FGenStream Phase(GenSeed::Derive(PlanetSeed, GenSeed::Label("phase")));
        Planet.PhaseRad = Phase.Unit() * 2.0 * UE_DOUBLE_PI;

        // Its own stream, so drawing it moves no other quantity of any world.
        if (Planet.Kind == EPlanetKind::GasGiant)
        {
            Planet.DayHours = DrawGiantDay(PlanetSeed);
        }

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

double FStarSystemGenerator::GenerateGiantDay(uint64 PlanetSeed)
{
    return DrawGiantDay(PlanetSeed);
}

uint64 FStarSystemGenerator::PlanetSeed(uint64 SystemSeed, int32 Index)
{
    return GenSeed::Derive(SystemSeed, GenSeed::Label("planet"), static_cast<uint64>(Index));
}

double FStarSystemGenerator::GenerateRelief(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)
{
    return DrawRelief(PlanetSeed, Planet, Priors);
}

FAirDraw FStarSystemGenerator::GenerateAir(uint64 PlanetSeed, const FPlanet& Planet, const FGenPriors& Priors)
{
    return DrawAir(PlanetSeed, Planet, Priors);
}
