#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Surface/GroundField.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/SunShadow.h"

/** One built tile (landing decision 6): what the mesh gets and what the
 *  LOD, the tests and the drawn-height check read. Built off the game
 *  thread; plain data. */
struct DEEPSPACE_API FTileBuild
{
    FTileKey Key;

    /** The ground under the node's centre, from the world's centre, cm,
     *  universe axes, in doubles: the tile's component is placed here every
     *  frame and its vertices are local to it. The ground's point rather than
     *  the datum's, so a tile's local coordinates span its own few metres of
     *  relief and not the kilometres below it: that is what keeps a 17 m tile
     *  sub-millimetre in float. */
    FVector3d Pivot = FVector3d::ZeroVector;

    double RadiusCm = 0.0;

    /** Vertex spacing, cm: the tile's band limit, the footprint its heights
     *  were evaluated at. */
    double SpacingCm = 0.0;

    /** Pivot-relative positions, cm, the grid then the skirt: scale 1, so a
     *  19 m tile keeps sub-millimetre precision in float. */
    TArray<FVector3f> Positions;

    /** Unit normals, universe axes: the band-limited gradient composed as
     *  the orbit composes it, normalize(D - Grad_t / R). */
    TArray<FVector3f> Normals;

    /** The grid's heights above the datum, cm, and unit directions: unrounded,
     *  for the morph, the LOD boxes and the tests. */
    TArray<double> Heights;
    TArray<FVector3d> Directions;

    TerrainQuadtree::FHeightRange Range;

    /** The same over each child's quarter (quadrants as FTileKey::Child). */
    TerrainQuadtree::FHeightRange QuadrantRange[4];

    /** The largest miss of an edge's straight segment against the band-limited
     *  ground at its midpoint, cm. */
    double MaxEdgeErrorCm = 0.0;

    /** How far the skirt hangs: 1.5 x (edge error + omitted bands) + 1 cm. */
    double SkirtDepthCm = 0.0;

    /** Each grid vertex's share of its star's disc seen past the ground --
     *  the cast shadow, SunShadow::Visible at the vertex's own height and the
     *  tile's spacing (the developer's ruling on slice (b)'s build: baked,
     *  not marched) -- 1 everywhere for a tile built without a light. The
     *  skirt's vertices carry their grid vertex's (UV0Of). */
    TArray<float> SunVisible;

    /** How much of BuildSeconds the shadow took. */
    double ShadowSeconds = 0.0;

    double BuildSeconds = 0.0;
};

namespace TerrainTile
{
    inline constexpr int32 Cells = TerrainQuadtree::CellsPerTile;
    inline constexpr int32 GridVerts = (Cells + 1) * (Cells + 1);
    inline constexpr int32 SkirtVerts = 4 * (Cells + 1);
    inline constexpr int32 VertexCount = GridVerts + SkirtVerts;
    inline constexpr int32 TriangleCount = 2 * Cells * Cells + 4 * 2 * Cells;

    /** The orbit-to-ground handover, cm over the datum (decision 7): the
     *  projection's magnification is exactly 1 there. */
    inline constexpr double HandoverAltitudeCm = 5.0e6;

    /** UV2.y carries the height in kilometres. The scale was chosen for
     *  ProceduralMeshComponent, whose half-precision UVs cannot hold
     *  centimetres to 10 km; UTerrainTileComponent, which draws the tiles,
     *  keeps full-precision UVs (SetUseFullPrecisionUVs), where a float of
     *  kilometres holds 10 km to under a millimetre. */
    inline constexpr double HeightUVScaleCm = 1.0e5;

    /** The fixed topology every tile shares: grid triangles (a, c, b) and
     *  (b, c, d) per cell -- inward, as the engine's meshes are wound -- then
     *  the skirt, each edge traversed with the tile's outside on its right. */
    DEEPSPACE_API const TArray<int32>& Indices();

    /** The light a tile's shadow is cast by, the day exit's slope and the
     *  march's samples: AWorldGround's, from SkyProjection::SunLightOf and
     *  SunShadow::SteepestSlope. An unset light casts nothing. */
    struct FTileShadow
    {
        SunShadow::FSunLight Sun;
        double SteepestSlope = 0.0;
        int32 Samples = SunShadow::DefaultSamples;
    };

    /** The tile for Key: heights at the tile's own spacing as the footprint,
     *  so a coarse tile never samples fine bands into vertex noise; normals
     *  from the analytic gradient of the same band-limited height; and each
     *  grid vertex's cast shadow under Shadow's light. Pure and thread-safe
     *  given a thread-safe Ground. Cancel, if given, is polled once a grid
     *  row of the shadow; set, the build returns at once with the shadow
     *  unfinished -- a tile only a let-go build makes, whose result nobody
     *  reads (AWorldGround::Detach). */
    DEEPSPACE_API FTileBuild Build(const IGroundField& Ground, const FTileKey& Key, const FTileShadow& Shadow = FTileShadow(),
                                   const std::atomic<bool>* Cancel = nullptr);

    /** M (decision 7): a smoothstep of the altitude over the datum, 0 at the
     *  handover and above, 1 at the drive floor and under. */
    DEEPSPACE_API double MorphFraction(double AltitudeCm, double DriveFloorAltitudeCm, double HandoverCm = HandoverAltitudeCm);

    /** Vertex V (grid or skirt) at morph M, pivot-relative: the built position
     *  less (1 - M) x its height along its own direction -- what M_SkyGround's
     *  World Position Offset does, (M - 1) x h x D. */
    DEEPSPACE_API FVector3d MorphedPosition(const FTileBuild& Tile, int32 Vertex, double Morph);

    /** The mesh's height above the datum at D, by its own triangles (the
     *  split along b-c), cm; unset if D is not on the tile. */
    DEEPSPACE_API TOptional<double> SampleHeight(const FTileBuild& Tile, const FVector3d& D);

    /** A child's height range from its parent's quarter, widened by what the
     *  parent's spacing omitted and its edge error: the child's LOD box. */
    DEEPSPACE_API TerrainQuadtree::FHeightRange ChildRange(const FTileBuild& Parent, int32 Quadrant, const IGroundField& Ground);

    /** The UV channels a mesh gets: UV1 = the normal's x and y; UV2 = its z
     *  and the height in km. */
    DEEPSPACE_API FVector2f UV1Of(const FTileBuild& Tile, int32 Vertex);
    DEEPSPACE_API FVector2f UV2Of(const FTileBuild& Tile, int32 Vertex);

    /** UV0: the vertex's cast shadow in x (M_SkyGround's per-vertex shadow),
     *  0 in y. UV0 was allocated full-precision and unused, so the shadow
     *  costs the mesh no memory. */
    DEEPSPACE_API FVector2f UV0Of(const FTileBuild& Tile, int32 Vertex);

    /** The grid vertex a skirt vertex hangs from; V itself for a grid vertex. */
    DEEPSPACE_API int32 GridOf(int32 Vertex);
}
