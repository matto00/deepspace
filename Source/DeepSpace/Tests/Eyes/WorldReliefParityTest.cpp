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
#include "Sky/SkyMaterialContract.h"
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
 *   - to M_SkyReliefProbeLegacy, the same terms from the engine's own noise
 *     nodes: "the orbital look unchanged", as a number;
 *   - to the C++, at the very D the GPU drew;
 *
 * over 256 x 256 samples at each of five footprints, per footprint and per
 * term, at the measured float floor (the developer's rulings after the
 * spike and at R2): every value and every detail term at 1/12, 1/96 and
 * 1/768 to a maximum absolute difference of 1e-3, and every value at 1/3072;
 * crater slopes to 5e-3 from 1/96 down (the crater offsets put even the
 * coarsest crater band near noise coordinate 8,000); detail slopes to 5e-3
 * at 1/3072; at 1/12288 detail values to 1.5e-3 and detail slopes to 8e-3.
 * Every float evaluation, the engine's own nodes included, differs from
 * double by that much at those noise coordinates. That table holds the
 * shared file to the engine's nodes; failing it is a port bug.
 *
 * The C++ is held to the GPU by the measured floor as a rule (the
 * developer's ruling applying R2): each term, at each footprint, to the
 * larger of the table's value and 1.25 x the engine's own nodes' distance
 * from the double C++, measured on the same samples in the same run. The
 * report prints what each term was held to, and marks those the floor set.
 * The rule's allowance grows with any C++-only error, so the C++'s own
 * guard is DeepSpace.Surface.WorldRelief.KnownValues (exact double values);
 * this test guards the file the GPU compiles.
 *
 * Slice (b) tightens both at the root (the lattice offset split into
 * integer and fraction). A
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
 * Writes Saved/Eyes/WorldReliefParity/report.txt: every gap, and two
 * diagnostics reported but not asserted -- the file's float build against
 * the GPU, and the C++ against the engine's nodes -- which tell a file that
 * computes the wrong thing from float's own floor (landing task R1's
 * verdict table).
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

    /** D units a pixel, times filter_pixels: from continents alone down to
     *  every band coarser than 1/12288 of the radius. */
    const FTolerance Footprints[] = {
        { 1.0 / 12.0,    1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3 },
        { 1.0 / 96.0,    1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3, 5.0e-3 },
        { 1.0 / 768.0,   1.0e-3, 1.0e-3, 1.0e-3, 1.0e-3, 5.0e-3 },
        { 1.0 / 3072.0,  1.0e-3, 1.0e-3, 1.0e-3, 5.0e-3, 5.0e-3 },
        { 1.0 / 12288.0, 1.0e-3, 1.5e-3, 1.0e-3, 8.0e-3, 5.0e-3 },
    };

    /** The C++-vs-GPU allowance over the engine's own distance from double
     *  (the measured floor as a rule). */
    constexpr double FloorRuleFactor = 1.25;

    /** A barren world's seed offset until task R4 hands the test Baemsekai
     *  IV's: multiples of 1/256, as every real one is. */
    const FVector3d Offset(12.5, 200.25, 77.0);

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

    /** The largest absolute difference seen, term by term. */
    struct FGap
    {
        double Continent = 0.0;
        double Detail = 0.0;
        double CraterAlbedo = 0.0;
        double DetailSlope = 0.0;
        double CraterSlope = 0.0;

        void Widen(const FFaceTerms& A, const FFaceTerms& B)
        {
            Continent = FMath::Max(Continent, FMath::Abs(A.Continent - B.Continent));
            Detail = FMath::Max(Detail, FMath::Abs(A.Detail - B.Detail));
            CraterAlbedo = FMath::Max(CraterAlbedo, FMath::Abs(A.CraterAlbedo - B.CraterAlbedo));
            DetailSlope = FMath::Max(DetailSlope, (A.DetailSlope - B.DetailSlope).GetAbsMax());
            CraterSlope = FMath::Max(CraterSlope, (A.CraterSlope - B.CraterSlope).GetAbsMax());
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

    FString DescribeTolerance(const FTolerance& To)
    {
        return FString::Printf(TEXT("continent %.1e, detail %.1e, crater albedo %.1e, detail slope %.1e, crater slope %.1e"),
            To.Continent, To.Detail, To.CraterAlbedo, To.DetailSlope, To.CraterSlope);
    }

    /** The measured floor as a rule: each term to the larger of the ruled
     *  table's value and FloorRuleFactor x the engine's own nodes' distance
     *  from the double C++ at this footprint, in this run. Says, per term,
     *  whether the table or the floor set it. */
    FTolerance CppHeldTo(const FTolerance& Table, const FGap& EngineVsDouble, FString& OutSetBy)
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
    UMaterial* Legacy = LoadObject<UMaterial>(nullptr, SkyMaterial::ReliefProbeLegacyPath);
    if (!TestNotNull(TEXT("M_SkyReliefProbe is built (Tools/setup_sky_materials.py)"), Shared)
        || !TestNotNull(TEXT("M_SkyReliefProbeLegacy is built"), Legacy))
    {
        return false;
    }

    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Test.World);
    Target->RenderTargetFormat = RTF_RGBA32f;
    Target->ClearColor = FLinearColor::Transparent;
    Target->bAutoGenerateMips = false;
    Target->InitAutoFormat(Side, Side);
    Target->UpdateResourceImmediate(true);
    UMaterialInstanceDynamic* NewProbe = UMaterialInstanceDynamic::Create(Shared, Test.World);
    UMaterialInstanceDynamic* OldProbe = UMaterialInstanceDynamic::Create(Legacy, Test.World);

    TArray<FString> Report;

    // -- The pipe: a probe that selects nothing draws its bias, exactly -------
    // Signed, above one, and below a float's half-precision: a target that
    // clamped, rounded, tonemapped or drew the grey default fails here.
    {
        const FLinearColor Known(-0.375f, 1234.5f, 3.0e-5f, 0.0f);
        const TArray<FVector3d> Drawn = Draw(Test.World, Target, NewProbe, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), Known);
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

    double WorstNewOld = 0.0;
    double WorstCppNew = 0.0;
    double WorstFloatNew = 0.0;
    double WorstCppOld = 0.0;
    double MostLeftOut = 0.0;
    double MostLeftOutInABand = 0.0;

    // Barren (stretch 1) against both the engine's nodes and the C++; a
    // giant (stretch 6, the belts' streaking) against the engine's nodes,
    // and the C++ reported only: a giant has no ground.
    struct FWorldCase { const TCHAR* Name; float Banding; double Stretch; bool bHoldCpp; };
    const FWorldCase Worlds[] = { { TEXT("barren"), 0.0f, 1.0, true }, { TEXT("giant"), 1.0f, 6.0, false } };
    for (const FWorldCase& World : Worlds)
    {
        for (UMaterialInstanceDynamic* Probe : { NewProbe, OldProbe })
        {
            Probe->SetVectorParameterValue(SkyMaterial::SurfaceSeed,
                FLinearColor(static_cast<float>(Offset.X), static_cast<float>(Offset.Y), static_cast<float>(Offset.Z), 8.0f));
            Probe->SetScalarParameterValue(SkyMaterial::Banding, World.Banding);
        }
        for (const FTolerance& To : Footprints)
        {
            const double FootprintD = To.Footprint;
            const float Footprint = static_cast<float>(FootprintD);
            NewProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
            OldProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
            const FDrawn New = DrawAll(Test.World, Target, NewProbe);
            const FDrawn Old = DrawAll(Test.World, Target, OldProbe);

            FGap NewVsOld;
            FGap CppVsNew;
            FGap FloatVsNew;
            FGap CppVsOld;
            int32 LeftOut = 0;
            int32 Unmatched = 0;
            const int32 CraterBands = WorldReliefNoise::Bands().CraterIndices.Num();
            TArray<int32> LeftOutByBand;
            LeftOutByBand.Init(0, CraterBands);
            for (int32 Index = 0; Index < Side * Side; ++Index)
            {
                const FVector3d& D = New.Direction[Index];
                Unmatched += D == Old.Direction[Index] ? 0 : 1;
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
                const FFaceTerms Gpu = New.At(Index);
                const FFaceTerms Engine = Old.At(Index);
                const FFaceTerms Cpp = WorldReliefNoise::FaceF64(D, Footprint, Offset, World.Stretch);
                const FFaceTerms Float = WorldReliefNoise::FaceF32(FVector3f(D), Footprint, FVector3f(Offset), static_cast<float>(World.Stretch));
                NewVsOld.Widen(Gpu, Engine);
                CppVsNew.Widen(Cpp, Gpu);
                FloatVsNew.Widen(Float, Gpu);
                CppVsOld.Widen(Cpp, Engine);
            }
            const double LeftOutShare = static_cast<double>(LeftOut) / (Side * Side);
            const FString At = FString::Printf(TEXT("%s, footprint 1/%.0f"), World.Name, 1.0 / FootprintD);
            const FString Held = DescribeTolerance(To);
            FString CppSetBy;
            const FTolerance CppTo = CppHeldTo(To, CppVsOld, CppSetBy);
            const FString CppHeld = DescribeTolerance(CppTo);
            TestEqual(At + TEXT(": both probes drew the same directions"), Unmatched, 0);
            FString ByBand;
            double MostInABand = 0.0;
            for (int32 Band = 0; Band < CraterBands; ++Band)
            {
                const double Share = static_cast<double>(LeftOutByBand[Band]) / (Side * Side);
                MostInABand = FMath::Max(MostInABand, Share);
                ByBand += FString::Printf(TEXT("%s%.3f%%"), Band == 0 ? TEXT("") : TEXT(", "), 100.0 * Share);
            }
            TestTrue(FString::Printf(TEXT("%s: at most 1%% of samples lie on any one crater band's steps (%s)"), *At, *ByBand),
                MostInABand <= MaxLeftOutPerBand);
            TestTrue(FString::Printf(TEXT("%s: the shared file draws what the engine's nodes drew, held to %s (%s)"),
                *At, *Held, *NewVsOld.Describe()), NewVsOld.Within(To));
            if (World.bHoldCpp)
            {
                TestTrue(FString::Printf(TEXT("%s: the C++ computes what the GPU drew, held to %s by the measured floor as a rule (%s)"),
                    *At, *CppHeld, *CppVsNew.Describe()), CppVsNew.Within(CppTo));
                WorstCppNew = FMath::Max(WorstCppNew, CppVsNew.Worst());
                WorstFloatNew = FMath::Max(WorstFloatNew, FloatVsNew.Worst());
                WorstCppOld = FMath::Max(WorstCppOld, CppVsOld.Worst());
            }
            WorstNewOld = FMath::Max(WorstNewOld, NewVsOld.Worst());
            MostLeftOut = FMath::Max(MostLeftOut, LeftOutShare);
            MostLeftOutInABand = FMath::Max(MostLeftOutInABand, MostInABand);
            Report.Add(FString::Printf(TEXT("%s: %d compared, %d left out (by crater band: %s)"), *At, Side * Side - LeftOut, LeftOut, *ByBand));
            Report.Add(TEXT("  shared file held to (the table):    ") + Held);
            Report.Add(TEXT("  C++ held to (the rule):             ") + CppHeld);
            Report.Add(TEXT("    set by:                           ") + CppSetBy);
            Report.Add(TEXT("  shared file vs engine nodes:        ") + NewVsOld.Describe());
            Report.Add(TEXT("  C++ (double) vs shared file:        ") + CppVsNew.Describe());
            Report.Add(TEXT("  C++ (float build) vs shared file:   ") + FloatVsNew.Describe());
            Report.Add(TEXT("  C++ (double) vs engine nodes:       ") + CppVsOld.Describe());
        }
    }

    Report.Add(FString::Printf(TEXT("SUMMARY shared-vs-engine %.2e, C++-vs-shared %.2e, float-C++-vs-shared %.2e, C++-vs-engine %.2e, left out at most %.3f%% (%.3f%% in one crater band)"),
        WorstNewOld, WorstCppNew, WorstFloatNew, WorstCppOld, 100.0 * MostLeftOut, 100.0 * MostLeftOutInABand));
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes") / TEXT("WorldReliefParity");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(Report.Last());
    return true;
}

#endif
