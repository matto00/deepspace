#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipVerticalLever.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * DeepSpace.Ship.Landing.GroundAlwaysCatches (decision 10's invariant), in
 * four siblings by where the flights start:
 *   - no footprint point ends a substep more than 1 cm under the ground, at
 *     every frame chop;
 *   - contacts reached by the levers are at or under TouchdownSpeed, along
 *     the ground's normal under the lowest point, the substep it first comes
 *     within 1 cm;
 *   - in the lever sweep the hard stop never fires: the caps foresee every
 *     contact. (Attitude near the ground, in .Rough, may lift; it may never
 *     leave a point under.)
 * The fixture ground is two crossed sines twice as steep as the real
 * relief's steepest measured slope (Fixture); the real-relief pass flies
 * Baemsekai IV and III.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundCatchesHighTest, "DeepSpace.Ship.Landing.GroundAlwaysCatchesHigh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundCatchesLowTest, "DeepSpace.Ship.Landing.GroundAlwaysCatchesLow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundCatchesRoughTest, "DeepSpace.Ship.Landing.GroundAlwaysCatchesRough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundCatchesRealTest, "DeepSpace.Ship.Landing.GroundAlwaysCatchesRealRelief",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundRayOnceAFrameTest, "DeepSpace.Ship.Landing.GroundRayOnceAFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundRayAcrossTest, "DeepSpace.Ship.Landing.GroundRayMovedAcross",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GroundCatchesLocal
{
    using namespace GroundFixtures;

    const TArray<double> CruiseSpeeds = { 0.0, 1.0e2, 2.0e4, 2.0e5, 2.0e6, -2.0e4 };
    const TArray<double> VerticalRates = { 0.0, -50.0, -500.0, -2.0e4, 2.0e4 };
    const TArray<double> Chops = { 1.0 / 240.0, 1.0 / 60.0, 1.0 / 20.0, 0.5, 2.0 };
    const TArray<double> Thrusts = { 1.0, 0.25 };
    const TArray<double> Sinks = { 0.0, 200.0 };

    double ThrottleFor(double Speed, const FShipFlightLimits& Limits)
    {
        if (Speed == 0.0)
        {
            return 0.0;
        }
        const double Lever = FMath::Loge(FMath::Max(FMath::Abs(Speed), ShipDriveLever::CruiseFloorCmPerSecond) / ShipDriveLever::CruiseFloorCmPerSecond)
                           / FMath::Loge(Limits.MaxSpeed / ShipDriveLever::CruiseFloorCmPerSecond);
        return Speed > 0.0 ? FMath::Max(Lever, 1.0e-9) : -FMath::Max(Lever, 1.0e-9);
    }

    struct FStart
    {
        const TCHAR* Name;
        FVector3d D;
        double Clearance;      // footprint clearance, cm; negative: at the drive floor sphere
    };

    struct FTally
    {
        int32 Flights = 0;
        double LeastClearance = TNumericLimits<double>::Max();
        double WorstContact = 0.0;
        int32 HardStops = 0;
        FString WorstCase;
    };

    /** Places the ship so its footprint clears the ground by Clearance: two
     *  passes, since tilting ground moves which point is lowest. */
    void PlaceOver(FShipFlightState& Flight, const FFlightSurface& World, const FStart& Start)
    {
        const FQuat Turn = Level(Start.D);
        if (Start.Clearance < 0.0)
        {
            Flight.SetUniverseTransform(World.Centre + FVector(Start.D) * (World.Radius + World.Floor), Turn);
            return;
        }
        double Agl = Start.Clearance + ShipLanding::DefaultGearClearanceCm;
        for (int32 Pass = 0; Pass < 3; ++Pass)
        {
            Flight.SetUniverseTransform(Above(World, Start.D, Agl), Turn);
            Agl += Start.Clearance - ShipLanding::FootprintClearance(World, Flight.GetUniversePosition(), Turn,
                                                                     ShipLanding::DefaultGearClearanceCm).Least;
        }
    }

    void Fly(FTally& Tally, const FFlightSurface& World, double G, const FStart& Start, double Cruise, double Vertical,
             double Chop, double Thrust, double Sink, double Budget)
    {
        FShipFlightState Flight;
        FShipFlightLimits Limits = Flight.GetLimits();
        Limits.LinearAcceleration = FShipFlightLimits().LinearAcceleration * Thrust;
        Limits.DriveThrust = Thrust;
        Limits.SinkBias = Sink;
        Flight.SetLimits(Limits);
        Flight.SetSurfaces({ World });
        Flight.SetWells({ { World.Centre, G * ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius } });
        PlaceOver(Flight, World, Start);
        FShipFlightCommand Command;
        Command.Throttle = ThrottleFor(Cruise, Limits);
        Command.Vertical = ShipVerticalLever::LeverOf(Vertical, Limits.VerticalTop);
        Flight.SetCommand(Command);
        Flight.ResetGroundLog();
        double RestFor = 0.0;
        for (double T = 0.0; T < Budget && RestFor < 1.0; T += Chop)
        {
            Flight.Step(Chop);
            RestFor = Flight.GetSpeed() < 1.0 ? RestFor + Chop : 0.0;
        }
        const FGroundLog& Log = Flight.GetGroundLog();
        ++Tally.Flights;
        Tally.HardStops += Log.HardStops;
        if (Log.LeastClearance < Tally.LeastClearance || Log.WorstContactSpeed > Tally.WorstContact || (Log.HardStops > 0 && Tally.HardStops == Log.HardStops))
        {
            Tally.WorstCase = FString::Printf(TEXT("%s cruise %.0f vertical %.0f chop %.4f thrust %.2f sink %.0f"),
                                              Start.Name, Cruise, Vertical, Chop, Thrust, Sink);
        }
        Tally.LeastClearance = FMath::Min(Tally.LeastClearance, Log.LeastClearance);
        Tally.WorstContact = FMath::Max(Tally.WorstContact, Log.WorstContactSpeed);
    }

    /** The steepest the real relief actually is, cm per cm, over 20,000
     *  directions. MaxSlope is a proven bound, and since S_max was ruled a
     *  measured maximum it is about fifteen times the truth (7.9 against 0.51
     *  here): sines at it would be 83-degree cliffs 3.6 km high, a ground no
     *  world has. */
    double SteepestMeasured(const IGroundField& Ground)
    {
        FRandomStream Stream(20260928);
        double Most = 0.0;
        for (int32 Sample = 0; Sample < 20000; ++Sample)
        {
            const FVector3d D(Stream.GetUnitVector());
            FVector3d Grad;
            Ground.HeightAndGradient(D, Grad, 0.0);
            Most = FMath::Max(Most, (Grad - FVector3d::DotProduct(Grad, D) * D).Size() / Ground.RadiusCm());
        }
        return Most;
    }

    /** The fixture ground: crossed sines twice as steep as the steepest the
     *  real relief measures -- 45 degrees at their steepest. */
    FFlightSurface Fixture()
    {
        const FGroundFieldRef Real = ShipGround::FromRelief(FixtureParams());
        const FGroundFieldRef Sines = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(
            FCrossedSines::WithSlope(Real->RadiusCm(), 2.0e5, 2.0 * SteepestMeasured(*Real)));
        return SurfaceOver(Sines, 1.02e6 + Sines->MaxHeightCm());
    }

    /** Directions on the fixture: flat-ish (a trough-to-crest midpoint), a
     *  15-degree slope, a peak (a crater rim's crest, in this field), and a
     *  ridge line. The field's arc coordinates are R D.x and R D.y. */
    FVector3d AtArc(const FFlightSurface& World, double X, double Y)
    {
        return FVector3d(X / World.Radius, Y / World.Radius, 1.0).GetSafeNormal();
    }

    void Sweep(FTally& Tally, const FFlightSurface& World, TConstArrayView<FStart> Starts, double Budget)
    {
        for (const FStart& Start : Starts)
            for (const double Cruise : CruiseSpeeds)
                for (const double Vertical : VerticalRates)
                    for (const double Chop : Chops)
                        for (const double Thrust : Thrusts)
                            for (const double Sink : Sinks)
                            {
                                Fly(Tally, World, 0.84, Start, Cruise, Vertical, Chop, Thrust, Sink, Budget);
                            }
    }

    bool Report(FAutomationTestBase& Test, const FTally& Tally, double Seconds, bool bLeverSweep)
    {
        Test.AddInfo(FString::Printf(TEXT("%d flights in %.1f s: least clearance %.3f cm, worst contact %.3f cm/s, %d hard stops; worst: %s"),
                                     Tally.Flights, Seconds, Tally.LeastClearance, Tally.WorstContact, Tally.HardStops, *Tally.WorstCase));
        Test.TestTrue(TEXT("no footprint point ends a substep more than 1 cm under the ground"), Tally.LeastClearance >= -1.0);
        Test.TestTrue(TEXT("every contact the levers reach is at or under the touchdown speed"),
                      Tally.WorstContact <= ShipFlight::DefaultTouchdownSpeed * (1.0 + 1e-6));
        if (bLeverSweep)
        {
            Test.TestEqual(TEXT("and the hard stop never fires: the caps foresee every contact"), Tally.HardStops, 0);
        }
        return true;
    }
}

bool FGroundCatchesHighTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const FFlightSurface World = Fixture();
    const FVector3d D = AtArc(World, 3.1e4, -1.7e4);
    const FStart Starts[] = { { TEXT("drive floor"), D, -1.0 }, { TEXT("5 km"), D, 5.0e5 }, { TEXT("500 m"), D, 5.0e4 } };
    FTally Tally;
    const double Start = FPlatformTime::Seconds();
    Sweep(Tally, World, Starts, 150.0);
    return Report(*this, Tally, FPlatformTime::Seconds() - Start, true);
}

bool FGroundCatchesLowTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const FFlightSurface World = Fixture();
    const FVector3d D = AtArc(World, 3.1e4, -1.7e4);
    const FStart Starts[] = { { TEXT("20 m"), D, 2.0e3 }, { TEXT("2 m"), D, 2.0e2 } };
    FTally Tally;
    const double Start = FPlatformTime::Seconds();
    Sweep(Tally, World, Starts, 150.0);
    return Report(*this, Tally, FPlatformTime::Seconds() - Start, true);
}

bool FGroundCatchesRoughTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const FFlightSurface World = Fixture();
    const double Wave = 2.0e5;
    const double K = 2.0 * UE_DOUBLE_PI / Wave;
    // Slope: the y crest (cos = 0) and x where the x slope alone is 15 degrees.
    const double Amplitude = World.Ground->MaxHeightCm();
    const double SlopeX = FMath::Acos(FMath::Clamp(FMath::Tan(FMath::DegreesToRadians(15.0)) / (0.5 * Amplitude * K), -1.0, 1.0)) / K;
    const FStart Starts[] = {
        { TEXT("2 m over a 15-degree slope"), AtArc(World, SlopeX, 0.25 * Wave), 2.0e2 },
        { TEXT("2 m over a crest (the rim)"), AtArc(World, 0.25 * Wave, 0.25 * Wave), 2.0e2 },
        { TEXT("2 m over a ridge"), AtArc(World, 0.25 * Wave, 0.0), 2.0e2 } };
    FTally Tally;
    const double Start = FPlatformTime::Seconds();
    Sweep(Tally, World, Starts, 150.0);
    Report(*this, Tally, FPlatformTime::Seconds() - Start, true);

    // Attitude inputs held at hover heights of 0-8 m, on every footprint
    // point: the hard stop may lift; no point may end a substep under.
    FTally Turning;
    for (const FStart& Where : Starts)
    {
        for (const double Clear : { 0.0, 2.0e2, 4.0e2, 8.0e2 })
        {
            for (const FVector& Attitude : { FVector(0, 1, 0), FVector(0, -1, 0), FVector(1, 0, 0), FVector(-1, 0, 0) })
            {
                FShipFlightState Flight;
                Flight.SetSurfaces({ World });
                PlaceOver(Flight, World, { Where.Name, Where.D, Clear });
                FShipFlightCommand Command;
                Command.AttitudeRate = Attitude;
                Flight.SetCommand(Command);
                Flight.ResetGroundLog();
                for (double T = 0.0; T < 5.0; T += 1.0 / 60.0)
                {
                    Flight.Step(1.0 / 60.0);
                }
                ++Turning.Flights;
                Turning.LeastClearance = FMath::Min(Turning.LeastClearance, Flight.GetGroundLog().LeastClearance);
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d turning hovers: least clearance %.3f cm"), Turning.Flights, Turning.LeastClearance));
    TestTrue(TEXT("turning near the ground, the ship levers itself up on its gear, never through it"), Turning.LeastClearance >= -1.0);
    return true;
}

bool FGroundCatchesRealTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    FTally Tally;
    const double Start = FPlatformTime::Seconds();
    for (const int32 Orbit : { 4, 3 })
    {
        const TOptional<FHomeWorld> Home = HomeWorld(Orbit);
        const TCHAR* Want = Orbit == 4 ? TEXT("Baemsekai IV") : TEXT("Baemsekai III");
        if (!TestTrue(FString::Printf(TEXT("home's orbit %d is %s, solid"), Orbit, Want), Home.IsSet() && Home->Name == Want))
        {
            return false;
        }
        const FFlightSurface World = SurfaceOver(Home->Ground, 1.02e6 + Home->Ground->MaxHeightCm());
        const double G = Home->Body.GravParam / FMath::Square(World.Radius) / ShipFlight::StandardGravityCmS2;
        int32 Case = 0;
        for (int32 C = 0; C < CruiseSpeeds.Num(); ++C)
        {
            for (int32 V = 0; V < VerticalRates.Num(); ++V, ++Case)
            {
                const FVector3d D = FVector3d(FMath::Sin(Case * 0.7), FMath::Cos(Case * 1.3), 0.5 + 0.1 * (Case % 5)).GetSafeNormal();
                const double Clears[] = { 5.0e4, 2.0e3, 2.0e2 };
                const FStart Where{ TEXT("real"), D, Clears[Case % 3] };
                Fly(Tally, World, G, Where, CruiseSpeeds[C], VerticalRates[V], Chops[Case % Chops.Num()],
                    Thrusts[(Case / 2) % 2], Sinks[Case % 2], 60.0);
            }
        }
    }
    return Report(*this, Tally, FPlatformTime::Seconds() - Start, true);
}

/*
 * The ray marched once a frame is invisible: a frame of 240 substeps flies
 * exactly as 240 frames of one, while cruise brakes down its ground ray
 * above the regime, nose straight down at 20 km/s. Only the frame's first
 * substep marches; the rest take what was flown along the ray off its hit.
 */
bool FGroundRayOnceAFrameTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
    const FFlightSurface World = SurfaceOver(Ground, 1.02e6 + Ground->MaxHeightCm());
    const FVector3d D = FVector3d(0.2, -0.1, 1.0).GetSafeNormal();
    const FQuat NoseDown = FRotationMatrix::MakeFromXZ(-FVector(D), FVector(1.0, 0.0, 0.0) - FVector(D) * D.X).ToQuat();

    auto Start = [&](FShipFlightState& Flight)
    {
        Flight.SetSurfaces({ World });
        Flight.SetUniverseTransform(Above(World, D, 4.0e7), NoseDown);
        FShipFlightCommand Command;
        Command.Throttle = 1.0;
        Flight.SetCommand(Command);
    };

    // Fine frames until cruise has been braking on the ray for a quarter
    // second, and still far above the regime.
    FShipFlightState Fine;
    Start(Fine);
    int32 Braking = -1;
    int32 Steps = 0;
    for (; Steps < 120 * 120 && (Braking < 0 || Steps < Braking + 30); ++Steps)
    {
        Fine.Step(FShipFlightState::FixedStep);
        if (Braking < 0 && Fine.GetHold() != EFlightHold::Free)
        {
            Braking = Steps;
        }
    }
    if (!TestTrue(TEXT("cruise brakes on the ground ray"), Braking >= 0))
    {
        return false;
    }
    TestTrue(TEXT("far above the regime"), Fine.GetGroundAltitude().Get(0.0) > 1.0e7 && Fine.GetRegimeWeight() == 0.0);

    FShipFlightState Coarse;
    Start(Coarse);
    for (int32 I = 0; I < Steps; ++I)
    {
        Coarse.Step(FShipFlightState::FixedStep);
    }
    const double SpeedBefore = Coarse.GetSpeed();
    Coarse.Step(240.0 * FShipFlightState::FixedStep);
    for (int32 I = 0; I < 240; ++I)
    {
        Fine.Step(FShipFlightState::FixedStep);
    }
    AddInfo(FString::Printf(TEXT("braking from %.0f m/s: fine %.3f m/s, one frame %.3f m/s; %.3f cm apart; %.0f km up"),
        SpeedBefore / 100.0, Fine.GetSpeed() / 100.0, Coarse.GetSpeed() / 100.0,
        (Fine.GetUniversePosition() - Coarse.GetUniversePosition()).Size(), Fine.GetGroundAltitude().Get(0.0) / 1.0e5));
    TestTrue(TEXT("still braking at the frame's end"), Fine.GetSpeed() < SpeedBefore - 100.0 && Fine.GetRegimeWeight() == 0.0);
    TestTrue(TEXT("a frame of 240 substeps flies the speed of 240 frames of one, within 1 cm/s"),
        FMath::Abs(Fine.GetSpeed() - Coarse.GetSpeed()) <= 1.0);
    TestTrue(TEXT("and to the same place, within 10 cm"), (Fine.GetUniversePosition() - Coarse.GetUniversePosition()).Size() <= 10.0);
    return true;
}

/*
 * The ray reused within a frame is only the old ray while the ship flies
 * along it. Sinking in the regime, the horizontal ray's origin drops across
 * it, and against rising ground the slope is nearer than the old hit less
 * the distance flown: a frame of 240 substeps must still fly as 240 frames
 * of one, the ship sinking toward a hill with the ground ahead holding it
 * back.
 */
bool FGroundRayAcrossTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const double Radius = 6.0e8;
    const FGroundFieldRef Ground = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(FCrossedSines::WithSlope(Radius, 4.0e5, 0.5));
    const FFlightSurface World = SurfaceOver(Ground, 1.02e6 + Ground->MaxHeightCm());
    // In the trough of both sines, the ground rising ahead along +X.
    const FVector3d D = FVector3d(-1.0e5 / Radius, -1.0e5 / Radius, 1.0).GetSafeNormal();

    auto Start = [&](FShipFlightState& Flight)
    {
        Flight.SetSurfaces({ World });
        Flight.SetWells({ { World.Centre, ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius } });
        Flight.SetUniverseTransform(Above(World, D, 3.0e4), Level(D));
        FShipFlightCommand Command;
        Command.Throttle = ThrottleFor(1.0e4, Flight.GetLimits());
        Command.Vertical = ShipVerticalLever::LeverOf(-500.0, Flight.GetLimits().VerticalTop);
        Flight.SetCommand(Command);
    };

    // Fine frames until the ground ahead has held the ship back for a
    // quarter second.
    FShipFlightState Fine;
    Start(Fine);
    int32 Holding = -1;
    int32 Steps = 0;
    for (; Steps < 120 * 120 && (Holding < 0 || Steps < Holding + 30); ++Steps)
    {
        Fine.Step(FShipFlightState::FixedStep);
        if (Holding < 0 && Fine.GetHold() != EFlightHold::Free && Fine.GetHeldFraction() > 0.05)
        {
            Holding = Steps;
        }
    }
    if (!TestTrue(TEXT("the ground ahead holds the ship back"), Holding >= 0))
    {
        return false;
    }
    TestTrue(TEXT("in the regime, sinking"), Fine.IsVerticalLive() && Fine.GetVerticalSpeed() < -100.0);

    FShipFlightState Coarse;
    Start(Coarse);
    for (int32 I = 0; I < Steps; ++I)
    {
        Coarse.Step(FShipFlightState::FixedStep);
    }
    const double SpeedBefore = Coarse.GetSpeed();
    Coarse.Step(240.0 * FShipFlightState::FixedStep);
    for (int32 I = 0; I < 240; ++I)
    {
        Fine.Step(FShipFlightState::FixedStep);
    }
    AddInfo(FString::Printf(TEXT("from %.2f m/s: fine %.3f m/s, one frame %.3f m/s; %.3f cm apart; %.1f m up"),
        SpeedBefore / 100.0, Fine.GetSpeed() / 100.0, Coarse.GetSpeed() / 100.0,
        (Fine.GetUniversePosition() - Coarse.GetUniversePosition()).Size(), Fine.GetGroundAltitude().Get(0.0) / 100.0));
    TestTrue(TEXT("a frame of 240 substeps flies the speed of 240 frames of one, within 1 cm/s"),
        FMath::Abs(Fine.GetSpeed() - Coarse.GetSpeed()) <= 1.0);
    TestTrue(TEXT("and to the same place, within 10 cm"), (Fine.GetUniversePosition() - Coarse.GetUniversePosition()).Size() <= 10.0);
    return true;
}

#endif
