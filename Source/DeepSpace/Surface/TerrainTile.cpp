#include "Surface/TerrainTile.h"

#include "HAL/PlatformTime.h"

namespace TerrainTileLocal
{
    using namespace TerrainTile;
    constexpr int32 Side = Cells + 1;

    /** The grid vertices round the edge, each edge traversed with the tile's
     *  outside on its right: bottom +I, right +J, top -I, left -J. */
    const TArray<int32>& Ring()
    {
        static const TArray<int32> Ring = []()
        {
            TArray<int32> Out;
            for (int32 I = 0; I < Side; ++I) { Out.Add(I); }
            for (int32 J = 0; J < Side; ++J) { Out.Add(J * Side + Cells); }
            for (int32 I = Cells; I >= 0; --I) { Out.Add(Cells * Side + I); }
            for (int32 J = Cells; J >= 0; --J) { Out.Add(J * Side); }
            return Out;
        }();
        return Ring;
    }
}

const TArray<int32>& TerrainTile::Indices()
{
    using namespace TerrainTileLocal;
    static const TArray<int32> Indices = []()
    {
        TArray<int32> Out;
        Out.Reserve(3 * TriangleCount);
        for (int32 J = 0; J < Cells; ++J)
        {
            for (int32 I = 0; I < Cells; ++I)
            {
                const int32 A = J * Side + I;
                const int32 B = A + 1;
                const int32 C = A + Side;
                const int32 D = C + 1;
                Out.Append({ A, C, B, B, C, D });
            }
        }
        for (int32 Edge = 0; Edge < 4; ++Edge)
        {
            for (int32 K = 0; K < Cells; ++K)
            {
                const int32 R0 = Edge * Side + K;
                const int32 G0 = Ring()[R0];
                const int32 G1 = Ring()[R0 + 1];
                const int32 S0 = GridVerts + R0;
                const int32 S1 = S0 + 1;
                Out.Append({ G0, G1, S0, G1, S1, S0 });
            }
        }
        return Out;
    }();
    return Indices;
}

int32 TerrainTile::GridOf(int32 Vertex)
{
    return Vertex < GridVerts ? Vertex : TerrainTileLocal::Ring()[Vertex - GridVerts];
}

FTileBuild TerrainTile::Build(const IGroundField& Ground, const FTileKey& Key)
{
    using namespace TerrainTileLocal;
    const double Start = FPlatformTime::Seconds();
    FTileBuild Tile;
    Tile.Key = Key;
    Tile.RadiusCm = Ground.RadiusCm();
    const double R = Tile.RadiusCm;
    Tile.SpacingCm = TerrainQuadtree::SpacingCm(Key.Level, R);
    const FVector3d CentreD = TerrainQuadtree::CentreDirection(Key);
    Tile.Pivot = CentreD * (R + Ground.Height(CentreD, Tile.SpacingCm));
    Tile.Positions.SetNumUninitialized(VertexCount);
    Tile.Normals.SetNumUninitialized(VertexCount);
    Tile.Heights.SetNumUninitialized(GridVerts);
    Tile.Directions.SetNumUninitialized(GridVerts);
    Tile.Range = { TNumericLimits<double>::Max(), -TNumericLimits<double>::Max() };
    for (TerrainQuadtree::FHeightRange& Quadrant : Tile.QuadrantRange)
    {
        Quadrant = Tile.Range;
    }

    for (int32 J = 0; J < Side; ++J)
    {
        for (int32 I = 0; I < Side; ++I)
        {
            const int32 V = J * Side + I;
            const FVector3d D = TerrainQuadtree::GridDirection(Key, I, J);
            FVector3d Grad = FVector3d::ZeroVector;
            const double H = Ground.HeightAndGradient(D, Grad, Tile.SpacingCm);
            const FVector3d Along = Grad - D * FVector3d::DotProduct(Grad, D);
            Tile.Directions[V] = D;
            Tile.Heights[V] = H;
            Tile.Positions[V] = FVector3f(D * (R + H) - Tile.Pivot);
            Tile.Normals[V] = FVector3f((D - Along / R).GetSafeNormal());
            Tile.Range.MinCm = FMath::Min(Tile.Range.MinCm, H);
            Tile.Range.MaxCm = FMath::Max(Tile.Range.MaxCm, H);
            for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
            {
                const bool bInI = (Quadrant & 1) ? I >= Cells / 2 : I <= Cells / 2;
                const bool bInJ = (Quadrant >> 1) ? J >= Cells / 2 : J <= Cells / 2;
                if (bInI && bInJ)
                {
                    Tile.QuadrantRange[Quadrant].MinCm = FMath::Min(Tile.QuadrantRange[Quadrant].MinCm, H);
                    Tile.QuadrantRange[Quadrant].MaxCm = FMath::Max(Tile.QuadrantRange[Quadrant].MaxCm, H);
                }
            }
        }
    }

    // The edges' own interpolation error, measured at every segment's middle.
    for (int32 R0 = 0; R0 + 1 < Ring().Num(); ++R0)
    {
        if ((R0 + 1) % Side == 0)
        {
            continue;   // between edges, not along one
        }
        const int32 A = Ring()[R0];
        const int32 B = Ring()[R0 + 1];
        const FVector3d Middle = (Tile.Directions[A] + Tile.Directions[B]).GetSafeNormal();
        Tile.MaxEdgeErrorCm = FMath::Max(Tile.MaxEdgeErrorCm,
            FMath::Abs(Ground.Height(Middle, Tile.SpacingCm) - 0.5 * (Tile.Heights[A] + Tile.Heights[B])));
    }
    Tile.SkirtDepthCm = 1.5 * (Tile.MaxEdgeErrorCm + Ground.OmittedBoundCm(Tile.SpacingCm)) + 1.0;

    for (int32 S = 0; S < SkirtVerts; ++S)
    {
        const int32 G = Ring()[S];
        const FVector3d& D = Tile.Directions[G];
        Tile.Positions[GridVerts + S] = FVector3f(D * (R + Tile.Heights[G] - Tile.SkirtDepthCm) - Tile.Pivot);
        Tile.Normals[GridVerts + S] = Tile.Normals[G];
    }
    Tile.BuildSeconds = FPlatformTime::Seconds() - Start;
    return Tile;
}

double TerrainTile::MorphFraction(double AltitudeCm, double DriveFloorAltitudeCm, double HandoverCm)
{
    if (DriveFloorAltitudeCm >= HandoverCm)
    {
        return AltitudeCm < HandoverCm ? 1.0 : 0.0;
    }
    const double T = FMath::Clamp((HandoverCm - AltitudeCm) / (HandoverCm - DriveFloorAltitudeCm), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

FVector3d TerrainTile::MorphedPosition(const FTileBuild& Tile, int32 Vertex, double Morph)
{
    const int32 G = GridOf(Vertex);
    return FVector3d(Tile.Positions[Vertex]) - Tile.Directions[G] * ((1.0 - Morph) * Tile.Heights[G]);
}

TOptional<double> TerrainTile::SampleHeight(const FTileBuild& Tile, const FVector3d& D)
{
    using namespace TerrainTileLocal;
    const TerrainQuadtree::FFace& Face = TerrainQuadtree::Face(Tile.Key.Face);
    const double Along = FVector3d::DotProduct(D, Face.Normal);
    if (!(Along > 0.0))
    {
        return {};
    }
    const double Count = static_cast<double>(static_cast<int64>(Cells) << Tile.Key.Level);
    const double GU = (FMath::Atan(FVector3d::DotProduct(D, Face.U) / Along) / (0.5 * UE_DOUBLE_PI) + 0.5) * Count - Tile.Key.X * static_cast<double>(Cells);
    const double GV = (FMath::Atan(FVector3d::DotProduct(D, Face.V) / Along) / (0.5 * UE_DOUBLE_PI) + 0.5) * Count - Tile.Key.Y * static_cast<double>(Cells);
    if (GU < -1.0e-9 || GV < -1.0e-9 || GU > Cells + 1.0e-9 || GV > Cells + 1.0e-9)
    {
        return {};
    }
    const int32 I = FMath::Clamp(FMath::FloorToInt32(GU), 0, Cells - 1);
    const int32 J = FMath::Clamp(FMath::FloorToInt32(GV), 0, Cells - 1);
    const double FX = GU - I;
    const double FY = GV - J;
    const double A = Tile.Heights[J * Side + I];
    const double B = Tile.Heights[J * Side + I + 1];
    const double C = Tile.Heights[(J + 1) * Side + I];
    const double Dd = Tile.Heights[(J + 1) * Side + I + 1];
    return FX + FY <= 1.0 ? A + FX * (B - A) + FY * (C - A)
                          : Dd + (1.0 - FX) * (C - Dd) + (1.0 - FY) * (B - Dd);
}

TerrainQuadtree::FHeightRange TerrainTile::ChildRange(const FTileBuild& Parent, int32 Quadrant, const IGroundField& Ground)
{
    const double Widen = Ground.OmittedBoundCm(Parent.SpacingCm) + Parent.MaxEdgeErrorCm;
    const TerrainQuadtree::FHeightRange& Quarter = Parent.QuadrantRange[Quadrant & 3];
    return { Quarter.MinCm - Widen, Quarter.MaxCm + Widen };
}

FVector2f TerrainTile::UV1Of(const FTileBuild& Tile, int32 Vertex)
{
    return FVector2f(Tile.Normals[Vertex].X, Tile.Normals[Vertex].Y);
}

FVector2f TerrainTile::UV2Of(const FTileBuild& Tile, int32 Vertex)
{
    return FVector2f(Tile.Normals[Vertex].Z, static_cast<float>(Tile.Heights[GridOf(Vertex)] / HeightUVScaleCm));
}
