#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipVerticalLever.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 8 in the flight: within 50 km of a world's cruise floor
 * the vertical lever is live, cruise flies the nose's horizontal projection,
 * and across 40-50 km both blend out; decision 5's climb top and starved
 * sink; decision 10's skim cap. Sign-off item 5 is RegimeTop.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingRegimeTopTest, "DeepSpace.Ship.Landing.RegimeTop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingHoverAndSinkTest, "DeepSpace.Ship.Landing.HoverAndSink",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingSkimAndLookTest, "DeepSpace.Ship.Landing.SkimAndLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LandingRegimeLocal
{
    using namespace GroundFixtures;
    constexpr double Dt = 1.0 / 60.0;

    double ThrottleFor(double Speed)
    {
        return FMath::Loge(Speed / ShipDriveLever::CruiseFloorCmPerSecond)
             / FMath::Loge(FShipFlightLimits().MaxSpeed / ShipDriveLever::CruiseFloorCmPerSecond);
    }

    /** A world near-flat enough that the ground's caps never bind by accident:
     *  metre-high swells a kilometre long. */
    FGroundFieldRef Swells(double Radius)
    {
        return MakeShared<FCrossedSines, ESPMode::ThreadSafe>(Radius, 100.0, 1.0e5);
    }

    FGravityWell WellOf(const FFlightSurface& World, double G)
    {
        return { World.Centre, G * ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius };
    }

    struct FFlight
    {
        FShipFlightState State;
        FFlightSurface World;
        FVector3d D;

        FFlight(const FGroundFieldRef& Ground, double AglCm, double G, const FQuat& Turn = FQuat::Identity)
        {
            World = SurfaceOver(Ground, 1.02e6 + Ground->MaxHeightCm());
            D = FVector3d(0.002, -0.001, 1.0).GetSafeNormal();
            State.SetSurfaces({ World });
            State.SetWells({ WellOf(World, G) });
            State.SetUniverseTransform(Above(World, D, AglCm), Level(D) * Turn);
        }

        void Command(double Throttle, double Vertical)
        {
            FShipFlightCommand Command = State.GetCommand();
            Command.Throttle = Throttle;
            Command.Vertical = Vertical;
            State.SetCommand(Command);
        }

        double Agl() const { return State.GetGroundAltitude().Get(-1.0); }

        FVector Up() const { return (State.GetUniversePosition() - World.Centre).GetSafeNormal(); }

        double Horizontal() const
        {
            const FVector V = State.GetVelocity();
            return (V - Up() * (V | Up())).Size();
        }
    };
}

bool FLandingRegimeTopTest::RunTest(const FString& Parameters)
{
    using namespace LandingRegimeLocal;
    const double R = 0.9 * UniverseUnits::CmPerEarthRadius;
    const double Gear = ShipLanding::DefaultGearClearanceCm;

    // A climb at 200 m/s from 30 km with cruise at STOP comes to rest in the
    // band, under 50 km, and never reverses.
    {
        FFlight Flight(Swells(R), 3.0e6, 0.84);
        Flight.Command(0.0, 1.0);
        double Highest = 0.0;
        double WorstReverse = 0.0;
        for (double T = 0.0; T < 600.0; T += Dt)
        {
            Flight.State.Step(Dt);
            Highest = FMath::Max(Highest, Flight.Agl());
            WorstReverse = FMath::Min(WorstReverse, Flight.State.GetVerticalSpeed());
        }
        AddInfo(FString::Printf(TEXT("the climb came to %.2f km, %.3f m/s"), Highest / 1.0e5, Flight.State.GetVerticalSpeed() / 100.0));
        TestTrue(TEXT("a vertical climb from 30 km never passes the regime's top"), Highest - Gear < ShipFlight::DefaultRegimeCm);
        TestTrue(TEXT("and comes to rest in the band, above 40 km"), Highest - Gear > 0.8 * ShipFlight::DefaultRegimeCm
                 && FMath::Abs(Flight.State.GetVerticalSpeed()) < 50.0);
        TestTrue(TEXT("and never reverses"), WorstReverse > -1.0);
        TestTrue(TEXT("the lever is still asking: the ship is above the ground's reach"), Flight.State.GetRegimeWeight() < 0.05);
    }

    // With cruise set nose-up the ship passes through 50 km with no step in
    // its velocity: nothing changes by more than the boosters can.
    {
        FFlight Flight(Swells(R), 4.5e6, 0.84, FQuat(FVector::RightVector, FMath::DegreesToRadians(-30.0)));
        Flight.Command(ThrottleFor(2.0e5), 1.0);
        FVector Previous = Flight.State.GetVelocity();
        double WorstStep = 0.0;
        bool bPassed = false;
        for (double T = 0.0; T < 120.0; T += Dt)
        {
            Flight.State.Step(Dt);
            // From rest the boosters are saturated for the first second
            // whatever the regime does; what is measured is the way through
            // its top, some ten seconds on.
            if (T >= 3.0)
            {
                WorstStep = FMath::Max(WorstStep, (Flight.State.GetVelocity() - Previous).Size() / (Flight.State.GetLimits().LinearAcceleration * Dt));
            }
            Previous = Flight.State.GetVelocity();
            bPassed |= Flight.Agl() > 1.2 * ShipFlight::DefaultRegimeCm;
        }
        TestTrue(TEXT("cruise nose-up climbs out through the regime's top"), bPassed);
        // The chase can never step the velocity, so what a hard line would
        // show is the boosters saturated for a moment as the target jumps;
        // the blend keeps the demand to a fraction of them.
        TestTrue(FString::Printf(TEXT("with no velocity step on the way (worst %.3f of the boosters)"), WorstStep), WorstStep <= 0.5);
    }

    // A sink asked above the regime with cruise at STOP moves nothing.
    {
        FFlight Flight(Swells(R), 6.0e6, 0.84);
        Flight.Command(0.0, -1.0);
        for (double T = 0.0; T < 10.0; T += Dt)
        {
            Flight.State.Step(Dt);
        }
        TestFalse(TEXT("60 km up is outside the regime"), Flight.State.IsInNearRegime());
        TestTrue(TEXT("and there the vertical lever moves nothing"), Flight.State.GetSpeed() < 1.0 && !Flight.State.IsVerticalLive());
    }

    // Hysteresis: enters under 50 km, leaves over 55 km.
    {
        FFlight Flight(Swells(R), 5.2e6 + Gear, 0.84);
        Flight.State.Step(Dt);
        TestFalse(TEXT("52 km, coming from above: not in"), Flight.State.IsInNearRegime());
        Flight.State.SetUniverseTransform(Above(Flight.World, Flight.D, 4.9e6 + Gear), Level(Flight.D));
        Flight.State.Step(Dt);
        TestTrue(TEXT("49 km: in"), Flight.State.IsInNearRegime());
        Flight.State.SetUniverseTransform(Above(Flight.World, Flight.D, 5.2e6 + Gear), Level(Flight.D));
        Flight.State.Step(Dt);
        TestTrue(TEXT("back to 52 km: still in"), Flight.State.IsInNearRegime());
        Flight.State.SetUniverseTransform(Above(Flight.World, Flight.D, 5.6e6 + Gear), Level(Flight.D));
        Flight.State.Step(Dt);
        TestFalse(TEXT("56 km: out"), Flight.State.IsInNearRegime());
    }
    return true;
}

bool FLandingHoverAndSinkTest::RunTest(const FString& Parameters)
{
    using namespace LandingRegimeLocal;
    const double R = 1.39 * UniverseUnits::CmPerEarthRadius;

    // Hover holds, anywhere, beside a 3 g world: gravity is held, not flown.
    {
        FFlight Flight(Swells(R), 1.0e5, 3.0);
        Flight.Command(0.0, 0.0);
        const FUniversePosition Start = Flight.State.GetUniversePosition();
        for (double T = 0.0; T < 600.0; T += 0.5)
        {
            Flight.State.Step(0.5);
        }
        TestTrue(FString::Printf(TEXT("ten minutes of HOVER beside 3 g moves it %.4f cm"), Flight.State.GetUniversePosition().DistanceTo(Start)),
                 Flight.State.GetUniversePosition().DistanceTo(Start) < 1.0);
    }

    // The climb top falls on heavy worlds, by gravity alone.
    {
        FFlight Flight(Swells(R), 2.0e5, 3.3);
        Flight.Command(0.0, 1.0);
        double Fastest = 0.0;
        for (double T = 0.0; T < 60.0; T += Dt)
        {
            Flight.State.Step(Dt);
            Fastest = FMath::Max(Fastest, Flight.State.GetVerticalSpeed());
        }
        const double Top = 2.0e4 / 3.3;
        TestTrue(FString::Printf(TEXT("on a 3.3 g world the climb tops out at 61 m/s (%.2f)"), Fastest / 100.0),
                 FMath::IsNearlyEqual(Fastest, Top, 0.01 * Top));
        // The lever reads the climb top of the pull where the ship is, a
        // little under 3.3 g after a minute's climb.
        const double TopHere = ShipVerticalLever::ClimbTop(ShipVerticalLever::DefaultTopCmPerSecond, Flight.State.GetLocalGravity().Size(),
                                                           ShipVerticalLever::DefaultHeavyFloor);
        TestTrue(FString::Printf(TEXT("and the lever reads it so (%.3f of %.3f m/s)"), Flight.State.GetVerticalLeverRate() / 100.0, TopHere / 100.0),
                 FMath::IsNearlyEqual(Flight.State.GetVerticalLeverRate(), TopHere, 1e-6) && FMath::IsNearlyEqual(TopHere, Top, 0.01 * Top));
    }

    // The starved sink: only under the floor, only at HOVER or sinking,
    // eased to rest on the ground.
    {
        FFlight Flight(Swells(R), 3.0e4, 1.0);
        FShipFlightLimits Limits = Flight.State.GetLimits();
        Limits.SinkBias = 200.0;
        Flight.State.SetLimits(Limits);
        Flight.Command(0.0, 0.0);
        double Sinking = 0.0;
        double RestFor = 0.0;
        double T = 0.0;
        for (; T < 600.0 && RestFor < 2.0; T += Dt)
        {
            Flight.State.Step(Dt);
            Sinking = FMath::Min(Sinking, Flight.State.GetVerticalSpeed());
            RestFor = Flight.State.GetSpeed() < 0.5 ? RestFor + Dt : 0.0;
        }
        TestTrue(FString::Printf(TEXT("starved at HOVER under the floor it sinks at 2 m/s (%.2f)"), -Sinking / 100.0),
                 FMath::IsNearlyEqual(-Sinking, 200.0, 2.0));
        TestTrue(TEXT("and the ground catches it: at rest, a foot on it"), RestFor >= 2.0
                 && FMath::Abs(Flight.State.GetFootprintClearance().Get(-1.0e9)) < 1.0);
        TestTrue(TEXT("never more than a centimetre in, never faster than 0.5 m/s at contact"),
                 Flight.State.GetGroundLog().LeastClearance >= -1.0 && Flight.State.GetGroundLog().WorstContactSpeed <= ShipFlight::DefaultTouchdownSpeed + 1e-6);

        Flight.Command(0.0, ShipVerticalLever::LeverOf(100.0, ShipVerticalLever::DefaultTopCmPerSecond));
        for (double Up = 0.0; Up < 5.0; Up += Dt)
        {
            Flight.State.Step(Dt);
        }
        TestTrue(TEXT("a starved ship asked to climb climbs: the sink is never applied to a climb"), Flight.State.GetVerticalSpeed() > 90.0);
    }
    {
        FFlight Flight(Swells(R), 0.0, 1.0);
        Flight.State.SetUniverseTransform(Flight.World.Centre + FVector(Flight.D) * (Flight.World.Radius + Flight.World.Floor + 1.0e5), Level(Flight.D));
        FShipFlightLimits Limits = Flight.State.GetLimits();
        Limits.SinkBias = 200.0;
        Flight.State.SetLimits(Limits);
        Flight.Command(0.0, 0.0);
        const FUniversePosition Start = Flight.State.GetUniversePosition();
        for (double T = 0.0; T < 60.0; T += 0.5)
        {
            Flight.State.Step(0.5);
        }
        TestTrue(TEXT("above the drive floor, starved, nothing sinks"), Flight.State.GetUniversePosition().DistanceTo(Start) < 1.0);
    }
    return true;
}

bool FLandingSkimAndLookTest::RunTest(const FString& Parameters)
{
    using namespace LandingRegimeLocal;
    const double R = 0.9 * UniverseUnits::CmPerEarthRadius;

    // 500 m up, cruise at full: the skim cap holds it to AGL / 2.5 s.
    {
        FFlight Flight(Swells(R), 5.0e4, 0.84);
        Flight.Command(1.0, 0.0);
        for (double T = 0.0; T < 30.0; T += Dt)
        {
            Flight.State.Step(Dt);
        }
        const double Cap = ShipFlight::SkimCap(Flight.Agl(), ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor);
        TestTrue(FString::Printf(TEXT("at 500 m full cruise skims at 200 m/s (%.1f of %.1f m/s)"), Flight.Horizontal() / 100.0, Cap / 100.0),
                 FMath::IsNearlyEqual(Flight.Horizontal(), Cap, 0.02 * Cap));
        TestTrue(TEXT("and says so: HOLDING OFF"), Flight.State.GetHold() == EFlightHold::HoldingOff);
    }

    // Looking down no longer dives: nose 60 degrees down, 50 m/s, HOVER.
    {
        FFlight Flight(Swells(R), 2.0e5, 0.84, FQuat(FVector::RightVector, FMath::DegreesToRadians(60.0)));
        Flight.Command(ThrottleFor(5.0e3), 0.0);
        const double Start = Flight.Agl();
        for (double T = 0.0; T < 20.0; T += Dt)
        {
            Flight.State.Step(Dt);
        }
        TestTrue(FString::Printf(TEXT("nose down at HOVER, the height holds (moved %.2f m)"), (Flight.Agl() - Start) / 100.0),
                 FMath::Abs(Flight.Agl() - Start) < 200.0);
        TestTrue(TEXT("and the ship goes where it points, in plan, at the lever's speed"),
                 FMath::IsNearlyEqual(Flight.Horizontal(), 5.0e3, 50.0));
    }
    return true;
}

#endif
