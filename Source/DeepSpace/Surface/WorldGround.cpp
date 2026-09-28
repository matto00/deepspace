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
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkySystem.h"
#include "Surface/TerrainTileComponent.h"

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
        TEXT("ds.Terrain.BuildTasks"), 2,
        TEXT("Terrain tile builds in flight at once, on worker threads: the machine's cap, never sized to the core count."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarUploadsPerFrame(
        TEXT("ds.Terrain.UploadsPerFrame"), 4,
        TEXT("Finished terrain tiles uploaded to the GPU a frame."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShow(
        TEXT("ds.Terrain.Show"), 1,
        TEXT("0 hides the ground and gives the world back to the sky's proxy, for comparison."),
        ECVF_Default);

    /** The handover's hysteresis: taken under 50 km, given back over 55. */
    constexpr double HandbackFactor = 1.1;

    bool SameRelief(const FWorldReliefParams& A, const FWorldReliefParams& B)
    {
        return A.SeedOffset == B.SeedOffset && A.RadiusCm == B.RadiusCm && A.PeakCm == B.PeakCm
            && A.Cratering == B.Cratering && A.Ground == B.Ground;
    }

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
    // The sky writes the body's look into this ground's material and asks
    // whether it has the body: this frame's answer, so the ground goes first.
    for (TActorIterator<AShipSky> Sky(GetWorld()); Sky; ++Sky)
    {
        Sky->AddTickPrerequisiteActor(this);
    }
}

void AWorldGround::EndPlay(const EEndPlayReason::Type Reason)
{
    for (FPending& Pending : InFlight)
    {
        Pending.Task.Wait();
    }
    InFlight.Reset();
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
    double NearAltitude = TNumericLimits<double>::Max();
    for (const FSkyBody& Candidate : System.Bodies)
    {
        if (Candidate.Ground == EGround::Solid)
        {
            const double Altitude = ShipAt.DistanceTo(Candidate.Position) - Candidate.Radius;
            if (Altitude < NearAltitude)
            {
                NearAltitude = Altitude;
                Near = &Candidate;
            }
        }
    }
    if (!Near || NearAltitude > TerrainQuadtree::PrefetchAltitudeCm)
    {
        Release();
        return;
    }
    // A new world, or this one reloaded with new priors: start again.
    if (Near->Id != Body || !Ground.IsValid() || !SameRelief(Near->Relief, GroundParams))
    {
        Release();
        Body = Near->Id;
        GroundParams = Near->Relief;
        Ground = ShipGround::FromRelief(Near->Relief);
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
    for (FPending& Pending : InFlight)
    {
        Pending.Task.Wait();
    }
    InFlight.Reset();
    Finished.Reset();
    for (const TPair<FTileKey, FResident>& Pair : Resident)
    {
        Free(Pair.Value.Component);
    }
    Resident.Reset();
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
    if (const FResident* Parent = Resident.Find(Key.Parent()))
    {
        return TerrainTile::ChildRange(Parent->Tile, Key.QuadrantInParent(), *Ground);
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
    const int32 Slots = FMath::Max(1, CVarBuildTasks.GetValueOnGameThread()) - InFlight.Num();
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
    for (const FTileKey& Key : Prefetch)
    {
        Consider(Key);
    }
    for (const FTileKey& Key : Wanted)
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
        InFlight.Add(FPending{ Key, UE::Tasks::Launch(UE_SOURCE_LOCATION,
            [Field, Key]() { return TerrainTile::Build(*Field, Key); }, UE::Tasks::ETaskPriority::BackgroundNormal) });
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
        Upload(Finished[Index]);
    }
    Finished.RemoveAt(0, Uploads);

    // What nothing needs goes back to the pool.
    for (auto It = Resident.CreateIterator(); It; ++It)
    {
        if (!Needed.Contains(It.Key()))
        {
            Free(It.Value().Component);
            It.RemoveCurrent();
            bResidencyChanged = true;
        }
    }
}

void AWorldGround::Upload(const FTileBuild& Tile)
{
    int32 Index = INDEX_NONE;
    bool bFirst = false;
    if (FreeComponents.Num() > 0)
    {
        Index = FreeComponents.Pop();
    }
    else
    {
        NewTileComponent();
        Index = Pool.Num() - 1;
        bFirst = true;
    }
    UPrimitiveComponent* Component = Pool[Index];
    UploadTo(Component, Tile, bFirst);
    // The band limit and the pivot, per tile, as custom primitive data: one
    // material instance serves every tile (decision 6).
    Component->SetCustomPrimitiveDataFloat(SkyMaterial::BandLimitPrimitiveIndex, static_cast<float>(Tile.SpacingCm / Radius));
    Component->SetCustomPrimitiveDataVector3(SkyMaterial::TilePivotPrimitiveIndex, FVector(Tile.Pivot));
    Resident.Add(Tile.Key, FResident{ Tile, Index });
    ++UploadsLastFrame;
    bResidencyChanged = true;
}

void AWorldGround::Free(int32 Component)
{
    if (Pool.IsValidIndex(Component) && Pool[Component])
    {
        Pool[Component]->SetVisibility(false);
        FreeComponents.Push(Component);
    }
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
    for (const FTileKey& Key : Prefetch)
    {
        if (!Resident.Contains(Key))
        {
            return false;
        }
    }
    for (const FTileKey& Leaf : Wanted)
    {
        FTileKey Coarse = Leaf;
        while (Coarse.Level > TerrainQuadtree::PrefetchLevel)
        {
            Coarse = Coarse.Parent();
        }
        if (!Resident.Contains(Coarse))
        {
            return false;
        }
    }
    return !Wanted.IsEmpty();
}

void AWorldGround::Place()
{
    const TSet<FTileKey> Shown(Drawn);
    for (const TPair<FTileKey, FResident>& Pair : Resident)
    {
        UPrimitiveComponent* Component = Pool[Pair.Value.Component];
        const bool bVisible = bDrawsBody && Shown.Contains(Pair.Key);
        if (bVisible)
        {
            // The pivot relative to the ship, subtracted in doubles, in
            // universe axes: the counter-frame's rotation turns it.
            Component->SetRelativeLocation(FVector(Pair.Value.Tile.Pivot - ShipFromCentre));
        }
        if (Component->IsVisible() != bVisible)
        {
            Component->SetVisibility(bVisible);
        }
    }
}

UPrimitiveComponent* AWorldGround::GetTileComponent(const FTileKey& Key) const
{
    const FResident* Found = Resident.Find(Key);
    return Found && Pool.IsValidIndex(Found->Component) ? Pool[Found->Component].Get() : nullptr;
}

const FTileBuild* AWorldGround::GetResidentTile(const FTileKey& Key) const
{
    const FResident* Found = Resident.Find(Key);
    return Found ? &Found->Tile : nullptr;
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
                if (const TOptional<double> Height = TerrainTile::SampleHeight(Found->Tile, Nadir))
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
    for (const TPair<int32, FIntVector>& Level : PerLevel)
    {
        Out += FString::Printf(TEXT("  level %2d: %4d drawn, %4d resident, %2d building\n"), Level.Key, Level.Value.X, Level.Value.Y, Level.Value.Z);
    }
    return Out;
}

UPrimitiveComponent* AWorldGround::NewTileComponent()
{
    UTerrainTileComponent* Tile = NewObject<UTerrainTileComponent>(this);
    Tile->bKeepForTest = true;
    Tile->SetupAttachment(Root);
    Tile->SetVisibility(false);
    Tile->RegisterComponent();
    Tile->SetMaterial(0, Material);
    Pool.Add(Tile);
    return Tile;
}

void AWorldGround::UploadTo(UPrimitiveComponent* Component, const FTileBuild& Tile, bool bFirst)
{
    CastChecked<UTerrainTileComponent>(Component)->SetTile(Tile);
}
