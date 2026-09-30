#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "Surface/TerrainTile.h"
#include "TerrainGroundComponent.generated.h"

/**
 * Every tile of the ground, drawn as ONE primitive (slice (b)'s last open
 * items: tiles move through one transform). The ship is the origin, so while
 * it moves every tile moves every frame; as a component a tile, that was a
 * transform update per tile -- Eyes.TerrainBudget read +7.7 ms a frame for
 * 2,200 tiles, and moving their one parent instead read +8.2, since the
 * engine still updates every child. Here a frame's motion is this
 * component's one transform: AWorldGround sets it to minus the ship's
 * position from the world's centre, under the counter-frame, and every tile
 * sits at its pivot in the component's space.
 *
 * The scene proxy holds each tile's GPU buffers (full-precision UVs, the
 * tiles' one shared index buffer) and draws on the dynamic path. Each drawn
 * tile is its own mesh element with its own primitive data -- the tile's
 * pivot composed with the component's transform in doubles on the render
 * thread, and the tile's band limit and pivot as custom primitive data -- so
 * M_SkyGround reads LocalPosition and TilePivot exactly as it did per tile.
 * The float budget is unchanged: a vertex is a float offset from its own
 * tile's pivot (sub-millimetre on a 19 m tile), and the pivot's place
 * relative to the ship is subtracted in doubles and only then handed to the
 * renderer, which keeps it relative to the camera.
 *
 * Tiles are added, removed and shown by render commands to the live proxy,
 * never by rebuilding it; a rebuilt proxy (a material change, a
 * re-registration) is built from the tiles this component holds. It holds
 * each tile by the same shared reference as AWorldGround's resident cut:
 * one copy between them.
 */
UCLASS(ClassGroup = Rendering)
class DEEPSPACE_API UTerrainGroundComponent : public UMeshComponent
{
    GENERATED_BODY()

public:
    UTerrainGroundComponent();

    using FTileRef = TSharedRef<const FTileBuild, ESPMode::ThreadSafe>;

    /** Adds Tile, hidden, or replaces the tile of the same key. BandLimit is
     *  the material's custom primitive datum for it (SpacingCm / radius). */
    void AddTile(const FTileRef& Tile, float BandLimit);

    /** Lets a tile go. */
    void RemoveTile(const FTileKey& Key);

    /** Every tile let go. */
    void ClearTiles();

    /** Exactly these tiles drawn, every other held hidden. A call with the
     *  set already shown sends nothing. */
    void SetShown(const TArray<FTileKey>& Keys);

    bool HasTile(const FTileKey& Key) const { return Tiles.Contains(Key); }
    bool IsTileShown(const FTileKey& Key) const;
    int32 GetTileCount() const { return Tiles.Num(); }
    int32 GetShownCount() const { return ShownCount; }
    const FTileBuild* GetTile(const FTileKey& Key) const;
    TArray<const FTileBuild*> GetTiles() const;

    /** Where a tile's pivot is drawn, world space: its pivot through this
     *  component's transform, in doubles. */
    FVector GetTileWorldLocation(const FTileKey& Key) const;

    /** A tile's box in this component's space: its vertices round its pivot,
     *  grown by the morph's reach (its largest height). */
    static FBox LocalBoxOf(const FTileBuild& Tile);

    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
    virtual int32 GetNumMaterials() const override { return 1; }

private:
    struct FEntry
    {
        FTileRef Tile;
        float BandLimit = 0.0f;
        FBox Box = FBox(ForceInit);
        bool bShown = false;
    };
    TMap<FTileKey, FEntry> Tiles;
    int32 ShownCount = 0;
    FBox ShownBox = FBox(ForceInit);

    void RecomputeShownBox();
};
