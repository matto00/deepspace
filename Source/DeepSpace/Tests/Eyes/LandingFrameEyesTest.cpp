#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Surface/WorldGround.h"
#include "TextureResource.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Slice (b)'s done-when, the part a test can measure: a 4K capture from the
 * helm's eye with the real sky and the real ground, at 50 km (the handover)
 * and 1.5 m over Baemsekai IV, render thread and GPU together, and the
 * tile counts at each. Not the game's frame -- `stat unit` in play is, which
 * the developer reads -- but the same scene, measured the same way every
 * time. Asserts the 16.6 ms budget.
 *
 *   Tools/eyes.sh Eyes.LandingFrame
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
    const FVector Up = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector Heading = FVector::CrossProduct(Up, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
    FString Report;
    for (const double Agl : { 5.0e6, 150.0 })
    {
        Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + Agl),
                        FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        // Looking out and a little down through the cockpit glass, as a pilot would.
        Capture->SetWorldLocationAndRotation(PilotEye, FRotator(-15.0, 0.0, 0.0));
        TArray<FColor> Pixel;
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
            Test.Step(1.0f / 60.0f);
            Test.World->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FlushRenderingCommands();
            // One pixel read back: the GPU has finished the frame before the clock reads.
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
        }
        const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0 / 20.0;
        const FString Line = FString::Printf(TEXT("%s: %.2f ms a frame (game step, render and GPU), %d tiles drawn\n%s\n"),
            Agl > 1.0e5 ? TEXT("50 km") : TEXT("1.5 m"), Ms, Test.Ground->GetDrawnKeys().Num(), *Test.Ground->Describe());
        Report += Line;
        AddInfo(Line);
        TestTrue(FString::Printf(TEXT("the frame fits 16.6 ms at %s (%.2f)"), Agl > 1.0e5 ? TEXT("50 km") : TEXT("1.5 m"), Ms), Ms <= 16.6);
    }
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/LandingFrame");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("report.txt")));
    return true;
}

#endif
