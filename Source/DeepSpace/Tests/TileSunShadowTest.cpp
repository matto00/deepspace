#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/TerrainTile.h"
#include "Surface/WorldGround.h"
#include "Tests/GroundFixtures.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileSunShadowTest, "DeepSpace.Surface.TileSunShadow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundShadowLightTest, "DeepSpace.Surface.GroundShadowLight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TileSunShadowLocal
{
    /** The star Elevation rad above D's level, toward Toward's part along it. */
    SunShadow::FSunLight LightAt(const FVector3d& D, const FVector3d& Toward, double Elevation, double AngularRadius)
    {
        const FVector3d Level = (Toward - D * FVector3d::DotProduct(Toward, D)).GetSafeNormal();
        SunShadow::FSunLight Sun;
        Sun.Direction = (D * FMath::Sin(Elevation) + Level * FMath::Cos(Elevation)).GetSafeNormal();
        Sun.AngularRadius = AngularRadius;
        return Sun;
    }
}

/**
 * A tile's vertices carry the cast shadow (the ruling: computed while the
 * tile is built, off the game thread, from the height function): every grid
 * vertex's SunVisible is SunShadow::Visible at its own direction and height,
 * at the tile's spacing -- to the float it is stored in -- the skirt's
 * vertices carry their grid vertex's, and UV0.x is what reaches the mesh.
 * The shadow never moves the ground. Without a light every vertex is whole.
 * A same-level neighbour's shadows on the shared edge are the tile's to the
 * bit; a 2:1 edge's step is measured and printed, not bounded (the shade can
 * step there, where the skirts close only the geometry's crack).
 * The tile is Baemsekai IV-like at level 6 (4.4 km vertices, 141 km across)
 * under a 2-degree sun, where planning measured half the ground shaded: a
 * tile that fixture changes leave uniform is moved, never the bound.
 */
bool FTileSunShadowTest::RunTest(const FString& Parameters)
{
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FGroundFieldRef Field = ShipGround::FromRelief(Params);
    const FTileKey Key = TerrainQuadtree::KeyAt(FVector3d(0.6, -0.48, 0.64).GetSafeNormal(), 6);
    const FVector3d Centre = TerrainQuadtree::CentreDirection(Key);
    TerrainTile::FTileShadow Shadow;
    Shadow.Sun = TileSunShadowLocal::LightAt(Centre, TerrainQuadtree::Face(Key.Face).U, FMath::DegreesToRadians(2.0), 0.029);
    Shadow.SteepestSlope = SunShadow::SteepestSlope(Params);
    const FTileBuild Lit = TerrainTile::Build(*Field, Key);
    const FTileBuild Cast = TerrainTile::Build(*Field, Key, Shadow);

    // The shadow never moves the ground.
    TestTrue(TEXT("the same positions"), Lit.Positions == Cast.Positions);
    TestTrue(TEXT("the same normals"), Lit.Normals == Cast.Normals);
    TestTrue(TEXT("the same heights"), Lit.Heights == Cast.Heights);

    // Every grid vertex is SunShadow::Visible at its own height and the tile's spacing.
    if (!TestEqual(TEXT("one shadow a grid vertex"), Cast.SunVisible.Num(), TerrainTile::GridVerts))
    {
        return false;
    }
    int32 Wrong = 0;
    int32 Shaded = 0;
    int32 Whole = 0;
    for (int32 V = 0; V < TerrainTile::GridVerts; ++V)
    {
        const float Want = static_cast<float>(SunShadow::Visible(*Field, Cast.Directions[V], Shadow.Sun, Cast.SpacingCm,
            Shadow.SteepestSlope, Shadow.Samples, Cast.Heights[V]).Visible);
        Wrong += Cast.SunVisible[V] == Want ? 0 : 1;
        Shaded += Cast.SunVisible[V] < 0.5f ? 1 : 0;
        Whole += Cast.SunVisible[V] > 0.5f ? 1 : 0;
        TestEqual(TEXT("UV0.x is the vertex's shadow"), TerrainTile::UV0Of(Cast, V).X, Cast.SunVisible[V]);
    }
    TestEqual(TEXT("every grid vertex carries SunShadow::Visible at its height and the tile's spacing"), Wrong, 0);
    AddInfo(FString::Printf(TEXT("level 6 under a 2-degree sun: %d of %d vertices under half, %d over"), Shaded, TerrainTile::GridVerts, Whole));
    TestTrue(TEXT("and the tile is not uniform: some ground shaded, some lit"), Shaded > 0 && Whole > 0);
    for (int32 S = 0; S < TerrainTile::SkirtVerts; ++S)
    {
        const int32 V = TerrainTile::GridVerts + S;
        TestEqual(TEXT("a skirt vertex carries its grid vertex's shadow"), TerrainTile::UV0Of(Cast, V).X, TerrainTile::UV0Of(Cast, TerrainTile::GridOf(V)).X);
    }

    // The edges. Two tiles of one level share their edge's directions, heights
    // and footprint, so their shadows there are equal to the bit. Across a 2:1
    // edge they are not: the finer tile shadows at half the coarser's
    // footprint, and its odd edge vertices carry values where the coarser
    // side is drawn as the mean of its two. That step is measured here, not
    // bounded, and Task 10 looks for it in the frames.
    constexpr int32 N1 = TerrainTile::Cells + 1;
    const auto SideVertex = [](int32 Side, int32 T)
    {
        switch (Side)
        {
        case 0: return T * N1;
        case 1: return T * N1 + TerrainTile::Cells;
        case 2: return T;
        default: return TerrainTile::Cells * N1 + T;
        }
    };
    const auto ShadowAt = [&](const FTileBuild& Tile, const FVector3d& D) -> TOptional<float>
    {
        for (int32 Side = 0; Side < 4; ++Side)
        {
            for (int32 T = 0; T < N1; ++T)
            {
                const int32 V = SideVertex(Side, T);
                if (Tile.Directions[V] == D)
                {
                    return Tile.SunVisible[V];
                }
            }
        }
        return {};
    };
    const TArray<FTileKey, TFixedAllocator<4>> Neighbours = TerrainQuadtree::EdgeNeighbours(Key);
    const FTileKey* Across = Neighbours.FindByPredicate([&](const FTileKey& N) { return N.Face == Key.Face; });
    if (TestNotNull(TEXT("the tile has a neighbour on its own face"), Across))
    {
        const FTileBuild Same = TerrainTile::Build(*Field, *Across, Shadow);
        int32 Shared = 0;
        int32 Unequal = 0;
        FVector3d Middle = FVector3d::ZeroVector;
        TSet<int32> Seen;   // a corner lies on two sides: count it once
        for (int32 Side = 0; Side < 4; ++Side)
        {
            for (int32 T = 0; T < N1; ++T)
            {
                const int32 V = SideVertex(Side, T);
                bool bAlready = false;
                Seen.Add(V, &bAlready);
                if (bAlready)
                {
                    continue;
                }
                if (const TOptional<float> There = ShadowAt(Cast, Same.Directions[V]))
                {
                    ++Shared;
                    Unequal += *There == Same.SunVisible[V] ? 0 : 1;
                    Middle += Same.Directions[V];
                }
            }
        }
        TestEqual(TEXT("a same-level neighbour shares a whole edge"), Shared, N1);
        TestEqual(TEXT("and its shadows there, to the bit"), Unequal, 0);

        // The finer tile: the level-7 child of the neighbour at the shared edge's middle.
        const FVector3d Inside = (Middle.GetSafeNormal() * 0.99 + TerrainQuadtree::CentreDirection(*Across) * 0.01).GetSafeNormal();
        const FTileBuild Fine = TerrainTile::Build(*Field, TerrainQuadtree::KeyAt(Inside, Key.Level + 1), Shadow);
        int32 Found = INDEX_NONE;
        for (int32 Side = 0; Side < 4 && Found == INDEX_NONE; ++Side)
        {
            Found = ShadowAt(Cast, Fine.Directions[SideVertex(Side, 0)]) && ShadowAt(Cast, Fine.Directions[SideVertex(Side, 2)]) ? Side : INDEX_NONE;
        }
        if (TestTrue(TEXT("the finer tile meets the coarser along one side"), Found != INDEX_NONE))
        {
            int32 Evens = 0;
            double StepSum = 0.0;
            double StepMax = 0.0;
            for (int32 T = 0; T < N1; ++T)
            {
                const FVector3d& D = Fine.Directions[SideVertex(Found, T)];
                TOptional<float> Coarse = ShadowAt(Cast, D);
                if (Coarse)
                {
                    ++Evens;
                }
                else
                {
                    const TOptional<float> Before = ShadowAt(Cast, Fine.Directions[SideVertex(Found, T - 1)]);
                    const TOptional<float> After = ShadowAt(Cast, Fine.Directions[SideVertex(Found, T + 1)]);
                    Coarse = Before && After ? TOptional<float>(0.5f * (*Before + *After)) : TOptional<float>();
                }
                if (Coarse)
                {
                    const double Step = FMath::Abs(Fine.SunVisible[SideVertex(Found, T)] - *Coarse);
                    StepSum += Step;
                    StepMax = FMath::Max(StepMax, Step);
                }
            }
            TestEqual(TEXT("the finer edge meets every other coarse vertex"), Evens, TerrainTile::Cells / 2 + 1);
            AddInfo(FString::Printf(TEXT("2:1 edge, level 6 to 7 under a 2-degree sun: shade steps by %.3f on average, %.3f at most, over %d vertices"),
                StepSum / N1, StepMax, N1));
            TestTrue(TEXT("and the step is measured"), FMath::IsFinite(StepMax));
        }
    }

    // Without a light, whole.
    bool bAllWhole = Lit.SunVisible.Num() == TerrainTile::GridVerts;
    for (const float Seen : Lit.SunVisible)
    {
        bAllWhole = bAllWhole && Seen == 1.0f;
    }
    TestTrue(TEXT("a tile built without a light has every vertex whole"), bAllWhole);
    TestEqual(TEXT("and UV0.y is unused"), TerrainTile::UV0Of(Cast, 7).Y, 0.0f);
    return true;
}

/**
 * AWorldGround builds its tiles under the sky's own light for the world
 * under the ship (SkyProjection::SunLightOf), the real ground's steepest
 * slope and ds.Terrain.ShadowSamples, and rebuilds when ds.Terrain.Shadows
 * changes, letting go of what was building without waiting and without
 * oversubscribing its workers. 800 km over Baemsekai IV only the prefetch
 * chain is wanted, so the test builds a handful of tiles. It stands over
 * IV's 10-degree dusk, where the march runs (over the opening side's
 * 62.6-degree sun the day exit makes a build ~3 ms, and the builds the
 * restart lets go could finish before it, leaving nothing to count).
 */
bool FGroundShadowLightTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    // The tiles' shadows only: no map is baked behind this test.
    FSkyWorld Test(TEXT("GroundShadowLightWorld"), 8, EShadows::Tiles);
    Test.BeginPlay();
    const FSkySystem Here = LocalSystem::Here(Test.World);
    if (!TestTrue(TEXT("the start system has a fourth body"), Here.Bodies.IsValidIndex(4)))
    {
        return false;
    }
    const FSkyBody& Fourth = Here.Bodies[4];
    const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, 4, 0.0, Test.Ship->GetFlightState().GetUniversePosition(),
                                                                 ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(10.0));
    if (!TestTrue(TEXT("goto dusk places over Baemsekai IV"), Dusk.IsSet()))
    {
        return false;
    }
    const FVector Out = (Dusk->Position - Fourth.Position).GetSafeNormal();
    Test.Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + 8.0e7), FRotationMatrix::MakeFromX(-Out).ToQuat());
    Test.Step(1.0f / 60.0f);
    Test.Ground->FlushBuildsForTest();
    Test.Step(1.0f / 60.0f);

    const TerrainTile::FTileShadow& Shadow = Test.Ground->GetTileShadow();
    const SunShadow::FSunLight Sky = SkyProjection::SunLightOf(Here, 4);
    TestTrue(TEXT("the tiles are built under the sky's own light for this world"),
        Shadow.Sun.Direction == Sky.Direction && Shadow.Sun.AngularRadius == Sky.AngularRadius);
    TestEqual(TEXT("with the real ground's steepest slope"), Shadow.SteepestSlope, SunShadow::SteepestSlope(Fourth.Relief));
    TestEqual(TEXT("and ds.Terrain.ShadowSamples' samples"), Shadow.Samples, SunShadow::DefaultSamples);

    // With no map (ds.Sky.ShadowMaps 0 here, as before any map lands) the
    // look the ground copies still casts at full strength: only the map's
    // fade is 0, so the vertices' shadow draws (M_SkyGround's map_fade).
    if (const UStaticMeshComponent* Proxy = Test.Sky->GetProxy(4))
    {
        if (UMaterialInstanceDynamic* Look = Cast<UMaterialInstanceDynamic>(Proxy->GetMaterial(0)))
        {
            TestEqual(TEXT("with no map the look casts at full strength"), Look->K2_GetScalarParameterValue(SkyMaterial::Shadows), ShipSky::ShadowStrength());
            TestEqual(TEXT("and only the map's fade is 0"), Look->K2_GetScalarParameterValue(SkyMaterial::ShadowMapFade), 0.0f);
        }
        else
        {
            AddError(TEXT("IV's proxy draws a dynamic instance"));
        }
    }
    else
    {
        AddError(TEXT("IV has a proxy"));
    }

    const FTileKey Key = TerrainQuadtree::KeyAt(FVector3d(Out), 8);
    const FTileBuild* Resident = Test.Ground->GetResidentTile(Key);
    if (!TestNotNull(TEXT("the level-8 tile under the ship is resident"), Resident))
    {
        return false;
    }
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    TestTrue(TEXT("and carries the shadow a fresh build under that light does"),
        Resident->SunVisible == TerrainTile::Build(*Field, Key, Shadow).SunVisible);

    {
        // Off: the ground starts again, and every vertex is whole. The builds
        // in flight when it restarts are let go, not waited on, and they
        // count against the cap until they finish. At 64 samples under a
        // 10-degree sun a build is far longer than a step, so they are.
        FScopedCVar Samples(TEXT("ds.Terrain.ShadowSamples"), 64.0f);
        Test.Step(1.0f / 60.0f);   // a restart under a new sample count: builds launch
        const int32 Cap = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Terrain.BuildTasks"))->GetInt();
        FScopedCVar Off(TEXT("ds.Terrain.Shadows"), 0.0f);
        Test.Step(1.0f / 60.0f);   // and another, with those still building
        // Without builds let go the cap below holds of any launch budget.
        TestTrue(FString::Printf(TEXT("the restart let go of builds still running (%d)"), Test.Ground->GetDrainingCount()),
            Test.Ground->GetDrainingCount() > 0);
        TestTrue(FString::Printf(TEXT("a restart never oversubscribes the workers (%d building, %d let go, cap %d)"),
            Test.Ground->GetBuildingCount(), Test.Ground->GetDrainingCount(), Cap),
            Test.Ground->GetBuildingCount() + Test.Ground->GetDrainingCount() <= Cap);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        TestFalse(TEXT("with ds.Terrain.Shadows 0 the ground builds under no light"), Test.Ground->GetTileShadow().Sun.IsSet());
        const FTileBuild* Rebuilt = Test.Ground->GetResidentTile(Key);
        bool bWhole = Rebuilt != nullptr;
        for (int32 V = 0; bWhole && V < Rebuilt->SunVisible.Num(); ++V)
        {
            bWhole = Rebuilt->SunVisible[V] == 1.0f;
        }
        TestTrue(TEXT("and the rebuilt tile is whole everywhere"), bWhole);
    }
    return true;
}

#endif
