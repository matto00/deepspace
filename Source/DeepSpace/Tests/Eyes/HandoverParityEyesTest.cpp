#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
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
#include "Surface/SunShadowMap.h"
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
    FSkyWorld Test(TEXT("HandoverParityWorld"), 8, EShadows::On);
    Test.Sky->bKeepShadowMapsForTest = true;
    Test.BeginPlay();
    // Every world's cast-shadow map is baked before any frame is judged.
    Test.Step(1.0f / 60.0f);
    Test.Sky->FlushShadowBakesForTest();
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
    // The cast shadow at the handover (the developer's ruling on slice (b)'s
    // build): at 49.9 km over Baemsekai IV under a 3-degree dusk the ground's
    // frame is still the orbit's to 1e-3, and the shadow reaches both -- with
    // ds.Sky.Shadows 0 each is brighter, by the same share. At 49.9 km the
    // morph is about 3e-5, so the ground's shadow is the orbit's map to that.
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    if (!TestNotNull(TEXT("ds.Sky.Shadows exists"), Shadows))
    {
        return false;
    }
    const float ShadowsWere = Shadows->GetFloat();
    const auto CentralMean = [](const TArray<FLinearColor>& Pixels)
    {
        double Sum = 0.0;
        int32 Count = 0;
        for (int32 Y = Size / 4; Y < 3 * Size / 4; ++Y)
        {
            for (int32 X = Size / 4; X < 3 * Size / 4; ++X)
            {
                const FLinearColor& P = Pixels[Y * Size + X];
                Sum += (P.R + P.G + P.B) / 3.0;
                ++Count;
            }
        }
        return Sum / Count;
    };
    // The seam, not the mean: the two meshes can put a pixel in slightly
    // different places (this test's header), and a shadow's edge moves more
    // pixels than shading does. So the per-pixel p99 gap is taken with the
    // shadow and without, and the shadow may not grow it past 3x.
    const auto GapP99 = [](const TArray<FLinearColor>& A, const TArray<FLinearColor>& B)
    {
        TArray<double> Gaps;
        for (int32 Y = Size / 4; Y < 3 * Size / 4; ++Y)
        {
            for (int32 X = Size / 4; X < 3 * Size / 4; ++X)
            {
                const FLinearColor& P = A[Y * Size + X];
                const FLinearColor& Q = B[Y * Size + X];
                Gaps.Add(FMath::Abs((P.R + P.G + P.B) / 3.0 - (Q.R + Q.G + Q.B) / 3.0));
            }
        }
        Gaps.Sort();
        return Gaps[FMath::FloorToInt32(0.99 * (Gaps.Num() - 1))];
    };
    const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, Index, 4.99e6, Ship->GetFlightState().GetUniversePosition(),
                                                                 ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(3.0));
    if (!TestTrue(TEXT("goto dusk places over Baemsekai IV"), Dusk.IsSet()))
    {
        return false;
    }
    // The view must hold shade before the shadow can be said to reach it.
    // The capture is 20 degrees straight down from 49.9 km, so its central
    // quarter is about 4.4 km of ground either way: about one 8.8 km texel.
    // Where that texel falls would decide the leg. So the map itself is read
    // first -- SunShadowMap::Sample over the central quarter at a pixel's
    // footprint -- and the placement is turned about the light, which keeps
    // the star 3 degrees up at the nadir, until the map shades that ground
    // by at least a tenth.
    const FSunShadowMap* Map = Test.Sky->GetShadowMapForTest(Fourth.Id);
    if (!TestNotNull(TEXT("Baemsekai IV's map is baked and kept"), Map))
    {
        return false;
    }
    const auto ViewMean = [&](const FUniversePosition& Position)
    {
        const FVector Offset = Position - Fourth.Position;
        const FVector3d Nadir = FVector3d(Offset.GetSafeNormal());
        const double Altitude = Offset.Size() - Fourth.Radius;
        const double TanHalf = FMath::Tan(FMath::DegreesToRadians(0.5 * Capture->FOVAngle));
        const double Half = Altitude * 0.5 * TanHalf / Fourth.Radius;     // the central quarter's half-width, rad
        const double Pixel = 2.0 * TanHalf * Altitude / Size / Fourth.Radius;   // one pixel's footprint, D units
        const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Nadir).GetSafeNormal();
        const FVector3d North = FVector3d::CrossProduct(Nadir, East);
        double Sum = 0.0;
        int32 Count = 0;
        for (int32 Y = -8; Y <= 8; ++Y)
        {
            for (int32 X = -8; X <= 8; ++X)
            {
                Sum += SunShadowMap::Sample(*Map, (Nadir + (East * X + North * Y) * (Half / 8.0)).GetSafeNormal(), Pixel);
                ++Count;
            }
        }
        return Sum / Count;
    };
    FNavPlacement Placed = *Dusk;
    double Shade = ViewMean(Placed.Position);
    for (int32 Turn = 1; Turn <= 720 && Shade > 0.9; ++Turn)
    {
        // 0.05 degrees a turn, about 4.7 km on IV: half a texel.
        const FQuat About(FVector(Map->FrameZ), Turn * FMath::DegreesToRadians(0.05));
        FNavPlacement Trial = *Dusk;
        Trial.Position = Fourth.Position + About.RotateVector(Dusk->Position - Fourth.Position);
        Trial.Orientation = About * Dusk->Orientation;
        const double Seen = ViewMean(Trial.Position);
        if (Seen < Shade)
        {
            Shade = Seen;
            Placed = Trial;
        }
    }
    if (!TestTrue(FString::Printf(TEXT("the view holds shade: the map's mean over the central quarter is %.3f, at most 0.9"), Shade), Shade <= 0.9))
    {
        return false;   // no leg can judge the handover's shadow on ground the map leaves lit
    }
    double Means[2][2] = { { 0.0, 0.0 }, { 0.0, 0.0 } };   // [shadows][ground, orbit]
    double DuskP99[2] = { 0.0, 0.0 };                          // [shadows]
    for (int32 On = 0; On < 2; ++On)
    {
        Shadows->Set(static_cast<float>(On), ECVF_SetByCode);
        Test.Ground->SetActorHiddenInGame(false);
        Ship->PlaceShip(Placed.Position, Placed.Orientation);
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        if (!TestTrue(TEXT("at dusk too the ground has the body, the proxy hidden"), Test.Ground->IsDrawingBody() && !Proxy->IsVisible()))
        {
            return false;
        }
        Capture->SetWorldLocationAndRotation(FVector::ZeroVector, Ship->UniverseToWorld(Fourth.Position).GetSafeNormal().Rotation());
        TArray<FLinearColor> Seen;
        Shoot(Seen);
        Means[On][0] = CentralMean(Seen);
        const TArray<FLinearColor> GroundSeen = Seen;
        Test.Ground->SetActorHiddenInGame(true);
        Proxy->SetVisibility(true);
        Shoot(Seen);
        Means[On][1] = CentralMean(Seen);
        DuskP99[On] = GapP99(GroundSeen, Seen);
    }
    Shadows->Set(ShadowsWere, ECVF_SetByCode);
    const double GroundShare = Means[1][0] / FMath::Max(Means[0][0], 1e-12);
    const double OrbitShare = Means[1][1] / FMath::Max(Means[0][1], 1e-12);
    const FString DuskLine = FString::Printf(TEXT("49.9 km over Baemsekai IV at a 3-degree dusk (the map's mean over the view %.3f): ground %.6f (%.6f without the shadow), orbit %.6f (%.6f); the shadow keeps %.4f of the ground's light, %.4f of the orbit's; per-pixel p99 %.2e with it, %.2e without"),
        Shade, Means[1][0], Means[0][0], Means[1][1], Means[0][1], GroundShare, OrbitShare, DuskP99[1], DuskP99[0]);
    AddInfo(DuskLine);
    // Without the shadow first: at a 3-degree dusk the ground and the orbit
    // must already agree, or no shadow can be held to them.
    TestTrue(FString::Printf(TEXT("without the shadow the ground's frame is the orbit's to 1e-3 of it at dusk (%.6f against %.6f)"), Means[0][0], Means[0][1]),
        FMath::Abs(Means[0][0] - Means[0][1]) <= 1.0e-3 * Means[0][1]);
    TestTrue(FString::Printf(TEXT("with the shadow the ground's frame is the orbit's to 1e-3 of it (%s)"), *DuskLine),
        FMath::Abs(Means[1][0] - Means[1][1]) <= 1.0e-3 * Means[1][1]);
    TestTrue(TEXT("the shadow reaches both frames: each is darker with it"), GroundShare < 0.99 && OrbitShare < 0.99);
    TestTrue(TEXT("by the same share, to 1e-3"), FMath::Abs(GroundShare - OrbitShare) <= 1.0e-3);
    TestTrue(FString::Printf(TEXT("and the seam: the shadow grows the per-pixel p99 gap at most 3x (%.2e against %.2e)"), DuskP99[1], DuskP99[0]),
        DuskP99[1] <= 3.0 * FMath::Max(DuskP99[0], 1.0e-4));
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/HandoverParity");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Line + TEXT("\n") + DuskLine + TEXT("\n"), *(Dir / TEXT("report.txt")));
    return true;
}

#endif
