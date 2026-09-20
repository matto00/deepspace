#include "Misc/AutomationTest.h"
#include "Ship/ShipPowerState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPowerStateTest,
    "DeepSpace.Ship.PowerState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPowerStateTest::RunTest(const FString& Parameters)
{
    // A fresh state draws nothing and has all its reactor output spare.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        TestEqual(TEXT("fresh state draws nothing"), State.GetTotalDraw(), 0.0f);
        TestEqual(TEXT("headroom equals reactor output"), State.GetHeadroom(), 1000.0f);
        TestFalse(TEXT("fresh state is not overloaded"), State.IsOverloaded());
    }

    // Draws accumulate.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        TestTrue(TEXT("first add succeeds"), State.AddDraw(TEXT("LifeSupport"), 300.0f));
        TestTrue(TEXT("second add succeeds"), State.AddDraw(TEXT("Lights"), 120.0f));
        TestEqual(TEXT("draws sum"), State.GetTotalDraw(), 420.0f);
        TestEqual(TEXT("headroom reduced"), State.GetHeadroom(), 580.0f);
    }

    // Adding the same module twice is rejected rather than silently doubling draw.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        State.AddDraw(TEXT("LifeSupport"), 300.0f);
        TestFalse(TEXT("duplicate add is rejected"), State.AddDraw(TEXT("LifeSupport"), 300.0f));
        TestEqual(TEXT("draw unchanged by rejected add"), State.GetTotalDraw(), 300.0f);
    }

    // Removal frees the draw; removing something absent is reported, not ignored.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        State.AddDraw(TEXT("LifeSupport"), 300.0f);
        TestTrue(TEXT("remove succeeds"), State.RemoveDraw(TEXT("LifeSupport")));
        TestEqual(TEXT("draw released"), State.GetTotalDraw(), 0.0f);
        TestFalse(TEXT("removing absent module reports false"), State.RemoveDraw(TEXT("Nothing")));
    }

    // Exceeding reactor output is representable, not clamped away. Whether an
    // overloaded ship browns out is a gameplay decision for a later milestone;
    // the data layer must not quietly decide it here.
    {
        FShipPowerState State;
        State.SetReactorOutput(100.0f);
        State.AddDraw(TEXT("Engines"), 250.0f);
        TestEqual(TEXT("headroom goes negative"), State.GetHeadroom(), -150.0f);
        TestTrue(TEXT("state reports overloaded"), State.IsOverloaded());
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
