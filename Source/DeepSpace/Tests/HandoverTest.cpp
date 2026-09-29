#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestFixtures.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 7: at 50 km the projection's magnification is exactly 1,
 * so the proxy is the true sphere; below it over a solid world the ground
 * draws the body and the proxy is hidden -- once the coarse cut is resident
 * above the drive floor, always under it -- and the body's look is the
 * ground's too. The depth stack is unchanged: the hidden proxy keeps its
 * rendered floor, so every other body still starts beyond it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkyHandoverTest, "DeepSpace.Sky.Handover",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkyProjectionAtGroundTest, "DeepSpace.Sky.ProjectionAtGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkyHandoverTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    // Nothing is uploaded while stepping, so the coarse cut is resident only
    // once the test flushes it: the prefetch starts building at 1,000 km,
    // and without this the 60 km frame's builds could land by the 45 km one.
    FScopedCVar Uploads(TEXT("ds.Terrain.UploadsPerFrame"), 0.0f);
    FSkyWorld Test(TEXT("HandoverWorld"), 8, EShadows::On);
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const int32 Index = 4;
    const FSkyBody& Fourth = Here.Bodies[Index];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const auto Over = [&](double DatumAltitude)
    {
        Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + DatumAltitude), FRotationMatrix::MakeFromX(-Out).ToQuat());
        Test.Step(1.0f / 60.0f);
    };
    const auto ProxyShown = [&]() { return Test.Sky->GetProxy(Index) && Test.Sky->GetProxy(Index)->IsVisible(); };

    Over(6.0e6);
    TestTrue(TEXT("60 km up the proxy draws Baemsekai IV"), ProxyShown() && !Test.Ground->IsDrawingBody());

    Over(4.5e6);
    TestTrue(TEXT("at 45 km, before the coarse cut is resident, the proxy still stands in"), ProxyShown());
    Test.Ground->FlushBuildsForTest();
    Test.Step(1.0f / 60.0f);
    TestTrue(TEXT("once it is, the ground takes the body and the proxy is hidden"), Test.Ground->IsDrawingBody() && !ProxyShown());
    TestTrue(TEXT("the morph has begun, and is not whole"), Test.Ground->GetMorph() > 0.0 && Test.Ground->GetMorph() < 1.0);

    UMaterialInstanceDynamic* Proxy = Cast<UMaterialInstanceDynamic>(Test.Sky->GetProxy(Index)->GetMaterial(0));
    UMaterialInstanceDynamic* Ground = Test.Ground->GetGroundMaterialInstance();
    TestTrue(TEXT("the ground wears the body's own look: the same light, colour, seed, brightness and relief"),
             Proxy && Ground
             && Proxy->K2_GetVectorParameterValue(SkyMaterial::LightDirection) == Ground->K2_GetVectorParameterValue(SkyMaterial::LightDirection)
             && Proxy->K2_GetVectorParameterValue(SkyMaterial::SurfaceSeed) == Ground->K2_GetVectorParameterValue(SkyMaterial::SurfaceSeed)
             && Proxy->K2_GetScalarParameterValue(SkyMaterial::Brightness) == Ground->K2_GetScalarParameterValue(SkyMaterial::Brightness)
             && Proxy->K2_GetScalarParameterValue(SkyMaterial::ReliefScale) == Ground->K2_GetScalarParameterValue(SkyMaterial::ReliefScale));

    Over(5.3e6);
    TestTrue(TEXT("at 53 km, inside the hysteresis, the ground keeps it"), Test.Ground->IsDrawingBody() && !ProxyShown());
    Over(5.6e6);
    TestTrue(TEXT("over 55 km it gives it back"), !Test.Ground->IsDrawingBody() && ProxyShown());

    // Under the drive floor the proxy is never drawn, resident or not.
    Over(UShipSubsystem::FloorFor(Fourth) - 5.0e5);
    TestTrue(TEXT("under the drive floor, the proxy is never drawn over a solid world"), Test.Ground->IsDrawingBody() && !ProxyShown());
    TestEqual(TEXT("and the relief is whole"), Test.Ground->GetMorph(), 1.0);

    // ds.Sky.Goto low: a placement under the ground is lifted by the ground's hard stop.
    {
        FOutputDeviceNull Log;
        const TArray<FString> Args = { TEXT("4"), TEXT("0.001") };
        AShipSky::Goto(*Ship, LocalSystem::Current(Test.World), false, Args, Log);
        for (int32 Frame = 0; Frame < 2; ++Frame)
        {
            Test.Step(1.0f / 60.0f);
        }
        TestTrue(TEXT("ds.Sky.Goto 4 0.001 ends above the ground, lifted by the hard stop"),
                 Ship->GetFlightState().GetFootprintClearance().Get(-1.0e9) >= -1.0);
    }
    return true;
}

bool FSkyProjectionAtGroundTest::RunTest(const FString& Parameters)
{
    // 1.5 m over the fixture home world: the hidden proxy keeps its clamp, so
    // its far side, and every other body beyond it, stays inside FarProxy --
    // lifting the clamp would put its centre at NearProxy / NearFactor, 2e8 km.
    const FSkySystem System = SkyTestFixtures::System();
    const FSkyBody& Home = System.Bodies[SkyTestFixtures::HomeIndex];
    const FVector Up = FVector(0.3, -0.2, 0.93).GetSafeNormal();
    const FSkyViewParams Params;
    const FSkyFrame Frame = SkyProjection::Project(System, Home.Position + Up * (Home.Radius + 150.0), Params);
    bool bInside = true;
    for (const FSkyBodyView& View : Frame.Bodies)
    {
        bInside &= View.ProxyLocation.Size() + View.ProxyRadius <= Params.FarProxy * (1.0 + 1e-9);
    }
    TestTrue(TEXT("at 1.5 m every body's proxy, the star's included, stays within FarProxy"), bInside);
    const FSkyBodyView& Near = Frame.Bodies[SkyTestFixtures::HomeIndex];
    TestTrue(TEXT("and the hidden proxy's near side is the rendered floor's, not the ship's height"),
             Near.ProxyLocation.Size() - Near.ProxyRadius >= Params.NearProxy * (1.0 - 1e-9));
    return true;
}

#endif
