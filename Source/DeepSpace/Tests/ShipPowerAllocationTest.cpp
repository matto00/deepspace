#include "Misc/AutomationTest.h"
#include "Ship/ShipPowerState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPowerAllocationTest,
    "DeepSpace.Ship.PowerAllocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The allocation model. This is where the bugs live, so it is pure
 * arithmetic with no world and it is tested exhaustively.
 *
 * The rule the whole feature rests on: under-powered systems degrade, they
 * never fail. There is no cutoff anywhere in here, no alarm and no error
 * state -- a badly allocated ship is a dim, sluggish ship, which the player
 * reads immediately and fixes if they care (docs/vision.md, the anti-chore
 * principle).
 */
bool FShipPowerAllocationTest::RunTest(const FString& Parameters)
{
    const FName A(TEXT("A"));
    const FName B(TEXT("B"));
    const FName C(TEXT("C"));

    // Available output is shared in proportion to weight when everyone wants
    // more than there is.
    {
        FShipPowerState State;
        State.SetReactorOutput(900.0f);
        State.SetConsumer(A, 1000.0f, 1.0f);
        State.SetConsumer(B, 1000.0f, 1.0f);
        State.SetConsumer(C, 1000.0f, 1.0f);
        TestEqual(TEXT("an even split is even"), State.GetShare(A), 300.0f);
        TestEqual(TEXT("and even for the others"), State.GetShare(C), 300.0f);
        TestEqual(TEXT("satisfaction is share over want"), State.GetSatisfaction(A), 0.3f);
        TestEqual(TEXT("everything available is allocated"), State.GetAllocatedPower(), 900.0f);
    }

    // Weight is the only thing that decides the split.
    {
        FShipPowerState State;
        State.SetReactorOutput(800.0f);
        State.SetConsumer(A, 5000.0f, 2.0f);
        State.SetConsumer(B, 5000.0f, 1.0f);
        State.SetConsumer(C, 5000.0f, 1.0f);
        TestEqual(TEXT("double weight takes double the power"), State.GetShare(A), 400.0f);
        TestEqual(TEXT("the rest divide the remainder"), State.GetShare(B), 200.0f);
    }

    // Nobody is given more than they want, and what the cap frees goes to
    // whoever is still short. Setting a silly split wastes nothing silently.
    {
        FShipPowerState State;
        State.SetReactorOutput(900.0f);
        State.SetConsumer(A, 100.0f, 1.0f);
        State.SetConsumer(B, 1000.0f, 1.0f);
        State.SetConsumer(C, 1000.0f, 1.0f);
        TestEqual(TEXT("a small consumer takes only what it wants"), State.GetShare(A), 100.0f);
        TestEqual(TEXT("and is fully satisfied"), State.GetSatisfaction(A), 1.0f);
        TestEqual(TEXT("the surplus redistributes"), State.GetShare(B), 400.0f);
        TestEqual(TEXT("evenly"), State.GetShare(C), 400.0f);
        TestEqual(TEXT("nothing is left unallocated"), State.GetAllocatedPower(), 900.0f);
    }

    // The redistribution cascades: capping one consumer can push another
    // over its own want, and that must be caught in the same settle.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        State.SetConsumer(A, 50.0f, 1.0f);
        State.SetConsumer(B, 400.0f, 1.0f);
        State.SetConsumer(C, 5000.0f, 1.0f);
        TestEqual(TEXT("the smallest is capped"), State.GetShare(A), 50.0f);
        // A's 333 provisional share caps to 50; the freed 283 pushes B's
        // share past 400, so B caps too, and everything left lands on C.
        TestEqual(TEXT("the second cap is caught in the same settle"), State.GetShare(B), 400.0f);
        TestEqual(TEXT("the rest goes to the one still short"), State.GetShare(C), 550.0f);
    }

    // A consumer switched off wants nothing and takes part in no split --
    // which is what makes killing the lights to boost a real choice rather
    // than a menu exercise.
    {
        FShipPowerState State;
        State.SetReactorOutput(600.0f);
        State.SetConsumer(A, 0.0f, 1.0f);
        State.SetConsumer(B, 1000.0f, 1.0f);
        TestEqual(TEXT("a consumer wanting nothing gets nothing"), State.GetShare(A), 0.0f);
        TestEqual(TEXT("and does not dilute the split"), State.GetShare(B), 600.0f);
        // Nothing is owed, so nothing is short. Satisfaction drives how well
        // a thing works, and a thing that is off is not working badly.
        TestEqual(TEXT("wanting nothing is not being starved"), State.GetSatisfaction(A), 1.0f);
    }

    // Weights summing to zero: no claim is made, so no power is handed out.
    // Not an error, and nothing is clamped or corrected behind the player.
    {
        FShipPowerState State;
        State.SetReactorOutput(600.0f);
        State.SetConsumer(A, 500.0f, 0.0f);
        State.SetConsumer(B, 500.0f, 0.0f);
        TestEqual(TEXT("no weight, no share"), State.GetShare(A), 0.0f);
        TestEqual(TEXT("nothing is allocated"), State.GetAllocatedPower(), 0.0f);
        TestEqual(TEXT("and everyone is starved, not broken"), State.GetSatisfaction(A), 0.0f);
    }

    // A negative weight is a nonsense a UI could still produce; it means no
    // claim, and it must not make another consumer's share negative.
    {
        FShipPowerState State;
        State.SetReactorOutput(600.0f);
        State.SetConsumer(A, 500.0f, -3.0f);
        State.SetConsumer(B, 500.0f, 1.0f);
        TestEqual(TEXT("a negative weight claims nothing"), State.GetShare(A), 0.0f);
        TestEqual(TEXT("and does not distort anyone else"), State.GetShare(B), 500.0f);
    }

    // Installed modules draw off the top: what consumers divide is what the
    // reactor has left, not its rating.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        State.AddDraw(TEXT("Avionics"), 400.0f);
        State.SetConsumer(A, 1000.0f, 1.0f);
        TestEqual(TEXT("consumers divide what is left"), State.GetShare(A), 600.0f);
        TestEqual(TEXT("total draw counts both"), State.GetTotalDraw(), 1000.0f);
        TestEqual(TEXT("a fully committed reactor has no headroom"), State.GetHeadroom(), 0.0f);
    }

    // An overloaded reactor leaves nothing to allocate. Still no failure
    // state: consumers are starved, they are not switched off.
    {
        FShipPowerState State;
        State.SetReactorOutput(100.0f);
        State.AddDraw(TEXT("Avionics"), 400.0f);
        State.SetConsumer(A, 1000.0f, 1.0f);
        TestEqual(TEXT("an overloaded reactor allocates nothing"), State.GetShare(A), 0.0f);
        TestEqual(TEXT("satisfaction bottoms out at zero, never below"),
                  State.GetSatisfaction(A), 0.0f);
        TestTrue(TEXT("and the state says so plainly"), State.IsOverloaded());
        // Floored, not signed: "available" is what there is to hand out, and
        // a screen showing minus three hundred watts available is a bug
        // report, not a readout.
        TestEqual(TEXT("there is never a negative amount available"),
                  State.GetAvailablePower(), 0.0f);
    }

    // Headroom is what is genuinely spare: consumers that want less than
    // their share leave power unallocated rather than absorbing it.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        State.SetConsumer(A, 200.0f, 1.0f);
        State.SetConsumer(B, 300.0f, 1.0f);
        TestEqual(TEXT("everyone gets what they want"), State.GetShare(B), 300.0f);
        TestEqual(TEXT("the surplus stays spare"), State.GetHeadroom(), 500.0f);
    }

    // Weights persist. Nothing drifts, decays or retunes itself: an
    // allocation is a preference the player set, not a thing to service.
    {
        FShipPowerState State;
        State.SetReactorOutput(600.0f);
        State.SetConsumer(A, 1000.0f, 3.0f);
        State.SetConsumer(B, 1000.0f, 1.0f);
        State.SetReactorOutput(1200.0f);
        TestEqual(TEXT("the weight survives the reactor changing"), State.GetWeight(A), 3.0f);
        TestEqual(TEXT("and the split is recomputed from it"), State.GetShare(A), 900.0f);

        State.SetWant(A, 0.0f);
        TestEqual(TEXT("switching one off hands everything to the other"),
                  State.GetShare(B), 1000.0f);
        TestEqual(TEXT("its weight is remembered while it is off"), State.GetWeight(A), 3.0f);
        State.SetWant(A, 1000.0f);
        TestEqual(TEXT("and applies again when it comes back"), State.GetShare(A), 900.0f);
    }

    // Consumers can be listed and removed; an unknown one reads as nothing
    // rather than asserting, because a screen may outlive what it showed.
    {
        FShipPowerState State;
        State.SetReactorOutput(600.0f);
        State.SetConsumer(A, 100.0f, 1.0f);
        State.SetConsumer(B, 100.0f, 1.0f);
        TestEqual(TEXT("both consumers are listed"), State.GetConsumers().Num(), 2);
        TestTrue(TEXT("removing one reports success"), State.RemoveConsumer(A));
        TestFalse(TEXT("removing it twice does not"), State.RemoveConsumer(A));
        TestEqual(TEXT("an unknown consumer has no share"), State.GetShare(A), 0.0f);
        TestEqual(TEXT("and no want, so it reads as satisfied"), State.GetSatisfaction(A), 1.0f);
    }

    return true;
}

#endif
