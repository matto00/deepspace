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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkimsLowOverRealGroundTest, "DeepSpace.Ship.Landing.SkimsLowOverRealGround",
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
        FString HardStopCase;
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
        if (Log.HardStops > 0)
        {
            Tally.HardStopCase = FString::Printf(TEXT("%s cruise %.0f vertical %.0f chop %.4f thrust %.2f sink %.0f"),
                                                 Start.Name, Cruise, Vertical, Chop, Thrust, Sink);
        }
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
        Test.AddInfo(FString::Printf(TEXT("%d flights in %.1f s: least clearance %.3f cm, worst contact %.5f cm/s, %d hard stops; worst: %s"),
                                     Tally.Flights, Seconds, Tally.LeastClearance, Tally.WorstContact, Tally.HardStops, *Tally.WorstCase));
        if (Tally.HardStops > 0)
        {
            Test.AddInfo(FString::Printf(TEXT("a hard stop fired flying %s"), *Tally.HardStopCase));
        }
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

    // A pairwise-covering subset of the grid (spec decision 10): every pair
    // of levels of any two factors -- cruise, vertical, start, chop, thrust,
    // sink -- is flown on each world. Vertical, start and chop are three
    // mutually orthogonal Latin squares over cruise's first five levels
    // (V = a, S = a + C, chop = a + 2C, mod 5), the sixth cruise level takes
    // one more square, and thrust and sink are the parities of two further
    // ones. Thirty flights a world, checked below rather than trusted.
    struct FCase { int32 Cruise, Vertical, Start, Chop, Thrust, Sink; };
    TArray<FCase> Cases;
    for (int32 C = 0; C < CruiseSpeeds.Num(); ++C)
    {
        for (int32 A = 0; A < 5; ++A)
        {
            const int32 S = C < 5 ? (A + C) % 5 : (A + 3) % 5;
            const int32 Chop = C < 5 ? (A + 2 * C) % 5 : (A + 1) % 5;
            Cases.Add({ C, A, S, Chop, ((A + 3 * C) % 5) % 2, ((A + 4 * C) % 5) % 2 });
        }
    }
    const int32 Levels[] = { CruiseSpeeds.Num(), VerticalRates.Num(), 5, Chops.Num(), Thrusts.Num(), Sinks.Num() };
    auto Factor = [](const FCase& Row, int32 F)
    {
        const int32 Of[] = { Row.Cruise, Row.Vertical, Row.Start, Row.Chop, Row.Thrust, Row.Sink };
        return Of[F];
    };
    for (int32 F = 0; F < 6; ++F)
    {
        for (int32 G = F + 1; G < 6; ++G)
        {
            TSet<int32> Seen;
            for (const FCase& Row : Cases)
            {
                Seen.Add(Factor(Row, F) * 16 + Factor(Row, G));
            }
            TestEqual(FString::Printf(TEXT("factors %d and %d: every pair of levels flown"), F, G), Seen.Num(), Levels[F] * Levels[G]);
        }
    }

    // The starts: the drive floor, 5 km, 500 m, 20 m and 2 m over the real
    // ground (the rough starts are the fixture's; here the relief is real).
    const double Clears[] = { -1.0, 5.0e5, 5.0e4, 2.0e3, 2.0e2 };
    const TCHAR* Names[] = { TEXT("real drive floor"), TEXT("real 5 km"), TEXT("real 500 m"), TEXT("real 20 m"), TEXT("real 2 m") };
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
        for (int32 Case = 0; Case < Cases.Num(); ++Case)
        {
            const FCase& Row = Cases[Case];
            const FVector3d D = FVector3d(FMath::Sin(Case * 0.7), FMath::Cos(Case * 1.3), 0.5 + 0.1 * (Case % 5)).GetSafeNormal();
            const FStart Where{ Names[Row.Start], D, Clears[Row.Start] };
            Fly(Tally, World, G, Where, CruiseSpeeds[Row.Cruise], VerticalRates[Row.Vertical], Chops[Row.Chop],
                Thrusts[Row.Thrust], Sinks[Row.Sink], 60.0);
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
 * Low over real relief, the ship still moves (decision 10's skim floor, 20
 * m/s at 50 m and below). The along-ground ray is level at the feet, so near
 * the ground each fresh sample proves only Above / sqrt(1 + MaxSlope^2) of it,
 * about an eighth of the clearance on the real relief: 64 fresh samples in a
 * frame see a few metres, and an exhausted march is a hit. Held to that, a
 * ship hovering 2-10 m up over flat ground was held to 0-15 m/s. The samples
 * are facts about the ground, so a later frame's march reuses them and
 * spends its fresh ones past where the last one stopped.
 *
 * The starts are searched for, never typed in: stretches of the real relief
 * where the ground along the path, and 12 m either side, stays within a metre
 * or two of the ground under the ship, so nothing ahead is really in the way.
 */
bool FSkimsLowOverRealGroundTest::RunTest(const FString& Parameters)
{
    using namespace GroundCatchesLocal;
    const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
    const FFlightSurface World = SurfaceOver(Ground, 1.02e6 + Ground->MaxHeightCm());
    const double R = World.Radius;
    const double Strip = 3.0e4;

    // The ground's height at a plan offset (Along, Side) from D, in metres of arc.
    auto HeightAt = [&](const FVector3d& D, const FVector3d& Heading, const FVector3d& Side, double Along, double Across)
    {
        return Ground->Height((D * R + Heading * Along + Side * Across).GetSafeNormal(), 0.0);
    };

    struct FClear
    {
        double Clearance;   // footprint clearance at the start, cm
        double Rise;        // the most the strip may rise over the ground under the ship, cm
        double Least;       // horizontal speed the ship must reach, cm/s
    };
    const FClear Cases[] = { { 2.0e2, 1.0e2, 1.5e3 }, { 5.0e2, 2.0e2, 1.5e3 }, { 1.0e3, 3.0e2, 1.8e3 } };
    FRandomStream Stream(20260928);
    for (const FClear& Case : Cases)
    {
        // A flat stretch: 300 m ahead, 12 m either side, never above the
        // ground under the ship by more than Rise.
        FVector3d D = FVector3d::ZeroVector;
        FVector3d Heading = FVector3d::ZeroVector;
        for (int32 Try = 0; Try < 4000 && D.IsZero(); ++Try)
        {
            const FVector3d Candidate(Stream.GetUnitVector());
            const FVector3d East = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), Candidate).GetSafeNormal();
            const FVector3d North = FVector3d::CrossProduct(Candidate, East);
            const double Turn = Stream.FRandRange(0.0, 2.0 * UE_DOUBLE_PI);
            const FVector3d H = East * FMath::Cos(Turn) + North * FMath::Sin(Turn);
            const FVector3d Side = FVector3d::CrossProduct(Candidate, H);
            const double Under = HeightAt(Candidate, H, Side, 0.0, 0.0);
            bool bFlat = true;
            for (double Along = -2.0e3; Along <= Strip && bFlat; Along += 1.0e2)
            {
                for (double Across = -1.2e3; Across <= 1.2e3 && bFlat; Across += 3.0e2)
                {
                    bFlat = HeightAt(Candidate, H, Side, Along, Across) <= Under + Case.Rise;
                }
            }
            if (bFlat)
            {
                D = Candidate;
                Heading = H;
            }
        }
        if (!TestFalse(FString::Printf(TEXT("a flat stretch of real relief for %.0f m up"), Case.Clearance / 100.0), D.IsZero()))
        {
            return false;
        }

        FShipFlightState Flight;
        Flight.SetSurfaces({ World });
        const FQuat Turn = Level(D, FVector(Heading));
        double Agl = Case.Clearance + ShipLanding::DefaultGearClearanceCm;
        for (int32 Pass = 0; Pass < 3; ++Pass)
        {
            Flight.SetUniverseTransform(Above(World, D, Agl), Turn);
            Agl += Case.Clearance - ShipLanding::FootprintClearance(World, Flight.GetUniversePosition(), Turn,
                                                                     ShipLanding::DefaultGearClearanceCm).Least;
        }
        const FUniversePosition From = Flight.GetUniversePosition();
        FShipFlightCommand Command;
        Command.Throttle = ThrottleFor(2.0e3, Flight.GetLimits());
        Flight.SetCommand(Command);
        Flight.ResetGroundLog();
        double Fastest = 0.0;
        for (double T = 0.0; T < 4.0; T += 1.0 / 60.0)
        {
            Flight.Step(1.0 / 60.0);
            const FVector Up = (Flight.GetUniversePosition() - World.Centre).GetSafeNormal();
            const FVector V = Flight.GetVelocity();
            Fastest = FMath::Max(Fastest, (V - Up * (V | Up)).Size());
        }
        const double Flown = (Flight.GetUniversePosition() - From).Size();
        AddInfo(FString::Printf(TEXT("%.0f m up over real relief: %.1f m/s at the most, %.0f m flown, least clearance %.1f cm, %d hard stops"),
                                Case.Clearance / 100.0, Fastest / 100.0, Flown / 100.0,
                                Flight.GetGroundLog().LeastClearance, Flight.GetGroundLog().HardStops));
        TestTrue(FString::Printf(TEXT("%.0f m up, the ship skims at %.0f m/s or more"), Case.Clearance / 100.0, Case.Least / 100.0),
                 Fastest >= Case.Least);
        TestTrue(TEXT("and stays on the flat stretch"), Flown < Strip - 2.0e3);
        TestTrue(TEXT("no footprint point under the ground"), Flight.GetGroundLog().LeastClearance >= -1.0);
        TestEqual(TEXT("and the hard stop never fires"), Flight.GetGroundLog().HardStops, 0);
    }
    return true;
}

#endif
