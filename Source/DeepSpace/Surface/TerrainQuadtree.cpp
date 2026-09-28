#include "Surface/TerrainQuadtree.h"

namespace
{
    using namespace TerrainQuadtree;

    const FFace Faces[6] = {
        { FVector3d(1, 0, 0), FVector3d(0, 1, 0), FVector3d(0, 0, 1) },
        { FVector3d(-1, 0, 0), FVector3d(0, 0, 1), FVector3d(0, 1, 0) },
        { FVector3d(0, 1, 0), FVector3d(0, 0, 1), FVector3d(1, 0, 0) },
        { FVector3d(0, -1, 0), FVector3d(1, 0, 0), FVector3d(0, 0, 1) },
        { FVector3d(0, 0, 1), FVector3d(1, 0, 0), FVector3d(0, 1, 0) },
        { FVector3d(0, 0, -1), FVector3d(0, 1, 0), FVector3d(1, 0, 0) },
    };

    /** Normalised with one fixed order of operations: the same cube point
     *  gives the same direction whichever face built it. */
    FVector3d Normalised(const FVector3d& C)
    {
        const double L = FMath::Sqrt(C.X * C.X + C.Y * C.Y + C.Z * C.Z);
        return FVector3d(C.X / L, C.Y / L, C.Z / L);
    }

    FVector3d CubePoint(int32 FaceIndex, double CU, double CV)
    {
        const FFace& F = Faces[FaceIndex];
        return F.Normal + F.U * CU + F.V * CV;
    }

    /** A point a hair outside the middle of one of Key's edges: 0 bottom
     *  (J = 0), 1 right (I = 32), 2 top (J = 32), 3 left (I = 0). */
    FVector3d EdgeProbe(const FTileKey& Key, int32 Edge)
    {
        const double Count = static_cast<double>(1ll << Key.Level);
        const double U = Edge == 1 ? Key.X + 1.0 + 1.0e-3 : Edge == 3 ? Key.X - 1.0e-3 : Key.X + 0.5;
        const double V = Edge == 2 ? Key.Y + 1.0 + 1.0e-3 : Edge == 0 ? Key.Y - 1.0e-3 : Key.Y + 0.5;
        const double AU = 0.5 * UE_DOUBLE_PI * (U / Count - 0.5);
        const double AV = 0.5 * UE_DOUBLE_PI * (V / Count - 0.5);
        return Normalised(CubePoint(Key.Face, FMath::Tan(AU), FMath::Tan(AV)));
    }

    double DistanceToNode(const FVector3d& Ship, const FTileKey& Key, const FHeightRange& Range, double RadiusCm)
    {
        const FVector3d Centre = CentreDirection(Key) * (RadiusCm + 0.5 * (Range.MinCm + Range.MaxCm));
        const double Reach = (RadiusCm + Range.MaxCm) * HalfDiagonalRadians(Key) + 0.5 * (Range.MaxCm - Range.MinCm);
        return FMath::Max(0.0, (Ship - Centre).Size() - Reach);
    }

    struct FCutter
    {
        const FVector3d& Ship;
        const FVector3d Nadir;
        const FCutParams& Params;
        FBoundsOf BoundsOf;
        const TArray<double>& Factors;
        TArray<FTileKey>& Leaves;

        void Visit(const FTileKey& Key, const FHeightRange& Range)
        {
            if (!Visible(Key, Ship, Range, Params))
            {
                return;
            }
            if (Key.Level < Params.MaxLevel)
            {
                const bool bChain = KeyAt(Nadir, Key.Level) == Key;
                const double Factor = bChain ? Params.SplitFactor : Factors[Key.Level];
                const bool bSplit = DistanceToNode(Ship, Key, Range, Params.RadiusCm) < Factor * EdgeLengthCm(Key.Level, Params.RadiusCm)
                                 || (bChain && Params.GroundAltitudeCm < ChainFullAltitudeCm);
                if (bSplit)
                {
                    TOptional<FHeightRange> Children[4];
                    bool bKnown = true;
                    for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
                    {
                        Children[Quadrant] = BoundsOf(Key.Child(Quadrant));
                        bKnown &= Children[Quadrant].IsSet();
                    }
                    if (bKnown)
                    {
                        for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
                        {
                            Visit(Key.Child(Quadrant), *Children[Quadrant]);
                        }
                        return;
                    }
                }
            }
            Leaves.Add(Key);
        }
    };

    /** Split any leaf more than one level coarser than an edge neighbour of
     *  another, until none is. */
    void Balance(TArray<FTileKey>& Leaves)
    {
        TSet<FTileKey> Set(Leaves);
        for (bool bChanged = true; bChanged;)
        {
            bChanged = false;
            for (const FTileKey& Leaf : Set.Array())
            {
                if (!Set.Contains(Leaf))
                {
                    continue;
                }
                for (int32 Edge = 0; Edge < 4; ++Edge)
                {
                    const FVector3d Outside = EdgeProbe(Leaf, Edge);
                    for (int32 Level = Leaf.Level - 2; Level >= 0; --Level)
                    {
                        const FTileKey Coarse = KeyAt(Outside, Level);
                        if (Set.Contains(Coarse))
                        {
                            Set.Remove(Coarse);
                            for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
                            {
                                Set.Add(Coarse.Child(Quadrant));
                            }
                            bChanged = true;
                            break;
                        }
                    }
                }
            }
        }
        Leaves = Set.Array();
        Leaves.Sort([](const FTileKey& A, const FTileKey& B)
        {
            return A.Level != B.Level ? A.Level < B.Level : A.Face != B.Face ? A.Face < B.Face : A.Y != B.Y ? A.Y < B.Y : A.X < B.X;
        });
    }
}

const TerrainQuadtree::FFace& TerrainQuadtree::Face(int32 Index)
{
    return Faces[FMath::Clamp(Index, 0, 5)];
}

int32 TerrainQuadtree::MaxLevel(double RadiusCm)
{
    const double Tiles = RadiusCm * 0.5 * UE_DOUBLE_PI / (CellsPerTile * TargetSpacingCm);
    return Tiles <= 1.0 ? 0 : FMath::Min(24, FMath::CeilToInt(FMath::Log2(Tiles)));
}

double TerrainQuadtree::FaceCoordinate(int64 TwiceIndexMinusCount, int64 Count)
{
    if (TwiceIndexMinusCount >= Count)
    {
        return 1.0;
    }
    if (TwiceIndexMinusCount <= -Count)
    {
        return -1.0;
    }
    if (TwiceIndexMinusCount == 0)
    {
        return 0.0;
    }
    const double Magnitude = FMath::Tan(static_cast<double>(FMath::Abs(TwiceIndexMinusCount)) * (0.25 * UE_DOUBLE_PI / static_cast<double>(Count)));
    return TwiceIndexMinusCount < 0 ? -Magnitude : Magnitude;
}

FVector3d TerrainQuadtree::GridDirection(const FTileKey& Key, int32 I, int32 J)
{
    const int64 Count = static_cast<int64>(CellsPerTile) << Key.Level;
    const int64 GU = static_cast<int64>(Key.X) * CellsPerTile + I;
    const int64 GV = static_cast<int64>(Key.Y) * CellsPerTile + J;
    return Normalised(CubePoint(Key.Face, FaceCoordinate(2 * GU - Count, Count), FaceCoordinate(2 * GV - Count, Count)));
}

FVector3d TerrainQuadtree::CentreDirection(const FTileKey& Key)
{
    return GridDirection(Key, CellsPerTile / 2, CellsPerTile / 2);
}

FTileKey TerrainQuadtree::KeyAt(const FVector3d& D, int32 Level)
{
    const FVector3d A(FMath::Abs(D.X), FMath::Abs(D.Y), FMath::Abs(D.Z));
    int32 FaceIndex = A.X >= A.Y && A.X >= A.Z ? (D.X >= 0.0 ? 0 : 1) : A.Y >= A.Z ? (D.Y >= 0.0 ? 2 : 3) : (D.Z >= 0.0 ? 4 : 5);
    const FFace& F = Faces[FaceIndex];
    const double Along = FVector3d::DotProduct(D, F.Normal);
    const double AU = FMath::Atan(FVector3d::DotProduct(D, F.U) / Along);
    const double AV = FMath::Atan(FVector3d::DotProduct(D, F.V) / Along);
    const int64 Count = 1ll << Level;
    FTileKey Key;
    Key.Face = static_cast<uint8>(FaceIndex);
    Key.Level = static_cast<uint8>(Level);
    Key.X = static_cast<uint32>(FMath::Clamp<int64>(FMath::FloorToInt64((AU / (0.5 * UE_DOUBLE_PI) + 0.5) * Count), 0, Count - 1));
    Key.Y = static_cast<uint32>(FMath::Clamp<int64>(FMath::FloorToInt64((AV / (0.5 * UE_DOUBLE_PI) + 0.5) * Count), 0, Count - 1));
    return Key;
}

double TerrainQuadtree::EdgeLengthCm(int32 Level, double RadiusCm)
{
    return RadiusCm * 0.5 * UE_DOUBLE_PI / static_cast<double>(1ll << Level);
}

double TerrainQuadtree::SpacingCm(int32 Level, double RadiusCm)
{
    return EdgeLengthCm(Level, RadiusCm) / CellsPerTile;
}

double TerrainQuadtree::HalfDiagonalRadians(const FTileKey& Key)
{
    const FVector3d Centre = CentreDirection(Key);
    double Widest = 0.0;
    for (const FVector3d& Corner : { GridDirection(Key, 0, 0), GridDirection(Key, CellsPerTile, 0),
                                     GridDirection(Key, 0, CellsPerTile), GridDirection(Key, CellsPerTile, CellsPerTile) })
    {
        Widest = FMath::Max(Widest, FMath::Atan2(FVector3d::CrossProduct(Centre, Corner).Size(), FVector3d::DotProduct(Centre, Corner)));
    }
    return Widest;
}

bool TerrainQuadtree::Visible(const FTileKey& Key, const FVector3d& Ship, const FHeightRange& Range, const FCutParams& Params)
{
    const double Distance = Ship.Size();
    const double Occluder = FMath::Max(Params.OccluderRadiusCm, 1.0);
    if (Distance <= Occluder)
    {
        return true;
    }
    const double Top = FMath::Max(Params.RadiusCm + Range.MaxCm, Occluder);
    const double Horizon = FMath::Acos(Occluder / Distance) + FMath::Acos(FMath::Min(1.0, Occluder / Top));
    const FVector3d Centre = CentreDirection(Key);
    const double Apart = FMath::Atan2(FVector3d::CrossProduct(Ship, Centre).Size(), FVector3d::DotProduct(Ship, Centre));
    return Apart - HalfDiagonalRadians(Key) <= Horizon;
}

TArray<FTileKey, TFixedAllocator<4>> TerrainQuadtree::EdgeNeighbours(const FTileKey& Key)
{
    TArray<FTileKey, TFixedAllocator<4>> Out;
    for (int32 Edge = 0; Edge < 4; ++Edge)
    {
        Out.Add(KeyAt(EdgeProbe(Key, Edge), Key.Level));
    }
    return Out;
}

bool TerrainQuadtree::Balanced(TConstArrayView<FTileKey> Leaves)
{
    const TSet<FTileKey> Set(Leaves);
    for (const FTileKey& Leaf : Leaves)
    {
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            const FVector3d Outside = EdgeProbe(Leaf, Edge);
            for (int32 Level = Leaf.Level - 2; Level >= 0; --Level)
            {
                if (Set.Contains(KeyAt(Outside, Level)))
                {
                    return false;
                }
            }
        }
    }
    return true;
}

TerrainQuadtree::FCut TerrainQuadtree::SelectCut(const FVector3d& ShipFromCentre, const FCutParams& Params, FBoundsOf BoundsOf)
{
    FCut Cut;
    Cut.SplitFactorByLevel.Init(Params.SplitFactor, Params.MaxLevel + 1);
    const FVector3d Nadir = ShipFromCentre.GetSafeNormal();
    if (ShipFromCentre.Size() - Params.RadiusCm <= PrefetchAltitudeCm)
    {
        for (int32 Level = 0; Level <= FMath::Min(PrefetchLevel, Params.MaxLevel); ++Level)
        {
            Cut.Prefetch.Add(KeyAt(Nadir, Level));
        }
    }
    for (;;)
    {
        Cut.Leaves.Reset();
        FCutter Cutter{ ShipFromCentre, Nadir, Params, BoundsOf, Cut.SplitFactorByLevel, Cut.Leaves };
        for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
        {
            const FTileKey Root{ static_cast<uint8>(FaceIndex), 0, 0, 0 };
            if (const TOptional<FHeightRange> Range = BoundsOf(Root))
            {
                Cutter.Visit(Root, *Range);
            }
        }
        Balance(Cut.Leaves);
        if (Cut.Leaves.Num() <= Params.MaxTiles)
        {
            break;
        }
        // Over the ceiling: the coarsest level that can still be lowered is
        // lowered a step -- the farthest nodes first. The chain under the
        // ship always cuts at the full factor, so it is never coarsened.
        Cut.bCapBinding = true;
        int32 Lower = INDEX_NONE;
        for (int32 Level = 0; Level < Params.MaxLevel && Lower == INDEX_NONE; ++Level)
        {
            if (Cut.SplitFactorByLevel[Level] > MinSplitFactor + 1.0e-9)
            {
                Lower = Level;
            }
        }
        if (Lower == INDEX_NONE)
        {
            break;
        }
        Cut.SplitFactorByLevel[Lower] = FMath::Max(MinSplitFactor, Cut.SplitFactorByLevel[Lower] - SplitFactorStep);
    }
    return Cut;
}

TArray<FTileKey> TerrainQuadtree::BuildOrder(TConstArrayView<FTileKey> Prefetch, TConstArrayView<FTileKey> Leaves,
                                             const FVector3d& Nadir, TFunctionRef<bool(const FTileKey&)> NeedsBuild)
{
    TSet<FTileKey> Seen;
    TArray<FTileKey> Order;
    const auto Consider = [&](const FTileKey& Key)
    {
        bool bAlready = false;
        Seen.Add(Key, &bAlready);
        if (!bAlready && NeedsBuild(Key))
        {
            Order.Add(Key);
        }
        return bAlready;
    };
    for (const FTileKey& Key : Prefetch)
    {
        Consider(Key);
    }
    for (const FTileKey& Leaf : Leaves)
    {
        for (FTileKey Key = Leaf;; Key = Key.Parent())
        {
            if (Consider(Key) || Key.Level == 0)
            {
                break;
            }
        }
    }
    Order.Sort([&](const FTileKey& A, const FTileKey& B)
    {
        if (A.Level != B.Level)
        {
            return A.Level < B.Level;
        }
        return FVector3d::DotProduct(CentreDirection(A), Nadir) > FVector3d::DotProduct(CentreDirection(B), Nadir);
    });
    return Order;
}
