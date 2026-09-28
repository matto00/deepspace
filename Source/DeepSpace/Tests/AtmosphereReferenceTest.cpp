#include "Misc/AutomationTest.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereReferenceKnownValuesTest,
    "DeepSpace.Atmosphere.ReferenceKnownValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereReferenceTestLocal
{
    double Saturation(const FVector3d& Colour)
    {
        const double Max = FMath::Max3(Colour.X, Colour.Y, Colour.Z);
        const double Min = FMath::Max(FMath::Min3(Colour.X, Colour.Y, Colour.Z), 0.0);
        return Max > 0.0 ? 1.0 - Min / Max : 0.0;
    }
}

bool FAtmosphereReferenceKnownValuesTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereReferenceTestLocal;
    using namespace AtmosphereTestFixtures;

    // -- Earth's air from its physics (decision 1's first known value) -----------
    const double Column = AtmosphereReference::ColumnMoleculesPerM2(1.01325, 9.80665, 28.9647);
    const double Tau = AtmosphereReference::RayleighCrossSectionAirM2(550.0) * Column;
    AddInfo(FString::Printf(TEXT("Earth's air: %.4e molecules per m^2, Rayleigh tau at 550 nm %.4f"), Column, Tau));
    TestTrue(TEXT("Earth's Rayleigh optical depth at 550 nm is 0.097 within 3%"), FMath::Abs(Tau / 0.097 - 1.0) < 0.03);

    // -- The column itself --------------------------------------------------------
    const double H = 7.463e5 / EarthRadiusCm;
    TestTrue(TEXT("straight up from the surface the column is one scale height"),
        FMath::Abs(AtmosphereReference::ColumnExact(1.0, 1.0, H) / H - 1.0) < 1.0e-6);
    TestTrue(TEXT("a ray pointing into the ground has no column: -1"),
        AtmosphereReference::ColumnExact(1.0, -0.1, H) < 0.0);

    // -- Earth's sky under the Sun --------------------------------------------------
    const FReferenceAir Earth(EarthAir(), SunK);
    TestTrue(TEXT("Earth's air has air"), Earth.HasAir());

    // The noon zenith: straight up under a sun 45 degrees high, not
    // overhead -- a sun at the zenith would put its own aureole there.
    FReferenceAir::FRay Zenith;
    Zenith.Eye = FVector3d(0.0, 0.0, 1.0 + 1.0e-6);
    Zenith.Direction = FVector3d(0.0, 0.0, 1.0);
    Zenith.Sun = FVector3d(std::sqrt(0.5), 0.0, std::sqrt(0.5));
    const FVector3d Sky = Earth.Trace(Zenith).InScatter;
    AddInfo(FString::Printf(TEXT("noon zenith under the Sun: (%.4f, %.4f, %.4f)"), Sky.X, Sky.Y, Sky.Z));
    TestTrue(TEXT("the noon zenith is blue over green over red"), Sky.Z > Sky.Y && Sky.Y > Sky.X && Sky.X > 0.0);

    FReferenceAir::FRay Horizon = Zenith;
    Horizon.Direction = FVector3d(0.0, 1.0, 0.0);
    const FVector3d Low = Earth.Trace(Horizon).InScatter;
    AddInfo(FString::Printf(TEXT("the horizon at right angles to the sun: (%.4f, %.4f, %.4f), saturation %.3f against the zenith's %.3f"),
        Low.X, Low.Y, Low.Z, Saturation(Low), Saturation(Sky)));
    TestTrue(TEXT("the horizon is whiter than the zenith"), Saturation(Low) < Saturation(Sky));

    const double Setting = FMath::DegreesToRadians(0.5);
    const FVector3d Sunset = Earth.SunThrough(FVector3d(0.0, 0.0, 1.0), FVector3d(std::cos(Setting), 0.0, std::sin(Setting)));
    const FVector3d Noon = Earth.SunThrough(FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, 1.0));
    AddInfo(FString::Printf(TEXT("the sun's light through the air at noon (%.3f, %.3f, %.3f), at half a degree (%.4f, %.4f, %.4f)"),
        Noon.X, Noon.Y, Noon.Z, Sunset.X, Sunset.Y, Sunset.Z));
    TestTrue(TEXT("a sun at the horizon transmits red over blue"), Sunset.X > Sunset.Z);
    TestTrue(TEXT("and less of its blue than at noon by half and more"), Sunset.Z < 0.5 * Noon.Z);

    const FVector3d Night = Earth.SunThrough(FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, -1.0));
    TestTrue(TEXT("the ground's own shadow: a sun below the ground reaches nothing"), Night.IsZero());

    // -- No air, nothing ---------------------------------------------------------------
    const FReferenceAir None(Airless(), SunK);
    const FReferenceAir::FResult Nothing = None.Trace(Zenith);
    TestFalse(TEXT("an airless world has no air"), None.HasAir());
    TestTrue(TEXT("and adds nothing and dims nothing"),
        Nothing.InScatter.IsZero() && Nothing.Transmittance == FVector3d::OneVector);
    return true;
}

#endif
