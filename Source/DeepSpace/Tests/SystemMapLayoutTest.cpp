#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"
#include "UI/SystemMapLayout.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenPriors.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// Five tests under one path and no test at the path itself: a test path with
// children becomes a group, and a group silently stops running its own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapScaleTest, "DeepSpace.UI.SystemMap.Scale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapWarpTest, "DeepSpace.UI.SystemMap.Warp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapTwelveWorldsFitTest, "DeepSpace.UI.SystemMap.TwelveWorldsFit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapShipOnTheWarpTest, "DeepSpace.UI.SystemMap.ShipOnTheWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapPickTest, "DeepSpace.UI.SystemMap.Pick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SystemMapLayoutTestLocal
{
    using namespace SystemMap;

    /** Far from the origin and off every axis, so nothing passes because the
     *  star happened to sit at zero. */
    const FUniversePosition StarAt(FInt64Vector(3, -7, 2), FVector(1.25e12, -3.5e12, 7.0e11));

    /** A system by hand: a star of luminosity L and one world per orbit, at
     *  the phases given (or a spread of them), giants where asked. */
    FStarSystem MakeSystem(const TArray<double>& OrbitsAU, double Luminosity = 1.0,
                           const TArray<double>& Phases = {}, const TSet<int32>& Giants = {})
    {
        FStarSystem System;
        System.Stub.Id = FSystemId{FInt64Vector(1, 2, 3), 4};
        System.Stub.Name = TEXT("Kessa");
        System.Stub.Position = StarAt;
        System.Stub.LuminositySolar = Luminosity;
        System.Star.LuminositySolar = Luminosity;
        // A main-sequence star's radius goes roughly as L^(1/5) near the Sun;
        // near enough for the standoff's ten-radii guard, which is all that
        // reads it here.
        System.Star.RadiusSolar = FMath::Pow(Luminosity, 0.2);
        for (int32 Orbit = 0; Orbit < OrbitsAU.Num(); ++Orbit)
        {
            FPlanet Planet;
            Planet.Id = FBodyId{System.Stub.Id, Orbit, -1};
            Planet.Designation = FString::Printf(TEXT("Kessa %d"), Orbit + 1);
            Planet.SemiMajorAxisAU = OrbitsAU[Orbit];
            Planet.PhaseRad = Phases.IsValidIndex(Orbit) ? Phases[Orbit] : 0.7 + 2.1 * Orbit;
            Planet.Kind = Giants.Contains(Orbit) ? EPlanetKind::GasGiant : EPlanetKind::Barren;
            Planet.RadiusEarth = Giants.Contains(Orbit) ? 11.0 : 1.0;
            System.Planets.Add(Planet);
        }
        return System;
    }

    /** The rings a pure log map would draw, with no warp. */
    TArray<double> PureLog(const FMapScale& Scale, const FStarSystem& System)
    {
        TArray<double> Rings;
        const double LogIn = FMath::LogX(10.0, Scale.InnerAU);
        const double LogRim = FMath::LogX(10.0, Scale.RimAU);
        for (const FPlanet& Planet : System.Planets)
        {
            Rings.Add(Scale.Pixels.StarPx + (FMath::LogX(10.0, Planet.SemiMajorAxisAU) - LogIn) / (LogRim - LogIn)
                * (Scale.Pixels.RimPx - Scale.Pixels.StarPx));
        }
        return Rings;
    }

    /** Every drawing invariant decision 3 promises, for one system. The
     *  room each dot is capped against is worked out here from the rings
     *  alone, never read back from the dot: a gap the code got wrong must
     *  not be the yardstick it is measured with. */
    bool Fits(FAutomationTestBase& Test, const FStarSystem& System, const TCHAR* What)
    {
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const FMapLayout Drawn = Layout(System, Scale);
        const double Gap = MinRingGap(Scale.Pixels);
        constexpr double Slack = 1.0e-9;
        bool bOk = Test.TestEqual(FString::Printf(TEXT("%s: one ring per world"), What), Scale.RingPx.Num(), System.Planets.Num());
        bOk &= Test.TestEqual(FString::Printf(TEXT("%s: one dot per world"), What), Drawn.Dots.Num(), System.Planets.Num());
        const int32 Worlds = Scale.RingPx.Num();
        for (int32 Ring = 0; bOk && Ring < Worlds; ++Ring)
        {
            const double Inner = Ring == 0 ? Scale.Pixels.StarPx : Scale.RingPx[Ring - 1];
            const double Outer = Ring + 1 < Worlds ? Scale.RingPx[Ring + 1] : Scale.Pixels.RimPx;
            bOk &= Test.TestTrue(FString::Printf(TEXT("%s: ring %d is at least a gap outside what is inside it (%.3f px)"),
                What, Ring, Scale.RingPx[Ring] - Inner), Scale.RingPx[Ring] - Inner >= Gap - Slack);
            bOk &= Test.TestTrue(FString::Printf(TEXT("%s: ring %d is at least a gap inside what is outside it, the rim included (%.3f px)"),
                What, Ring, Outer - Scale.RingPx[Ring]), Outer - Scale.RingPx[Ring] >= Gap - Slack);

            // The spec's local gap: to the ring, star edge or rim either side.
            const double Room = FMath::Min(Scale.RingPx[Ring] - Inner, Outer - Scale.RingPx[Ring]);
            const FMapDot& Dot = Drawn.Dots[Ring];
            bOk &= Test.TestTrue(FString::Printf(TEXT("%s: dot %d is on its ring"), What, Ring),
                FMath::IsNearlyEqual(FVector2D::Distance(Dot.Centre, Scale.Pixels.Centre), Scale.RingPx[Ring], 1.0e-6));
            bOk &= Test.TestTrue(FString::Printf(TEXT("%s: dot %d is drawn, and within its local gap less 2 px (%.2f in %.2f)"),
                What, Ring, Dot.SizePx, Room), Dot.SizePx > 0.0 && Dot.SizePx <= Room - DotClearancePx + Slack);
            bOk &= Test.TestTrue(FString::Printf(TEXT("%s: dot %d's target ring is round it, and within twice its gap less 2 px (%.2f in %.2f)"),
                What, Ring, Dot.TargetRingPx, Room), Dot.TargetRingPx > Dot.SizePx && Dot.TargetRingPx <= 2.0 * Room - DotClearancePx + Slack);
            if (Ring + 1 < Worlds)
            {
                // Two neighbours at the same azimuth, the closest they can be.
                const double Between = Scale.RingPx[Ring + 1] - Scale.RingPx[Ring]
                    - 0.5 * (Dot.SizePx + Drawn.Dots[Ring + 1].SizePx);
                bOk &= Test.TestTrue(FString::Printf(TEXT("%s: dots %d and %d never touch, even in line (%.2f px apart)"),
                    What, Ring, Ring + 1, Between), Between >= DotClearancePx - Slack);
            }
        }

        // The arrival is drawn clear of the outermost ring, by more than
        // the ship's glyph is wide, and a ship coming in from it moves on
        // the map every step of the way to the outermost orbit.
        const double ArrivalAU = NavStart::ArrivalStandoffAU(System, NavStart::DefaultStandoffAU);
        const double ArrivalPx = Scale.RadiusPx(ArrivalAU);
        const double LastPx = Scale.RingPx.IsEmpty() ? Scale.Pixels.StarPx : Scale.RingPx.Last();
        bOk &= Test.TestTrue(FString::Printf(TEXT("%s: the arrival standoff is inside the rim (%.2f px)"), What, ArrivalPx),
            ArrivalPx < Scale.Pixels.RimPx);
        if (!System.Planets.IsEmpty())
        {
            bOk &= Test.TestTrue(FString::Printf(TEXT("%s: the arrival is drawn its glyph's radius clear of the outermost ring (%.2f px)"),
                What, ArrivalPx - LastPx), ArrivalPx - LastPx >= 0.5 * ShipRingPx - Slack);

            const double OutermostAU = System.Planets.Last().SemiMajorAxisAU;
            const FVector Out = FVector(0.6, -0.8, 0.0);
            double Previous = TNumericLimits<double>::Max();
            bool bMoves = true;
            constexpr int32 Steps = 16;
            for (int32 Step = 0; Step <= Steps; ++Step)
            {
                const double AU = FMath::Lerp(ArrivalAU, OutermostAU, static_cast<double>(Step) / Steps);
                const double Px = FVector2D::Distance(
                    Ship(Scale, Scale.Star + Out * AU * UniverseUnits::CmPerAU, FQuat::Identity).Centre, Scale.Pixels.Centre);
                bMoves &= Px < Previous;
                Previous = Px;
            }
            bOk &= Test.TestTrue(FString::Printf(TEXT("%s: coming in from the arrival, the ship moves inward on the map at every step"), What), bMoves);
        }
        return bOk;
    }

    /** The panel direction a universe azimuth should be drawn in: +X up the
     *  glass, +Y to the right. Written out, not borrowed from the code. */
    FVector2D Expected(double Azimuth)
    {
        return FVector2D(FMath::Sin(Azimuth), -FMath::Cos(Azimuth));
    }
}

/**
 * The radial scale: the innermost orbit where the log map puts it, the
 * arrival standoff inside the rim, monotonic in radius, the azimuth exact and
 * the right way round, and a function of the system alone.
 */
bool FSystemMapScaleTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapLayoutTestLocal;

    // Room between every pair of orbits: nothing here is warped.
    const FStarSystem System = MakeSystem({0.4, 1.0, 5.0, 30.0});
    const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);

    TestEqual(TEXT("the inner knot is half the innermost orbit"), Scale.InnerAU, 0.2);
    TestEqual(TEXT("the rim is the arrival standoff with its margin"), Scale.RimAU,
              NavStart::ArrivalStandoffAU(System, NavStart::DefaultStandoffAU) * RimMargin);
    TestEqual(TEXT("the star's edge is the inner knot"), Scale.RadiusPx(Scale.InnerAU), Scale.Pixels.StarPx);
    TestEqual(TEXT("the rim is the rim knot"), Scale.RadiusPx(Scale.RimAU), Scale.Pixels.RimPx);

    const TArray<double> Log = PureLog(Scale, System);
    TestTrue(TEXT("the innermost orbit sits on its log radius"), FMath::IsNearlyEqual(Scale.RingPx[0], Log[0], 1.0e-9));
    TestTrue(TEXT("and is drawn off the star, at the scale's own radius"), FMath::IsNearlyEqual(Scale.RadiusPx(0.4), Scale.RingPx[0], 1.0e-9));

    const double ArrivalAU = NavStart::ArrivalStandoffAU(System, NavStart::DefaultStandoffAU);
    TestTrue(TEXT("the arrival standoff maps inside the rim, outside the last ring"),
             Scale.RadiusPx(ArrivalAU) < Scale.Pixels.RimPx && Scale.RadiusPx(ArrivalAU) > Scale.RingPx.Last());

    // Monotonic across every segment, and strictly so inside the knots.
    double Last = -1.0;
    bool bMonotonic = true;
    bool bStrict = true;
    for (int32 Step = 0; Step <= 4000; ++Step)
    {
        const double AU = Scale.InnerAU * 0.01 * FMath::Pow(10.0, Step * 0.001);
        const double Px = Scale.RadiusPx(AU);
        bMonotonic &= Px >= Last;
        if (AU > Scale.InnerAU && AU < Scale.RimAU && Last >= 0.0 && Step > 0)
        {
            const double Previous = Scale.InnerAU * 0.01 * FMath::Pow(10.0, (Step - 1) * 0.001);
            bStrict &= Previous <= Scale.InnerAU || Px > Last;
        }
        Last = Px;
    }
    TestTrue(TEXT("farther from the star is never drawn nearer"), bMonotonic);
    TestTrue(TEXT("and between the star's edge and the rim, always drawn farther"), bStrict);
    TestEqual(TEXT("inside the inner knot is the star's edge"), Scale.RadiusPx(0.01), Scale.Pixels.StarPx);
    TestEqual(TEXT("past the rim is the rim"), Scale.RadiusPx(1000.0), Scale.Pixels.RimPx);

    // The azimuth is exact, and the map is the right way round: universe +X
    // up the glass, +Y to the right.
    for (const double Azimuth : {0.0, 0.5 * UE_DOUBLE_PI, UE_DOUBLE_PI, -0.5 * UE_DOUBLE_PI, 0.3, 2.2, -1.7})
    {
        const FVector Offset(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0.0);
        const FVector2D Drawn = Place(Scale, StarAt + Offset * 3.0 * UniverseUnits::CmPerAU);
        const FVector2D Dir = (Drawn - Scale.Pixels.Centre).GetSafeNormal();
        TestTrue(FString::Printf(TEXT("azimuth %.2f is drawn at its own angle"), Azimuth),
                 Dir.Equals(Expected(Azimuth), 1.0e-9));
    }
    const FVector2D Up = Place(Scale, StarAt + FVector(3.0 * UniverseUnits::CmPerAU, 0.0, 0.0));
    const FVector2D Right = Place(Scale, StarAt + FVector(0.0, 3.0 * UniverseUnits::CmPerAU, 0.0));
    TestTrue(TEXT("universe +X is up the glass"), Up.Y < Scale.Pixels.Centre.Y && FMath::IsNearlyEqual(Up.X, Scale.Pixels.Centre.X, 1.0e-9));
    TestTrue(TEXT("universe +Y is to the right, not the mirror image"), Right.X > Scale.Pixels.Centre.X);

    // A function of the system alone: nothing about the ship goes in, so
    // asked twice it is the same scale to the bit.
    const FMapScale Again = Fit(System, NavStart::DefaultStandoffAU);
    TestTrue(TEXT("the same system gives the same scale"), Again.KnotLog == Scale.KnotLog && Again.KnotPx == Scale.KnotPx);
    return true;
}

/**
 * The warp: a tight pair drawn exactly MinRingGap apart, a system with room
 * everywhere drawn as the pure log map, and a system crowded against the rim
 * pulled back inside it by the second pass with every gap kept.
 */
bool FSystemMapWarpTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapLayoutTestLocal;

    const double Gap = MinRingGap(FMapPixels());

    // The corpus's tightest spacing, 8%.
    {
        const FStarSystem System = MakeSystem({1.0, 1.08, 10.0});
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const TArray<double> Log = PureLog(Scale, System);
        TestTrue(TEXT("a pure log map would lay the pair closer than the gap (or this proves nothing)"), Log[1] - Log[0] < Gap);
        TestTrue(TEXT("a pair of orbits 8% apart is drawn MinRingGap apart"),
                 FMath::IsNearlyEqual(Scale.RingPx[1] - Scale.RingPx[0], Gap, 1.0e-9));
        TestTrue(TEXT("and the ring with room is left where the log map puts it"),
                 FMath::IsNearlyEqual(Scale.RingPx[2], Log[2], 1.0e-9));
        TestTrue(TEXT("the inner of the pair is not moved"), FMath::IsNearlyEqual(Scale.RingPx[0], Log[0], 1.0e-9));
    }

    // Room everywhere: unwarped.
    {
        const FStarSystem System = MakeSystem({0.4, 1.0, 5.0, 30.0});
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const TArray<double> Log = PureLog(Scale, System);
        bool bSame = true;
        for (int32 Ring = 0; Ring < Log.Num(); ++Ring)
        {
            bSame &= FMath::IsNearlyEqual(Scale.RingPx[Ring], Log[Ring], 1.0e-9);
        }
        TestTrue(TEXT("a system with no tight pair is drawn as the pure log map"), bSame);
    }

    // Eleven orbits bunched just inside the outermost, near the rim: the
    // outward pass alone would push them off it.
    {
        TArray<double> Orbits = {0.1};
        for (int32 Index = 0; Index < 11; ++Index)
        {
            Orbits.Add(20.0 * FMath::Pow(1.03, Index));
        }
        const FStarSystem System = MakeSystem(Orbits);
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const TArray<double> Log = PureLog(Scale, System);
        double Pushed = Log[1];
        for (int32 Ring = 2; Ring < Log.Num(); ++Ring)
        {
            Pushed = FMath::Max(Log[Ring], Pushed + Gap);
        }
        TestTrue(TEXT("pushed outward only, the outermost would be past the rim (or this proves nothing)"),
                 Pushed > Scale.Pixels.RimPx);
        TestTrue(TEXT("the second pass puts the outermost a gap inside the rim, not on it"),
                 FMath::IsNearlyEqual(Scale.RingPx.Last(), Scale.Pixels.RimPx - Gap, 1.0e-9));
        Fits(*this, System, TEXT("crowded against the rim"));
    }
    return true;
}

/**
 * The worst system procgen can make still fits: the gap is derived so twelve
 * rings fit by construction, twelve worlds at the corpus's tightest spacing
 * at either end of its range keep every gap, and so does every system near a
 * generated home.
 */
bool FSystemMapTwelveWorldsFitTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapLayoutTestLocal;

    const FMapPixels Pixels;
    TestEqual(TEXT("MinRingGap is 8 px at the panel's sizes"), MinRingGap(Pixels), 8.0);
    TestTrue(TEXT("a gap per world and one outside the last fit between the star's edge and the rim"),
             MinRingGap(Pixels) * (GenGuarantees::MaxPlanets + 1) <= Pixels.RimPx - Pixels.StarPx);

    const int32 Twelve = GenGuarantees::MaxPlanets;
    TArray<double> Inner;
    TArray<double> Outer;
    for (int32 Orbit = 0; Orbit < Twelve; ++Orbit)
    {
        // The corpus's innermost orbits start at 0.003 AU and its outermost
        // reach 92 AU; 8% apart is its tightest pair.
        Inner.Add(0.003 * FMath::Pow(1.08, Orbit));
        Outer.Add(92.0 / FMath::Pow(1.08, Twelve - 1 - Orbit));
    }
    Fits(*this, MakeSystem(Inner, 0.001), TEXT("twelve tight worlds at the inner end, round a dim red dwarf"));
    Fits(*this, MakeSystem(Outer, 1.0, {}, {8, 9, 10, 11}), TEXT("twelve tight worlds at the outer end, giants outermost"));
    Fits(*this, MakeSystem({0.05}, 0.01), TEXT("one world"));
    Fits(*this, MakeSystem({}, 0.01), TEXT("no worlds"));

    // What procgen actually makes, round a generated home.
    const FGalaxyGenerator Galaxy(0x5EED5EEDull, FGenPriors{});
    const TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(FUniversePosition(), 40.0 * UniverseUnits::CmPerLightYear);
    TestTrue(TEXT("a corpus to check"), Near.Num() > 50);
    int32 Crowded = 0;
    for (const FStarSystemStub& Stub : Near)
    {
        const FStarSystem System = Galaxy.GenerateSystem(Stub);
        Crowded = FMath::Max(Crowded, System.Planets.Num());
        if (!Fits(*this, System, *FString::Printf(TEXT("generated %s"), *Stub.Name)))
        {
            break;
        }
    }
    AddInfo(FString::Printf(TEXT("%d generated systems drawn; the most crowded has %d worlds"), Near.Num(), Crowded));
    return true;
}

/**
 * The ship goes through the same warp as the worlds: on a world's dot at the
 * world, on a ring at its orbit's radius, between two rings between them,
 * held just outside the star's disc inside the inner knot, on the rim past
 * it, and drawn as far out as it is when it is over the pole.
 */
bool FSystemMapShipOnTheWarpTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapLayoutTestLocal;

    const FStarSystem System = MakeSystem({0.4, 1.0, 5.0, 30.0});
    const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
    const FMapLayout Drawn = Layout(System, Scale);
    const FVector2D Centre = Scale.Pixels.Centre;
    const auto From = [Centre](const FMapShip& Ship) { return FVector2D::Distance(Ship.Centre, Centre); };

    for (int32 Orbit = 0; Orbit < System.Planets.Num(); ++Orbit)
    {
        const FMapShip AtWorld = Ship(Scale, System.PlanetPosition(Orbit), FQuat::Identity);
        TestTrue(FString::Printf(TEXT("a ship at world %d is drawn on its dot"), Orbit),
                 AtWorld.Centre.Equals(Drawn.Dots[Orbit].Centre, 1.0e-6));
        TestTrue(TEXT("and is not pinned"), AtWorld.Pin == EMapPin::None);

        const double A = System.Planets[Orbit].SemiMajorAxisAU * UniverseUnits::CmPerAU;
        const FMapShip OnRing = Ship(Scale, StarAt + FVector(-0.6, 0.8, 0.0) * A, FQuat::Identity);
        TestTrue(FString::Printf(TEXT("a ship at orbit %d's radius is drawn on its ring"), Orbit),
                 FMath::IsNearlyEqual(From(OnRing), Scale.RingPx[Orbit], 1.0e-6));
    }

    const FMapShip Between = Ship(Scale, StarAt + FVector(0.0, -3.0 * UniverseUnits::CmPerAU, 0.0), FQuat::Identity);
    TestTrue(TEXT("a ship between orbits II and III is drawn between their rings"),
             From(Between) > Scale.RingPx[1] && From(Between) < Scale.RingPx[2]);

    // Between the star's surface and the inner knot (0.2 AU here).
    const FMapShip Inside = Ship(Scale, StarAt + FVector(0.1 * UniverseUnits::CmPerAU, 0.0, 0.0), FQuat::Identity);
    TestTrue(TEXT("inside the inner knot the ship says it is inside the innermost orbit"), Inside.Pin == EMapPin::Inside);
    TestTrue(TEXT("and is drawn just outside the star's disc, not on it"),
             From(Inside) > Scale.Pixels.StarPx && From(Inside) <= Scale.Pixels.StarPx + ShipRingPx && From(Inside) < Scale.RingPx[0]);
    TestTrue(TEXT("at its true azimuth"), (Inside.Centre - Centre).GetSafeNormal().Equals(FVector2D(0.0, -1.0), 1.0e-9));

    const FMapShip Beyond = Ship(Scale, StarAt + FVector(0.0, 2.0 * Scale.RimAU * UniverseUnits::CmPerAU, 0.0), FQuat::Identity);
    TestTrue(TEXT("past the rim the ship says it is beyond the map"), Beyond.Pin == EMapPin::Beyond);
    TestTrue(TEXT("and is held on the rim"), FMath::IsNearlyEqual(From(Beyond), Scale.Pixels.RimPx, 1.0e-6));

    // Top-down (developer's ruling, 2026-09-27): the plane distance places
    // the glyph, and the elevation is the footer's.
    const FMapShip Pole = Ship(Scale, StarAt + FVector(0.0, 0.0, 2.0 * UniverseUnits::CmPerAU), FQuat::Identity);
    TestTrue(TEXT("2 AU over the pole is held at the star's edge"),
             Pole.Pin == EMapPin::Inside && FMath::IsNearlyEqual(From(Pole), Scale.Pixels.StarPx + 0.5 * ShipRingPx, 1.0e-6));
    TestTrue(TEXT("and reports 90 degrees above the plane"), FMath::IsNearlyEqual(Pole.ElevationDeg, 90.0, 1.0e-6));
    const FMapShip OverRing = Ship(Scale, StarAt + FVector(-0.6, 0.8, 0.0) * (1.0 * UniverseUnits::CmPerAU)
                                              + FVector(0.0, 0.0, 3.0 * UniverseUnits::CmPerAU), FQuat::Identity);
    TestTrue(TEXT("3 AU over orbit II's ring is drawn on the ring"),
             FMath::IsNearlyEqual(From(OverRing), Scale.RingPx[1], 1.0e-6) && OverRing.Pin == EMapPin::None);

    const FMapShip Below = Ship(Scale, StarAt + FVector(1.0, 0.0, -1.0) * UniverseUnits::CmPerAU, FQuat::Identity);
    TestTrue(TEXT("below the plane is negative"), FMath::IsNearlyEqual(Below.ElevationDeg, -45.0, 1.0e-6));

    // The nose, projected into the plane.
    const FUniversePosition Out = StarAt + FVector(0.0, 3.0 * UniverseUnits::CmPerAU, 0.0);
    const FMapShip Forward = Ship(Scale, Out, FQuat::Identity);
    TestTrue(TEXT("a nose along universe +X points up the glass"), Forward.Nose.IsSet() && Forward.Nose->Equals(FVector2D(0.0, -1.0), 1.0e-9));
    const FMapShip Starboard = Ship(Scale, Out, FRotator(0.0, 90.0, 0.0).Quaternion());
    TestTrue(TEXT("yawed to +Y it points right"), Starboard.Nose.IsSet() && Starboard.Nose->Equals(FVector2D(1.0, 0.0), 1.0e-9));
    const FMapShip Climbing = Ship(Scale, Out, FRotator(65.0, 0.0, 0.0).Quaternion());
    TestTrue(TEXT("pitched 65 degrees, 25 from vertical, it still has a direction"), Climbing.Nose.IsSet());
    const FMapShip Vertical = Ship(Scale, Out, FRotator(80.0, 30.0, 0.0).Quaternion());
    TestFalse(TEXT("within 20 degrees of vertical the glyph is a ring alone"), Vertical.Nose.IsSet());
    return true;
}

/**
 * Picking: a dot picks itself, two dots 9 px apart split at the midpoint
 * with an exact tie to the inner, nothing 15 px from every dot, nothing on
 * the star even with a dot in reach -- and what a pick asks the ship.
 */
bool FSystemMapPickTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapLayoutTestLocal;

    FMapLayout Drawn;
    const FVector2D Centre = Drawn.Pixels.Centre;
    FMapDot A;
    A.Orbit = 0;
    A.Centre = Centre + FVector2D(0.0, -40.0);
    FMapDot B;
    B.Orbit = 1;
    B.Centre = Centre + FVector2D(0.0, -49.0);
    Drawn.Dots = {A, B};

    TestTrue(TEXT("a click on a dot picks it"), Pick(Drawn, A.Centre) == TOptional<int32>(0));
    TestTrue(TEXT("4 px from A toward B picks A"), Pick(Drawn, A.Centre + FVector2D(0.0, -4.0)) == TOptional<int32>(0));
    TestTrue(TEXT("5 px from A toward B picks B"), Pick(Drawn, A.Centre + FVector2D(0.0, -5.0)) == TOptional<int32>(1));
    TestTrue(TEXT("an exact tie picks the inner"), Pick(Drawn, A.Centre + FVector2D(0.0, -4.5)) == TOptional<int32>(0));
    TestTrue(TEXT("14 px from A, on the side away from B, still picks A"), Pick(Drawn, A.Centre + FVector2D(0.0, 14.0)) == TOptional<int32>(0));
    TestFalse(TEXT("15 px from every dot picks nothing"), Pick(Drawn, A.Centre + FVector2D(15.0, 0.0)).IsSet());

    // A first ring a gap outside the star's edge: the star's disc is within
    // 14 px of its dot, and still picks nothing.
    FMapLayout Tight;
    FMapDot C;
    C.Orbit = 0;
    C.Centre = Centre + FVector2D(0.0, -(Tight.Pixels.StarPx + MinRingGap(Tight.Pixels)));
    Tight.Dots = {C};
    TestTrue(TEXT("the star is within reach of the first dot (or this proves nothing)"),
             FVector2D::Distance(Centre + FVector2D(0.0, -3.0), C.Centre) < PickRadius);
    TestFalse(TEXT("a click on the star picks nothing"), Pick(Tight, Centre + FVector2D(0.0, -3.0)).IsSet());
    TestFalse(TEXT("nor its very centre"), Pick(Tight, Centre).IsSet());
    TestTrue(TEXT("just off the star's edge, toward the dot, picks it"),
             Pick(Tight, Centre + FVector2D(0.0, -(Tight.Pixels.StarPx + 0.5))) == TOptional<int32>(0));

    // On a laid-out system, each dot picks its own world.
    const FStarSystem System = MakeSystem({1.0, 1.08, 1.17, 10.0});
    const FMapLayout Laid = Layout(System, Fit(System, NavStart::DefaultStandoffAU));
    for (const FMapDot& Dot : Laid.Dots)
    {
        TestTrue(FString::Printf(TEXT("laid out, dot %d picks world %d"), Dot.Orbit, Dot.Orbit), Pick(Laid, Dot.Centre) == TOptional<int32>(Dot.Orbit));
    }

    // What a pick asks the ship: the chart's rule, for worlds.
    const FBodyId Second{System.Stub.Id, 1, -1};
    const TOptional<FMapSelection> Fresh = Select(System, 1, {});
    TestTrue(TEXT("with no target, a world is targeted"), Fresh && Fresh->Action == EMapSelect::Target && Fresh->Body == Second);
    const TOptional<FMapSelection> Again = Select(System, 1, Second);
    TestTrue(TEXT("the target again is cleared"), Again && Again->Action == EMapSelect::Clear && Again->Body == Second);
    const TOptional<FMapSelection> Other = Select(System, 0, Second);
    TestTrue(TEXT("another world replaces it"), Other && Other->Action == EMapSelect::Target && Other->Body.Planet == 0);
    FBodyId Elsewhere = Second;
    Elsewhere.System.Slot += 1;
    const TOptional<FMapSelection> Foreign = Select(System, 1, Elsewhere);
    TestTrue(TEXT("a target in another system is not this world: it is targeted, not cleared"),
             Foreign && Foreign->Action == EMapSelect::Target);
    TestFalse(TEXT("an orbit the system lacks asks nothing"), Select(System, 4, {}).IsSet());
    TestFalse(TEXT("nor a negative one"), Select(System, -1, {}).IsSet());
    return true;
}

#endif
