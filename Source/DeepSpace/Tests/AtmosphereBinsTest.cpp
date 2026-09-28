#include "Misc/AutomationTest.h"
#include "Atmosphere/AtmosphereBins.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereBinFoldsTest, "DeepSpace.Atmosphere.BinFolds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereBinsCarryTheSpectrumTest, "DeepSpace.Atmosphere.BinsCarryTheSpectrum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereBinsTestLocal
{
    using AtmosphereReference::FSpectrum;
    using SkyColour::Spectral::Count;

    /** How far Got is from Want, over max(Relative |Want|, Absolute). */
    double Over(double Got, double Want, double Relative, double Absolute)
    {
        return std::isfinite(Got) ? FMath::Abs(Got - Want) / FMath::Max(Relative * FMath::Abs(Want), Absolute) : 1.0e30;
    }
}

bool FAtmosphereBinFoldsTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBinsTestLocal;
    using AtmosphereBins::First;

    // The partition: every wavelength in exactly one bin, in order.
    bool bContiguous = First[0] == 0 && First[AtmosphereBins::Count] == Count;
    for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
    {
        bContiguous &= First[Bin + 1] > First[Bin];
    }
    TestTrue(TEXT("the bins cover the 16 wavelengths, each once, none empty"), bContiguous);

    for (const double Kelvin : {0.0, 1000.0, 2000.0, 2566.0, 5772.0, 15000.0, 40000.0})
    {
        const FSpectrum Star = AtmosphereReference::StarSpectrum(Kelvin);
        const FVector3d Own = AtmosphereReference::ToLinearSrgb(Star);

        // The folds are the star's light, split: they sum to its colour.
        FVector3d Sum = FVector3d::ZeroVector;
        bool bWeights = true;
        for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
        {
            Sum += AtmosphereBins::Fold(Star, Bin);
        }
        for (int32 I = 0; I < Count; ++I)
        {
            const double W = AtmosphereBins::Weight(Star, I);
            bWeights &= std::isfinite(W) && W > 0.0;
        }
        TestTrue(FString::Printf(TEXT("%.0f K: the folds sum to the star's colour"), Kelvin), (Sum - Own).GetAbsMax() <= 1.0e-12 * Own.GetAbsMax());
        TestTrue(FString::Printf(TEXT("%.0f K: every wavelength weighs something, and something finite"), Kelvin), bWeights);

        // Nothing lost is all of it kept, exactly.
        AtmosphereBins::FBins Ones;
        for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
        {
            Ones.Value[Bin] = 1.0;
        }
        const FVector3d Kept = AtmosphereBins::FoldThrough(Star, Ones);
        TestTrue(FString::Printf(TEXT("%.0f K: a clear path keeps all of the star's light"), Kelvin), (Kept - FVector3d::OneVector).GetAbsMax() < 1.0e-12);

        // A spectrum constant within each bin is carried exactly: averaged
        // to itself, and folded to what the reference integrates.
        FSpectrum Stepped;
        for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
        {
            for (int32 I = First[Bin]; I < First[Bin + 1]; ++I)
            {
                Stepped.Value[I] = 0.1 * (Bin + 1);
            }
        }
        const AtmosphereBins::FBins Averaged = AtmosphereBins::Average(Star, Stepped);
        bool bAveraged = true;
        for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
        {
            bAveraged &= FMath::Abs(Averaged.Value[Bin] - 0.1 * (Bin + 1)) < 1.0e-12;
        }
        TestTrue(FString::Printf(TEXT("%.0f K: a stepped spectrum averages to its steps"), Kelvin), bAveraged);
        const FVector3d Light = AtmosphereBins::FoldLight(Star, Averaged);
        const FVector3d Want = AtmosphereReference::Colour(Star, Stepped);
        TestTrue(FString::Printf(TEXT("%.0f K: and folds to the reference's colour of it"), Kelvin), (Light - Want).GetAbsMax() <= 1.0e-12 * Want.GetAbsMax());
        // The reference's own fold (ChannelAverage), wherever the star has a
        // channel's light to keep: under 2,000 K its blue is below the
        // floor, where the fold keeps all of nothing and the reference a
        // fraction of it.
        if (Kelvin >= 2000.0)
        {
            const FVector3d Through = AtmosphereBins::FoldThrough(Star, Averaged);
            const FVector3d ThroughWant = AtmosphereReference::ChannelAverage(Star, Stepped);
            TestTrue(FString::Printf(TEXT("%.0f K: and to its transmittance"), Kelvin), (Through - ThroughWant).GetAbsMax() < 1.0e-12);
        }
    }
    return true;
}

bool FAtmosphereBinsCarryTheSpectrumTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereBinsTestLocal;
    using namespace AtmosphereTestFixtures;

    // The bins' one approximation, alone: within a bin one optical depth
    // stands for several. Decision 1's airs under the home star, procgen's
    // coolest star and the Sun, along every path from the ground --
    // straight up to the horizon, each constituent through its own exact
    // column -- the binned transmittance against the spectrum's, each
    // channel within decision 1's 5% or 1e-3, as a display shows it: a
    // channel below zero counts as zero on both sides, and how often that
    // changed a value is counted and printed (planning note 17).
    double Worst = 0.0;
    FString Where;
    int32 Checked = 0;
    int32 Clamped = 0;
    for (const FNamedAir& Named : Extremes())
    {
        const AtmosphereReference::FSpectralAir Air = AtmosphereReference::Spectral(Named.Air);
        FSpectrum GasTau;
        for (int32 I = 0; I < Count; ++I)
        {
            GasTau.Value[I] = Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I];
        }
        for (const double Kelvin : {HomeStarK, CoolestStarK, SunK})
        {
            const FSpectrum Star = AtmosphereReference::StarSpectrum(Kelvin);
            const AtmosphereBins::FBins Gas = AtmosphereBins::Average(Star, GasTau);
            const AtmosphereBins::FBins Aerosol = AtmosphereBins::Average(Star, Air.AerosolExtinct);
            for (const double FromZenith : {0.0, 60.0, 80.0, 85.0, 88.0, 90.0})
            {
                // Airmasses: each constituent's column along the path over
                // its column straight up.
                const double Cos = FMath::Cos(FMath::DegreesToRadians(FromZenith));
                const double GasAirmass = AtmosphereReference::ColumnExact(1.0, Cos, Air.GasH) / Air.GasH;
                const double AerosolAirmass = AtmosphereReference::ColumnExact(1.0, Cos, Air.AerosolH) / Air.AerosolH;
                FSpectrum Through;
                for (int32 I = 0; I < Count; ++I)
                {
                    Through.Value[I] = std::exp(-(GasAirmass * GasTau.Value[I] + AerosolAirmass * Air.AerosolExtinct.Value[I]));
                }
                AtmosphereBins::FBins BinThrough;
                for (int32 Bin = 0; Bin < AtmosphereBins::Count; ++Bin)
                {
                    BinThrough.Value[Bin] = std::exp(-(GasAirmass * Gas.Value[Bin] + AerosolAirmass * Aerosol.Value[Bin]));
                }
                const FVector3d Want = AtmosphereReference::ChannelAverage(Star, Through);
                const FVector3d Got = AtmosphereBins::FoldThrough(Star, BinThrough);
                for (int32 C = 0; C < 3; ++C)
                {
                    ++Checked;
                    Clamped += (Got[C] < 0.0 || Want[C] < 0.0) ? 1 : 0;
                    const double Miss = Over(FMath::Max(Got[C], 0.0), FMath::Max(Want[C], 0.0), 0.05, 1.0e-3);
                    if (Miss > Worst)
                    {
                        Worst = Miss;
                        Where = FString::Printf(TEXT("%s, %.0f K, %.0f degrees from the zenith, channel %d: %.5f against %.5f"),
                            Named.Name, Kelvin, FromZenith, C, Got[C], Want[C]);
                    }
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d binned transmittances, %d changed by the gamut clamp; the worst at %.2f of its allowance: %s"), Checked, Clamped, Worst, *Where));
    TestEqual(TEXT("six airs, three stars, six paths, three channels"), Checked, 6 * 3 * 6 * 3);
    TestTrue(TEXT("every channel within 5% or 1e-3 of the spectrum's"), Worst <= 1.0);
    return true;
}

#endif
