#include "Components/SceneCaptureComponent2D.h"
#include "Components/WidgetComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Ship/NavStart.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipSubsystem.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"
#include "UI/SystemMapLayout.h"
#include "UI/SystemMapWidget.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * TEMPORARY: the render check of system map decision 11, for eyes, not a
 * guard. Named outside DeepSpace. so ./test.sh never runs it; run once
 * through the lock and never with -nullrhi:
 *
 *   . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
 *       "$PWD/DeepSpace.uproject" -ExecCmds="Automation RunTests Eyes.MapFromHelm" \
 *       -TestExit="Automation Test Queue Empty" -unattended -nopause -nosplash -NoLiveCoding \
 *       -RenderOffScreen -FORCELOGFLUSH
 *
 * It frames the map from the helm's eye as the helm sees it -- 30 degrees
 * across at 800 px, near the 26.7 px a degree a 103-degree view has at its
 * middle on 4K -- and writes Saved/Eyes/MapFromHelm/<shot>.png and
 * report.txt. For stage 1 the frames are judged on the rows, the rings and
 * the title; stage 3 runs it again with a target (home_target_ahead, with a
 * live ETA and "Near enough to fly"; home_target_far, with "Jump here";
 * home_footer_longest, the longest footer beside the button), to
 * judge the target ring, the band's lines and the button, and this file is
 * deleted with that verdict.
 *
 * NOT A VERDICT. This file writes the frames; the orchestrator, or the
 * developer at first playtest, judges them (decision 11). What the
 * implementer saw in the first runs is recorded as findings, not as the
 * gate: the rows, the title and the footer read at the helm's pixels. Two
 * faults were fixed -- a row button centred its columns, so no two rows
 * lined up, and the spec's 34 px numeral column cut "VIII" to "V" (now
 * 40 px). A reviewer reading the same frames found what is still open, and
 * these are the knobs, in SystemMapView.cpp and SystemMapLayout.h:
 *   - dot legibility: barren dots are small grey points, 3-4 px at the
 *     helm's scale (RockDotPx, MinWorldLuminance), and in a crowded system
 *     MinRingGap -- now 8 px, one gap kept outside the outermost ring --
 *     caps them at 6;
 *   - numeral crowding: in the twelve-world frame the 11 pt numerals sit on
 *     the neighbouring rings (NumeralSize).
 * A still frame cannot show shimmer as the head moves: that stays a
 * playtest note.
 *
 * The map refreshes itself here: nothing below calls RefreshFromShip. What
 * each frame shows is what the widget component's own paint, and Slate's
 * tick of the widget inside it, produced -- so a map that had stopped
 * refreshing itself would show the last system in the next shot, and the
 * title check after the move fails. (BuildScreen draws home itself, so the
 * opening shot proves nothing about the tick.)
 *
 * A fresh editor draws the widget's material as a checkerboard until its
 * shaders compile, hence FinishAllCompilation below.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMapFromHelmEyesTest,
    "Eyes.MapFromHelm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MapFromHelmEyesLocal
{
    /** The map's glass, as SystemMapScreenTest places it. */
    const FVector MapGlass(1711.0, 0.0, 105.0);

    constexpr float CaptureFovDeg = 30.0f;
    constexpr int32 CaptureWidth = 800;
    constexpr int32 CaptureHeight = 600;
}

bool FMapFromHelmEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace MapFromHelmEyesLocal;

    if (GUsingNullRHI)
    {
        AddError(TEXT("Eyes.MapFromHelm renders: run it without -nullrhi"));
        return false;
    }

    FSkyWorld Test(TEXT("MapFromHelmEyesWorld"));
    AShipMapScreen* Screen = Test.World->SpawnActor<AShipMapScreen>(MapGlass, FRotator::ZeroRotator);
    AActor* Camera = Test.World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("the map spawns"), Screen) || !TestNotNull(TEXT("and a camera"), Camera))
    {
        return false;
    }
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Camera, TEXT("HelmEye"));
    Camera->SetRootComponent(Capture);
    Capture->RegisterComponent();
    Capture->SetWorldLocationAndRotation(PilotEye, (MapGlass - PilotEye).Rotation());
    Capture->FOVAngle = CaptureFovDeg;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Camera);
    Target->RenderTargetFormat = RTF_RGBA8;
    Target->InitAutoFormat(CaptureWidth, CaptureHeight);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;

    UWidgetComponent* Panel = Screen->GetScreen();
    Panel->SetTickWhenOffscreen(true);
    Test.BeginPlay();

    USystemMapWidget* Map = Cast<USystemMapWidget>(Panel->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the map's widget"), Map))
    {
        return false;
    }

    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes") / TEXT("MapFromHelm");
    IFileManager::Get().MakeDirectory(*Dir, true);
    TArray<FString> Report;
    // Pixels a degree at the middle of a perspective view, which is what
    // the spec's 26.7 for the helm's 103 degrees on 3840 px is.
    const auto PerDegree = [](double Pixels, double FovDeg)
    {
        return 0.5 * Pixels / FMath::Tan(FMath::DegreesToRadians(0.5 * FovDeg)) * FMath::DegreesToRadians(1.0);
    };
    const double Seen = FMath::RadiansToDegrees(2.0 * FMath::Atan(34.0 * FMath::Cos(FMath::DegreesToRadians(29.0)) / (MapGlass - PilotEye).Size()));
    Report.Add(FString::Printf(TEXT("Map from the helm: %.1f px a degree (the helm's on 4K is %.1f); eye %s, glass %s, %.0f cm apart; the panel spans about %.1f deg."),
        PerDegree(CaptureWidth, CaptureFovDeg), PerDegree(3840.0, 103.0), *PilotEye.ToString(), *MapGlass.ToString(), (MapGlass - PilotEye).Size(), Seen));

    const auto Shoot = [&](const TCHAR* Shot)
    {
        for (int32 Frame = 0; Frame < 8; ++Frame)
        {
            // A material whose shaders are still compiling draws as the
            // engine's checkerboard; a fresh editor compiles the widget's
            // pass-through material on first use.
            if (GShaderCompilingManager)
            {
                GShaderCompilingManager->FinishAllCompilation();
            }
            Test.Step(1.0f / 60.0f);
            Panel->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
            Test.World->SendAllEndOfFrameUpdates();
            // Enqueued behind the widget's own draw, in order; reading the
            // target back below waits for both.
            Capture->CaptureScene();
        }
        const FString Path = Dir / FString(Shot) + TEXT(".png");
        TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*Path));
        const bool bWritten = File && FImageUtils::ExportRenderTarget2DAsPNG(Target, *File);
        TestTrue(FString::Printf(TEXT("%s is written"), Shot), bWritten);

        Report.Add(FString());
        Report.Add(FString::Printf(TEXT("[%s] %s"), Shot, *Path));
        Report.Add(FString::Printf(TEXT("  title:  SYSTEM  %s"), *Map->GetTitleText().ToString()));
        for (int32 Row = 0; Row < Map->GetShownRowCount(); ++Row)
        {
            Report.Add(FString::Printf(TEXT("  row %2d: %s"), Row, *Map->GetRowText(Row).ToString()));
        }
        Report.Add(FString::Printf(TEXT("  target: %s"), *Map->GetTargetText().ToString()));
        Report.Add(FString::Printf(TEXT("  footer: %s"), *Map->GetFooterText().ToString()));
        Report.Add(FString::Printf(TEXT("  button: %s%s"), Map->IsJumpButtonShown() ? *Map->GetJumpButtonText().ToString() : TEXT("(none)"),
                                   Map->IsJumpButtonShown() && !Map->IsJumpButtonEnabled() ? TEXT(" (disabled)") : TEXT("")));
        if (const SystemMap::FMapLayout* Drawn = Map->GetLayout())
        {
            FString Rings;
            for (const double Ring : Drawn->RingPx)
            {
                Rings += FString::Printf(TEXT(" %.1f"), Ring);
            }
            Report.Add(FString::Printf(TEXT("  rings (px):%s"), *Rings));
        }
    };

    // The opening: home, as the player first sees it.
    Shoot(TEXT("home_opening"));

    // Stage 3: with a target. The world the opening shot faces, the drive
    // taking the ship toward it, so the band carries a live ETA -- and it is
    // near enough to fly, so the button says so; then the outermost other
    // world, which can be jumped to.
    if (const TOptional<FStarSystem> Opening = Test.Universe->GetSystemAt(Test.Ship->GetFlightState().GetUniversePosition()))
    {
        const FVector Nose = Test.Ship->GetFlightState().GetUniverseOrientation().GetForwardVector();
        int32 Ahead = INDEX_NONE;
        double Best = -1.0;
        for (int32 Orbit = 0; Orbit < Opening->Planets.Num(); ++Orbit)
        {
            const double Along = FVector::DotProduct((Opening->PlanetPosition(Orbit) - Test.Ship->GetFlightState().GetUniversePosition()).GetSafeNormal(), Nose);
            if (Along > Best)
            {
                Best = Along;
                Ahead = Orbit;
            }
        }
        APawn* Pilot = Test.World->SpawnActor<APawn>();
        if (Ahead != INDEX_NONE && Pilot)
        {
            Test.Ship->SetTarget(FBodyId{ Opening->Stub.Id, Ahead, -1 });
            Test.Ship->SetPilot(Pilot);
            Test.Ship->SetDriveEngaged(Pilot, true);
            Test.Ship->SetDriveLever(Pilot, 6);
            for (int32 Tick = 0; Tick < 300; ++Tick)
            {
                Test.Ship->Tick(1.0f / 30.0f);
            }
            Shoot(TEXT("home_target_ahead"));
            Test.Ship->AllStop(Pilot);
            Test.Ship->ClearPilot();

            for (int32 Orbit = Opening->Planets.Num() - 1; Orbit >= 0; --Orbit)
            {
                if (Orbit != Ahead && !Test.Ship->IsNearEnoughToFly(*Opening, FBodyId{ Opening->Stub.Id, Orbit, -1 }))
                {
                    Test.Ship->SetTarget(FBodyId{ Opening->Stub.Id, Orbit, -1 });
                    Shoot(TEXT("home_target_far"));

                    // The longest footer the map writes, held inside the
                    // innermost orbit and off the plane, on the row the
                    // button shares: it must wrap short of the button.
                    const SystemMap::FMapScale Scale = SystemMap::Fit(*Opening, UShipSubsystem::GetStandoffAU());
                    const double Inner = 0.5 * Scale.InnerAU * UniverseUnits::CmPerAU;
                    const FQuat Facing = Test.Ship->GetFlightState().GetUniverseOrientation();
                    Test.Ship->PlaceShip(Opening->Stub.Position + Inner * FVector(FMath::Cos(FMath::DegreesToRadians(34.0)), 0.0,
                                                                                  FMath::Sin(FMath::DegreesToRadians(34.0))), Facing);
                    Shoot(TEXT("home_footer_longest"));
                    break;
                }
            }
            Test.Ship->ClearTarget();
        }
    }

    // The most crowded system among the first 2,000 stubs nearest home, met
    // at its arrival point from home: the tightest rings procgen makes.
    const TOptional<FStarSystem> Home = Test.Universe->GetSystemAt(Test.Ship->GetFlightState().GetUniversePosition());
    if (Home)
    {
        TArray<FStarSystemStub> Near = Test.Universe->GetSystemsNear(Home->Stub.Position, 60.0 * UniverseUnits::CmPerLightYear);
        Near.SetNum(FMath::Min(Near.Num(), 2000));
        TOptional<FStarSystem> Crowded;
        for (const FStarSystemStub& Stub : Near)
        {
            TOptional<FStarSystem> System = Test.Universe->GetSystem(Stub.Id);
            if (System && (!Crowded || System->Planets.Num() > Crowded->Planets.Num()))
            {
                Crowded = MoveTemp(System);
            }
        }
        if (Crowded)
        {
            Report.Add(FString());
            Report.Add(FString::Printf(TEXT("Most crowded of %d stubs: %s, %d worlds."), Near.Num(), *Crowded->Stub.Name, Crowded->Planets.Num()));
            const FUniversePosition Arrival = NavStart::ArrivalPoint(Home->Stub.Position, *Crowded);
            const FVector Toward = (Crowded->Stub.Position - Arrival).GetSafeNormal();
            Test.Ship->PlaceShip(Arrival, FRotationMatrix::MakeFromX(Toward).ToQuat());
            Shoot(TEXT("crowded_arrival"));
            TestEqual(TEXT("the map moved to the crowded system by its own tick"), Map->GetTitleText().ToString(),
                      NavText::Place(Crowded->Stub.Name, Crowded->Star.Class));
            TestEqual(TEXT("with a row per world"), Map->GetShownRowCount(), Crowded->Planets.Num());

            // Among its worlds, between the third and fourth orbits.
            if (Crowded->Planets.Num() >= 4)
            {
                const double Between = FMath::Sqrt(Crowded->Planets[2].SemiMajorAxisAU * Crowded->Planets[3].SemiMajorAxisAU);
                Test.Ship->PlaceShip(Crowded->Stub.Position + FVector(0.6, 0.8, 0.0) * Between * UniverseUnits::CmPerAU,
                                     FRotationMatrix::MakeFromX(-Toward).ToQuat());
                Shoot(TEXT("crowded_among_worlds"));
            }
        }
    }

    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(FString::Printf(TEXT("Eyes.MapFromHelm: frames in %s"), *Dir));
    return true;
}

#endif
