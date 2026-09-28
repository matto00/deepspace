#include "Misc/AutomationTest.h"
#include "Universe/AirFacts.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAirFactsTest,
    "DeepSpace.Universe.Gases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AirFactsTestLocal
{
    /** The generator's radius law for rock: R = M^0.28. */
    double RockyRadius(double MassEarth)
    {
        return std::pow(MassEarth, 0.28);
    }

    bool Between(double Value, double Low, double High)
    {
        return Value >= Low && Value <= High;
    }
}

bool FAirFactsTest::RunTest(const FString& Parameters)
{
    using namespace AirFactsTestLocal;
    const EAirMix Mixes[] = {EAirMix::NitrogenOxygen, EAirMix::CarbonDioxide, EAirMix::HydrogenHelium};

    // -- Retention: who keeps what (decision 2's measured cases) -----------------
    const double EarthN2 = AirFacts::JeansLambda(EAirMix::NitrogenOxygen, 1.0, 1.0, 255.0);
    const double EarthH2 = AirFacts::JeansLambda(EAirMix::HydrogenHelium, 1.0, 1.0, 255.0);
    AddInfo(FString::Printf(TEXT("an Earth: lambda %.2f for N2/O2, %.2f for H2/He"), EarthN2, EarthH2));
    TestTrue(TEXT("an Earth holds N2/O2 (lambda about 12)"), Between(EarthN2, 11.5, 12.5));
    TestTrue(TEXT("and loses H2/He (lambda about 3.4)"), Between(EarthH2, 3.2, 3.6));
    TestEqual(TEXT("fully"), AirFacts::Retention(EAirMix::NitrogenOxygen, 1.0, 1.0, 255.0), 1.0);
    TestEqual(TEXT("and none of it"), AirFacts::Retention(EAirMix::HydrogenHelium, 1.0, 1.0, 255.0), 0.0);

    const double Small = AirFacts::JeansLambda(EAirMix::NitrogenOxygen, 0.3, RockyRadius(0.3), 320.0);
    TestTrue(FString::Printf(TEXT("the smallest, hottest temperate world (0.3 M_E, 320 K) still holds N2/O2: lambda %.2f"), Small), Between(Small, 6.6, 7.2));
    TestEqual(TEXT("fully"), AirFacts::Retention(EAirMix::NitrogenOxygen, 0.3, RockyRadius(0.3), 320.0), 1.0);
    TestEqual(TEXT("and CO2"), AirFacts::Retention(EAirMix::CarbonDioxide, 0.3, RockyRadius(0.3), 320.0), 1.0);

    const double Cold = AirFacts::JeansLambda(EAirMix::HydrogenHelium, 10.0, RockyRadius(10.0), 200.0);
    TestTrue(FString::Printf(TEXT("a cold 10 M_E super-Earth at 200 K holds H2/He: lambda %.2f"), Cold), Between(Cold, 8.4, 9.0));

    const double Halfway = AirFacts::Retention(EAirMix::HydrogenHelium, 1.0, 1.0, 255.0 * FMath::Square(EarthH2 / 5.0));
    TestTrue(FString::Printf(TEXT("halfway between lost (4) and held (6), retention is the smoothstep's middle: %.4f"), Halfway),
        FMath::Abs(Halfway - 0.5) < 1.0e-9);

    // -- Scale heights -----------------------------------------------------------
    TestTrue(TEXT("Earth air at 255 K and 1 g: about 7.5 km"), Between(AirFacts::ScaleHeightKm(EAirMix::NitrogenOxygen, 255.0, 1.0), 7.3, 7.6));
    TestTrue(TEXT("H2/He on a 2.6 g giant at 110 K: about 15 km"), Between(AirFacts::ScaleHeightKm(EAirMix::HydrogenHelium, 110.0, 318.0 / 121.0), 15.0, 16.0));

    // -- The nadir guarantee as a pressure (decision 4) ------------------------------
    const double N2Ceiling = AirFacts::PressureCeilingBar(EAirMix::NitrogenOxygen, 1.0, 1.0);
    const double CO2Ceiling = AirFacts::PressureCeilingBar(EAirMix::CarbonDioxide, 1.0, 1.0);
    const double H2Ceiling = AirFacts::PressureCeilingBar(EAirMix::HydrogenHelium, 1.0, 1.0);
    AddInfo(FString::Printf(TEXT("ceilings at 1 g: N2/O2 %.4f bar, CO2 %.4f, H2/He %.4f"), N2Ceiling, CO2Ceiling, H2Ceiling));
    TestTrue(TEXT("N2/O2 about 1.8 bar"), Between(N2Ceiling, 1.78, 1.82));
    TestTrue(TEXT("CO2 about 1.17 bar"), Between(CO2Ceiling, 1.15, 1.19));
    TestTrue(TEXT("H2/He about 0.9 bar"), Between(H2Ceiling, 0.88, 0.91));
    for (const EAirMix Mix : Mixes)
    {
        for (const double G : {0.6, 1.0, 3.0})
        {
            const double Ceiling = AirFacts::PressureCeilingBar(Mix, G, 1.0);
            TestTrue(FString::Printf(TEXT("%s at %.1f g: straight down at 450 nm the ceiling is the guarantee's 0.5"), AirFacts::Name(Mix), G),
                FMath::Abs(AirFacts::NadirTau450(Mix, Ceiling, G) - GenGuarantees::MaxNadirTau450) < 1.0e-9);
        }
        TestTrue(FString::Printf(TEXT("%s: a heavier world holds more, the same pressure being less column"), AirFacts::Name(Mix)),
            FMath::Abs(AirFacts::PressureCeilingBar(Mix, 2.0, 1.0) / AirFacts::PressureCeilingBar(Mix, 1.0, 1.0) - 2.0) < 1.0e-12);
        TestTrue(FString::Printf(TEXT("%s: a world that barely holds its gas holds less of it"), AirFacts::Name(Mix)),
            FMath::Abs(AirFacts::PressureCeilingBar(Mix, 1.0, 0.25) / AirFacts::PressureCeilingBar(Mix, 1.0, 1.0) - 0.25) < 1.0e-12);
    }

    // -- A giant's disc (decision 2) --------------------------------------------------
    const double Light = AirFacts::GiantDiscPressureBar(15.0 / 121.0);
    const double Jupiter = AirFacts::GiantDiscPressureBar(318.0 / 121.0);
    const double Heavy = AirFacts::GiantDiscPressureBar(3000.0 / 121.0);
    AddInfo(FString::Printf(TEXT("giants' discs: 15 M_E %.4f bar, 318 M_E %.3f, 3,000 M_E %.2f"), Light, Jupiter, Heavy));
    TestTrue(TEXT("a 15 M_E giant's disc is its 0.11 bar level"), Between(Light, 0.10, 0.12));
    TestTrue(TEXT("a Jupiter's about 2.4 bar"), Between(Jupiter, 2.3, 2.45));
    TestTrue(TEXT("a 3,000 M_E giant's about 22 bar"), Between(Heavy, 21.0, 23.0));

    // -- The smooth ceiling -------------------------------------------------------------
    TestTrue(TEXT("far under the ceiling, the draw stands"), FMath::Abs(AirFacts::SmoothCeiling(0.01, 1.0) / 0.01 - 1.0) < 1.0e-7);
    TestTrue(TEXT("far over it, the ceiling"), FMath::Abs(AirFacts::SmoothCeiling(100.0, 1.0) - 1.0) < 1.0e-7);
    TestTrue(TEXT("at it, 2^-1/4 of it: the curve bends, it does not clip"),
        FMath::Abs(AirFacts::SmoothCeiling(1.0, 1.0) - std::pow(2.0, -0.25)) < 1.0e-12);
    double Previous = 0.0;
    bool bRises = true;
    bool bUnder = true;
    for (double Drawn = 0.05; Drawn < 20.0; Drawn *= 1.1)
    {
        const double P = AirFacts::SmoothCeiling(Drawn, 1.8);
        bRises &= P > Previous;
        bUnder &= P < FMath::Min(Drawn, 1.8);
        Previous = P;
    }
    TestTrue(TEXT("rising with the draw"), bRises);
    TestTrue(TEXT("and always under both the draw and the ceiling"), bUnder);
    TestEqual(TEXT("no ceiling, no air"), AirFacts::SmoothCeiling(1.0, 0.0), 0.0);

    // -- No air ---------------------------------------------------------------------------
    TestEqual(TEXT("no mix has no scale height"), AirFacts::ScaleHeightKm(EAirMix::None, 255.0, 1.0), 0.0);
    TestEqual(TEXT("nor any depth"), AirFacts::NadirTau450(EAirMix::None, 1.0, 1.0), 0.0);
    TestEqual(TEXT("nor any retention"), AirFacts::Retention(EAirMix::None, 1.0, 1.0, 255.0), 0.0);
    TestEqual(TEXT("and is called none"), FString(AirFacts::Name(EAirMix::None)), FString(TEXT("none")));
    return true;
}

#endif
