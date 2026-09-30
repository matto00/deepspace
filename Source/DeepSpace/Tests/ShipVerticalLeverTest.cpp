#include "Misc/AutomationTest.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipVerticalLever.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The third lever (landing decision 8): a climb or sink rate on a log scale,
 * 0.1 m/s just off zero to 200 m/s, the same both ways; zero is a place, the
 * detent; a sweep stops at HOVER; after X a press catches the ship at the
 * rate it is already moving; and the climb top falls on heavy worlds by
 * gravity alone (decision 5).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipVerticalLeverTest, "DeepSpace.Ship.VerticalLever",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipVerticalLeverTest::RunTest(const FString& Parameters)
{
    namespace V = ShipVerticalLever;
    const double Top = V::DefaultTopCmPerSecond;

    TestEqual(TEXT("zero is HOVER, exactly"), V::Rate(0.0, Top), 0.0);
    TestTrue(TEXT("a hair up is 0.1 m/s"), FMath::IsNearlyEqual(V::Rate(1.0e-9, Top), 10.0, 1e-3));
    TestEqual(TEXT("full up is 200 m/s"), V::Rate(1.0, Top), 2.0e4);
    TestEqual(TEXT("full down is 200 m/s sinking"), V::Rate(-1.0, Top), -2.0e4);
    TestTrue(TEXT("half is the geometric middle, 4.47 m/s"), FMath::IsNearlyEqual(V::Rate(0.5, Top), FMath::Sqrt(10.0 * 2.0e4), 1e-6));
    for (const double Rate : { 10.0, 50.0, 300.0, -300.0, 2.0e4, -1234.5 })
    {
        TestTrue(FString::Printf(TEXT("LeverOf inverts Rate at %.1f cm/s"), Rate),
                 FMath::IsNearlyEqual(V::Rate(V::LeverOf(Rate, Top), Top), Rate, 1e-9 * FMath::Abs(Rate)));
    }
    TestEqual(TEXT("a rate well under the floor is no lever at all"), V::LeverOf(2.0, Top), 0.0);

    // The sweep: 0.25 a second, stopping at HOVER from a climb; a sink is a
    // fresh press of C.
    double Lever = 0.3;
    for (int32 Frame = 0; Frame < 120; ++Frame)
    {
        Lever = V::Sweep(Lever, false, true, 0, 0, 1.0 / 60.0, V::DefaultSweep);
    }
    TestEqual(TEXT("C held from a climb stops at HOVER"), Lever, 0.0);
    TestEqual(TEXT("and holding it on moves nothing"), V::Sweep(Lever, false, true, 0, 0, 1.0, V::DefaultSweep), 0.0);
    TestTrue(TEXT("a fresh press of C leaves the detent to sink"), V::Sweep(Lever, false, true, 0, 1, 0.1, V::DefaultSweep) < 0.0);
    TestTrue(TEXT("rest to full in four seconds held"), FMath::IsNearlyEqual(V::Sweep(0.0, true, false, 1, 0, 4.0, V::DefaultSweep), 1.0, 1e-12));

    // After X: the lever at HOVER, the ship still sinking at 3 m/s; one C
    // catches it at the lever nearest 3 m/s.
    const double Caught = V::Catch(0.0, 0, 1, -300.0, Top);
    TestTrue(TEXT("one C catches a sinking ship at its own rate"), FMath::IsNearlyEqual(V::Rate(Caught, Top), -300.0, 1e-9));
    TestEqual(TEXT("a Space while sinking catches nothing; it sweeps from HOVER"), V::Catch(0.0, 1, 0, -300.0, Top), 0.0);
    TestEqual(TEXT("no press, no catch"), V::Catch(0.0, 0, 0, -300.0, Top), 0.0);
    TestEqual(TEXT("a lever already off HOVER is not caught"), V::Catch(0.2, 0, 1, -300.0, Top), 0.2);
    TestEqual(TEXT("a ship all but at rest is not caught"), V::Catch(0.0, 0, 1, -3.0, Top), 0.0);

    // The climb top, by gravity alone: 200 m/s x max(0.25, min(1, g_E / g)).
    const double G = ShipFlight::StandardGravityCmS2;
    TestEqual(TEXT("1 g: 200 m/s"), V::ClimbTop(Top, 1.0 * G, V::DefaultHeavyFloor), 2.0e4);
    TestEqual(TEXT("under 1 g: still 200 m/s"), V::ClimbTop(Top, 0.3 * G, V::DefaultHeavyFloor), 2.0e4);
    TestTrue(TEXT("2 g: 100 m/s"), FMath::IsNearlyEqual(V::ClimbTop(Top, 2.0 * G, V::DefaultHeavyFloor), 1.0e4, 1e-9));
    TestTrue(TEXT("3.3 g: 61 m/s"), FMath::IsNearlyEqual(V::ClimbTop(Top, 3.3 * G, V::DefaultHeavyFloor), 2.0e4 / 3.3, 1e-9));
    TestEqual(TEXT("4 g and more: the floor, 50 m/s"), V::ClimbTop(Top, 5.0 * G, V::DefaultHeavyFloor), 5.0e3);
    TestEqual(TEXT("no gravity: the top"), V::ClimbTop(Top, 0.0, V::DefaultHeavyFloor), 2.0e4);
    return true;
}

#endif
