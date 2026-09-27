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
 * Flying straight at a world in the plane the worlds are in, the ship's
 * glyph never moves away from that world's dot, and it is on the dot at the
 * world. Checked for every world of every system round a generated home,
 * from 32 starts each.
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
            for (const FUniversePosition& Start : Starts(System, Scale, Orbit, false))
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
    AddInfo(FString::Printf(TEXT("%d straight flights in the plane over %d systems"), Flights, Systems.Num()));
    TestTrue(TEXT("flights to check"), Flights > 1000);
    TestEqual(FString::Printf(TEXT("flights whose glyph moved away from the dot it was closing on (worst %.3f px)"), WorstRetreatPx),
              Retreats, 0);
    TestEqual(TEXT("flights that ended off the world's dot"), Misses, 0);
    return true;
}

/**
 * The ship's tick is the way its glyph moves. Flying nose first -- straight
 * at a world, from in the plane or out of it -- the glyph's next step on the
 * map is along the tick, to within a degree; and near a world, with the nose
 * on it, the tick points at its dot. Drawn in universe directions, the tick
 * was up to ~50 degrees off both: the map is stretched round each ring
 * several times more than across it.
 */
bool FSystemMapTickFollowsGlyphTest::RunTest(const FString& Parameters)
{
    using namespace SystemMapTrackingTestLocal;

    const TArray<FStarSystem> Systems = Corpus();
    int32 Checked = 0;
    int32 Astray = 0;
    double Worst = 0.0;
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
            for (const bool bOffPlane : {false, true})
            {
                for (const FUniversePosition& Start : Starts(System, Scale, Orbit, bOffPlane))
                {
                    const FVector Leg = World - Start;
                    if (Leg.Size() < 1.0e-3 * AU)
                    {
                        continue;
                    }
                    const FQuat Nose = Facing(Leg);
                    for (int32 Step = 0; Step < Steps; ++Step)
                    {
                        const FUniversePosition Where = Start + Leg * (double(Step) / Steps);
                        const FMapShip Glyph = Ship(Scale, Where, Nose);
                        if (!Glyph.Nose || Glyph.Pin != EMapPin::None)
                        {
                            continue;
                        }
                        const FVector2D Moved = Ship(Scale, Where + Leg * 1.0e-7, Nose).Centre - Glyph.Centre;
                        if (Moved.Size() < 1.0e-6)
                        {
                            continue;
                        }
                        ++Checked;
                        const double Off = AngleDeg(*Glyph.Nose, Moved);
                        Worst = FMath::Max(Worst, Off);
                        Astray += Off > 1.0 ? 1 : 0;
                    }
                }
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
    AddInfo(FString::Printf(TEXT("%d steps checked, worst %.3f deg; %d near a world, worst %.3f deg"),
                            Checked, Worst, NearChecked, NearWorst));
    TestTrue(TEXT("steps to check"), Checked > 10000 && NearChecked > 1000);
    TestEqual(TEXT("steps where the glyph moved more than a degree off its tick"), Astray, 0);
    TestEqual(TEXT("near a world, noses on it whose tick missed its dot by more than 1.5 degrees"), NearAstray, 0);
    return true;
}

/**
 * Bearings on the map. A radial warp keeps two bearings exactly: straight
 * toward or away from the star, and between two points the same distance
 * from it -- so a world on the ship's own orbit, or on its line to the star,
 * is where it truly is. Any other bearing is bent by the warp, and bounded by
 * nothing but it; the spread is reported, for the projection question.
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
    if (Bent.Num() > 0)
    {
        const auto At = [&Bent](double Q) { return Bent[FMath::Min(Bent.Num() - 1, int32(Q * Bent.Num()))]; };
        AddInfo(FString::Printf(TEXT("any other in-plane bearing, map vs truth, over %d: median %.1f, p90 %.1f, max %.1f deg"),
                                Bent.Num(), At(0.5), At(0.9), Bent.Last()));
    }
    return true;
}

#endif
