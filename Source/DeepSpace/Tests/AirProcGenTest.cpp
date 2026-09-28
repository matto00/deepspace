#include "Misc/AutomationTest.h"
#include "Tests/AirFixtureWorlds.h"
#include "Universe/AirFacts.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenPriors.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAirProcGenTest,
    "DeepSpace.Universe.Air",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** The atmospheres spec's fixture roles, found by its rules (AirFixtureWorlds):
 *  its log fills the spec's *Fixture worlds* table. Fails if a role has no
 *  world within AirFixtureWorlds::SearchSystems of home. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereFixtureWorldsTest,
    "DeepSpace.Atmosphere.FixtureWorlds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AirProcGenTestLocal
{
    constexpr uint64 Root = 20260925;
    constexpr int32 SystemCount = 3000;

    /** The Count systems nearest home under these priors, nearest first. */
    TArray<FStarSystem> Nearest(const FGenPriors& Priors, int32 Count)
    {
        TArray<FStarSystem> Systems;
        const FGalaxyGenerator Galaxy(Root, Priors);
        const TOptional<FStarSystemStub> Home = Galaxy.GenerateStub(Galaxy.StartSystem());
        if (!Home.IsSet())
        {
            return Systems;
        }
        TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(Home->Position, FGalaxyGenerator::MaxSearchRadiusLy * UniverseUnits::CmPerLightYear);
        const FUniversePosition Origin = Home->Position;
        Near.Sort([&Origin](const FStarSystemStub& A, const FStarSystemStub& B) { return Origin.DistanceTo(A.Position) < Origin.DistanceTo(B.Position); });
        for (int32 I = 0; I < FMath::Min(Count, Near.Num()); ++I)
        {
            Systems.Add(Galaxy.GenerateSystem(Near[I]));
        }
        return Systems;
    }

    bool IsTemperate(EPlanetKind Kind)
    {
        return Kind == EPlanetKind::Terrestrial || Kind == EPlanetKind::Ocean;
    }

    bool Names(const TArray<FString>& Refusals, const TCHAR* Line)
    {
        return Refusals.ContainsByPredicate([Line](const FString& Refusal) { return Refusal.Contains(Line); });
    }
}

bool FAirProcGenTest::RunTest(const FString& Parameters)
{
    using namespace AirProcGenTestLocal;
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
    TestTrue(TEXT("the ini's priors are accepted"), GenPriorDomain::Refusals(Priors).IsEmpty());
    const TArray<FStarSystem> Systems = Nearest(Priors, SystemCount);
    if (!TestEqual(TEXT("enough systems near home"), Systems.Num(), SystemCount))
    {
        return false;
    }

    // -- Every world, against the rules ---------------------------------------
    FString FirstBreak;
    int32 Temperate = 0;
    int32 Giants = 0;
    int32 OverCeiling = 0;
    int32 AtCeiling = 0;
    int32 ByMix[4] = {0, 0, 0, 0};
    for (const FStarSystem& System : Systems)
    {
        for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
        {
            const FPlanet& Planet = System.Planets[Index];
            const double G = Planet.SurfaceGravityEarth();
            const auto Break = [&FirstBreak, &Planet](const TCHAR* What)
            {
                if (FirstBreak.IsEmpty())
                {
                    FirstBreak = FString::Printf(TEXT("%s (%s, %.3f bar): %s"), *Planet.Designation, AirFacts::Name(Planet.AirMix), Planet.SurfacePressureBar, What);
                }
            };
            if (Planet.Kind == EPlanetKind::Barren || Planet.Kind == EPlanetKind::Ice)
            {
                if (Planet.AirMix != EAirMix::None || Planet.SurfacePressureBar != 0.0)
                {
                    Break(TEXT("barren and ice worlds are airless"));
                }
                continue;
            }
            if (Planet.Kind == EPlanetKind::GasGiant)
            {
                ++Giants;
                if (Planet.AirMix != EAirMix::HydrogenHelium || Planet.SurfacePressureBar != AirFacts::GiantDiscPressureBar(G))
                {
                    Break(TEXT("a giant is hydrogen and helium, its disc at the guarantee's depth"));
                }
                if (FMath::Abs(AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, G) - GenGuarantees::MaxNadirTau450) > 1.0e-9)
                {
                    Break(TEXT("a giant's disc is where the air above reaches MaxNadirTau450"));
                }
                continue;
            }
            ++Temperate;
            ++ByMix[static_cast<int32>(Planet.AirMix)];
            const double Retained = AirFacts::Retention(Planet.AirMix, Planet.MassEarth, Planet.RadiusEarth, Planet.EquilibriumK);
            if (Planet.AirMix == EAirMix::None || !(Planet.SurfacePressureBar > 0.0))
            {
                Break(TEXT("a temperate world under the ini's priors has air"));
            }
            if (!(Retained > 0.0))
            {
                Break(TEXT("no world holds a mix it cannot retain"));
            }
            if (AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, G) > GenGuarantees::MaxNadirTau450 + 1.0e-9)
            {
                Break(TEXT("every airy world at or under MaxNadirTau450"));
            }
            const FAirDraw Draw = FStarSystemGenerator::GenerateAir(FStarSystemGenerator::PlanetSeed(System.Stub.Seed, Index), Planet, Priors);
            if (Draw.Mix != Planet.AirMix || Draw.PressureBar != Planet.SurfacePressureBar)
            {
                Break(TEXT("GenerateAir is what Generate drew"));
            }
            if (Planet.SurfacePressureBar > Draw.CeilingBar)
            {
                Break(TEXT("never over the ceiling"));
            }
            OverCeiling += Draw.DrawnBar > Draw.CeilingBar ? 1 : 0;
            AtCeiling += Planet.SurfacePressureBar / Draw.CeilingBar > 0.99999 ? 1 : 0;
        }
    }
    AddInfo(FString::Printf(TEXT("%d temperate worlds: %d N2/O2, %d CO2, %d H2/He; %d giants; %d drew over their ceiling, %d sit within 1e-5 of it"),
        Temperate, ByMix[1], ByMix[2], ByMix[3], Giants, OverCeiling, AtCeiling));
    TestTrue(TEXT("every world keeps the rules: ") + FirstBreak, FirstBreak.IsEmpty());
    TestTrue(TEXT("the mixes spread: N2/O2 and CO2 both occur"), ByMix[1] > 0 && ByMix[2] > 0);
    TestTrue(TEXT("the ceiling bends some worlds (else the next check proves nothing)"), OverCeiling > 0);
    TestTrue(TEXT("the smooth ceiling leaves no spike: under a tenth as many at the cap as drew past it"), 10 * AtCeiling < OverCeiling);

    // -- The draws come from the two named streams ---------------------------------
    // Drawn again by hand from "air.mix" and "air.pressure" under the first
    // 50 temperate worlds' seeds: a mix or pressure on any other stream would
    // be a draw that moves when that stream's quantity is retuned, and 50
    // worlds leave no chance of a wrong stream agreeing everywhere.
    int32 StreamsChecked = 0;
    int32 StreamMisses = 0;
    for (int32 S = 0; StreamsChecked < 50 && S < Systems.Num(); ++S)
    {
        for (int32 Index = 0; StreamsChecked < 50 && Index < Systems[S].Planets.Num(); ++Index)
        {
            const FPlanet& Planet = Systems[S].Planets[Index];
            if (!IsTemperate(Planet.Kind))
            {
                continue;
            }
            ++StreamsChecked;
            const uint64 Seed = FStarSystemGenerator::PlanetSeed(Systems[S].Stub.Seed, Index);
            const EAirMix Mixes[] = {EAirMix::NitrogenOxygen, EAirMix::CarbonDioxide, EAirMix::HydrogenHelium};
            const double Weights[] = {Priors.AirMixWeightNitrogenOxygen, Priors.AirMixWeightCarbonDioxide, Priors.AirMixWeightHydrogenHelium};
            double Shaped[3];
            for (int32 M = 0; M < 3; ++M)
            {
                Shaped[M] = Weights[M] * AirFacts::Retention(Mixes[M], Planet.MassEarth, Planet.RadiusEarth, Planet.EquilibriumK);
            }
            FGenStream MixStream(GenSeed::Derive(Seed, GenSeed::Label("air.mix")));
            const EAirMix ByHand = Mixes[MixStream.Categorical(MakeArrayView(Shaped, 3))];
            const double Median = Planet.Kind == EPlanetKind::Ocean ? Priors.AirPressureMedianOceanBar : Priors.AirPressureMedianTerrestrialBar;
            FGenStream PressureStream(GenSeed::Derive(Seed, GenSeed::Label("air.pressure")));
            const double DrawnByHand = PressureStream.LogNormal(Median, Priors.AirPressureSigma);
            const FAirDraw Draw = FStarSystemGenerator::GenerateAir(Seed, Planet, Priors);
            StreamMisses += (Planet.AirMix != ByHand || Draw.DrawnBar != DrawnByHand) ? 1 : 0;
        }
    }
    TestEqual(TEXT("50 temperate worlds checked against their named streams"), StreamsChecked, 50);
    TestEqual(TEXT("every mix is its air.mix stream's and every drawn pressure its air.pressure stream's"), StreamMisses, 0);

    // -- The draws off: the air moves nothing else about any world ----------------
    // The spec's comparison. Each of the 500 nearest systems generated again
    // with no air drawn is the same system in everything but its air: an air
    // block that touched any other quantity, by however fixed an amount,
    // shows here, where two runs of the new code (below) cannot see it.
    bool bOffSame = true;
    bool bOffAirless = true;
    int32 OffWorlds = 0;
    for (int32 S = 0; bOffSame && S < 500; ++S)
    {
        const FStarSystem& On = Systems[S];
        const FStarSystem Off = FStarSystemGenerator::GenerateWithPlanetCount(On.Stub, Priors, On.Planets.Num(), EAirDraws::Off);
        bOffSame &= Off.Planets.Num() == On.Planets.Num() && Off.Star.TemperatureK == On.Star.TemperatureK && Off.Star.MassSolar == On.Star.MassSolar;
        for (int32 P = 0; bOffSame && P < On.Planets.Num(); ++P)
        {
            const FPlanet& A = On.Planets[P];
            const FPlanet& B = Off.Planets[P];
            ++OffWorlds;
            bOffSame &= A.Kind == B.Kind && A.MassEarth == B.MassEarth && A.RadiusEarth == B.RadiusEarth
                && A.SemiMajorAxisAU == B.SemiMajorAxisAU && A.EquilibriumK == B.EquilibriumK && A.PhaseRad == B.PhaseRad
                && A.ReliefKm == B.ReliefKm && A.DayHours == B.DayHours && A.Population == B.Population && A.GivenName == B.GivenName;
            bOffAirless &= B.AirMix == EAirMix::None && B.SurfacePressureBar == 0.0;
        }
    }
    TestTrue(FString::Printf(TEXT("with the draws off, each of %d worlds is the same world but for its air: kind, mass, radius, orbit, phase, relief, day and people"), OffWorlds),
        bOffSame && OffWorlds > 0);
    TestTrue(TEXT("and with the draws off, no world has air"), bOffAirless);

    // -- The two labels' priors move nothing else ----------------------------------
    FGenPriors Retuned = Priors;
    Retuned.AirMixWeightNitrogenOxygen = 0.001;
    Retuned.AirMixWeightCarbonDioxide = 1.0;
    Retuned.AirMixWeightHydrogenHelium = 1.0;
    Retuned.AirPressureMedianTerrestrialBar = 0.01;
    Retuned.AirPressureMedianOceanBar = 5.0;
    Retuned.AirPressureSigma = 2.0;
    const TArray<FStarSystem> Again = Nearest(Retuned, 500);
    bool bSame = Again.Num() == 500;
    bool bAirMoved = false;
    for (int32 S = 0; bSame && S < Again.Num(); ++S)
    {
        bSame &= Again[S].Planets.Num() == Systems[S].Planets.Num();
        for (int32 P = 0; bSame && P < Again[S].Planets.Num(); ++P)
        {
            const FPlanet& A = Systems[S].Planets[P];
            const FPlanet& B = Again[S].Planets[P];
            bSame &= A.Kind == B.Kind && A.MassEarth == B.MassEarth && A.RadiusEarth == B.RadiusEarth
                && A.SemiMajorAxisAU == B.SemiMajorAxisAU && A.EquilibriumK == B.EquilibriumK && A.PhaseRad == B.PhaseRad
                && A.ReliefKm == B.ReliefKm && A.DayHours == B.DayHours && A.Population == B.Population && A.GivenName == B.GivenName;
            bAirMoved |= A.AirMix != B.AirMix || A.SurfacePressureBar != B.SurfacePressureBar;
        }
    }
    TestTrue(TEXT("retuning every air prior moves no kind, mass, radius, orbit, phase, relief, day or people"), bSame);
    TestTrue(TEXT("and does move the air"), bAirMoved);

    // -- The domain names the line ------------------------------------------------------
    FGenPriors Bad = Priors;
    Bad.AirPressureSigma = 0.0;
    TestTrue(TEXT("a zero pressure sigma is refused by name"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirPressureSigma")));
    Bad = Priors;
    Bad.AirPressureSigma = 3.0;
    TestTrue(TEXT("and one past 2.5"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirPressureSigma")));
    Bad = Priors;
    Bad.AirPressureMedianOceanBar = -1.0;
    TestTrue(TEXT("a negative ocean median is refused by name"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirPressureMedianOceanBar")));
    Bad = Priors;
    Bad.AirMixWeightCarbonDioxide = -0.1;
    TestTrue(TEXT("a negative mix weight is refused by name"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirMixWeightCarbonDioxide")));
    Bad = Priors;
    Bad.AirMixWeightNitrogenOxygen = 0.0;
    Bad.AirMixWeightCarbonDioxide = 0.0;
    Bad.AirMixWeightHydrogenHelium = 0.0;
    TestTrue(TEXT("no gas at all is refused"), Names(GenPriorDomain::Refusals(Bad), TEXT("AirMixWeightNitrogenOxygen..AirMixWeightHydrogenHelium")));

    // -- Review focus 5: legal weights that leave a world nothing it can hold ------------
    FGenPriors Hydrogen = Priors;
    Hydrogen.AirMixWeightNitrogenOxygen = 0.0;
    Hydrogen.AirMixWeightCarbonDioxide = 0.0;
    Hydrogen.AirMixWeightHydrogenHelium = 1.0;
    TestTrue(TEXT("hydrogen alone is a legal set of weights"), GenPriorDomain::Refusals(Hydrogen).IsEmpty());
    int32 Left = 0;
    int32 Held = 0;
    FString HydrogenBreak;
    for (const FStarSystem& System : Nearest(Hydrogen, 500))
    {
        for (const FPlanet& Planet : System.Planets)
        {
            if (!IsTemperate(Planet.Kind))
            {
                continue;
            }
            const bool bCanHold = AirFacts::Retention(EAirMix::HydrogenHelium, Planet.MassEarth, Planet.RadiusEarth, Planet.EquilibriumK) > 0.0;
            if (bCanHold && Planet.AirMix == EAirMix::HydrogenHelium && Planet.SurfacePressureBar > 0.0)
            {
                ++Held;
            }
            else if (!bCanHold && Planet.AirMix == EAirMix::None && Planet.SurfacePressureBar == 0.0)
            {
                ++Left;
            }
            else if (HydrogenBreak.IsEmpty())
            {
                HydrogenBreak = Planet.Designation;
            }
        }
    }
    AddInfo(FString::Printf(TEXT("hydrogen only: %d temperate worlds hold it, %d cannot and are airless"), Held, Left));
    TestTrue(TEXT("a world that can hold nothing the weights allow is airless, and nothing is drawn: ") + HydrogenBreak, HydrogenBreak.IsEmpty());
    TestTrue(TEXT("some such worlds exist near home"), Left > 0);
    return true;
}

bool FAtmosphereFixtureWorldsTest::RunTest(const FString& Parameters)
{
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
    for (const AirFixtureWorlds::FFixture& Role : AirFixtureWorlds::Find(AirFixtureWorlds::UniverseSeed, Priors, AirFixtureWorlds::SearchSystems))
    {
        if (!TestTrue(FString::Printf(TEXT("fixture %s is found within %d systems of home"), Role.Role, AirFixtureWorlds::SearchSystems), Role.bFound))
        {
            continue;
        }
        const FPlanet& Planet = Role.Planet();
        const double G = Planet.SurfaceGravityEarth();
        AddInfo(FString::Printf(TEXT("fixture %s: %s | sector (%lld, %lld, %lld) slot %d, orbit index %d | %s | %.4g bar | %.3f g | tau450 %.3f | %.2f M_E | star %.0f K | %.2f ly"),
            Role.Role, *Planet.Designation,
            static_cast<long long>(Role.System.Stub.Id.Sector.X), static_cast<long long>(Role.System.Stub.Id.Sector.Y), static_cast<long long>(Role.System.Stub.Id.Sector.Z),
            Role.System.Stub.Id.Slot, Role.Index, AirFacts::Name(Planet.AirMix), Planet.SurfacePressureBar, G,
            AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, G), Planet.MassEarth,
            Role.System.Star.TemperatureK, Role.DistanceLy));
    }
    return true;
}

#endif
