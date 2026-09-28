#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Surface/GroundField.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestWorld.h"
#include "TextureResource.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * FRAMES, NOT A GUARD: Baemsekai III, IV and V under a low sun, for the
 * developer to judge the relief by (sign-off item 2; the ruling on slice
 * (b)'s build, cast shadows). Each world is taken four ways -- from 200 km
 * looking straight down the nose, and from the helm's eye 1.5 m over the
 * ground looking along the terminator, the sun on the side, each at the
 * dusk goto's 10 degrees and at 3 -- and each way twice, with ds.Sky.Shadows
 * 0 and 1. Before the term exists there is no such variable and both are the
 * same frame.
 *
 * Planning measured (the cast-shadow plan) that physical relief casts on
 * under 3% of the ground at ten degrees and on 40-60% at three: the ruled
 * frames are the ten-degree ones, the three-degree ones are where the term
 * is seen.
 *
 *   EYES_TAG=shadows-before Tools/eyes.sh Eyes.ReliefLook
 *   EYES_TAG=shadows-after  Tools/eyes.sh Eyes.ReliefLook
 *   python3 Tools/relief_look_compare.py Saved/Eyes/ReliefLook/shadows-before Saved/Eyes/ReliefLook/shadows-after
 *
 * Writes Saved/Eyes/ReliefLook/<tag>/world_<n>_<view>_shadows<0|1>.png and
 * report.txt: per frame the mean brightness without and with the term --
 * Rec. 601 luma, 0-255, exactly PIL's convert("L"), the measure the ruling's
 * 42.9 -> 17.3 (main's spec) was read with -- the share of the ground it
 * shades (of the pixels at 8 or more without it, those it takes under half),
 * each frame's pixel CRC, and its aliasing: the mean luma gap between the
 * 1x frame and the same view drawn at 2x and box-downsampled, without and
 * with the term. A shadow edge sharper than a pixel shows as alias_on well
 * over alias_off; a still frame at 1x cannot show it. Tools/
 * relief_look_compare.py holds a later run's shadows-off PNGs to these.
 *
 * A fresh editor draws a material as the engine's checkerboard until its
 * shaders compile, hence FinishAllCompilation before each capture.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReliefLookEyesTest, "Eyes.ReliefLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ReliefLookLocal
{
    struct FView
    {
        const TCHAR* Name;
        double AltitudeCm;     // above the datum from orbit; above the ground itself at the ground
        double SunDegrees;
    };

    const FView Views[] = {
        { TEXT("orbit"), 2.0e7, 10.0 },
        { TEXT("ground"), 150.0, 10.0 },
        { TEXT("orbit_low"), 2.0e7, 3.0 },
        { TEXT("ground_low"), 150.0, 3.0 },
    };

    /** PIL's convert("L"): (R 19595 + G 38470 + B 7471 + 0x8000) >> 16. */
    int32 Luma(const FColor& C)
    {
        return (C.R * 19595 + C.G * 38470 + C.B * 7471 + 0x8000) >> 16;
    }

    double MeanLuma(const TArray<FColor>& Pixels)
    {
        double Sum = 0.0;
        for (const FColor& Pixel : Pixels)
        {
            Sum += Luma(Pixel);
        }
        return Pixels.Num() > 0 ? Sum / Pixels.Num() : 0.0;
    }

    /** Of the pixels lit without the term (luma 8 or more), the share it
     *  takes to under half. */
    double Coverage(const TArray<FColor>& Off, const TArray<FColor>& On)
    {
        int32 Lit = 0;
        int32 Shaded = 0;
        for (int32 Index = 0; Index < Off.Num() && Index < On.Num(); ++Index)
        {
            const int32 Was = Luma(Off[Index]);
            if (Was >= 8)
            {
                ++Lit;
                Shaded += 2 * Luma(On[Index]) < Was ? 1 : 0;
            }
        }
        return Lit > 0 ? static_cast<double>(Shaded) / Lit : 0.0;
    }

    uint32 Crc(const TArray<FColor>& Pixels)
    {
        return FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    }

    /** Mean |luma| gap between a 1920 x 1080 frame and a 3840 x 2160 one
     *  box-downsampled 2 x 2. */
    double AliasGap(const TArray<FColor>& Once, const TArray<FColor>& Twice)
    {
        if (Once.Num() != 1920 * 1080 || Twice.Num() != 3840 * 2160)
        {
            return -1.0;
        }
        double Sum = 0.0;
        for (int32 Y = 0; Y < 1080; ++Y)
        {
            for (int32 X = 0; X < 1920; ++X)
            {
                const int32 Row = 2 * Y * 3840 + 2 * X;
                const double Down = 0.25 * (Luma(Twice[Row]) + Luma(Twice[Row + 1]) + Luma(Twice[Row + 3840]) + Luma(Twice[Row + 3841]));
                Sum += FMath::Abs(Down - Luma(Once[Y * 1920 + X]));
            }
        }
        return Sum / (1920.0 * 1080.0);
    }
}

bool FReliefLookEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ReliefLookLocal;
    if (GUsingNullRHI)
    {
        AddError(TEXT("Eyes.ReliefLook renders: run it without -nullrhi"));
        return false;
    }
    const FString TagEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_TAG"));
    const FString Tag = TagEnv.IsEmpty() ? FString(TEXT("untagged")) : TagEnv;
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/ReliefLook") / Tag;
    IFileManager::Get().MakeDirectory(*Dir, true);
    // Absent before the term exists: then both passes draw the same frame.
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    const float ShadowsWere = Shadows ? Shadows->GetFloat() : 1.0f;

    FSkyWorld Test(TEXT("ReliefLookWorld"));
    AActor* Camera = Test.World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("a camera"), Camera))
    {
        return false;
    }
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Camera, TEXT("HelmEye"));
    Camera->SetRootComponent(Capture);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Camera);
    Target->RenderTargetFormat = RTF_RGBA8;
    Target->InitAutoFormat(1920, 1080);
    Target->UpdateResourceImmediate(true);
    // The same view at 2x, box-downsampled against the 1x frame: aliasing.
    UTextureRenderTarget2D* Target2x = NewObject<UTextureRenderTarget2D>(Camera);
    Target2x->RenderTargetFormat = RTF_RGBA8;
    Target2x->InitAutoFormat(3840, 2160);
    Target2x->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->FOVAngle = 60.0f;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Test.BeginPlay();

    TArray<FString> Report;
    for (const int32 Index : { 3, 4, 5 })
    {
        const FSkySystem Here = LocalSystem::Here(Test.World);
        if (!TestTrue(FString::Printf(TEXT("the start system has a body %d"), Index), Here.Bodies.IsValidIndex(Index)))
        {
            return false;
        }
        const FSkyBody& Body = Here.Bodies[Index];
        const FSkyBody* Star = Here.Bodies.FindByPredicate([](const FSkyBody& Candidate) { return Candidate.Kind == ESkyBodyKind::Star; });
        for (const FView& View : Views)
        {
            const bool bGround = View.AltitudeCm < 1.0e5;
            const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, Index, bGround ? 0.0 : View.AltitudeCm,
                Test.Ship->GetFlightState().GetUniversePosition(), ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(View.SunDegrees));
            if (!TestTrue(FString::Printf(TEXT("dusk places over body %d"), Index), Dusk.IsSet() && Star))
            {
                return false;
            }
            if (bGround)
            {
                // On the ground under the same sun, looking along the
                // terminator with the star on the side: each hill's shadow
                // falls across the view, not behind it.
                const FVector Up = (Dusk->Position - Body.Position).GetSafeNormal();
                const FVector Sunward = (Star->Position - Body.Position).GetSafeNormal();
                const FVector Heading = FVector::CrossProduct(Up, Sunward).GetSafeNormal();
                const FGroundFieldRef Field = ShipGround::FromRelief(Body.Relief);
                Test.Ship->PlaceShip(Body.Position + Up * (Body.Radius + Field->Height(FVector3d(Up), 0.0) + View.AltitudeCm),
                                     FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
                Capture->SetWorldLocationAndRotation(PilotEye, FRotator(-10.0, 0.0, 0.0));
            }
            else
            {
                // The nose is at the world's centre: straight down.
                Test.Ship->PlaceShip(Dusk->Position, Dusk->Orientation);
                Capture->SetWorldLocationAndRotation(PilotEye, FRotator::ZeroRotator);
            }
            Test.Step(1.0f / 60.0f);
            Test.Ground->FlushBuildsForTest();
            Test.Step(1.0f / 60.0f);
            // Settle before either pass, in wall-clock time: the first run's
            // ground frames drew Baemsekai III red in the pass without the
            // term and grey in the pass with it -- the same parameters, the
            // same tiles, so a GPU pipeline still compiling after
            // FinishAllCompilation, not the scene. Thirty frames stepped as
            // fast as they come did not clear it; thirty flushed and 50 ms
            // apart do. What remains between two passes is the far tiles'
            // skirts and seams flickering frame to frame (the near ground is
            // identical), which the coverage and the compare tool both see.
            for (int32 Frame = 0; Frame < 30; ++Frame)
            {
                if (GShaderCompilingManager)
                {
                    GShaderCompilingManager->FinishAllCompilation();
                }
                Test.Step(1.0f / 60.0f);
                Test.World->SendAllEndOfFrameUpdates();
                Capture->CaptureScene();
                FlushRenderingCommands();
                FPlatformProcess::Sleep(0.05f);
            }

            TArray<FColor> Frames[2];
            double Alias[2] = { 0.0, 0.0 };
            for (int32 On = 0; On < 2; ++On)
            {
                if (Shadows)
                {
                    Shadows->Set(static_cast<float>(On), ECVF_SetByCode);
                }
                for (int32 Frame = 0; Frame < 8; ++Frame)
                {
                    if (GShaderCompilingManager)
                    {
                        GShaderCompilingManager->FinishAllCompilation();
                    }
                    Test.Step(1.0f / 60.0f);
                    Test.World->SendAllEndOfFrameUpdates();
                    Capture->CaptureScene();
                }
                Target->GameThread_GetRenderTargetResource()->ReadPixels(Frames[On]);
                const FString Name = FString::Printf(TEXT("world_%d_%s_shadows%d.png"), Index, View.Name, On);
                TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*(Dir / Name)));
                TestTrue(FString::Printf(TEXT("%s framed"), *Name), File && FImageUtils::ExportRenderTarget2DAsPNG(Target, *File));
                // The same view at 2x, for the aliasing measure; then back to 1x.
                Capture->TextureTarget = Target2x;
                for (int32 Frame = 0; Frame < 8; ++Frame)
                {
                    Test.Step(1.0f / 60.0f);
                    Test.World->SendAllEndOfFrameUpdates();
                    Capture->CaptureScene();
                }
                TArray<FColor> Twice;
                Target2x->GameThread_GetRenderTargetResource()->ReadPixels(Twice);
                Capture->TextureTarget = Target;
                Alias[On] = AliasGap(Frames[On], Twice);
            }
            const FString Line = FString::Printf(TEXT("frame world_%d_%s sun %.0f off_mean %.2f on_mean %.2f coverage %.4f off_crc %08x on_crc %08x alias_off %.3f alias_on %.3f"),
                Index, View.Name, View.SunDegrees, MeanLuma(Frames[0]), MeanLuma(Frames[1]), Coverage(Frames[0], Frames[1]),
                Crc(Frames[0]), Crc(Frames[1]), Alias[0], Alias[1]);
            Report.Add(Line);
            AddInfo(Line);
        }
    }
    if (Shadows)
    {
        Shadows->Set(ShadowsWere, ECVF_SetByCode);
    }
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return true;
}

#endif
