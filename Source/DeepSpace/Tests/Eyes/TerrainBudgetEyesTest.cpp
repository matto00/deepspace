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
#include "Surface/TerrainGroundComponent.h"
#include "Tests/GroundFixtures.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"

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
 * CUSTOM PRIMITIVE, recorded in the spec's decision 6. Its replacement, a
 * component per tile (UTerrainTileComponent, Task T5), drew in budget but
 * moved at +7.7 ms (below); since slice (b)'s last open items the tiles are
 * UTerrainGroundComponent, every tile in one primitive, and this measures
 * that, and that it draws at all:
 *   - the CPU copies (the FTileBuild the component holds per tile, as
 *     AWorldGround's resident cut shares it) at 2,500 tiles, at most 400 MB;
 *   - a 4K capture of 2,200 tiles, over the empty scene's, at most 6 ms of
 *     render thread and GPU together;
 *   - moving every tile, as the ship-is-the-origin frame does every frame --
 *     now the component's one transform -- at most 2 ms of game thread,
 *     and at most 2 ms more on the capture after it.
 * The numbers go to Saved/Eyes/TerrainBudget/report.txt and the log.
 *
 * EVERY CAPTURE IS ITS OWN ENGINE FRAMES. The test is latent: one capture
 * (warm-up or timed), one move and one base-colour check per step, four
 * engine frames apart, and each capture's time is its own -- capture, flush,
 * one-pixel read-back -- averaged over the timed ones as before. Inside one
 * RunTest no engine frame ends, and the Vulkan RHI retires what a capture
 * frees only at a frame's end, while a Development build scans the whole
 * deferred-deletion queue on every enqueue; so 69 captures in one frame each
 * cost more than the last, and the tiles' 20, taken after the empty scene's
 * 23, read +4.95 to +6.79 ms against 6 ms, intermittently. That is the
 * harness flaw Eyes.LandingFrame had (19342c9), fixed the same way.
 *
 * Each capture's clock starts with the render thread and the GPU idle (a
 * flush and a one-pixel read-back first), so the engine frame still in
 * flight when a step runs is nobody's cost; and the moved captures each move
 * every tile first and send the transforms (the frame's game thread, timed
 * with the move), as play does every frame, so the move's GPU Scene update
 * is in every one of them, never absorbed by the warm-ups. Before both, the
 * empty scene read 7.5-7.8 ms and the tiles +2.8 ms over it, and the move
 * +0.03..0.34 ms: the in-flight frame's wait, charged to the empty scene
 * and overlapped by the tiles', did not cancel. Measured so (2026-09-29, two
 * runs): empty 3.62-3.64 ms, the one-frame figure again; the tiles +6.13
 * and +5.97 ms against 6; every tile moved before each capture +7.81 and
 * +7.72 ms against 2; the move 2.15 and 2.12 ms of game thread against 2
 * (1.4 moving, 0.8 sending). Red, for the developer (the landing
 * cast-shadow plan's review record).
 *
 * One transform (slice (b)'s last open items, 2026-09-29). Measured first,
 * on the component per tile, the cheap candidate: the 2,200 tiles under one
 * parent and only the parent moved -- the engine still updates every child
 * -- read +8.17 ms on the capture and 1.67 ms of game thread, against the
 * same run's per-tile move at +7.72 and 2.01. So the tiles became one
 * primitive whose proxy draws each tile as its own mesh element with its
 * own primitive data, the pivot composed with the component's transform in
 * doubles as the frame is drawn.
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

    /** Engine frames between two steps: the Vulkan RHI retires a freed
     *  resource two frames after the frame it was freed in. */
    constexpr int32 FramesBetweenSteps = 4;

    /** Render thread and GPU together: capture, flush, then read one pixel
     *  back, which cannot return before the GPU has drawn the frame. One
     *  capture, in ms. The clock starts with the render thread and the GPU
     *  idle -- a flush and a read-back first -- so no capture is charged for
     *  the engine frame still in flight when its step runs: without that the
     *  empty scene read 7.5-7.8 ms here against 3.5 ms in one frame, and only
     *  the difference was trusted. */
    double CaptureMs(USceneCaptureComponent2D* Capture, UTextureRenderTarget2D* Target)
    {
        TArray<FColor> Pixel;
        FlushRenderingCommands();
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
        const double Start = FPlatformTime::Seconds();
        Capture->CaptureScene();
        FlushRenderingCommands();
        Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
        return (FPlatformTime::Seconds() - Start) * 1000.0;
    }

    /** What the steps share: the world, the tiles, the capture, and the sums. */
    struct FBudgetRun
    {
        FAutomationTestBase* Automation = nullptr;
        UWorld* World = nullptr;
        TStrongObjectPtr<AActor> Holder;
        TStrongObjectPtr<USceneCaptureComponent2D> Capture;
        TStrongObjectPtr<UTextureRenderTarget2D> Target;
        TStrongObjectPtr<UTerrainGroundComponent> Ground;
        TArray<FTileKey> Keys;
        TArray<TFunction<void()>> Steps;
        double CreateMsEach = 0.0;
        double ProcessMB = 0.0;
        int64 CopyBytes = 0;
        double CopyMBAtMax = 0.0;
        double UpdateMsEach = 0.0;
        double EmptyMs = 0.0;
        double TilesMs = 0.0;
        double MovedMs = 0.0;
        double MoveMs = 0.0;
        double SendMs = 0.0;
        TArray<double> Spread[3];   // each timed capture: empty, tiles, moved
        int32 EmptyGrey = -1;
        int32 TileGrey = -1;

        void ShowTiles(bool bShown)
        {
            Ground->SetShown(bShown ? Keys : TArray<FTileKey>());
        }

        /** Moves every tile a centimetre, as the ship-is-the-origin frame
         *  does every frame -- the one component's transform; the game
         *  thread's time into MoveMs. */
        void MoveTiles()
        {
            const double MoveStart = FPlatformTime::Seconds();
            Ground->SetRelativeLocation(Ground->GetRelativeLocation() + FVector(1.0, 0.0, 0.0));
            const double Moved = FPlatformTime::Seconds();
            // The end of the frame's game thread: the new transforms sent to
            // the render thread. CaptureScene would send them itself, inside
            // the draw's clock; the frame pays them on the game thread.
            World->SendAllEndOfFrameUpdates();
            const double Sent = FPlatformTime::Seconds();
            MoveMs += (Moved - MoveStart) * 1000.0 / (Warmups + Captures);
            SendMs += (Sent - Moved) * 1000.0 / (Warmups + Captures);
        }

        /** Warmups untimed captures, then Captures timed ones, each its own
         *  step; Into gets their mean. With bMove every step moves every
         *  tile first, as play does before each frame's draw, so each timed
         *  capture pays that move's GPU Scene update -- a warm-up can no
         *  longer absorb it, as the single move before the captures let it. */
        void QueueCaptures(double* Into, int32 Which, bool bMove = false)
        {
            for (int32 Warm = 0; Warm < Warmups; ++Warm)
            {
                Steps.Add([this, bMove]()
                {
                    if (bMove)
                    {
                        MoveTiles();
                    }
                    CaptureMs(Capture.Get(), Target.Get());
                });
            }
            for (int32 Shot = 0; Shot < Captures; ++Shot)
            {
                Steps.Add([this, Into, Which, bMove]()
                {
                    if (bMove)
                    {
                        MoveTiles();
                    }
                    const double Ms = CaptureMs(Capture.Get(), Target.Get());
                    Spread[Which].Add(Ms);
                    *Into += Ms / Captures;
                });
            }
        }

        /** The base colour at the centre, tiles shown or not: a budget met
         *  by drawing nothing is no verdict. */
        int32 CentreBaseColour(bool bShown)
        {
            ShowTiles(bShown);
            Capture->CaptureSource = SCS_BaseColor;
            Capture->CaptureScene();
            FlushRenderingCommands();
            TArray<FColor> Pixel;
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(1920, 1080, 1921, 1081));
            Capture->CaptureSource = SCS_FinalColorLDR;
            return Pixel.Num() > 0 ? static_cast<int32>(Pixel[0].G) : -1;
        }

        void Close()
        {
            FAutomationTestBase& T = *Automation;
            T.TestTrue(FString::Printf(TEXT("the tiles are drawn: the centre's base colour is %d with them, %d without"), TileGrey, EmptyGrey),
                       TileGrey > EmptyGrey + 20);
            const double DrawMs = TilesMs - EmptyMs;
            const double MovedDrawMs = MovedMs - TilesMs;
            const FString Report = FString::Printf(
                TEXT("tiles %d, %d triangles each\n")
                TEXT("add %.3f ms/tile, re-add %.3f ms/tile\n")
                TEXT("tile CPU copies %.1f MB at %d tiles (%.1f MB counted at %d); process grew %.1f MB\n")
                TEXT("4K capture: empty %.2f ms, tiles %.2f ms (+%.2f), every tile moved before each %.2f ms (+%.2f)\n")
                TEXT("  each capture, min..max: empty %.2f..%.2f, tiles %.2f..%.2f, moved %.2f..%.2f ms\n")
                TEXT("moving every tile: %.2f ms of game thread, %.2f ms moving and %.2f ms sending the transforms\n"),
                Tiles, TerrainTile::TriangleCount, CreateMsEach, UpdateMsEach, CopyMBAtMax, MaxTiles,
                CopyBytes / 1048576.0, Tiles, ProcessMB, EmptyMs, TilesMs, DrawMs, MovedMs, MovedDrawMs,
                FMath::Min(Spread[0]), FMath::Max(Spread[0]), FMath::Min(Spread[1]), FMath::Max(Spread[1]),
                FMath::Min(Spread[2]), FMath::Max(Spread[2]), MoveMs + SendMs, MoveMs, SendMs);
            const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/TerrainBudget");
            IFileManager::Get().MakeDirectory(*Dir, true);
            FFileHelper::SaveStringToFile(Report, *(Dir / TEXT("report.txt")));
            T.AddInfo(Report);

            T.TestTrue(FString::Printf(TEXT("the tiles' CPU copies at %d tiles are within %.0f MB (%.1f)"), MaxTiles, CopyBudgetMB, CopyMBAtMax),
                       CopyMBAtMax <= CopyBudgetMB);
            T.TestTrue(FString::Printf(TEXT("2,200 tiles cost at most %.0f ms of a 4K frame (%.2f)"), DrawBudgetMs, DrawMs),
                       DrawMs <= DrawBudgetMs);
            T.TestTrue(FString::Printf(TEXT("moving every tile costs at most %.0f ms of game thread (%.2f)"), MoveBudgetMs, MoveMs + SendMs),
                       MoveMs + SendMs <= MoveBudgetMs);
            T.TestTrue(FString::Printf(TEXT("and at most %.0f ms more to draw after (%.2f)"), MovedDrawBudgetMs, MovedDrawMs),
                       MovedDrawMs <= MovedDrawBudgetMs);

            Ground.Reset();
            Capture.Reset();
            Target.Reset();
            Holder.Reset();
            World->EndPlay(EEndPlayReason::RemovedFromWorld);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };

    /** Runs the steps one at a time, FramesBetweenSteps engine frames apart. */
    class FTerrainBudgetSteps : public IAutomationLatentCommand
    {
    public:
        explicit FTerrainBudgetSteps(TSharedRef<FBudgetRun> InRun) : Run(InRun) {}

        virtual bool Update() override
        {
            if (Wait > 0)
            {
                --Wait;
                return false;
            }
            if (Next < Run->Steps.Num())
            {
                Run->Steps[Next++]();
                Wait = FramesBetweenSteps;
                return false;
            }
            Run->Close();
            return true;
        }

    private:
        TSharedRef<FBudgetRun> Run;
        int32 Next = 0;
        int32 Wait = 0;
    };
}

bool FTerrainBudgetEyesTest::RunTest(const FString& Parameters)
{
    using namespace TerrainBudgetLocal;
    TSharedRef<FBudgetRun> Run = MakeShared<FBudgetRun>();
    Run->Automation = this;

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TerrainBudgetWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    Run->World = World;

    AActor* Holder = World->SpawnActor<AActor>();
    Run->Holder.Reset(Holder);
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
    UTerrainGroundComponent* Tiled = NewObject<UTerrainGroundComponent>(Holder);
    Tiled->SetupAttachment(Root);
    Tiled->RegisterComponent();
    Tiled->SetMaterial(0, Material);
    Run->Ground.Reset(Tiled);
    // Each tile its own copy, with its own key, its pivot on a square grid
    // in the component's space: what AWorldGround hands it, tile by tile.
    const auto Copy = [&Tile, PerRow](int32 N)
    {
        FTileBuild Placed = Tile;
        Placed.Key = FTileKey{ Tile.Key.Face, Tile.Key.Level, static_cast<uint32>(N % PerRow), static_cast<uint32>(N / PerRow) };
        Placed.Pivot = FVector3d((N % PerRow - PerRow / 2) * TileCm, (N / PerRow - PerRow / 2) * TileCm, 0.0);
        return MakeShared<const FTileBuild, ESPMode::ThreadSafe>(MoveTemp(Placed));
    };
    TArray<UTerrainGroundComponent::FTileRef> Built;
    for (int32 N = 0; N < Tiles; ++N)
    {
        Built.Add(Copy(N));
        Run->Keys.Add(Built.Last()->Key);
    }
    const uint64 UsedBefore = FPlatformMemory::GetStats().UsedPhysical;
    const double CreateStart = FPlatformTime::Seconds();
    for (const UTerrainGroundComponent::FTileRef& One : Built)
    {
        Tiled->AddTile(One, 1.0e-4f);
    }
    Tiled->SetShown(Run->Keys);
    FlushRenderingCommands();
    Run->CreateMsEach = (FPlatformTime::Seconds() - CreateStart) * 1000.0 / Tiles;
    Run->ProcessMB = (static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) - UsedBefore) / 1048576.0;

    for (const FTileBuild* Kept : Tiled->GetTiles())
    {
        Run->CopyBytes += Kept->Positions.GetAllocatedSize() + Kept->Normals.GetAllocatedSize()
                        + Kept->Heights.GetAllocatedSize() + Kept->Directions.GetAllocatedSize() + sizeof(FTileBuild);
    }
    Run->CopyMBAtMax = Run->CopyBytes / 1048576.0 * MaxTiles / Tiles;

    // What a tile costs to upload: its buffers filled, and the render
    // thread's share, the per-frame upload the budget allows four of.
    const double UpdateStart = FPlatformTime::Seconds();
    for (int32 N = 0; N < 100; ++N)
    {
        Tiled->AddTile(Built[N], 1.0e-4f);
    }
    Tiled->SetShown(Run->Keys);
    FlushRenderingCommands();
    Run->UpdateMsEach = (FPlatformTime::Seconds() - UpdateStart) * 1000.0 / 100.0;

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
    Run->Capture.Reset(Capture);
    Run->Target.Reset(Target);

    // The same captures in the same order as ever, each now its own step.
    FBudgetRun& R = *Run;
    R.Steps.Add([&R]() { R.ShowTiles(false); });
    R.QueueCaptures(&R.EmptyMs, 0);
    R.Steps.Add([&R]() { R.ShowTiles(true); });
    R.QueueCaptures(&R.TilesMs, 1);
    R.QueueCaptures(&R.MovedMs, 2, true);
    // The world has no light, so the lit colour is black either way; the
    // base colour pass shows the default material's grey where a tile is,
    // and nothing where none is.
    R.Steps.Add([&R]() { R.EmptyGrey = R.CentreBaseColour(false); });
    R.Steps.Add([&R]() { R.TileGrey = R.CentreBaseColour(true); });
    ADD_LATENT_AUTOMATION_COMMAND(FTerrainBudgetSteps(Run));
    return true;
}

#endif
