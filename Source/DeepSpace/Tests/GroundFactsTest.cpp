#include "Misc/AutomationTest.h"
#include "Sky/SkySystem.h"
#include "Surface/WorldReliefParams.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenSeed.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGroundFactsTest,
    "DeepSpace.Universe.GroundFacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GroundFactsTestLocal
{
    constexpr uint64 Root = 20260925;

    // The face's seed is the ground's now (landing decision 2), and it keeps
    // the derivation every world's face was made with: a change here moves
    // every place anybody has been.
    static_assert(GenSeed::SurfaceSeed(1, 2) == GenSeed::Derive(1, GenSeed::Label("sky.surface"), 2),
        "GenSeed::SurfaceSeed is the sky's derivation, unchanged");
}

bool FGroundFactsTest::RunTest(const FString& Parameters)
{
    using namespace GroundFactsTestLocal;

    // -- The mass parameters (landing decision 4): IAU 2015 nominal values ------
    const double Ratio = UniverseUnits::GMEarthCm3PerS2 / UniverseUnits::GMSunCm3PerS2;
    TestTrue(FString::Printf(TEXT("the Earth's GM over the Sun's (%.7e) is EarthMassSolar to 5e-5"), Ratio),
        FMath::Abs(Ratio / UniverseUnits::EarthMassSolar - 1.0) < 5.0e-5);
    const double EarthSurface = UniverseUnits::GMEarthCm3PerS2 / FMath::Square(UniverseUnits::CmPerEarthRadius);
    TestTrue(FString::Printf(TEXT("an Earth pulls 979.840 cm/s^2 at its equatorial radius (%.4f)"), EarthSurface),
        FMath::Abs(EarthSurface - 979.8398) < 1.0e-3);
    const double SunAtOneAU = UniverseUnits::GMSunCm3PerS2 / FMath::Square(UniverseUnits::CmPerAU);
    TestTrue(FString::Printf(TEXT("the Sun pulls 0.593008 cm/s^2 at 1 AU (%.6f)"), SunAtOneAU),
        FMath::Abs(SunAtOneAU - 0.593008) < 1.0e-5);

    // -- A default is no ground at all -------------------------------------------
    const FWorldReliefParams None;
    TestTrue(TEXT("default relief params are no ground: None, no peak, no radius"),
        None.Ground == EGround::None && None.PeakCm == 0.0 && None.RadiusCm == 0.0 && None.SeedOffset.IsZero());

    // -- Home, as the ini's priors make it ---------------------------------------
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
    const FGalaxyGenerator Galaxy(Root, Priors);
    const TArray<FStarSystemStub> HomeSector = Galaxy.GenerateSector(FInt64Vector(-1, -1, 0));
    if (!TestTrue(TEXT("home's sector holds a system"), HomeSector.Num() > 0))
    {
        return false;
    }
    TestEqual(TEXT("slot 0 of sector (-1, -1, 0) is Baemsekai, the landing fixtures' home"),
        HomeSector[0].Name, FString(TEXT("Baemsekai")));
    const FStarSystem Home = FStarSystemGenerator::Generate(HomeSector[0], Priors);
    const FSkySystem Sky = FSkySystem::FromSystem(Home, {});
    for (int32 Index = 0; Index < Home.Planets.Num(); ++Index)
    {
        TestEqual(FString::Printf(TEXT("%s's face is GenSeed::SurfaceSeed's"), *Home.Planets[Index].Designation),
            Sky.Bodies[Index + 1].SurfaceSeed, GenSeed::SurfaceSeed(Home.Stub.Seed, static_cast<uint64>(Index)));
    }

    // -- PlanetSeed is the seed Generate draws each world from -------------------
    // A giant's day comes from its own stream under the planet's seed, so the
    // day drawn again from PlanetSeed is the day Generate drew only if the two
    // are one seed.
    bool bFoundGiant = false;
    for (int64 X = -10; X <= 10 && !bFoundGiant; ++X)
    {
        for (int64 Y = -10; Y <= 10 && !bFoundGiant; ++Y)
        {
            for (int64 Z = -3; Z <= 3 && !bFoundGiant; ++Z)
            {
                for (const FStarSystemStub& Stub : Galaxy.GenerateSector(FInt64Vector(X, Y, Z)))
                {
                    const FStarSystem System = FStarSystemGenerator::Generate(Stub, Priors);
                    for (int32 Index = 0; Index < System.Planets.Num() && !bFoundGiant; ++Index)
                    {
                        if (System.Planets[Index].Kind != EPlanetKind::GasGiant)
                        {
                            continue;
                        }
                        bFoundGiant = true;
                        TestEqual(FString::Printf(TEXT("%s's day, drawn again from PlanetSeed, is the day Generate drew"),
                                *System.Planets[Index].Designation),
                            FStarSystemGenerator::GenerateGiantDay(FStarSystemGenerator::PlanetSeed(Stub.Seed, Index)),
                            System.Planets[Index].DayHours);
                    }
                    if (bFoundGiant)
                    {
                        break;
                    }
                }
            }
        }
    }
    TestTrue(TEXT("a giant within ten sectors of home, to check PlanetSeed against"), bFoundGiant);
    return true;
}

#endif
