#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * FRAMES, NOT A GUARD (sign-off item 2): Baemsekai III, IV and V from 200 km
 * under a low sun, from the helm's eye, for the developer to see the orbit's
 * relief flatten when (b) switches it to the ground's own slope. Run once
 * before the switch and once after, each into its own folder:
 *
 *   EYES_TAG=before Tools/eyes.sh Eyes.ReliefLook
 *   EYES_TAG=after  Tools/eyes.sh Eyes.ReliefLook
 *
 * Writes Saved/Eyes/ReliefLook/<tag>/world_<n>.png.
 *
 * A fresh editor draws a material as the engine's checkerboard until its
 * shaders compile, hence FinishAllCompilation before each capture.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReliefLookEyesTest, "Eyes.ReliefLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReliefLookEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    if (GUsingNullRHI)
    {
        AddError(TEXT("Eyes.ReliefLook renders: run it without -nullrhi"));
        return false;
    }
    const FString TagEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_TAG"));
    const FString Tag = TagEnv.IsEmpty() ? FString(TEXT("untagged")) : TagEnv;
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/ReliefLook") / Tag;
    IFileManager::Get().MakeDirectory(*Dir, true);

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
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->FOVAngle = 60.0f;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->SetWorldLocationAndRotation(PilotEye, FRotator::ZeroRotator);
    Test.BeginPlay();

    for (const TCHAR* Body : { TEXT("3"), TEXT("4"), TEXT("5") })
    {
        FOutputDeviceNull Out;
        const TArray<FString> Args = { Body, TEXT("200"), TEXT("dusk") };
        AShipSky::Goto(*Test.Ship, LocalSystem::Current(Test.World), false, Args, Out);
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
        const FString Path = Dir / FString::Printf(TEXT("world_%s.png"), Body);
        TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*Path));
        TestTrue(FString::Printf(TEXT("world %s framed"), Body), File && FImageUtils::ExportRenderTarget2DAsPNG(Target, *File));
    }
    return true;
}

#endif
