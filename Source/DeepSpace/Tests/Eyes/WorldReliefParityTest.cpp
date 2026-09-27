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
 * to a maximum absolute difference of 1e-3, over 256 x 256 samples at each
 * of five footprints. A crater band's albedo and slope step at its rim and
 * at the bisector between two sites, where any rounding at all can change
 * the side a sample lands on: samples within 2e-3 cells of a step are left
 * out, and no more than 1% may be.
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
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWorldReliefParityTest,
    "Eyes.WorldReliefParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefParityLocal
{
    constexpr int32 Side = 256;
    constexpr double Tolerance = 1.0e-3;
    constexpr double StepMarginCells = 2.0e-3;
    constexpr double MaxLeftOut = 0.01;

    /** D units a pixel, times filter_pixels: from continents alone down to
     *  every band coarser than 1/12288 of the radius. */
    const double Footprints[] = { 1.0 / 12.0, 1.0 / 96.0, 1.0 / 768.0, 1.0 / 3072.0, 1.0 / 12288.0 };

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
        for (const FVector3d& Pixel : Drawn)
        {
            Wrong += (Pixel.X == Known.R && Pixel.Y == Known.G && Pixel.Z == Known.B) ? 0 : 1;
        }
        const bool bWhole = TestEqual(TEXT("the target holds every pixel"), Drawn.Num(), Side * Side);
        if (!TestEqual(TEXT("and hands back what the probe drew, signed and unrounded, at every one"), Wrong, 0) || !bWhole)
        {
            return false;
        }
    }

    double WorstNewOld = 0.0;
    double WorstCppNew = 0.0;
    double WorstFloatNew = 0.0;
    double WorstCppOld = 0.0;
    double MostLeftOut = 0.0;

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
        for (const double FootprintD : Footprints)
        {
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
            for (int32 Index = 0; Index < Side * Side; ++Index)
            {
                const FVector3d& D = New.Direction[Index];
                Unmatched += D == Old.Direction[Index] ? 0 : 1;
                if (WorldReliefNoise::CraterMargin(D, Footprint, Offset) < StepMarginCells)
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
            TestEqual(At + TEXT(": both probes drew the same directions"), Unmatched, 0);
            TestTrue(FString::Printf(TEXT("%s: at most 1%% of samples lie on a crater's step (%.3f%%)"), *At, 100.0 * LeftOutShare),
                LeftOutShare <= MaxLeftOut);
            TestTrue(FString::Printf(TEXT("%s: the shared file draws what the engine's nodes drew (%s)"), *At, *NewVsOld.Describe()),
                NewVsOld.Worst() <= Tolerance);
            if (World.bHoldCpp)
            {
                TestTrue(FString::Printf(TEXT("%s: the C++ computes what the GPU drew (%s)"), *At, *CppVsNew.Describe()),
                    CppVsNew.Worst() <= Tolerance);
                WorstCppNew = FMath::Max(WorstCppNew, CppVsNew.Worst());
                WorstFloatNew = FMath::Max(WorstFloatNew, FloatVsNew.Worst());
                WorstCppOld = FMath::Max(WorstCppOld, CppVsOld.Worst());
            }
            WorstNewOld = FMath::Max(WorstNewOld, NewVsOld.Worst());
            MostLeftOut = FMath::Max(MostLeftOut, LeftOutShare);
            Report.Add(FString::Printf(TEXT("%s: %d compared, %d left out"), *At, Side * Side - LeftOut, LeftOut));
            Report.Add(TEXT("  shared file vs engine nodes:        ") + NewVsOld.Describe());
            Report.Add(TEXT("  C++ (double) vs shared file:        ") + CppVsNew.Describe());
            Report.Add(TEXT("  C++ (float build) vs shared file:   ") + FloatVsNew.Describe());
            Report.Add(TEXT("  C++ (double) vs engine nodes:       ") + CppVsOld.Describe());
        }
    }

    Report.Add(FString::Printf(TEXT("SUMMARY shared-vs-engine %.2e, C++-vs-shared %.2e, float-C++-vs-shared %.2e, C++-vs-engine %.2e, left out at most %.3f%%"),
        WorstNewOld, WorstCppNew, WorstFloatNew, WorstCppOld, 100.0 * MostLeftOut));
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes") / TEXT("WorldReliefParity");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(Report.Last());
    return true;
}

#endif
