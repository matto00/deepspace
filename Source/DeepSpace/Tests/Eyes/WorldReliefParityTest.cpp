#include <limits>

#include "Engine/Texture2DDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/SunShadowMap.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/WorldRelief.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A GUARD, not a look (landing decision 1). Named outside DeepSpace. so
 * ./test.sh never runs it: it renders, so it runs through the lock with
 * -RenderOffScreen and never -nullrhi,
 *
 *   Tools/eyes.sh Eyes.WorldReliefParity
 *
 * and slice (a)'s and (b)'s done-when run it. It draws M_SkyReliefProbe --
 * the shared file's raw face terms over a fixed patch at a fixed footprint,
 * untonemapped -- into a 32-bit float target, reads it back, and holds it
 *
 *   * to FWorldRelief::Face of Baemsekai IV, at the very D the GPU drew;
 *   * and a giant's -- stretch 6, the belts' streaking, the path no barren
 *     world takes -- to the same file in double (WorldReliefNoise::FaceF64),
 *     since a giant has no ground and so no FWorldRelief.
 *     (Until landing slice (a) merged it also held the file to the engine's
 *     own noise nodes, which is how "the orbital look unchanged" was proven;
 *     the spike's verdict line below records it.)
 *
 * over 256 x 256 samples at each of five footprints, per footprint and per
 * term, by the measured floor as a rule (the developer's rulings at R2 and
 * R4): each term, at each footprint, to the larger of the ruled table and
 * 1.25 x the engine's own nodes' distance from double. The engine's nodes
 * are retired with the legacy probe, so that distance is no longer
 * measured each run: it is the last one measured, R4's closing run on
 * each world (Footprints' EngineFloor and GiantEngineFloor, below). The
 * table (the rulings after the spike and at R2): every value and every detail term at 1/12,
 * 1/96 and 1/768 to 1e-3, and every value at 1/3072; crater slopes to 5e-3
 * from 1/96 down; detail slopes to 5e-3 at 1/3072; at 1/12288 detail values
 * to 1.5e-3 and detail slopes to 8e-3. A term over its allowance is the GPU's
 * float missing double by more than the engine's own nodes did: a port bug.
 * A pixel the GPU drew that is not finite fails outright: a NaN would
 * otherwise vanish from a running maximum at the next finite sample.
 * What the double file itself computes is
 * DeepSpace.Surface.WorldRelief.KnownValues's to guard (exact double
 * values): an error in the shared text reaches both compilers, and moves
 * double and the GPU together.
 *
 * Slice (b) tightened it at the root (Task 31b): the shared file keeps each
 * band's lattice offset as an exact integer part apart from its fraction,
 * so the GPU's float sees only D x frequency and a fraction, and every term
 * is now held to 1.25 x what the GPU then measured from double
 * (SplitHeld, GiantSplitHeld), each never looser than the rule above, which
 * is still computed and printed. What is left at the finest footprints is
 * D x frequency's own rounding in float, which no offset split reaches. A
 * crater band's albedo and slope step at a held crater's rim and at the
 * bisector beside one, where any rounding at all can change the side a
 * sample lands on: samples within 2e-3 cells of such a step are left out,
 * and no crater band's steps may leave out more than 1%.
 *
 * It must compile the Custom node to draw anything, so it also catches what
 * the headless suite cannot see: an HLSL error in WorldRelief.ush ships the
 * grey default material with every test green. Its first check is the pipe:
 * a probe selecting nothing reads back its bias, exactly, at every pixel.
 *
 * Writes Saved/Eyes/WorldReliefParity/report.txt: every gap, what each
 * term was held to and whether the table or the floor set it, and one
 * diagnostic reported but not asserted -- the file's float build against
 * the GPU.
 *
 * Spike verdict (landing R1): FLOAT FLOOR -- SUMMARY shared-vs-engine 3.11e-03, C++-vs-shared 3.63e-03, float-C++-vs-shared 5.22e-03, C++-vs-engine 3.51e-03, left out at most 1.376%
 *
 * Verdict under the ruling (per footprint and per term; held sites' steps only): GO -- SUMMARY shared-vs-engine 3.11e-03, C++-vs-shared 3.63e-03, float-C++-vs-shared 5.22e-03, C++-vs-engine 3.51e-03, left out at most 0.462%
 *
 * R2, every band (twelve detail, six crater): FLOAT FLOOR past the ruling -- SUMMARY shared-vs-engine 7.64e-03, C++-vs-shared 6.88e-03, float-C++-vs-shared 7.90e-03, C++-vs-engine 6.19e-03, left out at most 2.142%
 *
 * R2 under the ruling at R2 (per term at the measured floor, 1% per crater band): FLOAT FLOOR at two terms the ruling holds to 1e-3 -- C++ vs shared, barren: detail slope 1.18e-03 at 1/768 (float build vs GPU 1.83e-03, C++ vs engine 1.04e-03), crater albedo 1.01e-03 at 1/12288 (C++ vs engine 1.38e-03); shared vs engine within the ruling everywhere; left out at most 0.462% in one crater band
 *
 * R2 under the measured floor as a rule: PASS -- barren 1/768 detail slope 1.18e-03 held to 1.3e-03 (floor), 1/12288 crater albedo 1.01e-03 held to 1.7e-03 (floor); shared vs engine within the table everywhere; SUMMARY unchanged
 *
 * R2 Step 6, proven: detail band 6144 -> 6143 in the shared file KILLED here
 * (1/12288 detail 4.2e-1 against the engine's nodes); the floor rule taken
 * out KILLED here (the two excesses above). The plan's own mutant, band
 * 24576 -> 24575, SURVIVED here and is equivalent for this check: every
 * footprint it draws at fades bands 24576 and 49152 to exactly nothing (the
 * report was identical to the clean run's). KnownValues and
 * DeepSpace.Sky.MaterialContract both KILL it headlessly; the GPU's float at
 * those two bands is unguarded until a footprint below 1/12288 is ruled.
 *
 * R4, M_SkyBody on the shared file, the barren world Baemsekai IV through FWorldRelief: STOPPED -- one term over the table, shared file vs engine nodes, Baemsekai IV 1/768 detail slope 1.01e-03 against 1.0e-03 (C++ vs engine 1.25e-03, C++ vs shared 1.27e-03, held to 1.6e-03 by the rule and within it); every other term within the table and the rule; SUMMARY shared-vs-engine 7.64e-03, C++-vs-shared 7.42e-03, float-C++-vs-shared 9.36e-03, C++-vs-engine 6.23e-03, left out at most 0.462% in one crater band
 *
 * R4 under the port-bug test restated: PASS, both worlds -- shared file vs double within the measured floor as a rule at every footprint and term (Baemsekai IV 1/768 detail slope 1.27e-03 against the engine's 1.25e-03, held to 1.6e-03; the widest ratio over the engine, 1.27, is the giant's 1/3072 detail value, 3.69e-04 under the table's 1.0e-03); FWorldRelief vs the GPU identical to it; SUMMARY shared-vs-double 7.42e-03, engine-vs-double 7.01e-03, FWorldRelief-vs-shared 7.42e-03, shared-vs-engine 7.64e-03 (printed), float-C++-vs-shared 9.36e-03, left out at most 0.462% in one crater band
 *
 * R5, the legacy probe retired, FWorldRelief against the GPU at the recorded floor: PASS -- every term as at R4 (1/768 detail slope 1.27e-03 held to 1.6e-03); SUMMARY C++-vs-GPU 7.42e-03, float-C++-vs-GPU 9.36e-03, left out at most 0.444% in one crater band
 *
 * Task 31b, the lattice offsets split (landing slice (b)): PASS, both worlds, held to the split's own measure -- C++ vs GPU at 1/96 crater slope 5.67e-06 (was 2.16e-03), crater albedo 1.18e-06 (3.87e-04), continent 1.15e-06 (5.18e-05); at 1/12288 crater slope 5.26e-04 (4.30e-03), crater albedo 1.13e-04 (8.37e-04), detail 1.06e-03 (1.28e-03), detail slope 5.71e-03 (7.42e-03); the giant alike; SUMMARY C++-vs-GPU 6.10e-03 (7.42e-03), float-C++-vs-GPU 5.95e-03, left out at most 0.462% in one crater band. The orbital look unchanged: Baemsekai III, IV and V at 30 km and 12 km, dusk included, rendered before and after and diffed pixel for pixel (Task 31b's note in the plan).
 *
 * R5 after review, the giant restored (stretch 6, held to the file in double at its recorded floor) and every pixel held finite: PASS, both worlds -- the giant's every gap identical to R4's (1/3072 detail 3.69e-04, 1/12288 detail slope 7.21e-03 held to 8.8e-03); 0 pixels not finite; SUMMARY C++-vs-GPU 7.42e-03, float-C++-vs-GPU 9.36e-03, left out at most 0.462% in one crater band
 *
 * Task T2, the craters summed (WR_CraterSum) and the relief the ground's own slope: PASS, both worlds, the crater columns held to the summed kernels' own measure (1.25x, re-measured: 1/12288 crater albedo 1.79e-04, slope 6.76e-04 on Baemsekai IV; 2.11e-04 and 7.17e-04 on the giant) and every other term to Task 31b's; SUMMARY C++-vs-GPU 6.10e-03, float-C++-vs-GPU 5.95e-03, left out at most 2.142% in all (0.462% in one crater band) *
 * Cast shadow's map (the developer's ruling on slice (b)'s build, baked, 2026-09-28): PASS -- Baemsekai IV's map at 1,024 columns across the terminator (the star 3 degrees over the patch) and across the seam (75 degrees: no lower light puts this patch's centre at +-pi, and there the map is whole, so the seam is held on lit texels only), SunShadowMap::Sample vs the GPU 2.14e-03 against the float mirror's 1.79e-03 from double (held to 2.2e-03, at the seam, footprint 1.5e-03; every other footprint under 1.7e-04), at five footprints to level 2; SUMMARY shadow map C++-vs-GPU 2.14e-03, float-vs-double 1.79e-03
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWorldReliefParityTest,
    "Eyes.WorldReliefParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefParityLocal
{
    constexpr int32 Side = 256;
    constexpr double StepMarginCells = 2.0e-3;
    /** At most this share of the samples may lie on one crater band's steps. */
    constexpr double MaxLeftOutPerBand = 0.01;

    /** What each term is held to at one footprint: the measured float floor
     *  (the developer's rulings after the spike and at R2). */
    struct FTolerance
    {
        double Footprint;
        double Continent;
        double Detail;
        double CraterAlbedo;
        double DetailSlope;
        double CraterSlope;
    };

    /** One footprint: the ruled table, and the engine's own nodes' distance
     *  from double there -- continent, detail, crater albedo, detail slope,
     *  crater slope -- as R4's closing run measured it on Baemsekai IV
     *  (commit 1dd88fa's report, the last run with the legacy probe). */
    struct FFootprint
    {
        FTolerance Table;
        FTolerance EngineFloor;
    };

    /** D units a pixel, times filter_pixels: from continents alone down to
     *  every band coarser than 1/12288 of the radius. */
    const FFootprint Footprints[] = {
        { { 1.0 / 12.0,    1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3 }, { 1.0 / 12.0,    2.73e-5, 0.0,     0.0,     0.0,     0.0     } },
        { { 1.0 / 96.0,    1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3, 5.0e-3 }, { 1.0 / 96.0,    5.18e-5, 8.48e-5, 6.49e-4, 4.66e-4, 2.36e-3 } },
        { { 1.0 / 768.0,   1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3, 5.0e-3 }, { 1.0 / 768.0,   5.64e-5, 2.75e-4, 9.35e-4, 1.25e-3, 3.71e-3 } },
        { { 1.0 / 3072.0,  1.0e-3, 1.0e-3, 1.0e-3, 5.0e-3, 5.0e-3 }, { 1.0 / 3072.0,  5.69e-5, 4.54e-4, 1.20e-3, 2.42e-3, 4.41e-3 } },
        { { 1.0 / 12288.0, 1.0e-3, 1.5e-3, 1.0e-3, 8.0e-3, 5.0e-3 }, { 1.0 / 12288.0, 5.71e-5, 1.12e-3, 1.28e-3, 6.23e-3, 4.30e-3 } },
    };

    /** The giant's engine floor, footprint by footprint as above: R4's same
     *  closing run (commit 1dd88fa's report), engine nodes vs double at
     *  stretch 6. */
    const FTolerance GiantEngineFloor[] = {
        { 1.0 / 12.0,    4.57e-6, 0.0,     0.0,     0.0,     0.0     },
        { 1.0 / 96.0,    2.69e-5, 0.0,     6.90e-4, 0.0,     2.44e-3 },
        { 1.0 / 768.0,   4.31e-5, 1.06e-4, 1.30e-3, 6.34e-4, 3.97e-3 },
        { 1.0 / 3072.0,  4.58e-5, 2.91e-4, 1.42e-3, 1.87e-3, 4.40e-3 },
        { 1.0 / 12288.0, 4.65e-5, 1.18e-3, 1.38e-3, 7.01e-3, 4.97e-3 },
    };
    static_assert(UE_ARRAY_COUNT(GiantEngineFloor) == UE_ARRAY_COUNT(Footprints), "a giant floor per footprint");

    /** What each term is held to since the lattice offsets were split (Task
     *  31b): 1.25 x the GPU's distance from double that the split's first run
     *  measured, per footprint and per term, rounded up and never under 1e-7
     *  (a term every band of which has faded is exactly 0 on both sides).
     *  Baemsekai IV through FWorldRelief, then the giant against the file in
     *  double. The crater columns were measured again, by the same rule,
     *  when Task T2 made the craters the summed kernels (WR_CraterSum): at
     *  1/96 to 1/12288, Baemsekai IV's crater albedo 1.82e-6, 1.01e-5,
     *  4.78e-5, 1.79e-4 and slope 6.00e-6, 4.65e-5, 1.77e-4, 6.76e-4; the
     *  giant's albedo 1.83e-6, 1.08e-5, 4.90e-5, 2.11e-4 and slope 6.89e-6,
     *  4.08e-5, 1.86e-4, 7.17e-4. */
    const FTolerance SplitHeld[] = {
        { 1.0 / 12.0,    1.2e-6, 1.0e-7, 1.0e-7, 1.0e-7, 1.0e-7 },
        { 1.0 / 96.0,    1.5e-6, 1.1e-5, 2.3e-6, 5.8e-5, 7.5e-6 },
        { 1.0 / 768.0,   1.5e-6, 8.9e-5, 1.3e-5, 5.7e-4, 5.9e-5 },
        { 1.0 / 3072.0,  1.5e-6, 3.4e-4, 6.0e-5, 2.5e-3, 2.3e-4 },
        { 1.0 / 12288.0, 1.5e-6, 1.4e-3, 2.3e-4, 7.2e-3, 8.5e-4 },
    };
    const FTolerance GiantSplitHeld[] = {
        { 1.0 / 12.0,    2.7e-7, 1.0e-7, 1.0e-7, 1.0e-7, 1.0e-7 },
        { 1.0 / 96.0,    1.9e-6, 1.0e-7, 2.3e-6, 1.0e-7, 8.7e-6 },
        { 1.0 / 768.0,   3.4e-6, 5.8e-5, 1.4e-5, 3.8e-4, 5.1e-5 },
        { 1.0 / 3072.0,  3.6e-6, 2.9e-4, 6.2e-5, 1.7e-3, 2.4e-4 },
        { 1.0 / 12288.0, 3.7e-6, 1.3e-3, 2.7e-4, 7.7e-3, 9.0e-4 },
    };
    static_assert(UE_ARRAY_COUNT(SplitHeld) == UE_ARRAY_COUNT(Footprints), "a split tolerance per footprint");
    static_assert(UE_ARRAY_COUNT(GiantSplitHeld) == UE_ARRAY_COUNT(Footprints), "a giant split tolerance per footprint");

    /** The giant's seed offset: a made one, multiples of 1/256 as every real
     *  one is. The barren world is Baemsekai IV, with its own. */
    const FVector3d GiantOffset(12.5, 200.25, 77.0);
    constexpr double GiantStretch = 6.0;

    /** How much further from double than the engine's own nodes the GPU
     *  may be before it is a port bug (the measured floor as a rule; the
     *  port-bug test restated at R4). */
    constexpr double FloorRuleFactor = 1.25;

    enum class EPass : int32 { Terms = 0, DetailSlope = 1, CraterSlope = 2, Direction = 3 };

    FLinearColor Selecting(EPass Pass)
    {
        FLinearColor Select(0.0f, 0.0f, 0.0f, 0.0f);
        Select.Component(static_cast<int32>(Pass)) = 1.0f;
        return Select;
    }

    TArray<FVector3d> Draw(UWorld* World, UTextureRenderTarget2D* Target, UMaterialInstanceDynamic* Probe,
                           const FLinearColor& Select, const FLinearColor& Bias)
    {
        Probe->SetVectorParameterValue(SkyMaterial::ProbeSelect, Select);
        Probe->SetVectorParameterValue(SkyMaterial::ProbeBias, Bias);
        if (GShaderCompilingManager)
        {
            GShaderCompilingManager->FinishAllCompilation();
        }
        UKismetRenderingLibrary::DrawMaterialToRenderTarget(World, Target, Probe);
        FlushRenderingCommands();
        TArray<FLinearColor> Pixels;
        Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels, FReadSurfaceDataFlags(RCM_MinMax, CubeFace_MAX));
        TArray<FVector3d> Out;
        Out.Reserve(Pixels.Num());
        for (const FLinearColor& Pixel : Pixels)
        {
            Out.Emplace(Pixel.R, Pixel.G, Pixel.B);
        }
        return Out;
    }

    /** One probe at one footprint: its terms, and the D it drew them at. */
    struct FDrawn
    {
        TArray<FVector3d> Terms;
        TArray<FVector3d> DetailSlope;
        TArray<FVector3d> CraterSlope;
        TArray<FVector3d> Direction;

        FFaceTerms At(int32 Index) const
        {
            FFaceTerms Face;
            Face.Continent = Terms[Index].X;
            Face.Detail = Terms[Index].Y;
            Face.CraterAlbedo = Terms[Index].Z;
            Face.DetailSlope = DetailSlope[Index];
            Face.CraterSlope = CraterSlope[Index];
            return Face;
        }
    };

    FDrawn DrawAll(UWorld* World, UTextureRenderTarget2D* Target, UMaterialInstanceDynamic* Probe)
    {
        const FLinearColor NoBias(0.0f, 0.0f, 0.0f, 0.0f);
        FDrawn Drawn;
        Drawn.Terms = Draw(World, Target, Probe, Selecting(EPass::Terms), NoBias);
        Drawn.DetailSlope = Draw(World, Target, Probe, Selecting(EPass::DetailSlope), NoBias);
        Drawn.CraterSlope = Draw(World, Target, Probe, Selecting(EPass::CraterSlope), NoBias);
        Drawn.Direction = Draw(World, Target, Probe, Selecting(EPass::Direction), NoBias);
        return Drawn;
    }

    /** The largest absolute difference seen, term by term. A gap that is
     *  not finite sticks at infinity: FMath::Max(NaN, x) is x, so a plain
     *  running maximum loses a NaN at the next finite sample. */
    struct FGap
    {
        static double Wider(double Seen, double Gap)
        {
            return FMath::IsFinite(Gap) ? FMath::Max(Seen, Gap) : std::numeric_limits<double>::infinity();
        }

        static double AbsMax(const FVector3d& V)
        {
            return Wider(Wider(FMath::Abs(V.X), FMath::Abs(V.Y)), FMath::Abs(V.Z));
        }

        double Continent = 0.0;
        double Detail = 0.0;
        double CraterAlbedo = 0.0;
        double DetailSlope = 0.0;
        double CraterSlope = 0.0;

        void Widen(const FFaceTerms& A, const FFaceTerms& B)
        {
            Continent = Wider(Continent, FMath::Abs(A.Continent - B.Continent));
            Detail = Wider(Detail, FMath::Abs(A.Detail - B.Detail));
            CraterAlbedo = Wider(CraterAlbedo, FMath::Abs(A.CraterAlbedo - B.CraterAlbedo));
            DetailSlope = Wider(DetailSlope, AbsMax(A.DetailSlope - B.DetailSlope));
            CraterSlope = Wider(CraterSlope, AbsMax(A.CraterSlope - B.CraterSlope));
        }

        double WorstValue() const
        {
            return FMath::Max3(Continent, Detail, CraterAlbedo);
        }

        double WorstSlope() const
        {
            return FMath::Max(DetailSlope, CraterSlope);
        }

        /** Held per term, each to its own tolerance at this footprint. */
        bool Within(const FTolerance& To) const
        {
            return Continent <= To.Continent && Detail <= To.Detail && CraterAlbedo <= To.CraterAlbedo
                && DetailSlope <= To.DetailSlope && CraterSlope <= To.CraterSlope;
        }

        double Worst() const
        {
            return FMath::Max(FMath::Max3(Continent, Detail, CraterAlbedo), FMath::Max(DetailSlope, CraterSlope));
        }

        FString Describe() const
        {
            return FString::Printf(TEXT("continent %.2e, detail %.2e, crater albedo %.2e, detail slope %.2e, crater slope %.2e"),
                Continent, Detail, CraterAlbedo, DetailSlope, CraterSlope);
        }
    };

    bool IsFinite(const FVector3d& V)
    {
        return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z);
    }

    FString DescribeTolerance(const FTolerance& To)
    {
        return FString::Printf(TEXT("continent %.1e, detail %.1e, crater albedo %.1e, detail slope %.1e, crater slope %.1e"),
            To.Continent, To.Detail, To.CraterAlbedo, To.DetailSlope, To.CraterSlope);
    }

    /** The measured floor as a rule: each term to the larger of the ruled
     *  table's value and FloorRuleFactor x the engine's own nodes' recorded
     *  distance from double at this footprint. Says, per term, whether the
     *  table or the floor set it. */
    FTolerance HeldToByTheFloor(const FTolerance& Table, const FTolerance& EngineVsDouble, FString& OutSetBy)
    {
        FTolerance To = Table;
        TArray<FString> SetBy;
        auto Rule = [&SetBy](const TCHAR* Name, double Ruled, double EngineMiss)
        {
            const double Floor = FloorRuleFactor * EngineMiss;
            SetBy.Add(FString::Printf(TEXT("%s %s"), Name, Floor > Ruled ? TEXT("floor") : TEXT("table")));
            return FMath::Max(Ruled, Floor);
        };
        To.Continent = Rule(TEXT("continent"), Table.Continent, EngineVsDouble.Continent);
        To.Detail = Rule(TEXT("detail"), Table.Detail, EngineVsDouble.Detail);
        To.CraterAlbedo = Rule(TEXT("crater albedo"), Table.CraterAlbedo, EngineVsDouble.CraterAlbedo);
        To.DetailSlope = Rule(TEXT("detail slope"), Table.DetailSlope, EngineVsDouble.DetailSlope);
        To.CraterSlope = Rule(TEXT("crater slope"), Table.CraterSlope, EngineVsDouble.CraterSlope);
        OutSetBy = FString::Join(SetBy, TEXT(", "));
        return To;
    }
}

bool FWorldReliefParityTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefParityLocal;

    if (GUsingNullRHI)
    {
        AddError(TEXT("Eyes.WorldReliefParity renders: run it with Tools/eyes.sh, never -nullrhi"));
        return false;
    }

    SkyTestWorld::FSkyWorld Test(TEXT("WorldReliefParityWorld"));
    UMaterial* Shared = LoadObject<UMaterial>(nullptr, SkyMaterial::ReliefProbePath);
    if (!TestNotNull(TEXT("M_SkyReliefProbe is built (Tools/setup_sky_materials.py)"), Shared))
    {
        return false;
    }

    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Test.World);
    Target->RenderTargetFormat = RTF_RGBA32f;
    Target->ClearColor = FLinearColor::Transparent;
    Target->bAutoGenerateMips = false;
    Target->InitAutoFormat(Side, Side);
    Target->UpdateResourceImmediate(true);
    UMaterialInstanceDynamic* Probe = UMaterialInstanceDynamic::Create(Shared, Test.World);

    TArray<FString> Report;

    // -- The pipe: a probe that selects nothing draws its bias, exactly -------
    // Signed, above one, and below a float's half-precision: a target that
    // clamped, rounded, tonemapped or drew the grey default fails here.
    {
        const FLinearColor Known(-0.375f, 1234.5f, 3.0e-5f, 0.0f);
        const TArray<FVector3d> Drawn = Draw(Test.World, Target, Probe, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), Known);
        int32 Wrong = 0;
        FString FirstWrong;
        for (int32 Index = 0; Index < Drawn.Num(); ++Index)
        {
            const FVector3d& Pixel = Drawn[Index];
            const bool bRight = Pixel.X == Known.R && Pixel.Y == Known.G && Pixel.Z == Known.B;
            if (!bRight && Wrong++ == 0)
            {
                FirstWrong = FString::Printf(TEXT("pixel %d read (%.9g, %.9g, %.9g)"), Index, Pixel.X, Pixel.Y, Pixel.Z);
            }
        }
        const bool bWhole = TestEqual(TEXT("the target holds every pixel"), Drawn.Num(), Side * Side);
        if (!TestEqual(FString::Printf(TEXT("and hands back what the probe drew, signed and unrounded, at every one (%s)"), *FirstWrong), Wrong, 0) || !bWhole)
        {
            return false;
        }
    }

    double WorstReliefShared = 0.0;
    double WorstFloatShared = 0.0;
    double MostLeftOut = 0.0;
    double MostLeftOutInABand = 0.0;

    // Baemsekai IV, the landing fixtures' first world, through FWorldRelief:
    // what the flight and the terrain read.
    const TOptional<FStarSystem> Home = Test.Universe->GetSystem(Test.Universe->GetStartSystem());
    if (!TestTrue(TEXT("home generates"), Home.IsSet()))
    {
        return false;
    }
    const FSkySystem HomeSky = FSkySystem::FromSystem(*Home, {});
    if (!TestTrue(TEXT("home has a fourth world"), HomeSky.Bodies.IsValidIndex(4)))
    {
        return false;
    }
    const FSkyBody& Fourth = HomeSky.Bodies[4];
    TestEqual(TEXT("the barren world is Baemsekai IV"), Fourth.Id, FName(TEXT("Baemsekai IV")));
    const FWorldRelief Ground(Fourth.Relief);

    // Barren: Baemsekai IV, held through FWorldRelief. A giant: stretch 6, a
    // made offset, no ground, so held to the file in double.
    struct FWorldCase
    {
        const TCHAR* Name;
        FLinearColor Seed;
        FVector3d Offset;
        float Banding;
        double Stretch;
        const FWorldRelief* Relief;
    };
    const FWorldCase Worlds[] = {
        { TEXT("Baemsekai IV"), ShipSky::SurfaceSeed(Fourth.SurfaceSeed, Fourth.BeltPairs), Fourth.Relief.SeedOffset, 0.0f, 1.0, &Ground },
        { TEXT("giant"), FLinearColor(static_cast<float>(GiantOffset.X), static_cast<float>(GiantOffset.Y), static_cast<float>(GiantOffset.Z), 8.0f),
          GiantOffset, 1.0f, GiantStretch, nullptr },
    };
    for (const FWorldCase& World : Worlds)
    {
        const FVector3d& Offset = World.Offset;
        Probe->SetVectorParameterValue(SkyMaterial::SurfaceSeed, World.Seed);
        Probe->SetScalarParameterValue(SkyMaterial::Banding, World.Banding);
        for (int32 Row = 0; Row < static_cast<int32>(UE_ARRAY_COUNT(Footprints)); ++Row)
        {
            const FTolerance& To = Footprints[Row].Table;
            const FTolerance& EngineFloor = World.Relief ? Footprints[Row].EngineFloor : GiantEngineFloor[Row];
            const double FootprintD = To.Footprint;
            const float Footprint = static_cast<float>(FootprintD);
            Probe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
            const FDrawn Drawn = DrawAll(Test.World, Target, Probe);

            FGap HeldGap;
            FGap FloatVsShared;
            int32 LeftOut = 0;
            int32 NotFinite = 0;
            const int32 CraterBands = WorldReliefNoise::Bands().CraterIndices.Num();
            TArray<int32> LeftOutByBand;
            LeftOutByBand.Init(0, CraterBands);
            for (int32 Index = 0; Index < Side * Side; ++Index)
            {
                const FVector3d& D = Drawn.Direction[Index];
                if (!IsFinite(D) || !IsFinite(Drawn.Terms[Index]) || !IsFinite(Drawn.DetailSlope[Index]) || !IsFinite(Drawn.CraterSlope[Index]))
                {
                    // Before the step mask: a NaN D is never near a step
                    // (every comparison with it is false), so it would be
                    // compared, and a NaN term would vanish from the gap.
                    ++NotFinite;
                    continue;
                }
                bool bOnStep = false;
                for (int32 Band = 0; Band < CraterBands; ++Band)
                {
                    if (WorldReliefNoise::CraterBandMargin(D, Footprint, Offset, Band) < StepMarginCells)
                    {
                        ++LeftOutByBand[Band];
                        bOnStep = true;
                    }
                }
                if (bOnStep)
                {
                    ++LeftOut;
                    continue;
                }
                const FFaceTerms Gpu = Drawn.At(Index);
                const FFaceTerms Held = World.Relief
                    ? World.Relief->Face(D, static_cast<double>(Footprint) * World.Relief->GetParams().RadiusCm)
                    : WorldReliefNoise::FaceF64(D, Footprint, Offset, World.Stretch);
                HeldGap.Widen(Held, Gpu);
                FloatVsShared.Widen(WorldReliefNoise::FaceF32(FVector3f(D), Footprint, FVector3f(Offset), static_cast<float>(World.Stretch)), Gpu);
            }
            const double LeftOutShare = static_cast<double>(LeftOut) / (Side * Side);
            const FString At = FString::Printf(TEXT("%s, footprint 1/%.0f"), World.Name, 1.0 / FootprintD);
            FString SetBy;
            const FTolerance Rule = HeldToByTheFloor(To, EngineFloor, SetBy);
            const FTolerance& HeldTo = World.Relief ? SplitHeld[Row] : GiantSplitHeld[Row];
            const FString HeldText = DescribeTolerance(HeldTo);
            TestTrue(FString::Printf(TEXT("%s: the split's tolerances are never looser than the rule they tighten (%s against %s)"),
                *At, *HeldText, *DescribeTolerance(Rule)),
                HeldTo.Continent <= Rule.Continent && HeldTo.Detail <= Rule.Detail && HeldTo.CraterAlbedo <= Rule.CraterAlbedo
                    && HeldTo.DetailSlope <= Rule.DetailSlope && HeldTo.CraterSlope <= Rule.CraterSlope);
            FString ByBand;
            double MostInABand = 0.0;
            for (int32 Band = 0; Band < CraterBands; ++Band)
            {
                const double Share = static_cast<double>(LeftOutByBand[Band]) / (Side * Side);
                MostInABand = FMath::Max(MostInABand, Share);
                ByBand += FString::Printf(TEXT("%s%.3f%%"), Band == 0 ? TEXT("") : TEXT(", "), 100.0 * Share);
            }
            TestEqual(At + TEXT(": every pixel the GPU drew is finite"), NotFinite, 0);
            TestTrue(FString::Printf(TEXT("%s: at most 1%% of samples lie on any one crater band's steps (%s)"), *At, *ByBand),
                MostInABand <= MaxLeftOutPerBand);
            TestTrue(FString::Printf(TEXT("%s: %s computes what the GPU drew, held to %s, the split's own measure (%s)"),
                *At, World.Relief ? TEXT("FWorldRelief") : TEXT("the file in double"), *HeldText, *HeldGap.Describe()), HeldGap.Within(HeldTo));
            WorstReliefShared = FMath::Max(WorstReliefShared, HeldGap.Worst());
            WorstFloatShared = FMath::Max(WorstFloatShared, FloatVsShared.Worst());
            MostLeftOut = FMath::Max(MostLeftOut, LeftOutShare);
            MostLeftOutInABand = FMath::Max(MostLeftOutInABand, MostInABand);
            Report.Add(FString::Printf(TEXT("%s: %d compared, %d left out (by crater band: %s), %d not finite"),
                *At, Side * Side - LeftOut - NotFinite, LeftOut, *ByBand, NotFinite));
            Report.Add(TEXT("  held to (the split, Task 31b):      ") + HeldText);
            Report.Add(TEXT("  the rule it tightens:               ") + DescribeTolerance(Rule));
            Report.Add(TEXT("    set by:                           ") + SetBy);
            Report.Add(TEXT("  engine floor (recorded, R4):        ") + DescribeTolerance(EngineFloor));
            Report.Add((World.Relief ? TEXT("  C++ (double) vs GPU (asserted):     ") : TEXT("  file (double) vs GPU (asserted):    ")) + HeldGap.Describe());
            Report.Add(TEXT("  C++ (float build) vs GPU:           ") + FloatVsShared.Describe());
        }
    }

    // -- The ground's normal (landing decision 9, Task T7) -------------------
    // M_SkyGroundProbe draws M_SkyGround's per-pixel normal over the same
    // patch, for a flat vertex (its normal D itself) carrying the bands of
    // the tile level the cut uses at 50 km over Baemsekai IV; the C++ is
    // WorldReliefShading::Ground on the same D. With
    // DeepSpace.Surface.GroundShadesAsOrbit (the ground's normal is the
    // orbit's on the CPU) this holds the ground's normal on the GPU.
    double WorstGroundNormal = 0.0;
    {
        UMaterial* GroundShared = LoadObject<UMaterial>(nullptr, SkyMaterial::GroundProbePath);
        if (!TestNotNull(TEXT("M_SkyGroundProbe is built (Tools/setup_sky_materials.py)"), GroundShared))
        {
            return false;
        }
        UMaterialInstanceDynamic* GroundProbe = UMaterialInstanceDynamic::Create(GroundShared, Test.World);
        const double VertexBandLimit = TerrainQuadtree::SpacingCm(9, Fourth.Relief.RadiusCm) / Fourth.Relief.RadiusCm;
        GroundProbe->SetVectorParameterValue(SkyMaterial::SurfaceSeed, ShipSky::SurfaceSeed(Fourth.SurfaceSeed, Fourth.BeltPairs));
        GroundProbe->SetScalarParameterValue(SkyMaterial::Cratering, static_cast<float>(Fourth.Relief.Cratering));
        GroundProbe->SetScalarParameterValue(SkyMaterial::ReliefScale, static_cast<float>(Ground.SlopeScale()));
        GroundProbe->SetScalarParameterValue(SkyMaterial::VertexBandLimit, static_cast<float>(VertexBandLimit));
        const FLinearColor NoBias(0.0f, 0.0f, 0.0f, 0.0f);
        // Both probes draw probe_direction's patch: the relief probe's
        // Direction pass is the D each ground pixel was shaded at.
        const TArray<FVector3d> Directions = Draw(Test.World, Target, Probe, Selecting(EPass::Direction), NoBias);
        const FVector3d& Offset = Fourth.Relief.SeedOffset;
        const int32 CraterBands = WorldReliefNoise::Bands().CraterIndices.Num();
        for (int32 Row = 0; Row < static_cast<int32>(UE_ARRAY_COUNT(Footprints)); ++Row)
        {
            const double FootprintD = Footprints[Row].Table.Footprint;
            const float Footprint = static_cast<float>(FootprintD);
            GroundProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
            const TArray<FVector3d> GroundNormals = Draw(Test.World, Target, GroundProbe, NoBias, NoBias);
            double Gap = 0.0;
            int32 LeftOut = 0;
            int32 NotFinite = 0;
            TArray<int32> LeftOutByBand;
            LeftOutByBand.Init(0, CraterBands);
            for (int32 Index = 0; Index < Side * Side; ++Index)
            {
                const FVector3d& D = Directions[Index];
                if (!IsFinite(D) || !IsFinite(GroundNormals[Index]))
                {
                    ++NotFinite;
                    continue;
                }
                bool bOnStep = false;
                for (int32 Band = 0; Band < CraterBands; ++Band)
                {
                    if (WorldReliefNoise::CraterBandMargin(D, Footprint, Offset, Band) < StepMarginCells)
                    {
                        ++LeftOutByBand[Band];
                        bOnStep = true;
                    }
                }
                if (bOnStep)
                {
                    ++LeftOut;
                    continue;
                }
                const FVector3d Held = WorldReliefShading::Ground(Fourth.Relief, D, D, static_cast<double>(Footprint), VertexBandLimit).Normal;
                Gap = FGap::Wider(Gap, FGap::AbsMax(Held - GroundNormals[Index]));
            }
            double MostInABand = 0.0;
            for (int32 Band = 0; Band < CraterBands; ++Band)
            {
                MostInABand = FMath::Max(MostInABand, static_cast<double>(LeftOutByBand[Band]) / (Side * Side));
            }
            const FString At = FString::Printf(TEXT("Baemsekai IV's ground normal, footprint 1/%.0f"), 1.0 / FootprintD);
            TestEqual(At + TEXT(": every pixel the GPU drew is finite"), NotFinite, 0);
            TestTrue(FString::Printf(TEXT("%s: at most 1%% of samples lie on any one crater band's steps (%.3f%%)"), *At, 100.0 * MostInABand),
                MostInABand <= MaxLeftOutPerBand);
            TestTrue(FString::Printf(TEXT("%s: WorldReliefShading::Ground computes what the GPU drew, to 1e-3 a component (%.2e)"), *At, Gap),
                Gap <= 1.0e-3);
            WorstGroundNormal = FMath::Max(WorstGroundNormal, Gap);
            Report.Add(FString::Printf(TEXT("%s: %d compared, %d left out, %d not finite"), *At, Side * Side - LeftOut - NotFinite, LeftOut, NotFinite));
            Report.Add(FString::Printf(TEXT("  ground normal C++ vs GPU:          %.2e"), Gap));
        }
    }

    // -- The cast shadow's map (the developer's ruling on slice (b)'s build:
    // baked, not marched) ------------------------------------------------------
    // M_SkyShadowProbe draws the map's lookup -- the shared file's
    // WR_ShadowMapCoord, WR_ShadowTapsAt and WR_Bilinear over the texture's own
    // texels -- over the probe patch. The map is Baemsekai IV's, baked here at
    // 1,024 columns under two lights 3 degrees over the patch's centre: one
    // turned so the map's frame puts the centre at azimuth 0 (the patch across
    // the terminator), one at +-pi (the patch across the map's seam). The C++
    // is SunShadowMap::Sample at the very D the GPU drew, at five footprints
    // from far finer than a texel (6.1e-3 rad) to one that reads level 2.
    // Held by the measured floor as a rule: 1e-3, or 1.25 x the float
    // mirror's distance from double at that footprint, whichever is larger.
    double WorstShadow = 0.0;
    double WorstShadowFloat = 0.0;
    {
        UMaterial* ShadowShared = LoadObject<UMaterial>(nullptr, SkyMaterial::ShadowProbePath);
        if (!TestNotNull(TEXT("M_SkyShadowProbe is built (Tools/setup_sky_materials.py)"), ShadowShared))
        {
            return false;
        }
        UMaterialInstanceDynamic* ShadowProbe = UMaterialInstanceDynamic::Create(ShadowShared, Test.World);
        const FLinearColor NoBias(0.0f, 0.0f, 0.0f, 0.0f);
        const TArray<FVector3d> Directions = Draw(Test.World, Target, Probe, Selecting(EPass::Direction), NoBias);
        const FVector3d Centre = Directions[(Side / 2) * Side + Side / 2].GetSafeNormal();
        const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Centre).GetSafeNormal();
        const FVector3d North = FVector3d::CrossProduct(Centre, East);
        const FReliefGround Relief(Fourth.Relief);
        const SunShadow::FSunLight HomeLight = SkyProjection::SunLightOf(HomeSky, 4);
        const double Steepest = SunShadow::SteepestSlope(Fourth.Relief);
        struct FPlace
        {
            const TCHAR* Name;
            double Azimuth;
        };
        for (const FPlace& Place : { FPlace{ TEXT("across the terminator"), 0.0 }, FPlace{ TEXT("across the seam"), UE_DOUBLE_PI } })
        {
            // Turn the light about the centre until the map's frame puts the
            // centre at the azimuth asked. Across the terminator the star
            // stands 3 degrees over the centre, whatever the azimuth: the
            // frame's X is square to the world's Z, so with this patch one
            // turn of a 3-degree light only sweeps the centre's azimuth
            // through about 100 degrees, and neither 0 nor pi is in it (the
            // first run: 39.7 degrees off both). The seam needs the azimuth,
            // so its light is also raised, 3 to 75 degrees, until one puts
            // the centre at pi.
            SunShadow::FSunLight Sun = HomeLight;
            double Nearest = TNumericLimits<double>::Max();
            double Raised = 3.0;
            const bool bSeam = Place.Azimuth != 0.0;
            for (const double Degrees : { 3.0, 6.0, 10.0, 15.0, 20.0, 30.0, 45.0, 60.0, 75.0 })
            {
                if (!bSeam && Degrees != 3.0)
                {
                    break;
                }
                const double Elevation = FMath::DegreesToRadians(Degrees);
                for (int32 Turn = 0; Turn < 3600; ++Turn)
                {
                    const double Around = Turn * 2.0 * UE_DOUBLE_PI / 3600.0;
                    SunShadow::FSunLight Trial = HomeLight;
                    Trial.Direction = (Centre * FMath::Sin(Elevation)
                        + (East * FMath::Cos(Around) + North * FMath::Sin(Around)) * FMath::Cos(Elevation)).GetSafeNormal();
                    const FSunShadowMap Shape = SunShadowMap::Shape(Relief, Trial, 1024);
                    const double Phi = FMath::Atan2(FVector3d::DotProduct(Centre, Shape.FrameY), FVector3d::DotProduct(Centre, Shape.FrameX));
                    const double Off = bSeam ? FMath::Abs(FMath::Fmod(Phi - Place.Azimuth + 3.0 * UE_DOUBLE_PI, 2.0 * UE_DOUBLE_PI) - UE_DOUBLE_PI) : 0.0;
                    if (Off < Nearest)
                    {
                        Nearest = Off;
                        Sun = Trial;
                        Raised = Degrees;
                    }
                }
            }
            TestTrue(FString::Printf(TEXT("a light puts the patch %s (%.3f deg off, the star %.0f deg over the centre)"), Place.Name,
                FMath::RadiansToDegrees(Nearest), Raised), Nearest < FMath::DegreesToRadians(0.5));
            Report.Add(FString::Printf(TEXT("Baemsekai IV's shadow map, %s: the star %.0f deg over the patch's centre, the centre %.3f deg from the azimuth asked"),
                Place.Name, Raised, FMath::RadiansToDegrees(Nearest)));
            const TSharedPtr<const FSunShadowMap, ESPMode::ThreadSafe> Baked =
                MakeShared<const FSunShadowMap, ESPMode::ThreadSafe>(SunShadowMap::Bake(Relief, Sun, Steepest, 1024));
            const FSunShadowMap& Map = *Baked;
            UTexture2DDynamic* Texture = ShipSky::MakeShadowTextureNow(Baked, TEXT("Parity"));
            if (!TestNotNull(TEXT("the map uploads"), Texture))
            {
                return false;
            }
            FlushRenderingCommands();
            ShadowProbe->SetTextureParameterValue(SkyMaterial::ShadowMap, Texture);
            ShadowProbe->SetVectorParameterValue(SkyMaterial::ShadowFrameX, ShipSky::ShadowFrameX(Map));
            ShadowProbe->SetVectorParameterValue(SkyMaterial::ShadowFrameZ, ShipSky::ShadowFrameZ(Map));
            for (const double FootprintD : { 1.0e-5, 1.0e-4, 1.5e-3, 6.0e-3, 2.4e-2 })
            {
                const float Footprint = static_cast<float>(FootprintD);
                ShadowProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
                const TArray<FVector3d> Drawn = Draw(Test.World, Target, ShadowProbe, NoBias, NoBias);
                double Gap = 0.0;
                double FloatGap = 0.0;
                int32 NotFinite = 0;
                int32 Shaded = 0;
                for (int32 Index = 0; Index < Side * Side; ++Index)
                {
                    const FVector3d& D = Directions[Index];
                    const FVector3d& Pixel = Drawn[Index];
                    if (!IsFinite(D) || !IsFinite(Pixel))
                    {
                        ++NotFinite;
                        continue;
                    }
                    const double Held = SunShadowMap::Sample(Map, D, static_cast<double>(Footprint));
                    const double Float = SunShadowMap::SampleF32(Map, FVector3f(D), Footprint);
                    Gap = FGap::Wider(Gap, FMath::Abs(Held - Pixel.X));
                    FloatGap = FGap::Wider(FloatGap, FMath::Abs(Held - Float));
                    Shaded += Held < 0.5 ? 1 : 0;
                }
                const double HeldTo = FMath::Max(1.0e-3, FloorRuleFactor * FloatGap);
                const FString At = FString::Printf(TEXT("Baemsekai IV's shadow map, %s, footprint %.1e"), Place.Name, FootprintD);
                TestEqual(At + TEXT(": every pixel the GPU drew is finite"), NotFinite, 0);
                TestTrue(FString::Printf(TEXT("%s: SunShadowMap::Sample computes what the GPU drew, held to %.1e (the float mirror %.2e from double): %.2e"),
                    *At, HeldTo, FloatGap, Gap), Gap <= HeldTo);
                WorstShadow = FMath::Max(WorstShadow, Gap);
                WorstShadowFloat = FMath::Max(WorstShadowFloat, FloatGap);
                Report.Add(FString::Printf(TEXT("%s: %d compared, %.1f%% under half, %d not finite"), *At, Side * Side - NotFinite,
                    100.0 * Shaded / (Side * Side), NotFinite));
                Report.Add(FString::Printf(TEXT("  shadow map C++ vs GPU %.2e, held to %.1e; float mirror vs double %.2e"), Gap, HeldTo, FloatGap));
            }
        }
    }
    Report.Add(FString::Printf(TEXT("SUMMARY shadow map C++-vs-GPU %.2e, float-vs-double %.2e"), WorstShadow, WorstShadowFloat));
    Report.Add(FString::Printf(TEXT("SUMMARY ground normal C++-vs-GPU %.2e"), WorstGroundNormal));
    Report.Add(FString::Printf(TEXT("SUMMARY C++-vs-GPU %.2e, float-C++-vs-GPU %.2e, left out at most %.3f%% (%.3f%% in one crater band)"),
        WorstReliefShared, WorstFloatShared, 100.0 * MostLeftOut, 100.0 * MostLeftOutInABand));
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes") / TEXT("WorldReliefParity");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(Report.Last());
    return true;
}

#endif
