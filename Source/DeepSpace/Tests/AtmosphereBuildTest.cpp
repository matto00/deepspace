#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereChannelFitTest,
    "DeepSpace.Atmosphere.ChannelFit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereStarTemperatureExtremesTest,
    "DeepSpace.Atmosphere.StarTemperatureExtremes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereBuildTestLocal
{
    double Luminance(const FVector3d& Colour)
    {
        return 0.2126 * Colour.X + 0.7152 * Colour.Y + 0.0722 * Colour.Z;
    }

    bool FiniteAndNonNegative(const FVector3d& V)
    {
        return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z) && V.X >= 0.0 && V.Y >= 0.0 && V.Z >= 0.0;
    }

    bool Same(const FAtmosphereAir& A, const FAtmosphereAir& B)
    {
        return A.GasScatter == B.GasScatter && A.GasExtinct == B.GasExtinct && A.AerosolScatter == B.AerosolScatter
            && A.AerosolExtinct == B.AerosolExtinct && A.GasH == B.GasH && A.AerosolH == B.AerosolH && A.AerosolG == B.AerosolG && A.Top == B.Top;
    }
}

bool FAtmosphereChannelFitTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBuildTestLocal;
    using namespace AtmosphereTestFixtures;

    const FAtmosphere Earth = FAtmosphere::Build(EarthAir(), SunK);
    const FAtmosphereAir& A = Earth.GetAir();
    TestTrue(TEXT("Earth's air has air"), Earth.HasAir());
    TestTrue(TEXT("its top is ten of its gas's scale heights"), FMath::IsNearlyEqual(A.Top, 10.0 * A.GasH, 1.0e-15));

    // The gas scatters blue most, under a G star.
    TestTrue(FString::Printf(TEXT("under the Sun the gas scatters blue over green over red (%.4f, %.4f, %.4f per radius)"),
        A.GasScatter.X, A.GasScatter.Y, A.GasScatter.Z), A.GasScatter.Z > A.GasScatter.Y && A.GasScatter.Y > A.GasScatter.X);

    // Exact at the nadir column: the fitted extinction lets through, straight
    // down, exactly what the spectrum does.
    const AtmosphereReference::FSpectralAir Spectral = AtmosphereReference::Spectral(EarthAir());
    const AtmosphereReference::FSpectrum Star = AtmosphereReference::StarSpectrum(SunK);
    AtmosphereReference::FSpectrum Through;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        Through.Value[I] = std::exp(-(Spectral.GasScatter.Value[I] + Spectral.GasAbsorb.Value[I] + Spectral.AerosolExtinct.Value[I]));
    }
    const FVector3d Nadir = AtmosphereReference::ChannelAverage(Star, Through);
    for (int32 C = 0; C < 3; ++C)
    {
        const double Fitted = std::exp(-(A.GasExtinct[C] * A.GasH + A.AerosolExtinct[C] * A.AerosolH));
        TestTrue(FString::Printf(TEXT("channel %d: the fit lets through %.9f straight down, the spectrum %.9f"), C, Fitted, Nadir[C]),
            FMath::Abs(Fitted - Nadir[C]) < 1.0e-9);
    }

    // Exact in the thin limit: the scatter is the star's light times the
    // spectrum's, integrated.
    const FVector3d Thin = AtmosphereReference::Colour(Star, Spectral.GasScatter);
    TestTrue(TEXT("the gas's scatter is the thin limit's, star's colour and all"),
        (A.GasScatter * A.GasH - Thin).GetAbsMax() < 1.0e-12);

    // Linear in the column.
    const FAtmosphere Double = FAtmosphere::Build(NitrogenOxygen(2.0), SunK);
    TestTrue(TEXT("twice the pressure, twice the scatter"),
        (Double.GetAir().GasScatter - 2.0 * A.GasScatter).GetAbsMax() < 1.0e-9 * A.GasScatter.GetAbsMax());

    // The star's colour, and the white air that is the scatter without it.
    TestTrue(FString::Printf(TEXT("the star's colour has luminance 1 (%.6f)"), Luminance(Earth.GetStarColour())),
        FMath::Abs(Luminance(Earth.GetStarColour()) - 1.0) < 1.0e-3);
    for (int32 C = 0; C < 3; ++C)
    {
        TestTrue(FString::Printf(TEXT("channel %d: white scatter times the star's colour is the scatter"), C),
            FMath::Abs(Earth.GetWhiteAir().GasScatter[C] * Earth.GetStarColour()[C] - A.GasScatter[C]) <= 1.0e-9 * A.GasScatter[C]);
    }
    TestTrue(TEXT("the white air's extinction is the air's"), Earth.GetWhiteAir().GasExtinct == A.GasExtinct);

    // The star is in the scatter: a red dwarf's sky scatters redder light.
    // Held by value: GetAir() returns a reference into the FAtmosphere, and a
    // reference into a temporary dies with the statement.
    const FAtmosphere HomeAir = FAtmosphere::Build(EarthAir(), HomeStarK);
    const FAtmosphereAir& Home = HomeAir.GetAir();
    TestTrue(TEXT("under the home star the gas's scatter is redder than under the Sun"),
        Home.GasScatter.X / Home.GasScatter.Z > A.GasScatter.X / A.GasScatter.Z);

    // No air.
    const FAtmosphere None = FAtmosphere::Build(Airless(), SunK);
    TestFalse(TEXT("an airless world has no air"), None.HasAir());
    TestTrue(TEXT("and no coefficients"), None.GetAir().GasScatter.IsZero() && None.GetAir().AerosolExtinct.IsZero() && None.GetAir().Top == 0.0);
    TestTrue(TEXT("and no table"), None.GetTable().IsEmpty());
    return true;
}

bool FAtmosphereStarTemperatureExtremesTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBuildTestLocal;
    using namespace AtmosphereTestFixtures;

    // Review focus 4: temperatures past the blackbody's range, and a 1,000 K
    // star with almost no blue for the fit to divide by.
    const double Kelvins[] = {0.0, 500.0, 1000.0, 2000.0, 2566.0, 15000.0, 40000.0};
    for (const double Kelvin : Kelvins)
    {
        for (const FAirSpec& Spec : {EarthAir(), CarbonDioxide(CarbonDioxideCeilingBar), Giant()})
        {
            const FAtmosphere Air = FAtmosphere::Build(Spec, Kelvin);
            const FAtmosphereAir& A = Air.GetAir();
            const FAtmosphereAir& W = Air.GetWhiteAir();
            TestTrue(FString::Printf(TEXT("%.0f K: every coefficient finite and not negative"), Kelvin),
                FiniteAndNonNegative(A.GasScatter) && FiniteAndNonNegative(A.GasExtinct) && FiniteAndNonNegative(A.AerosolScatter)
                && FiniteAndNonNegative(A.AerosolExtinct) && FiniteAndNonNegative(W.GasScatter) && FiniteAndNonNegative(W.AerosolScatter));
            TestTrue(FString::Printf(TEXT("%.0f K: the star's colour has luminance 1"), Kelvin),
                FMath::Abs(Luminance(Air.GetStarColour()) - 1.0) < 1.0e-3);
        }
    }
    TestTrue(TEXT("clamped as Blackbody clamps: 0 K and 500 K build as 1,000 K"),
        Same(FAtmosphere::Build(EarthAir(), 0.0).GetAir(), FAtmosphere::Build(EarthAir(), 1000.0).GetAir())
        && Same(FAtmosphere::Build(EarthAir(), 500.0).GetAir(), FAtmosphere::Build(EarthAir(), 1000.0).GetAir()));
    TestTrue(TEXT("and 40,000 K as 15,000 K"),
        Same(FAtmosphere::Build(EarthAir(), 40000.0).GetAir(), FAtmosphere::Build(EarthAir(), 15000.0).GetAir()));
    return true;
}

#endif
