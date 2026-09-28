#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Surface/WorldGround.h"
#include "TextureResource.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Landing decision 9's rendered case: from 49.9 km over Baemsekai IV, looking
 * straight down, the frame the ground draws (M_SkyGround on the tiles, the
 * proxy hidden) against the same frame the sky draws (M_SkyBody on the
 * proxy, the ground hidden), untonemapped. Both are unlit, so the scene
 * colour is each material's emissive. The mean over the central quarter
 * must agree to 1e-3 of itself -- the handover's "no brightness step" -- and
 * the ground must not be the engine's default material. The per-pixel
 * 99th percentile is reported: a sub-pixel difference in where the two
 * meshes put a pixel moves single pixels, never the mean.
 *
 *   Tools/eyes.sh Eyes.HandoverParity
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHandoverParityEyesTest, "Eyes.HandoverParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHandoverParityEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("HandoverParityWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const int32 Index = 4;
    const FSkyBody& Fourth = Here.Bodies[Index];
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    // Just under the handover: the morph has barely begun, so both sides
    // draw the same sphere, and the sky has copied the body's look across.
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + 4.99e6), FRotationMatrix::MakeFromX(-Out).ToQuat());
    Test.Step(1.0f / 60.0f);
    Test.Ground->FlushBuildsForTest();
    Test.Step(1.0f / 60.0f);
    UStaticMeshComponent* Proxy = Test.Sky->GetProxy(Index);
    if (!TestTrue(TEXT("the ground has the body, the proxy hidden"), Test.Ground->IsDrawingBody() && Proxy && !Proxy->IsVisible()))
    {
        return false;
    }
    const TArray<FTileKey> Keys = Test.Ground->GetDrawnKeys();
    const UPrimitiveComponent* AnyTile = Keys.Num() > 0 ? Test.Ground->GetTileComponent(Keys[0]) : nullptr;
    const UMaterialInterface* Worn = AnyTile ? AnyTile->GetMaterial(0) : nullptr;
    TestTrue(TEXT("the tiles wear M_SkyGround, not the engine's default"),
             Worn && Worn->GetMaterial() && Worn->GetMaterial()->GetPathName() == FString(SkyMaterial::GroundPath));

    constexpr int32 Size = 512;
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Test.Sky);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Test.Sky);
    Target->InitCustomFormat(Size, Size, PF_FloatRGBA, true);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->FOVAngle = 20.0f;
    Capture->CaptureSource = SCS_SceneColorHDR;
    // One exposure for both frames, whatever the sky's volume says.
    Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Capture->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    Capture->PostProcessBlendWeight = 1.0f;
    // At the ship, looking at the world's centre: straight down.
    const FVector Down = Ship->UniverseToWorld(Fourth.Position).GetSafeNormal();
    Capture->SetWorldLocationAndRotation(FVector::ZeroVector, Down.Rotation());

    const auto Shoot = [&](TArray<FLinearColor>& Pixels)
    {
        for (int32 Warm = 0; Warm < 3; ++Warm)
        {
            // A fresh editor draws a material as the engine's default until
            // its shaders compile: without this both frames are the default
            // material, lit by the sun, and agree or not by accident.
            if (GShaderCompilingManager)
            {
                GShaderCompilingManager->FinishAllCompilation();
            }
            Test.World->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FlushRenderingCommands();
        }
        Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels);
    };
    TArray<FLinearColor> Ground;
    Shoot(Ground);
    // The same scene with the sky's proxy instead. Nothing steps between
    // the two, so nothing else moves.
    Test.Ground->SetActorHiddenInGame(true);
    Proxy->SetVisibility(true);
    TArray<FLinearColor> Orbit;
    Shoot(Orbit);

    double SumGround = 0.0;
    double SumOrbit = 0.0;
    TArray<double> Gaps;
    for (int32 Y = Size / 4; Y < 3 * Size / 4; ++Y)
    {
        for (int32 X = Size / 4; X < 3 * Size / 4; ++X)
        {
            const FLinearColor& A = Ground[Y * Size + X];
            const FLinearColor& B = Orbit[Y * Size + X];
            const double LumA = (A.R + A.G + A.B) / 3.0;
            const double LumB = (B.R + B.G + B.B) / 3.0;
            SumGround += LumA;
            SumOrbit += LumB;
            Gaps.Add(FMath::Abs(LumA - LumB));
        }
    }
    const double MeanGround = SumGround / Gaps.Num();
    const double MeanOrbit = SumOrbit / Gaps.Num();
    Gaps.Sort();
    const double P99 = Gaps[FMath::FloorToInt32(0.99 * (Gaps.Num() - 1))];
    const FString Line = FString::Printf(TEXT("49.9 km over Baemsekai IV: ground mean %.6f, orbit mean %.6f, relative gap %.2e; per-pixel p99 %.2e"),
                                         MeanGround, MeanOrbit, FMath::Abs(MeanGround - MeanOrbit) / FMath::Max(MeanOrbit, 1e-12), P99);
    AddInfo(Line);
    TestTrue(TEXT("the orbit's frame is lit"), MeanOrbit > 0.0);
    TestTrue(FString::Printf(TEXT("the ground's frame is the orbit's to 1e-3 of it: no brightness step at the handover (%s)"), *Line),
             FMath::Abs(MeanGround - MeanOrbit) <= 1.0e-3 * MeanOrbit);
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/HandoverParity");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Line + TEXT("\n"), *(Dir / TEXT("report.txt")));
    return true;
}

#endif
