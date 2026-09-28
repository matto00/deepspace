#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Surface/TerrainTileComponent.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/* Decision 6's fallback: a tile primitive on the static draw path, no CPU
 * copy beyond a test hook, never casting a shadow. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainTileComponentTest, "DeepSpace.Surface.TileComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainTileComponentTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TileComponentWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    AActor* Holder = World->SpawnActor<AActor>();
    UTerrainTileComponent* Tile = NewObject<UTerrainTileComponent>(Holder);
    Tile->bKeepForTest = true;
    Holder->SetRootComponent(Tile);
    Tile->RegisterComponent();
    const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
    const FTileBuild Build = TerrainTile::Build(*Ground, TerrainQuadtree::KeyAt(FVector3d(0, 0, 1), 12));
    Tile->SetTile(Build);
    TestNotNull(TEXT("the tile is kept for the test"), Tile->GetTileForTest());
    const FBox Bounds = Tile->CalcBounds(FTransform::Identity).GetBox();
    bool bInside = true;
    for (const FVector3f& P : Build.Positions)
    {
        bInside &= Bounds.ExpandBy(1.0).IsInside(FVector(P));
    }
    TestTrue(TEXT("its bounds hold every vertex"), bInside);
    TestFalse(TEXT("it casts no shadow"), Tile->CastShadow);
    TestFalse(TEXT("nor lights by distance field"), Tile->bAffectDistanceFieldLighting);
    TestEqual(TEXT("and has no collision"), Tile->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
