#include "Sky/SkySphereMesh.h"

SkySphereMesh::FGeometry SkySphereMesh::Build(int32 CellsPerEdge, float InRadius)
{
    FGeometry Out;
    const int32 N = FMath::Max(CellsPerEdge, 1);

    // The cube coordinate of each line across a face, at equal angles from
    // its centre. Built symmetric and exact at the edges -- tan(pi / 4) is not
    // 1 in doubles -- so a seam vertex is the same tuple from both faces it
    // sits on, and welding by value is exact.
    TArray<double> T;
    T.SetNum(N + 1);
    for (int32 I = 0; I <= N; ++I)
    {
        T[I] = FMath::Tan(-0.25 * UE_DOUBLE_PI + 0.5 * UE_DOUBLE_PI * I / N);
    }
    T[0] = -1.0;
    T[N] = 1.0;
    for (int32 I = 0; I <= N / 2; ++I)
    {
        T[N - I] = -T[I];
    }
    if (N % 2 == 0)
    {
        T[N / 2] = 0.0;
    }

    // Each face by its outward axis and two in-plane axes, (U x V) = Normal,
    // so every face's cells run the same way round.
    struct FFace { FVector Normal, U, V; };
    const FFace Faces[6] = {
        { FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1) },
        { FVector(-1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0) },
        { FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0) },
        { FVector(0, -1, 0), FVector(1, 0, 0), FVector(0, 0, 1) },
        { FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0) },
        { FVector(0, 0, -1), FVector(0, 1, 0), FVector(1, 0, 0) },
    };

    TMap<FVector, int32> Welded;
    TArray<int32> Grid;
    Grid.SetNum((N + 1) * (N + 1));
    for (const FFace& Face : Faces)
    {
        for (int32 J = 0; J <= N; ++J)
        {
            for (int32 I = 0; I <= N; ++I)
            {
                const FVector Cube = Face.Normal + Face.U * T[I] + Face.V * T[J];
                int32* Found = Welded.Find(Cube);
                if (!Found)
                {
                    const FVector Direction = Cube.GetUnsafeNormal();
                    const int32 Index = Out.Positions.Add(FVector3f(Direction * InRadius));
                    Out.Normals.Add(FVector3f(Direction));
                    Out.UVs.Add(FVector2f(static_cast<float>(I) / N, static_cast<float>(J) / N));
                    Found = &Welded.Add(Cube, Index);
                }
                Grid[J * (N + 1) + I] = *Found;
            }
        }
        for (int32 J = 0; J < N; ++J)
        {
            for (int32 I = 0; I < N; ++I)
            {
                const int32 A = Grid[J * (N + 1) + I];
                const int32 B = Grid[J * (N + 1) + I + 1];
                const int32 C = Grid[(J + 1) * (N + 1) + I + 1];
                const int32 D = Grid[(J + 1) * (N + 1) + I];
                // A, B, C, D run anticlockwise seen from outside; the engine
                // fronts a triangle whose (B - A) x (C - A) points inward, so
                // each is emitted the other way round. Split along the
                // shorter diagonal: toward the cube's corners the cells lean
                // into rhombi, and the long diagonal is the widest chord the
                // limb would show.
                const bool bAC = (Out.Positions[A] - Out.Positions[C]).SizeSquared()
                    <= (Out.Positions[B] - Out.Positions[D]).SizeSquared();
                if (bAC)
                {
                    Out.Indices.Append({ A, C, B, A, D, C });
                }
                else
                {
                    Out.Indices.Append({ A, D, B, B, D, C });
                }
            }
        }
    }
    return Out;
}

double SkySphereMesh::LongestEdgeAngle(const FGeometry& Geometry)
{
    double Widest = 0.0;
    for (int32 Tri = 0; Tri + 2 < Geometry.Indices.Num(); Tri += 3)
    {
        for (int32 Edge = 0; Edge < 3; ++Edge)
        {
            const FVector A(Geometry.Normals[Geometry.Indices[Tri + Edge]]);
            const FVector B(Geometry.Normals[Geometry.Indices[Tri + (Edge + 1) % 3]]);
            // atan2 of cross and dot: exact for the milliradian edges here,
            // where acos of a dot next to 1 has nothing left to say.
            Widest = FMath::Max(Widest, FMath::Atan2((A ^ B).Size(), A | B));
        }
    }
    return Widest;
}

double SkySphereMesh::FacetPixels(double EdgeAngle, double DistanceOverRadius, double PixelAngle)
{
    const double Sagitta = 2.0 * FMath::Square(FMath::Sin(0.25 * EdgeAngle));   // 1 - cos(a / 2), stably
    const double ToLimb = FMath::Sqrt(FMath::Max(FMath::Square(DistanceOverRadius) - 1.0, 0.0));
    return ToLimb > 0.0 && PixelAngle > 0.0 ? Sagitta / ToLimb / PixelAngle : TNumericLimits<double>::Max();
}

double SkySphereMesh::NearestDistanceFor(double EdgeAngle, double MaxPixels, double PixelAngle)
{
    const double Sagitta = 2.0 * FMath::Square(FMath::Sin(0.25 * EdgeAngle));
    const double ToLimb = Sagitta / FMath::Max(MaxPixels * PixelAngle, UE_DOUBLE_SMALL_NUMBER);
    return FMath::Sqrt(1.0 + ToLimb * ToLimb);
}

double SkySphereMesh::ScreenSize(double DistanceOverRadius, double FovDegrees, double Aspect)
{
    const double HalfScale = 0.5 / FMath::Tan(0.5 * FMath::DegreesToRadians(FovDegrees));
    const double Multiple = HalfScale * FMath::Max(1.0, Aspect);
    return 2.0 * Multiple / FMath::Max(DistanceOverRadius, UE_DOUBLE_SMALL_NUMBER);
}

double SkySphereMesh::DesignPixelAngle()
{
    return 2.0 * FMath::Tan(0.5 * FMath::DegreesToRadians(DesignFovDegrees)) / DesignWidthPixels;
}

TArray<float> SkySphereMesh::LodScreenSizes(TConstArrayView<double> EdgeAngles)
{
    TArray<float> Sizes;
    for (int32 Lod = 0; Lod < EdgeAngles.Num(); ++Lod)
    {
        const double Distance = Lod == 0 ? 1.0 : NearestDistanceFor(EdgeAngles[Lod], MaxFacetPixels, DesignPixelAngle());
        Sizes.Add(static_cast<float>(ScreenSize(Distance, DesignFovDegrees, DesignAspect)));
    }
    return Sizes;
}
