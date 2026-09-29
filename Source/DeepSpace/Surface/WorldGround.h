#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/TerrainTile.h"
#include "Tasks/Task.h"
#include "Universe/UniversePosition.h"
#include "WorldGround.generated.h"

class UMaterialInstanceDynamic;
class UTerrainGroundComponent;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;
struct FSkySystem;

/**
 * The ground under the ship (landing decisions 6 and 7): the nearest solid
 * world's cube-sphere quadtree, cut by CDLOD from the ship's origin, built on
 * worker threads (UE::Tasks, at most ds.Terrain.BuildTasks at once -- the
 * machine's cap, never the core count), uploaded as tiles at most
 * ds.Terrain.UploadsPerFrame a frame, and drawn as ONE primitive
 * attached to the counter-frame (UTerrainGroundComponent): the component
 * at minus the ship's position from the world's centre, each tile at its
 * pivot in the component's space, composed in doubles on the render thread,
 * scale 1, turned by the counter-frame's rotation, its vertices local. So a
 * frame's motion is one transform, not one per tile.
 *
 * It takes the body from AShipSky below 50 km once the coarse cut is
 * resident (and gives it back over 55 km), and always under the drive floor,
 * where the proxy is never drawn; the relief grows in by one morph fraction
 * between the handover and the drive floor. Residency never gates the
 * ship's motion. Polls the ship and the universe every frame and stores
 * nothing about either beyond its own drawing (the sky's rule).
 *
 * Placed by Tools/build_hauler.py as hauler_ground, tagged Sky.Ground, with
 * its material assigned there (ADR 0002).
 */
UCLASS()
class DEEPSPACE_API AWorldGround : public AActor
{
    GENERATED_BODY()

public:
    AWorldGround();

    /** Sky.Ground: how the level check finds it. */
    static const FName GroundTag;

    /** M_SkyGround, assigned by build_hauler.py. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
    TObjectPtr<UMaterialInterface> GroundMaterial;

    /** The frame's work: which world, the cut, builds, uploads, what to draw,
     *  the handover and the morph, and every drawn tile placed. */
    void SyncToShip();
    void SyncTo(const FSkySystem& System, bool bInTransit);

    /** Tests: build everything the cut wants, round after round, uploading
     *  without the per-frame cap, until the cut is resident. */
    void FlushBuildsForTest();

    /** The body the ground draws instead of the sky's proxy; none when it
     *  does not. */
    FName GetDrawnBody() const { return bDrawsBody ? Body : NAME_None; }
    bool IsDrawingBody() const { return bDrawsBody; }

    /** M, 0 at the handover to 1 at the drive floor. */
    double GetMorph() const { return Morph; }

    TArray<FTileKey> GetDrawnKeys() const { return Drawn; }
    /** The one primitive every tile is drawn with; null before BeginPlay. */
    UTerrainGroundComponent* GetTilesComponent() const { return Tiles; }
    const FTileBuild* GetResidentTile(const FTileKey& Key) const;
    int32 GetUploadsLastFrame() const { return UploadsLastFrame; }
    int32 GetResidentCount() const { return Resident.Num(); }
    /** The child ranges the cut remembers (BoundsOf), and the keys it needs. */
    int32 GetKnownBoundsCount() const { return KnownBounds.Num(); }
    int32 GetNeededCount() const { return NeededKeys().Num(); }
    /** The remembered ranges with no needed ancestor within
     *  ForgetAfterLevels: 0 after every tick. */
    int32 GetKnownBoundsFarFromCut() const;
    /** How many levels over a remembered range one of its ancestors must be
     *  needed for it to be kept. */
    static constexpr int32 ForgetAfterLevels = 3;

    /** The drawn (morphed) ground's height over the datum at the ship's
     *  nadir, cm; unset if no drawn tile holds it. */
    TOptional<double> DrawnHeightUnderShip() const;

    /** The one material instance every tile shares: AShipSky writes the
     *  body's look into it, this actor writes Morph. */
    UMaterialInstanceDynamic* GetGroundMaterialInstance() const { return Material; }

    /** The light, steepest slope and samples every tile of this ground is
     *  built under: the sky's own light for the world (SkyProjection::
     *  SunLightOf), unset while ds.Terrain.Shadows is 0. */
    const TerrainTile::FTileShadow& GetTileShadow() const { return TileShadow; }

    /** Builds in flight, and builds let go by a restart that have not yet
     *  finished: together never more than ds.Terrain.BuildTasks. */
    int32 GetBuildingCount() const { return InFlight.Num(); }
    int32 GetDrainingCount() const { return Draining.Num(); }

    /** The bytes the resident cut's vertex shadows hold on the CPU, read
     *  from the arrays: each tile once, whether the resident cut or the
     *  tiles' component (for proxy recreation) holds it, or both. */
    int64 GetTileShadowBytes() const;

    /** The cut per level -- drawn, resident, building -- the cap, the morph. */
    FString Describe() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere, Category = "Ground")
    TObjectPtr<USceneComponent> Root;

    /** Made in BeginPlay, never saved into the level. */
    UPROPERTY(Transient)
    TObjectPtr<UTerrainGroundComponent> Tiles;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> Material;

    /** One tile, held by the resident cut and by the component drawing it:
     *  one copy between them, not two. */
    using FTileRef = TSharedRef<const FTileBuild, ESPMode::ThreadSafe>;
    struct FResident
    {
        FTileRef Tile;
    };
    TMap<FTileKey, FResident> Resident;
    /** Each child range the cut has read from a resident parent, kept while
     *  the cut is within two levels of it: see BoundsOf and ForgetBoundsFarFrom. */
    mutable TMap<FTileKey, TerrainQuadtree::FHeightRange> KnownBounds;

    struct FPending
    {
        FTileKey Key;
        UE::Tasks::TTask<FTileBuild> Task;
    };
    TArray<FPending> InFlight;
    TArray<FTileBuild> Finished;

    TArray<FTileKey> Wanted;
    TArray<FTileKey> Prefetch;
    TArray<FTileKey> Drawn;
    /** What the component was last told to show. */
    TArray<FTileKey> Shown;

    FGroundFieldRef Ground;
    FWorldReliefParams GroundParams;

    TerrainTile::FTileShadow TileShadow;

    /** Builds let go by Release or EndPlay: cancelled, never waited on, and
     *  counted against ds.Terrain.BuildTasks until they finish. Their
     *  lambdas hold only the field, the key, the light and the flag -- never
     *  this -- so the actor may go before they do. */
    TArray<UE::Tasks::TTask<FTileBuild>> Draining;
    TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> BuildCancel;

    /** Cancel and let go of every build in flight. */
    void Detach();
    FName Body;
    FUniversePosition Centre;
    double Radius = 0.0;
    double DriveFloorAltitude = 0.0;
    FVector3d ShipFromCentre = FVector3d::ZeroVector;
    FVector3d LastCutFrom = FVector3d::ZeroVector;
    double LastCutTime = -1.0;
    bool bResidencyChanged = true;
    /** Select made a new cut since the last Collect. */
    bool bCutChanged = true;
    bool bCapBinding = false;
    double Morph = 0.0;
    bool bDrawsBody = false;
    int32 UploadsLastFrame = 0;

    void Release();
    void Select(double GroundAltitudeCm);
    void Launch();
    void Collect(int32 Budget);
    void ForgetBoundsFarFrom(const TSet<FTileKey>& Needed);
    static bool IsNearCut(const FTileKey& Key, const TSet<FTileKey>& Needed);
    void Upload(FTileBuild&& Tile);
    void Resolve();
    void Place();
    bool CoarseResident() const;
    TSet<FTileKey> NeededKeys() const;
    TOptional<TerrainQuadtree::FHeightRange> BoundsOf(const FTileKey& Key) const;
};
