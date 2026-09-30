#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"
#include "Surface/TerrainTile.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decisions 6 and 7's tile, pure: the fixed topology wound as the
 * engine's meshes are, shared edges of same-level neighbours identical,
 * vertices local to a pivot with sub-millimetre precision near the ship, a
 * skirt deep enough for the edge error and the omitted bands, the mesh under
 * the ship within GearClearance / 10 of the analytic ground, and heights
 * that are the sphere at the handover and the full relief at the drive
 * floor. Reports a tile's build time and the largest split pop in 4K pixels.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainTileTest, "DeepSpace.Surface.Tile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainTileTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    using namespace TerrainTile;
    const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
    const double R = Ground->RadiusCm();
    const int32 Finest = TerrainQuadtree::MaxLevel(R);

    TestEqual(TEXT("2,304 triangles, three indices each"), Indices().Num(), 3 * TriangleCount);
    bool bInRange = true;
    for (const int32 Index : Indices())
    {
        bInRange &= Index >= 0 && Index < VertexCount;
    }
    TestTrue(TEXT("every index names a vertex"), bInRange);

    FRandomStream Random(8);
    const auto RandomKey = [&](int32 Level)
    {
        FTileKey Key = TerrainQuadtree::KeyAt(FVector3d(Random.GetUnitVector()), Level);
        return Key;
    };

    // Winding: seen from outside, (B - A) x (C - A) points into the world.
    {
        const FTileBuild Tile = Build(*Ground, RandomKey(12));
        int32 Inward = 0;
        for (int32 T = 0; T < 2 * Cells * Cells; ++T)
        {
            const FVector3d A = Tile.Pivot + FVector3d(Tile.Positions[Indices()[3 * T]]);
            const FVector3d B = Tile.Pivot + FVector3d(Tile.Positions[Indices()[3 * T + 1]]);
            const FVector3d C = Tile.Pivot + FVector3d(Tile.Positions[Indices()[3 * T + 2]]);
            Inward += FVector3d::DotProduct(FVector3d::CrossProduct(B - A, C - A), A.GetSafeNormal()) < 0.0 ? 1 : 0;
        }
        TestEqual(TEXT("every grid triangle is wound as the engine's own meshes"), Inward, 2 * Cells * Cells);
    }

    // Shared edges: same directions and heights, to the last bit; positions
    // agree to float precision across the two pivots.
    int32 Identical = 0;
    int32 Compared = 0;
    double WorstGap = 0.0;
    for (int32 Trial = 0; Trial < 12; ++Trial)
    {
        const FTileKey Key = RandomKey(Random.RandRange(8, Finest));
        const FTileBuild Tile = Build(*Ground, Key);
        for (const FTileKey& Neighbour : TerrainQuadtree::EdgeNeighbours(Key))
        {
            const FTileBuild Other = Build(*Ground, Neighbour);
            for (int32 V = 0; V < GridVerts; ++V)
            {
                for (int32 W = 0; W < GridVerts; ++W)
                {
                    if (Tile.Directions[V] == Other.Directions[W])
                    {
                        ++Compared;
                        Identical += Tile.Heights[V] == Other.Heights[W] ? 1 : 0;
                        WorstGap = FMath::Max(WorstGap, ((Tile.Pivot + FVector3d(Tile.Positions[V])) - (Other.Pivot + FVector3d(Other.Positions[W]))).Size());
                    }
                }
            }
        }
    }
    TestTrue(TEXT("neighbours share their edge vertices"), Compared >= 12 * 4 * (Cells + 1));
    TestEqual(TEXT("and the heights there are identical"), Identical, Compared);
    TestTrue(FString::Printf(TEXT("and the positions agree across pivots (worst %.4f cm)"), WorstGap), WorstGap < 1.0);

    // Precision near the ship: a finest tile's vertices hold sub-millimetre.
    {
        const FTileBuild Tile = Build(*Ground, RandomKey(Finest));
        double Worst = 0.0;
        for (int32 V = 0; V < GridVerts; ++V)
        {
            const FVector3d Exact = Tile.Directions[V] * (R + Tile.Heights[V]);
            Worst = FMath::Max(Worst, ((Tile.Pivot + FVector3d(Tile.Positions[V])) - Exact).Size());
        }
        TestTrue(FString::Printf(TEXT("a finest tile's float vertices are within 0.1 mm (%.5f cm)"), Worst), Worst < 0.01);
        TestTrue(TEXT("its skirt covers its edge error and the bands it omits"),
                 Tile.SkirtDepthCm >= Tile.MaxEdgeErrorCm + Ground->OmittedBoundCm(Tile.SpacingCm));
    }

    // The mesh under the ship is the ground, to GearClearance / 10.
    {
        double Worst = 0.0;
        for (int32 Trial = 0; Trial < 50; ++Trial)
        {
            const FVector3d D(Random.GetUnitVector());
            const FTileBuild Tile = Build(*Ground, TerrainQuadtree::KeyAt(D, Finest));
            const TOptional<double> Drawn = SampleHeight(Tile, D);
            Worst = FMath::Max(Worst, Drawn ? FMath::Abs(*Drawn - Ground->Height(D, 0.0)) : 1.0e9);
        }
        TestTrue(FString::Printf(TEXT("the finest mesh is within 15 cm of the analytic ground (worst %.2f cm)"), Worst),
                 Worst <= ShipLanding::DefaultGearClearanceCm / 10.0);
    }

    // The morph: the sphere at the handover, the full relief at the drive floor.
    {
        const double FloorAlt = 1.02e6 + Ground->MaxHeightCm();
        TestEqual(TEXT("M is 0 at the handover"), MorphFraction(HandoverAltitudeCm, FloorAlt), 0.0);
        TestEqual(TEXT("and 1 at the drive floor"), MorphFraction(FloorAlt, FloorAlt), 1.0);
        TestEqual(TEXT("and under it"), MorphFraction(1.0e3, FloorAlt), 1.0);
        TestTrue(TEXT("and between, between"), MorphFraction(3.0e6, FloorAlt) > 0.0 && MorphFraction(3.0e6, FloorAlt) < 1.0);
        const FTileBuild Tile = Build(*Ground, RandomKey(14));
        double OffSphere = 0.0;
        double OffRelief = 0.0;
        for (int32 V = 0; V < GridVerts; ++V)
        {
            OffSphere = FMath::Max(OffSphere, FMath::Abs((Tile.Pivot + MorphedPosition(Tile, V, 0.0)).Size() - R));
            OffRelief = FMath::Max(OffRelief, (MorphedPosition(Tile, V, 1.0) - FVector3d(Tile.Positions[V])).Size());
        }
        TestTrue(FString::Printf(TEXT("tile heights are the sphere at M = 0 (%.4f cm)"), OffSphere), OffSphere < 0.05);
        TestTrue(TEXT("and the full relief at M = 1"), OffRelief < 1.0e-6);
        TestTrue(TEXT("the height rides UV2.y in kilometres"),
                 FMath::IsNearlyEqual(UV2Of(Tile, 7).Y * HeightUVScaleCm, Tile.Heights[7], 1.0));
    }

    // Reported: build cost, and the largest vertex pop at a split in 4K pixels.
    {
        const double Start = FPlatformTime::Seconds();
        double WorstPixels = 0.0;
        constexpr int32 Builds = 20;
        for (int32 Trial = 0; Trial < Builds / 2; ++Trial)
        {
            const FTileKey Child = RandomKey(Random.RandRange(10, Finest));
            const FTileBuild Coarse = Build(*Ground, Child.Parent());
            const FTileBuild Fine = Build(*Ground, Child);
            const double SplitDistance = TerrainQuadtree::DefaultSplitFactor * TerrainQuadtree::EdgeLengthCm(Coarse.Key.Level, R);
            for (int32 V = 0; V < GridVerts; ++V)
            {
                if (const TOptional<double> Before = SampleHeight(Coarse, Fine.Directions[V]))
                {
                    WorstPixels = FMath::Max(WorstPixels, FMath::Atan(FMath::Abs(Fine.Heights[V] - *Before) / SplitDistance) / (2.0 / 3840.0));
                }
            }
        }
        const double MsEach = (FPlatformTime::Seconds() - Start) * 1000.0 / Builds;
        AddInfo(FString::Printf(TEXT("a tile builds in %.2f ms; the largest split pop is %.2f 4K pixels"), MsEach, WorstPixels));
        TestTrue(TEXT("both are numbers"), FMath::IsFinite(MsEach) && FMath::IsFinite(WorstPixels));
        // PMC uploads UV2.y (the height, km) as a half: 10 mantissa bits, so
        // a height in [2^e, 2^(e+1)) km rounds by at most 2^(e-11) km. The
        // morph scales that by (1 - M), which is 1 only at the handover.
        const double PeakKm = FMath::Max(Ground->MaxHeightCm(), 1.0) / HeightUVScaleCm;
        const double HalfRoundingCm = FMath::Pow(2.0, FMath::FloorToDouble(FMath::Log2(PeakKm)) - 11.0) * HeightUVScaleCm;
        const double HalfPixels = FMath::Atan(HalfRoundingCm / HandoverAltitudeCm) / (2.0 / 3840.0);
        AddInfo(FString::Printf(TEXT("the half-float height rounds by at most %.0f cm: %.2f 4K pixels at the handover"), HalfRoundingCm, HalfPixels));
        TestTrue(TEXT("the half-float height's rounding stays under a pixel at the handover"), HalfPixels < 1.0);
    }
    return true;
}

#endif
