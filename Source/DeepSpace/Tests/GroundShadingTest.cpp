#include "Misc/AutomationTest.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/WorldRelief.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 9: the ground shades exactly as the orbit does. A tile's
 * vertex normal carries the bands its spacing resolves; the pixel adds only
 * what the vertices do not, max(0, fade - carried); on ground finer than its
 * pixels -- every tile at the handover, CDLOD keeping spacing in proportion
 * to distance -- the sum is the orbit's, band for band. So at 50 km there is
 * no brightness step and no blur: the same numbers, not nearly the same.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundShadesAsOrbitTest, "DeepSpace.Surface.GroundShadesAsOrbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroundShadesAsOrbitTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    const FWorldReliefParams Params = FixtureParams();
    const FGroundFieldRef Ground = ShipGround::FromRelief(Params);
    const double R = Params.RadiusCm;
    FRandomStream Random(50);
    double WorstNormal = 0.0;
    double WorstFace = 0.0;
    for (int32 Level = 6; Level <= 12; ++Level)
    {
        const double Spacing = TerrainQuadtree::SpacingCm(Level, R);
        const double Footprint = 0.1 * Spacing / R;
        for (int32 Trial = 0; Trial < 100; ++Trial)
        {
            const FVector3d D(Random.GetUnitVector());
            const FVector3d Vertex = ShipGround::NormalAt(*Ground, D, Spacing);
            const WorldReliefShading::FSurface Orbit = WorldReliefShading::Orbit(Params, D, Footprint);
            const WorldReliefShading::FSurface Tile = WorldReliefShading::Ground(Params, D, Vertex, Footprint, Spacing / R);
            WorstNormal = FMath::Max(WorstNormal, (Orbit.Normal - Tile.Normal).Size());
            WorstFace = FMath::Max3(WorstFace, FMath::Abs(Orbit.Terms.Continent - Tile.Terms.Continent),
                                    FMath::Max(FMath::Abs(Orbit.Terms.Detail - Tile.Terms.Detail),
                                               FMath::Abs(Orbit.Terms.CraterAlbedo - Tile.Terms.CraterAlbedo)));
        }
    }
    TestTrue(FString::Printf(TEXT("the ground's normal is the orbit's (worst %.3g)"), WorstNormal), WorstNormal <= 1.0e-9);
    TestTrue(FString::Printf(TEXT("and its face terms the orbit's, exactly (worst %.3g)"), WorstFace), WorstFace == 0.0);

    // And the split is real: with the vertices carrying nothing, the pixel
    // carries everything -- the orbit, again.
    const FVector3d D = FVector3d(0.2, 0.7, -0.3).GetSafeNormal();
    const WorldReliefShading::FSurface Orbit = WorldReliefShading::Orbit(Params, D, 1.0e-6);
    const WorldReliefShading::FSurface Bare = WorldReliefShading::Ground(Params, D, D, 1.0e-6, 1.0);
    TestTrue(TEXT("a flat vertex with no bands carried shades as the orbit"), (Orbit.Normal - Bare.Normal).Size() <= 1.0e-12);
    TestTrue(TEXT("and the orbit's relief is really tilting it"), (Orbit.Normal - D).Size() > 1.0e-6);

    // The split grows in with the relief. At the handover (Morph 0) the
    // ground shades as the orbit whatever its vertex normal holds -- the GPU
    // interpolates that normal across a triangle, which loses a band the
    // vertices barely resolve, and at a 3-degree dusk the frame stepped 2.2%
    // darker (Eyes.HandoverParity). At the drive floor (Morph 1) it is the
    // tile's plus the pixel's, as it always was.
    {
        const double Spacing = TerrainQuadtree::SpacingCm(8, R);
        const FVector3d Tilted = (D + FVector3d(0.05, -0.02, 0.03)).GetSafeNormal();
        const WorldReliefShading::FSurface AtHandover = WorldReliefShading::Ground(Params, D, Tilted, 1.0e-6, Spacing / R, 0.0);
        TestTrue(FString::Printf(TEXT("at Morph 0 any vertex normal shades as the orbit (%.3g)"), (Orbit.Normal - AtHandover.Normal).Size()),
            (Orbit.Normal - AtHandover.Normal).Size() <= 1.0e-12);
        const WorldReliefShading::FSurface AtFloor = WorldReliefShading::Ground(Params, D, Tilted, 1.0e-6, Spacing / R, 1.0);
        const WorldReliefShading::FSurface Before = WorldReliefShading::Ground(Params, D, Tilted, 1.0e-6, Spacing / R);
        TestTrue(TEXT("at Morph 1 it is the tile's plus the pixel's, the default"), AtFloor.Normal == Before.Normal);
        TestTrue(TEXT("and there the vertex normal really moves it"), (AtFloor.Normal - Orbit.Normal).Size() > 1.0e-3);
    }
    return true;
}

#endif
