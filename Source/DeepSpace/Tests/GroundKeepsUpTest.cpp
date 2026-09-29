#include "Misc/AutomationTest.h"
#include "Tests/GroundKeepsUpScenario.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Slice (b)'s done-when with the ship moving (landing decision 6: residency
 * never gates motion; the skim cap and the prefetch keep the ground ahead
 * of the ship). Over Baemsekai IV: from the drive floor at the full sink,
 * then cruising at the skim cap's top at 500 m and at 50 m. The builds run
 * on ds.Terrain.BuildTasks workers, uploaded ds.Terrain.UploadsPerFrame a
 * frame, never flushed; each frame is paced to the wall clock, as play is,
 * so the workers get the time they get in play -- which makes this the
 * slowest test in the suite, about two minutes, by design. Every frame
 * under the drive floor the ground draws the body and the proxy is hidden;
 * every frame under 1 km the drawn ground under the ship is within
 * GearClearance / 10 of the analytic ground. The flight itself is
 * GroundKeepsUpScenario.h's, which Eyes.ShadowBakeCost flies with the
 * cast shadow on.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundKeepsUpTest, "DeepSpace.Surface.GroundKeepsUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroundKeepsUpTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FScopedCVar Tasks(TEXT("ds.Terrain.BuildTasks"), 2.0f);
    FScopedCVar Uploads(TEXT("ds.Terrain.UploadsPerFrame"), 4.0f);
    FSkyWorld Test(TEXT("GroundKeepsUpWorld"));
    if (!TestNotNull(TEXT("the ground spawns before play"), Test.Ground))
    {
        return false;
    }
    Test.BeginPlay();
    const GroundKeepsUpScenario::FResult R = GroundKeepsUpScenario::Fly(Test);
    if (!TestTrue(TEXT("the flight was flown"), R.bValid && R.Top.Num() == 2 && R.HeldOff.Num() == 2))
    {
        return false;
    }
    TestTrue(FString::Printf(TEXT("the descent reached the full sink, 200 m/s (%.1f m/s)"), R.Fastest / 100.0), R.Fastest >= 0.99 * 2.0e4);
    for (int32 Leg = 0; Leg < 2; ++Leg)
    {
        const double Height = Leg == 0 ? 500.0 : 50.0;
        AddInfo(FString::Printf(TEXT("at %.0f m: cruise reached %.2f of the skim cap%s"), Height, R.Top[Leg], R.HeldOff[Leg] ? TEXT(", held off a ridge") : TEXT("")));
        TestTrue(FString::Printf(TEXT("at %.0f m the ship flew at the skim cap's top, or a ridge held it off (%.2f)"), Height, R.Top[Leg]),
                 R.Top[Leg] >= 0.9 || R.HeldOff[Leg]);
    }
    AddInfo(FString::Printf(TEXT("%d frames under the drive floor, %d under 1 km; worst drawn gap %.2f cm"), R.UnderFloor, R.Low, R.Worst));
    TestTrue(TEXT("the flight spent frames under the floor and under 1 km"), R.UnderFloor > 1000 && R.Low > 1000);
    TestEqual(TEXT("under the drive floor the proxy is never drawn"), R.ProxyShown, 0);
    TestEqual(TEXT("and the ground draws the body every frame"), R.NotDrawing, 0);
    TestEqual(TEXT("under 1 km there is always drawn ground under the ship"), R.Missing, 0);
    TestTrue(FString::Printf(TEXT("and it is within GearClearance / 10 of the analytic ground, moving (worst %.2f cm)"), R.Worst),
             R.Worst <= R.Tolerance);
    return true;
}

#endif
