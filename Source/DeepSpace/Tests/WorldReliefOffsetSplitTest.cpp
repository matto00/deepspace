#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Surface/WorldRelief.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The shared file keeps each band's lattice offset as an exact integer part
 * apart from its fraction (landing slice (b), Task 31b; the developer's
 * ruling at R2: "slice (b) then tightens it at the root"). A band's noise
 * coordinate was D x frequency + the seed offset + (37, 59, 83) x its band
 * number: up to about 1,250 for the detail bands and 9,000 for the craters,
 * where a float's step is 1e-4 to 1e-3 of a cell -- the float floor every
 * GPU evaluation sat on, the engine's own nodes included. Split, the float
 * arithmetic sees only D x frequency and a fraction, and the integer part
 * goes to the lattice hash alone, exactly.
 *
 * Held headlessly through the file's float build, which performs the GPU's
 * operations in the GPU's precision (WorldReliefNoise::FaceF32), against
 * its double build, at the parity test's five footprints and at the very
 * float D a GPU would draw at; Eyes.WorldReliefParity holds the GPU itself.
 * Samples within 2e-3 cells of a held crater's step are left out, as there.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefOffsetSplitTest, "DeepSpace.Surface.WorldRelief.OffsetSplit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefOffsetSplitLocal
{
    constexpr double StepMarginCells = 2.0e-3;
    constexpr int32 Samples = 20000;

    /** What the float build may miss double by, term by term, at one
     *  footprint: continent, detail, crater albedo, detail slope, crater
     *  slope. */
    struct FHeld
    {
        double Footprint;
        double Continent;
        double Detail;
        double CraterAlbedo;
        double DetailSlope;
        double CraterSlope;
    };

    /** 1.25 x what the split measured here (the larger of the two worlds,
     *  20,000 samples, FRandomStream(31)), rounded up, and never under 1e-7.
     *  Before the split the same run measured, at 1/96, 1/768, 1/3072 and
     *  1/12288: continent 8.3e-5 to 9.2e-5, crater albedo 3.9e-4 to 7.4e-4,
     *  crater slope 2.4e-3 to 3.9e-3, detail 1.5e-4 to 1.5e-3, detail slope
     *  6.5e-4 to 8.7e-3. The finest detail bands gain least: their error is
     *  D x frequency's own rounding, which no offset split reaches. */
    const FHeld Held[] = {
        { 1.0 / 12.0,    2.2e-6, 1.0e-7, 1.0e-7, 1.0e-7, 1.0e-7 },
        { 1.0 / 96.0,    3.8e-6, 1.2e-5, 1.3e-6, 6.0e-5, 7.8e-6 },
        { 1.0 / 768.0,   5.5e-6, 1.1e-4, 7.4e-6, 7.1e-4, 4.3e-5 },
        { 1.0 / 3072.0,  5.7e-6, 4.0e-4, 2.9e-5, 2.2e-3, 1.7e-4 },
        { 1.0 / 12288.0, 5.8e-6, 1.5e-3, 1.2e-4, 8.9e-3, 6.6e-4 },
    };

    struct FWorld
    {
        const TCHAR* Name;
        FVector3d Offset;
        double Stretch;
    };

    /** Offsets are multiples of 1/256 in [0, 256), as every real one is:
     *  one near the top of the range, where the old sums were largest, and
     *  the parity test's giant. */
    const FWorld Worlds[] = {
        { TEXT("far offset"), FVector3d(255.99609375, 200.25, 127.5), 1.0 },
        { TEXT("giant"), FVector3d(12.5, 200.25, 77.0), 6.0 },
    };

    double AbsMax(const FVector3d& V)
    {
        return FMath::Max3(FMath::Abs(V.X), FMath::Abs(V.Y), FMath::Abs(V.Z));
    }
}

bool FWorldReliefOffsetSplitTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefOffsetSplitLocal;
    const int32 CraterBands = WorldReliefNoise::Bands().CraterIndices.Num();
    for (const FWorld& World : Worlds)
    {
        for (const FHeld& To : Held)
        {
            FRandomStream Random(31);
            FHeld Gap = { To.Footprint, 0.0, 0.0, 0.0, 0.0, 0.0 };
            int32 LeftOut = 0;
            for (int32 Sample = 0; Sample < Samples; ++Sample)
            {
                // The D a GPU draws at is a float; both builds are handed it.
                const FVector3f DFloat(Random.GetUnitVector());
                const FVector3d D(DFloat);
                bool bOnStep = false;
                for (int32 Band = 0; Band < CraterBands && !bOnStep; ++Band)
                {
                    bOnStep = WorldReliefNoise::CraterBandMargin(D, To.Footprint, World.Offset, Band) < StepMarginCells;
                }
                if (bOnStep)
                {
                    ++LeftOut;
                    continue;
                }
                const FFaceTerms Double = WorldReliefNoise::FaceF64(D, To.Footprint, World.Offset, World.Stretch);
                const FFaceTerms Float = WorldReliefNoise::FaceF32(DFloat, static_cast<float>(To.Footprint), FVector3f(World.Offset),
                                                                   static_cast<float>(World.Stretch));
                Gap.Continent = FMath::Max(Gap.Continent, FMath::Abs(Double.Continent - Float.Continent));
                Gap.Detail = FMath::Max(Gap.Detail, FMath::Abs(Double.Detail - Float.Detail));
                Gap.CraterAlbedo = FMath::Max(Gap.CraterAlbedo, FMath::Abs(Double.CraterAlbedo - Float.CraterAlbedo));
                Gap.DetailSlope = FMath::Max(Gap.DetailSlope, AbsMax(Double.DetailSlope - Float.DetailSlope));
                Gap.CraterSlope = FMath::Max(Gap.CraterSlope, AbsMax(Double.CraterSlope - Float.CraterSlope));
            }
            const FString Measured = FString::Printf(TEXT("continent %.2e, detail %.2e, crater albedo %.2e, detail slope %.2e, crater slope %.2e"),
                Gap.Continent, Gap.Detail, Gap.CraterAlbedo, Gap.DetailSlope, Gap.CraterSlope);
            const FString HeldTo = FString::Printf(TEXT("continent %.1e, detail %.1e, crater albedo %.1e, detail slope %.1e, crater slope %.1e"),
                To.Continent, To.Detail, To.CraterAlbedo, To.DetailSlope, To.CraterSlope);
            AddInfo(FString::Printf(TEXT("%s, 1/%.0f: float build vs double %s (%d of %d left out on a step)"),
                World.Name, 1.0 / To.Footprint, *Measured, LeftOut, Samples));
            TestTrue(FString::Printf(TEXT("%s, footprint 1/%.0f: the float build tracks double within %s (%s)"),
                World.Name, 1.0 / To.Footprint, *HeldTo, *Measured),
                Gap.Continent <= To.Continent && Gap.Detail <= To.Detail && Gap.CraterAlbedo <= To.CraterAlbedo
                    && Gap.DetailSlope <= To.DetailSlope && Gap.CraterSlope <= To.CraterSlope);
            TestTrue(FString::Printf(TEXT("%s, footprint 1/%.0f: at most 3%% of samples lie on a step, six crater bands' (%d)"), World.Name, 1.0 / To.Footprint, LeftOut),
                LeftOut <= Samples * 3 / 100);
        }
    }
    return true;
}

#endif
