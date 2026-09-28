#include "Misc/AutomationTest.h"
#include "Surface/TerrainQuadtree.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 6's quadtree, pure: six equal-angle cube faces (the
 * SM_SkyBody mapping), shared edges exact, and CDLOD from the ship's origin
 * with boxes from each node's own height range, the 2:1 rule, the horizon,
 * the ship's chain to MaxLevel near the ground, and MaxTiles coarsening the
 * far levels first. Reports the tile and triangle counts that set the
 * budgets (the simulation said 860 at 50 km to 2,200 at 1.5 m).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainQuadtreeTest, "DeepSpace.Surface.Quadtree",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TerrainQuadtreeLocal
{
    using namespace TerrainQuadtree;

    /** 2 km local peaks: the root faces' analytic bounds, and so the horizon. */
    const FHeightRange Peaks{ 0.0, 2.0e5 };

    /** A node's own height range, as its parent's built vertices would give
     *  it (decision 6: "never the world's whole relief shell"): a rise of a
     *  quarter of its edge -- ground as steep as the steepest the relief
     *  draws, about 0.44, over half the node -- never past the peaks. The
     *  roots, with no parent, take the analytic peaks. */
    TOptional<FHeightRange> OwnRange(const FTileKey& Key, double RadiusCm)
    {
        if (Key.Level == 0)
        {
            return Peaks;
        }
        return FHeightRange{ 0.0, FMath::Min(Peaks.MaxCm, 0.25 * EdgeLengthCm(Key.Level, RadiusCm)) };
    }

    /** The leaf holding D, searching every level; none if D is culled. */
    TOptional<FTileKey> LeafAt(const TSet<FTileKey>& Leaves, const FVector3d& D, int32 MaxLevelIn)
    {
        for (int32 Level = 0; Level <= MaxLevelIn; ++Level)
        {
            const FTileKey Key = KeyAt(D, Level);
            if (Leaves.Contains(Key))
            {
                return Key;
            }
        }
        return {};
    }
}

bool FTerrainQuadtreeTest::RunTest(const FString& Parameters)
{
    using namespace TerrainQuadtreeLocal;
    const double Earth = UniverseUnits::CmPerEarthRadius;
    TestEqual(TEXT("an Earth's finest level is 19: 19 m tiles, 0.6 m between vertices"), MaxLevel(Earth), 19);
    TestEqual(TEXT("a small world has fewer"), MaxLevel(1.0e7), 13);
    TestTrue(FString::Printf(TEXT("level 19's spacing on an Earth is 0.6 m (%.3f)"), SpacingCm(19, Earth) / 100.0),
             FMath::IsNearlyEqual(SpacingCm(19, Earth), 59.7, 0.5));

    // Keys and directions agree, and shared edges are the same directions exactly.
    FRandomStream Random(19);
    int32 RoundTrips = 0;
    int32 EdgesExact = 0;
    int32 Edges = 0;
    for (int32 Trial = 0; Trial < 400; ++Trial)
    {
        FTileKey Key;
        Key.Face = static_cast<uint8>(Random.RandRange(0, 5));
        Key.Level = static_cast<uint8>(Random.RandRange(0, 19));
        Key.X = static_cast<uint32>(Random.RandRange(0, (1 << Key.Level) - 1));
        Key.Y = static_cast<uint32>(Random.RandRange(0, (1 << Key.Level) - 1));
        RoundTrips += KeyAt(CentreDirection(Key), Key.Level) == Key ? 1 : 0;
        TSet<FVector3d> Boundary;
        for (int32 K = 0; K <= CellsPerTile; ++K)
        {
            Boundary.Add(GridDirection(Key, K, 0));
            Boundary.Add(GridDirection(Key, K, CellsPerTile));
            Boundary.Add(GridDirection(Key, 0, K));
            Boundary.Add(GridDirection(Key, CellsPerTile, K));
        }
        for (const FTileKey& Neighbour : EdgeNeighbours(Key))
        {
            ++Edges;
            int32 Shared = 0;
            for (int32 K = 0; K <= CellsPerTile; ++K)
            {
                for (const FVector3d& D : { GridDirection(Neighbour, K, 0), GridDirection(Neighbour, K, CellsPerTile),
                                            GridDirection(Neighbour, 0, K), GridDirection(Neighbour, CellsPerTile, K) })
                {
                    Shared += Boundary.Contains(D) ? 1 : 0;
                }
            }
            // 33 shared, corners counted twice on each side's own list.
            EdgesExact += Shared >= CellsPerTile + 1 ? 1 : 0;
        }
    }
    TestEqual(TEXT("every key's centre maps back to it"), RoundTrips, 400);
    TestEqual(TEXT("every shared edge is the same 33 directions, to the last bit, across faces too"), EdgesExact, Edges);

    // The cut, over a sweep of altitudes, no cap: each node its own box
    // (the counts that set the budgets), and again with the whole 2 km shell
    // as every node's box -- what the spec forbids the terrain to do, since
    // it refines everything under the highest peak's height to MaxLevel,
    // and so the harshest case for the structure: no overlap, 2:1, coverage,
    // the chain and the prefetch hold under both.
    struct FCount { double Altitude; int32 Tiles; int32 ShellTiles; };
    TArray<FCount> Counts;
    // 500 m: under ChainFullAltitudeCm the chain is forced to MaxLevel while
    // the distance rule alone would stop some five levels short, so the 2:1
    // balance must cascade outward from the chain, level by level.
    for (const double Altitude : { 150.0, 2.0e3, 5.0e4, 1.0e5, 1.0e6, 5.0e6, 1.0e8 })
    for (const bool bShell : { false, true })
    {
        const FVector3d Nadir = FVector3d(0.31, -0.62, 0.72).GetSafeNormal();
        const FVector3d Ship = Nadir * (Earth + Altitude);
        FCutParams Params;
        Params.RadiusCm = Earth;
        Params.MaxLevel = MaxLevel(Earth);
        Params.MaxTiles = 1000000;
        Params.OccluderRadiusCm = Earth;
        Params.GroundAltitudeCm = Altitude;
        const FCut Cut = SelectCut(Ship, Params, [&](const FTileKey& Key)
            { return bShell ? TOptional<FHeightRange>(Peaks) : OwnRange(Key, Earth); });
        if (!bShell)
        {
            Counts.Add({ Altitude, Cut.Leaves.Num(), 0 });
        }
        else
        {
            Counts.Last().ShellTiles = Cut.Leaves.Num();
        }
        const TSet<FTileKey> Leaves(Cut.Leaves);
        const FString Boxes = bShell ? TEXT(" (every box the whole shell)") : TEXT("");

        bool bNoOverlap = true;
        for (const FTileKey& Leaf : Cut.Leaves)
        {
            for (FTileKey Up = Leaf; Up.Level > 0;)
            {
                Up = Up.Parent();
                bNoOverlap &= !Leaves.Contains(Up);
            }
        }
        TestTrue(FString::Printf(TEXT("%.0f m%s: no leaf overlaps another"), Altitude / 100.0, *Boxes), bNoOverlap);
        TestTrue(FString::Printf(TEXT("%.0f m%s: 2:1 between neighbours"), Altitude / 100.0, *Boxes), Balanced(Cut.Leaves));

        // Everything the ship can see of a bare sphere is covered.
        const double Horizon = FMath::Acos(Earth / (Earth + Altitude));
        int32 Uncovered = 0;
        for (int32 Sample = 0; Sample < 2000; ++Sample)
        {
            const FVector3d Tangent = FVector3d::CrossProduct(Nadir, FVector3d(Random.GetUnitVector())).GetSafeNormal();
            const double Angle = Horizon * FMath::Sqrt(Random.FRand()) * 0.999;
            const FVector3d D = Nadir * FMath::Cos(Angle) + Tangent * FMath::Sin(Angle);
            Uncovered += LeafAt(Leaves, D, Params.MaxLevel).IsSet() ? 0 : 1;
        }
        TestEqual(FString::Printf(TEXT("%.0f m%s: every direction inside the horizon is drawn"), Altitude / 100.0, *Boxes), Uncovered, 0);
        if (Altitude < ChainFullAltitudeCm)
        {
            TestTrue(FString::Printf(TEXT("%.0f m%s: the ship's own chain reaches MaxLevel"), Altitude / 100.0, *Boxes),
                     Leaves.Contains(KeyAt(Nadir, Params.MaxLevel)));
        }
        if (Altitude < PrefetchAltitudeCm + 1.0)
        {
            TestEqual(TEXT("the chain to level 8 is prefetched"), Cut.Prefetch.Num(), PrefetchLevel + 1);
        }
    }
    for (const FCount& Count : Counts)
    {
        AddInfo(FString::Printf(TEXT("%.3f km: %d tiles, %.2f M triangles (every box the whole 2 km shell: %d)"), Count.Altitude / 1.0e5,
                                Count.Tiles, Count.Tiles * 2304.0 / 1.0e6, Count.ShellTiles));
    }

    // The cap: the far levels coarsen first; the ship's chain never does.
    {
        const FVector3d Nadir(0.0, 0.0, 1.0);
        FCutParams Params;
        Params.RadiusCm = Earth;
        Params.MaxLevel = MaxLevel(Earth);
        Params.MaxTiles = 600;
        Params.OccluderRadiusCm = Earth;
        Params.GroundAltitudeCm = 150.0;
        const FCut Cut = SelectCut(Nadir * (Earth + 150.0), Params, [&](const FTileKey& Key) { return OwnRange(Key, Earth); });
        TestTrue(TEXT("over the cap it says it is binding"), Cut.bCapBinding);
        TestTrue(FString::Printf(TEXT("and gets under it (%d)"), Cut.Leaves.Num()), Cut.Leaves.Num() <= 600);
        TestTrue(TEXT("with the ship's chain still at MaxLevel"), Cut.Leaves.Contains(KeyAt(Nadir, Params.MaxLevel)));
        bool bCoarseFirst = true;
        for (int32 Level = 1; Level < Cut.SplitFactorByLevel.Num(); ++Level)
        {
            bCoarseFirst &= Cut.SplitFactorByLevel[Level] >= Cut.SplitFactorByLevel[Level - 1];
        }
        TestTrue(TEXT("lowering the coarsest levels' split factor first"), bCoarseFirst);
    }
    return true;
}

#endif
