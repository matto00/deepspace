#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/TerrainTile.h"
#include "Tasks/Task.h"
#include "Universe/UniversePosition.h"
#include "WorldGround.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;
struct FSkySystem;

/**
 * The ground under the ship (landing decisions 6 and 7): the nearest solid
 * world's cube-sphere quadtree, cut by CDLOD from the ship's origin, built on
 * worker threads (UE::Tasks, at most ds.Terrain.BuildTasks at once -- the
 * machine's cap, never the core count), uploaded into pooled mesh tiles at
 * most ds.Terrain.UploadsPerFrame a frame, and drawn attached to the
 * counter-frame: each tile at its double-precision pivot relative to the
 * ship, scale 1, turned by the counter-frame's rotation, its vertices local.
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
    UPrimitiveComponent* GetTileComponent(const FTileKey& Key) const;
    const FTileBuild* GetResidentTile(const FTileKey& Key) const;
    int32 GetUploadsLastFrame() const { return UploadsLastFrame; }
    int32 GetResidentCount() const { return Resident.Num(); }

    /** The drawn (morphed) ground's height over the datum at the ship's
     *  nadir, cm; unset if no drawn tile holds it. */
    TOptional<double> DrawnHeightUnderShip() const;

    /** The one material instance every tile shares: AShipSky writes the
     *  body's look into it, this actor writes Morph. */
    UMaterialInstanceDynamic* GetGroundMaterialInstance() const { return Material; }

    /** The cut per level -- drawn, resident, building -- the cap, the morph. */
    FString Describe() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere, Category = "Ground")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UPrimitiveComponent>> Pool;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> Material;

    struct FResident
    {
        FTileBuild Tile;
        int32 Component = INDEX_NONE;
    };
    TMap<FTileKey, FResident> Resident;

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
    TArray<int32> FreeComponents;

    FGroundFieldRef Ground;
    FWorldReliefParams GroundParams;
    FName Body;
    FUniversePosition Centre;
    double Radius = 0.0;
    double DriveFloorAltitude = 0.0;
    FVector3d ShipFromCentre = FVector3d::ZeroVector;
    FVector3d LastCutFrom = FVector3d::ZeroVector;
    double LastCutTime = -1.0;
    bool bResidencyChanged = true;
    bool bCapBinding = false;
    double Morph = 0.0;
    bool bDrawsBody = false;
    int32 UploadsLastFrame = 0;

    void Release();
    void Select(double GroundAltitudeCm);
    void Launch();
    void Collect(int32 Budget);
    void Upload(const FTileBuild& Tile);
    void Free(int32 Component);
    void Resolve();
    void Place();
    bool CoarseResident() const;
    TSet<FTileKey> NeededKeys() const;
    TOptional<TerrainQuadtree::FHeightRange> BoundsOf(const FTileKey& Key) const;

    /** The mesh component a tile is drawn with, and how a tile gets into it:
     *  UTerrainTileComponent, a custom primitive drawn on the dynamic path
     *  (the static path cached its batch and drew nothing in UE 5.8), since
     *  the first-day gate failed ProceduralMeshComponent (Eyes.TerrainBudget;
     *  these two are all the swap touched). */
    UPrimitiveComponent* NewTileComponent();
    void UploadTo(UPrimitiveComponent* Component, const FTileBuild& Tile, bool bFirst);
};
