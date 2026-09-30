#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Surface/WorldRelief.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 3: a crater band as a sum of compact kernels, one per
 * kept site over the 2 x 2 x 2 corners that can reach the sample (the 3 x 3
 * x 3 round it until the frame ruling's profile, the same sums to the bit:
 * DeepSpace.Surface.CraterKernelCorners), so craters are a
 * continuous height -- no cliff where a kept crater's rim meets the bisector
 * with a dropped neighbour, which Voronoi F1 had and the material hid. The
 * bounds, the slope bound and the gradient hold with craters in.
 *
 * Craters add to the band sum S under the ruled normalisation (planning note
 * 3, RULED): Height = PeakCm x PeakCap(S / S_max), S_max still the detail
 * bands' measured maximum, so MaxHeightCm is exactly PeakCm by the cap and
 * the drawn peaks still reach it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefCratersContinuousTest, "DeepSpace.Surface.WorldRelief.CratersContinuous",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefCraterGradientTest, "DeepSpace.Surface.WorldRelief.CraterGradient",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefCraterBoundsTest, "DeepSpace.Surface.WorldRelief.CraterBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefCratersLocal
{
    /** Bare rock, craters kept whole: every crater band is in the height. */
    FWorldReliefParams Barren(double Cratering = 1.0)
    {
        FWorldReliefParams Params;
        Params.SeedOffset = FVector3d(12.5, 40.25, 7.75);
        Params.RadiusCm = 0.9 * UniverseUnits::CmPerEarthRadius;
        Params.PeakCm = 6.4e5;
        Params.Cratering = Cratering;
        Params.Ground = EGround::Solid;
        return Params;
    }

    /** A test's sampling, not a world's draw: uniform over the sphere. */
    FVector3d RandomDirection(FRandomStream& Random)
    {
        return FVector3d(Random.GetUnitVector());
    }

    /** A fixed direction where the crater sum is not zero (if a change of
     *  constants ever makes it zero here, move it and say so). */
    FVector3d From0()
    {
        return FVector3d(0.3, -0.5, 0.81).GetSafeNormal();
    }
}

bool FWorldReliefCratersContinuousTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefCratersLocal;
    const FWorldRelief Relief(Barren());
    const double R = Relief.GetParams().RadiusCm;
    // Along twenty great-circle arcs of 0.2 radian, each crossing a couple
    // of the coarsest crater band's cells and many of the finer ones', in
    // steps of 1e-5 radian (57 m): a cliff shows as a step steeper than the
    // slope bound allows -- the coarse bands' cliffs were hundreds of metres.
    FRandomStream Random(3);
    const double Step = 1.0e-5;
    double Worst = 0.0;
    for (int32 Arc = 0; Arc < 20; ++Arc)
    {
        const FVector3d From = RandomDirection(Random);
        const FVector3d Across = FVector3d::CrossProduct(From, RandomDirection(Random)).GetSafeNormal();
        double Previous = Relief.Height(From, 0.0);
        for (int32 I = 1; I < 20000; ++I)
        {
            const double Angle = I * Step;
            const FVector3d D = From * FMath::Cos(Angle) + Across * FMath::Sin(Angle);
            const double H = Relief.Height(D, 0.0);
            Worst = FMath::Max(Worst, FMath::Abs(H - Previous) / (R * Step));
            Previous = H;
        }
    }
    TestTrue(FString::Printf(TEXT("no step anywhere steeper than MaxSlope (worst %.4f of %.4f)"), Worst, Relief.MaxSlope()),
             Worst <= Relief.MaxSlope());
    TestTrue(TEXT("and the craters are really in the ground: with none kept, the heights differ"),
             Relief.Height(From0(), 0.0) != FWorldRelief(Barren(0.0)).Height(From0(), 0.0));
    return true;
}

bool FWorldReliefCraterGradientTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefCratersLocal;
    const FWorldRelief Relief(Barren());
    FRandomStream Random(11);
    const double H = 1.0e-8;
    int32 Agree = 0;
    int32 CraterAgree = 0;
    const int32 Trials = 2000;
    for (int32 Trial = 0; Trial < Trials; ++Trial)
    {
        const FVector3d D = RandomDirection(Random);
        const FVector3d T = FVector3d::CrossProduct(D, RandomDirection(Random)).GetSafeNormal();
        {
            FVector3d Grad;
            Relief.HeightAndGradient(D, Grad, 0.0);
            const FVector3d Along = Grad - D * FVector3d::DotProduct(Grad, D);
            const double Numeric = (Relief.Height((D + T * H).GetSafeNormal(), 0.0) - Relief.Height((D - T * H).GetSafeNormal(), 0.0)) / (2.0 * H);
            const double Analytic = FVector3d::DotProduct(Along, T);
            const double Tolerance = 0.01 * FMath::Abs(Analytic) + 1.0e-4 * Relief.GetParams().RadiusCm * Relief.MaxSlope();
            Agree += FMath::Abs(Numeric - Analytic) <= Tolerance ? 1 : 0;
        }
        {
            // The craters alone, so the detail's larger slope cannot hide them.
            FVector3d Grad;
            Relief.CraterSum(D, 0.0, &Grad);
            const double Numeric = (Relief.CraterSum(D + T * H, 0.0) - Relief.CraterSum(D - T * H, 0.0)) / (2.0 * H);
            const double Analytic = FVector3d::DotProduct(Grad, T);
            CraterAgree += FMath::Abs(Numeric - Analytic) <= 0.01 * FMath::Abs(Analytic) + 1.0e-6 ? 1 : 0;
        }
    }
    // A rim crest is a kink (the profile's slope turns there), a set of
    // measure zero that a random sample can still land beside.
    TestTrue(FString::Printf(TEXT("the analytic gradient, craters in, agrees with finite differences (%d of %d)"), Agree, Trials),
             Agree >= Trials * 995 / 1000);
    TestTrue(FString::Printf(TEXT("and the craters' own gradient is their sum's (%d of %d)"), CraterAgree, Trials),
             CraterAgree >= Trials * 995 / 1000);
    return true;
}

bool FWorldReliefCraterBoundsTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefCratersLocal;
    const FWorldRelief Relief(Barren());
    TestEqual(TEXT("the drawn peak is MaxHeightCm, exactly"), Relief.MaxHeightCm(), Relief.GetParams().PeakCm);
    TestTrue(TEXT("SlopeScale is the peak over the radius and S_max"),
             FMath::IsNearlyEqual(Relief.SlopeScale(), Relief.GetParams().PeakCm / (Relief.GetParams().RadiusCm * Relief.SMax()), 1e-15));
    FRandomStream Random(5);
    double Highest = -TNumericLimits<double>::Max();
    double Lowest = TNumericLimits<double>::Max();
    double CraterHighest = 0.0;
    double CraterSteepest = 0.0;
    int32 CraterOutside = 0;
    const int32 Samples = 100000;
    for (int32 Trial = 0; Trial < Samples; ++Trial)
    {
        const FVector3d D = RandomDirection(Random);
        const double Height = Relief.Height(D, 0.0);
        Highest = FMath::Max(Highest, Height);
        Lowest = FMath::Min(Lowest, Height);
        FVector3d Grad;
        const double Craters = Relief.CraterSum(D, 0.0, &Grad);
        CraterHighest = FMath::Max(CraterHighest, FMath::Abs(Craters));
        CraterSteepest = FMath::Max(CraterSteepest, Grad.Size());
        CraterOutside += (FMath::Abs(Craters) <= FWorldRelief::CraterBound() && Grad.Size() <= FWorldRelief::CraterSlopeBound()) ? 0 : 1;
    }
    AddInfo(FString::Printf(TEXT("sampled %.0f..%.0f m within %.0f..%.0f m; the craters alone reached %.3f of their bound and %.3f of their slope bound"),
        Lowest / 100.0, Highest / 100.0, Relief.MinHeightCm() / 100.0, Relief.MaxHeightCm() / 100.0,
        CraterHighest / FWorldRelief::CraterBound(), CraterSteepest / FWorldRelief::CraterSlopeBound()));
    TestTrue(TEXT("no sample of 100,000 above MaxHeightCm or below MinHeightCm, craters in"),
             Highest <= Relief.MaxHeightCm() && Lowest >= Relief.MinHeightCm());
    TestTrue(FString::Printf(TEXT("and the drawn peaks still reach at least 0.9 of the peak, craters in (%.3f)"),
             FMath::Max(Highest, -Lowest) / Relief.MaxHeightCm()), FMath::Max(Highest, -Lowest) >= 0.9 * Relief.MaxHeightCm());
    TestEqual(TEXT("the craters alone never pass their bound or their slope bound"), CraterOutside, 0);
    const double Footprint = 5.0e4;
    const double FootprintRadius = Footprint / Relief.GetParams().RadiusCm;
    TestTrue(TEXT("a footprint's omitted bound covers what the fade removed, craters in"),
             [&]() { FRandomStream Local(9); for (int32 I = 0; I < 5000; ++I) { const FVector3d D = RandomDirection(Local);
                 if (FMath::Abs(Relief.Height(D, 0.0) - Relief.Height(D, Footprint)) > Relief.OmittedBoundCm(Footprint)) { return false; } } return true; }());
    TestTrue(TEXT("and the craters' own omitted bound covers what their fade removed"),
             [&]() { FRandomStream Local(13); for (int32 I = 0; I < 5000; ++I) { const FVector3d D = RandomDirection(Local);
                 if (FMath::Abs(Relief.CraterSum(D, 0.0) - Relief.CraterSum(D, FootprintRadius)) > FWorldRelief::CraterOmittedBound(FootprintRadius)) { return false; } } return true; }());
    FWorldReliefParams Ocean = Barren();
    Ocean.Ground = EGround::None;
    const FWorldRelief Flat(Ocean);
    TestEqual(TEXT("a world with no ground reads its datum whatever its PeakCm"), Flat.Height(From0(), 0.0), 0.0);
    TestEqual(TEXT("and has no height"), Flat.MaxHeightCm(), 0.0);
    return true;
}

#endif
