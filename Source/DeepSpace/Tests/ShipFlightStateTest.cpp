#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightStateTest,
    "DeepSpace.Ship.FlightState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    FShipFlightCommand MakeCommand(double Throttle, const FVector& AttitudeRate)
    {
        FShipFlightCommand Command;
        Command.Throttle = Throttle;
        Command.AttitudeRate = AttitudeRate;
        return Command;
    }

    /** Run Elapsed seconds in chunks no larger than the catch-up cap. */
    void RunFor(FShipFlightState& State, double Elapsed, double CallDelta)
    {
        const int32 Calls = FMath::RoundToInt32(Elapsed / CallDelta);
        for (int32 Index = 0; Index < Calls; ++Index)
        {
            State.Step(CallDelta);
        }
    }
}

bool FShipFlightStateTest::RunTest(const FString& Parameters)
{
    const double Step = FShipFlightState::FixedStep;

    // A fresh ship is parked, and stays parked when nobody commands it.
    {
        FShipFlightState State;
        RunFor(State, 2.0, 1.0 / 60.0);
        TestEqual(TEXT("an uncommanded ship stays at the origin"),
                  State.GetUniversePosition().DistanceTo(FUniversePosition()), 0.0);
        TestTrue(TEXT("an uncommanded ship keeps its attitude"),
                 State.GetUniverseOrientation().Equals(FQuat::Identity));
        TestEqual(TEXT("an uncommanded ship is at rest"), State.GetSpeed(), 0.0);
        TestEqual(TEXT("and not turning"), State.GetAngularVelocity().Size(), 0.0);
    }

    // Full throttle accelerates at exactly LinearAcceleration, and stops at
    // MaxSpeed however long it runs.
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(1.0, FVector::ZeroVector));

        State.Step(Step);
        TestEqual(TEXT("one substep of full throttle"), State.GetSpeed(),
                  State.GetLimits().LinearAcceleration * Step);
        TestEqual(TEXT("acceleration is reported"), State.GetLinearAcceleration().Size(),
                  State.GetLimits().LinearAcceleration);
        TestEqual(TEXT("thrust is along the ship's nose"), State.GetVelocity().GetSafeNormal(),
                  FVector::ForwardVector);

        // MaxSpeed is reached in 5 s at cruise; 30 s is well past it.
        RunFor(State, 30.0, 1.0 / 60.0);
        TestEqual(TEXT("cruise tops out at MaxSpeed"), State.GetSpeed(), State.GetLimits().MaxSpeed);
        TestEqual(TEXT("and stops accelerating there"), State.GetLinearAcceleration().Size(), 0.0);

        // At constant velocity, displacement is velocity times elapsed time.
        const FUniversePosition Before = State.GetUniversePosition();
        const FVector Velocity = State.GetVelocity();
        State.Step(1.0);
        TestEqual(TEXT("displacement is velocity times time"),
                  (State.GetUniversePosition() - Before).X, Velocity.X * 1.0);
    }

    // A held yaw spins up to MaxAngularRate.Y and no further. Angular
    // acceleration is what throwing bodies around will read, so it is an
    // output in its own right.
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(0.0, FVector(0.0, 1.0, 0.0)));

        State.Step(0.1);
        TestEqual(TEXT("yaw rate builds at AngularAcceleration"), State.GetAngularVelocity().Y,
                  State.GetLimits().AngularAcceleration.Y * 0.1);
        TestEqual(TEXT("and reports that acceleration"), State.GetAngularAcceleration().Y,
                  State.GetLimits().AngularAcceleration.Y);

        RunFor(State, 5.0, 1.0 / 60.0);
        TestEqual(TEXT("yaw tops out at MaxAngularRate"), State.GetAngularVelocity().Y,
                  State.GetLimits().MaxAngularRate.Y);
        TestEqual(TEXT("and stops accelerating there"), State.GetAngularAcceleration().Y, 0.0);
        TestEqual(TEXT("a pure yaw does not leak into pitch or roll"),
                  FVector(State.GetAngularVelocity().X, 0.0, State.GetAngularVelocity().Z),
                  FVector::ZeroVector);
    }

    // A full turn is a full turn: 2*pi of yaw comes back to where it started.
    // The limits here are chosen so the rate is reached in one substep and the
    // period is a whole number of substeps, which isolates the integration.
    {
        FShipFlightLimits Fast;
        Fast.MaxAngularRate = FVector(0.0, UE_DOUBLE_PI / 4.0, 0.0);
        Fast.AngularAcceleration = FVector(1000.0, 1000.0, 1000.0);

        FShipFlightState State;
        State.SetLimits(Fast);
        State.SetCommand(MakeCommand(0.0, FVector(0.0, 1.0, 0.0)));
        State.Step(Step);
        TestEqual(TEXT("the rate is reached immediately"), State.GetAngularVelocity().Y,
                  Fast.MaxAngularRate.Y);

        State.SetUniverseTransform(FUniversePosition(), FQuat::Identity);
        RunFor(State, 8.0, 2.0);   // one period at pi/4 rad/s
        TestTrue(TEXT("a full turn returns to identity"),
                 State.GetUniverseOrientation().GetForwardVector().Equals(FVector::ForwardVector, 1e-5));
    }

    // Quaternion integration drifts off the unit sphere. This is the test that
    // catches a missing renormalisation.
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(1.0, FVector(1.0, -1.0, 1.0)));
        RunFor(State, 84.0, 2.0);   // 10,080 substeps
        TestTrue(TEXT("the orientation is still a unit quaternion"),
                 FMath::Abs(State.GetUniverseOrientation().Size() - 1.0) < 1e-6);
    }

    // A bad caller cannot exceed the limits by asking for more than full.
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(5.0, FVector(3.0, -9.0, 2.0)));
        TestEqual(TEXT("throttle is clamped"), State.GetCommand().Throttle, 1.0);
        TestEqual(TEXT("attitude is clamped per axis"), State.GetCommand().AttitudeRate,
                  FVector(1.0, -1.0, 1.0));
    }

    // Standing up stops the turn but not the cruise.
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(0.6, FVector(0.5, 0.5, 0.5)));
        State.ReleaseAttitude();
        TestEqual(TEXT("attitude is released"), State.GetCommand().AttitudeRate, FVector::ZeroVector);
        TestEqual(TEXT("throttle is not"), State.GetCommand().Throttle, 0.6);
    }

    // Determinism. The same elapsed time delivered three different ways must
    // land in the same place. This is what the fixed step buys, and the test
    // that fails first if someone "simplifies" the accumulator away.
    //
    // 2.03125 s is 243.75 substeps: every chopping below is an exact binary
    // fraction, so the substep count cannot tip on a rounding error and the
    // test cannot flake.
    {
        const FShipFlightCommand Command = MakeCommand(1.0, FVector(0.5, -0.3, 0.7));

        FShipFlightState Lump;
        Lump.SetCommand(Command);
        Lump.Step(2.03125);

        FShipFlightState Even;
        Even.SetCommand(Command);
        RunFor(Even, 2.03125, 0.03125);

        FShipFlightState Uneven;
        Uneven.SetCommand(Command);
        for (int32 Index = 0; Index < 13; ++Index)
        {
            Uneven.Step(0.0625);
            Uneven.Step(0.015625);
            Uneven.Step(0.078125);
        }

        TestTrue(TEXT("even chopping matches one lump"),
                 (Even.GetUniversePosition() - Lump.GetUniversePosition()).Size() < 1e-6);
        TestTrue(TEXT("uneven chopping matches one lump"),
                 (Uneven.GetUniversePosition() - Lump.GetUniversePosition()).Size() < 1e-6);
        TestTrue(TEXT("and so does the attitude"),
                 Uneven.GetUniverseOrientation().Equals(Lump.GetUniverseOrientation(), 1e-8));
        TestTrue(TEXT("and the velocity"),
                 Uneven.GetVelocity().Equals(Lump.GetVelocity(), 1e-6));
        TestTrue(TEXT("the ship actually went somewhere"),
                 Lump.GetUniversePosition().DistanceTo(FUniversePosition()) > 100.0);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
