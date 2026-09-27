#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"
#include "UI/SystemMapLayout.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenPriors.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// Siblings of DeepSpace.UI.SystemMap's other tests; nothing at the group's
// own path, which would silently stop running.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapStraightApproachTest, "DeepSpace.UI.SystemMap.StraightApproach",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapTickFollowsGlyphTest, "DeepSpace.UI.SystemMap.TickFollowsGlyph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapBearingTest, "DeepSpace.UI.SystemMap.Bearing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapHeldTickTest, "DeepSpace.UI.SystemMap.HeldTick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSystemMapRadiusSlopeTest, "DeepSpace.UI.SystemMap.RadiusSlope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SystemMapTrackingTestLocal
{
    using namespace SystemMap;

    constexpr double AU = UniverseUnits::CmPerAU;

    /** Steps along each flight: fine enough that a glyph stepping back
     *  between two of them by more than rounding would be caught. */
    constexpr int32 Steps = 200;

    /** The systems procgen makes round a generated home, and one by hand
     *  whose star is far from the origin and off every axis. */
    TArray<FStarSystem> Corpus()
    {
        TArray<FStarSystem> Systems;
        const FGalaxyGenerator Galaxy(0x5EED5EEDull, FGenPriors{});
        for (const FStarSystemStub& Stub : Galaxy.FindSystemsWithin(FUniversePosition(), 20.0 * UniverseUnits::CmPerLightYear))
        {
            FStarSystem System = Galaxy.GenerateSystem(Stub);
            if (System.Planets.Num() > 0)
            {
                Systems.Add(MoveTemp(System));
            }
        }

        FStarSystem Hand;
        Hand.Stub.Id = FSystemId{FInt64Vector(1, 2, 3), 4};
        Hand.Stub.Position = FUniversePosition(FInt64Vector(3, -7, 2), FVector(1.25e12, -3.5e12, 7.0e11));
        Hand.Stub.LuminositySolar = Hand.Star.LuminositySolar = 1.0;
        Hand.Star.RadiusSolar = 1.0;
        const double Orbits[] = {0.4, 0.7, 1.0, 1.6, 5.0, 30.0};
        for (int32 Orbit = 0; Orbit < UE_ARRAY_COUNT(Orbits); ++Orbit)
        {
            FPlanet Planet;
            Planet.Id = FBodyId{Hand.Stub.Id, Orbit, -1};
            Planet.SemiMajorAxisAU = Orbits[Orbit];
            Planet.PhaseRad = 0.7 + 2.1 * Orbit;
            Planet.RadiusEarth = 1.0;
            Hand.Planets.Add(Planet);
        }
        Systems.Add(MoveTemp(Hand));
        return Systems;
    }

    /** Where the flights toward a world begin: inside, on and outside its
     *  orbit and near the rim, all round it, in the plane or out of it. */
    TArray<FUniversePosition> Starts(const FStarSystem& System, const FMapScale& Scale, int32 Orbit, bool bOffPlane)
    {
        TArray<FUniversePosition> Out;
        const double A = System.Planets[Orbit].SemiMajorAxisAU;
        for (const double RadiusAU : {0.6 * A, A, 1.7 * A, 0.9 * Scale.RimAU})
        {
            for (int32 Spoke = 0; Spoke < 8; ++Spoke)
            {
                const double Azimuth = 0.3 + Spoke * UE_DOUBLE_PI / 4.0;
                const double Height = bOffPlane ? (Spoke % 2 == 0 ? 0.6 : -0.9) * RadiusAU : 0.0;
                Out.Add(System.Stub.Position + FVector(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0.0) * (RadiusAU * AU)
                        + FVector(0.0, 0.0, Height * AU));
            }
        }
        return Out;
    }

    /** 16 interstellar arrival points: at the standoff, spread over the
     *  sphere (a Fibonacci lattice), elevations from pole to pole and
     *  azimuths all round. */
    TArray<FUniversePosition> Arrivals(const FStarSystem& System)
    {
        TArray<FUniversePosition> Out;
        const double Standoff = NavStart::ArrivalStandoffAU(System, NavStart::DefaultStandoffAU) * AU;
        for (int32 Arrival = 0; Arrival < 16; ++Arrival)
        {
            const double Up = 1.0 - (Arrival + 0.5) / 8.0;
            const double Azimuth = Arrival * 2.39996322972865332;
            const double Flat = FMath::Sqrt(FMath::Max(0.0, 1.0 - Up * Up));
            Out.Add(System.Stub.Position + FVector(Flat * FMath::Cos(Azimuth), Flat * FMath::Sin(Azimuth), Up) * Standoff);
        }
        return Out;
    }

    /** The distance from the star in the plane, AU: what places the glyph. */
    double InPlaneAU(const FStarSystem& System, const FUniversePosition& Where)
    {
        const FVector Offset = Where - System.Stub.Position;
        return FVector2D(Offset.X, Offset.Y).Size() / AU;
    }

    /** Whether the glyph at a distance in the plane is held -- pinned, or at
     *  the floor clear of the star's disc -- so its radius cannot move. */
    bool IsHeld(const FMapScale& Scale, double DistanceAU)
    {
        return DistanceAU <= Scale.InnerAU || DistanceAU >= Scale.RimAU
            || Scale.RadiusPx(DistanceAU) < Scale.Pixels.StarPx + 0.5 * ShipRingPx;
    }

    /** Signed angle between two panel directions, degrees, magnitude only. */
    double AngleDeg(const FVector2D& A, const FVector2D& B)
    {
        return FMath::RadiansToDegrees(FMath::Abs(FMath::Atan2(A ^ B, A | B)));
    }

    FQuat Facing(const FVector& Direction)
    {
        return FRotationMatrix::MakeFromX(Direction.GetSafeNormal()).ToQuat();
    }
}

/**
 * Flying straight at a world, from in the plane or out of it, the ship's
 * glyph never moves away from that world's dot, and it is on the dot at the
 * world. Checked for every world of every system round a generated home,
 * from 32 starts in the plane and 32 off it, and from 16 more placed as an
 * interstellar arrival is: at the standoff, from anywhere round the star.
 * Off the plane this holds because the glyph is placed top-down
 * (developer's ruling, 2026-09-27); by the 3D distance it did not.
 */
bool FSystemMapStraightApproachTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapTrackingTestLocal;

    const TArray<FStarSystem> Systems = Corpus();
    TestTrue(TEXT("a corpus to fly"), Systems.Num() > 10);

    int32 Flights = 0;
    int32 Retreats = 0;
    int32 Misses = 0;
    double WorstRetreatPx = 0.0;
    for (const FStarSystem& System : Systems)
    {
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const FMapLayout Drawn = Layout(System, Scale);
        for (int32 Orbit = 0; Orbit < System.Planets.Num(); ++Orbit)
        {
            const FUniversePosition World = System.PlanetPosition(Orbit);
            const FVector2D Dot = Drawn.Dots[Orbit].Centre;
            TArray<FUniversePosition> From = Starts(System, Scale, Orbit, false);
            From.Append(Starts(System, Scale, Orbit, true));
            From.Append(Arrivals(System));
            for (const FUniversePosition& Start : From)
            {
                const FVector Leg = World - Start;
                if (Leg.Size() < 1.0e-3 * AU)
                {
                    continue;
                }
                ++Flights;
                const FQuat Nose = Facing(Leg);
                double Previous = TNumericLimits<double>::Max();
                bool bRetreated = false;
                for (int32 Step = 0; Step <= Steps; ++Step)
                {
                    const FUniversePosition Where = Step == Steps ? World : Start + Leg * (double(Step) / Steps);
                    const double Gap = FVector2D::Distance(Ship(Scale, Where, Nose).Centre, Dot);
                    if (Gap > Previous + 1.0e-6)
                    {
                        bRetreated = true;
                        WorstRetreatPx = FMath::Max(WorstRetreatPx, Gap - Previous);
                    }
                    Previous = Gap;
                }
                Retreats += bRetreated ? 1 : 0;
                Misses += Previous > 1.0e-6 ? 1 : 0;
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d straight flights, in the plane and off it, over %d systems"), Flights, Systems.Num()));
    TestTrue(TEXT("flights to check"), Flights > 1000);
    TestEqual(FString::Printf(TEXT("flights whose glyph moved away from the dot it was closing on (worst %.3f px)"), WorstRetreatPx),
              Retreats, 0);
    TestEqual(TEXT("flights that ended off the world's dot"), Misses, 0);
    return true;
}

/**
 * The ship's tick is the way its glyph moves. Flying nose first -- straight
 * at a world, from in the plane, out of it and from an interstellar arrival,
 * and straight away from it again, so the flights leave the world's own knot
 * outward as well as inward -- the glyph's next step on the map is along the
 * tick, to within a degree, wherever the glyph is free to move (a held one
 * is DeepSpace.UI.SystemMap.HeldTick's); and near a world, with the nose on
 * it, the tick points at its dot. Drawn in universe directions, the tick was
 * up to 90 degrees off the glyph's motion and ~50 off the dot: the map is
 * stretched round each ring several times more than across it.
 */
bool FSystemMapTickFollowsGlyphTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapTrackingTestLocal;

    const TArray<FStarSystem> Systems = Corpus();
    int32 Checked = 0;
    int32 Astray = 0;
    double Worst = 0.0;
    int32 OnKnotOutward = 0;
    int32 OnKnotInward = 0;
    int32 NearChecked = 0;
    int32 NearAstray = 0;
    double NearWorst = 0.0;
    for (const FStarSystem& System : Systems)
    {
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const FMapLayout Drawn = Layout(System, Scale);
        for (int32 Orbit = 0; Orbit < System.Planets.Num(); ++Orbit)
        {
            const FUniversePosition World = System.PlanetPosition(Orbit);
            const double A = System.Planets[Orbit].SemiMajorAxisAU * AU;
            TArray<FUniversePosition> From = Starts(System, Scale, Orbit, false);
            From.Append(Starts(System, Scale, Orbit, true));
            From.Append(Arrivals(System));

            const auto Fly = [&](const FUniversePosition& Start, const FVector& Leg, bool bFromWorld)
            {
                const FQuat Nose = Facing(Leg);
                for (int32 Step = 0; Step <= Steps; ++Step)
                {
                    // Exactly on the world at one end: the knot itself.
                    const bool bAtWorld = bFromWorld ? Step == 0 : Step == Steps;
                    const FUniversePosition Where = bAtWorld ? World : Start + Leg * (double(Step) / Steps);
                    const FMapShip Glyph = Ship(Scale, Where, Nose);
                    if (!Glyph.Nose || IsHeld(Scale, InPlaneAU(System, Where)))
                    {
                        continue;
                    }
                    const FVector2D Moved = Ship(Scale, Where + Leg * 1.0e-7, Nose).Centre - Glyph.Centre;
                    if (Moved.Size() < 1.0e-6)
                    {
                        continue;
                    }
                    ++Checked;
                    if (bAtWorld)
                    {
                        const FVector ToStar = System.Stub.Position - World;
                        ((FVector2D(Leg.X, Leg.Y) | FVector2D(ToStar.X, ToStar.Y)) < 0.0 ? OnKnotOutward : OnKnotInward) += 1;
                    }
                    const double Off = AngleDeg(*Glyph.Nose, Moved);
                    Worst = FMath::Max(Worst, Off);
                    Astray += Off > 1.0 ? 1 : 0;
                }
            };
            for (const FUniversePosition& Start : From)
            {
                const FVector Leg = World - Start;
                if (Leg.Size() < 1.0e-3 * AU)
                {
                    continue;
                }
                Fly(Start, Leg, false);
                Fly(World, -Leg, true);
            }

            // Near the world, within 5% of its orbit, from all round it: the
            // nose on the world puts the tick on its dot.
            for (int32 Spoke = 0; Spoke < 16; ++Spoke)
            {
                const double Azimuth = 0.2 + Spoke * UE_DOUBLE_PI / 8.0;
                for (const double Reach : {0.001, 0.01, 0.05})
                {
                    const FUniversePosition Where = World + FVector(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0.0) * (Reach * A);
                    const FMapShip Glyph = Ship(Scale, Where, Facing(World - Where));
                    const FVector2D ToDot = Drawn.Dots[Orbit].Centre - Glyph.Centre;
                    if (!Glyph.Nose || ToDot.Size() < 1.0e-6)
                    {
                        continue;
                    }
                    ++NearChecked;
                    const double Off = AngleDeg(*Glyph.Nose, ToDot);
                    NearWorst = FMath::Max(NearWorst, Off);
                    NearAstray += Off > 1.5 ? 1 : 0;
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d steps checked (%d leaving a world's knot outward, %d inward), worst %.3f deg; %d near a world, worst %.3f deg"),
                            Checked, OnKnotOutward, OnKnotInward, Worst, NearChecked, NearWorst));
    TestTrue(TEXT("steps to check"), Checked > 10000 && NearChecked > 1000);
    TestTrue(TEXT("steps exactly on a world's knot, leaving it outward and inward"), OnKnotOutward > 100 && OnKnotInward > 100);
    TestEqual(TEXT("steps where the glyph moved more than a degree off its tick"), Astray, 0);
    TestEqual(TEXT("near a world, noses on it whose tick missed its dot by more than 1.5 degrees"), NearAstray, 0);
    return true;
}

/**
 * Bearings on the map. A radial warp keeps two bearings exactly: straight
 * toward or away from the star, and between two points the same distance
 * from it -- so a world on the ship's own orbit, or on its line to the star,
 * is where it truly is -- off the plane too, since the ship is drawn
 * top-down. Any other bearing is bent by the warp, and bounded by nothing but
 * it. The developer ruled the bent bearings kept as built (2026-09-27), and
 * CLAUDE.md and the map spec quote their spread (median ~4, up to ~50
 * degrees), so the spread is held to that, both ways: a change that re-warps
 * the map, better or worse, fails here and has to be a deliberate one, with
 * the docs and the ruling revisited.
 */
bool FSystemMapBearingTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapTrackingTestLocal;

    const TArray<FStarSystem> Systems = Corpus();
    const auto PanelBearing = [](const FVector& Truth) { return FVector2D(Truth.Y, -Truth.X).GetSafeNormal(); };

    int32 Kept = 0;
    int32 Broken = 0;
    TArray<double> Bent;
    for (const FStarSystem& System : Systems)
    {
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const FMapLayout Drawn = Layout(System, Scale);
        for (int32 Orbit = 0; Orbit < System.Planets.Num(); ++Orbit)
        {
            const FUniversePosition World = System.PlanetPosition(Orbit);
            const FVector FromStar = World - System.Stub.Position;
            const FVector2D Dot = Drawn.Dots[Orbit].Centre;
            const auto Check = [&](const FUniversePosition& Where)
            {
                const FVector2D OnMap = Dot - Ship(Scale, Where, FQuat::Identity).Centre;
                const double Off = AngleDeg(OnMap, PanelBearing(World - Where));
                ++Kept;
                Broken += Off > 0.01 ? 1 : 0;
            };

            // On the world's line to the star, inside and outside it (clear
            // of the pins at either end).
            for (const double Along : {0.7, 1.3})
            {
                const double DistanceAU = Along * FromStar.Size() / AU;
                if (DistanceAU > Scale.InnerAU * 1.01 && DistanceAU < Scale.RimAU * 0.99)
                {
                    Check(System.Stub.Position + FromStar * Along);
                }
            }
            // On its orbit, elsewhere round it.
            for (const double Turn : {0.05, 0.4, 1.5, 3.0})
            {
                Check(System.Stub.Position + FromStar.RotateAngleAxisRad(Turn, FVector::UpVector));
            }

            // And off the plane, over either: top-down, a ship above the
            // world's orbit is on its ring and above its line to the star is
            // on that line (developer's ruling, 2026-09-27). Drawn by the 3D
            // distance, as the spec first chose, the orbit's bearing broke.
            for (const double Height : {0.5, -1.2})
            {
                const FVector Up(0.0, 0.0, Height * FromStar.Size());
                for (const double Turn : {0.05, 0.4, 1.5, 3.0})
                {
                    Check(System.Stub.Position + FromStar.RotateAngleAxisRad(Turn, FVector::UpVector) + Up);
                }
                for (const double Along : {0.7, 1.3})
                {
                    const double DistanceAU = Along * FromStar.Size() / AU;
                    if (DistanceAU > Scale.InnerAU * 1.01 && DistanceAU < Scale.RimAU * 0.99)
                    {
                        Check(System.Stub.Position + FromStar * Along + Up);
                    }
                }
            }

            // Anywhere else in the plane: bent by the warp.
            for (int32 Spoke = 0; Spoke < 8; ++Spoke)
            {
                const double Azimuth = 0.3 + Spoke * UE_DOUBLE_PI / 4.0;
                for (const double Radius : {0.6, 1.7})
                {
                    const double RadiusAU = Radius * FromStar.Size() / AU;
                    if (RadiusAU <= Scale.InnerAU || RadiusAU >= Scale.RimAU)
                    {
                        continue;
                    }
                    const FUniversePosition Where = System.Stub.Position
                        + FVector(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0.0) * (RadiusAU * AU);
                    Bent.Add(AngleDeg(Dot - Ship(Scale, Where, FQuat::Identity).Centre, PanelBearing(World - Where)));
                }
            }
        }
    }
    TestTrue(TEXT("bearings to check"), Kept > 500);
    TestEqual(TEXT("radial and same-orbit bearings the map bent by more than 0.01 degrees"), Broken, 0);

    Bent.Sort();
    TestTrue(TEXT("bent bearings to measure"), Bent.Num() > 1000);
    if (Bent.Num() > 0)
    {
        const auto At = [&Bent](double Q) { return Bent[FMath::Min(Bent.Num() - 1, int32(Q * Bent.Num()))]; };
        AddInfo(FString::Printf(TEXT("any other in-plane bearing, map vs truth, over %d: median %.1f, p90 %.1f, max %.1f deg"),
                                Bent.Num(), At(0.5), At(0.9), Bent.Last()));
        // As built, over this corpus: median 4.2, p90 16.0, max 49.3.
        TestTrue(TEXT("the bent bearings' median is the ~4 degrees the docs quote"), At(0.5) > 3.5 && At(0.5) < 5.0);
        TestTrue(TEXT("their 90th percentile is as built"), At(0.9) > 13.0 && At(0.9) < 19.0);
        TestTrue(TEXT("and the worst is the ~50 degrees the docs quote"), Bent.Last() > 45.0 && Bent.Last() < 55.0);
    }
    return true;
}

/**
 * Where the glyph is held -- pinned inside the inner knot or past the rim, or
 * at the floor clear of the star's disc -- its radius cannot move, and the
 * tick is the way it would move were it free (SystemMap::MotionOnMap). So it
 * turns smoothly with the nose: swinging the nose a degree at a time all the
 * way round, the tick never jumps, and a nose straight out draws a tick
 * straight out. And it meets the free glyph's tick where each hold lets go:
 * at the inner knot, where the floor does, and at the rim. Checked all round
 * every system of the corpus, in the plane and off it, and from every
 * interstellar arrival point, which top-down often starts held. Taken from
 * the true derivative, which has no radial part there, a nose a degree off
 * radial drew a tick square to it, and one exactly radial fell back to the
 * nose: 90 degrees for a degree of heading.
 */
bool FSystemMapHeldTickTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapTrackingTestLocal;

    /** The most a tick may turn for a degree of heading: the warp's own
     *  anisotropy, bounded well under the flip it replaced. */
    constexpr double MaxTurnDeg = 30.0;

    const TArray<FStarSystem> Systems = Corpus();
    int32 Swept = 0;
    int32 HeldSwept = 0;
    int32 Missing = 0;
    int32 Jumps = 0;
    double WorstTurn = 0.0;
    int32 NotRadial = 0;
    int32 Seams = 0;
    int32 Torn = 0;
    double WorstSeam = 0.0;
    for (const FStarSystem& System : Systems)
    {
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const double FloorPx = Scale.Pixels.StarPx + 0.5 * ShipRingPx;

        // Where the floor lets go, between the inner knot and orbit I.
        double Lo = Scale.InnerAU;
        double Hi = System.Planets[0].SemiMajorAxisAU;
        for (int32 Halving = 0; Halving < 200; ++Halving)
        {
            const double Mid = FMath::Sqrt(Lo * Hi);
            (Scale.RadiusPx(Mid) < FloorPx ? Lo : Hi) = Mid;
        }
        const double FloorLetsGoAU = Hi;

        TArray<FUniversePosition> Where;
        for (int32 Spoke = 0; Spoke < 8; ++Spoke)
        {
            const double Azimuth = 0.3 + Spoke * UE_DOUBLE_PI / 4.0;
            const FVector Out(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0.0);
            for (const double DistanceAU : {0.5 * Scale.InnerAU, Scale.InnerAU * (1.0 + 1.0e-6),
                                            FMath::Sqrt(Scale.InnerAU * FloorLetsGoAU), 1.3 * Scale.RimAU})
            {
                for (const double Height : {0.0, 0.7})
                {
                    Where.Add(System.Stub.Position + (Out + FVector(0.0, 0.0, Height)) * (DistanceAU * AU));
                }
            }
        }
        Where.Append(Arrivals(System));

        for (const FUniversePosition& At : Where)
        {
            const FVector Offset = At - System.Stub.Position;
            const FVector Radial = FVector(Offset.X, Offset.Y, 0.0).GetSafeNormal();
            const FVector2D PanelOut = FVector2D(Radial.Y, -Radial.X);
            const bool bHeld = IsHeld(Scale, InPlaneAU(System, At));
            for (const double ClimbDeg : {0.0, 40.0})
            {
                ++Swept;
                HeldSwept += bHeld ? 1 : 0;
                TOptional<FVector2D> Previous;
                for (int32 Rel = -180; Rel <= 180; ++Rel)
                {
                    const FVector Flat = Radial.RotateAngleAxis(double(Rel), FVector::UpVector);
                    const FVector Nose = Flat * FMath::Cos(FMath::DegreesToRadians(ClimbDeg))
                                       + FVector::UpVector * FMath::Sin(FMath::DegreesToRadians(ClimbDeg));
                    const FMapShip Glyph = Ship(Scale, At, Facing(Nose));
                    if (!Glyph.Nose)
                    {
                        ++Missing;
                        Previous.Reset();
                        continue;
                    }
                    if (Previous)
                    {
                        const double Turn = AngleDeg(*Previous, *Glyph.Nose);
                        WorstTurn = FMath::Max(WorstTurn, Turn);
                        Jumps += Turn > MaxTurnDeg ? 1 : 0;
                    }
                    if (Rel == 0)
                    {
                        NotRadial += AngleDeg(*Glyph.Nose, PanelOut) > 1.0e-3 ? 1 : 0;
                    }
                    Previous = Glyph.Nose;
                }
            }
        }

        // Each hold's edge: just inside and just outside, one tick.
        for (const double EdgeAU : {Scale.InnerAU, FloorLetsGoAU, Scale.RimAU})
        {
            for (int32 Spoke = 0; Spoke < 4; ++Spoke)
            {
                const double Azimuth = 0.7 + Spoke * UE_DOUBLE_PI / 2.0;
                const FVector Out(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0.0);
                for (const double Rel : {0.0, 10.0, 35.0, 60.0, 89.0, 120.0, 150.0, 179.0, -45.0, -100.0})
                {
                    const FQuat Nose = Facing(Out.RotateAngleAxis(Rel, FVector::UpVector));
                    const FMapShip In = Ship(Scale, System.Stub.Position + Out * (EdgeAU * (1.0 - 1.0e-9) * AU), Nose);
                    const FMapShip Past = Ship(Scale, System.Stub.Position + Out * (EdgeAU * (1.0 + 1.0e-9) * AU), Nose);
                    if (!In.Nose || !Past.Nose)
                    {
                        ++Missing;
                        continue;
                    }
                    ++Seams;
                    const double Off = AngleDeg(*In.Nose, *Past.Nose);
                    WorstSeam = FMath::Max(WorstSeam, Off);
                    Torn += Off > 0.1 ? 1 : 0;
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d sweeps of the nose (%d held), worst turn %.2f deg per degree; %d seams, worst %.4f deg"),
                            Swept, HeldSwept, WorstTurn, Seams, WorstSeam));
    TestTrue(TEXT("held glyphs to sweep"), HeldSwept > 1000 && Seams > 1000);
    TestEqual(TEXT("noses off vertical with no tick"), Missing, 0);
    TestEqual(FString::Printf(TEXT("degrees of heading that turned the tick more than %.0f degrees"), MaxTurnDeg), Jumps, 0);
    TestEqual(TEXT("noses straight out whose tick was not straight out"), NotRadial, 0);
    TestEqual(TEXT("hold edges where the tick jumped by more than 0.1 degrees"), Torn, 0);
    return true;
}

/**
 * The warp's slope, which the tick's radial part is made from. At every
 * world's own knot -- the distance a ship at the world computes, not the
 * knot's -- moving outward it is the slope of the segment outside and moving
 * inward the one inside, each the finite difference of RadiusPx on that
 * side. Zero on the far side of either clamp; and continued past both
 * (WarpPxPerDex), the first and last segments' own.
 */
bool FSystemMapRadiusSlopeTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapTrackingTestLocal;

    constexpr double H = 1.0e-7;
    const TArray<FStarSystem> Systems = Corpus();
    int32 Knots = 0;
    int32 Kinked = 0;
    int32 Wrong = 0;
    for (const FStarSystem& System : Systems)
    {
        const FMapScale Scale = Fit(System, NavStart::DefaultStandoffAU);
        const auto Forward = [&Scale](double D) { return (Scale.RadiusPx(D * (1.0 + H)) - Scale.RadiusPx(D)) / (D * H); };
        const auto Backward = [&Scale](double D) { return (Scale.RadiusPx(D) - Scale.RadiusPx(D * (1.0 - H))) / (D * H); };
        const auto Agrees = [&Wrong](double Got, double Want)
        {
            const bool bAgrees = FMath::Abs(Got - Want) <= 1.0e-4 * FMath::Abs(Want) + 1.0e-12;
            Wrong += bAgrees ? 0 : 1;
            return bAgrees;
        };

        // Clear of knots that sit on top of one another: no segment there.
        const auto Distinct = [&Scale](int32 Knot)
        {
            return (Knot == 0 || Scale.KnotLog[Knot] - Scale.KnotLog[Knot - 1] > 1.0e-6)
                && (Knot == Scale.KnotLog.Num() - 1 || Scale.KnotLog[Knot + 1] - Scale.KnotLog[Knot] > 1.0e-6);
        };
        for (int32 Orbit = 0; Orbit < System.Planets.Num(); ++Orbit)
        {
            if (!Distinct(Orbit + 1))
            {
                continue;
            }
            const double D = InPlaneAU(System, System.PlanetPosition(Orbit));
            ++Knots;
            Kinked += FMath::Abs(Forward(D) - Backward(D)) > 0.01 * Forward(D) ? 1 : 0;
            Agrees(Scale.RadiusSlopePxPerAU(D, true), Forward(D));
            Agrees(Scale.RadiusSlopePxPerAU(D, false), Backward(D));

            // And between two knots, one slope either way.
            const double Mid = FMath::Pow(10.0, 0.5 * (Scale.KnotLog[Orbit + 1] + Scale.KnotLog[Orbit + 2]));
            Agrees(Scale.RadiusSlopePxPerAU(Mid, true), Forward(Mid));
            Agrees(Scale.RadiusSlopePxPerAU(Mid, false), Forward(Mid));
        }

        // The clamps: flat on the side past each end.
        Agrees(Scale.RadiusSlopePxPerAU(Scale.InnerAU, true), Forward(Scale.InnerAU));
        Agrees(Scale.RadiusSlopePxPerAU(Scale.InnerAU, false), 0.0);
        Agrees(Scale.RadiusSlopePxPerAU(Scale.RimAU, true), 0.0);
        Agrees(Scale.RadiusSlopePxPerAU(Scale.RimAU, false), Backward(Scale.RimAU));
        for (const bool bOutward : {true, false})
        {
            Agrees(Scale.RadiusSlopePxPerAU(0.5 * Scale.InnerAU, bOutward), 0.0);
            Agrees(Scale.RadiusSlopePxPerAU(2.0 * Scale.RimAU, bOutward), 0.0);
        }

        // Continued past them: the end segments' own px per dex.
        const int32 Last = Scale.KnotLog.Num() - 1;
        const double FirstPxPerDex = (Scale.KnotPx[1] - Scale.KnotPx[0]) / (Scale.KnotLog[1] - Scale.KnotLog[0]);
        const double LastPxPerDex = (Scale.KnotPx[Last] - Scale.KnotPx[Last - 1]) / (Scale.KnotLog[Last] - Scale.KnotLog[Last - 1]);
        for (const bool bOutward : {true, false})
        {
            Agrees(Scale.WarpPxPerDex(0.5 * Scale.InnerAU, bOutward), FirstPxPerDex);
            Agrees(Scale.WarpPxPerDex(Scale.InnerAU, bOutward), FirstPxPerDex);
            Agrees(Scale.WarpPxPerDex(2.0 * Scale.RimAU, bOutward), LastPxPerDex);
            Agrees(Scale.WarpPxPerDex(Scale.RimAU, bOutward), LastPxPerDex);
        }
    }
    AddInfo(FString::Printf(TEXT("%d worlds' knots, %d where the two sides' slopes differ by more than 1%%"), Knots, Kinked));
    TestTrue(TEXT("knots where the side matters"), Knots > 100 && Kinked > 50);
    TestEqual(TEXT("slopes that disagreed with RadiusPx's own finite difference, or with a clamp"), Wrong, 0);
    return true;
}

#endif
