#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "Surface/TerrainTile.h"
#include "TerrainTileComponent.generated.h"

/**
 * One terrain tile (landing decision 6's fallback, taken because
 * Eyes.TerrainBudget failed PMC): a small primitive whose scene proxy owns the
 * GPU buffers, with full-precision UVs, and is rebuilt when the tile is. It
 * draws on the dynamic path -- the static path cached the batch and drew
 * nothing in UE 5.8 -- which the same gate holds to its budgets. It keeps one
 * FTileBuild when bKeepForTest (AWorldGround sets it on every tile, since the
 * renderer may recreate a proxy on its own), about 30% of PMC's copies (T5's
 * re-measurement: 153.5 MB against 508.4 MB).
 */
UCLASS(ClassGroup = Rendering)
class DEEPSPACE_API UTerrainTileComponent : public UMeshComponent
{
    GENERATED_BODY()

public:
    UTerrainTileComponent();

    using FTileRef = TSharedRef<const FTileBuild, ESPMode::ThreadSafe>;

    /** The tile to draw; rebuilds the render proxy. The shared overload
     *  keeps the caller's tile rather than a copy of it: AWorldGround's
     *  resident cut and its components hold one tile between them. */
    void SetTile(const FTileBuild& Tile);
    void SetTile(const FTileRef& Tile);

    /** Lets the tile go: a pooled component waiting for its next tile holds
     *  none, and draws nothing if the renderer recreates its proxy. */
    void ClearTile();

    /** Only when bKeepForTest: the last tile set. */
    const FTileBuild* GetTileForTest() const { return bKeepForTest && bHasTile ? Kept.Get() : nullptr; }
    bool bKeepForTest = false;

    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual int32 GetNumMaterials() const override { return 1; }

private:
    /** Handed to the next proxy, then dropped. */
    TSharedPtr<const FTileBuild, ESPMode::ThreadSafe> Pending;
    TSharedPtr<const FTileBuild, ESPMode::ThreadSafe> Kept;
    bool bHasTile = false;
    FBox LocalBounds = FBox(ForceInit);
};
