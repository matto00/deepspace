#include "Surface/WorldGround.h"

#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/OutputDevice.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkySystem.h"
#include "Surface/SunShadow.h"
#include "Surface/TerrainGroundComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldGround, Log, All);

const FName AWorldGround::GroundTag(TEXT("Sky.Ground"));

namespace
{
    TAutoConsoleVariable<float> CVarSplitFactor(
        TEXT("ds.Terrain.SplitFactor"), static_cast<float>(TerrainQuadtree::DefaultSplitFactor),
        TEXT("A terrain node splits when the ship is closer than this many of its edges."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarMaxTiles(
        TEXT("ds.Terrain.MaxTiles"), TerrainQuadtree::DefaultMaxTiles,
        TEXT("A ceiling on drawn terrain tiles: over it the farthest levels coarsen first, never the ship's own chain."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarBuildTasks(
        TEXT("ds.Terrain.BuildTasks"), 3,
        TEXT("Terrain tile builds in flight at once, on worker threads: 3 (ruled 2026-09-28: shadowed tiles at dusk cost up to 9.5x to build), never sized to the core count."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarUploadsPerFrame(
        TEXT("ds.Terrain.UploadsPerFrame"), 4,
        TEXT("Finished terrain tiles uploaded to the GPU a frame."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShow(
        TEXT("ds.Terrain.Show"), 1,
        TEXT("0 hides the ground and gives the world back to the sky's proxy, for comparison."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadows(
        TEXT("ds.Terrain.Shadows"), 1,
        TEXT("1: every tile's vertices carry the cast shadow, computed as the tile is built, off the game thread; 0 builds them without it. Changing it rebuilds the ground."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowSamples(
        TEXT("ds.Terrain.ShadowSamples"), SunShadow::DefaultSamples,
        TEXT("How many samples each vertex's march toward the sun takes (2-64). Changing it rebuilds the ground."),
        ECVF_Default);

    /** What this frame's tiles are built under: the sky's light for the
     *  world, its ground's steepest slope, the samples; nothing when off. */
    TerrainTile::FTileShadow ShadowFor(const FSkySystem& System, int32 Index)
    {
        TerrainTile::FTileShadow Shadow;
        if (CVarShadows.GetValueOnGameThread() != 0)
        {
            Shadow.Sun = SkyProjection::SunLightOf(System, Index);
            Shadow.SteepestSlope = SunShadow::SteepestSlope(System.Bodies[Index].Relief);
            Shadow.Samples = FMath::Clamp(CVarShadowSamples.GetValueOnGameThread(), 2, 64);
        }
        return Shadow;
    }

    bool SameShadow(const TerrainTile::FTileShadow& A, const TerrainTile::FTileShadow& B)
    {
        return A.Sun.Direction == B.Sun.Direction && A.Sun.AngularRadius == B.Sun.AngularRadius
            && A.SteepestSlope == B.SteepestSlope && A.Samples == B.Samples;
    }

    /** The handover's hysteresis: taken under 50 km, given back over 55. */
    constexpr double HandbackFactor = 1.1;

    void DescribeGround(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        int32 Count = 0;
        for (TActorIterator<AWorldGround> It(World); It; ++It, ++Count)
        {
            Out.Log(It->Describe());
        }
        if (Count == 0)
        {
            Out.Log(TEXT("ds.Terrain.Describe: no ground in this world."));
        }
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice DescribeCommand(
        TEXT("ds.Terrain.Describe"),
        TEXT("The terrain's cut per level: drawn, resident and building tiles, whether the tile cap binds, and the morph."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DescribeGround));
}

AWorldGround::AWorldGround()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    SetRootComponent(Root);
    Tags.AddUnique(GroundTag);
}

void AWorldGround::BeginPlay()
{
    Super::BeginPlay();
    TActorIterator<AShipCounterFrame> Frame(GetWorld());
    if (Frame)
    {
        AttachToActor(*Frame, FAttachmentTransformRules::KeepRelativeTransform);
        SetActorRelativeTransform(FTransform::Identity);
    }
    else
    {
        UE_LOG(LogWorldGround, Warning, TEXT("No AShipCounterFrame in the level; the ground will not turn with the ship."));
    }
    Material = UMaterialInstanceDynamic::Create(GroundMaterial ? GroundMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface), this);
    // Every tile, one primitive: made here, never saved into the level.
    Tiles = NewObject<UTerrainGroundComponent>(this, TEXT("Tiles"));
    Tiles->SetupAttachment(Root);
    Tiles->RegisterComponent();
    Tiles->SetMaterial(0, Material);
    // The sky writes the body's look into this ground's material and asks
    // whether it has the body: this frame's answer, so the ground goes first.
    for (TActorIterator<AShipSky> Sky(GetWorld()); Sky; ++Sky)
    {
        Sky->AddTickPrerequisiteActor(this);
    }
}

void AWorldGround::EndPlay(const EEndPlayReason::Type Reason)
{
    // Let go, never waited on: the builds hold no this, and stop within a row.
    Detach();
    Draining.Reset();
    Super::EndPlay(Reason);
}

void AWorldGround::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    SyncToShip();
}

void AWorldGround::SyncToShip()
{
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return;
    }
    SyncTo(Ship->IsInTransit() ? FSkySystem() : LocalSystem::Here(GetWorld()), Ship->IsInTransit());
}

void AWorldGround::SyncTo(const FSkySystem& System, bool bInTransit)
{
    UploadsLastFrame = 0;
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship || bInTransit)
    {
        Release();
        return;
    }
    const FUniversePosition ShipAt = Ship->GetFlightState().GetUniversePosition();

    // The nearest solid world within the prefetch range: the ground there.
    const FSkyBody* Near = nullptr;
    int32 NearIndex = INDEX_NONE;
    double NearAltitude = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
    {
        const FSkyBody& Candidate = System.Bodies[Index];
        if (Candidate.Ground == EGround::Solid)
        {
            const double Altitude = ShipAt.DistanceTo(Candidate.Position) - Candidate.Radius;
            if (Altitude < NearAltitude)
            {
                NearAltitude = Altitude;
                Near = &Candidate;
                NearIndex = Index;
            }
        }
    }
    if (!Near || NearAltitude > TerrainQuadtree::PrefetchAltitudeCm)
    {
        Release();
        return;
    }
    // A new world, this one reloaded with new priors, or its tiles' shadow
    // switched, resampled or lit otherwise: start again.
    const TerrainTile::FTileShadow Shadow = ShadowFor(System, NearIndex);
    if (Near->Id != Body || !Ground.IsValid() || !SameRelief(Near->Relief, GroundParams) || !SameShadow(Shadow, TileShadow))
    {
        Release();
        Body = Near->Id;
        GroundParams = Near->Relief;
        Ground = ShipGround::FromRelief(Near->Relief);
        TileShadow = Shadow;
    }
    Centre = Near->Position;
    Radius = Near->Radius;
    DriveFloorAltitude = UShipSubsystem::FloorFor(*Near);
    ShipFromCentre = FVector3d(ShipAt - Centre);
    const double Agl = ShipFromCentre.Size() - Radius - Ground->Height(ShipFromCentre.GetSafeNormal(), 0.0);

    Select(Agl);
    Collect(FMath::Max(0, CVarUploadsPerFrame.GetValueOnGameThread()));
    Launch();
    Resolve();

    // The handover (decision 7): always under the drive floor, where the
    // proxy is never drawn; above it, under 50 km once the coarse cut is
    // resident, and given back over 55 km.
    if (CVarShow.GetValueOnGameThread() == 0)
    {
        bDrawsBody = false;
    }
    else if (NearAltitude < DriveFloorAltitude)
    {
        bDrawsBody = true;
    }
    else if (!bDrawsBody && NearAltitude < TerrainTile::HandoverAltitudeCm && CoarseResident())
    {
        bDrawsBody = true;
    }
    else if (bDrawsBody && NearAltitude > TerrainTile::HandoverAltitudeCm * HandbackFactor)
    {
        bDrawsBody = false;
    }
    Morph = TerrainTile::MorphFraction(NearAltitude, DriveFloorAltitude);
    if (Material)
    {
        Material->SetScalarParameterValue(SkyMaterial::Morph, static_cast<float>(Morph));
    }
    Place();
}

void AWorldGround::Release()
{
    Detach();
    Finished.Reset();
    if (Tiles)
    {
        Tiles->ClearTiles();
    }
    Shown.Reset();
    Resident.Reset();
    KnownBounds.Reset();
    Wanted.Reset();
    Prefetch.Reset();
    Drawn.Reset();
    Ground.Reset();
    Body = NAME_None;
    bDrawsBody = false;
    bResidencyChanged = true;
}

TOptional<TerrainQuadtree::FHeightRange> AWorldGround::BoundsOf(const FTileKey& Key) const
{
    if (Key.Level == 0)
    {
        return TerrainQuadtree::FHeightRange{ Ground->MinHeightCm(), Ground->MaxHeightCm() };
    }
    // Remembered once known: a child's range is its resident parent's, and
    // the cut culls and splits by it. Forgotten when the parent is freed, the
    // cut changed with residency -- a tile at the horizon that culled its
    // own children once resident was then not needed, freed, and needed
    // again, rebuilt frame after frame. The range is the parent tile's, so
    // it cannot change for the ground's life; it is forgotten only once the
    // cut has moved two levels away from it (ForgetBoundsFarFrom), and all
    // of them by Release.
    if (const TerrainQuadtree::FHeightRange* Known = KnownBounds.Find(Key))
    {
        return *Known;
    }
    if (const FResident* Parent = Resident.Find(Key.Parent()))
    {
        return KnownBounds.Add(Key, TerrainTile::ChildRange(*Parent->Tile, Key.QuadrantInParent(), *Ground));
    }
    return {};
}

void AWorldGround::Select(double GroundAltitudeCm)
{
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    const double Moved = (ShipFromCentre - LastCutFrom).Size();
    if (!Wanted.IsEmpty() && !bResidencyChanged && Moved < 0.05 * FMath::Max(GroundAltitudeCm, 100.0) && Now - LastCutTime < 0.25)
    {
        return;
    }
    TerrainQuadtree::FCutParams Params;
    Params.RadiusCm = Radius;
    Params.MaxLevel = TerrainQuadtree::MaxLevel(Radius);
    Params.SplitFactor = FMath::Max(1.0f, CVarSplitFactor.GetValueOnGameThread());
    Params.MaxTiles = FMath::Max(64, CVarMaxTiles.GetValueOnGameThread());
    Params.OccluderRadiusCm = Radius + Ground->MinHeightCm();
    Params.GroundAltitudeCm = GroundAltitudeCm;
    const TerrainQuadtree::FCut Cut = TerrainQuadtree::SelectCut(ShipFromCentre, Params,
        [this](const FTileKey& Key) { return BoundsOf(Key); });
    Wanted = Cut.Leaves;
    Prefetch = Cut.Prefetch;
    bCapBinding = Cut.bCapBinding;
    LastCutFrom = ShipFromCentre;
    LastCutTime = Now;
    bResidencyChanged = false;
    bCutChanged = true;
}

TSet<FTileKey> AWorldGround::NeededKeys() const
{
    TSet<FTileKey> Needed(Prefetch);
    for (const FTileKey& Leaf : Wanted)
    {
        for (FTileKey Key = Leaf;; Key = Key.Parent())
        {
            bool bAlready = false;
            Needed.Add(Key, &bAlready);
            if (bAlready || Key.Level == 0)
            {
                break;
            }
        }
    }
    return Needed;
}

void AWorldGround::Launch()
{
    // What a restart let go still holds a worker until it finishes.
    Draining.RemoveAll([](const UE::Tasks::TTask<FTileBuild>& Task) { return Task.IsCompleted(); });
    const int32 Slots = FMath::Max(1, CVarBuildTasks.GetValueOnGameThread()) - InFlight.Num() - Draining.Num();
    if (Slots <= 0 || !Ground.IsValid())
    {
        return;
    }
    TArray<FTileKey> Candidates;
    const auto Consider = [&](const FTileKey& Key)
    {
        if (!Resident.Contains(Key)
            && !InFlight.ContainsByPredicate([&](const FPending& Pending) { return Pending.Key == Key; })
            && !Finished.ContainsByPredicate([&](const FTileBuild& Built) { return Built.Key == Key; }))
        {
            Candidates.AddUnique(Key);
        }
    };
    // Every key the cut needs: the prefetch, the leaves, and the leaves'
    // ancestors, which Resolve draws while a leaf under them builds. An
    // ancestor that was never a leaf -- Balance splits a leaf the ship has
    // just reached into children, making it interior the frame it enters
    // the cut -- was never built, so the cut stopped at it again next frame
    // and Balance split it again: the chain under the ship stuck at level 12
    // at 30 m, 163 cm off the ground (GroundKeepsUp, on 3 build tasks).
    for (const FTileKey& Key : NeededKeys())
    {
        Consider(Key);
    }
    // Coarse first -- the chain and the cap under the ship -- then nearest.
    const FVector3d Nadir = ShipFromCentre.GetSafeNormal();
    Candidates.Sort([&](const FTileKey& A, const FTileKey& B)
    {
        if (A.Level != B.Level)
        {
            return A.Level < B.Level;
        }
        return FVector3d::DotProduct(TerrainQuadtree::CentreDirection(A), Nadir) > FVector3d::DotProduct(TerrainQuadtree::CentreDirection(B), Nadir);
    });
    for (int32 Index = 0; Index < FMath::Min(Slots, Candidates.Num()); ++Index)
    {
        const FTileKey Key = Candidates[Index];
        const FGroundFieldRef Field = Ground;
        const TerrainTile::FTileShadow Shadow = TileShadow;
        if (!BuildCancel.IsValid())
        {
            BuildCancel = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);
        }
        const TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> Cancel = BuildCancel;
        InFlight.Add(FPending{ Key, UE::Tasks::Launch(UE_SOURCE_LOCATION,
            [Field, Key, Shadow, Cancel]() { return TerrainTile::Build(*Field, Key, Shadow, Cancel.Get()); },
            UE::Tasks::ETaskPriority::BackgroundNormal) });
    }
}

void AWorldGround::Collect(int32 Budget)
{
    for (int32 Index = InFlight.Num() - 1; Index >= 0; --Index)
    {
        if (InFlight[Index].Task.IsCompleted())
        {
            Finished.Add(MoveTemp(InFlight[Index].Task.GetResult()));
            InFlight.RemoveAtSwap(Index);
        }
    }
    const TSet<FTileKey> Needed = NeededKeys();
    Finished.RemoveAll([&](const FTileBuild& Built) { return !Needed.Contains(Built.Key); });
    Finished.Sort([](const FTileBuild& A, const FTileBuild& B) { return A.Key.Level < B.Key.Level; });
    const int32 Uploads = FMath::Min(Budget, Finished.Num());
    for (int32 Index = 0; Index < Uploads; ++Index)
    {
        Upload(MoveTemp(Finished[Index]));
    }
    Finished.RemoveAt(0, Uploads);

    // What nothing needs is let go.
    for (auto It = Resident.CreateIterator(); It; ++It)
    {
        if (!Needed.Contains(It.Key()))
        {
            if (Tiles)
            {
                Tiles->RemoveTile(It.Key());
            }
            It.RemoveCurrent();
            bResidencyChanged = true;
        }
    }
    // And what the cut has moved away from is forgotten, once for each new cut.
    if (bCutChanged)
    {
        ForgetBoundsFarFrom(Needed);
        bCutChanged = false;
    }
}

bool AWorldGround::IsNearCut(const FTileKey& Key, const TSet<FTileKey>& Needed)
{
    FTileKey Up = Key;
    for (int32 Step = 0; Step < ForgetAfterLevels && Up.Level > 0; ++Step)
    {
        Up = Up.Parent();
        if (Needed.Contains(Up))
        {
            return true;
        }
    }
    return Key.Level <= ForgetAfterLevels;
}

int32 AWorldGround::GetKnownBoundsFarFromCut() const
{
    const TSet<FTileKey> Needed = NeededKeys();
    int32 Far = 0;
    for (const TPair<FTileKey, TerrainQuadtree::FHeightRange>& Known : KnownBounds)
    {
        Far += IsNearCut(Known.Key, Needed) ? 0 : 1;
    }
    return Far;
}

void AWorldGround::ForgetBoundsFarFrom(const TSet<FTileKey>& Needed)
{
    // A child's range is kept while one of its three nearest ancestors is
    // needed. BoundsOf keeps them so that the cut does not change with
    // residency -- the horizon tile freed because the ranges it gave let the
    // cut stop above it -- and so they must outlive their parent by more
    // than a level: kept only while the parent or grandparent was needed,
    // the cut changed with residency again, and GroundKeepsUp's skim at 50 m
    // drew ground 790 m off under the ship. Farther from the cut than three
    // levels, nothing reads them until the ship comes back, when a resident
    // parent gives the same range again. So the ranges are bounded by the
    // cut -- at most 84 for each key it needs -- and not by the ground flown
    // over (DeepSpace.Surface.GroundForgetsBounds).
    for (auto It = KnownBounds.CreateIterator(); It; ++It)
    {
        if (!IsNearCut(It.Key(), Needed))
        {
            It.RemoveCurrent();
        }
    }
}

void AWorldGround::Upload(FTileBuild&& Built)
{
    const FTileRef Tile = MakeShared<const FTileBuild, ESPMode::ThreadSafe>(MoveTemp(Built));
    // The band limit and the pivot, per tile, as custom primitive data: one
    // material instance serves every tile (decision 6).
    if (Tiles)
    {
        Tiles->AddTile(Tile, static_cast<float>(Tile->SpacingCm / Radius));
    }
    Resident.Add(Tile->Key, FResident{ Tile });
    ++UploadsLastFrame;
    bResidencyChanged = true;
}

void AWorldGround::Resolve()
{
    Drawn.Reset();
    const TSet<FTileKey> Leaves(Wanted);
    TSet<FTileKey> Interior;
    for (const FTileKey& Leaf : Wanted)
    {
        for (FTileKey Up = Leaf; Up.Level > 0;)
        {
            Up = Up.Parent();
            bool bAlready = false;
            Interior.Add(Up, &bAlready);
            if (bAlready)
            {
                break;
            }
        }
    }
    // A node is drawn only when resident; a parent stays drawn until all four
    // of its children can be -- never a hole, never two levels over one spot.
    TFunction<bool(const FTileKey&, TArray<FTileKey>&)> Draw = [&](const FTileKey& Key, TArray<FTileKey>& Out) -> bool
    {
        if (Leaves.Contains(Key))
        {
            if (Resident.Contains(Key))
            {
                Out.Add(Key);
                return true;
            }
            return false;
        }
        if (!Interior.Contains(Key))
        {
            return true;   // culled: nothing to draw, nothing missing
        }
        TArray<FTileKey> Children;
        bool bAll = true;
        for (int32 Quadrant = 0; Quadrant < 4 && bAll; ++Quadrant)
        {
            bAll &= Draw(Key.Child(Quadrant), Children);
        }
        if (bAll)
        {
            Out.Append(Children);
            return true;
        }
        if (Resident.Contains(Key))
        {
            Out.Add(Key);
            return true;
        }
        return false;
    };
    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        Draw(FTileKey{ static_cast<uint8>(FaceIndex), 0, 0, 0 }, Drawn);
    }
}

bool AWorldGround::CoarseResident() const
{
    return TerrainQuadtree::CoarseResident(Prefetch, Wanted, [this](const FTileKey& Key) { return Resident.Contains(Key); });
}

void AWorldGround::Place()
{
    if (!Tiles)
    {
        return;
    }
    // The frame's one move: every tile sits at its pivot in the component's
    // space, and the component at minus the ship's position from the
    // world's centre, in universe axes, which the counter-frame turns.
    Tiles->SetRelativeLocation(FVector(-ShipFromCentre));
    // And what is drawn, only when it changes.
    static const TArray<FTileKey> None;
    const TArray<FTileKey>& Now = bDrawsBody ? Drawn : None;
    if (Now != Shown)
    {
        Shown = Now;
        Tiles->SetShown(Shown);
    }
}

const FTileBuild* AWorldGround::GetResidentTile(const FTileKey& Key) const
{
    const FResident* Found = Resident.Find(Key);
    return Found ? &Found->Tile.Get() : nullptr;
}

TOptional<double> AWorldGround::DrawnHeightUnderShip() const
{
    const FVector3d Nadir = ShipFromCentre.GetSafeNormal();
    for (const FTileKey& Key : Drawn)
    {
        if (TerrainQuadtree::KeyAt(Nadir, Key.Level) == Key)
        {
            if (const FResident* Found = Resident.Find(Key))
            {
                if (const TOptional<double> Height = TerrainTile::SampleHeight(*Found->Tile, Nadir))
                {
                    return *Height * Morph;
                }
            }
        }
    }
    return {};
}

void AWorldGround::FlushBuildsForTest()
{
    // Tests only: what a restart let go finishes before the cut is timed.
    for (UE::Tasks::TTask<FTileBuild>& Task : Draining)
    {
        Task.Wait();
    }
    Draining.Reset();
    for (int32 Round = 0; Round < 4096; ++Round)
    {
        bResidencyChanged = true;
        SyncToShip();
        if (InFlight.IsEmpty() && Finished.IsEmpty())
        {
            break;   // nothing left to build: the cut is resident
        }
        while (!InFlight.IsEmpty() || !Finished.IsEmpty())
        {
            if (!InFlight.IsEmpty())
            {
                InFlight[0].Task.Wait();
            }
            Collect(TNumericLimits<int32>::Max());
            Launch();
        }
    }
    bResidencyChanged = true;
    SyncToShip();
}

FString AWorldGround::Describe() const
{
    if (!Ground.IsValid())
    {
        return TEXT("ds.Terrain: no solid world within 1,000 km.");
    }
    TMap<int32, FIntVector> PerLevel;   // drawn, resident, building
    for (const FTileKey& Key : Drawn) { ++PerLevel.FindOrAdd(Key.Level).X; }
    for (const TPair<FTileKey, FResident>& Pair : Resident) { ++PerLevel.FindOrAdd(Pair.Key.Level).Y; }
    for (const FPending& Pending : InFlight) { ++PerLevel.FindOrAdd(Pending.Key.Level).Z; }
    PerLevel.KeySort(TLess<int32>());
    FString Out = FString::Printf(TEXT("ds.Terrain: %s, %s the body, morph %.3f, %d drawn, %d resident, %d building%s\n"),
        *Body.ToString(), bDrawsBody ? TEXT("drawing") : TEXT("not drawing"), Morph, Drawn.Num(), Resident.Num(), InFlight.Num(),
        bCapBinding ? TEXT(", the tile cap BINDING") : TEXT(""));
    Out += FString::Printf(TEXT("  cast shadow: %s, sun %.3f deg in radius, %d samples, day exit %.1f deg\n"),
        TileShadow.Sun.IsSet() ? TEXT("on") : TEXT("off"), FMath::RadiansToDegrees(TileShadow.Sun.AngularRadius), TileShadow.Samples,
        FMath::RadiansToDegrees(FMath::Atan(TileShadow.SteepestSlope)));
    for (const TPair<int32, FIntVector>& Level : PerLevel)
    {
        Out += FString::Printf(TEXT("  level %2d: %4d drawn, %4d resident, %2d building\n"), Level.Key, Level.Value.X, Level.Value.Y, Level.Value.Z);
    }
    return Out;
}

void AWorldGround::Detach()
{
    // Before the shadow a build was ~5 ms, and waiting was cheap. With it, a
    // build is 16-70 ms, and Release runs on every fold opened near a world.
    if (BuildCancel.IsValid())
    {
        BuildCancel->store(true, std::memory_order_relaxed);
    }
    BuildCancel.Reset();
    for (FPending& Pending : InFlight)
    {
        Draining.Add(MoveTemp(Pending.Task));
    }
    InFlight.Reset();
}

int64 AWorldGround::GetTileShadowBytes() const
{
    TSet<const FTileBuild*> Counted;
    int64 Bytes = 0;
    const auto Count = [&](const FTileBuild* Tile)
    {
        bool bAlready = false;
        Counted.Add(Tile, &bAlready);
        Bytes += bAlready ? 0 : Tile->SunVisible.GetAllocatedSize();
    };
    for (const TPair<FTileKey, FResident>& Pair : Resident)
    {
        Count(&Pair.Value.Tile.Get());
    }
    if (Tiles)
    {
        for (const FTileBuild* Held : Tiles->GetTiles())
        {
            Count(Held);
        }
    }
    return Bytes;
}
