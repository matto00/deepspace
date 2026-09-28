#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereBinnedAirTest,
    "DeepSpace.Atmosphere.BinnedAir",
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

    bool FiniteAndNonNegative(const AtmosphereBins::FBins& B)
    {
        bool bGood = true;
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            bGood &= std::isfinite(B.Value[K]) && B.Value[K] >= 0.0;
        }
        return bGood;
    }

    bool Same(const AtmosphereBins::FBins& A, const AtmosphereBins::FBins& B)
    {
        bool bSame = true;
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            bSame &= A.Value[K] == B.Value[K];
        }
        return bSame;
    }

    bool Same(const FAtmosphereAir& A, const FAtmosphereAir& B)
    {
        bool bSame = Same(A.GasScatter, B.GasScatter) && Same(A.GasExtinct, B.GasExtinct) && Same(A.AerosolScatter, B.AerosolScatter)
            && Same(A.AerosolExtinct, B.AerosolExtinct) && A.GasH == B.GasH && A.AerosolH == B.AerosolH && A.AerosolG == B.AerosolG && A.Top == B.Top;
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            bSame &= A.Fold[K] == B.Fold[K];
        }
        return bSame;
    }
}

bool FAtmosphereBinnedAirTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBuildTestLocal;
    using namespace AtmosphereTestFixtures;

    // No tables: the coefficients are the question here, and a table is a
    // second a world.
    const FAtmosphere Earth = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::None);
    const FAtmosphereAir& A = Earth.GetAir();
    TestTrue(TEXT("Earth's air has air"), Earth.HasAir());
    TestTrue(TEXT("its top is ten of its gas's scale heights"), FMath::IsNearlyEqual(A.Top, 10.0 * A.GasH, 1.0e-15));

    // The bins are colourless and the gas's scattering falls from the blue
    // bin to the red, as lambda^-4 does.
    bool bFalls = true;
    for (int32 K = 1; K < AtmosphereBins::Count; ++K)
    {
        bFalls &= A.GasScatter.Value[K] < A.GasScatter.Value[K - 1];
    }
    TestTrue(TEXT("the gas scatters less in every redder bin"), bFalls);

    // Each bin is its wavelengths' own air, averaged as AtmosphereBins
    // averages, per radius.
    const AtmosphereReference::FSpectralAir Spectral = AtmosphereReference::Spectral(EarthAir());
    const AtmosphereReference::FSpectrum Star = AtmosphereReference::StarSpectrum(SunK);
    AtmosphereReference::FSpectrum GasTau;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        GasTau.Value[I] = Spectral.GasScatter.Value[I] + Spectral.GasAbsorb.Value[I];
    }
    const AtmosphereBins::FBins Scatter = AtmosphereBins::Average(Star, Spectral.GasScatter);
    const AtmosphereBins::FBins Extinct = AtmosphereBins::Average(Star, GasTau);
    const AtmosphereBins::FBins Aerosol = AtmosphereBins::Average(Star, Spectral.AerosolExtinct);
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        TestTrue(FString::Printf(TEXT("bin %d: the gas's scattering and extinction, and the aerosol's, are the bin's average over its scale height"), K),
            FMath::Abs(A.GasScatter.Value[K] * A.GasH - Scatter.Value[K]) <= 1.0e-12 * Scatter.Value[K]
            && FMath::Abs(A.GasExtinct.Value[K] * A.GasH - Extinct.Value[K]) <= 1.0e-12 * Extinct.Value[K]
            && FMath::Abs(A.AerosolExtinct.Value[K] * A.AerosolH - Aerosol.Value[K]) <= 1.0e-12 * Aerosol.Value[K]);
        TestTrue(FString::Printf(TEXT("bin %d: its fold is the star's light there"), K), A.Fold[K] == AtmosphereBins::Fold(Star, K));
    }

    // The ozone absorbs: the gas takes out more than it scatters where the
    // Chappuis band is, and exactly what it scatters where it is not.
    TestTrue(TEXT("in the 600 nm bin the gas's extinction exceeds its scattering"),
        A.GasExtinct.Value[6] > 1.01 * A.GasScatter.Value[6]);

    // Linear in the column.
    const FAtmosphere Double = FAtmosphere::Build(NitrogenOxygen(2.0), SunK, EAtmosphereTable::None);
    bool bLinear = true;
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        bLinear &= FMath::Abs(Double.GetAir().GasScatter.Value[K] - 2.0 * A.GasScatter.Value[K]) <= 1.0e-9 * A.GasScatter.Value[K];
    }
    TestTrue(TEXT("twice the pressure, twice the scatter"), bLinear);

    // The star is in the fold and nowhere else: its colour is the folds'
    // sum, at unit luminance.
    FVector3d Folds = FVector3d::ZeroVector;
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        Folds += A.Fold[K];
    }
    TestTrue(FString::Printf(TEXT("the star's colour has luminance 1 (%.6f)"), Luminance(Earth.GetStarColour())),
        FMath::Abs(Luminance(Earth.GetStarColour()) - 1.0) < 1.0e-3);
    TestTrue(TEXT("and is the sum of the folds"), (Folds - Earth.GetStarColour()).GetAbsMax() <= 1.0e-12);

    // A red dwarf's air folds redder light, though its bins scatter alike.
    const FAtmosphere HomeAir = FAtmosphere::Build(EarthAir(), HomeStarK, EAtmosphereTable::None);
    const FAtmosphereAir& Home = HomeAir.GetAir();
    TestTrue(TEXT("under the home star the red bin's fold outweighs the blue bin's more than under the Sun"),
        Home.Fold[AtmosphereBins::Count - 1].X / Home.Fold[0].Z > A.Fold[AtmosphereBins::Count - 1].X / A.Fold[0].Z);

    // No air.
    const FAtmosphere None = FAtmosphere::Build(Airless(), SunK);
    TestFalse(TEXT("an airless world has no air"), None.HasAir());
    TestTrue(TEXT("and no coefficients"), None.GetAir().GasScatter.Value[0] == 0.0 && None.GetAir().AerosolExtinct.Value[0] == 0.0 && None.GetAir().Top == 0.0);
    TestTrue(TEXT("and no table"), None.GetTable().IsEmpty());
    return true;
}

bool FAtmosphereStarTemperatureExtremesTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBuildTestLocal;
    using namespace AtmosphereTestFixtures;

    // Review focus 4: temperatures past the blackbody's range, and a 1,000 K
    // star with almost no blue in its bins' weights.
    const double Kelvins[] = {0.0, 500.0, 1000.0, 2000.0, 2566.0, 15000.0, 40000.0};
    for (const double Kelvin : Kelvins)
    {
        for (const FAirSpec& Spec : {EarthAir(), CarbonDioxide(CarbonDioxideCeilingBar), Giant()})
        {
            const FAtmosphere Air = FAtmosphere::Build(Spec, Kelvin, EAtmosphereTable::None);
            const FAtmosphereAir& A = Air.GetAir();
            bool bFolds = true;
            for (int32 K = 0; K < AtmosphereBins::Count; ++K)
            {
                bFolds &= std::isfinite(A.Fold[K].X) && std::isfinite(A.Fold[K].Y) && std::isfinite(A.Fold[K].Z);
            }
            TestTrue(FString::Printf(TEXT("%.0f K: every coefficient finite and not negative, every fold finite"), Kelvin),
                FiniteAndNonNegative(A.GasScatter) && FiniteAndNonNegative(A.GasExtinct) && FiniteAndNonNegative(A.AerosolScatter)
                && FiniteAndNonNegative(A.AerosolExtinct) && bFolds);
            TestTrue(FString::Printf(TEXT("%.0f K: the star's colour has luminance 1"), Kelvin),
                FMath::Abs(Luminance(Air.GetStarColour()) - 1.0) < 1.0e-3);
        }
    }
    TestTrue(TEXT("clamped as Blackbody clamps: 0 K and 500 K build as 1,000 K"),
        Same(FAtmosphere::Build(EarthAir(), 0.0, EAtmosphereTable::None).GetAir(), FAtmosphere::Build(EarthAir(), 1000.0, EAtmosphereTable::None).GetAir())
        && Same(FAtmosphere::Build(EarthAir(), 500.0, EAtmosphereTable::None).GetAir(), FAtmosphere::Build(EarthAir(), 1000.0, EAtmosphereTable::None).GetAir()));
    TestTrue(TEXT("and 40,000 K as 15,000 K"),
        Same(FAtmosphere::Build(EarthAir(), 40000.0, EAtmosphereTable::None).GetAir(), FAtmosphere::Build(EarthAir(), 15000.0, EAtmosphereTable::None).GetAir()));
    return true;
}

#endif
