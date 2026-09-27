#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightSurface.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightSurfaceTest,
    "DeepSpace.Ship.FlightSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    constexpr double AU = UniverseUnits::CmPerAU;
    constexpr double C = ShipDriveLever::LightCmPerSecond;

    /** The boosters' full acceleration, cm/s^2: FShipFlightLimits' 2 km/s^2. */
    constexpr double Boost = 2.0e5;
    constexpr double Hold = ShipFlight::DefaultHoldSeconds;
    constexpr double Step = 1.0 / 120.0;

    /** Off the universe origin and across chunks, as the real galaxy is. */
    FUniversePosition Somewhere()
    {
        return FUniversePosition(FInt64Vector(3, -2, 0), FVector(1.0e12, 5.0e12, 0.0));
    }

    /** An Earth with the sky's 10.2 km floor, a literal here so that this
     *  test does not lean on the function that makes the floor. */
    FFlightSurface Earth()
    {
        FFlightSurface Surface;
        Surface.Centre = Somewhere();
        Surface.Radius = UniverseUnits::CmPerEarthRadius;
        Surface.Floor = 1.02e6;
        return Surface;
    }

    /** The system's edge, 15,800 AU about the star, 10 km inside. */
    FFlightSurface Edge()
    {
        FFlightSurface Surface;
        Surface.Centre = Somewhere();
        Surface.Radius = 15800.0 * AU;
        Surface.Floor = ShipFlight::DefaultFloorCm;
        Surface.bInsideOut = true;
        return Surface;
    }

    bool RelativeErrorOk(double Actual, double Expected)
    {
        return FMath::Abs(Actual - Expected) <= 1.0e-12 * FMath::Max(FMath::Abs(Expected), 1.0);
    }

    /** A direction off every axis, so no component is special. */
    FVector Out()
    {
        return FVector(-0.8, 0.45, 0.4).GetSafeNormal();
    }

    /** Some unit vector perpendicular to V. */
    FVector Across(const FVector& V)
    {
        return FVector::CrossProduct(V, FVector(0.3, -0.2, 0.93)).GetSafeNormal();
    }

    /** The unit direction from Ship that passes Miss cm from Centre. */
    FVector Aimed(const FUniversePosition& Ship, const FUniversePosition& Centre, double Miss)
    {
        const FVector ToCentre = Centre - Ship;
        const double Distance = ToCentre.Size();
        const FVector In = ToCentre / Distance;
        const double Sin = Miss / Distance;
        return In * FMath::Sqrt(1.0 - Sin * Sin) + Across(In) * Sin;
    }

    /** How far Ship + Direction x D is from Centre. */
    double LandsAt(const FUniversePosition& Ship, const FVector& Direction, double D, const FUniversePosition& Centre)
    {
        return ((Ship + Direction.GetSafeNormal() * D) - Centre).Size();
    }

    /**
     * The approach as the flight flies it, one dimension along the ray: at
     * the lever's speed until the cap holds it, then at the cap, substep by
     * substep at 120 Hz. Records the distance left and the speed at every
     * whole second, and returns the time to the floor.
     */
    double Fly(double From, double Lever, TArray<TPair<double, double>>& EverySecond, double HoldSeconds = Hold)
    {
        double D = From;
        double T = 0.0;
        int32 Substeps = 0;
        while (D > 1.0e-6 && Substeps < 100 * 1000 * 1000)
        {
            const double V = FMath::Min(Lever, ShipFlight::MaySpeed(D, Boost, HoldSeconds, Step));
            if (Substeps % 120 == 0)
            {
                EverySecond.Add({ D, V });
            }
            D -= V * Step;
            T += Step;
            ++Substeps;
        }
        return T;
    }
}

bool FShipFlightSurfaceTest::RunTest(const FString& Parameters)
{
    using namespace ShipFlight;
    const FFlightSurface World = Earth();
    const double FloorRadius = World.Radius + World.Floor;

    // A ray at the centre meets the floor sphere at the distance less its
    // radius, from 0.2 AU and from 250,000 km.
    for (const double Distance : { 0.2 * AU, 2.5e10 })
    {
        const FUniversePosition Ship = World.Centre + Out() * Distance;
        const TOptional<double> D = RayToFloor(World, Ship, -Out());
        TestTrue(FString::Printf(TEXT("the centre is met from %.3g cm"), Distance), D.IsSet());
        TestTrue(FString::Printf(TEXT("at its distance less the floor radius, to a centimetre (%.3f off)"), D.Get(0.0) - (Distance - FloorRadius)),
                 FMath::Abs(D.Get(0.0) - (Distance - FloorRadius)) < 1.0);
        TestTrue(TEXT("a direction's length does not matter"),
                 FMath::IsNearlyEqual(RayToFloor(World, Ship, -Out() * 1.0e6).Get(-1.0), D.Get(0.0), 1.0e-3));
        TestFalse(TEXT("pointing away meets nothing"), RayToFloor(World, Ship, Out()).IsSet());
        TestFalse(TEXT("nor does sideways, from outside"), RayToFloor(World, Ship, Across(Out())).IsSet());
    }

    // The limb: a millionth of the floor radius inside it is a hit that lands
    // on the floor sphere; a millionth outside is a miss.
    for (const double Distance : { 0.2 * AU, 2.5e10, 2.0e9 })
    {
        const FUniversePosition Ship = World.Centre + Out() * Distance;
        const FVector In = Aimed(Ship, World.Centre, FloorRadius * (1.0 - 1.0e-6));
        const FVector Outside = Aimed(Ship, World.Centre, FloorRadius * (1.0 + 1.0e-6));
        const TOptional<double> D = RayToFloor(World, Ship, In);
        TestTrue(FString::Printf(TEXT("just inside the limb from %.3g cm is a hit"), Distance), D.IsSet());
        const double Landed = LandsAt(Ship, In, D.Get(0.0), World.Centre);
        TestTrue(FString::Printf(TEXT("and it lands on the floor sphere (%.3f cm off)"), Landed - FloorRadius),
                 FMath::Abs(Landed - FloorRadius) < 10.0);
        TestTrue(TEXT("no nearer than the centre ray's hit, no farther than the tangent"),
                 D.Get(0.0) >= Distance - FloorRadius - 1.0
                 && D.Get(0.0) <= FMath::Sqrt((Distance - FloorRadius) * (Distance + FloorRadius)) + 1.0);
        TestFalse(FString::Printf(TEXT("just outside the limb from %.3g cm is a miss"), Distance),
                  RayToFloor(World, Ship, Outside).IsSet());
    }

    // The limb from the far side of a system, where the precision is: from
    // 30 AU and from the 138 AU of procgen's longest leg, a billionth of the
    // floor radius (0.64 cm) either side of an Earth's limb. Distance^2 -
    // Along^2 there is two numbers of 1e29 to 1e31 agreeing to their last
    // digits, and misplaces the ray by hundreds of metres to kilometres; the
    // cross product keeps it to a fraction of a centimetre.
    for (const double Distance : { 30.0 * AU, 138.0 * AU })
    {
        const FUniversePosition Ship = World.Centre + Out() * Distance;
        const FVector In = Aimed(Ship, World.Centre, FloorRadius * (1.0 - 1.0e-9));
        const FVector Outside = Aimed(Ship, World.Centre, FloorRadius * (1.0 + 1.0e-9));
        const TOptional<double> D = RayToFloor(World, Ship, In);
        TestTrue(FString::Printf(TEXT("a billionth inside the limb from %.0f AU is a hit"), Distance / AU), D.IsSet());
        const double Landed = LandsAt(Ship, In, D.Get(0.0), World.Centre);
        TestTrue(FString::Printf(TEXT("and it lands on the floor sphere to a centimetre (%.3f cm off)"), Landed - FloorRadius),
                 D.IsSet() && FMath::Abs(Landed - FloorRadius) < 1.0);
        TestFalse(FString::Printf(TEXT("a billionth outside it from %.0f AU is a miss"), Distance / AU),
                  RayToFloor(World, Ship, Outside).IsSet());
    }

    // On the floor sphere: below the horizon meets it at 0, above leaves it.
    {
        const FUniversePosition Ship = World.Centre + Out() * FloorRadius;
        const FVector Tangent = Across(Out());
        const FVector Below = (Tangent - Out() * 1.0e-3).GetSafeNormal();
        const FVector Above = (Tangent + Out() * 1.0e-3).GetSafeNormal();
        TestTrue(TEXT("on the floor, below the horizon, the floor is here"),
                 RayToFloor(World, Ship, Below).IsSet() && RayToFloor(World, Ship, Below).GetValue() < 1.0);
        TestTrue(TEXT("and straight down too"), RayToFloor(World, Ship, -Out()).Get(-1.0) < 1.0 && RayToFloor(World, Ship, -Out()).IsSet());
        TestFalse(TEXT("on the floor, above the horizon, the lever is free"), RayToFloor(World, Ship, Above).IsSet());
        TestFalse(TEXT("and straight up"), RayToFloor(World, Ship, Out()).IsSet());

        // Skimming 1 km over the floor: the horizon is acos(Rf / (Rf + h))
        // below level. A nose just above it flies a tangent, untouched; just
        // below it meets the floor a horizon away; steeper, nearer.
        const double Height = 1.0e5;
        const FUniversePosition Skim = World.Centre + Out() * (FloorRadius + Height);
        const double Horizon = FMath::Acos(FloorRadius / (FloorRadius + Height));
        auto Dipped = [&](double Dip) { return Tangent * FMath::Cos(Dip) - Out() * FMath::Sin(Dip); };
        const TOptional<double> Far = RayToFloor(World, Skim, Dipped(Horizon * 1.001));
        const TOptional<double> Steeper = RayToFloor(World, Skim, Dipped(Horizon * 3.0));
        TestFalse(TEXT("skimming, a nose just above the horizon is free"), RayToFloor(World, Skim, Dipped(Horizon * 0.999)).IsSet());
        TestTrue(FString::Printf(TEXT("just below it meets the floor about a horizon away (%.0f m)"), Far.Get(0.0) / 100.0),
                 Far.IsSet() && FMath::Abs(Far.GetValue() / FMath::Sqrt(Height * (2.0 * FloorRadius + Height)) - 1.0) < 0.1);
        TestTrue(TEXT("and a steeper one nearer"), Steeper.IsSet() && Steeper.GetValue() < 0.5 * Far.Get(0.0));
    }

    // Under the floor, and inside the body: heading in is 0, heading out free.
    {
        for (const double Depth : { 1.0e5, World.Floor + 1.0e7, FloorRadius - 1.0e4 })
        {
            const FUniversePosition Ship = World.Centre + Out() * (FloorRadius - Depth);
            TestEqual(FString::Printf(TEXT("%.3g cm under the floor, heading down, it may not move"), Depth),
                      RayToFloor(World, Ship, -Out()).Get(-1.0), 0.0);
            TestEqual(TEXT("nor heading a little down"),
                      RayToFloor(World, Ship, (Across(Out()) - Out() * 1.0e-3).GetSafeNormal()).Get(-1.0), 0.0);
            TestFalse(TEXT("heading up it climbs at the lever's speed"), RayToFloor(World, Ship, Out()).IsSet());
            TestFalse(TEXT("and a little up too"),
                      RayToFloor(World, Ship, (Across(Out()) + Out() * 1.0e-3).GetSafeNormal()).IsSet());
            TestTrue(TEXT("its clearance is negative"), FloorClearance(World, Ship) < 0.0);
        }
        TestFalse(TEXT("at the very centre, every way is up"), RayToFloor(World, World.Centre, Out()).IsSet());
        TestFalse(TEXT("every way"), RayToFloor(World, World.Centre, -Out()).IsSet());
    }

    // From inside the edge, every ray meets it, at the far root.
    {
        const FFlightSurface Boundary = Edge();
        const double EdgeFloor = Boundary.Radius - Boundary.Floor;
        const FUniversePosition Ship = Boundary.Centre + Out() * AU;
        const TOptional<double> Outward = RayToFloor(Boundary, Ship, Out());
        const TOptional<double> Inward = RayToFloor(Boundary, Ship, -Out());
        const TOptional<double> Sideways = RayToFloor(Boundary, Ship, Across(Out()));
        TestTrue(TEXT("outward, the edge's floor less the ship's distance"),
                 Outward.IsSet() && RelativeErrorOk(Outward.GetValue(), EdgeFloor - AU));
        TestTrue(TEXT("inward, through the star, the far root"),
                 Inward.IsSet() && RelativeErrorOk(Inward.GetValue(), EdgeFloor + AU));
        TestTrue(TEXT("sideways, the half chord"),
                 Sideways.IsSet() && RelativeErrorOk(Sideways.GetValue(), FMath::Sqrt((EdgeFloor - AU) * (EdgeFloor + AU))));

        bool bEveryRay = true;
        double Worst = 0.0;
        for (int32 Index = 0; Index < 64; ++Index)
        {
            const double Theta = Index * 2.39996;   // the golden angle
            const double Z = 1.0 - (Index + 0.5) / 32.0;
            const double R = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
            const FVector Direction(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Z);
            const TOptional<double> D = RayToFloor(Boundary, Ship, Direction);
            bEveryRay &= D.IsSet() && D.GetValue() > 0.0;
            Worst = FMath::Max(Worst, FMath::Abs(LandsAt(Ship, Direction, D.Get(0.0), Boundary.Centre) - EdgeFloor) / EdgeFloor);
        }
        TestTrue(TEXT("every ray from inside meets the edge"), bEveryRay);
        TestTrue(FString::Printf(TEXT("on its floor sphere, to 1e-12 (worst %.2e)"), Worst), Worst < 1.0e-12);

        const FUniversePosition Beyond = Boundary.Centre + Out() * (EdgeFloor + 1.0e5);
        TestEqual(TEXT("beyond the edge's floor, heading further out, it may not move"),
                  RayToFloor(Boundary, Beyond, Out()).Get(-1.0), 0.0);
        TestTrue(TEXT("heading back in, it may, as far as the far side"),
                 RayToFloor(Boundary, Beyond, -Out()).IsSet() && RayToFloor(Boundary, Beyond, -Out()).GetValue() > 1.9 * EdgeFloor);
        TestTrue(TEXT("its clearance is negative out there"), FloorClearance(Boundary, Beyond) < 0.0);
        TestTrue(TEXT("and positive inside"), RelativeErrorOk(FloorClearance(Boundary, Ship), EdgeFloor - AU));
    }

    TestFalse(TEXT("a zero direction has no path"), RayToFloor(World, World.Centre + Out() * AU, FVector::ZeroVector).IsSet());

    // MaySpeed: D / N far out, the braking curve near in, never more than
    // D / step, and continuous and monotonic in between.
    {
        // The braking curve at 80% of the boosters, stepped: v^2 / 2b + v
        // Step / 2 = D, so v = sqrt(2 b D + h^2) - h with h = b Step / 2.
        const double Braking = 2.0 * 0.8 * Boost;
        const double H = 0.25 * Braking * Step;
        const auto Stepped = [&](double D) { return FMath::Sqrt(Braking * D + H * H) - H; };
        TestTrue(TEXT("far out, D / N"), FMath::IsNearlyEqual(MaySpeed(1.0e11, Boost, Hold, Step), 1.0e11 / Hold, 1e-3));
        TestTrue(TEXT("near in, the stepped braking curve at 80% of the boosters"),
                 FMath::IsNearlyEqual(MaySpeed(1.0e6, Boost, Hold, Step), Stepped(1.0e6), 1e-6));
        TestTrue(TEXT("with no substep, the continuous one"),
                 FMath::IsNearlyEqual(MaySpeed(1.0e6, Boost, Hold, 0.0), FMath::Sqrt(Braking * 1.0e6), 1e-6));
        TestTrue(TEXT("they meet 51.2 km out at 12.8 km/s, where the hold's D / N wins over the stepped curve"),
                 FMath::IsNearlyEqual(5.12e6 / Hold, FMath::Sqrt(2.0 * BrakingMargin * Boost * 5.12e6), 1e-6)
                 && FMath::IsNearlyEqual(MaySpeed(5.12e6, Boost, Hold, Step), 1.28e6, 1e-6));

        // Stepped, the curve is one the boosters can follow to rest: a ship
        // on it that flies one substep at that speed finds the curve exactly
        // b Step lower, at every speed down to the landing substep, which
        // the D / Step bound shortens and which still asks for less than the
        // boosters' whole a Step. The continuous curve falls faster than the
        // boosters can brake in its last few substeps -- under about 50 m/s
        // at full thrust -- and a ship with inertia met its floor at speed.
        {
            const double Loss = 0.5 * Braking * Step;
            double WorstRatio = 0.0;
            int32 OffCurve = 0;
            int32 Substeps = 0;
            double D = 1.25e7;   // 125 km: cruise's stopping distance from 20 km/s
            double V = MaySpeed(D, Boost, 0.0, Step);
            while (D > 0.0 && V > 0.0 && Substeps < 100000)
            {
                D -= V * Step;
                const double Next = MaySpeed(D, Boost, 0.0, Step);
                const double Ratio = (V - Next) / Loss;
                WorstRatio = FMath::Max(WorstRatio, Ratio);
                OffCurve += FMath::Abs(Ratio - 1.0) > 1e-6 ? 1 : 0;
                V = Next;
                ++Substeps;
            }
            TestTrue(FString::Printf(TEXT("braking from 125 km to the floor, the curve falls b Step a substep but for the landing (%d substeps of %d off it)"),
                                     OffCurve, Substeps),
                     OffCurve <= 2 && Substeps > 1000);
            TestTrue(FString::Printf(TEXT("and never faster than the boosters' whole a Step (worst %.4f of b Step, against 1.25)"),
                                     WorstRatio),
                     WorstRatio <= 1.0 / BrakingMargin + 1e-9);
            TestTrue(FString::Printf(TEXT("and it reaches the floor (%.6f cm to go, %d substeps)"), D, Substeps),
                     FMath::Abs(D) < 1e-3 && V < 1e-3);
        }
        TestTrue(TEXT("a millimetre out, one substep may not carry it past"),
                 FMath::IsNearlyEqual(MaySpeed(0.1, Boost, Hold, Step), 0.1 / Step, 1e-12));
        TestEqual(TEXT("at the floor, nothing"), MaySpeed(0.0, Boost, Hold, Step), 0.0);
        TestEqual(TEXT("under it, nothing"), MaySpeed(-5.0, Boost, Hold, Step), 0.0);
        TestTrue(TEXT("no boosters: the hold alone"), FMath::IsNearlyEqual(MaySpeed(1.0e4, 0.0, Hold, Step), 1.0e4 / Hold, 1e-12));

        // No hold (ds.Drive.HoldSeconds 0 or less) is the braking curve alone:
        // still a cap, never none.
        for (const double NoHold : { 0.0, -1.0 })
        {
            TestTrue(FString::Printf(TEXT("a hold of %.0f s: far out, the braking curve, not D / step"), NoHold),
                     FMath::IsNearlyEqual(MaySpeed(1.0e11, Boost, NoHold, Step), Stepped(1.0e11), 1e-6));
            TestTrue(TEXT("and with no substep bound either, still the braking curve"),
                     FMath::IsNearlyEqual(MaySpeed(1.0e11, Boost, NoHold, 0.0), FMath::Sqrt(2.0 * BrakingMargin * Boost * 1.0e11), 1e-6));
            TestTrue(TEXT("near in, the same braking curve as with a hold"),
                     FMath::IsNearlyEqual(MaySpeed(1.0e4, Boost, NoHold, Step), MaySpeed(1.0e4, Boost, Hold, Step), 1e-12));
            TestEqual(TEXT("with no boosters as well, no closing at all"), MaySpeed(1.0e4, 0.0, NoHold, Step), 0.0);
        }

        bool bIncreasing = true;
        bool bContinuous = true;
        bool bStepBound = true;
        double Previous = MaySpeed(1.0e-6, Boost, Hold, Step);
        for (double D = 1.0e-6 * 1.001; D < 1.0e18; D *= 1.001)
        {
            const double V = MaySpeed(D, Boost, Hold, Step);
            bIncreasing &= V > Previous;
            bContinuous &= V / Previous <= 1.001 * (1.0 + 1e-12);
            bStepBound &= V <= D / Step * (1.0 + 1e-15);
            Previous = V;
        }
        TestTrue(TEXT("MaySpeed rises with D"), bIncreasing);
        TestTrue(TEXT("never faster than D itself: continuous, no step anywhere"), bContinuous);
        TestTrue(TEXT("and never more than D / step"), bStepBound);
    }

    // The room: the least clearance over every surface, never negative.
    {
        FFlightSurface Moon;
        Moon.Centre = World.Centre + FVector(3.844e10, 0.0, 0.0);
        Moon.Radius = 1.7374e8;
        Moon.Floor = ShipFlight::DefaultFloorCm;
        const TArray<FFlightSurface> Surfaces = { World, Moon, Edge() };

        const FUniversePosition High = World.Centre + Out() * (FloorRadius + 1.0e8);
        TestTrue(TEXT("1,000 km over the Earth's floor, the room is 1,000 km"), FMath::IsNearlyEqual(Room(Surfaces, High), 1.0e8, 1.0));
        const FUniversePosition NearMoon = Moon.Centre + FVector(0.0, Moon.Radius + Moon.Floor + 5.0e6, 0.0);
        TestTrue(TEXT("50 km over the moon's floor, the moon is what counts"), FMath::IsNearlyEqual(Room(Surfaces, NearMoon), 5.0e6, 1.0));
        TestEqual(TEXT("under a floor, no room"), Room(Surfaces, World.Centre + Out() * World.Radius), 0.0);
        TestEqual(TEXT("between stars, no surfaces and no room"), Room(TConstArrayView<FFlightSurface>(), High), 0.0);
    }

    // The live ETA's law (ruling 3).
    {
        TestFalse(TEXT("at rest it never arrives"), FMath::IsFinite(SecondsToFloor(1.0e10, 0.0, Boost, Hold)));
        TestFalse(TEXT("nor with no braking at all"), FMath::IsFinite(SecondsToFloor(1.0e10, C, 0.0, Hold)));
        TestEqual(TEXT("at the floor it is there"), SecondsToFloor(0.0, C, Boost, Hold), 0.0);

        const double D = 0.2 * AU;
        const double Knee = 2.0 * BrakingMargin * Boost * Hold * Hold;
        TestTrue(TEXT("the knee is 51.2 km at full boosters"), FMath::IsNearlyEqual(Knee, 5.12e6, 1e-6));
        TestTrue(TEXT("above the knee: (D - vN) / v + N ln(vN / knee) + 2N"),
                 FMath::IsNearlyEqual(SecondsToFloor(D, C, Boost, Hold),
                                      (D - C * Hold) / C + Hold * FMath::Loge(C * Hold / Knee) + 2.0 * Hold, 1e-9));
        TestTrue(TEXT("already under the cap, the cap's time alone"),
                 FMath::IsNearlyEqual(SecondsToFloor(2.5e10, C, Boost, Hold), Hold * FMath::Loge(2.5e10 / Knee) + 2.0 * Hold, 1e-9));
        const double Braking = 2.0 * BrakingMargin * Boost;
        TestTrue(TEXT("below the knee's speed: held to the braking curve, then braking"),
                 FMath::IsNearlyEqual(SecondsToFloor(1.0e6, 1.0e4, Boost, Hold),
                                      (1.0e6 - 1.0e8 / Braking) / 1.0e4 + 2.0 * FMath::Sqrt((1.0e8 / Braking) / Braking), 1e-9));

        // Agrees with the flown approach, from every second of it.
        struct FCase { const TCHAR* Name; double From; double Lever; };
        for (const FCase& Case : { FCase{ TEXT("0.2 AU at 0.1 c"), 0.2 * AU, 0.1 * C },
                                   FCase{ TEXT("250,000 km at 0.1 c"), 2.5e10, 0.1 * C },
                                   FCase{ TEXT("1,000 km at cruise's 20 km/s"), 1.0e8, 2.0e6 },
                                   FCase{ TEXT("10 km at 200 m/s"), 1.0e6, 2.0e4 },
                                   FCase{ TEXT("0.2 AU at 1 c, past the drive's top"), 0.2 * AU, C } })
        {
            TArray<TPair<double, double>> EverySecond;
            const double Total = Fly(Case.From, Case.Lever, EverySecond);
            double Worst = 0.0;
            for (int32 Second = 0; Second < EverySecond.Num(); ++Second)
            {
                const double Eta = SecondsToFloor(EverySecond[Second].Key, EverySecond[Second].Value, Boost, Hold);
                Worst = FMath::Max(Worst, FMath::Abs(Eta - (Total - Second)));
            }
            AddInfo(FString::Printf(TEXT("%s: flown in %.1f s; the ETA's worst disagreement %.3f s"), Case.Name, Total, Worst));
            TestTrue(FString::Printf(TEXT("%s: flown in %.1f s, and the ETA agrees to 0.5 s from every second (worst %.3f s)"),
                                     Case.Name, Total, Worst),
                     Worst < 0.5 && EverySecond.Num() > 5);
        }

        // No hold: it holds Speed to d1 = v^2 / (1.6 a), then brakes, 2 d1 / v.
        for (const double NoHold : { 0.0, -1.0 })
        {
            const double V = 2.0e4;
            const double D1 = V * V / (2.0 * BrakingMargin * Boost);
            TestTrue(FString::Printf(TEXT("a hold of %.0f s: held to the braking curve, then braking"), NoHold),
                     FMath::IsNearlyEqual(SecondsToFloor(1.0e6, V, Boost, NoHold), (1.0e6 - D1) / V + 2.0 * D1 / V, 1e-9));
            TestTrue(TEXT("and under the braking curve already, braking alone"),
                     FMath::IsNearlyEqual(SecondsToFloor(1.0e3, V, Boost, NoHold), 2.0 * FMath::Sqrt(1.0e3 / (2.0 * BrakingMargin * Boost)), 1e-9));
        }
        for (const FCase& Case : { FCase{ TEXT("no hold, 10 km at 200 m/s"), 1.0e6, 2.0e4 },
                                   FCase{ TEXT("no hold, 1,000 km at cruise's 20 km/s"), 1.0e8, 2.0e6 },
                                   FCase{ TEXT("no hold, 250,000 km at 0.1 c"), 2.5e10, 0.1 * C } })
        {
            TArray<TPair<double, double>> EverySecond;
            const double Total = Fly(Case.From, Case.Lever, EverySecond, 0.0);
            double Worst = 0.0;
            for (int32 Second = 0; Second < EverySecond.Num(); ++Second)
            {
                const double Eta = SecondsToFloor(EverySecond[Second].Key, EverySecond[Second].Value, Boost, 0.0);
                Worst = FMath::Max(Worst, FMath::Abs(Eta - (Total - Second)));
            }
            TestTrue(FString::Printf(TEXT("%s: flown in %.1f s, and the ETA agrees to 0.5 s from every second (worst %.3f s)"),
                                     Case.Name, Total, Worst),
                     Worst < 0.5 && EverySecond.Num() > 5);
        }

        bool bRisesWithD = true;
        bool bFallsWithSpeed = true;
        for (const double V : { 1.0e3, 2.0e4, 1.28e6, 2.0e6, 1.0e7, 0.1 * C, C })
        {
            double Previous = 0.0;
            for (double Dist = 1.0; Dist < 1.0e16; Dist *= 1.1)
            {
                const double T = SecondsToFloor(Dist, V, Boost, Hold);
                bRisesWithD &= T > Previous;
                Previous = T;
            }
        }
        for (const double Dist : { 1.0e3, 5.12e6, 1.0e7, 2.5e10, 0.2 * AU, 30.0 * AU })
        {
            double Previous = SecondsToFloor(Dist, 1.0, Boost, Hold);
            for (double V = 1.1; V < C; V *= 1.1)
            {
                const double T = SecondsToFloor(Dist, V, Boost, Hold);
                bFallsWithSpeed &= T <= Previous * (1.0 + 1e-12);
                Previous = T;
            }
        }
        TestTrue(TEXT("farther is later"), bRisesWithD);
        TestTrue(TEXT("faster is never later"), bFallsWithSpeed);

        const double KneeSpeed = 2.0 * BrakingMargin * Boost * Hold;
        TestTrue(TEXT("continuous across the knee's speed"),
                 FMath::IsNearlyEqual(SecondsToFloor(1.0e8, KneeSpeed * (1.0 - 1e-12), Boost, Hold),
                                      SecondsToFloor(1.0e8, KneeSpeed * (1.0 + 1e-12), Boost, Hold), 1e-6));
        TestTrue(TEXT("and across where the cap binds"),
                 FMath::IsNearlyEqual(SecondsToFloor(C * Hold * (1.0 - 1e-12), C, Boost, Hold),
                                      SecondsToFloor(C * Hold * (1.0 + 1e-12), C, Boost, Hold), 1e-6));
    }
    return true;
}

#endif
