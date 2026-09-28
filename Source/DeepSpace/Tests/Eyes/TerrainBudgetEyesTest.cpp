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
#include "ProceduralMeshComponent.h"
#include "RenderingThread.h"
#include "TextureResource.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * THE FIRST-DAY GATE of landing slice (b) (decision 6, sign-off item 15): what
 * ProceduralMeshComponent costs at the tile counts the quadtree simulation
 * gave -- 2,200 tiles at 1.5 m, 2,500 at the MaxTiles ceiling -- in the
 * fixed topology every real tile has (33 x 33 and a 4 x 33 skirt, 2,304
 * triangles). Named outside DeepSpace. so ./test.sh never runs it; it must
 * render, so it never runs under -nullrhi:
 *
 *   Tools/eyes.sh Eyes.TerrainBudget
 *
 * It asserts the gate, so a red run is the verdict CUSTOM PRIMITIVE:
 *   - PMC's CPU copies (FProcMeshVertex, the double position and normal
 *     every section keeps) at 2,500 tiles, at most 400 MB;
 *   - a 4K capture of 2,200 tiles, over the empty scene's, at most 6 ms of
 *     render thread and GPU together (PMC draws on the dynamic path, so
 *     every tile's batch is rebuilt every pass);
 *   - moving every tile, as the ship-is-the-origin frame does every frame,
 *     at most 2 ms of game thread, and at most 2 ms more on the capture
 *     after it (GPU Scene updates).
 * The numbers go to Saved/Eyes/TerrainBudget/report.txt and the log.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainBudgetEyesTest, "Eyes.TerrainBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TerrainBudgetLocal
{
    constexpr int32 Cells = 32;
    constexpr int32 Side = Cells + 1;
    constexpr int32 Tiles = 2200;
    constexpr int32 MaxTiles = 2500;
    constexpr double TileCm = 1900.0;
    constexpr int32 Warmups = 3;
    constexpr int32 Captures = 20;

    constexpr double CopyBudgetMB = 400.0;
    constexpr double DrawBudgetMs = 6.0;
    constexpr double MoveBudgetMs = 2.0;
    constexpr double MovedDrawBudgetMs = 2.0;

    struct FSynthetic
    {
        TArray<FVector> Positions;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UV0;
        TArray<FVector2D> UV1;
        TArray<FVector2D> UV2;
    };

    void AddVertex(FSynthetic& Tile, const FVector& Position)
    {
        Tile.Positions.Add(Position);
        Tile.Normals.Add(FVector::UpVector);
        Tile.UV0.Add(FVector2D::ZeroVector);
        Tile.UV1.Add(FVector2D::ZeroVector);
        Tile.UV2.Add(FVector2D(1.0, 0.0));
    }

    /** The real topology, flat: nothing measured here depends on heights. */
    FSynthetic MakeTile()
    {
        FSynthetic Tile;
        for (int32 J = 0; J < Side; ++J)
        {
            for (int32 I = 0; I < Side; ++I)
            {
                AddVertex(Tile, FVector(I * TileCm / Cells, J * TileCm / Cells, 0.0));
            }
        }
        TArray<int32> Ring;
        for (int32 I = 0; I < Side; ++I) { Ring.Add(I); }
        for (int32 J = 0; J < Side; ++J) { Ring.Add(J * Side + Cells); }
        for (int32 I = Cells; I >= 0; --I) { Ring.Add(Cells * Side + I); }
        for (int32 J = Cells; J >= 0; --J) { Ring.Add(J * Side); }
        for (const int32 Grid : Ring)
        {
            AddVertex(Tile, Tile.Positions[Grid] - FVector(0.0, 0.0, 50.0));
        }
        for (int32 J = 0; J < Cells; ++J)
        {
            for (int32 I = 0; I < Cells; ++I)
            {
                const int32 A = J * Side + I;
                const int32 B = A + 1;
                const int32 C = A + Side;
                const int32 D = C + 1;
                Tile.Triangles.Append({ A, C, B, B, C, D });
            }
        }
        const int32 SkirtBase = Side * Side;
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            for (int32 K = 0; K < Cells; ++K)
            {
                const int32 R0 = Edge * Side + K;
                Tile.Triangles.Append({ Ring[R0], Ring[R0 + 1], SkirtBase + R0,
                                        Ring[R0 + 1], SkirtBase + R0 + 1, SkirtBase + R0 });
            }
        }
        return Tile;
    }

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

    const FSynthetic Tile = MakeTile();
    const int32 PerRow = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(Tiles)));
    const uint64 UsedBefore = FPlatformMemory::GetStats().UsedPhysical;
    const double CreateStart = FPlatformTime::Seconds();
    TArray<UProceduralMeshComponent*> Pool;
    for (int32 N = 0; N < Tiles; ++N)
    {
        UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(Holder);
        Mesh->SetupAttachment(Root);
        Mesh->bUseAsyncCooking = false;
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
        Mesh->bAffectDistanceFieldLighting = false;
        Mesh->bAffectDynamicIndirectLighting = false;
        Mesh->bVisibleInRayTracing = false;
        Mesh->RegisterComponent();
        Mesh->CreateMeshSection(0, Tile.Positions, Tile.Triangles, Tile.Normals, Tile.UV0, Tile.UV1, Tile.UV2,
                                TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
        Mesh->SetMaterial(0, Material);
        Mesh->SetRelativeLocation(FVector((N % PerRow - PerRow / 2) * TileCm, (N / PerRow - PerRow / 2) * TileCm, 0.0));
        Pool.Add(Mesh);
    }
    FlushRenderingCommands();
    const double CreateMsEach = (FPlatformTime::Seconds() - CreateStart) * 1000.0 / Tiles;
    const double ProcessMB = (static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) - UsedBefore) / 1048576.0;

    int64 CopyBytes = 0;
    for (UProceduralMeshComponent* Mesh : Pool)
    {
        if (const FProcMeshSection* Section = Mesh->GetProcMeshSection(0))
        {
            CopyBytes += Section->ProcVertexBuffer.Num() * static_cast<int64>(sizeof(FProcMeshVertex))
                       + Section->ProcIndexBuffer.Num() * static_cast<int64>(sizeof(uint32));
        }
    }
    const double CopyMBAtMax = CopyBytes / 1048576.0 * MaxTiles / Tiles;

    // What a pooled tile costs to refill: UpdateMeshSection, the per-frame
    // upload the budget allows four of.
    const double UpdateStart = FPlatformTime::Seconds();
    for (int32 N = 0; N < 100; ++N)
    {
        Pool[N]->UpdateMeshSection(0, Tile.Positions, Tile.Normals, Tile.UV0, Tile.UV1, Tile.UV2,
                                   TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>());
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

    for (UProceduralMeshComponent* Mesh : Pool) { Mesh->SetVisibility(false); }
    const double EmptyMs = CaptureMs(Capture, Target);
    for (UProceduralMeshComponent* Mesh : Pool) { Mesh->SetVisibility(true); }
    const double TilesMs = CaptureMs(Capture, Target);

    const double MoveStart = FPlatformTime::Seconds();
    for (UProceduralMeshComponent* Mesh : Pool)
    {
        Mesh->SetRelativeLocation(Mesh->GetRelativeLocation() + FVector(1.0, 0.0, 0.0));
    }
    const double MoveMs = (FPlatformTime::Seconds() - MoveStart) * 1000.0;
    const double MovedMs = CaptureMs(Capture, Target);

    const double DrawMs = TilesMs - EmptyMs;
    const double MovedDrawMs = MovedMs - TilesMs;
    const FString Report = FString::Printf(
        TEXT("tiles %d, %d triangles each\n")
        TEXT("create %.3f ms/tile, UpdateMeshSection %.3f ms/tile\n")
        TEXT("PMC CPU copies %.1f MB at %d tiles (%.1f MB counted at %d); process grew %.1f MB\n")
        TEXT("4K capture: empty %.2f ms, tiles %.2f ms (+%.2f), after moving every tile %.2f ms (+%.2f)\n")
        TEXT("moving every tile: %.2f ms of game thread\n"),
        Tiles, Tile.Triangles.Num() / 3, CreateMsEach, UpdateMsEach, CopyMBAtMax, MaxTiles,
        CopyBytes / 1048576.0, Tiles, ProcessMB, EmptyMs, TilesMs, DrawMs, MovedMs, MovedDrawMs, MoveMs);
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/TerrainBudget");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("report.txt")));
    AddInfo(Report);

    TestTrue(FString::Printf(TEXT("PMC's CPU copies at %d tiles are within %.0f MB (%.1f)"), MaxTiles, CopyBudgetMB, CopyMBAtMax),
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
