#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/** One node of a world's cube-sphere quadtree (landing decision 6). */
struct DEEPSPACE_API FTileKey
{
    /** 0..5, SkySphereMesh's face table: +X, -X, +Y, -Y, +Z, -Z. */
    uint8 Face = 0;

    /** 0 is the whole face; each level halves the angle. */
    uint8 Level = 0;

    /** 0 .. 2^Level - 1 along the face's U and V. */
    uint32 X = 0;
    uint32 Y = 0;

    bool operator==(const FTileKey& Other) const
    {
        return Face == Other.Face && Level == Other.Level && X == Other.X && Y == Other.Y;
    }
    bool operator!=(const FTileKey& Other) const { return !(*this == Other); }

    FTileKey Parent() const { return FTileKey{ Face, static_cast<uint8>(Level - 1), X >> 1, Y >> 1 }; }

    /** Quadrant 0: (2X, 2Y); 1: (2X + 1, 2Y); 2: (2X, 2Y + 1); 3: (2X + 1, 2Y + 1). */
    FTileKey Child(int32 Quadrant) const
    {
        return FTileKey{ Face, static_cast<uint8>(Level + 1), (X << 1) | static_cast<uint32>(Quadrant & 1), (Y << 1) | static_cast<uint32>(Quadrant >> 1) };
    }

    int32 QuadrantInParent() const { return static_cast<int32>((X & 1) | ((Y & 1) << 1)); }

    /** Other is this node or lies under it. */
    bool Contains(const FTileKey& Other) const
    {
        return Other.Face == Face && Other.Level >= Level
            && (Other.X >> (Other.Level - Level)) == X && (Other.Y >> (Other.Level - Level)) == Y;
    }
};

FORCEINLINE uint32 GetTypeHash(const FTileKey& Key)
{
    return HashCombine(HashCombine(::GetTypeHash(static_cast<uint32>(Key.Face) | (static_cast<uint32>(Key.Level) << 8)), ::GetTypeHash(Key.X)), ::GetTypeHash(Key.Y));
}

/**
 * The terrain's quadtree as pure arithmetic (landing decision 6): six cube
 * faces on SM_SkyBody's equal-angle mapping, split into quadrants; CDLOD
 * measured from the ship's origin, never the camera, so it is the same in
 * tests and in play; each node's box its own height range, taken from its
 * parent's built vertices (the caller's BoundsOf); an explicit 2:1 rule; the
 * horizon; the ship's own chain to MaxLevel near the ground; and MaxTiles,
 * a ceiling that coarsens the farthest levels first. No UObject, no world.
 */
namespace TerrainQuadtree
{
    inline constexpr int32 CellsPerTile = 32;
    inline constexpr double TargetSpacingCm = 100.0;
    inline constexpr double DefaultSplitFactor = 2.0;
    inline constexpr int32 DefaultMaxTiles = 2500;

    /** The chain under the ship to this level is wanted from PrefetchAltitudeCm
     *  down, so the coarse cap is resident well before the handover. */
    inline constexpr int32 PrefetchLevel = 8;
    inline constexpr double PrefetchAltitudeCm = 1.0e8;

    /** Under this height above the ground the ship's own chain goes to
     *  MaxLevel, whatever the distance rule says: the drawn ground under the
     *  ship is then within GearClearance / 10 of the analytic ground. */
    inline constexpr double ChainFullAltitudeCm = 1.0e5;

    inline constexpr double MinSplitFactor = 1.0;
    inline constexpr double SplitFactorStep = 0.25;

    struct FFace
    {
        FVector3d Normal;
        FVector3d U;
        FVector3d V;
    };

    /** SkySphereMesh's face table, (U x V) = Normal. */
    DEEPSPACE_API const FFace& Face(int32 Index);

    /** ceil(log2(R x pi/2 / (32 x TargetSpacing))), at least 0. */
    DEEPSPACE_API int32 MaxLevel(double RadiusCm);

    /** The cube coordinate tan(angle) of grid line Index of Count across a
     *  face, as 2 Index - Count: exactly -1, 0 and 1 at the ends and middle,
     *  and odd, tan of |angle| signed, so a line shared by two faces or two
     *  levels is the same number to the last bit. */
    DEEPSPACE_API double FaceCoordinate(int64 TwiceIndexMinusCount, int64 Count);

    /** Unit direction of grid vertex (I, J), 0..CellsPerTile, of Key: the cube
     *  point normalised with a fixed order of operations, so shared vertices
     *  of neighbouring nodes are identical. */
    DEEPSPACE_API FVector3d GridDirection(const FTileKey& Key, int32 I, int32 J);

    /** The node's centre direction: its grid vertex (16, 16). */
    DEEPSPACE_API FVector3d CentreDirection(const FTileKey& Key);

    /** The node at Level holding direction D. */
    DEEPSPACE_API FTileKey KeyAt(const FVector3d& D, int32 Level);

    /** A node's edge at Level, R x (pi/2) / 2^Level, cm; and its vertex
     *  spacing, that over CellsPerTile. */
    DEEPSPACE_API double EdgeLengthCm(int32 Level, double RadiusCm);
    DEEPSPACE_API double SpacingCm(int32 Level, double RadiusCm);

    /** The largest angle from the node's centre to a corner. */
    DEEPSPACE_API double HalfDiagonalRadians(const FTileKey& Key);

    struct FHeightRange
    {
        double MinCm = 0.0;
        double MaxCm = 0.0;
    };

    struct FCutParams
    {
        double RadiusCm = 0.0;
        int32 MaxLevel = 0;
        double SplitFactor = DefaultSplitFactor;
        int32 MaxTiles = DefaultMaxTiles;

        /** The horizon's sphere: R plus the world's lowest height. */
        double OccluderRadiusCm = 0.0;

        /** The ship's origin above the ground under it, for the chain rule. */
        double GroundAltitudeCm = TNumericLimits<double>::Max();
    };

    struct FCut
    {
        /** The nodes to draw: the cut's leaves, culled nodes left out. */
        TArray<FTileKey> Leaves;

        /** The chain under the ship to PrefetchLevel, within PrefetchAltitudeCm. */
        TArray<FTileKey> Prefetch;

        /** The split factor each level was cut with: lowered from the
         *  coarsest level up while the cap binds. */
        TArray<double> SplitFactorByLevel;

        bool bCapBinding = false;
    };

    /** A node's height range, or unset when it cannot be known yet (its
     *  parent is not built): the cut then stops at the parent. */
    using FBoundsOf = TFunctionRef<TOptional<FHeightRange>(const FTileKey&)>;

    /** The cut for a ship at ShipFromCentre (cm, universe axes). BoundsOf of a
     *  root face must be set (the world's analytic bounds). */
    DEEPSPACE_API FCut SelectCut(const FVector3d& ShipFromCentre, const FCutParams& Params, FBoundsOf BoundsOf);

    /** False when the node, at its own top height, lies wholly below the
     *  horizon of the occluder sphere as seen from Ship. */
    DEEPSPACE_API bool Visible(const FTileKey& Key, const FVector3d& Ship, const FHeightRange& Range, const FCutParams& Params);

    /** The same-level nodes across each of Key's four edges. */
    DEEPSPACE_API TArray<FTileKey, TFixedAllocator<4>> EdgeNeighbours(const FTileKey& Key);

    /** No leaf has an edge neighbour more than one level coarser. */
    DEEPSPACE_API bool Balanced(TConstArrayView<FTileKey> Leaves);
}
