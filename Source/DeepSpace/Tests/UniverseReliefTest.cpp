#include "Misc/AutomationTest.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenPriors.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUniverseReliefTest,
    "DeepSpace.Universe.Relief",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace UniverseReliefTestLocal
{
    constexpr uint64 Root = 20260925;
    constexpr int32 SystemCount = 3000;

    bool IsSolid(EPlanetKind Kind)
    {
        return Kind == EPlanetKind::Barren || Kind == EPlanetKind::Ice || Kind == EPlanetKind::Terrestrial;
    }

    /** Decision 3's ceiling, written out again rather than called, so the
     *  test and the generator are two readings of the spec. */
    double Ceiling(const FPlanet& Planet, const FGenPriors& P)
    {
        const double Strength = Planet.Kind == EPlanetKind::Ice ? P.ReliefStrengthIceKm
            : Planet.Kind == EPlanetKind::Terrestrial ? P.ReliefStrengthRockKm * P.ReliefTerrestrialFactor
            : P.ReliefStrengthRockKm;
        const double RadiusKm = Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius / UniverseUnits::CmPerKm;
        return FMath::Min3(Strength / Planet.SurfaceGravityEarth(), GenGuarantees::MaxReliefKm,
            GenGuarantees::MaxReliefRadiusFraction * RadiusKm);
    }

    TArray<FStarSystem> Systems(const FGenPriors& Priors, int32 Count)
    {
        const FGalaxyGenerator Galaxy(Root, Priors);
        TArray<FStarSystem> Out;
        for (int64 X = -16; X <= 16 && Out.Num() < Count; ++X)
        {
            for (int64 Y = -16; Y <= 16 && Out.Num() < Count; ++Y)
            {
                for (int64 Z = -4; Z <= 4 && Out.Num() < Count; ++Z)
                {
                    for (const FStarSystemStub& Stub : Galaxy.GenerateSector(FInt64Vector(X, Y, Z)))
                    {
                        if (Out.Num() < Count)
                        {
                            Out.Add(FStarSystemGenerator::Generate(Stub, Priors));
                        }
                    }
                }
            }
        }
        return Out;
    }

    FPlanet Made(EPlanetKind Kind, double MassEarth, double RadiusEarth)
    {
        FPlanet Planet;
        Planet.Kind = Kind;
        Planet.MassEarth = MassEarth;
        Planet.RadiusEarth = RadiusEarth;
        return Planet;
    }

    /** Everything about a world but its relief. */
    bool SameButRelief(const FPlanet& A, const FPlanet& B)
    {
        return A.Designation == B.Designation && A.GivenName == B.GivenName && A.Kind == B.Kind
            && A.MassEarth == B.MassEarth && A.RadiusEarth == B.RadiusEarth && A.EquilibriumK == B.EquilibriumK
            && A.SemiMajorAxisAU == B.SemiMajorAxisAU && A.PhaseRad == B.PhaseRad
            && A.Population == B.Population && A.DayHours == B.DayHours;
    }
}

bool FUniverseReliefTest::RunTest(const FString& Parameters)
{
    using namespace UniverseReliefTestLocal;
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();

    // -- The law, on made worlds: one seed is one Beta draw, so only the ceiling moves
    const uint64 Seed = FStarSystemGenerator::PlanetSeed(12345, 2);
    const double AtOneG = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Barren, 1.0, 1.0), Priors);
    const double AtTwoG = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Barren, 2.0, 1.0), Priors);
    TestTrue(FString::Printf(TEXT("a 1 g barren world has relief under its 1 g ceiling (%.4f km)"), AtOneG),
        AtOneG > 0.0 && AtOneG <= Priors.ReliefStrengthRockKm);
    FGenStream ReliefStream(GenSeed::Derive(Seed, GenSeed::Label("relief")));
    TestTrue(TEXT("the share is the first Beta of the planet's own \"relief\" stream (landing decision 2)"),
        FMath::IsNearlyEqual(AtOneG, Priors.ReliefStrengthRockKm * ReliefStream.Beta(Priors.ReliefBetaA, Priors.ReliefBetaB), 1.0e-12 * AtOneG));
    TestTrue(FString::Printf(TEXT("twice the gravity holds up half the mountain (%.4f, then %.4f km)"), AtOneG, AtTwoG),
        FMath::IsNearlyEqual(AtTwoG, 0.5 * AtOneG, 1.0e-12 * AtOneG));
    const double Light = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Barren, 0.1, 1.0), Priors);
    TestTrue(FString::Printf(TEXT("a light world's ceiling is the guarantee's 10 km, not 90 (%.4f km)"), Light),
        FMath::IsNearlyEqual(Light, AtOneG * GenGuarantees::MaxReliefKm / Priors.ReliefStrengthRockKm, 1.0e-12 * Light));

    FGenPriors Alike = Priors;
    Alike.ReliefTerrestrialBetaA = Priors.ReliefBetaA;
    Alike.ReliefTerrestrialBetaB = Priors.ReliefBetaB;
    const double Rock = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Barren, 1.0, 1.0), Alike);
    const double Land = FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Terrestrial, 1.0, 1.0), Alike);
    TestTrue(TEXT("under the same Beta, weather takes the terrestrial factor off the ceiling"),
        FMath::IsNearlyEqual(Land, Rock * Priors.ReliefTerrestrialFactor, 1.0e-12 * Rock));
    TestEqual(TEXT("an ocean has no ground"), FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::Ocean, 1.0, 1.0), Priors), 0.0);
    TestEqual(TEXT("nor has a giant"), FStarSystemGenerator::GenerateRelief(Seed, Made(EPlanetKind::GasGiant, 300.0, 11.0), Priors), 0.0);

    // -- Every world of 3,000 systems: within its ceiling and the guarantee ----
    const TArray<FStarSystem> Found = Systems(Priors, SystemCount);
    int32 Solid = 0;
    int32 Outside = 0;
    int32 Dry = 0;
    int32 Old = 0;
    int32 Weathered = 0;
    double OldShare = 0.0;
    double WeatheredShare = 0.0;
    for (const FStarSystem& System : Found)
    {
        for (const FPlanet& Planet : System.Planets)
        {
            if (!IsSolid(Planet.Kind))
            {
                Dry += Planet.ReliefKm == 0.0 ? 0 : 1;
                continue;
            }
            ++Solid;
            const double Cap = Ceiling(Planet, Priors);
            const bool bInside = Planet.ReliefKm > 0.0 && Planet.ReliefKm <= Cap * (1.0 + 1.0e-12)
                && Planet.ReliefKm <= GenGuarantees::MaxReliefKm;
            Outside += bInside ? 0 : 1;
            if (Planet.Kind == EPlanetKind::Terrestrial)
            {
                WeatheredShare += Planet.ReliefKm / Cap;
                ++Weathered;
            }
            else
            {
                OldShare += Planet.ReliefKm / Cap;
                ++Old;
            }
        }
    }
    TestEqual(TEXT("no ocean or giant has relief"), Dry, 0);
    TestEqual(FString::Printf(TEXT("every one of %d solid worlds lies in (0, its ceiling], never above 10 km"), Solid), Outside, 0);
    if (TestTrue(FString::Printf(TEXT("enough barren and ice worlds to read the draw (%d)"), Old), Old >= 1000))
    {
        const double Mean = Priors.ReliefBetaA / (Priors.ReliefBetaA + Priors.ReliefBetaB);
        TestTrue(FString::Printf(TEXT("old crust reaches %.3f of its ceiling on average, Beta's %.3f"), OldShare / Old, Mean),
            FMath::Abs(OldShare / Old - Mean) < 0.02);
    }
    if (TestTrue(FString::Printf(TEXT("enough terrestrial worlds to read the draw (%d)"), Weathered), Weathered >= 50))
    {
        const double Mean = Priors.ReliefTerrestrialBetaA / (Priors.ReliefTerrestrialBetaA + Priors.ReliefTerrestrialBetaB);
        TestTrue(FString::Printf(TEXT("weathered crust reaches %.3f of its ceiling on average, Beta's %.3f"), WeatheredShare / Weathered, Mean),
            FMath::Abs(WeatheredShare / Weathered - Mean) < 0.1);
    }

    // -- The relief stream moves nothing else (landing decision 2) --------------
    FGenPriors Other = Priors;
    Other.ReliefStrengthRockKm = 3.0;
    Other.ReliefStrengthIceKm = 4.0;
    Other.ReliefTerrestrialFactor = 0.5;
    Other.ReliefBetaA = 1.5;
    Other.ReliefBetaB = 3.0;
    Other.ReliefTerrestrialBetaA = 2.0;
    Other.ReliefTerrestrialBetaB = 5.0;
    const TArray<FStarSystem> Again = Systems(Other, 300);
    int32 Moved = 0;
    int32 Reliefs = 0;
    for (int32 S = 0; S < Again.Num(); ++S)
    {
        if (Again[S].Planets.Num() != Found[S].Planets.Num())
        {
            ++Moved;
            continue;
        }
        for (int32 P = 0; P < Again[S].Planets.Num(); ++P)
        {
            Moved += SameButRelief(Again[S].Planets[P], Found[S].Planets[P]) ? 0 : 1;
            Reliefs += Again[S].Planets[P].ReliefKm != Found[S].Planets[P].ReliefKm ? 1 : 0;
        }
    }
    TestEqual(TEXT("retuning every relief prior moves no other number of any world"), Moved, 0);
    TestTrue(TEXT("and does move the relief"), Reliefs > 0);

    // -- The domain: a prior its sampler cannot take is refused, by name -------
    const auto Refuses = [&Priors](TFunctionRef<void(FGenPriors&)> Edit, const TCHAR* Name)
    {
        FGenPriors Wrong = Priors;
        Edit(Wrong);
        return GenPriorDomain::Refusals(Wrong).ContainsByPredicate([Name](const FString& Line) { return Line.StartsWith(Name); });
    };
    TestTrue(TEXT("a Beta shape under a tenth is refused"), Refuses([](FGenPriors& P) { P.ReliefBetaA = 0.05; }, TEXT("ReliefBetaA=")));
    TestTrue(TEXT("so is the terrestrial one"), Refuses([](FGenPriors& P) { P.ReliefTerrestrialBetaB = 0.0; }, TEXT("ReliefTerrestrialBetaB=")));
    TestTrue(TEXT("a strength of zero is refused"), Refuses([](FGenPriors& P) { P.ReliefStrengthRockKm = 0.0; }, TEXT("ReliefStrengthRockKm=")));
    TestTrue(TEXT("so is ice's"), Refuses([](FGenPriors& P) { P.ReliefStrengthIceKm = -1.0; }, TEXT("ReliefStrengthIceKm=")));
    TestTrue(TEXT("and a weathering factor of zero"), Refuses([](FGenPriors& P) { P.ReliefTerrestrialFactor = 0.0; }, TEXT("ReliefTerrestrialFactor=")));
    TestTrue(TEXT("the ini's priors are taken"), GenPriorDomain::Refusals(Priors).IsEmpty());

    // -- Home's fixtures (the spec's *Fixture worlds*) -------------------------
    const FGalaxyGenerator Galaxy(Root, Priors);
    const FStarSystem Home = FStarSystemGenerator::Generate(Galaxy.GenerateSector(FInt64Vector(-1, -1, 0))[0], Priors);
    TestEqual(TEXT("home is Baemsekai"), Home.Stub.Name, FString(TEXT("Baemsekai")));
    const struct { int32 Index; EPlanetKind Kind; } Fixtures[] = {
        { 2, EPlanetKind::Barren }, { 3, EPlanetKind::Barren }, { 4, EPlanetKind::Terrestrial } };
    for (const auto& Fixture : Fixtures)
    {
        if (!TestTrue(TEXT("home has the fixture's orbit"), Home.Planets.IsValidIndex(Fixture.Index)))
        {
            continue;
        }
        const FPlanet& World = Home.Planets[Fixture.Index];
        TestTrue(FString::Printf(TEXT("%s is the kind the fixtures say"), *World.Designation), World.Kind == Fixture.Kind);
        TestTrue(FString::Printf(TEXT("%s has relief (%.3f km at %.2f g)"), *World.Designation, World.ReliefKm, World.SurfaceGravityEarth()),
            World.ReliefKm > 0.0);
        AddInfo(FString::Printf(TEXT("%s: %.3f km of relief at %.2f g"), *World.Designation, World.ReliefKm, World.SurfaceGravityEarth()));
    }
    return true;
}

#endif
