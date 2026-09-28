#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Surface/TerrainTileComponent.h"
#include "Tests/GroundFixtures.h"
#include "TextureResource.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * THE FIRST-DAY GATE of landing slice (b) (decision 6, sign-off item 15): what
 * the terrain's tile primitive costs at the tile counts the quadtree simulation
 * gave -- 2,200 tiles at 1.5 m, 2,500 at the MaxTiles ceiling -- in the
 * fixed topology every real tile has (33 x 33 and a 4 x 33 skirt, 2,304
 * triangles). Named outside DeepSpace. so ./test.sh never runs it; it must
 * render, so it never runs under -nullrhi:
 *
 *   Tools/eyes.sh Eyes.TerrainBudget
 *
 * It asserts the gate. Run first on ProceduralMeshComponent, it failed three
 * of four (PMC's 148-byte FProcMeshVertex copies, 508.4 MB at 2,500 tiles;
 * its dynamic-path draw, 68.86 ms, and 108.36 ms after a move): the verdict
 * CUSTOM PRIMITIVE, recorded in the spec's decision 6. It now measures what
 * replaced it, UTerrainTileComponent (Task T5), and that it draws at all:
 *   - the CPU copies (the FTileBuild each tile component keeps, as
 *     AWorldGround sets bKeepForTest) at 2,500 tiles, at most 400 MB;
 *   - a 4K capture of 2,200 tiles, over the empty scene's, at most 6 ms of
 *     render thread and GPU together;
 *   - moving every tile, as the ship-is-the-origin frame does every frame,
 *     at most 2 ms of game thread, and at most 2 ms more on the capture
 *     after it (GPU Scene updates).
 * The numbers go to Saved/Eyes/TerrainBudget/report.txt and the log.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainBudgetEyesTest, "Eyes.TerrainBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TerrainBudgetLocal
{
    constexpr int32 Tiles = 2200;
    constexpr int32 MaxTiles = 2500;
    constexpr double TileCm = 1900.0;
    constexpr int32 Warmups = 3;
    constexpr int32 Captures = 20;

    constexpr double CopyBudgetMB = 400.0;
    constexpr double DrawBudgetMs = 6.0;
    constexpr double MoveBudgetMs = 2.0;
    constexpr double MovedDrawBudgetMs = 2.0;

    /** Render thread and GPU together: capture, flush, then read one pixel
     *  back, which cannot return before the GPU has drawn the frame. */
    double CaptureMs(USceneCaptureComponent2D* Capture, UTextureRenderTarget2D* Target)
    {
        TArray<FColor> Pixel;
        auto One = [&]()
        {
            Capture->CaptureScene();
            FlushRenderingCommands();
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
        };
        for (int32 Warm = 0; Warm < Warmups; ++Warm)
        {
            One();
        }
        const double Start = FPlatformTime::Seconds();
        for (int32 Shot = 0; Shot < Captures; ++Shot)
        {
            One();
        }
        return (FPlatformTime::Seconds() - Start) * 1000.0 / Captures;
    }
}

bool FTerrainBudgetEyesTest::RunTest(const FString& Parameters)
{
    using namespace TerrainBudgetLocal;

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TerrainBudgetWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    AActor* Holder = World->SpawnActor<AActor>();
    USceneComponent* Root = NewObject<USceneComponent>(Holder, TEXT("Root"));
    Holder->SetRootComponent(Root);
    Root->RegisterComponent();
    UMaterialInterface* Material = UMaterial::GetDefaultMaterial(MD_Surface);

    // One real tile at the finest level, flat enough at the pole to lie in
    // the capture's view: every component draws the same topology.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FGroundFieldRef Ground = ShipGround::FromRelief(Params);
    const FTileBuild Tile = TerrainTile::Build(*Ground, TerrainQuadtree::KeyAt(FVector3d(0.0, 0.0, 1.0), TerrainQuadtree::MaxLevel(Params.RadiusCm)));
    const int32 PerRow = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(Tiles)));
    const uint64 UsedBefore = FPlatformMemory::GetStats().UsedPhysical;
    const double CreateStart = FPlatformTime::Seconds();
    TArray<UTerrainTileComponent*> Pool;
    for (int32 N = 0; N < Tiles; ++N)
    {
        UTerrainTileComponent* Mesh = NewObject<UTerrainTileComponent>(Holder);
        Mesh->bKeepForTest = true;
        Mesh->SetupAttachment(Root);
        Mesh->RegisterComponent();
        Mesh->SetMaterial(0, Material);
        Mesh->SetTile(Tile);
        Mesh->SetRelativeLocation(FVector((N % PerRow - PerRow / 2) * TileCm, (N / PerRow - PerRow / 2) * TileCm, 0.0));
        Pool.Add(Mesh);
    }
    FlushRenderingCommands();
    const double CreateMsEach = (FPlatformTime::Seconds() - CreateStart) * 1000.0 / Tiles;
    const double ProcessMB = (static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) - UsedBefore) / 1048576.0;

    int64 CopyBytes = 0;
    for (UTerrainTileComponent* Mesh : Pool)
    {
        if (const FTileBuild* Kept = Mesh->GetTileForTest())
        {
            CopyBytes += Kept->Positions.GetAllocatedSize() + Kept->Normals.GetAllocatedSize()
                       + Kept->Heights.GetAllocatedSize() + Kept->Directions.GetAllocatedSize() + sizeof(FTileBuild);
        }
    }
    const double CopyMBAtMax = CopyBytes / 1048576.0 * MaxTiles / Tiles;

    // What a pooled tile costs to refill: SetTile, a new render proxy, the
    // per-frame upload the budget allows four of.
    const double UpdateStart = FPlatformTime::Seconds();
    for (int32 N = 0; N < 100; ++N)
    {
        Pool[N]->SetTile(Tile);
    }
    FlushRenderingCommands();
    const double UpdateMsEach = (FPlatformTime::Seconds() - UpdateStart) * 1000.0 / 100.0;

    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Holder);
    Capture->SetupAttachment(Root);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Holder);
    Target->InitCustomFormat(3840, 2160, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->FOVAngle = 90.0f;
    Capture->CaptureSource = SCS_FinalColorLDR;
    // 800 m up, straight down: the whole 890 m square of tiles is in view.
    Capture->SetRelativeLocationAndRotation(FVector(0.0, 0.0, 80000.0), FRotator(-90.0, 0.0, 0.0));

    for (UTerrainTileComponent* Mesh : Pool) { Mesh->SetVisibility(false); }
    const double EmptyMs = CaptureMs(Capture, Target);
    for (UTerrainTileComponent* Mesh : Pool) { Mesh->SetVisibility(true); }
    const double TilesMs = CaptureMs(Capture, Target);

    const double MoveStart = FPlatformTime::Seconds();
    for (UTerrainTileComponent* Mesh : Pool)
    {
        Mesh->SetRelativeLocation(Mesh->GetRelativeLocation() + FVector(1.0, 0.0, 0.0));
    }
    const double MoveMs = (FPlatformTime::Seconds() - MoveStart) * 1000.0;
    const double MovedMs = CaptureMs(Capture, Target);

    // A budget met by drawing nothing is no verdict: the tiles must reach
    // the picture. The world has no light, so the lit colour is black either
    // way; the base colour pass shows the default material's grey where a
    // tile is, and nothing where none is.
    const auto CentreBaseColour = [&](bool bShown)
    {
        for (UTerrainTileComponent* Mesh : Pool) { Mesh->SetVisibility(bShown); }
        Capture->CaptureSource = SCS_BaseColor;
        Capture->CaptureScene();
        FlushRenderingCommands();
        TArray<FColor> Pixel;
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(1920, 1080, 1921, 1081));
        Capture->CaptureSource = SCS_FinalColorLDR;
        return Pixel.Num() > 0 ? static_cast<int32>(Pixel[0].G) : -1;
    };
    const int32 EmptyGrey = CentreBaseColour(false);
    const int32 TileGrey = CentreBaseColour(true);
    TestTrue(FString::Printf(TEXT("the tiles are drawn: the centre's base colour is %d with them, %d without"), TileGrey, EmptyGrey),
             TileGrey > EmptyGrey + 20);

    const double DrawMs = TilesMs - EmptyMs;
    const double MovedDrawMs = MovedMs - TilesMs;
    const FString Report = FString::Printf(
        TEXT("tiles %d, %d triangles each\n")
        TEXT("create %.3f ms/tile, SetTile %.3f ms/tile\n")
        TEXT("tile CPU copies %.1f MB at %d tiles (%.1f MB counted at %d); process grew %.1f MB\n")
        TEXT("4K capture: empty %.2f ms, tiles %.2f ms (+%.2f), after moving every tile %.2f ms (+%.2f)\n")
        TEXT("moving every tile: %.2f ms of game thread\n"),
        Tiles, TerrainTile::TriangleCount, CreateMsEach, UpdateMsEach, CopyMBAtMax, MaxTiles,
        CopyBytes / 1048576.0, Tiles, ProcessMB, EmptyMs, TilesMs, DrawMs, MovedMs, MovedDrawMs, MoveMs);
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/TerrainBudget");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("report.txt")));
    AddInfo(Report);

    TestTrue(FString::Printf(TEXT("the tiles' CPU copies at %d tiles are within %.0f MB (%.1f)"), MaxTiles, CopyBudgetMB, CopyMBAtMax),
             CopyMBAtMax <= CopyBudgetMB);
    TestTrue(FString::Printf(TEXT("2,200 tiles cost at most %.0f ms of a 4K frame (%.2f)"), DrawBudgetMs, DrawMs),
             DrawMs <= DrawBudgetMs);
    TestTrue(FString::Printf(TEXT("moving every tile costs at most %.0f ms of game thread (%.2f)"), MoveBudgetMs, MoveMs),
             MoveMs <= MoveBudgetMs);
    TestTrue(FString::Printf(TEXT("and at most %.0f ms more to draw after (%.2f)"), MovedDrawBudgetMs, MovedDrawMs),
             MovedDrawMs <= MovedDrawBudgetMs);

    World->EndPlay(EEndPlayReason::RemovedFromWorld);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
