#include "Components/SceneCaptureComponent2D.h"
#include "DynamicRHI.h"
#include "Engine/TextureRenderTarget2D.h"
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
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainTileComponent.h"
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
 * The frame is REPORTED, not asserted: the frame ruling ("profile first,
 * with the cast shadows in, and fix the real cost") owns the budget, and the
 * landing plan's Task 39 notes hold the profile.
 *
 * With the cast shadows in, as the game ships them: the tiles' vertices
 * carry theirs and every world's map is baked before any case is timed.
 * Six cases: the two above under the start's sun, and four at a dusk placed
 * as Eyes.ReliefLook places it -- 50 km, 1.5 m and 200 km at the dusk goto's
 * 10 degrees, and 1.5 m at 3. Each case prints its sun's elevation and its
 * height over the ground itself (agl_m, before and after timing). Each is
 * timed with ds.Sky.Shadows 0 and 1 in ABBA order, five times over after
 * one untimed round, and the medians are reported with their interquartile
 * spread and the raw times. Tools/landing_frame_gate.py reads two runs'
 * reports against the medians' own error.
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
 * The one assertion is that the switch reaches the materials, proven by
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
 */
namespace
{
    using namespace SkyTestWorld;

    /** Engine frames between two steps: the Vulkan RHI retires a freed
     *  resource two frames after the frame it was freed in. */
    constexpr int32 FramesBetweenSteps = 4;

    struct FLandingCase
    {
        const TCHAR* Slug;     // one word: Tools/landing_frame_gate.py splits on spaces
        double AglCm;
        double DuskDegrees;    // 0: the start's own sun
        double ReadStops;      // the switch proof's exposure, brighter than the game's exposure
    };

    const FLandingCase LandingCases[] = {
        { TEXT("50km"), 5.0e6, 0.0, 0.0 },
        { TEXT("1.5m"), 150.0, 0.0, 0.0 },
        { TEXT("50km_dusk10"), 5.0e6, 10.0, 3.0 },
        // Metered as Eyes.ReliefLook's IV ground_low, the same geometry.
        { TEXT("1.5m_dusk3"), 150.0, 3.0, 5.0 },
        { TEXT("200km_dusk10"), 2.0e7, 10.0, 3.0 },
        { TEXT("1.5m_dusk10"), 150.0, 10.0, 4.0 },
    };

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
        FString Report;
        TArray<TFunction<void()>> Steps;

        // The case being run.
        const FLandingCase* Case = nullptr;
        bool bPlaced = false;
        FVector Up = FVector::ZeroVector;
        TArray<double> Times[2];
        double AglBefore = 0.0;
        double StepSeconds = 0.0;
        int32 StepFrames = 0;
        FString TimedRegion;

        const FSkyBody& Fourth() const { return Here.Bodies[FourthIndex]; }

        /** Over the ground itself, not the datum: the 1.5 m cases printed
         *  -1701.89 when the datum was the reference. */
        double Agl() const
        {
            const FVector3d Offset = Test->Ship->GetFlightState().GetUniversePosition() - Fourth().Position;
            return Offset.Length() - Fourth().Radius - Field->Height(Offset.GetSafeNormal(), 0.0);
        }

        void Settle(USceneCaptureComponent2D* With)
        {
            if (GShaderCompilingManager)
            {
                GShaderCompilingManager->FinishAllCompilation();
            }
            Test->Step(1.0f / 60.0f);
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
            Test->Step(1.0f / 60.0f);
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
                Test->Step(1.0f / 60.0f);
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
            Times[0].Reset();
            Times[1].Reset();
            StepSeconds = 0.0;
            StepFrames = 0;
            UShipSubsystem* Ship = Test->Ship;
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
                Ship->PlaceShip(Fourth().Position + Up * (Fourth().Radius + Field->Height(FVector3d(Up), 0.0) + Case->AglCm),
                                FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
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
            bPlaced = true;
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
            // The switch's proof, at the read exposure: without the term twice,
            // back to back, then with it (the class comment says why).
            TArray<FColor> Frames[3];
            ProofCapture->SetWorldLocationAndRotation(Capture->GetComponentLocation(), Capture->GetComponentRotation());
            EyesFrames::Expose(ProofCapture, Case->ReadStops);
            for (int32 Pass = 0; Pass < 3; ++Pass)
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
            const EyesFrames::FShade Shade = EyesFrames::Shade(Frames[0], Frames[1], Frames[2]);
            const double On = MedianOf(Times[1]);
            const double Off = MedianOf(Times[0]);
            const double Spread = FMath::Max(IqrOf(Times[0]), IqrOf(Times[1]));
            const double SunDegrees = FMath::RadiansToDegrees(FMath::Asin(FVector::DotProduct(Up, Sunward)));
            const FString Line = FString::Printf(
                TEXT("case %s sun %.2f on_ms %.3f off_ms %.3f spread_ms %.3f switch %s switch_cov %.4f switch_lit %.4f flicker %.4f read_stops %.0f read_p95 %d frame_crc_on %08x frame_crc_off %08x tiles %d step_ms %.3f agl_m %.2f..%.2f times_on %s times_off %s\n%s\n"),
                Case->Slug, SunDegrees, On, Off, Spread, Shadows ? TEXT("present") : TEXT("absent"),
                Shade.Coverage, Shade.LitShare, Shade.Flicker, Case->ReadStops, EyesFrames::LumaPercentile(Frames[0], 0.95),
                FCrc::MemCrc32(Frames[2].GetData(), Frames[2].Num() * sizeof(FColor)),
                FCrc::MemCrc32(Frames[0].GetData(), Frames[0].Num() * sizeof(FColor)),
                Test->Ground->GetDrawnKeys().Num(), StepFrames > 0 ? StepSeconds * 1000.0 / StepFrames : 0.0,
                AglBefore / 100.0, AglAfter / 100.0,
                *Joined(Times[1]), *Joined(Times[0]), *Test->Ground->Describe());
            Report += Line;
            Automation->AddInfo(Line);
            if (On > 16.6)
            {
                Automation->AddInfo(FString::Printf(TEXT("%s is over the 16.6 ms frame (%.2f): reported for the frame ruling's profiling, not asserted here"), Case->Slug, On));
            }
            if (Shadows && Case->DuskDegrees > 0.0 && Case->DuskDegrees < 5.0 && Case->AglCm < 1.0e5)
            {
                Automation->TestTrue(FString::Printf(TEXT("the switch's proof can see: at %s at least 1%% of the frame is lit and held still (%.2f%%)"),
                    Case->Slug, 100.0 * Shade.LitShare), Shade.LitShare >= 0.01);
                Automation->TestTrue(FString::Printf(TEXT("the switch reaches the materials: at %s the term shades at least 10%% of the lit, held ground (%.2f%%)"),
                    Case->Slug, 100.0 * Shade.Coverage), Shade.Coverage >= 0.10);
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
            TArray<UTerrainTileComponent*> Tiles;
            Test->Ground->GetComponents<UTerrainTileComponent>(Tiles);
            for (UTerrainTileComponent* Tile : Tiles)
            {
                Tile->SetMaterial(0, bPlainGround ? static_cast<UMaterialInterface*>(UMaterial::GetDefaultMaterial(MD_Surface)) : Test->Ground->GetGroundMaterialInstance());
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
    // never for the gate, which reads all six.
    TArray<FString> Only;
    FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_CASES")).ParseIntoArray(Only, TEXT(","));
    for (const FLandingCase& Case : LandingCases)
    {
        if (Only.IsEmpty() || Only.Contains(Case.Slug))
        {
            Run->QueueCase(Case);
        }
    }
    ADD_LATENT_AUTOMATION_COMMAND(FLandingFrameSteps(Run));
    return true;
}

#endif
