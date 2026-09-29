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
#include "Tests/Eyes/EyesFrames.h"
#include "Tests/SkyTestWorld.h"
#include "TextureResource.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * FRAMES, NOT A GUARD: Baemsekai III, IV and V under a low sun, for the
 * developer to judge the relief by (sign-off item 2; the ruling on slice
 * (b)'s build, cast shadows). Each world is taken four ways -- from 200 km
 * looking straight down the nose, and from the helm's eye 1.5 m over the
 * ground looking along the terminator, the sun on the side, each at the
 * dusk goto's 10 degrees and at 3 -- and each way with ds.Sky.Shadows 0 and
 * 1. Before the term exists there is no such variable, the line says
 * `switch absent`, and both are the same scene.
 *
 * Planning measured (the cast-shadow plan) that physical relief casts on
 * under 3% of the ground at ten degrees and on 40-60% at three at a pixel's
 * footprint; the bake's are the tiles' vertices on the ground and the map's
 * 8.8 km texels from orbit, where it measured 15-19% on IV and V at three: the ruled
 * frames are the ten-degree ones, the three-degree ones are where the term
 * is seen -- at the read exposure, since at the game's they are black.
 *
 *   EYES_TAG=shadows-before Tools/eyes.sh Eyes.ReliefLook
 *   EYES_TAG=shadows-after  Tools/eyes.sh Eyes.ReliefLook
 *   python3 Tools/relief_look_compare.py Saved/Eyes/ReliefLook/shadows-before Saved/Eyes/ReliefLook/shadows-after
 *
 * Writes Saved/Eyes/ReliefLook/<tag>/:
 *
 * - world_<n>_<view>_shadows<0|1>.png, at the scene capture's default
 *   exposure -- which is not the game's (EyesFrames::Expose) but is what
 *   the ruling's 42.9 -> 17.3 (main's spec) was read at -- and their mean
 *   brightness without and with the term (off_mean, on_mean): Rec. 601
 *   luma, 0-255, exactly PIL's convert("L"), the ruling's measure.
 * - world_<n>_<view>_read_shadows<0|0b|1>.png, at the game's exposure
 *   ReadStops brighter (EyesFrames.h says why): two captures without the
 *   term, back to back, and one with it. Every measure is read here: the
 *   share of the frame lit and held still between the two without the term
 *   (lit, held), the share of that the term takes under half (coverage),
 *   the same measure between the two without it over every lit pixel
 *   (flicker: the noise, never the term), and each frame's aliasing -- the
 *   mean luma gap between the 1x frame and the same view drawn at 2x and
 *   box-downsampled, over the held pixels -- for both captures without the
 *   term (alias_off, alias_off2: their gap is the measure's noise) and with
 *   it (alias_on). A shadow edge sharper than a pixel shows as alias_on over
 *   alias_off by more than that noise; a still frame at 1x cannot show it.
 *   read_p95 is the read frame's 95th-percentile luma, for re-choosing
 *   ReadStops.
 *
 * Tools/relief_look_compare.py holds a later run's read frames without the
 * term to these, on the pixels both runs held still.
 *
 * EYES_METER=1 draws each view without the term at the game's exposure
 * -6, -3, ... 12 stops brighter and prints each one's lit share and 95th
 * and 99.9th percentiles, and nothing else: how ReadStops below were chosen
 * (a p95 near 200 and the 99.9th under 255).
 *
 * A fresh editor draws a material as the engine's checkerboard until its
 * shaders compile, hence FinishAllCompilation before each capture.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReliefLookEyesTest, "Eyes.ReliefLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ReliefLookLocal
{
    using EyesFrames::Luma;

    struct FView
    {
        const TCHAR* Name;
        double AltitudeCm;     // above the datum from orbit; above the ground itself at the ground
        double SunDegrees;
        double ReadStops[3];   // brighter than the game's exposure, for III, IV and V (EYES_METER)
    };

    const FView Views[] = {
        // From EYES_METER (2026-09-28): at 0 stops, the game's own exposure,
        // every 3-degree frame and IV's ground at 10 had under 2% of the
        // frame lit, so the game shows them black too.
        { TEXT("orbit"), 2.0e7, 10.0, { 3.0, 3.0, 3.0 } },
        { TEXT("ground"), 150.0, 10.0, { 3.0, 4.0, 3.0 } },
        { TEXT("orbit_low"), 2.0e7, 3.0, { 4.0, 4.0, 4.0 } },
        { TEXT("ground_low"), 150.0, 3.0, { 4.0, 5.0, 4.0 } },
    };

    double MeanLuma(const TArray<FColor>& Pixels)
    {
        double Sum = 0.0;
        for (const FColor& Pixel : Pixels)
        {
            Sum += Luma(Pixel);
        }
        return Pixels.Num() > 0 ? Sum / Pixels.Num() : 0.0;
    }

    uint32 Crc(const TArray<FColor>& Pixels)
    {
        return FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    }

    /** Mean |luma| gap between a 1920 x 1080 frame and a 3840 x 2160 one
     *  box-downsampled 2 x 2, over the pixels Held marks (all, if empty). */
    double AliasGap(const TArray<FColor>& Once, const TArray<FColor>& Twice, const TBitArray<>& Held)
    {
        if (Once.Num() != 1920 * 1080 || Twice.Num() != 3840 * 2160)
        {
            return -1.0;
        }
        double Sum = 0.0;
        int32 Count = 0;
        for (int32 Y = 0; Y < 1080; ++Y)
        {
            for (int32 X = 0; X < 1920; ++X)
            {
                if (Held.Num() > 0 && !Held[Y * 1920 + X])
                {
                    continue;
                }
                const int32 Row = 2 * Y * 3840 + 2 * X;
                const double Down = 0.25 * (Luma(Twice[Row]) + Luma(Twice[Row + 1]) + Luma(Twice[Row + 3840]) + Luma(Twice[Row + 3841]));
                Sum += FMath::Abs(Down - Luma(Once[Y * 1920 + X]));
                ++Count;
            }
        }
        return Count > 0 ? Sum / Count : 0.0;
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
    const bool bMeter = FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_METER")) == TEXT("1");
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/ReliefLook") / Tag;
    IFileManager::Get().MakeDirectory(*Dir, true);
    // Absent before the term exists: then both passes draw the same scene,
    // and the report says so rather than passing for a run with the switch.
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    const float ShadowsWere = Shadows ? Shadows->GetFloat() : 1.0f;
    if (!Shadows)
    {
        AddWarning(TEXT("ds.Sky.Shadows does not exist: every pair is one scene twice (switch absent). Right before the term is built; after it, the name is wrong."));
    }

    FSkyWorld Test(TEXT("ReliefLookWorld"), 8, EShadows::On);
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
    // The cast shadow is baked: every world's map before any frame.
    Test.Step(1.0f / 60.0f);
    Test.Sky->FlushShadowBakesForTest();

    // Eight frames stepped and captured, then the target read back.
    const auto Shoot = [&](UTextureRenderTarget2D* Into, TArray<FColor>& Pixels)
    {
        Capture->TextureTarget = Into;
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
        Into->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        Capture->TextureTarget = Target;
    };
    const auto Save = [&](const FString& Name)
    {
        TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*(Dir / Name)));
        TestTrue(FString::Printf(TEXT("%s framed"), *Name), File && FImageUtils::ExportRenderTarget2DAsPNG(Target, *File));
    };
    const auto SetShadows = [&](int32 On)
    {
        if (Shadows)
        {
            Shadows->Set(static_cast<float>(On), ECVF_SetByCode);
        }
    };

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
            EyesFrames::Expose(Capture, EyesFrames::CaptureDefault);
            Test.Step(1.0f / 60.0f);
            Test.Ground->FlushBuildsForTest();
            Test.Step(1.0f / 60.0f);
            // Settle before either pass, in wall-clock time: the first run's
            // ground frames drew Baemsekai III red in the pass without the
            // term and grey in the pass with it -- the same parameters, the
            // same tiles, so a GPU pipeline still compiling after
            // FinishAllCompilation, not the scene. Thirty frames stepped as
            // fast as they come did not clear it; thirty flushed and 50 ms
            // apart do. What remains between two captures is the far tiles'
            // skirts and seams flickering frame to frame, which is why every
            // measure below is read where two captures held still.
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
            const FString Name = FString::Printf(TEXT("world_%d_%s"), Index, View.Name);

            if (bMeter)
            {
                SetShadows(0);
                FString Line = FString::Printf(TEXT("meter %s"), *Name);
                for (int32 Stops = -6; Stops <= 12; Stops += 3)
                {
                    EyesFrames::Expose(Capture, Stops);
                    TArray<FColor> Pixels;
                    Shoot(Target, Pixels);
                    int32 Lit = 0;
                    for (const FColor& Pixel : Pixels)
                    {
                        Lit += Luma(Pixel) >= EyesFrames::LitLuma ? 1 : 0;
                    }
                    Line += FString::Printf(TEXT(" s%d lit %.4f p95 %d p999 %d"), Stops, Pixels.Num() ? static_cast<double>(Lit) / Pixels.Num() : 0.0,
                        EyesFrames::LumaPercentile(Pixels, 0.95), EyesFrames::LumaPercentile(Pixels, 0.999));
                }
                EyesFrames::Expose(Capture, EyesFrames::CaptureDefault);
                Report.Add(Line);
                AddInfo(Line);
                continue;
            }

            // The ruled frames, at the game's exposure.
            TArray<FColor> Game[2];
            for (int32 On = 0; On < 2; ++On)
            {
                SetShadows(On);
                Shoot(Target, Game[On]);
                Save(FString::Printf(TEXT("%s_shadows%d.png"), *Name, On));
            }

            // The measured frames, at the read exposure: without the term
            // twice, back to back, then with it; each with its 2x twin.
            const double Stops = View.ReadStops[Index - 3];
            EyesFrames::Expose(Capture, Stops);
            TArray<FColor> Frames[3];
            TArray<FColor> Twice[3];
            const TCHAR* Suffix[3] = { TEXT("0"), TEXT("0b"), TEXT("1") };
            for (int32 Pass = 0; Pass < 3; ++Pass)
            {
                SetShadows(Pass == 2 ? 1 : 0);
                Shoot(Target, Frames[Pass]);
                Save(FString::Printf(TEXT("%s_read_shadows%s.png"), *Name, Suffix[Pass]));
                Shoot(Target2x, Twice[Pass]);
            }
            EyesFrames::Expose(Capture, EyesFrames::CaptureDefault);
            const EyesFrames::FShade Shade = EyesFrames::Shade(Frames[0], Frames[1], Frames[2]);
            TBitArray<> Held(false, Frames[0].Num());
            for (int32 Pixel = 0; Pixel < Frames[0].Num() && Pixel < Frames[1].Num(); ++Pixel)
            {
                Held[Pixel] = EyesFrames::Held(Frames[0][Pixel], Frames[1][Pixel]);
            }
            const FString Line = FString::Printf(
                TEXT("frame %s sun %.0f off_mean %.2f on_mean %.2f read_stops %.0f read_p95 %d lit %.4f held %.4f coverage %.4f flicker %.4f off_crc %08x on_crc %08x alias_off %.3f alias_off2 %.3f alias_on %.3f switch %s"),
                *Name, View.SunDegrees, MeanLuma(Game[0]), MeanLuma(Game[1]), Stops, EyesFrames::LumaPercentile(Frames[0], 0.95),
                Shade.LitShare, Shade.HeldShare, Shade.Coverage, Shade.Flicker, Crc(Game[0]), Crc(Game[1]),
                AliasGap(Frames[0], Twice[0], Held), AliasGap(Frames[1], Twice[1], Held), AliasGap(Frames[2], Twice[2], Held),
                Shadows ? TEXT("present") : TEXT("absent"));
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
