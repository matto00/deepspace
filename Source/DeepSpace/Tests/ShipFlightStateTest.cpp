#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Sky/SkyProjection.h"
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

        // MaxSpeed is reached in 10 s at cruise; 30 s is well past it.
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

    // A held pitch spins up to MaxAngularRate.Y and no further. Angular
    // acceleration is what throwing bodies around will read, so it is an
    // output in its own right.
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(0.0, FVector(0.0, 1.0, 0.0)));

        State.Step(0.1);
        TestEqual(TEXT("pitch rate builds at AngularAcceleration"), State.GetAngularVelocity().Y,
                  State.GetLimits().AngularAcceleration.Y * 0.1);
        TestEqual(TEXT("and reports that acceleration"), State.GetAngularAcceleration().Y,
                  State.GetLimits().AngularAcceleration.Y);

        RunFor(State, 5.0, 1.0 / 60.0);
        TestEqual(TEXT("pitch tops out at MaxAngularRate"), State.GetAngularVelocity().Y,
                  State.GetLimits().MaxAngularRate.Y);
        TestEqual(TEXT("and stops accelerating there"), State.GetAngularAcceleration().Y, 0.0);
        TestEqual(TEXT("a pure pitch does not leak into yaw or roll"),
                  FVector(State.GetAngularVelocity().X, 0.0, State.GetAngularVelocity().Z),
                  FVector::ZeroVector);
    }

    // A full turn is a full turn: 2*pi of pitch comes back to where it started.
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
        State.SetCommand(MakeCommand(-5.0, FVector::ZeroVector));
        TestEqual(TEXT("and astern, to the lever's astern end-stop"), State.GetCommand().Throttle, -State.CruiseAsternLimit());
        TestTrue(TEXT("which asks for 200 m/s astern"), FMath::IsNearlyEqual(State.GetLeverSpeed(), -State.GetLimits().AsternSpeed, 1e-6));
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

namespace FlightDriveTestLocal
{
    constexpr double Km = UniverseUnits::CmPerKm;
    constexpr double AU = UniverseUnits::CmPerAU;
    constexpr double Light = ShipDriveLever::LightCmPerSecond;
    constexpr double EarthRadius = UniverseUnits::CmPerEarthRadius;
    constexpr double JupiterRadius = 7.1492e9;
    constexpr double MoonRadius = 1.7374e8;

    /** The lever's top notch at ds.Drive.Top's default, 0.1 c: 11. */
    int32 TopNotch()
    {
        return ShipDriveLever::NotchCount(ShipDriveLever::DefaultTopLight * Light) - 1;
    }

    /** A world's floor sphere, at the floor the subsystem's FloorFor gives a
     *  planet: the larger of ds.Flight.Floor's default and the sky's own. */
    FFlightSurface World(const FUniversePosition& Centre, double Radius)
    {
        FFlightSurface Surface;
        Surface.Centre = Centre;
        Surface.Radius = Radius;
        Surface.Floor = FMath::Max(ShipFlight::DefaultFloorCm, SkyProjection::RenderedFloor(Radius, FSkyViewParams()));
        return Surface;
    }

    FFlightSurface Star(const FUniversePosition& Centre, double Radius)
    {
        FFlightSurface Surface;
        Surface.Centre = Centre;
        Surface.Radius = Radius;
        Surface.Floor = ShipFlight::DefaultStarFloorRadii * Radius;
        return Surface;
    }

    FUniversePosition At(double X, double Y = 0.0, double Z = 0.0)
    {
        return FUniversePosition(FVector(X, Y, Z));
    }

    FQuat Facing(const FVector& Nose)
    {
        return FRotationMatrix::MakeFromX(Nose.GetSafeNormal()).ToQuat();
    }

    /** A ship at From, its nose along Nose, the drive engaged at Notch. */
    FShipFlightState Driving(const FUniversePosition& From, const FVector& Nose, int32 Notch,
                             TArray<FFlightSurface> Surfaces = {})
    {
        FShipFlightState State;
        State.SetUniverseTransform(From, Facing(Nose));
        FShipFlightCommand Command;
        Command.bDrive = true;
        Command.DriveNotch = Notch;
        State.SetCommand(Command);
        State.SetSurfaces(MoveTemp(Surfaces));
        return State;
    }

    double LeastClearance(const FShipFlightState& State)
    {
        double Least = TNumericLimits<double>::Max();
        for (const FFlightSurface& Surface : State.GetSurfaces())
        {
            Least = FMath::Min(Least, ShipFlight::FloorClearance(Surface, State.GetUniversePosition()));
        }
        return Least;
    }

    /** What one flight did, frame by frame. */
    struct FFlown
    {
        /** When the ship was first within AtFloorCm of Target's floor; -1 never. */
        double AtFloorAfter = -1.0;

        /** The least clearance over every surface, at any frame. */
        double Least = TNumericLimits<double>::Max();

        /** The least distance to Target's centre, at any frame. */
        double Closest = TNumericLimits<double>::Max();

        /** Whether, moving, the velocity was along the nose at every frame. */
        bool bAlongNose = true;
    };

    /**
     * Fly for Seconds in frames of FrameDelta, one frame of HitchSeconds
     * instead once HitchAt has passed (none if HitchAt is negative): what the
     * subsystem's tick does, less the subsystem.
     */
    FFlown Fly(FShipFlightState& State, double Seconds, double FrameDelta, const FFlightSurface* Target = nullptr,
               double HitchAt = -1.0, double HitchSeconds = 2.0)
    {
        FFlown Flown;
        double Time = 0.0;
        bool bHitched = HitchAt < 0.0;
        while (Time < Seconds)
        {
            const double Delta = !bHitched && Time >= HitchAt ? HitchSeconds : FrameDelta;
            bHitched |= Delta == HitchSeconds;
            State.Step(Delta);
            Time += Delta;
            Flown.Least = FMath::Min(Flown.Least, LeastClearance(State));
            if (State.GetSpeed() > 0.0)
            {
                Flown.bAlongNose &= State.GetVelocity().GetSafeNormal().Equals(
                    State.GetUniverseOrientation().GetForwardVector(), 1e-9);
            }
            if (Target)
            {
                Flown.Closest = FMath::Min(Flown.Closest, State.GetUniversePosition().DistanceTo(Target->Centre));
                if (Flown.AtFloorAfter < 0.0
                    && ShipFlight::FloorClearance(*Target, State.GetUniversePosition()) <= FShipFlightState::AtFloorCm)
                {
                    Flown.AtFloorAfter = Time;
                }
            }
        }
        return Flown;
    }
}

bool FShipFlightDriveTest::RunTest(const FString& Parameters)
{
    using namespace FlightDriveTestLocal;
    const double Step = FShipFlightState::FixedStep;
    const int32 Top = TopNotch();
    const double TopSpeed = ShipDriveLever::NotchSpeed(Top);
    TestEqual(TEXT("the lever tops out at notch 11"), Top, 11);
    TestEqual(TEXT("which is 0.1 c"), TopSpeed, 0.1 * Light);

    // -- The lever ------------------------------------------------------------
    // A notch's speed is its table speed once settled, the ease never
    // overshoots, STOP is rest, and there is no reverse.
    {
        FShipFlightState State = Driving(FUniversePosition(), FVector::ForwardVector, 5);
        double Previous = 0.0;
        bool bRising = true;
        bool bNeverOver = true;
        for (int32 Frame = 0; Frame < 8 * 60; ++Frame)
        {
            State.Step(1.0 / 60.0);
            bRising &= State.GetSpeed() >= Previous;
            bNeverOver &= State.GetSpeed() <= ShipDriveLever::NotchSpeed(5);
            Previous = State.GetSpeed();
        }
        TestEqual(TEXT("settled, notch 5 is 500 km/s exactly"), State.GetSpeed(), ShipDriveLever::NotchSpeed(5));
        TestTrue(TEXT("rising all the way"), bRising);
        TestTrue(TEXT("and never past it"), bNeverOver);
        TestTrue(TEXT("along the nose"), State.GetVelocity().GetSafeNormal().Equals(FVector::ForwardVector, 1e-12));
        TestEqual(TEXT("the drive has no inertia, so reports no acceleration"), State.GetLinearAcceleration().Size(), 0.0);
        TestEqual(TEXT("the mode is the drive"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::Drive));
        TestEqual(TEXT("the lever speed is the notch's"), State.GetLeverSpeed(), ShipDriveLever::NotchSpeed(5));

        FShipFlightCommand Command = State.GetCommand();
        Command.DriveNotch = 0;
        State.SetCommand(Command);
        for (int32 Frame = 0; Frame < 10 * 60; ++Frame)
        {
            State.Step(1.0 / 60.0);
        }
        TestEqual(TEXT("STOP is rest, exactly"), State.GetSpeed(), 0.0);

        Command.DriveNotch = -4;
        State.SetCommand(Command);
        TestEqual(TEXT("there is no reverse: below STOP is STOP"), State.GetCommand().DriveNotch, 0);
        Command.DriveNotch = 99;
        State.SetCommand(Command);
        TestEqual(TEXT("and past the top is the top"), State.GetCommand().DriveNotch, Top);
    }

    // -- The top --------------------------------------------------------------
    // No notch and no speed above 0.1 c, whatever the limit says.
    {
        FShipFlightState State = Driving(FUniversePosition(), FVector::ForwardVector, 99);
        FShipFlightLimits Fast = State.GetLimits();
        Fast.DriveTop = 100.0 * Light;
        State.SetLimits(Fast);
        TestEqual(TEXT("a top of 100 c adds no notch"), State.GetDriveNotchCount(), Top + 1);
        FShipFlightCommand Command = State.GetCommand();
        Command.DriveNotch = 99;
        State.SetCommand(Command);
        TestEqual(TEXT("and the lever still stops at 0.1 c"), State.GetCommand().DriveNotch, Top);
        double Fastest = 0.0;
        for (int32 Frame = 0; Frame < 20 * 60; ++Frame)
        {
            State.Step(1.0 / 60.0);
            Fastest = FMath::Max(Fastest, State.GetSpeed());
        }
        TestEqual(TEXT("the ship tops out at 0.1 c exactly"), State.GetSpeed(), TopSpeed);
        TestTrue(TEXT("and was never faster"), Fastest <= TopSpeed);

        // A lower top takes the notches above it away, the lever's with them.
        Fast.DriveTop = 0.07 * Light;
        State.SetLimits(Fast);
        TestEqual(TEXT("a top of 0.07 c is 11 positions, 20,000 km/s the last"), State.GetDriveNotchCount(), Top);
        TestEqual(TEXT("and the lever comes down to it"), State.GetCommand().DriveNotch, Top - 1);
    }

    // -- The cap binds only on the path ----------------------------------------
    // A path that misses a world's floor is not touched, bit for bit.
    {
        const FFlightSurface Earth = World(At(0.02 * AU), EarthRadius);
        const FVector Off = FRotator(0.0, 1.0, 0.0).RotateVector(FVector::ForwardVector);
        FShipFlightState Near = Driving(FUniversePosition(), Off, Top, {Earth});
        FShipFlightState Alone = Driving(FUniversePosition(), Off, Top);
        for (int32 Frame = 0; Frame < 200 * 60; ++Frame)
        {
            Near.Step(1.0 / 60.0);
            Alone.Step(1.0 / 60.0);
        }
        TestTrue(TEXT("a degree off from 0.02 AU, the flight past an Earth is bit-identical to one with no Earth"),
                 Near.GetUniversePosition() == Alone.GetUniversePosition() && Near.GetVelocity() == Alone.GetVelocity());
        TestEqual(TEXT("and nothing held it"), static_cast<int32>(Near.GetHold()), static_cast<int32>(EFlightHold::Free));
    }

    // -- Aim error, and the arrival ---------------------------------------------
    // From 0.02 AU at 0.1 c the ship either reaches the floor or passes at its
    // undisturbed miss distance, and never ends farther than it started except
    // by passing. On the world, it stays along the nose the whole way down.
    // 0.1 degrees is 5,200 km off: a grazing hit on the floor sphere.
    for (const double Degrees : {0.0, 0.01, 0.1, 1.0})
    {
        const FFlightSurface Earth = World(At(0.02 * AU), EarthRadius);
        const FVector Nose = FRotator(0.0, Degrees, 0.0).RotateVector(FVector::ForwardVector);
        FShipFlightState State = Driving(FUniversePosition(), Nose, Top, {Earth});
        const FFlown Flown = Fly(State, 200.0, 1.0 / 60.0, &Earth);
        const double Miss = 0.02 * AU * FMath::Sin(FMath::DegreesToRadians(Degrees));
        const FString Case = FString::Printf(TEXT("%.2f deg off from 0.02 AU at 0.1 c"), Degrees);
        TestTrue(Case + TEXT(": never below the floor"), Flown.Least >= -1.0);
        TestTrue(Case + TEXT(": the velocity along the nose throughout, to 1e-9"), Flown.bAlongNose);
        if (Miss < Earth.FloorRadius())
        {
            // 135 s by SecondsToFloor at 0.1 c, and the ease up from STOP.
            TestTrue(FString::Printf(TEXT("%s: the nose meets the floor sphere, and the ship is on the floor within 145 s (%.1f s)"),
                                     *Case, Flown.AtFloorAfter),
                     Flown.AtFloorAfter > 0.0 && Flown.AtFloorAfter <= 145.0);
            TestTrue(Case + TEXT(": and at rest there"), State.GetSpeed() < 1.0);
        }
        else
        {
            TestTrue(FString::Printf(TEXT("%s: passes at its undisturbed miss, %.0f km up against %.0f"), *Case,
                                     (Flown.Closest - EarthRadius) / Km, (Miss - EarthRadius) / Km),
                     FMath::IsNearlyEqual(Flown.Closest / Miss, 1.0, 5e-3));
            TestEqual(Case + TEXT(": and leaves at the lever's speed"), State.GetSpeed(), TopSpeed);
        }
    }

    // From 250,000 km at 0.1 c: on the floor within 50 s (43 s by
    // SecondsToFloor, and the ease up), and never below any floor at any
    // frame chop, a two-second hitch included.
    {
        const FFlightSurface Earth = World(At(2.5e5 * Km), EarthRadius);
        for (const double Hz : {30.0, 60.0, 144.0, 0.0})
        {
            FShipFlightState State = Driving(FUniversePosition(), FVector::ForwardVector, Top, {Earth});
            const FFlown Flown = Hz > 0.0 ? Fly(State, 80.0, 1.0 / Hz, &Earth) : Fly(State, 80.0, 1.0 / 60.0, &Earth, 20.0);
            const FString Chop = Hz > 0.0 ? FString::Printf(TEXT("at %.0f Hz"), Hz) : FString(TEXT("with a 2 s hitch"));
            TestTrue(FString::Printf(TEXT("250,000 km at 0.1 c %s: on the floor within 50 s (%.1f s)"), *Chop, Flown.AtFloorAfter),
                     Flown.AtFloorAfter > 0.0 && Flown.AtFloorAfter <= 50.0);
            TestTrue(FString::Printf(TEXT("%s: never below the floor (least %.3f cm)"), *Chop, Flown.Least), Flown.Least >= -1.0);
            TestTrue(Chop + TEXT(": at rest on it"), State.GetSpeed() < 1.0);
            TestEqual(Chop + TEXT(": held there"), static_cast<int32>(State.GetHold()), static_cast<int32>(EFlightHold::AtFloor));
        }
    }

    // The same, one substep at a time: the hold says HoldingOff and then
    // AtFloor, each once, never flickering; and the finish is the braking
    // curve's, with no step in speed. The last few substeps -- under ten
    // substeps' worth of braking, 267 m/s, the last 22 m -- are the curve's
    // discrete crawl onto the floor, bounded separately.
    {
        const FFlightSurface Earth = World(At(2.5e5 * Km), EarthRadius);
        FShipFlightState State = Driving(FUniversePosition(), FVector::ForwardVector, Top, {Earth});
        TArray<EFlightHold> Sequence{EFlightHold::Free};
        const double Braking = 2.0 * ShipFlight::BrakingMargin * State.GetLimits().LinearAcceleration;
        const double KneeSpeed = Braking * State.GetLimits().HoldSeconds;
        double Previous = 0.0;
        double WorstFinish = 0.0;
        double WorstCrawl = 0.0;
        const double CrawlSpeed = 10.0 * Braking * Step;
        for (int32 Sub = 0; Sub < 80 * 120; ++Sub)
        {
            State.Step(Step);
            if (State.GetHold() != Sequence.Last())
            {
                Sequence.Add(State.GetHold());
            }
            const double Speed = State.GetSpeed();
            // On the braking curve: under the knee by a substep of braking,
            // since the stepped curve meets the hold b x Step under the
            // continuous knee, and the hold falls at 2b there.
            if (Sequence.Num() > 1 && Previous <= KneeSpeed - Braking * Step)
            {
                double& Worst = Previous > CrawlSpeed ? WorstFinish : WorstCrawl;
                Worst = FMath::Max(Worst, Previous - Speed);
            }
            Previous = Speed;
        }
        TestTrue(FString::Printf(TEXT("the hold goes Free, HoldingOff, AtFloor, once each (%d changes)"), Sequence.Num() - 1),
                 Sequence.Num() == 3 && Sequence[1] == EFlightHold::HoldingOff && Sequence[2] == EFlightHold::AtFloor);
        const double Curve = 0.5 * Braking * Step;
        TestTrue(FString::Printf(TEXT("above %.0f m/s the finish falls no faster than the braking curve: %.2f cm/s a substep against %.2f"),
                                 CrawlSpeed / 100.0, WorstFinish, Curve),
                 WorstFinish <= 1.05 * Curve);
        TestTrue(FString::Printf(TEXT("and the last 22 m's crawl is under twice it: %.2f cm/s"), WorstCrawl),
                 WorstCrawl <= 2.0 * Curve + 1e-9);
    }

    // -- Every surface, not the nearest ------------------------------------------
    // At 0.1 c past a giant toward its moon, and toward a planet while the
    // star is nearer: at every frame chop, no frame ends inside any floor
    // sphere, and the ship arrives at what its nose was on. The moon is
    // smaller than one 2 s hitch's travel at 0.1 c, 60,000 km, so only a
    // per-substep ray keeps it solid through the hitch.
    {
        const FFlightSurface Giant = World(At(1.0e12), JupiterRadius);
        const FFlightSurface Moon = World(At(1.0e12 + 4.0e10, 8.1e9), MoonRadius);
        const FFlightSurface Sun = Star(At(0.0, 2.0e11), UniverseUnits::CmPerSolarRadius);
        const FFlightSurface Planet = World(At(5.0e11), EarthRadius);
        struct FCase { const TCHAR* Name; TArray<FFlightSurface> Surfaces; const FFlightSurface* Target; };
        const FCase Cases[] = {
            {TEXT("past a giant to its moon"), {Giant, Moon}, &Moon},
            {TEXT("to a planet with the star nearer"), {Sun, Planet}, &Planet},
        };
        for (const FCase& Case : Cases)
        {
            TestTrue(FString::Printf(TEXT("%s: the start's nearest surface is not the target"), Case.Name),
                     ShipFlight::Room(Case.Surfaces, FUniversePosition()) < ShipFlight::FloorClearance(*Case.Target, FUniversePosition()));
            for (const double Hz : {30.0, 60.0, 144.0, 0.0})
            {
                FShipFlightState State = Driving(FUniversePosition(), Case.Target->Centre - FUniversePosition(), Top, Case.Surfaces);
                // 382 s to the moon and 202 s to the planet by SecondsToFloor.
                const FFlown Flown = Hz > 0.0 ? Fly(State, 420.0, 1.0 / Hz, Case.Target)
                                              : Fly(State, 420.0, 1.0 / 60.0, Case.Target, 30.0);
                const FString Chop = FString::Printf(TEXT("%s %s"), Case.Name,
                    Hz > 0.0 ? *FString::Printf(TEXT("at %.0f Hz"), Hz) : TEXT("with a 2 s hitch"));
                TestTrue(FString::Printf(TEXT("%s: inside no floor sphere (least %.3f cm)"), *Chop, Flown.Least), Flown.Least >= -1.0);
                TestTrue(FString::Printf(TEXT("%s: on the target's floor (%.1f s)"), *Chop, Flown.AtFloorAfter), Flown.AtFloorAfter > 0.0);
            }
        }
    }

    // -- Under a floor, and inside a body ---------------------------------------
    // It climbs out at the lever's speed and cannot descend.
    {
        const FFlightSurface Earth = World(FUniversePosition(), EarthRadius);
        const FUniversePosition Starts[] = {At(EarthRadius + 5.0 * Km), At(0.5 * EarthRadius)};
        const TCHAR* Wheres[] = {TEXT("under the floor"), TEXT("inside the body")};
        for (int32 Case = 0; Case < 2; ++Case)
        {
            const FUniversePosition& From = Starts[Case];
            const TCHAR* Where = Wheres[Case];
            FShipFlightState Climbing = Driving(From, FVector::ForwardVector, 3, {Earth});
            FShipFlightState Free = Driving(From, FVector::ForwardVector, 3);
            for (int32 Frame = 0; Frame < 120; ++Frame)
            {
                Climbing.Step(1.0 / 60.0);
                Free.Step(1.0 / 60.0);
            }
            TestTrue(FString::Printf(TEXT("%s, nose up, the ship climbs exactly as it would with no world there"), Where),
                     Climbing.GetUniversePosition() == Free.GetUniversePosition() && Climbing.GetSpeed() > 0.0);

            FShipFlightState Diving = Driving(From, -FVector::ForwardVector, 3, {Earth});
            for (int32 Frame = 0; Frame < 120; ++Frame)
            {
                Diving.Step(1.0 / 60.0);
            }
            TestTrue(FString::Printf(TEXT("%s, nose down, it does not descend at all"), Where),
                     Diving.GetUniversePosition() == From.Normalised() && Diving.GetSpeed() == 0.0);
            TestEqual(FString::Printf(TEXT("%s, and says it is at the floor"), Where),
                      static_cast<int32>(Diving.GetHold()), static_cast<int32>(EFlightHold::AtFloor));
        }
    }

    // -- What the cap holds, the ease follows ------------------------------------
    // In notch space the eased position never rises faster than the response
    // times the thrust -- not when a turn off the limb releases the cap at
    // 0.1 c, not when a climb does -- and falls faster in one substep only:
    // capture, the nose coming onto a world too fast to allow. The world is
    // 30,000 km off: the cap binds at 0.1 c inside 120,000 km.
    {
        const FFlightSurface Earth = World(At(3.0e9), EarthRadius);
        FShipFlightState State = Driving(At(0.0, -3.0e9 - 1.0e12), FVector::ForwardVector, Top);
        for (int32 Frame = 0; Frame < 10 * 60; ++Frame)
        {
            State.Step(1.0 / 60.0);   // 0.1 c, with nothing anywhere
        }
        TestEqual(TEXT("up to 0.1 c with nothing near"), State.GetSpeed(), TopSpeed);

        // Beside the world, the nose across it; then onto it.
        State.SetSurfaces({Earth});
        State.SetUniverseTransform(FUniversePosition(), Facing(FVector::RightVector));
        State.Step(Step);
        TestEqual(TEXT("passing across a world, the cap does nothing"), State.GetSpeed(), TopSpeed);
        State.SetUniverseTransform(State.GetUniversePosition(), Facing(Earth.Centre - State.GetUniversePosition()));

        const double Rise = State.GetLimits().DriveResponse * State.GetLimits().DriveThrust * Step;
        int32 FastFalls = 0;
        double WorstRise = 0.0;
        const auto Watch = [&](int32 Substeps)
        {
            for (int32 Sub = 0; Sub < Substeps; ++Sub)
            {
                const double Before = State.GetDrivePosition();
                State.Step(Step);
                const double Change = State.GetDrivePosition() - Before;
                WorstRise = FMath::Max(WorstRise, Change);
                FastFalls += -Change > Rise + 1e-12 ? 1 : 0;
            }
        };
        Watch(1);
        TestTrue(FString::Printf(TEXT("capture drops the speed in that substep: to %.4f c"), State.GetSpeed() / Light),
                 State.GetSpeed() < 0.5 * TopSpeed);
        TestEqual(TEXT("and holds off"), static_cast<int32>(State.GetHold()), static_cast<int32>(EFlightHold::HoldingOff));
        Watch(10 * 120);

        // A turn off the limb: the speed rises from where the ship was.
        const double Held = State.GetDrivePosition();
        State.SetUniverseTransform(State.GetUniversePosition(), Facing(FVector::UpVector));
        Watch(1);
        TestEqual(TEXT("turned off the world, the cap lets go"), static_cast<int32>(State.GetHold()), static_cast<int32>(EFlightHold::Free));
        TestTrue(TEXT("and the speed rises one ease-step from where it was held, not back to 0.1 c"),
                 State.GetDrivePosition() - Held <= Rise + 1e-12 && State.GetSpeed() < 0.5 * TopSpeed);
        Watch(2 * 120);
        TestEqual(TEXT("capture was the only fast fall"), FastFalls, 1);

        // A climb off the floor, from rest.
        FShipFlightState Grounded = Driving(At(EarthRadius + Earth.Floor - 1.0), -FVector::ForwardVector, Top,
                                            {World(FUniversePosition(), EarthRadius)});
        Grounded.Step(1.0);
        TestEqual(TEXT("on the floor, nose in, at rest"), Grounded.GetSpeed(), 0.0);
        Grounded.SetUniverseTransform(Grounded.GetUniversePosition(), Facing(FVector::ForwardVector));
        double WorstClimb = 0.0;
        for (int32 Sub = 0; Sub < 240; ++Sub)
        {
            const double Before = Grounded.GetDrivePosition();
            Grounded.Step(Step);
            WorstClimb = FMath::Max(WorstClimb, Grounded.GetDrivePosition() - Before);
        }
        TestTrue(FString::Printf(TEXT("climbing off it, never faster than the response: %.5f notches a substep against %.5f"),
                                 FMath::Max(WorstClimb, WorstRise), Rise),
                 FMath::Max(WorstClimb, WorstRise) <= Rise + 1e-12);
    }

    // -- The edge is a surface like a body ------------------------------------------
    // 0.01 AU inside it, flown out at 0.1 c: the ship settles on its floor,
    // just inside (85 s by SecondsToFloor), and never leaves its system.
    {
        FFlightSurface Edge;
        Edge.Radius = 1.0e14;
        Edge.Floor = ShipFlight::DefaultFloorCm;
        Edge.bInsideOut = true;
        FShipFlightState State = Driving(At(Edge.Radius - 0.01 * AU), FVector::ForwardVector, Top, {Edge});
        const FFlown Flown = Fly(State, 150.0, 1.0 / 60.0, &Edge);
        TestTrue(FString::Printf(TEXT("flown out at 0.1 c it never passes the edge's floor (least %.3f cm)"), Flown.Least), Flown.Least >= -1.0);
        TestTrue(TEXT("and settles on it, 10 km inside the edge"), Flown.AtFloorAfter > 0.0 && State.GetSpeed() < 1.0);
        TestEqual(TEXT("the edge holds it as a floor does"), static_cast<int32>(State.GetHold()), static_cast<int32>(EFlightHold::AtFloor));
    }

    // -- Starved boosters slow the response, never the top -------------------------
    {
        const auto SecondsTo = [&](double Thrust, int32 From, int32 To)
        {
            FShipFlightState State = Driving(FUniversePosition(), FVector::ForwardVector, From);
            FShipFlightLimits Limits = State.GetLimits();
            Limits.DriveThrust = Thrust;
            State.SetLimits(Limits);
            for (int32 Second = 0; Second < 60; ++Second)
            {
                State.Step(1.0);   // settled at From: longer than any ease
            }
            FShipFlightCommand Command = State.GetCommand();
            Command.DriveNotch = To;
            State.SetCommand(Command);
            int32 Substeps = 0;
            while (FMath::Abs(State.GetDrivePosition() - To) > 0.05 * FMath::Abs(To - From) && Substeps < 100000)
            {
                State.Step(Step);
                ++Substeps;
            }
            return Substeps * Step;
        };
        const double Tap = SecondsTo(1.0, 5, 6);
        const double StarvedTap = SecondsTo(0.25, 5, 6);
        TestTrue(FString::Printf(TEXT("a one-notch tap settles to 95%% in 1.2 s (%.3f s)"), Tap), FMath::IsNearlyEqual(Tap, 1.2, 0.02));
        TestTrue(FString::Printf(TEXT("at a quarter thrust it takes four times as long: %.3f s against %.3f"), StarvedTap, Tap),
                 FMath::IsNearlyEqual(StarvedTap / Tap, 4.0, 0.03));
        const double Climb = SecondsTo(1.0, 0, Top);
        const double StarvedClimb = SecondsTo(0.25, 0, Top);
        TestTrue(FString::Printf(TEXT("and STOP to 0.1 c does too: %.2f s against %.2f"), StarvedClimb, Climb),
                 FMath::IsNearlyEqual(StarvedClimb / Climb, 4.0, 0.03));

        FShipFlightState Starved = Driving(FUniversePosition(), FVector::ForwardVector, Top);
        FShipFlightLimits Limits = Starved.GetLimits();
        Limits.DriveThrust = 0.25;
        Starved.SetLimits(Limits);
        Starved.Step(2.0);
        Starved.Step(2.0);
        for (int32 Second = 0; Second < 60; ++Second)
        {
            Starved.Step(1.0);
        }
        TestEqual(TEXT("and gets there: the top is 0.1 c at a quarter thrust too"), Starved.GetSpeed(), TopSpeed);
    }

    // -- The drive never touches attitude --------------------------------------------
    {
        const FVector Turn(0.5, -0.3, 0.7);
        FShipFlightState Driven = Driving(FUniversePosition(), FVector::ForwardVector, Top);
        FShipFlightState Cruising;
        FShipFlightCommand Command = Driven.GetCommand();
        Command.AttitudeRate = Turn;
        Driven.SetCommand(Command);
        Cruising.SetCommand(MakeCommand(1.0, Turn));
        for (int32 Frame = 0; Frame < 180; ++Frame)
        {
            Driven.Step(1.0 / 60.0);
            Cruising.Step(1.0 / 60.0);
        }
        TestTrue(TEXT("attitude under the drive matches attitude under cruise"),
                 Driven.GetUniverseOrientation().Equals(Cruising.GetUniverseOrientation(), 1e-12));
        TestEqual(TEXT("and so does the turn rate"), Driven.GetAngularVelocity(), Cruising.GetAngularVelocity());
        TestTrue(TEXT("and turning, the velocity follows the nose"),
                 Driven.GetVelocity().GetSafeNormal().Equals(Driven.GetUniverseOrientation().GetForwardVector(), 1e-9));
    }

    // -- A lever, in both modes, across the pilot standing up -------------------------
    {
        FShipFlightState State = Driving(FUniversePosition(), FVector::ForwardVector, 7);
        FShipFlightCommand Command = State.GetCommand();
        Command.Throttle = 0.4;
        Command.AttitudeRate = FVector(0.2, 0.2, 0.2);
        State.SetCommand(Command);
        State.ReleaseAttitude();
        TestTrue(TEXT("standing up leaves the drive engaged"), State.GetCommand().bDrive);
        TestEqual(TEXT("its lever where it was"), State.GetCommand().DriveNotch, 7);
        TestEqual(TEXT("and cruise's"), State.GetCommand().Throttle, 0.4);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightSpoolDownTest,
    "DeepSpace.Ship.FlightSpoolDown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Leaving the drive is a state, not a clamp (flight-feel decision 4): each
 * row of its table. Cruise's lever is live from the press of F; the ship
 * eases down along the nose on the drive's curve to cruise's top; the cap
 * holds throughout; F again resumes; X stops both and cruise brakes to rest.
 */
bool FShipFlightSpoolDownTest::RunTest(const FString& Parameters)
{
    using namespace FlightDriveTestLocal;
    const double Step = FShipFlightState::FixedStep;
    const int32 Top = TopNotch();
    const double TopSpeed = ShipDriveLever::NotchSpeed(Top);
    const double Cruise = FShipFlightLimits::Cruise().MaxSpeed;
    const double Astern = FShipFlightLimits::Cruise().AsternSpeed;

    const auto AtLight = [&](TArray<FFlightSurface> Surfaces = {})
    {
        FShipFlightState State = Driving(FUniversePosition(), FVector::ForwardVector, Top);
        for (int32 Second = 0; Second < 10; ++Second)
        {
            State.Step(1.0);   // STOP to 0.1 c, settled: under eight seconds
        }
        State.SetSurfaces(MoveTemp(Surfaces));
        return State;
    };
    const auto Leave = [](FShipFlightState& State, double Throttle)
    {
        FShipFlightCommand Command = State.GetCommand();
        Command.bDrive = false;
        Command.Throttle = Throttle;
        State.SetCommand(Command);
    };

    // Leaving at 0.1 c: no clamp; cruise's lever live; the drive's shown dim.
    {
        const double HalfLever = ShipDriveLever::CruiseSpeed(0.5, Cruise, Astern);
        FShipFlightState State = AtLight();
        TestEqual(TEXT("at 0.1 c"), State.GetSpeed(), TopSpeed);
        Leave(State, 0.5);
        TestEqual(TEXT("F above cruise's top spools down"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::SpoolingDown));
        TestEqual(TEXT("and does not clamp"), State.GetSpeed(), TopSpeed);
        TestEqual(TEXT("cruise's lever is live: half way on its log scale, 141 m/s"), State.GetLeverSpeed(), HalfLever);
        TestEqual(TEXT("and the drive's is the other, where it was"), State.GetOtherLeverSpeed(), TopSpeed);

        // Turning all the while: the velocity stays along the nose.
        FShipFlightCommand Command = State.GetCommand();
        Command.AttitudeRate = FVector(0.0, 0.0, 1.0);
        State.SetCommand(Command);
        double Previous = State.GetSpeed();
        bool bNeverRises = true;
        bool bAlongNose = true;
        double Spooled = -1.0;
        for (int32 Sub = 1; Sub <= 20 * 120 && Spooled < 0.0; ++Sub)
        {
            State.Step(Step);
            bNeverRises &= State.GetSpeed() <= Previous;
            Previous = State.GetSpeed();
            if (State.GetMode() == EFlightMode::SpoolingDown)
            {
                bAlongNose &= State.GetVelocity().GetSafeNormal().Equals(State.GetUniverseOrientation().GetForwardVector(), 1e-9);
            }
            else
            {
                Spooled = Sub * Step;
            }
        }
        TestTrue(FString::Printf(TEXT("from 0.1 c the spool takes about 3.3 seconds (%.2f s)"), Spooled), Spooled > 3.2 && Spooled < 3.5);
        TestTrue(TEXT("never rising on the way"), bNeverRises);
        TestTrue(TEXT("along the nose while turning, to 1e-9"), bAlongNose);
        TestTrue(FString::Printf(TEXT("and it hands cruise the ship at cruise's top (%.1f m/s)"), State.GetSpeed() / 100.0),
                 State.GetSpeed() <= Cruise && State.GetSpeed() >= Cruise * 0.97);
        TestEqual(TEXT("an ordinary cruising ship"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::Cruise));
        Command = State.GetCommand();
        Command.AttitudeRate = FVector::ZeroVector;
        State.SetCommand(Command);
        for (int32 Second = 0; Second < 6; ++Second)
        {
            State.Step(2.0);   // 20 km/s to 141 m/s at 2 km/s^2: ten seconds
        }
        TestTrue(TEXT("which chases its own lever from there"), FMath::IsNearlyEqual(State.GetSpeed(), HalfLever, 1e-6));
    }

    // F again mid-spool: the drive resumes from where the spool had got to,
    // its lever where it was.
    {
        FShipFlightState State = AtLight();
        Leave(State, 1.0);
        State.Step(2.0);
        const double Position = State.GetDrivePosition();
        const double Speed = State.GetSpeed();
        TestTrue(TEXT("two seconds into the spool, well down from 0.1 c"), Speed < 0.01 * Light && Speed > Cruise);
        FShipFlightCommand Command = State.GetCommand();
        Command.bDrive = true;
        State.SetCommand(Command);
        TestEqual(TEXT("F again is the drive"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::Drive));
        TestEqual(TEXT("from the spool's own position"), State.GetDrivePosition(), Position);
        TestEqual(TEXT("its lever still at 0.1 c"), State.GetCommand().DriveNotch, Top);
        State.Step(Step);
        TestTrue(TEXT("and the speed turns back up from there, by one ease-step"),
                 State.GetSpeed() > Speed && State.GetDrivePosition() - Position <= State.GetLimits().DriveResponse * Step + 1e-12);
    }

    // X mid-spool: both to STOP, the spool carries on, and cruise brakes to
    // rest -- and stays there if F is pressed after.
    {
        FShipFlightState State = AtLight();
        Leave(State, 1.0);
        State.Step(1.0);
        FShipFlightCommand Command = State.GetCommand();
        Command.Throttle = 0.0;
        Command.DriveNotch = 0;
        State.SetCommand(Command);
        TestEqual(TEXT("after X the spool carries on"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::SpoolingDown));
        for (int32 Second = 0; Second < 20; ++Second)
        {
            State.Step(1.0);
        }
        TestEqual(TEXT("and cruise brakes to rest"), State.GetSpeed(), 0.0);
        Command = State.GetCommand();
        Command.bDrive = true;
        State.SetCommand(Command);
        State.Step(2.0);
        TestEqual(TEXT("F after X: still at rest"), State.GetSpeed(), 0.0);
    }

    // The cap holds throughout the spool: leaving the drive at 0.1 c with the
    // nose on a world 30,000 km off, inside the 120,000 km where the cap
    // binds at 0.1 c, it is captured as the drive would be.
    {
        FShipFlightState State = AtLight();
        const FFlightSurface Earth = World(State.GetUniversePosition() + FVector(3.0e9, 0.0, 0.0), EarthRadius);
        State.SetSurfaces({Earth});
        Leave(State, 1.0);
        bool bHeld = false;
        double Fraction = -1.0;
        double FractionOf = -2.0;
        double Least = TNumericLimits<double>::Max();
        for (int32 Sub = 0; Sub < 6 * 120; ++Sub)
        {
            State.Step(Step);
            const bool bHeldNow = State.GetMode() == EFlightMode::SpoolingDown && State.GetHold() == EFlightHold::HoldingOff;
            bHeld |= bHeldNow;
            if (bHeldNow)
            {
                // Against the speed the spool began from, 0.1 c, and not
                // cruise's lever: that is 20 km/s, and would clamp to 0.
                Fraction = State.GetHeldFraction();
                FractionOf = 1.0 - State.GetSpeed() / TopSpeed;
            }
            Least = FMath::Min(Least, LeastClearance(State));
        }
        TestTrue(TEXT("spooling down onto a world 30,000 km ahead, the cap holds it off"), bHeld);
        TestTrue(FString::Printf(TEXT("and the hold is measured against the 0.1 c the spool began from: %.6f against %.6f"),
                                 Fraction, FractionOf),
                 Fraction > 0.0 && FMath::IsNearlyEqual(Fraction, FractionOf, 1e-9));
        TestTrue(TEXT("and it never passes the floor"), Least >= -1.0);
    }

    // Engaging from a forward cruise starts where cruise was: no substep
    // jumps. (Astern or sideways it does -- the spec's known edge.)
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(1.0, FVector::ZeroVector));
        for (int32 Second = 0; Second < 6; ++Second)
        {
            State.Step(2.0);   // rest to 20 km/s: ten seconds
        }
        TestTrue(TEXT("cruising at 20 km/s"), FMath::IsNearlyEqual(State.GetSpeed(), Cruise, 1e-6));
        FShipFlightCommand Command = State.GetCommand();
        Command.bDrive = true;
        Command.DriveNotch = 0;
        State.SetCommand(Command);
        TestTrue(TEXT("engaged, the drive's position is the ship's forward speed"),
                 FMath::IsNearlyEqual(ShipDriveLever::SpeedAt(State.GetDrivePosition()), Cruise, 1e-6));
        State.Step(Step);
        TestTrue(FString::Printf(TEXT("and the first substep eases from there (%.1f m/s)"), State.GetSpeed() / 100.0),
                 State.GetSpeed() < Cruise && State.GetSpeed() > 0.97 * Cruise);

        // Leaving at or under cruise's top is cruise at once: nothing to spool.
        Command.bDrive = false;
        State.SetCommand(Command);
        TestEqual(TEXT("left under cruise's top, it is cruise at once"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::Cruise));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightCruiseSettlesTest,
    "DeepSpace.Ship.FlightCruiseSettles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Cruise's setpoint controller at the bottom of its log lever (the
 * 2026-09-27 ruling). The boosters change the velocity by up to 2 km/s^2 x
 * 1/120 s, 16.7 m/s a substep -- more than the whole of a 10 m/s setting --
 * so a controller that stepped by its authority would hunt around a low
 * setting for ever. It does not: it closes an error smaller than its
 * authority in one substep, exactly. From rest, from cruise's top, at full
 * and at a quarter thrust, every setting from 1 to 10 m/s is reached without
 * passing it, without turning back, and then held exactly. And a lever swept
 * up from rest is followed exactly while it is slow; rest to cruise's top is
 * ten seconds with the lever thrown, thirteen with Shift held.
 */
bool FShipFlightCruiseSettlesTest::RunTest(const FString& Parameters)
{
    const double Step = FShipFlightState::FixedStep;
    const FShipFlightLimits Rated = FShipFlightLimits::Cruise();
    const double LogSpan = FMath::Loge(Rated.MaxSpeed / ShipDriveLever::CruiseFloorCmPerSecond);

    for (const double Thrust : {1.0, 0.25})
    {
        for (const double Metres : {1.0, 2.0, 3.0, 5.0, 10.0})
        {
            // The lever position that asks for exactly this many metres a
            // second on the log scale.
            // 1 m/s is the floor itself, a hair off the detent.
            const double Position = FMath::Max(FMath::Loge(Metres * 100.0 / ShipDriveLever::CruiseFloorCmPerSecond) / LogSpan, 1.0e-9);
            for (const bool bFromAbove : {false, true})
            {
                FShipFlightState State;
                FShipFlightLimits Limits = Rated;
                Limits.LinearAcceleration *= Thrust;
                State.SetLimits(Limits);
                if (bFromAbove)
                {
                    State.SetCommand(MakeCommand(1.0, FVector::ZeroVector));
                    for (int32 Second = 0; Second < 50; ++Second)
                    {
                        State.Step(1.0);   // at a quarter thrust, forty seconds to the top
                    }
                }
                State.SetCommand(MakeCommand(Position, FVector::ZeroVector));
                const double Want = State.GetLeverSpeed();
                const FString Case = FString::Printf(TEXT("%.0f m/s at %.2f thrust, from %s"), Metres, Thrust,
                                                     bFromAbove ? TEXT("cruise's top") : TEXT("rest"));
                TestTrue(Case + TEXT(": the lever asks for it"), FMath::IsNearlyEqual(Want, Metres * 100.0, 1e-6 * Metres * 100.0));

                bool bNeverPast = true;
                bool bNeverTurns = true;
                bool bHeld = true;
                int32 SettledAt = -1;
                double Previous = State.GetSpeed();
                for (int32 Sub = 1; Sub <= 60 * 120; ++Sub)
                {
                    State.Step(Step);
                    const double Speed = State.GetSpeed();
                    bNeverPast &= bFromAbove ? Speed >= Want * (1.0 - 1e-12) : Speed <= Want * (1.0 + 1e-12);
                    bNeverTurns &= bFromAbove ? Speed <= Previous * (1.0 + 1e-12) : Speed >= Previous * (1.0 - 1e-12);
                    if (SettledAt < 0 && FMath::IsNearlyEqual(Speed, Want, 1e-9 * Want))
                    {
                        SettledAt = Sub;
                    }
                    else if (SettledAt >= 0)
                    {
                        bHeld &= FMath::IsNearlyEqual(Speed, Want, 1e-9 * Want);
                    }
                    Previous = Speed;
                }
                // From rest the setting is inside one substep's authority at
                // either thrust but a quarter's 4.2 m/s against 5 and 10;
                // from the top, 2.5 s of braking at full and 40 at a quarter.
                const double Allowed = (bFromAbove ? Rated.MaxSpeed / Limits.LinearAcceleration : Want / Limits.LinearAcceleration) + 2.0 * Step;
                TestTrue(FString::Printf(TEXT("%s: never past it"), *Case), bNeverPast);
                TestTrue(FString::Printf(TEXT("%s: never turning back -- no hunting"), *Case), bNeverTurns);
                TestTrue(FString::Printf(TEXT("%s: settled exactly on it in %.3f s, no later than the boosters allow (%.3f s)"),
                                         *Case, SettledAt * Step, Allowed),
                         SettledAt > 0 && SettledAt * Step <= Allowed);
                TestTrue(FString::Printf(TEXT("%s: and held there exactly for the rest of a minute"), *Case), bHeld);
                TestEqual(FString::Printf(TEXT("%s: reporting no acceleration once there"), *Case), State.GetLinearAcceleration().Size(), 0.0);
            }
        }
    }

    // Swept up from rest with Shift held at ds.Cruise.Sweep, at 60 Hz: while
    // the lever asks for 10 m/s or less it changes far slower than the
    // boosters can, and the ship is on it exactly, every frame.
    {
        FShipFlightState State;
        double Throttle = 0.0;
        bool bOnLever = true;
        int32 LowFrames = 0;
        double AtTop = -1.0;
        for (int32 Frame = 1; Frame <= 20 * 60 && AtTop < 0.0; ++Frame)
        {
            Throttle = ShipDriveLever::SweepCruise(Throttle, true, false, Frame == 1 ? 1 : 0, 0, 1.0 / 60.0,
                                                   ShipDriveLever::DefaultCruiseSweep, State.CruiseAsternLimit());
            FShipFlightCommand Command = State.GetCommand();
            Command.Throttle = Throttle;
            State.SetCommand(Command);
            State.Step(1.0 / 60.0);
            if (State.GetLeverSpeed() <= 1000.0)
            {
                ++LowFrames;
                bOnLever &= FMath::IsNearlyEqual(State.GetSpeed(), State.GetLeverSpeed(), 1e-9 * State.GetLeverSpeed());
            }
            if (State.GetSpeed() >= Rated.MaxSpeed * (1.0 - 1e-12))
            {
                AtTop = Frame / 60.0;
            }
        }
        TestTrue(FString::Printf(TEXT("swept up, the ship is on the lever exactly while it asks for 10 m/s or less (%d frames)"), LowFrames),
                 bOnLever && LowFrames > 30);
        AddInfo(FString::Printf(TEXT("rest to cruise's top with Shift held: %.2f s"), AtTop));
        TestTrue(FString::Printf(TEXT("and rest to cruise's top with Shift held is about 13 s (%.2f s)"), AtTop),
                 AtTop > 12.5 && AtTop < 13.5);
    }

    // The lever thrown to full from rest: 20 km/s at 2 km/s^2 is ten seconds.
    {
        FShipFlightState State;
        State.SetCommand(MakeCommand(1.0, FVector::ZeroVector));
        int32 Substeps = 0;
        while (State.GetSpeed() < Rated.MaxSpeed && Substeps < 60 * 120)
        {
            State.Step(Step);
            ++Substeps;
        }
        AddInfo(FString::Printf(TEXT("rest to cruise's top with the lever thrown: %.3f s"), Substeps * Step));
        TestTrue(FString::Printf(TEXT("rest to cruise's top with the lever thrown is ten seconds (%.3f s)"), Substeps * Step),
                 FMath::IsNearlyEqual(Substeps * Step, 10.0, 1.5 * Step));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightCruiseFloorTest,
    "DeepSpace.Ship.FlightCruiseFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Cruise obeys the floor too (flight-feel decisions 5 and 6): its target is
 * held to the cap, so it brakes to rest on a floor under its own inertia at
 * any thrust; a slide after a turn meets the hard stop and slides along it;
 * and far from anything it is exactly the cruise there always was.
 */
bool FShipFlightCruiseFloorTest::RunTest(const FString& Parameters)
{
    using namespace FlightDriveTestLocal;
    const FFlightSurface Earth = World(FUniversePosition(), EarthRadius);
    const FUniversePosition High = At(Earth.FloorRadius() + 30.0 * Km);

    for (const double Thrust : {1.0, 0.25})
    {
        FShipFlightState State;
        FShipFlightLimits Limits = State.GetLimits();
        Limits.LinearAcceleration *= Thrust;
        State.SetLimits(Limits);
        State.SetUniverseTransform(High, Facing(-FVector::ForwardVector));
        State.SetCommand(MakeCommand(1.0, FVector::ZeroVector));
        State.SetSurfaces({Earth});
        FFlown Flown;
        double Hardest = 0.0;
        for (int32 Frame = 0; Frame < 400 * 60; ++Frame)
        {
            const double Before = State.GetSpeed();
            const FFlown One = Fly(State, 1.0 / 60.0, 1.0 / 60.0, &Earth);
            Flown.Least = FMath::Min(Flown.Least, One.Least);
            if (Flown.AtFloorAfter < 0.0 && One.AtFloorAfter > 0.0)
            {
                Flown.AtFloorAfter = (Frame + 1) / 60.0;
            }
            Hardest = FMath::Max(Hardest, (Before - State.GetSpeed()) * 60.0);
        }
        const FString Case = FString::Printf(TEXT("cruise at %.2f thrust, nose on a world 30 km above its floor"), Thrust);
        TestTrue(FString::Printf(TEXT("%s: brakes within the boosters, never meeting the hard stop (%.1f against %.1f m/s^2)"),
                                 *Case, Hardest / 100.0, Limits.LinearAcceleration / 100.0),
                 Hardest <= Limits.LinearAcceleration * (1.0 + 1e-9));
        TestTrue(FString::Printf(TEXT("%s: never below the floor (least %.3f cm)"), *Case, Flown.Least), Flown.Least >= -1.0);
        TestTrue(FString::Printf(TEXT("%s: comes to rest on it (%.1f s, %.3f cm/s)"), *Case, Flown.AtFloorAfter, State.GetSpeed()),
                 Flown.AtFloorAfter > 0.0 && State.GetSpeed() < 1.0);
        TestEqual(Case + TEXT(": at the floor"), static_cast<int32>(State.GetHold()), static_cast<int32>(EFlightHold::AtFloor));
    }

    // A turn made while drifting in: the hard stop, and a slide. Brought in
    // at cruise's top with the world not yet a surface -- a floor raised in
    // play, say -- to 10 km over it, where 20 km/s cannot be shed by
    // anything short of 100 km; then the world is there, and the nose turns
    // along it. (At cruise's top the braking curve would have started 125
    // km up, and a turn made on it can stop the fall by a few metres or
    // not, which is no test of the hard stop.)
    {
        FShipFlightState State;
        State.SetUniverseTransform(At(Earth.FloorRadius() + 400.0 * Km), Facing(-FVector::ForwardVector));
        State.SetCommand(MakeCommand(1.0, FVector::ZeroVector));
        while (ShipFlight::FloorClearance(Earth, State.GetUniversePosition()) > 10.0 * Km)
        {
            State.Step(1.0 / 60.0);
        }
        State.SetSurfaces({Earth});
        TestTrue(TEXT("drifting in at cruise's top"), State.GetSpeed() > 0.99 * State.GetLimits().MaxSpeed);
        State.SetUniverseTransform(State.GetUniversePosition(), Facing(FVector::RightVector));
        double Least = TNumericLimits<double>::Max();
        for (int32 Frame = 0; Frame < 20 * 60; ++Frame)
        {
            State.Step(1.0 / 60.0);
            Least = FMath::Min(Least, ShipFlight::FloorClearance(Earth, State.GetUniversePosition()));
        }
        TestTrue(FString::Printf(TEXT("turned along the surface too late to stop, it meets the floor (least %.1f cm)"), Least),
                 Least < FShipFlightState::AtFloorCm);
        TestTrue(TEXT("and never enters it"), Least >= -1.0);
        TestTrue(TEXT("and slides on along it"), State.GetSpeed() > 0.5 * State.GetLimits().MaxSpeed
                 && (State.GetVelocity() | (State.GetUniversePosition() - Earth.Centre).GetSafeNormal()) >= -1e-6);
    }

    // Under a floor, cruise may climb and may not descend.
    {
        FShipFlightState State;
        State.SetUniverseTransform(At(EarthRadius + 5.0 * Km), Facing(-FVector::ForwardVector));
        State.SetCommand(MakeCommand(1.0, FVector::ZeroVector));
        State.SetSurfaces({Earth});
        double Lowest = State.GetUniversePosition().DistanceTo(Earth.Centre);
        for (int32 Frame = 0; Frame < 10 * 60; ++Frame)
        {
            State.Step(1.0 / 60.0);
            Lowest = FMath::Min(Lowest, State.GetUniversePosition().DistanceTo(Earth.Centre));
        }
        TestTrue(TEXT("under the floor, nose down, cruise does not descend"), Lowest >= EarthRadius + 5.0 * Km - 1e-3);
    }

    // Far from anything, cruise is exactly the cruise there always was.
    {
        const FShipFlightCommand Command = MakeCommand(1.0, FVector(0.5, -0.3, 0.7));
        FShipFlightState Near;
        FShipFlightState Without;
        Near.SetCommand(Command);
        Without.SetCommand(Command);
        Near.SetSurfaces({World(At(AU), EarthRadius)});
        Near.Step(2.03125);
        Without.Step(2.03125);
        TestTrue(TEXT("a world 1 AU ahead changes nothing about a cruise"),
                 Near.GetUniversePosition() == Without.GetUniversePosition() && Near.GetVelocity() == Without.GetVelocity());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightJumpTest,
    "DeepSpace.Ship.FlightJump",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipFlightJumpTest::RunTest(const FString& Parameters)
{
    // A jump is a translation, and the ship at rest: nothing else. Turning
    // the ship would turn the distant dome, which is the one thing a jump
    // must not move.
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
        TestTrue(TEXT("the ship was under way into it"), Velocity.Size() > 1000.0);
        TestTrue(TEXT("and arrives at rest, exactly"), State.GetVelocity().IsZero());
        TestTrue(TEXT("angular velocity is bit-identical"), State.GetAngularVelocity() == AngularVelocity);
    }

    // Every jump arrives at rest, whatever the drive was doing: a lever left
    // at 0.1 c must not fly the arrival at the star (flight-feel decision 4).
    {
        FShipFlightState State;
        FShipFlightCommand Command;
        Command.bDrive = true;
        Command.DriveNotch = 11;
        State.SetCommand(Command);
        State.Step(2.0);
        State.Step(2.0);
        State.Step(2.0);
        TestTrue(TEXT("under the drive, far past cruise"), State.GetSpeed() > 0.05 * ShipDriveLever::LightCmPerSecond);
        State.JumpTo(FUniversePosition(FInt64Vector(530000, -120000, 7), FVector::ZeroVector));
        TestTrue(TEXT("after JumpTo the velocity is exactly zero"), State.GetVelocity().IsZero());
        TestEqual(TEXT("and so is the drive's eased position"), State.GetDrivePosition(), 0.0);
        TestEqual(TEXT("the mode it went in with"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::Drive));

        // Leaving the drive and jumping mid-spool ends the spool too.
        Command.DriveNotch = 11;
        State.SetCommand(Command);
        State.Step(2.0);
        State.Step(2.0);
        State.Step(2.0);
        Command.bDrive = false;
        State.SetCommand(Command);
        TestEqual(TEXT("spooling down"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::SpoolingDown));
        State.JumpTo(FUniversePosition());
        TestEqual(TEXT("a jump mid-spool arrives in cruise, at rest"), static_cast<int32>(State.GetMode()), static_cast<int32>(EFlightMode::Cruise));
        TestTrue(TEXT("at rest"), State.GetVelocity().IsZero());
    }

    // The charge parameter defaults to today's rate, so the power tests and
    // the subsystem's existing call are unchanged.
    {
        FShipFlightState Default;
        FShipFlightState Explicit;
        Default.ChargeJumpDrive(4.5, 1.0);
        Explicit.ChargeJumpDrive(4.5, 1.0, FShipFlightState::JumpChargeSeconds);
        TestEqual(TEXT("default parameter is JumpChargeSeconds"), Default.GetJumpCharge(), Explicit.GetJumpCharge());
        TestTrue(TEXT("and today's rate: 4.5 s of 45 is a tenth"),
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
