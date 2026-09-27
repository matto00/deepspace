#include "Surface/WorldRelief.h"

#include <cmath>

// The shared file, twice. WR_CPP selects its C++ half and WR_REAL its
// precision; each copy lives in its own namespace, so the two sets of WR_
// symbols never meet. The relative path is the file the GPU includes as
// /Project/Private/WorldRelief.ush: a change to it that C++ cannot compile
// fails ./build.sh.
#define WR_CPP 1
namespace WorldReliefF64
{
#define WR_REAL double
#include "../../../Shaders/Private/WorldRelief.ush"
#undef WR_REAL
}
namespace WorldReliefF32
{
#define WR_REAL float
#include "../../../Shaders/Private/WorldRelief.ush"
#undef WR_REAL
}
#undef WR_CPP

namespace WorldReliefLocal
{
    template <typename TTerms>
    FFaceTerms ToFaceTerms(const TTerms& Terms)
    {
        FFaceTerms Face;
        Face.Continent = Terms.Continent;
        Face.Detail = Terms.Detail;
        Face.DetailSlope = FVector3d(Terms.DetailSlopeX, Terms.DetailSlopeY, Terms.DetailSlopeZ);
        Face.CraterAlbedo = Terms.CraterAlbedo;
        Face.CraterSlope = FVector3d(Terms.CraterSlopeX, Terms.CraterSlopeY, Terms.CraterSlopeZ);
        return Face;
    }
}

FIntVector WorldReliefNoise::Hash16(int32 X, int32 Y, int32 Z)
{
    const WorldReliefF64::WR_Hash3 Hash = WorldReliefF64::WR_Rand3DPCG16(X, Y, Z);
    return FIntVector(static_cast<int32>(Hash.X), static_cast<int32>(Hash.Y), static_cast<int32>(Hash.Z));
}

double WorldReliefNoise::Simplex(const FVector3d& V, FVector3d& OutGradient)
{
    const WorldReliefF64::WR_Noise4 Noise = WorldReliefF64::WR_Simplex(V.X, V.Y, V.Z);
    OutGradient = FVector3d(Noise.GX, Noise.GY, Noise.GZ);
    return Noise.Value;
}

double WorldReliefNoise::GradientNoise(const FVector3d& V)
{
    return WorldReliefF64::WR_GradientNoise(V.X, V.Y, V.Z);
}

FVector4d WorldReliefNoise::Voronoi(const FVector3d& V)
{
    const WorldReliefF64::WR_Site Site = WorldReliefF64::WR_Voronoi(V.X, V.Y, V.Z);
    return FVector4d(Site.X, Site.Y, Site.Z, Site.Distance);
}

double WorldReliefNoise::CellHash(const FVector3d& V)
{
    return WorldReliefF64::WR_CellHash(V.X, V.Y, V.Z);
}

FFaceTerms WorldReliefNoise::FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch)
{
    return WorldReliefLocal::ToFaceTerms(WorldReliefF64::WR_SurfaceTerms(D.X, D.Y, D.Z, FootprintD, Offset.X, Offset.Y, Offset.Z, Stretch));
}

FFaceTerms WorldReliefNoise::FaceF32(const FVector3f& D, float FootprintD, const FVector3f& Offset, float Stretch)
{
    return WorldReliefLocal::ToFaceTerms(WorldReliefF32::WR_SurfaceTerms(D.X, D.Y, D.Z, FootprintD, Offset.X, Offset.Y, Offset.Z, Stretch));
}

double WorldReliefNoise::CraterMargin(const FVector3d& D, double FootprintD, const FVector3d& Offset)
{
    double Margin = TNumericLimits<double>::Max();
    for (int32 Band = 0; Band < WR_CRATER_BANDS; ++Band)
    {
        const double Frequency = WorldReliefF64::WR_CRATER_FREQUENCY[Band];
        if (FootprintD * Frequency >= 1.0)
        {
            continue; // faded to nothing: no step to land on either side of
        }
        const int32 Index = WorldReliefF64::WR_CRATER_INDEX[Band];
        const FVector3d V = D * Frequency + Offset + FVector3d(37.0 * Index, 59.0 * Index, 83.0 * Index);
        const FVector3d Cell(FMath::Floor(V.X), FMath::Floor(V.Y), FMath::Floor(V.Z));
        double Nearest = TNumericLimits<double>::Max();
        double Second = TNumericLimits<double>::Max();
        bool bNearestHeld = false;
        bool bSecondHeld = false;
        for (int32 Corner = 0; Corner < 8; ++Corner)
        {
            const FVector3d Lattice = Cell + FVector3d(static_cast<double>(Corner & 1), static_cast<double>((Corner >> 1) & 1),
                static_cast<double>((Corner >> 2) & 1));
            const WorldReliefF64::WR_Vec3 Jitter = WorldReliefF64::WR_VoronoiJitter(Lattice.X, Lattice.Y, Lattice.Z);
            const FVector3d Site = Lattice + FVector3d(Jitter.X, Jitter.Y, Jitter.Z);
            const double Distance = FVector3d::Dist(V, Site);
            // WR_CraterBand's keep: the hash of the site's own cell.
            const bool bHeld = WorldReliefF64::WR_CellHash(Site.X + 0.5, Site.Y + 0.5, Site.Z + 0.5) <= WorldReliefF64::WR_CRATER_KEEP;
            if (Distance < Nearest)
            {
                Second = Nearest;
                bSecondHeld = bNearestHeld;
                Nearest = Distance;
                bNearestHeld = bHeld;
            }
            else if (Distance < Second)
            {
                Second = Distance;
                bSecondHeld = bHeld;
            }
        }
        const double Reach = 1.5 * WorldReliefF64::WR_CRATER_RADIUS;
        if (bNearestHeld)
        {
            Margin = FMath::Min(Margin, FMath::Abs(Nearest - WorldReliefF64::WR_CRATER_RADIUS));
        }
        if ((bNearestHeld && Nearest < Reach) || (bSecondHeld && Second < Reach))
        {
            Margin = FMath::Min(Margin, Second - Nearest);
        }
    }
    return Margin;
}

WorldReliefNoise::FBands WorldReliefNoise::Bands()
{
    using namespace WorldReliefF64;
    FBands Out;
    Out.ContinentFrequency = WR_CONTINENT_FREQUENCY;
    Out.ContinentLevels = WR_CONTINENT_LEVELS;
    Out.LevelScale = WR_LEVEL_SCALE;
    for (int32 Band = 0; Band < WR_DETAIL_BANDS; ++Band)
    {
        Out.DetailFrequencies.Add(WR_DETAIL_FREQUENCY[Band]);
        Out.DetailWeights.Add(WR_DETAIL_WEIGHT[Band]);
        Out.DetailIndices.Add(WR_DETAIL_INDEX[Band]);
    }
    for (int32 Band = 0; Band < WR_CRATER_BANDS; ++Band)
    {
        Out.CraterFrequencies.Add(WR_CRATER_FREQUENCY[Band]);
        Out.CraterIndices.Add(WR_CRATER_INDEX[Band]);
    }
    Out.CraterRadius = WR_CRATER_RADIUS;
    Out.CraterInvRadius = WR_CRATER_INV_RADIUS;
    Out.CraterWall = WR_CRATER_WALL;
    Out.CraterRimFall = WR_CRATER_RIM_FALL;
    Out.CraterKeep = WR_CRATER_KEEP;
    Out.CraterFloorDark = WR_CRATER_FLOOR_DARK;
    Out.CraterRimBright = WR_CRATER_RIM_BRIGHT;
    return Out;
}
