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

namespace WR64 = WorldReliefF64;

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

FFaceTerms WorldReliefNoise::FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch,
                                       double VertexBandLimit)
{
    return WorldReliefLocal::ToFaceTerms(WorldReliefF64::WR_SurfaceTerms(D.X, D.Y, D.Z, FootprintD, Offset.X, Offset.Y, Offset.Z, Stretch, VertexBandLimit));
}

FFaceTerms WorldReliefNoise::FaceF32(const FVector3f& D, float FootprintD, const FVector3f& Offset, float Stretch,
                                       float VertexBandLimit)
{
    return WorldReliefLocal::ToFaceTerms(WorldReliefF32::WR_SurfaceTerms(D.X, D.Y, D.Z, FootprintD, Offset.X, Offset.Y, Offset.Z, Stretch, VertexBandLimit));
}

double WorldReliefNoise::CraterBandMargin(const FVector3d& D, double FootprintD, const FVector3d& Offset, int32 Band)
{
    double Margin = TNumericLimits<double>::Max();
    check(Band >= 0 && Band < WR_CRATER_BANDS);
    {
        const double Frequency = WorldReliefF64::WR_CRATER_FREQUENCY[Band];
        if (FootprintD * Frequency >= 1.0)
        {
            return Margin; // faded to nothing: no step to land on either side of
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

double WorldReliefNoise::CraterMargin(const FVector3d& D, double FootprintD, const FVector3d& Offset)
{
    double Margin = TNumericLimits<double>::Max();
    for (int32 Band = 0; Band < WR_CRATER_BANDS; ++Band)
    {
        Margin = FMath::Min(Margin, CraterBandMargin(D, FootprintD, Offset, Band));
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

FWorldRelief::FWorldRelief(const FWorldReliefParams& InParams)
    : Params(InParams)
{
    const WorldReliefNoise::FBands Bands = WorldReliefNoise::Bands();
    Frequencies = Bands.DetailFrequencies;
    Weights = Bands.DetailWeights;
    Indices = Bands.DetailIndices;
    // The C++ goes on where the GPU stops: an octave a band, each from its
    // own corner of the noise at the next band number, at the finest band's
    // weight, down to BandLimitCm and never past it.
    while (Params.RadiusCm > 0.0 && Params.RadiusCm / (2.0 * Frequencies.Last()) >= BandLimitCm)
    {
        // Copies first: Add must never be handed a reference into the array it grows.
        const double Frequency = 2.0 * Frequencies.Last();
        const double Weight = Weights.Last();
        const int32 Index = Indices.Last() + 1;
        Frequencies.Add(Frequency);
        Weights.Add(Weight);
        Indices.Add(Index);
    }
    for (int32 Band = 0; Band < Frequencies.Num(); ++Band)
    {
        SumBound += Weights[Band] * WorldReliefNoise::SimplexValueBound / Frequencies[Band];
    }
}

double FWorldRelief::FootprintOf(double FootprintCm) const
{
    return Params.RadiusCm > 0.0 ? FMath::Max(FootprintCm, 0.0) / Params.RadiusCm : 0.0;
}

double FWorldRelief::DetailSum(const FVector3d& D, double FootprintD, FVector3d* OutGradient) const
{
    double Total = 0.0;
    FVector3d Gradient = FVector3d::ZeroVector;
    for (int32 Band = 0; Band < Frequencies.Num(); ++Band)
    {
        const int32 Index = Indices[Band];
        // The band's lattice offset split as the shared file splits it
        // (Task 31b): the same function as the whole sum, rounded where the
        // GPU's float build rounds.
        const WorldReliefF64::WR_Noise4 Noise = WorldReliefF64::WR_DetailBand(D.X, D.Y, D.Z, Frequencies[Band],
            WorldReliefF64::WR_SplitOffset(Params.SeedOffset.X, Params.SeedOffset.Y, Params.SeedOffset.Z, Index),
            FootprintD, Weights[Band]);
        // A band's height is its value over its frequency, so its gradient
        // in D is the noise's own gradient: every band has the same slope.
        Total += Noise.Value / Frequencies[Band];
        Gradient += FVector3d(Noise.GX, Noise.GY, Noise.GZ);
    }
    if (OutGradient)
    {
        *OutGradient = Gradient;
    }
    return Total;
}

double FWorldRelief::DetailSlopeBound() const
{
    double GradientBound = 0.0;
    for (int32 Band = 0; Band < Frequencies.Num(); ++Band)
    {
        GradientBound += Weights[Band] * WorldReliefNoise::SimplexGradientBound;
    }
    return GradientBound;
}

double FWorldRelief::DetailOmittedBound(double FootprintD) const
{
    double Omitted = 0.0;
    for (int32 Band = 0; Band < Frequencies.Num(); ++Band)
    {
        // Faded by saturate(1 - footprint x frequency): the fade took at most
        // that share of the band's bound.
        Omitted += FMath::Min(1.0, FMath::Max(FootprintD, 0.0) * Frequencies[Band]) * Weights[Band] * WorldReliefNoise::SimplexValueBound / Frequencies[Band];
    }
    return Omitted;
}

double FWorldRelief::PeakCap(double X, double* OutSlope)
{
    const double Magnitude = FMath::Abs(X);
    if (Magnitude <= PeakCapKnee)
    {
        if (OutSlope)
        {
            *OutSlope = 1.0;
        }
        return X;
    }
    constexpr double Room = 1.0 - PeakCapKnee;
    const double Bent = std::tanh((Magnitude - PeakCapKnee) / Room);
    if (OutSlope)
    {
        *OutSlope = 1.0 - Bent * Bent;
    }
    // Never past 1: tanh stays below 1 until it rounds to it, and then this
    // is the knee plus exactly the room left.
    return FMath::Sign(X) * FMath::Min(1.0, PeakCapKnee + Room * Bent);
}

namespace WorldReliefCraterLocal
{
    /** The crater profile's largest magnitude, depth units: the bowl's floor,
     *  depth (1 - rim), or the rim's crest, depth rim. */
    double ProfileMagnitude()
    {
        return FMath::Max(WR64::WR_CRATER_DEPTH * (1.0 - WR64::WR_CRATER_RIM), WR64::WR_CRATER_DEPTH * WR64::WR_CRATER_RIM);
    }

    /** Its slope's largest magnitude: the bowl's 2 depth at the crest, or the
     *  rim's 4 depth rim. */
    double WallMagnitude()
    {
        return FMath::Max(2.0 * WR64::WR_CRATER_DEPTH, 4.0 * WR64::WR_CRATER_DEPTH * WR64::WR_CRATER_RIM);
    }
}

double FWorldRelief::CraterBound()
{
    using namespace WorldReliefCraterLocal;
    double Bound = 0.0;
    for (int32 Crater = 0; Crater < WR_CRATER_BANDS; ++Crater)
    {
        const double F = WR64::WR_CRATER_FREQUENCY[Crater];
        Bound += WR64::WR_CRATER_BOUND_COUNT * WR64::WR_CRATER_RADIUS * ProfileMagnitude() / F;
    }
    return Bound;
}

double FWorldRelief::CraterSlopeBound()
{
    return WR_CRATER_BANDS * WR64::WR_CRATER_BOUND_COUNT * WorldReliefCraterLocal::WallMagnitude();
}

double FWorldRelief::CraterOmittedBound(double FootprintRadius)
{
    using namespace WorldReliefCraterLocal;
    double Bound = 0.0;
    for (int32 Crater = 0; Crater < WR_CRATER_BANDS; ++Crater)
    {
        const double F = WR64::WR_CRATER_FREQUENCY[Crater];
        Bound += (1.0 - FMath::Clamp(1.0 - FootprintRadius * F, 0.0, 1.0)) * WR64::WR_CRATER_BOUND_COUNT * WR64::WR_CRATER_RADIUS * ProfileMagnitude() / F;
    }
    return Bound;
}

double FWorldRelief::CraterSum(const FVector3d& D, double FootprintRadius, FVector3d* OutGradient) const
{
    const WR64::WR_CraterTerm Craters = WR64::WR_CraterSum(D.X, D.Y, D.Z, Params.SeedOffset.X, Params.SeedOffset.Y,
                                                           Params.SeedOffset.Z, FootprintRadius, 1.0);
    if (OutGradient)
    {
        *OutGradient = FVector3d(Craters.GX, Craters.GY, Craters.GZ);
    }
    return Craters.H;
}

bool FWorldRelief::HasGround() const
{
    return Params.Ground == EGround::Solid && Params.PeakCm > 0.0 && Params.RadiusCm > 0.0 && SumBound > 0.0;
}

double FWorldRelief::Kept() const
{
    return FMath::Max(Params.Cratering, 0.0);
}

double FWorldRelief::MaxHeightCm() const
{
    return Params.Ground == EGround::Solid ? FMath::Max(Params.PeakCm, 0.0) : 0.0;
}

double FWorldRelief::MinHeightCm() const
{
    return -MaxHeightCm();
}

double FWorldRelief::SlopeScale() const
{
    return HasGround() ? Params.PeakCm / (Params.RadiusCm * SMax()) : 0.0;
}

double FWorldRelief::Height(const FVector3d& D, double FootprintCm) const
{
    FVector3d Unused;
    return HeightAndGradient(D, Unused, FootprintCm);
}

double FWorldRelief::HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const
{
    Grad = FVector3d::ZeroVector;
    if (!HasGround())
    {
        return 0.0;
    }
    const double Footprint = FootprintOf(FootprintCm);
    FVector3d DetailGradient;
    const double Detail = DetailSum(D, Footprint, &DetailGradient);
    FVector3d CraterGradient;
    const double Craters = CraterSum(D, Footprint, &CraterGradient);
    const double Total = Detail + Kept() * Craters;
    double CapSlope = 1.0;
    const double Capped = PeakCap(Total / SMax(), &CapSlope);
    Grad = (DetailGradient + CraterGradient * Kept()) * (Params.PeakCm / SMax() * CapSlope);
    return Params.PeakCm * Capped;
}

FFaceTerms FWorldRelief::Face(const FVector3d& D, double FootprintCm) const
{
    return WorldReliefNoise::FaceF64(D, FootprintOf(FootprintCm), Params.SeedOffset, 1.0);
}

double FWorldRelief::OmittedBoundCm(double FootprintCm) const
{
    if (!HasGround())
    {
        return 0.0;
    }
    // PeakCap is 1-Lipschitz, so the height moves at most PeakCm / S_max
    // times what S lost. And what remains of S, at most its bound less what
    // was omitted, bounds the faded height: with every band faded it is 0,
    // and the difference is at most the peak itself.
    const double Footprint = FootprintOf(FootprintCm);
    const double Omitted = DetailOmittedBound(Footprint) + Kept() * CraterOmittedBound(Footprint);
    const double Remaining = FMath::Max(0.0, SumBound + Kept() * CraterBound() - Omitted);
    const double ByLipschitz = Params.PeakCm * Omitted / SMax();
    const double ByRange = Params.PeakCm * (1.0 + FMath::Min(1.0, Remaining / SMax()));
    return FMath::Min(ByLipschitz, ByRange);
}

double FWorldRelief::MaxSlope() const
{
    if (!HasGround())
    {
        return 0.0;
    }
    // dHeight/dD is bounded by PeakCm / S_max x PeakCap's slope (at most 1)
    // x the sum's Lipschitz bound, detail and craters; an arc of length s
    // along the ground moves D by s / R.
    return SlopeScale() * (DetailSlopeBound() + Kept() * CraterSlopeBound());
}

double FWorldRelief::FinestWavelengthCm() const
{
    return Frequencies.IsEmpty() ? 0.0 : Params.RadiusCm / Frequencies.Last();
}
