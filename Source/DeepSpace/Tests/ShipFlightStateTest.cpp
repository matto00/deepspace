#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Universe/UniverseUnits.h"

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

// Not "DeepSpace.Ship.FlightState.Drive": a test whose path has children
// becomes a group, and the test above silently stops running.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightDriveTest,
    "DeepSpace.Ship.FlightDrive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** A body the drive can close on: a sphere, as NearestSurfaceDistance
     *  would see it. */
    struct FDriveTarget
    {
        FUniversePosition Centre;
        double RadiusCm = 0.0;

        double SurfaceDistance(const FShipFlightState& State) const
        {
            return State.GetUniversePosition().DistanceTo(Centre) - RadiusCm;
        }
    };

    /** What the subsystem does each frame: read the room once, then step. */
    void DriveFor(FShipFlightState& State, const FDriveTarget& Target, double Elapsed,
                  double FrameDelta = 1.0 / 60.0)
    {
        const int32 Frames = FMath::RoundToInt32(Elapsed / FrameDelta);
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            State.SetDriveRoom(Target.SurfaceDistance(State));
            State.Step(FrameDelta);
        }
        State.SetDriveRoom(Target.SurfaceDistance(State));
    }

    FShipFlightCommand DriveCommand(double Throttle, const FVector& AttitudeRate = FVector::ZeroVector)
    {
        FShipFlightCommand Command = MakeCommand(Throttle, AttitudeRate);
        Command.bDrive = true;
        return Command;
    }

    /** An Earth, 1 AU dead ahead of a ship parked at the origin. */
    FDriveTarget EarthAtOneAU()
    {
        FDriveTarget Target;
        Target.Centre = FUniversePosition(FVector(UniverseUnits::CmPerAU, 0.0, 0.0));
        Target.RadiusCm = UniverseUnits::CmPerEarthRadius;
        return Target;
    }
}

bool FShipFlightDriveTest::RunTest(const FString& Parameters)
{
    const FDriveTarget Earth = EarthAtOneAU();

    // The drive's whole claim: the room falls by e every DriveTau, whatever
    // the room is. That is what makes a two-minute approach one continuous
    // event rather than a long nothing and then a sudden planet.
    {
        FShipFlightState State;
        State.SetCommand(DriveCommand(1.0));
        State.SetDriveRoom(Earth.SurfaceDistance(State));
        const double Start = State.GetDriveRoom();
        TestEqual(TEXT("room is the surface distance less the floor"), Start,
                  Earth.SurfaceDistance(State) - State.GetLimits().DriveFloor);

        const double Tau = State.GetLimits().DriveTau;
        DriveFor(State, Earth, Tau);
        TestTrue(TEXT("after DriveTau the room is R/e, to 1%"),
                 FMath::IsNearlyEqual(State.GetDriveRoom() / (Start / UE_DOUBLE_EULERS_NUMBER), 1.0, 0.01));

        // And again from wherever it now is: exponential, not a fixed rate.
        const double Middle = State.GetDriveRoom();
        DriveFor(State, Earth, Tau);
        TestTrue(TEXT("and R/e again over the next DriveTau"),
                 FMath::IsNearlyEqual(State.GetDriveRoom() / (Middle / UE_DOUBLE_EULERS_NUMBER), 1.0, 0.01));
        TestEqual(TEXT("the drive follows the nose"), State.GetVelocity().GetSafeNormal(), FVector::ForwardVector);
        TestEqual(TEXT("the drive has no inertia, so reports no acceleration"),
                  State.GetLinearAcceleration().Size(), 0.0);
    }

    // The room never goes below 0, and out of room the drive is cruise: it
    // hands back at the floor and goes no nearer than cruise speed allows.
    {
        FShipFlightState State;
        const double Floor = State.GetLimits().DriveFloor;
        State.SetDriveRoom(Floor * 0.5);
        TestEqual(TEXT("below the floor there is no room"), State.GetDriveRoom(), 0.0);
        State.SetDriveRoom(-1.0e9);
        TestEqual(TEXT("inside a body there is no room"), State.GetDriveRoom(), 0.0);

        FDriveTarget Low = Earth;
        Low.Centre = FUniversePosition(FVector(Earth.RadiusCm + Floor + 1000.0 * UniverseUnits::CmPerKm, 0.0, 0.0));
        State.SetCommand(DriveCommand(1.0));

        double Fastest = 0.0;
        const int32 Frames = 150 * 60;
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            State.SetDriveRoom(Low.SurfaceDistance(State));
            State.Step(1.0 / 60.0);
            if (State.GetDriveRoom() <= 0.0)
            {
                Fastest = FMath::Max(Fastest, State.GetSpeed());
            }
        }
        TestTrue(TEXT("the ship reached the floor"), State.GetDriveRoom() <= 0.0);
        TestTrue(TEXT("at the floor the drive is cruise and no faster"),
                 Fastest <= State.GetLimits().MaxSpeed * (1.0 + 1e-9));
        TestTrue(TEXT("and still cruise, not stopped"),
                 FMath::IsNearlyEqual(State.GetSpeed(), State.GetLimits().MaxSpeed, 1e-6));
    }

    // The drive never touches attitude: the pilot steers throughout, exactly
    // as under cruise, and a ship nobody turns does not turn.
    {
        const FVector Turn(0.5, -0.3, 0.7);
        FShipFlightState Driving;
        FShipFlightState Cruising;
        Driving.SetCommand(DriveCommand(1.0, Turn));
        Cruising.SetCommand(MakeCommand(1.0, Turn));
        DriveFor(Driving, Earth, 3.0);
        for (int32 Frame = 0; Frame < 180; ++Frame)
        {
            Cruising.Step(1.0 / 60.0);
        }
        TestTrue(TEXT("attitude under the drive matches attitude under cruise"),
                 Driving.GetUniverseOrientation().Equals(Cruising.GetUniverseOrientation(), 1e-12));
        TestEqual(TEXT("and so does the turn rate"), Driving.GetAngularVelocity(), Cruising.GetAngularVelocity());

        FShipFlightState Straight;
        Straight.SetCommand(DriveCommand(1.0));
        DriveFor(Straight, Earth, 10.0);
        TestTrue(TEXT("the drive never changes orientation"),
                 Straight.GetUniverseOrientation().Equals(FQuat::Identity, 0.0));
    }

    // Throttle below full stretches the time constant: the lever sets how
    // long the approach takes, and nothing says which is right.
    {
        FShipFlightState State;
        State.SetCommand(DriveCommand(0.5));
        State.SetDriveRoom(Earth.SurfaceDistance(State));
        const double Start = State.GetDriveRoom();
        DriveFor(State, Earth, 2.0 * State.GetLimits().DriveTau);
        TestTrue(TEXT("half throttle takes twice DriveTau to close by e"),
                 FMath::IsNearlyEqual(State.GetDriveRoom() / (Start / UE_DOUBLE_EULERS_NUMBER), 1.0, 0.01));
    }

    // Boosters on a quarter thrust make the effective tau four times longer.
    // The subsystem divides DriveTau by the thrust fraction; this pins what
    // that does to the approach.
    {
        const double Thrust = 0.25;
        FShipFlightLimits Starved;
        Starved.DriveTau = FShipFlightLimits::Cruise().DriveTau / Thrust;

        FShipFlightState State;
        State.SetLimits(Starved);
        State.SetCommand(DriveCommand(1.0));
        State.SetDriveRoom(Earth.SurfaceDistance(State));
        const double Start = State.GetDriveRoom();

        const double RatedTau = FShipFlightLimits::Cruise().DriveTau;
        DriveFor(State, Earth, RatedTau);
        TestTrue(TEXT("a starved ship has closed only a quarter-tau after one rated tau"),
                 FMath::IsNearlyEqual(State.GetDriveRoom() / (Start * FMath::Exp(-Thrust)), 1.0, 0.01));
        DriveFor(State, Earth, RatedTau / Thrust - RatedTau);
        TestTrue(TEXT("and R/e after four rated tau: slower, never stopped"),
                 FMath::IsNearlyEqual(State.GetDriveRoom() / (Start / UE_DOUBLE_EULERS_NUMBER), 1.0, 0.01));
    }

    // Disengaging clamps the speed to cruise: the lever off is cruise, at
    // once, still along the nose.
    {
        FShipFlightState State;
        State.SetCommand(DriveCommand(1.0));
        DriveFor(State, Earth, 1.0);
        TestTrue(TEXT("the drive is far past cruise"), State.GetSpeed() > 1000.0 * State.GetLimits().MaxSpeed);

        State.SetCommand(MakeCommand(1.0, FVector::ZeroVector));
        TestTrue(TEXT("disengaging clamps speed to MaxSpeed"),
                 FMath::IsNearlyEqual(State.GetSpeed(), State.GetLimits().MaxSpeed, 1e-6));
        TestTrue(TEXT("without turning the velocity"),
                 State.GetVelocity().GetSafeNormal().Equals(FVector::ForwardVector, 1e-12));
        State.Step(1.0);
        TestTrue(TEXT("and cruise carries on from there"),
                 FMath::IsNearlyEqual(State.GetSpeed(), State.GetLimits().MaxSpeed, 1e-6));
    }

    // It is a lever: standing up leaves it where it was.
    {
        FShipFlightState State;
        State.SetCommand(DriveCommand(0.8, FVector(0.2, 0.2, 0.2)));
        State.ReleaseAttitude();
        TestTrue(TEXT("the drive survives the pilot standing up"), State.GetCommand().bDrive);
    }

    // Drive off, the room is ignored entirely: cruise is exactly today's.
    {
        const FShipFlightCommand Command = MakeCommand(1.0, FVector(0.5, -0.3, 0.7));
        FShipFlightState WithRoom;
        FShipFlightState Without;
        WithRoom.SetCommand(Command);
        Without.SetCommand(Command);
        WithRoom.SetDriveRoom(UniverseUnits::CmPerAU);
        WithRoom.Step(2.03125);
        Without.Step(2.03125);
        TestTrue(TEXT("drive off, the room changes nothing"),
                 WithRoom.GetUniversePosition() == Without.GetUniversePosition()
                 && WithRoom.GetVelocity() == Without.GetVelocity());
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightJumpTest,
    "DeepSpace.Ship.FlightJump",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipFlightJumpTest::RunTest(const FString& Parameters)
{
    // A jump is a translation and nothing else. Turning the ship would turn
    // the distant dome, which is the one thing a jump must not move.
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(0.7, FVector(0.4, -0.6, 0.2)));
        State.Step(3.0);

        const FQuat Orientation = State.GetUniverseOrientation();
        const FVector Velocity = State.GetVelocity();
        const FVector AngularVelocity = State.GetAngularVelocity();

        const FUniversePosition Arrival(FInt64Vector(530000, -120000, 7), FVector(12345.0, 678.0, 9.0));
        State.JumpTo(Arrival);

        TestTrue(TEXT("the ship is at the arrival"), State.GetUniversePosition() == Arrival.Normalised());
        TestTrue(TEXT("orientation is bit-identical"),
                 State.GetUniverseOrientation().X == Orientation.X && State.GetUniverseOrientation().Y == Orientation.Y
                 && State.GetUniverseOrientation().Z == Orientation.Z && State.GetUniverseOrientation().W == Orientation.W);
        TestTrue(TEXT("velocity is bit-identical"), State.GetVelocity() == Velocity);
        TestTrue(TEXT("angular velocity is bit-identical"), State.GetAngularVelocity() == AngularVelocity);
    }

    // The charge parameter defaults to today's rate, so the power tests and
    // the subsystem's existing call are unchanged.
    {
        FShipFlightState Default;
        FShipFlightState Explicit;
        Default.ChargeJumpDrive(9.0, 1.0);
        Explicit.ChargeJumpDrive(9.0, 1.0, FShipFlightState::JumpChargeSeconds);
        TestEqual(TEXT("default parameter is JumpChargeSeconds"), Default.GetJumpCharge(), Explicit.GetJumpCharge());
        TestTrue(TEXT("and today's rate: 9 s of 90 is a tenth"),
                 FMath::IsNearlyEqual(Default.GetJumpCharge(), 0.1, 1e-12));

        FShipFlightState Quick;
        Quick.ChargeJumpDrive(3.0, 1.0, 3.0);
        TestEqual(TEXT("SecondsFromCold sets the time to full"), Quick.GetJumpCharge(), 1.0);

        FShipFlightState Half;
        Half.ChargeJumpDrive(3.0, 0.5, 3.0);
        TestTrue(TEXT("a half-fed engine takes twice as long"),
                 FMath::IsNearlyEqual(Half.GetJumpCharge(), 0.5, 1e-12));
    }

    // Nothing decays: a charge left alone is a charge still there, however
    // long the ship flies. The anti-chore principle, as a test.
    {
        FShipFlightState State;
        State.ChargeJumpDrive(45.0, 1.0);
        const double Held = State.GetJumpCharge();
        State.SetCommand(MakeCommand(1.0, FVector(0.1, 0.0, 0.0)));
        for (int32 Second = 0; Second < 600; ++Second)
        {
            State.Step(1.0);
        }
        TestEqual(TEXT("ten minutes of flight leaves the charge where it was"), State.GetJumpCharge(), Held);

        State.ChargeJumpDrive(1000.0, 1.0);
        TestEqual(TEXT("it fills to 1 and no further"), State.GetJumpCharge(), 1.0);
        State.SpendJumpCharge();
        TestEqual(TEXT("opening the fold spends all of it"), State.GetJumpCharge(), 0.0);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
