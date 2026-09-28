#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightSurface.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The ground's laws as pure arithmetic (landing decisions 10 and 12): the
 * approach law with a knee the boosters can follow, the ray march that
 * never says "no hit" when it has not seen, the skim cap, and the ETA that
 * integrates the same laws. Siblings under DeepSpace.Ship.Landing, which is
 * never itself a test.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundApproachLawTest, "DeepSpace.Ship.Landing.ApproachLaw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundRayTest, "DeepSpace.Ship.Landing.RayToGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundSkimCapTest, "DeepSpace.Ship.Landing.SkimCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundSecondsTest, "DeepSpace.Ship.Landing.SecondsToGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace GroundLawTestLocal
{
    constexpr double Boost = 2.0e5;   // 2 km/s^2
    constexpr double N = ShipFlight::DefaultApproachSeconds;
    constexpr double Touch = ShipFlight::DefaultTouchdownSpeed;
    constexpr double Step = 1.0 / 120.0;

    ShipFlight::FGroundLaw FullLaw()
    {
        return { Boost, N, Touch, ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor };
    }
}

bool FGroundApproachLawTest::RunTest(const FString& Parameters)
{
    using namespace GroundLawTestLocal;
    const double Margin = ShipFlight::BrakingMargin * Boost;

    // The demanded deceleration, v dv/dD, never exceeds 0.8 A: an inertial
    // ship can follow the law all the way down.
    double Worst = 0.0;
    for (double D = 1.0; D < 3.0e7; D *= 1.002)
    {
        const double V = ShipFlight::GroundApproachSpeed(D, Boost, N, 0.0, 0.0);
        const double Next = ShipFlight::GroundApproachSpeed(D * 1.0001, Boost, N, 0.0, 0.0);
        Worst = FMath::Max(Worst, V * (Next - V) / (D * 1.0e-4) / Margin);
    }
    TestTrue(FString::Printf(TEXT("the law never demands more than 0.8 A (worst %.4f of it)"), Worst), Worst <= 1.0 + 1e-3);

    const double Knee = Margin * N * N;
    TestTrue(FString::Printf(TEXT("the knee is 25.6 km at full thrust (%.1f km)"), Knee / 1.0e5), FMath::IsNearlyEqual(Knee, 2.56e6, 1.0));
    TestTrue(TEXT("where the exponential's D / N meets the braking parabola"),
             FMath::IsNearlyEqual(ShipFlight::GroundApproachSpeed(Knee, Boost, N, 0.0, 0.0), Knee / N, 1e-6)
             && FMath::IsNearlyEqual(ShipFlight::GroundApproachSpeed(Knee * (1.0 + 1e-9), Boost, N, 0.0, 0.0), Knee / N, 1.0));
    TestTrue(TEXT("starved, a quarter thrust, the knee is 6.4 km"),
             FMath::IsNearlyEqual(ShipFlight::BrakingMargin * 0.25 * Boost * N * N, 6.4e5, 1.0));
    TestEqual(TEXT("a metre up, 0.5 m/s: never the 57 m/s the braking curve allows"),
              ShipFlight::GroundApproachSpeed(100.0, Boost, N, Touch, Step), Touch);
    TestTrue(TEXT("which MaySpeed would have allowed there"), ShipFlight::MaySpeed(100.0, Boost, N, Step) > 5.0e3);
    TestEqual(TEXT("under the substep's own reach, D / Step: no substep crosses the ground"),
              ShipFlight::GroundApproachSpeed(0.2, Boost, N, Touch, Step), 0.2 / Step);
    TestEqual(TEXT("at the ground, nothing"), ShipFlight::GroundApproachSpeed(0.0, Boost, N, Touch, Step), 0.0);
    TestEqual(TEXT("ds.Land.ApproachSeconds 0 is clamped to half a second, never a division by zero"),
              ShipFlight::GroundApproachSpeed(1.0e4, Boost, 0.0, Touch, 0.0),
              ShipFlight::GroundApproachSpeed(1.0e4, Boost, ShipFlight::MinApproachSeconds, Touch, 0.0));
    TestEqual(TEXT("800 m up the full 200 m/s sink first binds"), ShipFlight::GroundApproachSpeed(8.0e4, Boost, N, Touch, 0.0), 2.0e4);
    return true;
}

bool FGroundRayTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    const FGroundFieldRef Relief = ShipGround::FromRelief(FixtureParams());
    const FFlightSurface World = SurfaceOver(Relief, 1.0e6 + Relief->MaxHeightCm());
    const FVector3d Pole(0.0, 0.0, 1.0);

    // Under the ground: heading out it may always climb; heading in, 0.
    const FUniversePosition Under = World.Centre + FVector(Pole) * (World.Radius + Relief->Height(Pole, 0.0) - 50.0);
    TestFalse(TEXT("under the ground, a ray heading up meets nothing"),
              ShipFlight::RayToGround(World, Under, FVector(Pole), 0.0, 1.0e6).IsSet());
    const TOptional<double> Down = ShipFlight::RayToGround(World, Under, -FVector(Pole), 0.0, 1.0e6);
    TestTrue(TEXT("and one heading down meets it at once"), Down.IsSet() && *Down == 0.0);

    // An exhausted march is a hit, never "no hit": a grazing ray a metre
    // over the crests of ground steeper than any real world. Along +X from
    // the pole the field is 0.5 A sin(k x), crests at A / 2, so a level ray a
    // metre over them never meets the ground -- it creeps past every crest a
    // Lipschitz step at a time and runs out of steps long before the shell
    // (a ray a metre over the pole's own height meets the first flank in a
    // handful of steps, and would prove nothing about exhaustion).
    const FGroundFieldRef Steep = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(FCrossedSines::WithSlope(World.Radius, 2.0e3, 60.0));
    const FFlightSurface Cliffs = SurfaceOver(Steep, 1.0e6);
    int32 Steps = 0;
    const TOptional<double> Grazing = ShipFlight::RayToGround(Cliffs, Above(Cliffs, Pole, 0.5 * Steep->MaxHeightCm() + 100.0),
                                                              FVector(1.0, 0.0, 0.0), 0.0, 1.0e7, &Steps);
    TestTrue(FString::Printf(TEXT("a march that runs out of steps is a hit (%d steps)"), Steps),
             Grazing.IsSet() && Steps == ShipFlight::GroundMarchSteps);

    // On real relief: from every height the skim cap allows, a level ray
    // sees 2.5 x AGL in its 64 steps, and everything it reports is true --
    // nothing crosses the ground before a hit, or before the lookahead when
    // it says clear.
    FRandomStream Random(20260927);
    int32 Exhausted = 0;
    int32 Lies = 0;
    int32 Trials = 0;
    for (const double Agl : { 2.0e3, 5.0e4, 5.0e5, 5.0e6 })
    {
        for (int32 Trial = 0; Trial < 25; ++Trial, ++Trials)
        {
            const FVector3d D = FVector3d(Random.FRandRange(-0.05, 0.05), Random.FRandRange(-0.05, 0.05), 1.0).GetSafeNormal();
            const FUniversePosition From = Above(World, D, Agl);
            const FVector Heading = FVector::CrossProduct(FVector(D), FVector(Random.GetUnitVector())).GetSafeNormal();
            const double Lookahead = 2.5 * Agl;
            int32 Used = 0;
            const TOptional<double> Hit = ShipFlight::RayToGround(World, From, Heading, 0.0, Lookahead, &Used);
            Exhausted += Used >= ShipFlight::GroundMarchSteps ? 1 : 0;
            const double Seen = Hit ? *Hit : Lookahead;
            for (int32 Sample = 0; Sample <= 400; ++Sample)
            {
                const double T = Seen * Sample / 400.0 * 0.999;
                const FVector P = (From + Heading * T) - World.Centre;
                const double R = P.Size();
                if (R - World.Radius - Relief->Height(FVector3d(P / R), 0.0) < -1.0)
                {
                    ++Lies;
                    break;
                }
            }
        }
    }
    TestEqual(FString::Printf(TEXT("no march of %d runs out of steps before 2.5 x AGL"), Trials), Exhausted, 0);
    TestEqual(TEXT("and none reports clear ground that is not"), Lies, 0);
    return true;
}

bool FGroundSkimCapTest::RunTest(const FString& Parameters)
{
    const double Skim = ShipFlight::DefaultSkimSeconds;
    const double Floor = ShipFlight::DefaultSkimFloor;
    TestEqual(TEXT("20 km/s at 50 km: cruise's top, so the cap is slack at the regime's top"), ShipFlight::SkimCap(5.0e6, Skim, Floor), 2.0e6);
    TestEqual(TEXT("2 km/s at 5 km"), ShipFlight::SkimCap(5.0e5, Skim, Floor), 2.0e5);
    TestEqual(TEXT("200 m/s at 500 m"), ShipFlight::SkimCap(5.0e4, Skim, Floor), 2.0e4);
    TestEqual(TEXT("20 m/s at 50 m"), ShipFlight::SkimCap(5.0e3, Skim, Floor), 2.0e3);
    TestEqual(TEXT("and 20 m/s below, however low"), ShipFlight::SkimCap(100.0, Skim, Floor), Floor);
    TestEqual(TEXT("under the ground, still the floor"), ShipFlight::SkimCap(-50.0, Skim, Floor), Floor);
    return true;
}

bool FGroundSecondsTest::RunTest(const FString& Parameters)
{
    using namespace GroundLawTestLocal;
    const ShipFlight::FGroundLaw Law = FullLaw();
    // (H - 800 m) / 200 m/s + N ln(800 m / (N x 0.5 m/s)) + N: 74 s from
    // 10 km, 124 s from 20 km (decision 10's descent time).
    const double Tail = N * FMath::Loge(8.0e4 / (N * Touch)) + N;
    const double From10 = ShipFlight::SecondsToGround(1.0e6, 2.0e4, 1.0, Law);
    const double From20 = ShipFlight::SecondsToGround(2.0e6, 2.0e4, 1.0, Law);
    TestTrue(FString::Printf(TEXT("straight down from 10 km at 200 m/s: %.2f s"), From10),
             FMath::IsNearlyEqual(From10, (1.0e6 - 8.0e4) / 2.0e4 + Tail, 0.05));
    TestTrue(FString::Printf(TEXT("and from 20 km: %.2f s"), From20),
             FMath::IsNearlyEqual(From20, (2.0e6 - 8.0e4) / 2.0e4 + Tail, 0.05));
    TestEqual(TEXT("at the ground, now"), ShipFlight::SecondsToGround(0.0, 2.0e4, 1.0, Law), 0.0);
    TestTrue(TEXT("at rest, never"), !FMath::IsFinite(ShipFlight::SecondsToGround(1.0e5, 0.0, 1.0, Law)));

    // It counts down a second a second: flying dt at the speed the laws
    // allow takes dt off the ETA, at any distance and any path angle.
    FRandomStream Random(7);
    double Worst = 0.0;
    for (int32 Trial = 0; Trial < 60; ++Trial)
    {
        const double L = FMath::Pow(10.0, Random.FRandRange(2.0, 7.0));
        const double Sine = Random.FRandRange(0.05, 1.0);
        const double Speed = FMath::Pow(10.0, Random.FRandRange(2.0, 6.0));
        const double Cosine = FMath::Sqrt(1.0 - Sine * Sine);
        const double Allowed = FMath::Min(Speed, FMath::Min(
            ShipFlight::GroundApproachSpeed(L * Sine, Law.BrakingAccel, Law.ApproachSeconds, Law.TouchdownSpeed, 0.0) / Sine,
            ShipFlight::SkimCap(L * Sine, Law.SkimSeconds, Law.SkimFloor) / Cosine));
        const double Dt = FMath::Min(0.01, 0.001 * L / Allowed);
        const double Rate = (ShipFlight::SecondsToGround(L, Speed, Sine, Law)
                           - ShipFlight::SecondsToGround(L - Allowed * Dt, Speed, Sine, Law)) / Dt;
        Worst = FMath::Max(Worst, FMath::Abs(Rate - 1.0));
    }
    TestTrue(FString::Printf(TEXT("the ETA falls a second a second (worst %.4f off)"), Worst), Worst <= 0.01);
    return true;
}

#endif
