#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Surface/TerrainGroundComponent.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/* Every tile through one primitive (slice (b)'s last open items): tiles at
 * their pivots in the component's space, so moving the component moves them
 * all; shown and hidden by key; bounds round what is shown; the caller's
 * tile held, not copied; never casting a shadow. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainGroundComponentTest, "DeepSpace.Surface.GroundComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerrainGroundComponentTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GroundComponentWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    AActor* Holder = World->SpawnActor<AActor>();
    UTerrainGroundComponent* Tiles = NewObject<UTerrainGroundComponent>(Holder);
    Holder->SetRootComponent(Tiles);
    Tiles->RegisterComponent();

    const FGroundFieldRef Ground = ShipGround::FromRelief(FixtureParams());
    const FTileKey Here = TerrainQuadtree::KeyAt(FVector3d(0, 0, 1), 12);
    const FTileKey Beside = TerrainQuadtree::KeyAt(FVector3d(0.01, 0, 1).GetSafeNormal(), 12);
    const UTerrainGroundComponent::FTileRef A = MakeShared<const FTileBuild, ESPMode::ThreadSafe>(TerrainTile::Build(*Ground, Here));
    const UTerrainGroundComponent::FTileRef B = MakeShared<const FTileBuild, ESPMode::ThreadSafe>(TerrainTile::Build(*Ground, Beside));
    TestNotEqual(TEXT("the fixture's two tiles are two"), Here, Beside);
    Tiles->AddTile(A, 1.0e-4f);
    Tiles->AddTile(B, 1.0e-4f);
    TestEqual(TEXT("two tiles held"), Tiles->GetTileCount(), 2);
    TestEqual(TEXT("and none shown until asked"), Tiles->GetShownCount(), 0);
    TestTrue(TEXT("the caller's tile is held, not a copy"), Tiles->GetTile(Here) == &A.Get());

    Tiles->SetShown({ Here });
    TestTrue(TEXT("the tile asked for is shown"), Tiles->IsTileShown(Here));
    TestFalse(TEXT("and the other is not"), Tiles->IsTileShown(Beside));
    const FBox Bounds = Tiles->CalcBounds(FTransform::Identity).GetBox();
    bool bInside = true;
    for (const FVector3f& P : A->Positions)
    {
        bInside &= Bounds.ExpandBy(1.0).IsInside(FVector(A->Pivot) + FVector(P));
    }
    TestTrue(TEXT("the bounds hold every vertex of what is shown, at its pivot"), bInside);
    Tiles->SetShown({});
    TestEqual(TEXT("with nothing shown the bounds hold nothing"), Tiles->CalcBounds(FTransform::Identity).SphereRadius, 0.0);
    Tiles->SetShown({ Here });

    // One move moves every tile: the component's transform is the only one.
    const FVector Ship(-1.0e8, 3.0e5, -2.0e7);
    const FQuat Turn(FVector(0.3, -0.5, 0.8).GetSafeNormal(), 0.7);
    Tiles->SetWorldLocationAndRotation(Turn.RotateVector(Ship), Turn);
    double Worst = 0.0;
    for (const FTileKey& Key : { Here, Beside })
    {
        const FVector Expected = Turn.RotateVector(Ship + FVector(Tiles->GetTile(Key)->Pivot));
        Worst = FMath::Max(Worst, (Tiles->GetTileWorldLocation(Key) - Expected).Size());
    }
    TestTrue(FString::Printf(TEXT("every tile is at its pivot through the one transform, in doubles (%.2e cm off)"), Worst), Worst < 1.0e-3);

    Tiles->RemoveTile(Here);
    TestEqual(TEXT("a tile let go is gone"), Tiles->GetTileCount(), 1);
    TestEqual(TEXT("and no longer shown"), Tiles->GetShownCount(), 0);
    Tiles->ClearTiles();
    TestEqual(TEXT("every tile let go"), Tiles->GetTileCount(), 0);

    TestFalse(TEXT("it casts no shadow"), Tiles->CastShadow);
    TestFalse(TEXT("nor lights by distance field"), Tiles->bAffectDistanceFieldLighting);
    TestEqual(TEXT("and has no collision"), Tiles->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
