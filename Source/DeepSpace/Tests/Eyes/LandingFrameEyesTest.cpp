#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Surface/GroundField.h"
#include "Surface/WorldGround.h"
#include "TextureResource.h"
#include "Tests/Eyes/EyesFrames.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Slice (b)'s done-when, the part a test can measure: a 4K capture from the
 * helm's eye with the real sky and the real ground, at 50 km (the handover)
 * and 1.5 m over Baemsekai IV, render thread and GPU together, and the
 * tile counts at each. Not the game's frame -- `stat unit` in play is, which
 * the developer reads -- but the same scene, measured the same way every
 * time.
 *
 * The frame is REPORTED, not asserted: the spec on main records the ground
 * at 19.4 ms against 16.6 before any shadow, and rules "profile first, with
 * the cast shadows in, and fix the real cost" -- that work owns the budget.
 *
 * The cast shadow's gate (the cast-shadow plan, Task 1b and Task 4) is
 * priced here. Six cases: the two above under whatever sun Baemsekai IV has
 * at the start, and four at a dusk placed as Eyes.ReliefLook places it --
 * 50 km, 1.5 m and 200 km at the dusk goto's 10 degrees, and 1.5 m at 3,
 * where every pixel of the ground marches. Each case prints its sun's
 * elevation, and its height over the ground itself (agl_m, before and
 * after timing). Each is timed with ds.Sky.Shadows 0 and 1 (when it exists)
 * in ABBA order, five times over after one untimed round, and the medians
 * are reported with their interquartile spread and the raw times. The gate
 * is Tools/landing_frame_gate.py: the frame with the term against this run
 * with EYES_TAG=baseline, taken before the term existed, read against the
 * medians' own error. On minus off is reported beside it, not gated on: at 0
 * the pixel still runs the larger shader.
 *
 * The one assertion, when the switch exists, is that it reaches the
 * materials, proven by pixels at 1.5m_dusk3. Not by comparing one capture
 * with another over the whole frame: the far tiles flicker between captures,
 * and that alone read 6% "coverage" there with no term at all (21-89% in the
 * spike's runs, over a frame nearly black at the game's exposure). So the
 * proof is drawn at the game's exposure some stops brighter (ReadStops;
 * EyesFrames.h), 1080p, twice without
 * the term and once with it, and read only on the pixels the two without it
 * held still: at least 1% of the frame must be lit and held, and the term
 * must take at least 10% of those under half (planning measured 40-60% at 3 degrees). With no switch that coverage
 * is the flicker inside held pixels, which the baselines read at 0.5-2.3% (switch_cov),
 * and it must stay far under 10% for the proof to mean anything. Each line
 * says `switch present` or `switch absent`; absent, a warning says so.
 *
 *   EYES_TAG=baseline Tools/eyes.sh Eyes.LandingFrame      (before any shader change)
 *   EYES_TAG=shadows  Tools/eyes.sh Eyes.LandingFrame
 *   python3 Tools/landing_frame_gate.py Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/shadows
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingFrameEyesTest, "Eyes.LandingFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandingFrameEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("LandingFrameWorld"), 3000);
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Test.Sky);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Test.Sky);
    Target->InitCustomFormat(3840, 2160, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->FOVAngle = 90.0f;
    Capture->CaptureSource = SCS_FinalColorLDR;

    const FSkySystem Here = LocalSystem::Here(Test.World);
    if (!TestTrue(TEXT("the start system has a fourth body"), Here.Bodies.Num() > 4))
    {
        return false;
    }
    const FSkyBody& Fourth = Here.Bodies[4];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FSkyBody* Star = Here.Bodies.FindByPredicate([](const FSkyBody& Candidate) { return Candidate.Kind == ESkyBodyKind::Star; });
    if (!TestNotNull(TEXT("the start system has a star"), Star))
    {
        return false;
    }
    const FVector Sunward = (Star->Position - Fourth.Position).GetSafeNormal();
    // Every case is placed from the opening, whatever the one before did.
    const FUniversePosition Opening = Ship->GetFlightState().GetUniversePosition();
    const FVector StartUp = (Opening - Fourth.Position).GetSafeNormal();
    const FVector StartHeading = FVector::CrossProduct(StartUp, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
    const FString TagEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_TAG"));
    const FString Tag = TagEnv.IsEmpty() ? FString(TEXT("untagged")) : TagEnv;
    // Absent before the term exists (the baseline and the spike): then both
    // halves of each A/B draw the same frame.
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    const float ShadowsWere = Shadows ? Shadows->GetFloat() : 1.0f;
    if (!Shadows)
    {
        AddWarning(TEXT("ds.Sky.Shadows does not exist: on and off time one frame, and the switch is not proven (switch absent). Right before the term is built; after it, the name is wrong."));
    }
    // The switch's proof is read at 1080p: 8M pixels three times over at 4K
    // would add nothing a 2M-pixel frame cannot show.
    UTextureRenderTarget2D* ProofTarget = NewObject<UTextureRenderTarget2D>(Test.Sky);
    ProofTarget->InitCustomFormat(1920, 1080, PF_B8G8R8A8, false);
    ProofTarget->UpdateResourceImmediate(true);
    // Its own capture, so the timed one is never given a view state: the
    // first run that switched the timed capture's on for the proof and off
    // again had 100-190 ms frames in the cases after it.
    USceneCaptureComponent2D* ProofCapture = NewObject<USceneCaptureComponent2D>(Test.Sky);
    ProofCapture->RegisterComponent();
    ProofCapture->TextureTarget = ProofTarget;
    ProofCapture->bCaptureEveryFrame = false;
    ProofCapture->FOVAngle = 90.0f;
    ProofCapture->CaptureSource = SCS_FinalColorLDR;

    // The game step's share of the timed frames, reported beside them.
    double StepSeconds = 0.0;
    int32 StepFrames = 0;
    const auto TimeFrame = [&](float Strength) -> double
    {
        if (Shadows)
        {
            Shadows->Set(Strength, ECVF_SetByCode);
        }
        TArray<FColor> Pixel;
        // The sky writes this frame's Shadows into every material.
        Test.Step(1.0f / 60.0f);
        for (int32 Warm = 0; Warm < 3; ++Warm)
        {
            // A fresh editor draws a material as the engine's default until
            // its shaders compile: timing that would time the wrong frame.
            if (GShaderCompilingManager)
            {
                GShaderCompilingManager->FinishAllCompilation();
            }
            Test.World->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FlushRenderingCommands();
        }
        const double Start = FPlatformTime::Seconds();
        for (int32 Shot = 0; Shot < 20; ++Shot)
        {
            const double StepStart = FPlatformTime::Seconds();
            Test.Step(1.0f / 60.0f);
            StepSeconds += FPlatformTime::Seconds() - StepStart;
            Test.World->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FlushRenderingCommands();
            // One pixel read back: the GPU has finished the frame before the clock reads.
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
        }
        StepFrames += 20;
        return (FPlatformTime::Seconds() - Start) * 1000.0 / 20.0;
    };
    const auto Median = [](TArray<double> Values)
    {
        Values.Sort();
        const int32 N = Values.Num();
        return N == 0 ? 0.0 : (N % 2 ? Values[N / 2] : 0.5 * (Values[N / 2 - 1] + Values[N / 2]));
    };
    // The spread is the interquartile range (the report's contract), not
    // the half-range: one hitch in twenty rounds is not the noise the gate
    // must read against. The raw times are printed beside it.
    const auto Iqr = [](TArray<double> Values)
    {
        Values.Sort();
        const int32 N = Values.Num();
        return N < 4 ? (N == 0 ? 0.0 : Values.Last() - Values[0]) : Values[(3 * N) / 4] - Values[N / 4];
    };
    const auto Joined = [](const TArray<double>& Values)
    {
        FString Out;
        for (const double Value : Values)
        {
            Out += FString::Printf(TEXT("%s%.2f"), Out.IsEmpty() ? TEXT("") : TEXT(","), Value);
        }
        return Out;
    };

    struct FCase
    {
        const TCHAR* Slug;     // one word: Tools/landing_frame_gate.py splits on spaces
        double AglCm;
        double DuskDegrees;    // 0: the start's own sun
        double ReadStops;      // the switch proof's exposure, brighter than the game's exposure
    };
    const FCase Cases[] = {
        { TEXT("50km"), 5.0e6, 0.0, 0.0 },
        { TEXT("1.5m"), 150.0, 0.0, 0.0 },
        { TEXT("50km_dusk10"), 5.0e6, 10.0, 3.0 },
        // Metered as Eyes.ReliefLook's IV ground_low, the same geometry.
        { TEXT("1.5m_dusk3"), 150.0, 3.0, 5.0 },
        { TEXT("200km_dusk10"), 2.0e7, 10.0, 3.0 },
        // Last, on purpose. Taken straight after 50km_dusk10 -- the same
        // ground, from 50 km -- every frame here cost more than the one
        // before (150 ms rising past 1.4 s over 200 frames, the game step a
        // steady 2.7 ms, dynamic shadows off or on), and the cases after it
        // stayed slow (1.5m_dusk3 at 51-90 ms after it, 15.7 ms before it).
        // Taken last, after 200km_dusk10, it is 16.0 ms. A render-side
        // accumulation that depends on the order, found by the cast-shadow
        // plan's baseline and not diagnosed there.
        { TEXT("1.5m_dusk10"), 150.0, 10.0, 4.0 },
    };
    FString Report;
    // EYES_CASES=1.5m_dusk10,... runs only those cases: for looking into one,
    // never for the gate, which reads all six.
    TArray<FString> Only;
    FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_CASES")).ParseIntoArray(Only, TEXT(","));
    for (const FCase& Case : Cases)
    {
        if (!Only.IsEmpty() && !Only.Contains(Case.Slug))
        {
            continue;
        }
        FVector Up = StartUp;
        FVector Heading = StartHeading;
        if (Case.AglCm > 1.0e7)
        {
            // From orbit, as the ruled frames: the goto's own placement, the
            // nose at the world's centre, the frame all ground.
            const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, 4, Case.AglCm, Opening, ShipSky::EGotoSide::Dusk,
                                                                         FMath::DegreesToRadians(Case.DuskDegrees));
            if (!TestTrue(FString::Printf(TEXT("goto dusk places over Baemsekai IV (%s)"), Case.Slug), Dusk.IsSet()))
            {
                continue;
            }
            Up = (Dusk->Position - Fourth.Position).GetSafeNormal();
            Ship->PlaceShip(Dusk->Position, Dusk->Orientation);
        }
        else if (Case.DuskDegrees > 0.0)
        {
            const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, 4, 0.0, Opening, ShipSky::EGotoSide::Dusk,
                                                                         FMath::DegreesToRadians(Case.DuskDegrees));
            if (!TestTrue(FString::Printf(TEXT("goto dusk places over Baemsekai IV (%s)"), Case.Slug), Dusk.IsSet()))
            {
                continue;
            }
            // As Eyes.ReliefLook: along the terminator, the star on the side.
            Up = (Dusk->Position - Fourth.Position).GetSafeNormal();
            Heading = FVector::CrossProduct(Up, Sunward).GetSafeNormal();
        }
        if (Case.AglCm <= 1.0e7)
        {
            Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + Case.AglCm),
                            FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
        }
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        // Looking out and a little down through the cockpit glass, as a pilot
        // would; from orbit straight down the nose.
        Capture->SetWorldLocationAndRotation(PilotEye, Case.AglCm > 1.0e7 ? FRotator::ZeroRotator : FRotator(-15.0, 0.0, 0.0));
        // Settle in wall-clock time before the first timing, as Eyes.ReliefLook
        // does: a pipeline still compiling after FinishAllCompilation drew the
        // ground in the wrong colour there, and would time the wrong frame here.
        for (int32 Settle = 0; Settle < 30; ++Settle)
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
        const double SunDegrees = FMath::RadiansToDegrees(FMath::Asin(FVector::DotProduct(Up, Sunward)));

        StepSeconds = 0.0;
        StepFrames = 0;
        // Over the ground itself, not the datum: the 1.5 m cases printed
        // -1701.89 when the datum was the reference.
        const auto Agl = [&]()
        {
            const FVector3d Offset = Ship->GetFlightState().GetUniversePosition() - Fourth.Position;
            return Offset.Length() - Fourth.Radius - Field->Height(Offset.GetSafeNormal(), 0.0);
        };
        const double AglBefore = Agl();
        // One untimed round first: the first frame timed after the settle
        // was 3-5 ms slow in every case, and ABBA always put it in the off
        // slot.
        TimeFrame(0.0f);
        TimeFrame(1.0f);
        // ABBA, five times over: 0 1 1 0, ... The first baseline, three
        // rounds, read a half-range of 2-3 ms in every case: too wide for a
        // 1 ms gate (the plan's remedy: five rounds).
        TArray<double> Times[2];
        for (int32 Round = 0; Round < 5; ++Round)
        {
            for (const int32 On : { 0, 1, 1, 0 })
            {
                Times[On].Add(TimeFrame(static_cast<float>(On)));
            }
        }
        const double AglAfter = Agl();
        // The switch's proof, at the read exposure: without the term twice,
        // back to back, then with it (the class comment says why).
        TArray<FColor> Frames[3];
        ProofCapture->SetWorldLocationAndRotation(Capture->GetComponentLocation(), Capture->GetComponentRotation());
        EyesFrames::Expose(ProofCapture, Case.ReadStops);
        for (int32 Pass = 0; Pass < 3; ++Pass)
        {
            if (Shadows)
            {
                Shadows->Set(Pass == 2 ? 1.0f : 0.0f, ECVF_SetByCode);
            }
            for (int32 Frame = 0; Frame < 8; ++Frame)
            {
                if (GShaderCompilingManager)
                {
                    GShaderCompilingManager->FinishAllCompilation();
                }
                Test.Step(1.0f / 60.0f);
                Test.World->SendAllEndOfFrameUpdates();
                ProofCapture->CaptureScene();
            }
            ProofTarget->GameThread_GetRenderTargetResource()->ReadPixels(Frames[Pass]);
        }
        const EyesFrames::FShade Shade = EyesFrames::Shade(Frames[0], Frames[1], Frames[2]);
        const double On = Median(Times[1]);
        const double Off = Median(Times[0]);
        const double Spread = FMath::Max(Iqr(Times[0]), Iqr(Times[1]));
        const FString Line = FString::Printf(
            TEXT("case %s sun %.2f on_ms %.3f off_ms %.3f spread_ms %.3f switch %s switch_cov %.4f switch_lit %.4f flicker %.4f read_stops %.0f read_p95 %d frame_crc_on %08x frame_crc_off %08x tiles %d step_ms %.3f agl_m %.2f..%.2f times_on %s times_off %s\n%s\n"),
            Case.Slug, SunDegrees, On, Off, Spread, Shadows ? TEXT("present") : TEXT("absent"),
            Shade.Coverage, Shade.LitShare, Shade.Flicker, Case.ReadStops, EyesFrames::LumaPercentile(Frames[0], 0.95),
            FCrc::MemCrc32(Frames[2].GetData(), Frames[2].Num() * sizeof(FColor)),
            FCrc::MemCrc32(Frames[0].GetData(), Frames[0].Num() * sizeof(FColor)),
            Test.Ground->GetDrawnKeys().Num(), StepFrames > 0 ? StepSeconds * 1000.0 / StepFrames : 0.0,
            AglBefore / 100.0, AglAfter / 100.0,
            *Joined(Times[1]), *Joined(Times[0]), *Test.Ground->Describe());
        Report += Line;
        AddInfo(Line);
        if (On > 16.6)
        {
            AddInfo(FString::Printf(TEXT("%s is over the 16.6 ms frame (%.2f): reported for the spec's profiling, not asserted here"), Case.Slug, On));
        }
        if (Shadows && Case.DuskDegrees > 0.0 && Case.DuskDegrees < 5.0 && Case.AglCm < 1.0e5)
        {
            TestTrue(FString::Printf(TEXT("the switch's proof can see: at %s at least 1%% of the frame is lit and held still (%.2f%%)"),
                Case.Slug, 100.0 * Shade.LitShare), Shade.LitShare >= 0.01);
            TestTrue(FString::Printf(TEXT("the switch reaches the materials: at %s the term shades at least 10%% of the lit, held ground (%.2f%%)"),
                Case.Slug, 100.0 * Shade.Coverage), Shade.Coverage >= 0.10);
        }
    }
    if (Shadows)
    {
        Shadows->Set(ShadowsWere, ECVF_SetByCode);
    }
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/LandingFrame") / Tag;
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("report.txt")));
    return true;
}

#endif
