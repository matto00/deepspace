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
 * GroundKeepsUpScenario.h's.
 *
 * Both fly the cast shadows as the game ships them (ds.Terrain.Shadows and
 * ds.Sky.ShadowMaps 1), never a test world's default of none: the tiles'
 * shadow is most of a build's cost, and that cost is what this guards.
 * GroundKeepsUp starts over the opening's side of IV, under a 62.6-degree
 * sun, where the day exit makes the shadow nearly free (worst gap 14.40 cm
 * with it or without). GroundKeepsUpAtDusk starts under a 10-degree sun,
 * where a tile costs about 9x its heights: the worst gap was 25.22 cm with
 * the shadow and 15.03 without, against 15.00 (the cast-shadow plan's Task
 * 7b, measured 2026-09-28). It is red until the developer's ruling on it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundKeepsUpTest, "DeepSpace.Surface.GroundKeepsUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundKeepsUpAtDuskTest, "DeepSpace.Surface.GroundKeepsUpAtDusk",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GroundKeepsUpLocal
{
    /** The flight and its judgement, over the opening side or at dusk. */
    bool FlyAndJudge(FAutomationTestBase& T, bool bAtDusk)
    {
        using namespace SkyTestWorld;
        FScopedCVar Tasks(TEXT("ds.Terrain.BuildTasks"), 2.0f);
        FScopedCVar Uploads(TEXT("ds.Terrain.UploadsPerFrame"), 4.0f);
        // The shadows as the game ships them: the tiles' and the maps'.
        FSkyWorld Test(bAtDusk ? TEXT("GroundKeepsUpAtDuskWorld") : TEXT("GroundKeepsUpWorld"), 8, EShadows::On);
        if (!T.TestNotNull(TEXT("the ground spawns before play"), Test.Ground))
        {
            return false;
        }
        Test.BeginPlay();
        if (bAtDusk && !T.TestTrue(TEXT("the ship is put over IV's dusk"), GroundKeepsUpScenario::PlaceOverDusk(Test)))
        {
            return false;
        }
        const GroundKeepsUpScenario::FResult R = GroundKeepsUpScenario::Fly(Test);
        if (!T.TestTrue(TEXT("the flight was flown"), R.bValid && R.Top.Num() == 2 && R.HeldOff.Num() == 2))
        {
            return false;
        }
        T.TestTrue(FString::Printf(TEXT("the descent reached the full sink, 200 m/s (%.1f m/s)"), R.Fastest / 100.0), R.Fastest >= 0.99 * 2.0e4);
        for (int32 Leg = 0; Leg < 2; ++Leg)
        {
            const double Height = Leg == 0 ? 500.0 : 50.0;
            T.AddInfo(FString::Printf(TEXT("at %.0f m: cruise reached %.2f of the skim cap%s"), Height, R.Top[Leg], R.HeldOff[Leg] ? TEXT(", held off a ridge") : TEXT("")));
            T.TestTrue(FString::Printf(TEXT("at %.0f m the ship flew at the skim cap's top, or a ridge held it off (%.2f)"), Height, R.Top[Leg]),
                   R.Top[Leg] >= 0.9 || R.HeldOff[Leg]);
        }
        T.AddInfo(FString::Printf(TEXT("%d frames under the drive floor, %d under 1 km; worst drawn gap %.2f cm"), R.UnderFloor, R.Low, R.Worst));
        T.TestTrue(TEXT("the flight spent frames under the floor and under 1 km"), R.UnderFloor > 1000 && R.Low > 1000);
        T.TestEqual(TEXT("under the drive floor the proxy is never drawn"), R.ProxyShown, 0);
        T.TestEqual(TEXT("and the ground draws the body every frame"), R.NotDrawing, 0);
        T.TestEqual(TEXT("under 1 km there is always drawn ground under the ship"), R.Missing, 0);
        T.TestTrue(FString::Printf(TEXT("and it is within GearClearance / 10 of the analytic ground, moving (worst %.2f cm)"), R.Worst),
                   R.Worst <= R.Tolerance);
        return true;
    }
}

bool FGroundKeepsUpTest::RunTest(const FString& Parameters)
{
    return GroundKeepsUpLocal::FlyAndJudge(*this, false);
}

bool FGroundKeepsUpAtDuskTest::RunTest(const FString& Parameters)
{
    return GroundKeepsUpLocal::FlyAndJudge(*this, true);
}

#endif
