#include "Components/PrimitiveComponent.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Surface/TerrainGroundComponent.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 6's actor: the cut built off the game thread and drawn
 * as tiles of one primitive (UTerrainGroundComponent) on the counter-frame,
 * the component at minus the ship's position from the world's centre and
 * each tile at its pivot in the component's space, never casting a shadow or reaching distance fields, indirect light
 * or ray tracing; a capped number of uploads a frame; and under 1 km the
 * drawn ground under the ship within GearClearance / 10 of the analytic
 * ground. Spawned before play, builds flushed inline.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldGroundActorTest, "DeepSpace.Surface.GroundActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldGroundForgetsBoundsTest, "DeepSpace.Surface.GroundForgetsBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGroundActorTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("GroundActorWorld"));
    if (!TestNotNull(TEXT("the ground spawns before play"), Test.Ground))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    AWorldGround* Ground = Test.Ground;
    TestTrue(TEXT("the ground is tagged for the level's check"), Ground->ActorHasTag(AWorldGround::GroundTag));
    TestTrue(TEXT("and rides the counter-frame"), Ground->GetAttachParentActor() == Test.Frame);

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Fourth = Here.Bodies[4];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const auto PlaceAt = [&](const FVector& Up, double Agl)
    {
        Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + Agl),
                        FRotationMatrix::MakeFromXZ(FVector::CrossProduct(Up, FVector(0.3, 0.9, 0.1)).GetSafeNormal(), Up).ToQuat());
        Test.Step(1.0f / 60.0f);
    };

    PlaceAt(Out, 5.0e4);
    Ground->FlushBuildsForTest();
    Test.Step(1.0f / 60.0f);
    const TArray<FTileKey> Drawn = Ground->GetDrawnKeys();
    AddInfo(FString::Printf(TEXT("500 m over Baemsekai IV: %d tiles drawn, %d resident\n%s"), Drawn.Num(), Ground->GetResidentCount(), *Ground->Describe()));
    TestTrue(TEXT("500 m up the ground draws a cut"), Drawn.Num() > 100);
    TestTrue(TEXT("and claims the body"), Ground->IsDrawingBody() && Ground->GetDrawnBody() == Fourth.Id);

    // Every tile is drawn through one primitive, the frame's one transform.
    const UTerrainGroundComponent* Tiles = Ground->GetTilesComponent();
    if (!TestNotNull(TEXT("the ground draws its tiles through one component"), Tiles))
    {
        return false;
    }
    int32 Misplaced = 0;
    int32 Hidden = 0;
    for (const FTileKey& Key : Drawn)
    {
        const FTileBuild* Built = Ground->GetResidentTile(Key);
        if (!Built || !Tiles->HasTile(Key))
        {
            ++Misplaced;
            continue;
        }
        const FVector Expected = Ship->UniverseToWorld(Fourth.Position + FVector(Built->Pivot));
        Misplaced += (Tiles->GetTileWorldLocation(Key) - Expected).Size() > 1.0 ? 1 : 0;
        Hidden += Tiles->IsTileShown(Key) ? 0 : 1;
    }
    TestEqual(TEXT("every drawn tile sits at UniverseToWorld of its pivot"), Misplaced, 0);
    TestEqual(TEXT("and is shown"), Hidden, 0);
    TestEqual(TEXT("and nothing else is"), Tiles->GetShownCount(), Drawn.Num());
    TestFalse(TEXT("the tiles cast no shadow, reach no distance field, indirect light or ray tracing, and do not collide"),
              Tiles->CastShadow || Tiles->bAffectDistanceFieldLighting || Tiles->bAffectDynamicIndirectLighting
              || Tiles->bVisibleInRayTracing || Tiles->GetCollisionEnabled() != ECollisionEnabled::NoCollision || !Tiles->IsVisible());

    const double Nadir = Field->Height(FVector3d((Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal()), 0.0);
    const TOptional<double> DrawnHeight = Ground->DrawnHeightUnderShip();
    TestTrue(FString::Printf(TEXT("under 1 km the drawn ground under the ship is within 15 cm of the ground (%.2f cm)"),
                             DrawnHeight ? FMath::Abs(*DrawnHeight - Nadir) : -1.0),
             DrawnHeight.IsSet() && FMath::Abs(*DrawnHeight - Nadir) <= ShipLanding::DefaultGearClearanceCm / 10.0);
    TestEqual(TEXT("under the drive floor the relief is whole: the morph is 1"), Ground->GetMorph(), 1.0);

    // A few kilometres over, frame by frame: never more than
    // ds.Terrain.UploadsPerFrame tiles a frame. At the defaults (3 builds in
    // flight, 4 uploads) the cap can never bind -- at most three builds finish
    // a frame -- so this leg builds four at once against one upload a frame,
    // where finished tiles pile up and only the cap holds them back.
    FScopedCVar Builds(TEXT("ds.Terrain.BuildTasks"), 4.0f);
    FScopedCVar OneUpload(TEXT("ds.Terrain.UploadsPerFrame"), 1.0f);
    const FVector Aside = (Out + FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal() * 5.0e-4).GetSafeNormal();
    PlaceAt(Aside, 5.0e4);
    int32 Most = 0;
    int32 Frames = 0;
    for (int32 Frame = 0; Frame < 300; ++Frame)
    {
        FPlatformProcess::Sleep(0.005f);
        Test.Step(1.0f / 60.0f);
        Most = FMath::Max(Most, Ground->GetUploadsLastFrame());
        Frames += Ground->GetUploadsLastFrame() > 0 ? 1 : 0;
    }
    TestTrue(TEXT("tiles arrive as the ship moves"), Frames > 0);
    TestTrue(FString::Printf(TEXT("never more than ds.Terrain.UploadsPerFrame a frame (%d)"), Most),
             Most <= SkyTestWorld::CVarFloat(TEXT("ds.Terrain.UploadsPerFrame")));

    // ds.Terrain.Show 0 gives the body back.
    {
        FScopedCVar Show(TEXT("ds.Terrain.Show"), 0.0f);
        Test.Step(1.0f / 60.0f);
        TestFalse(TEXT("ds.Terrain.Show 0 hides the ground"), Ground->IsDrawingBody());
    }
    return true;
}

/*
 * The child ranges the cut remembers (AWorldGround::BoundsOf) are bounded by
 * the cut, not by the ground flown over: a long, low flight over one world
 * forgets what it has left behind: none is kept three levels from the cut,
 * and 160 km flown remembers at most 3.5 ranges for each key it needs. And what it keeps still does its job: settled, a ship that does not
 * move uploads nothing -- no tile at the horizon freed and built again,
 * frame after frame.
 */
bool FWorldGroundForgetsBoundsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("GroundForgetsBoundsWorld"));
    if (!TestNotNull(TEXT("the ground spawns before play"), Test.Ground))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    AWorldGround* Ground = Test.Ground;
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Fourth = Here.Bodies[4];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector Axis = FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
    const auto SettleAt = [&](const FVector& Up, int32 Passes)
    {
        Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + 5.0e4),
                        FRotationMatrix::MakeFromXZ(FVector::CrossProduct(Up, Axis).GetSafeNormal(), Up).ToQuat());
        for (int32 Pass = 0; Pass < Passes; ++Pass)
        {
            Test.Step(1.0f / 60.0f);
            Ground->FlushBuildsForTest();
        }
        Test.Step(1.0f / 60.0f);
    };

    // 500 m over, eight hops of 20 km: 160 km of ground. Kept for the
    // ground's life, the ranges grow by about 700 a hop, 6784 by the eighth
    // (4.8 for each key the cut needs); forgotten three levels from the
    // cut, 3188 (2.3 for each), and 800 km on still 3.3 for each.
    SettleAt(Out, 1);
    int32 Far = 0;
    FString Counts;
    FVector Up = Out;
    for (int32 Hop = 1; Hop <= 8; ++Hop)
    {
        Up = FQuat(Axis, 2.0e6 / Fourth.Radius).RotateVector(Up);
        SettleAt(Up, 1);
        Far = FMath::Max(Far, Ground->GetKnownBoundsFarFromCut());
        Counts += FString::Printf(TEXT(" %d"), Ground->GetKnownBoundsCount());
    }
    const int32 Last = Ground->GetKnownBoundsCount();
    const int32 Needed = Ground->GetNeededCount();
    AddInfo(FString::Printf(TEXT("ranges remembered after each hop:%s; %d keys needed at the end"), *Counts, Needed));
    TestTrue(TEXT("the flight remembers ranges"), Last > 0);
    TestEqual(TEXT("none is kept more than three levels from the cut"), Far, 0);
    TestTrue(FString::Printf(TEXT("and 160 km on, at most 3.5 for each key the cut needs (%d for %d)"), Last, Needed), 2 * Last <= 7 * Needed);

    // Settled at the last hop, standing still: nothing is built again.
    SettleAt(Up, 3);
    // Settled, standing still: nothing is built again.
    int32 Uploads = 0;
    for (int32 Frame = 0; Frame < 60; ++Frame)
    {
        FPlatformProcess::Sleep(0.002f);
        Test.Step(1.0f / 60.0f);
        Uploads += Ground->GetUploadsLastFrame();
    }
    TestEqual(TEXT("and a ship that does not move uploads nothing: no horizon tile is freed and built again"), Uploads, 0);
    return true;
}

#endif
