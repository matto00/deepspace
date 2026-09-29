#include "DynamicRHI.h"
#include "Engine/Texture2DDynamic.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyProjection.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/SunShadowMap.h"
#include "Surface/TerrainTile.h"
#include "Surface/WorldGround.h"
#include "Tests/GroundKeepsUpScenario.h"
#include "Tests/SkyTestWorld.h"
#include "TextureResource.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A MEASUREMENT, not a guard: what the baked cast shadow costs (the
 * cast-shadow plan, Task 7). Named outside DeepSpace. so ./test.sh never runs
 * it -- it is timed, and a timing belongs behind the lock on a quiet machine
 * -- and run as the rendered checks are:
 *
 *   Tools/eyes.sh Eyes.ShadowBakeCost
 *
 * It renders nothing. It measures:
 *
 * - The cold cut: the ground at 1.5 m over Baemsekai IV under a 10-degree
 *   dusk, resident from nothing, without and with the shadow
 *   (ds.Terrain.Shadows 0 then 1), on the ground's own tasks; and a release
 *   of the ground with shadowed builds in flight, on the game thread.
 * - A tile: 64 of that cut's keys, each built here, on this thread, without
 *   the shadow and with it under a 3-, 10- and 60-degree sun over the cut's
 *   centre. Medians and 90th percentiles of BuildSeconds, and their ratio.
 * - A world's map: Baemsekai I-V, and the corpus's two extremes -- the most
 *   relief for its size (Baiti I: 0.44 Earth radii, 9.74 km) and the
 *   largest solid world (Tishras I: 2.13, 2.57 km) -- at Cratering 1, the
 *   costliest, under IV's light, each baked here on one thread at
 *   ds.Sky.ShadowMapWidth.
 * - The system: the sky's own bakes of every solid world on
 *   ds.Sky.ShadowBakeTasks, the GPU memory they hold as the RHI counts it
 *   (RHIComputeMemorySize), what the sky still holds on the CPU for their
 *   uploads, the process's physical memory before and after, and the
 *   landing: the slowest frame's game-thread work (a texture's creation and
 *   that frame's pieces handed over) and the render thread's on the slowest
 *   frame's pieces -- a map goes up in pieces over several frames (the
 *   developer's ruling, 2026-09-28).
 * - The per-system cap (ruled 2026-09-28: 128 MB): the corpus's worst system
 *   -- Trabo, 12 solid worlds in Saved/procgen_corpus.tsv today; planning put
 *   it at 16 -- baked by the sky itself, its widths after the cap and its
 *   GPU memory; and the same system with four of its worlds again, 16 in
 *   all, held to the same cap. Run last, in a world of its own, and landed
 *   over engine frames, a frame's pieces a frame, as play lands them
 *   (FLandWorstSystems): landed inside RunTest, where no frame ends and the
 *   Vulkan RHI never recycles a staging buffer, a new staging page stalled
 *   one piece 4.3-5.2 ms about every 35 MB, at 512 KB and 128 KB pieces
 *   alike -- map_upload_rt_ms_all 4.54 and 5.19 ms; over frames, 0.11.
 * - A jump within the system: the bakes it starts.
 * - The flight: GroundKeepsUp's, with the shadow on, paced to the wall clock;
 *   and the wait, from 49 km over fresh ground, for the ground to draw the
 *   body.
 * - The resident cut's vertex shadows, both copies, from the arrays.
 *
 * Each budget prints one line, `budget <name> <measured> <limit> within|OVER`;
 * the plan's rule reads them. No timing is asserted, because a loaded
 * machine would make a timing assertion a coin toss. Memory is asserted: every
 * solid world's map baked and on the GPU, each system's maps within the
 * per-system cap as the RHI sizes them, and the cut's vertex shadows within
 * their budget.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowBakeCostEyesTest, "Eyes.ShadowBakeCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShadowBakeCostLocal
{
    // The budgets: Task 0's answers, and Task 7b's rulings (2026-09-28).
    constexpr double ColdCutBudgetSeconds = 30.0;
    constexpr double ReleaseBudgetMs = 2.0;
    constexpr double TileRatioBudget = 10.0;
    constexpr double WorldBakeBudgetSeconds = 15.0;
    constexpr double WorldMapBudgetMB = 16.0;
    constexpr double SystemGpuBudgetMB = 128.0;   // the per-system cap, ruled: every system, the worst included
    constexpr double SystemCpuBudgetMB = 0.0;
    constexpr double MapLandBudgetMs = 4.0;       // the game thread's slowest frame of a landing
    constexpr double MapUploadBudgetMs = 4.0;     // and the render thread's: no frame hitches, either side
    constexpr double JumpRebakesBudget = 0.0;
    constexpr double CoarseBudgetSeconds = 10.0;
    constexpr double TileShadowBudgetMB = 12.0;
    // Saved/procgen_corpus.tsv's system with the most solid worlds (12), today.
    const FSystemId WorstSystem{ FInt64Vector(-3, -10, -1), 0 };
    constexpr int32 WorstSystemWorlds = 16;   // planning's figure, made from it with four worlds again
    constexpr double EarthRadiusCm = 6.371e8;

    double Percentile(TArray<double> Values, double Share)
    {
        if (Values.Num() == 0)
        {
            return 0.0;
        }
        Values.Sort();
        return Values[FMath::Clamp(FMath::FloorToInt32(Share * (Values.Num() - 1)), 0, Values.Num() - 1)];
    }

    FString Budget(const TCHAR* Name, double Measured, double Limit)
    {
        return FString::Printf(TEXT("budget %s %.3f %.3f %s"), Name, Measured, Limit, Measured <= Limit ? TEXT("within") : TEXT("OVER"));
    }

    double MB(double Bytes)
    {
        return Bytes / (1024.0 * 1024.0);
    }

    /** The GPU's own size for a texture, as the RHI counts it; after a flush. */
    double GpuBytes(const UTexture2DDynamic* Texture)
    {
        const FTextureResource* Resource = Texture ? Texture->GetResource() : nullptr;
        return Resource && Resource->TextureRHI ? static_cast<double>(RHIComputeMemorySize(Resource->TextureRHI)) : 0.0;
    }

    struct FSystemMemory
    {
        int32 Maps = 0;
        double GpuMB = 0.0;
        double LevelsMB = 0.0;
        TArray<FString> Widths;
    };

    int32 SolidWorlds(const FSkySystem& System)
    {
        int32 Solid = 0;
        for (const FSkyBody& Body : System.Bodies)
        {
            Solid += Body.Ground == EGround::Solid && Body.Kind != ESkyBodyKind::Star ? 1 : 0;
        }
        return Solid;
    }

    /** Every solid world's texture the sky holds for System, as the RHI
     *  counts it -- and every solid world must have one, or the memory is
     *  a count of the maps that happened to land. */
    FSystemMemory Measure(FAutomationTestBase& T, AShipSky& Sky, const FSkySystem& System)
    {
        FSystemMemory M;
        ON_SCOPE_EXIT
        {
            T.TestEqual(FString::Printf(TEXT("%s: every solid world's map is baked and on the GPU"), *System.SystemId.ToString()),
                M.Maps, SolidWorlds(System));
        };
        for (const FSkyBody& Body : System.Bodies)
        {
            UTexture2DDynamic* Texture = Sky.GetShadowTexture(Body.Id);
            const FSunShadowMap* Map = Sky.GetShadowMapForTest(Body.Id);
            if (!Texture || !Map)
            {
                continue;
            }
            ++M.Maps;
            M.GpuMB += MB(GpuBytes(Texture));
            M.LevelsMB += MB(Map->Bytes());
            M.Widths.Add(FString::Printf(TEXT("%s %d"), *Body.Id.ToString(), Map->Width));
            const FTextureResource* Resource = Texture->GetResource();
            T.TestTrue(FString::Printf(TEXT("%s: the GPU holds every level at the map's width"), *Body.Id.ToString()),
                Resource && Resource->TextureRHI && Resource->GetSizeX() == static_cast<uint32>(Map->Width)
                && Resource->TextureRHI->GetNumMips() == static_cast<uint32>(Map->LevelCount()));
        }
        return M;
    }
}

namespace ShadowBakeCostLocal
{
    /** The worst systems' part, latent: every map lands over engine frames,
     *  a frame's pieces a frame, as play lands them. Inside one RunTest no
     *  engine frame ends, and the Vulkan RHI recycles an upload's staging
     *  buffer only at a frame's end, so landing Trabo's maps there grew the
     *  staging pool by every piece and a new page of it stalled one piece
     *  4.3-5.2 ms about every 35 MB, at 512 KB pieces and at 128 KB alike
     *  (2026-09-29) -- the harness's cost, which play never pays. */
    struct FWorstSystems
    {
        FAutomationTestBase* Automation = nullptr;
        TArray<FString> Report;
        TArray<FSkySystem> Systems;
        int32 Serial = 0;
        TUniquePtr<SkyTestWorld::FSkyWorld> Test;
        int32 Next = -1;          // -1: the world's own start system is landing
        double Started = 0.0;
        int32 Frames = 0;

        void Line(const FString& Text)
        {
            Report.Add(Text);
            Automation->AddInfo(Text);
        }

        void Close()
        {
            Line(Budget(TEXT("map_land_ms_all"), 1e3 * Test->Sky->GetSlowestShadowLandSeconds(), MapLandBudgetMs));
            Line(Budget(TEXT("map_upload_rt_ms_all"), 1e3 * Test->Sky->GetSlowestShadowUploadRenderSeconds(), MapUploadBudgetMs));
            const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/ShadowBakeCost");
            IFileManager::Get().MakeDirectory(*Dir, true);
            FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            Test.Reset();
        }
    };

    class FLandWorstSystems : public IAutomationLatentCommand
    {
    public:
        explicit FLandWorstSystems(TSharedRef<FWorstSystems> InRun) : Run(InRun) {}

        virtual bool Update() override
        {
            FWorstSystems& R = *Run;
            if (!R.Test)
            {
                R.Test = MakeUnique<SkyTestWorld::FSkyWorld>(TEXT("ShadowBakeCostWorstWorld"), 8, SkyTestWorld::EShadows::On);
                R.Test->Sky->bKeepShadowMapsForTest = true;
                R.Test->BeginPlay();
                R.Test->Step(1.0f / 60.0f);
                return false;
            }
            // One frame's pieces a frame, the bakes waited for.
            R.Test->Sky->PumpShadowBakesForTest();
            ++R.Frames;
            if (R.Test->Sky->GetShadowBakesPending() > 0)
            {
                return false;
            }
            if (R.Next >= 0)
            {
                FlushRenderingCommands();
                const FSkySystem& System = R.Systems[R.Next];
                const double Seconds = FPlatformTime::Seconds() - R.Started;
                const FSystemMemory M = Measure(*R.Automation, *R.Test->Sky, System);
                const TCHAR* Name = R.Next == 0 ? TEXT("worst") : TEXT("worst16");
                R.Line(FString::Printf(TEXT("system %s (%s): %d solid worlds baked and landed over %d engine frames in %.2f s, GPU %.2f MB resident (the levels are %.2f MB) under the %.0f MB cap; widths %s"),
                    Name, *System.SystemId.ToString(), M.Maps, R.Frames, Seconds, M.GpuMB, M.LevelsMB, MB(static_cast<double>(ShipSky::ShadowSystemCapBytes)), *FString::Join(M.Widths, TEXT(", "))));
                R.Line(Budget(R.Next == 0 ? TEXT("system_gpu_mb_worst") : TEXT("system_gpu_mb_worst16"), M.GpuMB, SystemGpuBudgetMB));
                // The path the game uses: SyncShadowMaps' widths, sized by the
                // RHI. The levels' own bytes are smaller, and a cap met on them
                // left Trabo's twelve at 142 MB on the GPU.
                R.Automation->TestTrue(FString::Printf(TEXT("%s's maps fit the per-system cap on the GPU (%.2f MB)"), Name, M.GpuMB),
                    M.GpuMB <= MB(static_cast<double>(ShipSky::ShadowSystemCapBytes)));
            }
            if (++R.Next >= R.Systems.Num())
            {
                R.Close();
                return true;
            }
            R.Started = FPlatformTime::Seconds();
            R.Frames = 0;
            R.Test->Sky->SyncTo(R.Systems[R.Next], ++R.Serial, false);
            return false;
        }

    private:
        TSharedRef<FWorstSystems> Run;
    };
}

bool FShadowBakeCostEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShadowBakeCostLocal;
    // The game's own ds.Terrain.BuildTasks (3, ruled), never pinned.
    FScopedCVar Uploads(TEXT("ds.Terrain.UploadsPerFrame"), 4.0f);
    const FPlatformMemoryStats Before = FPlatformMemory::GetStats();
    FSkyWorld Test(TEXT("ShadowBakeCostWorld"), 8, EShadows::On);
    Test.Sky->bKeepShadowMapsForTest = true;
    Test.BeginPlay();
    Test.Step(1.0f / 60.0f);
    const double SkyStart = FPlatformTime::Seconds();
    Test.Sky->FlushShadowBakesForTest();
    const double SkySeconds = FPlatformTime::Seconds() - SkyStart;
    FlushRenderingCommands();
    const FPlatformMemoryStats After = FPlatformMemory::GetStats();
    const FSkySystem Here = LocalSystem::Here(Test.World);
    if (!TestTrue(TEXT("the start system has worlds I-V"), Here.Bodies.IsValidIndex(5)))
    {
        return false;
    }
    const FSkyBody& Fourth = Here.Bodies[4];
    const FSkyBody* Star = Here.Bodies.FindByPredicate([](const FSkyBody& Candidate) { return Candidate.Kind == ESkyBodyKind::Star; });
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    TArray<FString> Report;
    const auto Line = [&](const FString& Text)
    {
        Report.Add(Text);
        AddInfo(Text);
    };

    // -- The system: the sky's own bakes, and their memory as the RHI counts it
    {
        const FSystemMemory M = Measure(*this, *Test.Sky, Here);
        const double CpuMB = MB(static_cast<double>(Test.Sky->GetShadowUploadCpuBytes()));
        IConsoleVariable* BakeTasks = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.ShadowBakeTasks"));
        IConsoleVariable* UploadKB = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.ShadowUploadKB"));
        Line(FString::Printf(TEXT("system the sky's bakes: %d worlds on %d tasks, %.2f s from the load to the last texture; GPU %.2f MB resident (the levels are %.2f MB), CPU %.2f MB left; process physical %+.1f MB over the world's start; widths %s"),
            M.Maps, BakeTasks ? BakeTasks->GetInt() : 0, SkySeconds, M.GpuMB, M.LevelsMB, CpuMB,
            MB(static_cast<double>(After.UsedPhysical) - static_cast<double>(Before.UsedPhysical)), *FString::Join(M.Widths, TEXT(", "))));
        Line(FString::Printf(TEXT("upload in pieces of %d KB a frame: the slowest frame's landing %.3f ms on the game thread, %.3f ms on the render thread"),
            UploadKB ? UploadKB->GetInt() : 0, 1e3 * Test.Sky->GetSlowestShadowLandSeconds(), 1e3 * Test.Sky->GetSlowestShadowUploadRenderSeconds()));
        Line(Budget(TEXT("system_gpu_mb"), M.GpuMB, SystemGpuBudgetMB));
        // Memory is not a timing: the cap is asserted, as the RHI sizes it.
        TestTrue(FString::Printf(TEXT("the start system's maps fit the per-system cap on the GPU (%.2f MB)"), M.GpuMB),
            M.GpuMB <= MB(static_cast<double>(ShipSky::ShadowSystemCapBytes)));
        Line(Budget(TEXT("system_cpu_mb"), CpuMB, SystemCpuBudgetMB));
        Line(Budget(TEXT("map_land_ms"), 1e3 * Test.Sky->GetSlowestShadowLandSeconds(), MapLandBudgetMs));
        Line(Budget(TEXT("map_upload_rt_ms"), 1e3 * Test.Sky->GetSlowestShadowUploadRenderSeconds(), MapUploadBudgetMs));
    }

    // -- A jump within the system: the serial moves, the sky rebuilds, nothing is baked
    {
        const int32 Started = Test.Sky->GetShadowBakesStarted();
        Test.Sky->SyncTo(Here, LocalSystem::Serial(Test.World) + 1, false);
        for (int32 Tick = 0; Tick < 10; ++Tick)
        {
            Test.Step(1.0f / 60.0f);
        }
        const int32 Rebakes = Test.Sky->GetShadowBakesStarted() - Started;
        Line(FString::Printf(TEXT("jump within the system: %d bakes started"), Rebakes));
        Line(Budget(TEXT("jump_rebakes"), Rebakes, JumpRebakesBudget));
    }

    // -- The cold cut, and a release with shadowed builds in flight --------------
    const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, 4, 0.0, Test.Ship->GetFlightState().GetUniversePosition(),
                                                                 ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(10.0));
    if (!TestTrue(TEXT("goto dusk places over Baemsekai IV"), Dusk.IsSet() && Star))
    {
        return false;
    }
    const FVector Up = (Dusk->Position - Fourth.Position).GetSafeNormal();
    const FVector Sunward = (Star->Position - Fourth.Position).GetSafeNormal();
    const FVector Heading = FVector::CrossProduct(Up, Sunward).GetSafeNormal();
    const auto AtDusk = [&]()
    {
        Test.Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + 150.0),
                             FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
    };
    AtDusk();
    IConsoleVariable* TerrainShadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Terrain.Shadows"));
    IConsoleVariable* BuildTasks = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Terrain.BuildTasks"));
    if (!TestNotNull(TEXT("ds.Terrain.Shadows exists"), TerrainShadows) || !TestNotNull(TEXT("ds.Terrain.BuildTasks exists"), BuildTasks))
    {
        return false;
    }
    const auto ColdCut = [&](int32 On)
    {
        // The switch restarts the ground (Task 4), so the next flush builds the whole cut.
        TerrainShadows->Set(On, ECVF_SetByCode);
        const double Start = FPlatformTime::Seconds();
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        return FPlatformTime::Seconds() - Start;
    };
    const double CutWithout = ColdCut(0);
    const double CutWith = ColdCut(1);
    const int32 Resident = Test.Ground->GetResidentCount();
    Line(FString::Printf(TEXT("cut 1.5m_dusk10 over Baemsekai IV: %d tiles resident on %d tasks; without the shadow %.2f s, with it %.2f s (x%.2f)"),
        Resident, BuildTasks->GetInt(), CutWithout, CutWith, CutWith / FMath::Max(CutWithout, 1e-6)));
    Line(Budget(TEXT("cold_cut_s"), CutWith, ColdCutBudgetSeconds));
    const double TileShadowMB = MB(static_cast<double>(Test.Ground->GetTileShadowBytes()));
    Line(FString::Printf(TEXT("cut vertex shadows: %.2f MB from the arrays, each tile once (%.2f MB by the formula)"),
        TileShadowMB, MB(Resident * TerrainTile::GridVerts * sizeof(float))));
    Line(Budget(TEXT("tile_shadow_mb"), TileShadowMB, TileShadowBudgetMB));
    TestTrue(FString::Printf(TEXT("the cut's vertex shadows are held once, within %.0f MB (%.2f MB)"), TileShadowBudgetMB, TileShadowMB),
        TileShadowMB <= TileShadowBudgetMB);
    {
        // A restart with builds in flight: a sample count the cut was not
        // built at. The release is read as its frame over an ordinary one's
        // median, so the frame's own work is not charged to it.
        FScopedCVar Samples(TEXT("ds.Terrain.ShadowSamples"), 13.0f);
        TArray<double> Ordinary;
        for (int32 Tick = 0; Tick < 11; ++Tick)
        {
            const double Began = FPlatformTime::Seconds();
            Test.Step(1.0f / 60.0f);   // the first restarts under 13 samples; the rest build
            Ordinary.Add(1e3 * (FPlatformTime::Seconds() - Began));
        }
        Ordinary.RemoveAt(0);
        const int32 Building = Test.Ground->GetBuildingCount();
        TerrainShadows->Set(0, ECVF_SetByCode);
        const double Start = FPlatformTime::Seconds();
        Test.Step(1.0f / 60.0f);   // the restart's release, and the frame around it
        const double Ms = 1e3 * (FPlatformTime::Seconds() - Start) - Percentile(Ordinary, 0.5);
        TerrainShadows->Set(1, ECVF_SetByCode);
        Line(FString::Printf(TEXT("release with %d shadowed builds in flight: %.2f ms over an ordinary frame's median (%.2f ms)"),
            Building, Ms, Percentile(Ordinary, 0.5)));
        TestTrue(TEXT("builds were in flight when the ground let go"), Building > 0);
        Line(Budget(TEXT("release_ms"), Ms, ReleaseBudgetMs));
    }
    AtDusk();
    Test.Step(1.0f / 60.0f);
    Test.Ground->FlushBuildsForTest();

    // -- A tile ---------------------------------------------------------------
    const TArray<FTileKey> Drawn = Test.Ground->GetDrawnKeys();
    TArray<FTileKey> Keys;
    for (int32 Index = 0; Index < Drawn.Num() && Keys.Num() < 64; Index += FMath::Max(1, Drawn.Num() / 64))
    {
        Keys.Add(Drawn[Index]);
    }
    const TerrainTile::FTileShadow Ruled = Test.Ground->GetTileShadow();
    const FVector3d Centre = FVector3d(Up);
    const FVector3d Level = (FVector3d(Sunward) - Centre * FVector3d::DotProduct(FVector3d(Sunward), Centre)).GetSafeNormal();
    for (const double Degrees : { 10.0, 3.0, 60.0 })
    {
        TerrainTile::FTileShadow Shadow = Ruled;
        const double Elevation = FMath::DegreesToRadians(Degrees);
        Shadow.Sun.Direction = (Centre * FMath::Sin(Elevation) + Level * FMath::Cos(Elevation)).GetSafeNormal();
        TArray<double> Without;
        TArray<double> With;
        TArray<double> Ratio;
        for (const FTileKey& Key : Keys)
        {
            const double Bare = TerrainTile::Build(*Field, Key).BuildSeconds;
            const double Cast = TerrainTile::Build(*Field, Key, Shadow).BuildSeconds;
            Without.Add(Bare);
            With.Add(Cast);
            Ratio.Add(Cast / FMath::Max(Bare, 1e-9));
        }
        Line(FString::Printf(TEXT("tile sun %.0f: %d keys, without the shadow median %.2f ms p90 %.2f ms, with it median %.2f ms p90 %.2f ms, ratio median x%.2f p90 x%.2f (%d samples)"),
            Degrees, Keys.Num(), 1e3 * Percentile(Without, 0.5), 1e3 * Percentile(Without, 0.9), 1e3 * Percentile(With, 0.5),
            1e3 * Percentile(With, 0.9), Percentile(Ratio, 0.5), Percentile(Ratio, 0.9), Shadow.Samples));
        if (Degrees == 10.0)
        {
            Line(Budget(TEXT("tile_ratio"), Percentile(Ratio, 0.5), TileRatioBudget));
        }
    }

    // -- A world's map: the start system's five, and the corpus's two extremes
    const int32 Columns = ShipSky::ShadowMapWidth();
    const SunShadow::FSunLight FourthLight = SkyProjection::SunLightOf(Here, 4);
    struct FWorld
    {
        FString Name;
        FWorldReliefParams Relief;
        SunShadow::FSunLight Sun;
    };
    TArray<FWorld> Worlds;
    for (int32 Index = 1; Index <= 5; ++Index)
    {
        if (Here.Bodies[Index].Ground == EGround::Solid)
        {
            Worlds.Add({ Here.Bodies[Index].Id.ToString(), Here.Bodies[Index].Relief, SkyProjection::SunLightOf(Here, Index) });
        }
    }
    const auto Extreme = [&](const TCHAR* Name, double RadiusEarth, double PeakKm)
    {
        FWorldReliefParams Relief = Fourth.Relief;
        Relief.RadiusCm = RadiusEarth * EarthRadiusCm;
        Relief.PeakCm = PeakKm * 1.0e5;
        Relief.Cratering = 1.0;
        Worlds.Add({ FString::Printf(TEXT("%s-like (the corpus's, at Cratering 1)"), Name), Relief, FourthLight });
    };
    Extreme(TEXT("Baiti I"), 0.443141, 9.74015);
    Extreme(TEXT("Tishras I"), 2.12699, 2.5735);
    double Slowest = 0.0;
    double Largest = 0.0;
    for (const FWorld& World : Worlds)
    {
        const FReliefGround Ground(World.Relief);
        const double Start = FPlatformTime::Seconds();
        const FSunShadowMap Map = SunShadowMap::Bake(Ground, World.Sun, SunShadow::SteepestSlope(World.Relief), Columns);
        const double Seconds = FPlatformTime::Seconds() - Start;
        Slowest = FMath::Max(Slowest, Seconds);
        Largest = FMath::Max(Largest, MB(Map.Bytes()));
        TestTrue(FString::Printf(TEXT("%s's map baked"), *World.Name), Map.LevelCount() > 0);
        Line(FString::Printf(TEXT("map %s: %d x %d, %d levels, %.2f s on one thread, %.2f MB, psi_lo %.2f deg"),
            *World.Name, Map.Width, Map.Rows, Map.LevelCount(), Seconds, MB(Map.Bytes()), FMath::RadiansToDegrees(Map.PsiLo)));
    }
    Line(Budget(TEXT("world_bake_s"), Slowest, WorldBakeBudgetSeconds));
    Line(Budget(TEXT("world_map_mb"), Largest, WorldMapBudgetMB));

    // -- The flight, with the shadow on ------------------------------------------
    {
        // Fresh ground: far enough off that the ground lets go of everything.
        const FVector Away = (Test.Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
        Test.Ship->PlaceShip(Fourth.Position + Away * (Fourth.Radius + 5.0e8), FRotationMatrix::MakeFromX(-Away).ToQuat());
        Test.Step(1.0f / 60.0f);
        const GroundKeepsUpScenario::FResult R = GroundKeepsUpScenario::Fly(Test);
        TestTrue(TEXT("the flight was flown"), R.bValid);
        Line(FString::Printf(TEXT("flight GroundKeepsUp's with the shadow on: %d frames under the drive floor, %d under 1 km; %d without drawn ground, %d not drawing the body, worst drawn gap %.2f cm (allowed %.2f)"),
            R.UnderFloor, R.Low, R.Missing, R.NotDrawing, R.Worst, R.Tolerance));
        Line(Budget(TEXT("flight_missing"), R.Missing, 0.0));
        Line(Budget(TEXT("flight_worst_cm"), R.Worst, R.Tolerance));
        Line(Budget(TEXT("flight_not_drawing"), R.NotDrawing, 0.0));
    }
    {
        // The handover's wait: from 49 km over ground nothing was built for.
        const FVector Side = FVector::CrossProduct(Up, Sunward).GetSafeNormal();
        const FVector Fresh = (Up * FMath::Cos(0.8) + Side * FMath::Sin(0.8)).GetSafeNormal();
        Test.Ship->PlaceShip(Fourth.Position + Fresh * (Fourth.Radius + 5.0e8), FRotationMatrix::MakeFromX(-Fresh).ToQuat());
        Test.Step(1.0f / 60.0f);
        Test.Ship->PlaceShip(Fourth.Position + Fresh * (Fourth.Radius + 4.9e6), FRotationMatrix::MakeFromX(-Fresh).ToQuat());
        const double Start = FPlatformTime::Seconds();
        double Waited = -1.0;
        while (FPlatformTime::Seconds() - Start < 60.0)
        {
            const double Began = FPlatformTime::Seconds();
            Test.Step(1.0f / 60.0f);
            if (Test.Ground->IsDrawingBody())
            {
                Waited = FPlatformTime::Seconds() - Start;
                break;
            }
            while (FPlatformTime::Seconds() - Began < 1.0 / 60.0)
            {
                FPlatformProcess::Sleep(0.001f);
            }
        }
        Line(FString::Printf(TEXT("coarse the ground drew the body %.2f s after arriving 49 km over fresh ground, shadow on (-1: not in 60 s)"), Waited));
        Line(Budget(TEXT("coarse_s"), Waited < 0.0 ? 60.0 : Waited, CoarseBudgetSeconds));
    }

    // -- The per-system cap: the corpus's worst system, baked by the sky -----
    UUniverseSubsystem* Universe = Test.World->GetSubsystem<UUniverseSubsystem>();
    const TOptional<FStarSystem> Worst = Universe ? Universe->GetSystem(WorstSystem) : TOptional<FStarSystem>();
    FSkySystem Twelve = LocalSystem::Here(Worst);
    const int32 Solid = SolidWorlds(Twelve);
    TestTrue(FString::Printf(TEXT("the corpus's worst system is generated (%s, %d solid worlds)"), *Twelve.SystemId.ToString(), Solid), Worst.IsSet() && Solid >= 10);
    FSkySystem Sixteen = Twelve;
    for (const FSkyBody& Body : Twelve.Bodies)
    {
        if (Sixteen.Bodies.Num() >= Twelve.Bodies.Num() + (WorstSystemWorlds - Solid) || Body.Ground != EGround::Solid || Body.Kind == ESkyBodyKind::Star)
        {
            continue;
        }
        FSkyBody Again = Body;
        Again.Id = FName(*(Body.Id.ToString() + TEXT(" again")));
        Sixteen.Bodies.Add(Again);
    }
    // Landed over engine frames, as play lands them, in a world of their own
    // (this one goes with RunTest): FWorstSystems.
    TSharedRef<FWorstSystems> Systems = MakeShared<FWorstSystems>();
    Systems->Automation = this;
    Systems->Report = Report;
    Systems->Systems = { Twelve, Sixteen };
    Systems->Serial = LocalSystem::Serial(Test.World) + 100;
    ADD_LATENT_AUTOMATION_COMMAND(FLandWorstSystems(Systems));
    return true;
}

#endif
