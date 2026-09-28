#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 10: F is never refused. Pressed under a solid world's
 * drive floor, the drive does not take the ship -- the ship flies cruise and
 * the vertical lever, Shift and Ctrl move cruise, the drive's notch keeps
 * its setting and its position is held at the ship's own forward speed --
 * and the drive takes over only 500 m above the floor with the nose clear
 * of it. Once the drive flies, its own cap keeps it at or above the floor,
 * so the two modes cannot alternate.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingDriveUnderFloorTest, "DeepSpace.Ship.Landing.DriveUnderFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLandingDriveTakesOverTest, "DeepSpace.Ship.Landing.DriveTakesOverAbove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace LandingDriveLocal
{
    using namespace GroundFixtures;
    constexpr double Dt = 1.0 / 60.0;

    struct FFlight
    {
        FShipFlightState State;
        FFlightSurface World;
        FVector3d D;

        FFlight(double AglCm, const FQuat& Turn = FQuat::Identity)
        {
            const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
            World = SurfaceOver(Ground, 1.02e6 + Ground->MaxHeightCm());
            D = FVector3d(0.004, 0.003, 1.0).GetSafeNormal();
            State.SetSurfaces({ World });
            State.SetWells({ { World.Centre, 0.84 * ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius } });
            State.SetUniverseTransform(Above(World, D, AglCm), Level(D) * Turn);
        }

        void Engage(int32 Notch, double Throttle, double Vertical)
        {
            FShipFlightCommand Command = State.GetCommand();
            Command.bDrive = true;
            Command.DriveNotch = Notch;
            Command.Throttle = Throttle;
            Command.Vertical = Vertical;
            State.SetCommand(Command);
        }

        double OverFloor() const { return ShipFlight::FloorClearance(World, State.GetUniversePosition()); }
    };
}

bool FLandingDriveUnderFloorTest::RunTest(const FString& Parameters)
{
    using namespace LandingDriveLocal;
    FFlight Flight(5.0e4);
    const int32 Top = Flight.State.GetDriveNotchCount() - 1;
    Flight.Engage(Top, 0.0, 0.0);

    bool bAlwaysBelow = true;
    for (double T = 0.0; T < 10.0; T += Dt)
    {
        Flight.State.Step(Dt);
        bAlwaysBelow &= Flight.State.GetMode() == EFlightMode::DriveBelowFloor;
    }
    TestTrue(TEXT("F at 500 m: the ship says DriveBelowFloor, every substep"), bAlwaysBelow);
    TestTrue(TEXT("and with cruise at STOP it does not move -- never 0.1 c through mountains"), Flight.State.GetSpeed() < 1.0);
    TestEqual(TEXT("the drive's notch keeps its setting"), Flight.State.GetCommand().DriveNotch, Top);
    TestEqual(TEXT("the live lever is cruise's"), Flight.State.GetLeverSpeed(), 0.0);
    TestEqual(TEXT("the dim one the drive's"), Flight.State.GetOtherLeverSpeed(), ShipDriveLever::NotchSpeed(Top));
    TestTrue(TEXT("and the vertical lever is live"), Flight.State.IsVerticalLive());

    FShipFlightCommand Command = Flight.State.GetCommand();
    Command.Throttle = 1.0;
    Flight.State.SetCommand(Command);
    double Fastest = 0.0;
    double WorstOverCap = 0.0;
    double HeldWorst = 0.0;
    for (double T = 0.0; T < 30.0; T += Dt)
    {
        Flight.State.Step(Dt);
        Fastest = FMath::Max(Fastest, Flight.State.GetSpeed());
        const double Cap = ShipFlight::SkimCap(Flight.State.GetGroundAltitude().Get(0.0), ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor);
        WorstOverCap = FMath::Max(WorstOverCap, Flight.State.GetSpeed() / Cap);
        const double Forward = FMath::Max(0.0, Flight.State.GetVelocity() | Flight.State.GetUniverseOrientation().GetForwardVector());
        HeldWorst = FMath::Max(HeldWorst, FMath::Abs(ShipDriveLever::SpeedAt(Flight.State.GetDrivePosition()) - Forward));
    }
    TestTrue(FString::Printf(TEXT("cruise's lever flies it (%.1f m/s)"), Fastest / 100.0), Fastest > 1.0e3);
    TestTrue(FString::Printf(TEXT("held to the skim cap, never the drive's 0.1 c (worst %.3f of the cap)"), WorstOverCap), WorstOverCap <= 1.05);
    TestTrue(FString::Printf(TEXT("the drive's position is held at the ship's forward speed (worst %.3f cm/s)"), HeldWorst), HeldWorst < 1.0);
    TestTrue(TEXT("and no point of the hull went under the ground"), Flight.State.GetGroundLog().LeastClearance >= -1.0);

    // F again, flying under the floor: cruise at once -- there is nothing to
    // spool down from, and no dead stop.
    const double Before = Flight.State.GetSpeed();
    FShipFlightCommand Leave = Flight.State.GetCommand();
    Leave.bDrive = false;
    Flight.State.SetCommand(Leave);
    Flight.State.Step(Dt);
    TestTrue(TEXT("F again under the floor: cruise, with no spool-down"), Flight.State.GetMode() == EFlightMode::Cruise);
    TestTrue(FString::Printf(TEXT("and no dead stop (%.1f -> %.1f m/s)"), Before / 100.0, Flight.State.GetSpeed() / 100.0),
             Flight.State.GetSpeed() >= 0.9 * Before);
    return true;
}

bool FLandingDriveTakesOverTest::RunTest(const FString& Parameters)
{
    using namespace LandingDriveLocal;

    // Under the floor, nose up 20 degrees, climbing on the vertical lever
    // with the drive engaged at its first notch: once 500 m over the floor
    // with the nose clear of it, the drive takes over, once, for good.
    {
        FFlight Flight(0.0, FQuat(FVector::RightVector, FMath::DegreesToRadians(-20.0)));
        Flight.State.SetUniverseTransform(Flight.World.Centre + FVector(Flight.D) * (Flight.World.Radius + Flight.World.Floor - 2.0e5),
                                          Flight.State.GetUniverseOrientation());
        const double Below = -Flight.OverFloor();
        Flight.Engage(1, 0.0, 1.0);
        int32 Changes = 0;
        EFlightMode Was = EFlightMode::DriveBelowFloor;
        double HandedAt = -1.0;
        double SpeedBefore = 0.0;
        double SpeedAfter = 0.0;
        for (double T = 0.0; T < 90.0; T += Dt)
        {
            const double Before = Flight.State.GetSpeed();
            Flight.State.Step(Dt);
            const EFlightMode Now = Flight.State.GetMode();
            if (Now != Was)
            {
                ++Changes;
                HandedAt = Flight.OverFloor();
                SpeedBefore = Before;
                SpeedAfter = Flight.State.GetSpeed();
            }
            Was = Now;
        }
        AddInfo(FString::Printf(TEXT("started %.0f m under; handed over %.0f m above the floor"), Below / 100.0, HandedAt / 100.0));
        TestEqual(TEXT("one change of mode, DriveBelowFloor to Drive"), Changes, 1);
        TestTrue(TEXT("the drive flies it now"), Was == EFlightMode::Drive);
        TestTrue(TEXT("handed over 500 m above the floor, not before"), HandedAt >= ShipFlight::DefaultDriveHandbackCm);
        TestTrue(TEXT("from what the ship was doing: no jump in speed at the handover"),
                 SpeedAfter <= SpeedBefore + ShipDriveLever::NotchSpeed(1) * 0.1);
    }

    // 600 m over the floor with the nose 10 degrees below the horizon: the
    // drive takes the ship and brings it to AT THE FLOOR, never under, and
    // never hands it back.
    {
        FFlight Flight(0.0, FQuat(FVector::RightVector, FMath::DegreesToRadians(10.0)));
        Flight.State.SetUniverseTransform(Flight.World.Centre + FVector(Flight.D) * (Flight.World.Radius + Flight.World.Floor + 6.0e4),
                                          Flight.State.GetUniverseOrientation());
        Flight.Engage(1, 0.0, 0.0);
        bool bNeverBelow = true;
        double Lowest = TNumericLimits<double>::Max();
        for (double T = 0.0; T < 120.0; T += Dt)
        {
            Flight.State.Step(Dt);
            bNeverBelow &= Flight.State.GetMode() == EFlightMode::Drive;
            Lowest = FMath::Min(Lowest, Flight.OverFloor());
        }
        TestTrue(TEXT("above the floor the drive flies, and keeps flying"), bNeverBelow);
        TestTrue(FString::Printf(TEXT("its cap keeps it on or over the floor (lowest %.2f m)"), Lowest / 100.0), Lowest >= -1.0);
        TestTrue(TEXT("and it comes to AT THE FLOOR"), Flight.State.GetHold() == EFlightHold::AtFloor);
    }
    return true;
}

#endif
