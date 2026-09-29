#include "Algo/AnyOf.h"
#include "Components/SceneCaptureComponent2D.h"
#include "DynamicRHI.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "ShaderCompiler.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainGroundComponent.h"
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
 * time: the game step, then the capture, then a one-pixel read-back, in
 * series, so game thread, render thread and GPU are summed, where play
 * overlaps them.
 *
 * The frame is ASSERTED, every case within 16.6 ms, still or moving (slice
 * (b)'s last open items, 2026-09-29: "every case, moving or still, must stay
 * within 16.6 ms at 4K"); it was reported only, while the frame ruling
 * ("profile first, with the cast shadows in, and fix the real cost") owned
 * it, and the landing plan's Task 39 notes hold that profile.
 *
 * With the cast shadows in, as the game ships them: the tiles' vertices
 * carry theirs and every world's map is baked before any case is timed.
 * Six cases: the two above under the start's sun, and four at a dusk placed
 * as Eyes.ReliefLook places it -- 50 km, 1.5 m and 200 km at the dusk goto's
 * 10 degrees, and 1.5 m at 3. And six in motion (slice (b)'s last open
 * items: the ship is the origin, so a ship under way moves every drawn tile
 * every frame, which no still case paid for): HOVER and cruise full ahead at
 * the skim cap at 1.5 m and 500 m, and the drive's first notch with the nose
 * 10 degrees down at 50 km, each under the start's sun and at the 10-degree
 * dusk; each flies a second before its clock, asserts it kept at least half
 * the speed it was set to (speed_mps), and has no switch proof. Each case
 * prints its sun's elevation and its height over the ground itself (agl_m,
 * before and after timing). Each is
 * timed with ds.Sky.Shadows 0 and 1 in ABBA order, five times over after
 * one untimed round, and the medians are reported with their interquartile
 * spread and the raw times. Tools/landing_frame_gate.py reads two runs'
 * reports against the medians' own error.
 *
 * The cast shadow is BAKED (the developer's ruling on slice (b)'s build,
 * 2026-09-28): the tiles' vertices carry it, each world's map is baked when
 * the system loads, and the ruling is that it costs nothing per frame. So
 * the gate is that the frame does not rise: each of the six cases with
 * ds.Sky.Shadows 1, against the same case in the run with EYES_TAG=baseline,
 * taken before any shadow existed, read by Tools/landing_frame_gate.py
 * --not-rise against the medians' own error. On minus off is reported
 * beside it, and is about 0 by construction: the lookup runs either way.
 *
 * EVERY ROUND IS ITS OWN ENGINE FRAMES. The test is latent: one step (a
 * case's placement, one timed round, the proof, one profile variant) per
 * engine frame, four frames apart. Inside one RunTest no engine frame ends,
 * and the Vulkan RHI retires the resources a capture frees only at a
 * frame's end -- while a Development build checks each new one against all
 * those still queued (FDeferredDeletionQueue2's double-delete scan). The
 * queue grew with every capture, and so did every capture's cost: the
 * profile read DeleteRHIResources on the RHI thread at 4.8 ms a capture in
 * the second case and 24 ms in the sixth, an empty 4K capture at 4 ms and
 * then 32, and the order-dependent "accumulation" this test once recorded
 * (1.5m_dusk10 at 16 ms taken last, 51-90 ms after another case) was that
 * queue. The game ends a frame every frame.
 *
 * Beside the frame, the switch is asserted to reach the materials, proven by
 * pixels at 1.5m_dusk3. Not by comparing one capture with another over the
 * whole frame: the far tiles flicker between captures. So the proof is drawn
 * at the game's exposure some stops brighter (ReadStops; EyesFrames.h),
 * 1080p, twice without the term and once with it, and read only on the
 * pixels the two without it held still: at least 1% of the frame must be lit
 * and held, and the term must take at least 10% of those under half.
 *
 * EYES_PROFILE=1 adds, after each case, `profile` lines: the frame broken
 * into game step, the capture's game-thread half, the render thread's, and
 * the read-back's wait, and the same frame with the ground, the sky, both,
 * or the ground's material taken away, and at a quarter of the pixels. A
 * named trace region brackets each case's timed rounds and each variant
 * (`LF <case> timed`, `LF <case> <variant>`), for Insights'
 * ExportTimerStatistics -region= over a run with -trace=cpu,gpu,frame,region.
 *
 *   EYES_TAG=<tag> Tools/eyes.sh Eyes.LandingFrame
 *   python3 Tools/landing_frame_gate.py Saved/Eyes/LandingFrame/<before> Saved/Eyes/LandingFrame/<after> [budget]
 *   EYES_TAG=baseline      Tools/eyes.sh Eyes.LandingFrame      (before any shadow: Task 1's)
 *   EYES_TAG=shadows-baked Tools/eyes.sh Eyes.LandingFrame
 *   python3 Tools/landing_frame_gate.py --not-rise Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/shadows-baked
 *
 * Cast shadow, baked (2026-09-29): GO -- not rising; cost over the before-baseline, ms: 50km -3.09, 1.5m -5.83, 50km_dusk10 -1.52, 1.5m_dusk10 -3.85, 1.5m_dusk3 -3.25, 200km_dusk10 -5.43, each within its band (1.05, 0.37, 0.20, 0.26, 0.20, 0.36); whole frames 8.05..13.21 ms (not gated: the spec's profiling); the switch at 1.5m_dusk3 took 98.6% of the lit and held pixels. The baseline predates this test's per-frame harness (19342c9) and the crater kernel's 8 corners (fc829a7), both of which cut the frame, so GO here cannot price the shadow alone; on minus off in the same run is -0.09..+0.08 ms. Against the last run before the ground's ancestors were built (z-close, the same harness, shadows in), the dusk ground cases rose +0.53 (50km_dusk10), +1.43 (1.5m_dusk3), +1.46 ms (1.5m_dusk10), with the drawn tiles 174 -> 347, 694 -> 1116, 685 -> 1091: the cut is now drawn as selected, where before it fell back to coarse ancestors that were never built.
 */
namespace
{
    using namespace SkyTestWorld;

    /** Engine frames between two steps: the Vulkan RHI retires a freed
     *  resource two frames after the frame it was freed in. */
    constexpr int32 FramesBetweenSteps = 4;

    /** How the ship moves while a case is timed. The ship is the origin, so
     *  a ship in motion moves every drawn tile every frame: the still cases
     *  never paid for that (slice (b)'s last open items). */
    enum class EMotion : uint8
    {
        Still,
        /** HOVER, cruise full ahead: held to the skim cap, max(20 m/s, AGL / 2.5 s). */
        Skim,
        /** On the gear the flight law holds the ship at rest against the
         *  first rise ahead (the footprint cap: the first run read 0.0 m/s
         *  at 1.5 m under both suns), so there the ship is carried along
         *  the ground at the skim cap, placed every frame at its height over
         *  the ground. The frame cannot tell who moved it. */
        Glide,
        /** The drive's first notch, the nose 10 degrees under the level: the
         *  drive's approach toward its floor, the soft cap holding it. */
        Drive,
    };

    struct FLandingCase
    {
        const TCHAR* Slug;     // one word: Tools/landing_frame_gate.py splits on spaces
        double AglCm;
        double DuskDegrees;    // 0: the start's own sun
        double ReadStops;      // the switch proof's exposure, brighter than the game's exposure
        EMotion Motion = EMotion::Still;
    };

    const FLandingCase LandingCases[] = {
        { TEXT("50km"), 5.0e6, 0.0, 0.0 },
        { TEXT("1.5m"), 150.0, 0.0, 0.0 },
        { TEXT("50km_dusk10"), 5.0e6, 10.0, 3.0 },
        // Metered as Eyes.ReliefLook's IV ground_low, the same geometry.
        { TEXT("1.5m_dusk3"), 150.0, 3.0, 5.0 },
        { TEXT("200km_dusk10"), 2.0e7, 10.0, 3.0 },
        { TEXT("1.5m_dusk10"), 150.0, 10.0, 4.0 },
        // In motion (slice (b)'s last open items): at the skim cap at 1.5 m
        // and 500 m, and on the drive's approach at 50 km, noon and dusk.
        { TEXT("1.5m_skim"), 150.0, 0.0, 0.0, EMotion::Glide },
        { TEXT("500m_skim"), 5.0e4, 0.0, 0.0, EMotion::Skim },
        { TEXT("50km_drive"), 5.0e6, 0.0, 0.0, EMotion::Drive },
        { TEXT("1.5m_skim_dusk10"), 150.0, 10.0, 0.0, EMotion::Glide },
        { TEXT("500m_skim_dusk10"), 5.0e4, 10.0, 0.0, EMotion::Skim },
        { TEXT("50km_drive_dusk10"), 5.0e6, 10.0, 0.0, EMotion::Drive },
    };

    /** The case the switch's proof reads (1.5m_dusk3): the only one under a
     *  sun low enough, from near enough, for the term to shade held ground,
     *  and still, so the proof's frames hold. A run that leaves it out
     *  proves nothing about the switch, and says so (RunTest). */
    bool ProvesTheSwitch(const FLandingCase& Case)
    {
        return Case.Motion == EMotion::Still && Case.DuskDegrees > 0.0 && Case.DuskDegrees < 5.0 && Case.AglCm < 1.0e5;
    }

    /** The frame, ms: 60 Hz. */
    constexpr double FrameBudgetMs = 16.6;

    /** A moving case's ship must move: at least this share of the speed it
     *  was set to, over the timed rounds, or the case timed a ship at rest. */
    constexpr double MovingShare = 0.5;

    double MedianOf(TArray<double> Values)
    {
        Values.Sort();
        const int32 N = Values.Num();
        return N == 0 ? 0.0 : (N % 2 ? Values[N / 2] : 0.5 * (Values[N / 2 - 1] + Values[N / 2]));
    }

    /** The interquartile range (the report's contract), not the half-range:
     *  one hitch in twenty rounds is not the noise the gate must read
     *  against. The raw times are printed beside it. */
    double IqrOf(TArray<double> Values)
    {
        Values.Sort();
        const int32 N = Values.Num();
        return N < 4 ? (N == 0 ? 0.0 : Values.Last() - Values[0]) : Values[(3 * N) / 4] - Values[N / 4];
    }

    FString Joined(const TArray<double>& Values)
    {
        FString Out;
        for (const double Value : Values)
        {
            Out += FString::Printf(TEXT("%s%.2f"), Out.IsEmpty() ? TEXT("") : TEXT(","), Value);
        }
        return Out;
    }

    USceneCaptureComponent2D* NewCapture(AActor* Owner, int32 Width, int32 Height, UTextureRenderTarget2D*& OutTarget)
    {
        USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Owner);
        Capture->RegisterComponent();
        OutTarget = NewObject<UTextureRenderTarget2D>(Owner);
        OutTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
        OutTarget->UpdateResourceImmediate(true);
        Capture->TextureTarget = OutTarget;
        Capture->bCaptureEveryFrame = false;
        Capture->FOVAngle = 90.0f;
        Capture->CaptureSource = SCS_FinalColorLDR;
        return Capture;
    }

    struct FProfileProbe
    {
        FRenderQueryRHIRef Begin;
        FRenderQueryRHIRef End;
        double RenderStart = 0.0;
        double RenderEnd = 0.0;
    };

    /** Everything the steps share: what RunTest's locals were before the
     *  test went latent. */
    struct FLandingFrameRun
    {
        FAutomationTestBase* Automation = nullptr;
        TUniquePtr<FSkyWorld> Test;
        USceneCaptureComponent2D* Capture = nullptr;
        UTextureRenderTarget2D* Target = nullptr;
        USceneCaptureComponent2D* ProofCapture = nullptr;
        UTextureRenderTarget2D* ProofTarget = nullptr;
        USceneCaptureComponent2D* ProfileCapture = nullptr;
        UTextureRenderTarget2D* ProfileTarget = nullptr;
        IConsoleVariable* Shadows = nullptr;
        float ShadowsWere = 1.0f;
        FSkySystem Here;
        int32 FourthIndex = 4;
        FGroundFieldRef Field;
        FVector Sunward = FVector::ZeroVector;
        FUniversePosition Opening;
        FVector StartUp = FVector::ZeroVector;
        FVector StartHeading = FVector::ZeroVector;
        FString Tag;
        bool bProfile = false;
        APawn* Pilot = nullptr;
        FString Report;
        TArray<TFunction<void()>> Steps;

        // The case being run.
        const FLandingCase* Case = nullptr;
        bool bPlaced = false;
        FVector Up = FVector::ZeroVector;
        TArray<double> Times[2];
        double AglBefore = 0.0;
        /** A moving case: where the timed rounds began, and the speed the
         *  ship must at least half keep over them, cm/s. */
        TOptional<FUniversePosition> MovedFrom;
        double MustMoveCmPerSecond = 0.0;
        /** A gliding case: where it set off, which way, and how far on. */
        FVector GlideUp = FVector::ZeroVector;
        FVector GlideHeading = FVector::ZeroVector;
        double GlideRadiansPerSecond = 0.0;
        double GlideSeconds = 0.0;
        double StepSeconds = 0.0;
        int32 StepFrames = 0;
        /** Every Step since MovedFrom: the simulated time the ship flew. */
        int32 SimSteps = 0;
        FString TimedRegion;

        const FSkyBody& Fourth() const { return Here.Bodies[FourthIndex]; }

        /** Over the ground itself, not the datum: the 1.5 m cases printed
         *  -1701.89 when the datum was the reference. */
        double Agl() const
        {
            const FVector3d Offset = Test->Ship->GetFlightState().GetUniversePosition() - Fourth().Position;
            return Offset.Length() - Fourth().Radius - Field->Height(Offset.GetSafeNormal(), 0.0);
        }

        /** One frame of the game: a gliding case's ship first carried along
         *  its great circle, AglCm over the ground itself, heading level. */
        void StepShip()
        {
            constexpr float Dt = 1.0f / 60.0f;
            if (Case && GlideRadiansPerSecond > 0.0)
            {
                GlideSeconds += Dt;
                const FVector Axis = FVector::CrossProduct(GlideUp, GlideHeading).GetSafeNormal();
                const FQuat Turn(Axis, GlideRadiansPerSecond * GlideSeconds);
                const FVector Along = Turn.RotateVector(GlideUp);
                const FVector Ahead = Turn.RotateVector(GlideHeading);
                Test->Ship->PlaceShip(Fourth().Position + Along * (Fourth().Radius + Field->Height(FVector3d(Along), 0.0) + Case->AglCm),
                                      FRotationMatrix::MakeFromXZ(Ahead, Along).ToQuat());
            }
            Test->Step(Dt);
        }

        void Settle(USceneCaptureComponent2D* With)
        {
            if (GShaderCompilingManager)
            {
                GShaderCompilingManager->FinishAllCompilation();
            }
            StepShip();
            Test->World->SendAllEndOfFrameUpdates();
            With->CaptureScene();
        }

        /** One timed round: 20 frames at one strength, after three untimed. */
        double TimeFrame(float Strength)
        {
            if (Shadows)
            {
                Shadows->Set(Strength, ECVF_SetByCode);
            }
            TArray<FColor> Pixel;
            // The sky writes this frame's Shadows into every material.
            StepShip();
            ++SimSteps;
            for (int32 Warm = 0; Warm < 3; ++Warm)
            {
                // A fresh editor draws a material as the engine's default until
                // its shaders compile: timing that would time the wrong frame.
                if (GShaderCompilingManager)
                {
                    GShaderCompilingManager->FinishAllCompilation();
                }
                Test->World->SendAllEndOfFrameUpdates();
                Capture->CaptureScene();
                FlushRenderingCommands();
            }
            // The first read-back waits on whatever the engine's own frame left
            // the GPU: a pixel read before the clock starts.
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
            const double Start = FPlatformTime::Seconds();
            for (int32 Shot = 0; Shot < 20; ++Shot)
            {
                const double StepStart = FPlatformTime::Seconds();
                StepShip();
                ++SimSteps;
                StepSeconds += FPlatformTime::Seconds() - StepStart;
                Test->World->SendAllEndOfFrameUpdates();
                Capture->CaptureScene();
                FlushRenderingCommands();
                // One pixel read back: the GPU has finished the frame before the clock reads.
                Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
            }
            StepFrames += 20;
            return (FPlatformTime::Seconds() - Start) * 1000.0 / 20.0;
        }

        void Place(const FLandingCase& InCase)
        {
            Case = &InCase;
            bPlaced = false;
            GlideRadiansPerSecond = 0.0;
            Times[0].Reset();
            Times[1].Reset();
            StepSeconds = 0.0;
            StepFrames = 0;
            UShipSubsystem* Ship = Test->Ship;
            // A case after a moving one starts from rest: PlaceShip moves the
            // ship and keeps its velocity and the drive's eased lever.
            for (int32 Frame = 0; Frame < 1200 && Ship->GetShipSpeed() > 1.0f; ++Frame)
            {
                Ship->AllStop(Pilot);
                Test->Step(1.0f / 60.0f);
            }
            Up = StartUp;
            FVector Heading = StartHeading;
            if (Case->AglCm > 1.0e7)
            {
                // From orbit, as the ruled frames: the goto's own placement, the
                // nose at the world's centre, the frame all ground.
                const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, FourthIndex, Case->AglCm, Opening, ShipSky::EGotoSide::Dusk,
                                                                             FMath::DegreesToRadians(Case->DuskDegrees));
                if (!Automation->TestTrue(FString::Printf(TEXT("goto dusk places over Baemsekai IV (%s)"), Case->Slug), Dusk.IsSet()))
                {
                    return;
                }
                Up = (Dusk->Position - Fourth().Position).GetSafeNormal();
                Ship->PlaceShip(Dusk->Position, Dusk->Orientation);
            }
            else if (Case->DuskDegrees > 0.0)
            {
                const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, FourthIndex, 0.0, Opening, ShipSky::EGotoSide::Dusk,
                                                                             FMath::DegreesToRadians(Case->DuskDegrees));
                if (!Automation->TestTrue(FString::Printf(TEXT("goto dusk places over Baemsekai IV (%s)"), Case->Slug), Dusk.IsSet()))
                {
                    return;
                }
                // As Eyes.ReliefLook: along the terminator, the star on the side.
                Up = (Dusk->Position - Fourth().Position).GetSafeNormal();
                Heading = FVector::CrossProduct(Up, Sunward).GetSafeNormal();
            }
            if (Case->AglCm <= 1.0e7)
            {
                // The drive's approach: the nose 10 degrees under the level.
                const FVector Nose = Case->Motion == EMotion::Drive
                    ? (Heading * FMath::Cos(FMath::DegreesToRadians(10.0)) - Up * FMath::Sin(FMath::DegreesToRadians(10.0))).GetSafeNormal()
                    : Heading;
                Ship->PlaceShip(Fourth().Position + Up * (Fourth().Radius + Field->Height(FVector3d(Up), 0.0) + Case->AglCm),
                                FRotationMatrix::MakeFromXZ(Nose, Up).ToQuat());
            }
            Test->Step(1.0f / 60.0f);
            Test->Ground->FlushBuildsForTest();
            Test->Step(1.0f / 60.0f);
            // Looking out and a little down through the cockpit glass, as a pilot
            // would; from orbit straight down the nose.
            Capture->SetWorldLocationAndRotation(PilotEye, Case->AglCm > 1.0e7 ? FRotator::ZeroRotator : FRotator(-15.0, 0.0, 0.0));
            // Settle in wall-clock time before the first timing, as Eyes.ReliefLook
            // does: a pipeline still compiling after FinishAllCompilation drew the
            // ground in the wrong colour there, and would time the wrong frame here.
            for (int32 Round = 0; Round < 30; ++Round)
            {
                Settle(Capture);
                FlushRenderingCommands();
                FPlatformProcess::Sleep(0.05f);
            }
            AglBefore = Agl();
            MovedFrom.Reset();
            MustMoveCmPerSecond = 0.0;
            if (Case->Motion != EMotion::Still)
            {
                // Under way before the clock: HOVER and cruise full ahead, or
                // the drive's first notch engaged; then a second of flight,
                // paced to the wall clock so the workers build as in play.
                Ship->SetPilot(Pilot);
                Ship->SetVerticalLever(Pilot, 0.0);
                if (Case->Motion == EMotion::Glide)
                {
                    const double Cap = ShipFlight::SkimCap(Case->AglCm, ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor);
                    GlideUp = Up;
                    GlideHeading = Heading;
                    GlideRadiansPerSecond = Cap / Fourth().Radius;
                    GlideSeconds = 0.0;
                    MustMoveCmPerSecond = MovingShare * Cap;
                }
                else if (Case->Motion == EMotion::Skim)
                {
                    Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
                    MustMoveCmPerSecond = MovingShare * ShipFlight::SkimCap(AglBefore, ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor);
                }
                else
                {
                    Ship->SetDriveLever(Pilot, 1);
                    Ship->SetDriveEngaged(Pilot, true);
                    // At least 1 km/s: fifty times the fastest skim at 1.5 m.
                    MustMoveCmPerSecond = MovingShare * 1.0e5;
                }
                for (int32 Frame = 0; Frame < 60; ++Frame)
                {
                    const double Began = FPlatformTime::Seconds();
                    Settle(Capture);
                    FlushRenderingCommands();
                    FPlatformProcess::Sleep(FMath::Max(0.0f, static_cast<float>(1.0 / 60.0 - (FPlatformTime::Seconds() - Began))));
                }
                MovedFrom = Test->Ship->GetFlightState().GetUniversePosition();
                StepFrames = 0;
                StepSeconds = 0.0;
                SimSteps = 0;
            }
            bPlaced = true;
        }

        /** Levers back to rest, so the next case is placed from a ship that
         *  is not under way. */
        void Stop()
        {
            GlideRadiansPerSecond = 0.0;
            if (Case && Case->Motion != EMotion::Still)
            {
                Test->Ship->AllStop(Pilot);
                Test->Ship->SetDriveEngaged(Pilot, false);
                Test->Ship->SetFlightCommand(Pilot, 0.0f, FVector::ZeroVector);
            }
        }

        void Round(float Strength, bool bTimed)
        {
            if (!bPlaced)
            {
                return;
            }
            if (bTimed && Times[0].IsEmpty() && Times[1].IsEmpty())
            {
                TimedRegion = FString::Printf(TEXT("LF %s timed"), Case->Slug);
                TRACE_BEGIN_REGION(*TimedRegion);
            }
            const double Ms = TimeFrame(Strength);
            if (bTimed)
            {
                Times[Strength > 0.5f ? 1 : 0].Add(Ms);
            }
        }

        void Finish()
        {
            if (!bPlaced)
            {
                return;
            }
            TRACE_END_REGION(*TimedRegion);
            const double AglAfter = Agl();
            // A moving case: how fast the ship flew over the timed rounds.
            const double SimSeconds = SimSteps / 60.0;
            const double SpeedCmPerSecond = MovedFrom && SimSeconds > 0.0
                ? (Test->Ship->GetFlightState().GetUniversePosition() - *MovedFrom).Length() / SimSeconds : 0.0;
            // The switch's proof, at the read exposure: without the term twice,
            // back to back, then with it (the class comment says why). Only
            // for a still ship: a moving one holds no pixel still.
            TArray<FColor> Frames[3];
            ProofCapture->SetWorldLocationAndRotation(Capture->GetComponentLocation(), Capture->GetComponentRotation());
            EyesFrames::Expose(ProofCapture, Case->ReadStops);
            for (int32 Pass = 0; Pass < 3 && Case->Motion == EMotion::Still; ++Pass)
            {
                if (Shadows)
                {
                    Shadows->Set(Pass == 2 ? 1.0f : 0.0f, ECVF_SetByCode);
                }
                for (int32 Frame = 0; Frame < 8; ++Frame)
                {
                    Settle(ProofCapture);
                }
                ProofTarget->GameThread_GetRenderTargetResource()->ReadPixels(Frames[Pass]);
            }
            const EyesFrames::FShade Shade = Case->Motion == EMotion::Still ? EyesFrames::Shade(Frames[0], Frames[1], Frames[2]) : EyesFrames::FShade();
            const double On = MedianOf(Times[1]);
            const double Off = MedianOf(Times[0]);
            const double Spread = FMath::Max(IqrOf(Times[0]), IqrOf(Times[1]));
            const double SunDegrees = FMath::RadiansToDegrees(FMath::Asin(FVector::DotProduct(Up, Sunward)));
            const FString Line = FString::Printf(
                TEXT("case %s sun %.2f on_ms %.3f off_ms %.3f spread_ms %.3f switch %s switch_cov %.4f switch_lit %.4f flicker %.4f read_stops %.0f read_p95 %d frame_crc_on %08x frame_crc_off %08x tiles %d step_ms %.3f speed_mps %.1f agl_m %.2f..%.2f times_on %s times_off %s\n%s\n"),
                Case->Slug, SunDegrees, On, Off, Spread, Shadows ? TEXT("present") : TEXT("absent"),
                Shade.Coverage, Shade.LitShare, Shade.Flicker, Case->ReadStops, Frames[0].Num() > 0 ? EyesFrames::LumaPercentile(Frames[0], 0.95) : 0,
                FCrc::MemCrc32(Frames[2].GetData(), Frames[2].Num() * sizeof(FColor)),
                FCrc::MemCrc32(Frames[0].GetData(), Frames[0].Num() * sizeof(FColor)),
                Test->Ground->GetDrawnKeys().Num(), StepFrames > 0 ? StepSeconds * 1000.0 / StepFrames : 0.0,
                SpeedCmPerSecond / 100.0, AglBefore / 100.0, AglAfter / 100.0,
                *Joined(Times[1]), *Joined(Times[0]), *Test->Ground->Describe());
            Report += Line;
            Automation->AddInfo(Line);
            if (Case->Motion != EMotion::Still)
            {
                Automation->TestTrue(FString::Printf(TEXT("%s timed a ship under way: %.1f m/s over the timed rounds, at least %.1f"),
                    Case->Slug, SpeedCmPerSecond / 100.0, MustMoveCmPerSecond / 100.0), SpeedCmPerSecond >= MustMoveCmPerSecond);
                Stop();
            }
            // Every case, still or moving, within the frame (slice (b)'s last
            // open items, 2026-09-29): the median of its timed rounds.
            Automation->TestTrue(FString::Printf(TEXT("%s is within the 16.6 ms frame at 4K (%.2f)"), Case->Slug, On), On <= FrameBudgetMs);
            if (Shadows && ProvesTheSwitch(*Case))
            {
                Automation->TestTrue(FString::Printf(TEXT("the switch's proof can see: at %s at least 1%% of the frame is lit and held still (%.2f%%)"),
                    Case->Slug, 100.0 * Shade.LitShare), Shade.LitShare >= 0.01);
                Automation->TestTrue(FString::Printf(TEXT("the switch reaches the materials: at %s the term shades at least 10%% of the lit, held ground (%.2f%%)"),
                    Case->Slug, 100.0 * Shade.Coverage), Shade.Coverage >= 0.10);
                // And of the frame: a switch that never reached the materials
                // leaves every frame shadowed, the lit share a few far ridge
                // tops that flicker, and the coverage above is then theirs --
                // 11-45% of 3.5% of the frame, where the switch takes 98% of
                // 57% (the proxy's strength pinned at 1 survived the coverage
                // alone, twice).
                Automation->TestTrue(FString::Printf(TEXT("the switch reaches the materials: at %s the term darkens at least 5%% of the whole frame (%.2f%%)"),
                    Case->Slug, 100.0 * Shade.TakenShare), Shade.TakenShare >= 0.05);
            }
            if (Shadows)
            {
                Shadows->Set(1.0f, ECVF_SetByCode);
            }
        }

        /** One profile variant: 16 frames, the first four untimed, each split
         *  into the game step, the capture's game-thread half, the render
         *  thread's span of the capture, the flush and the read-back's wait. */
        void Profile(const TCHAR* Name, USceneCaptureComponent2D* Cap, UTextureRenderTarget2D* Into, bool bStep)
        {
            if (!bPlaced)
            {
                return;
            }
            Cap->SetWorldLocationAndRotation(Capture->GetComponentLocation(), Capture->GetComponentRotation());
            TArray<double> Game, Enqueue, Render, Flush, Wait, Total;
            const FString Region = FString::Printf(TEXT("LF %s %s"), Case->Slug, Name);
            TRACE_BEGIN_REGION(*Region);
            for (int32 Frame = 0; Frame < 16; ++Frame)
            {
                if (GShaderCompilingManager)
                {
                    GShaderCompilingManager->FinishAllCompilation();
                }
                FProfileProbe Probe;
                FProfileProbe* P = &Probe;
                const double T0 = FPlatformTime::Seconds();
                if (bStep)
                {
                    Test->Step(1.0f / 60.0f);
                }
                Test->World->SendAllEndOfFrameUpdates();
                const double T1 = FPlatformTime::Seconds();
                ENQUEUE_RENDER_COMMAND(LandingProfileBegin)([P](FRHICommandListImmediate&) { P->RenderStart = FPlatformTime::Seconds(); });
                Cap->CaptureScene();
                const double T2 = FPlatformTime::Seconds();
                ENQUEUE_RENDER_COMMAND(LandingProfileEnd)([P](FRHICommandListImmediate&) { P->RenderEnd = FPlatformTime::Seconds(); });
                FlushRenderingCommands();
                const double T3 = FPlatformTime::Seconds();
                TArray<FColor> One;
                Into->GameThread_GetRenderTargetResource()->ReadPixels(One, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
                const double T4 = FPlatformTime::Seconds();
                if (Frame < 4)
                {
                    continue;   // warm: a first frame after a change is not the frame
                }
                Game.Add((T1 - T0) * 1000.0);
                Enqueue.Add((T2 - T1) * 1000.0);
                Render.Add((Probe.RenderEnd - Probe.RenderStart) * 1000.0);
                Flush.Add((T3 - T2) * 1000.0);
                Wait.Add((T4 - T3) * 1000.0);
                Total.Add((T4 - T0) * 1000.0);
            }
            TRACE_END_REGION(*Region);
            const FString Line = FString::Printf(
                TEXT("profile %s %s total_ms %.3f game_ms %.3f enqueue_ms %.3f render_ms %.3f flush_ms %.3f wait_ms %.3f\n"),
                Case->Slug, Name, MedianOf(Total), MedianOf(Game), MedianOf(Enqueue), MedianOf(Render), MedianOf(Flush), MedianOf(Wait));
            Report += Line;
            Automation->AddInfo(Line);
        }

        void ShowAll(bool bPlainGround)
        {
            if (UTerrainGroundComponent* Tiles = Test->Ground->GetTilesComponent())
            {
                Tiles->SetMaterial(0, bPlainGround ? static_cast<UMaterialInterface*>(UMaterial::GetDefaultMaterial(MD_Surface)) : Test->Ground->GetGroundMaterialInstance());
            }
        }

        void QueueCase(const FLandingCase& InCase)
        {
            Steps.Add([this, &InCase]() { Place(InCase); });
            // One untimed round first: the first frame timed after the settle
            // was 3-5 ms slow in every case, and ABBA always put it in the off
            // slot.
            Steps.Add([this]() { Round(0.0f, false); });
            Steps.Add([this]() { Round(1.0f, false); });
            // ABBA, five times over: 0 1 1 0, ... The first baseline, three
            // rounds, read a half-range of 2-3 ms in every case: too wide for a
            // 1 ms gate (the plan's remedy: five rounds).
            for (int32 Rounds = 0; Rounds < 5; ++Rounds)
            {
                for (const int32 On : { 0, 1, 1, 0 })
                {
                    Steps.Add([this, On]() { Round(static_cast<float>(On), true); });
                }
            }
            Steps.Add([this]() { Finish(); });
            if (bProfile)
            {
                Steps.Add([this]() { Profile(TEXT("full"), Capture, Target, true); });
                Steps.Add([this]() { Capture->HiddenActors = { Test->Ground }; Profile(TEXT("no_ground"), Capture, Target, false); });
                Steps.Add([this]() { Capture->HiddenActors = { Test->Sky, Test->Frame }; Profile(TEXT("no_sky"), Capture, Target, false); });
                Steps.Add([this]() { Capture->HiddenActors = { Test->Ground, Test->Sky, Test->Frame }; Profile(TEXT("empty"), Capture, Target, false); });
                Steps.Add([this]() { Capture->HiddenActors.Reset(); ShowAll(true); });
                Steps.Add([this]() { Profile(TEXT("ground_plain"), Capture, Target, false); });
                Steps.Add([this]() { ShowAll(false); });
                Steps.Add([this]() { Profile(TEXT("full_again"), Capture, Target, false); });
                Steps.Add([this]() { Profile(TEXT("1080p"), ProfileCapture, ProfileTarget, false); });
            }
        }

        void Close()
        {
            if (Shadows)
            {
                Shadows->Set(ShadowsWere, ECVF_SetByCode);
            }
            const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/LandingFrame") / Tag;
            IFileManager::Get().MakeDirectory(*Dir, true);
            FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("report.txt")));
            // Play ends and the world goes before the test does.
            Test.Reset();
        }
    };

    /** Runs the steps one at a time, FramesBetweenSteps engine frames apart. */
    class FLandingFrameSteps : public IAutomationLatentCommand
    {
    public:
        explicit FLandingFrameSteps(TSharedRef<FLandingFrameRun> InRun) : Run(InRun) {}

        virtual bool Update() override
        {
            if (Wait > 0)
            {
                --Wait;
                return false;
            }
            if (Next < Run->Steps.Num())
            {
                Run->Steps[Next++]();
                Wait = FramesBetweenSteps;
                return false;
            }
            Run->Close();
            return true;
        }

    private:
        TSharedRef<FLandingFrameRun> Run;
        int32 Next = 0;
        int32 Wait = 0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingFrameEyesTest, "Eyes.LandingFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLandingFrameEyesTest::RunTest(const FString& Parameters)
{
    TSharedRef<FLandingFrameRun> Run = MakeShared<FLandingFrameRun>();
    Run->Automation = this;
    // With the cast shadows in, as the game ships them (the frame ruling:
    // "profile first, with the cast shadows in"): the tiles' vertices carry
    // theirs, and every world's map is baked before any case is timed, so no
    // bake shares the machine with a timed frame.
    Run->Test = MakeUnique<FSkyWorld>(TEXT("LandingFrameWorld"), 3000, EShadows::On);
    FSkyWorld& Test = *Run->Test;
    Test.BeginPlay();
    Test.Step(1.0f / 60.0f);
    Test.Sky->FlushShadowBakesForTest();
    Run->Capture = NewCapture(Test.Sky, 3840, 2160, Run->Target);
    // The switch's proof is read at 1080p, on its own capture, so the timed
    // one is never given a view state.
    Run->ProofCapture = NewCapture(Test.Sky, 1920, 1080, Run->ProofTarget);
    Run->ProfileCapture = NewCapture(Test.Sky, 1920, 1080, Run->ProfileTarget);

    Run->Pilot = Test.World->SpawnActor<APawn>();
    Run->Here = LocalSystem::Here(Test.World);
    if (!TestTrue(TEXT("the start system has a fourth body"), Run->Here.Bodies.Num() > 4))
    {
        Run->Test.Reset();
        return false;
    }
    Run->Field = ShipGround::FromRelief(Run->Fourth().Relief);
    const FSkyBody* Star = Run->Here.Bodies.FindByPredicate([](const FSkyBody& Candidate) { return Candidate.Kind == ESkyBodyKind::Star; });
    if (!TestNotNull(TEXT("the start system has a star"), Star))
    {
        Run->Test.Reset();
        return false;
    }
    Run->Sunward = (Star->Position - Run->Fourth().Position).GetSafeNormal();
    // Every case is placed from the opening, whatever the one before did.
    Run->Opening = Test.Ship->GetFlightState().GetUniversePosition();
    Run->StartUp = (Run->Opening - Run->Fourth().Position).GetSafeNormal();
    Run->StartHeading = FVector::CrossProduct(Run->StartUp, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
    const FString TagEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_TAG"));
    Run->Tag = TagEnv.IsEmpty() ? FString(TEXT("untagged")) : TagEnv;
    Run->bProfile = !FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_PROFILE")).IsEmpty();
    Run->Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    Run->ShadowsWere = Run->Shadows ? Run->Shadows->GetFloat() : 1.0f;
    if (!Run->Shadows)
    {
        AddWarning(TEXT("ds.Sky.Shadows does not exist: on and off time one frame, and the switch is not proven (switch absent)."));
    }

    // EYES_CASES=1.5m_dusk10,... runs only those cases: for looking into one,
    // never for the gate, which reads all six. A name that is no case is an
    // error, not a filter that quietly matches nothing; and a run without the
    // switch's proof case warns, since it then asserts nothing about the
    // frames at all.
    TArray<FString> Only;
    FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_CASES")).ParseIntoArray(Only, TEXT(","));
    bool bNamesKnown = true;
    for (const FString& Name : Only)
    {
        const bool bKnown = Algo::AnyOf(LandingCases, [&Name](const FLandingCase& Case) { return Name == Case.Slug; });
        bNamesKnown &= TestTrue(FString::Printf(TEXT("EYES_CASES names a case (%s)"), *Name), bKnown);
    }
    if (!bNamesKnown)
    {
        Run->Test.Reset();
        return false;
    }
    bool bProofQueued = false;
    for (const FLandingCase& Case : LandingCases)
    {
        if (Only.IsEmpty() || Only.Contains(Case.Slug))
        {
            Run->QueueCase(Case);
            bProofQueued |= ProvesTheSwitch(Case);
        }
    }
    if (!bProofQueued)
    {
        AddWarning(TEXT("EYES_CASES leaves out 1.5m_dusk3, the switch's proof: this run asserts the frames' time and nothing about the switch."));
    }
    ADD_LATENT_AUTOMATION_COMMAND(FLandingFrameSteps(Run));
    return true;
}

#endif
