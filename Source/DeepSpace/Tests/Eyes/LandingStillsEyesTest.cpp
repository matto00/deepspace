#include "Components/SceneCaptureComponent2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/StringOutputDevice.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Sky/ShipSky.h"
#include "Sky/SkySystem.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * TEMPORARY: landing slice (a)'s done-when stills, for eyes, not a guard.
 * Named outside DeepSpace. so ./test.sh never runs it:
 *
 *   LANDING_STILLS_STAGE=before Tools/eyes.sh Eyes.LandingStills
 *
 * It flies home's Baemsekai III, IV and V as the done-when's console lines
 * do -- ds.Sky.Goto 3 30, 4 30, 5 30 and 4 30 dusk, through AShipSky::Goto
 * itself -- and writes one 1920 x 1080 frame of each from the helm's eye,
 * facing the world, into Saved/landing-a/<stage>_<shot>.png. Run once on
 * the engine-node M_SkyBody (before) and once on the shared file's
 * (after); compare the pairs. The ship's hull is not in this world, so the
 * frame is the sky alone. Deleted when slice (a) merges.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLandingStillsEyesTest,
    "Eyes.LandingStills",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LandingStillsEyesLocal
{
    constexpr int32 Width = 1920;
    constexpr int32 Height = 1080;
    constexpr float FovDeg = 90.0f;
}

bool FLandingStillsEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace LandingStillsEyesLocal;

    if (GUsingNullRHI)
    {
        AddError(TEXT("Eyes.LandingStills renders: run it with Tools/eyes.sh, never -nullrhi"));
        return false;
    }
    FString Stage = FPlatformMisc::GetEnvironmentVariable(TEXT("LANDING_STILLS_STAGE"));
    if (Stage.IsEmpty())
    {
        Stage = TEXT("now");
    }

    FSkyWorld Test(TEXT("LandingStillsWorld"));
    if (UStaticMesh* BodyMesh = LoadObject<UStaticMesh>(nullptr, SkyMaterial::BodyMeshPath))
    {
        Test.Sky->BodyMesh = BodyMesh; // what place_sky gives the level
    }
    AActor* Camera = Test.World->SpawnActor<AActor>();
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Camera, TEXT("HelmEye"));
    Camera->SetRootComponent(Capture);
    Capture->RegisterComponent();
    Capture->FOVAngle = FovDeg;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    // Two runs must differ only in the material: no history-dependent AA.
    Capture->ShowFlags.SetTemporalAA(false);
    Capture->ShowFlags.SetMotionBlur(false);
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Camera);
    Target->RenderTargetFormat = RTF_RGBA8;
    Target->InitAutoFormat(Width, Height);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Test.BeginPlay();

    const TOptional<FStarSystem> Home = Test.Universe->GetSystem(Test.Universe->GetStartSystem());
    if (!TestTrue(TEXT("home generates"), Home.IsSet()))
    {
        return false;
    }
    const FSkySystem Sky = FSkySystem::FromSystem(*Home, {});

    const FString Dir = FPaths::ProjectSavedDir() / TEXT("landing-a");
    IFileManager::Get().MakeDirectory(*Dir, true);
    TArray<FString> Report;
    struct FShot { const TCHAR* Name; TArray<FString> Args; };
    const FShot Shots[] = {
        { TEXT("III_30km"), { TEXT("3"), TEXT("30") } },
        { TEXT("IV_30km"), { TEXT("4"), TEXT("30") } },
        { TEXT("V_30km"), { TEXT("5"), TEXT("30") } },
        { TEXT("IV_30km_dusk"), { TEXT("4"), TEXT("30"), TEXT("dusk") } },
    };
    for (const FShot& Shot : Shots)
    {
        FStringOutputDevice Said;
        AShipSky::Goto(*Test.Ship, Sky, false, Shot.Args, Said);
        // The ship is the origin and faces the world along its own +X.
        Capture->SetWorldLocationAndRotation(PilotEye, FRotator::ZeroRotator);
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
        const FString Path = Dir / FString::Printf(TEXT("%s_%s.png"), *Stage, Shot.Name);
        TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*Path));
        TestTrue(FString::Printf(TEXT("%s is written"), Shot.Name), File && FImageUtils::ExportRenderTarget2DAsPNG(Target, *File));
        Report.Add(FString::Printf(TEXT("%s: %s -> %s"), Shot.Name, *FString(Said).TrimStartAndEnd(), *Path));
    }
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / (Stage + TEXT("_report.txt"))));
    AddInfo(FString::Join(Report, TEXT(" | ")));
    return true;
}

#endif
