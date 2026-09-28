#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipLanding.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 10's two floors: over a solid world the drive's floor is
 * the sphere, and cruise reads the ground and never the sphere, at every
 * altitude -- so a cruising ship descends through the drive floor to the
 * ground. And the hard stop, for the ground alone, lifts: a ship is never
 * left inside drawn rock. Sphere floors keep "never lifted".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingCruiseUnderDriveFloorTest, "DeepSpace.Ship.Landing.CruiseUnderDriveFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingHardStopTest, "DeepSpace.Ship.Landing.HardStopLiftsGroundOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LandingFloorsLocal
{
    using namespace GroundFixtures;

    /** The cruise lever that asks for Speed cm/s ahead. */
    double ThrottleFor(double Speed)
    {
        return FMath::Loge(Speed / ShipDriveLever::CruiseFloorCmPerSecond)
             / FMath::Loge(FShipFlightLimits().MaxSpeed / ShipDriveLever::CruiseFloorCmPerSecond);
    }

    /** Pitched Degrees nose down from Base (+Y pitch puts the nose down). */
    FQuat NoseDown(const FQuat& Base, double Degrees)
    {
        return Base * FQuat(FVector::RightVector, FMath::DegreesToRadians(Degrees));
    }
}

bool FLandingCruiseUnderDriveFloorTest::RunTest(const FString& Parameters)
{
    using namespace LandingFloorsLocal;
    const FGroundFieldRef Relief = ShipGround::FromRelief(FixtureParams());
    const double DriveFloor = 1.02e6 + Relief->MaxHeightCm();
    const FFlightSurface World = SurfaceOver(Relief, DriveFloor);
    const FVector3d D = FVector3d(0.01, -0.02, 1.0).GetSafeNormal();

    FShipFlightState Flight;
    Flight.SetSurfaces({ World });
    // Above the regime cruise still flies along the nose; with no regime at
    // all this is the two floors alone -- the sphere is not cruise's floor.
    FShipFlightLimits NoRegime = Flight.GetLimits();
    NoRegime.RegimeCm = 0.0;
    Flight.SetLimits(NoRegime);
    Flight.SetUniverseTransform(Above(World, D, 5.0e5), NoseDown(Level(D), 30.0));
    FShipFlightCommand Command;
    Command.Throttle = ThrottleFor(2.0e5);
    Flight.SetCommand(Command);

    bool bWentUnder = false;
    double RestFor = 0.0;
    double Seconds = 0.0;
    while (Seconds < 400.0 && RestFor < 2.0)
    {
        Flight.Step(1.0 / 60.0);
        Seconds += 1.0 / 60.0;
        bWentUnder |= Flight.GetDepthUnderDriveFloor() > 0.0;
        RestFor = Flight.GetSpeed() < 1.0 ? RestFor + 1.0 / 60.0 : 0.0;
    }
    const TOptional<double> Clear = Flight.GetFootprintClearance();
    const TOptional<double> Agl = Flight.GetGroundAltitude();
    AddInfo(FString::Printf(TEXT("at rest %.1f s in: footprint %.2f m, origin %.2f m above the ground"),
                            Seconds, Clear.Get(-1.0) / 100.0, Agl.Get(-1.0) / 100.0));
    TestTrue(TEXT("5 km up is under a solid world's drive floor"), Flight.GetDepthUnderDriveFloor() > 0.0 && bWentUnder);
    TestTrue(TEXT("cruise flies on down through it -- the sphere is not cruise's floor"), Agl.IsSet() && *Agl < 3.0e3);
    TestTrue(TEXT("and comes to rest on the ground"), RestFor >= 2.0);
    TestTrue(TEXT("with no point of the hull more than a centimetre under it"), Flight.GetGroundLog().LeastClearance >= -1.0);
    TestTrue(TEXT("and a point of it within a few metres of it"), Clear.IsSet() && *Clear <= 5.0e2);
    TestTrue(TEXT("the room is the ground's, less the gear's clearance"),
             FMath::IsNearlyEqual(Flight.GetRoom(), FMath::Max(*Agl - ShipLanding::DefaultGearClearanceCm, 0.0), 1.0e-6));
    FShipFlightState OnC;
    OnC.SetSurfaces({ World });
    OnC.SetUniverseTransform(Above(World, D, 5.0e5), Level(D));
    FShipFlightCommand Sink;
    Sink.Vertical = -1.0;
    OnC.SetCommand(Sink);
    double Rested = 0.0;
    for (double T = 0.0; T < 200.0 && Rested < 2.0; T += 1.0 / 60.0)
    {
        OnC.Step(1.0 / 60.0);
        Rested = OnC.GetSpeed() < 0.5 ? Rested + 1.0 / 60.0 : 0.0;
    }
    TestTrue(TEXT("at 5 km, C brings a cruising ship down through the drive floor to rest on the ground"),
             Rested >= 2.0 && FMath::Abs(OnC.GetFootprintClearance().Get(-1.0e9)) < 1.0 && OnC.GetGroundLog().LeastClearance >= -1.0);
    return true;
}

bool FLandingHardStopTest::RunTest(const FString& Parameters)
{
    using namespace LandingFloorsLocal;
    const double Gear = ShipLanding::DefaultGearClearanceCm;
    const FGroundFieldRef Relief = ShipGround::FromRelief(FixtureParams());
    const FFlightSurface World = SurfaceOver(Relief, 1.02e6 + Relief->MaxHeightCm());
    const FVector3d D = FVector3d(-0.03, 0.02, 1.0).GetSafeNormal();

    // Placed under the ground: one substep lifts every point out of it.
    {
        FShipFlightState Flight;
        Flight.SetSurfaces({ World });
        Flight.SetUniverseTransform(Above(World, D, -300.0), Level(D));
        Flight.Step(FShipFlightState::FixedStep);
        TestTrue(FString::Printf(TEXT("placed 3 m into the ground, lifted out in one substep (%.3f cm)"), Flight.GetFootprintClearance().Get(-1.0e9)),
                 Flight.GetFootprintClearance().Get(-1.0e9) >= -1.0);
        TestEqual(TEXT("which is the ground's hard stop firing"), Flight.GetGroundLog().HardStops, 1);
    }

    // A sphere floor, raised over a ship in play, still teleports nothing.
    {
        FFlightSurface Sphere = World;
        Sphere.Ground.Reset();
        FShipFlightState Flight;
        Flight.SetSurfaces({ Sphere });
        const FUniversePosition Under = Sphere.Centre + FVector(D) * (Sphere.Radius + Sphere.Floor - 1.0e5);
        Flight.SetUniverseTransform(Under, Level(D));
        Flight.Step(1.0);
        TestTrue(TEXT("under a sphere floor the ship is never lifted"),
                 FMath::Abs(Flight.GetUniversePosition().DistanceTo(Sphere.Centre) - Under.DistanceTo(Sphere.Centre)) < 1.0);
    }

    // A priors reload moves the ground under a hovering ship: the next
    // substep re-seats it.
    {
        FShipFlightState Flight;
        Flight.SetSurfaces({ World });
        Flight.SetUniverseTransform(Above(World, D, Gear + 20.0), Level(D));
        Flight.Step(FShipFlightState::FixedStep);
        FWorldReliefParams Taller = FixtureParams();
        Taller.PeakCm *= 2.0;
        const FGroundFieldRef Reloaded = ShipGround::FromRelief(Taller);
        Flight.SetSurfaces({ SurfaceOver(Reloaded, World.Floor + Relief->MaxHeightCm()) });
        Flight.Step(FShipFlightState::FixedStep);
        TestTrue(TEXT("a reload that raises the ground re-seats the ship on it"), Flight.GetFootprintClearance().Get(-1.0e9) >= -1.0);
    }

    // The pilot pitches a corner down at 1.5 m: the ship levers itself up
    // on its own gear, never through it, no faster than the turn lifts it.
    {
        FShipFlightState Flight;
        Flight.SetSurfaces({ World });
        Flight.SetUniverseTransform(Above(World, D, Gear + 1.0), Level(D));

        // Placed 1.5 m over the ground under the origin, the relief under a
        // foot 16 m forward can stand higher than that: one level substep
        // seats the ship on the ground as it lies before the turn begins, so
        // what is measured is the turn's lift and not the placement's.
        const double Placed = Flight.GetUniversePosition().DistanceTo(World.Centre);
        Flight.Step(FShipFlightState::FixedStep);
        AddInfo(FString::Printf(TEXT("seated by %.1f cm before the turn"), Flight.GetUniversePosition().DistanceTo(World.Centre) - Placed));

        FShipFlightCommand Command;
        Command.AttitudeRate = FVector(0.0, 1.0, 0.0);
        Flight.SetCommand(Command);
        const double Reach = ShipLanding::ReachCm(Gear);
        double WorstRise = 0.0;
        double Least = TNumericLimits<double>::Max();
        double Previous = Flight.GetUniversePosition().DistanceTo(World.Centre);
        for (int32 Sub = 0; Sub < 3 * 120; ++Sub)
        {
            Flight.Step(FShipFlightState::FixedStep);
            const double Now = Flight.GetUniversePosition().DistanceTo(World.Centre);
            WorstRise = FMath::Max(WorstRise, (Now - Previous) / FShipFlightState::FixedStep);
            Previous = Now;
            Least = FMath::Min(Least, Flight.GetFootprintClearance().Get(-1.0e9));
        }
        const double TurnLift = Flight.GetLimits().MaxAngularRate.Y * Reach;
        TestTrue(FString::Printf(TEXT("pitching down at 1.5 m, no point goes under (least %.3f cm)"), Least), Least >= -1.0);
        TestTrue(FString::Printf(TEXT("and the ship rises no faster than its turn lifts the hull (%.1f of %.1f cm/s)"), WorstRise, TurnLift),
                 WorstRise <= 1.05 * TurnLift + 5.0);
    }
    return true;
}

#endif
