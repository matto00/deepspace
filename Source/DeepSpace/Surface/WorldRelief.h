#pragma once

#include "CoreMinimal.h"
#include "Surface/WorldReliefParams.h"

/**
 * A world's ground in C++, from Shaders/Private/WorldRelief.ush: the one noise
 * file the GPU compiles too (landing decision 1; ADR 0006, amended). Pure: no
 * UObject, no UWorld, no CVar.
 */

/** M_SkyBody's raw face terms at one direction and footprint, before any
 *  knob or clamp: what the shared file's WR_SurfaceTerms returns, and what
 *  M_SkyReliefProbe draws for Eyes.WorldReliefParity. */
struct FFaceTerms
{
    /** The coarse band, -1..1: continents and basins, or a giant's belts' wander. */
    double Continent = 0.0;

    /** The detail bands' values, each times its weight and its footprint fade. */
    double Detail = 0.0;

    /** Their gradient in noise space per unit of the stretched direction:
     *  the relief's slope before ReliefScale. */
    FVector3d DetailSlope = FVector3d::ZeroVector;

    /** The crater bands' albedo: darker floors, brighter rims. */
    double CraterAlbedo = 0.0;

    /** Their height's gradient, the same units. */
    FVector3d CraterSlope = FVector3d::ZeroVector;
};

/** The shared file's functions, reached from C++ in double unless named
 *  otherwise: for the tests that pin them, the parity test that holds them
 *  to the GPU, and the material contract that holds the JSON to their tables. */
namespace WorldReliefNoise
{
    /** |value| of one simplex band never exceeds this. Per corner the smoothed
     *  term is scale x (1 - 2 r^2)^3 x (g . f), with g a cube corner, so at
     *  most scale x sqrt(3) x max_r r (1 - 2 r^2)^3 = 0.7960, at r^2 = 1/14;
     *  four corners. A proof, not a sample: 200,000 samples reach 0.9997, so it
     *  is about three times loose. Height is therefore not normalised by it
     *  (the developer's ruling on planning note 3): it is FWorldRelief's
     *  guarantee argument, the most S could ever reach against SMax. */
    inline constexpr double SimplexValueBound = 3.1840869651792727;

    /** |gradient| of one simplex band never exceeds this: per corner
     *  sqrt(3) x scale x max_x (1 - 2x)^2 (1 + 10x) = 6.054, at x = r^2 = 0.1;
     *  four corners. Samples reach 5.93. */
    inline constexpr double SimplexGradientBound = 24.21582543463124;

    /** Rand3DPCG16: 16 random bits in each of X, Y, Z. */
    DEEPSPACE_API FIntVector Hash16(int32 X, int32 Y, int32 Z);

    /** Simplex noise at V and its gradient (JacobianSimplex_ALU's first row). */
    DEEPSPACE_API double Simplex(const FVector3d& V, FVector3d& OutGradient);

    /** GradientNoise3D_ALU at V, untiled. */
    DEEPSPACE_API double GradientNoise(const FVector3d& V);

    /** VoronoiNoise3D_ALU at quality 1: the nearest site (xyz) and its distance (w). */
    DEEPSPACE_API FVector4d Voronoi(const FVector3d& V);

    /** The cell noise's first channel, 0..1. */
    DEEPSPACE_API double CellHash(const FVector3d& V);

    /** Every raw term at D, for a footprint in D units, a seed offset and a
     *  stretch: the file's entry point in double, and in float -- the
     *  float build performs the GPU's operations in the GPU's precision.
     *  VertexBandLimit is what a tile's vertices carry (radius units): the
     *  slopes keep only the rest; 1.0, the default, is the orbit's. */
    DEEPSPACE_API FFaceTerms FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch,
                                     double VertexBandLimit = 1.0);
    DEEPSPACE_API FFaceTerms FaceF32(const FVector3f& D, float FootprintD, const FVector3f& Offset, float Stretch,
                                     float VertexBandLimit = 1.0f);

    /** How near D lies to a crater's step, in cells, across every crater
     *  band the footprint has not faded: the least of the rim's distance
     *  (|F1 - radius|) and the bisector's (F2 - F1). A height or slope that
     *  steps there differs by a whole step for any rounding at all.
     *
     *  Only real steps count (the developer's ruling after the spike): a
     *  rim only where the nearest site holds a crater, and a bisector only
     *  where a held site of the two lies within 1.5 radii -- the rim's
     *  reach, past which every term is 0 whichever side is taken. Where no
     *  step is real the margin is the largest double. */
    DEEPSPACE_API double CraterMargin(const FVector3d& D, double FootprintD, const FVector3d& Offset);

    /** The same margin in one crater band alone (0-based, the file's order),
     *  so a test can cap the samples each band's steps leave out (the
     *  developer's ruling at R2: 1% per crater band). */
    DEEPSPACE_API double CraterBandMargin(const FVector3d& D, double FootprintD, const FVector3d& Offset, int32 Band);

    /** The file's tables and constants, for DeepSpace.Sky.MaterialContract. */
    struct FBands
    {
        double ContinentFrequency = 0.0;
        int32 ContinentLevels = 0;
        double LevelScale = 0.0;
        TArray<double> DetailFrequencies;
        TArray<double> DetailWeights;
        TArray<int32> DetailIndices;
        TArray<double> CraterFrequencies;
        TArray<int32> CraterIndices;
        double CraterRadius = 0.0;
        double CraterInvRadius = 0.0;
        double CraterWall = 0.0;
        double CraterRimFall = 0.0;
        double CraterKeep = 0.0;
        double CraterFloorDark = 0.0;
        double CraterRimBright = 0.0;
    };
    DEEPSPACE_API FBands Bands();
}

/**
 * A world's height function (landing decision 1). Every consumer -- the
 * flight's ground query, the terrain's tiles, the HUD, the material -- reads
 * this, and the material compiles the same noise, so they agree by
 * construction. Built from FSkyBody::Relief.
 *
 * Height is PeakCm x PeakCap(S(D) / S_max): S is the detail bands' sum, each
 * band's height its value over its frequency so every scale has the same
 * slope, and S_max its *measured* maximum (SMaxMeasured), under a smooth
 * hard cap (the developer's ruling, 2026-09-27, on planning note 3). The
 * proven bound is about 4.4 times S_max, and normalising by it drew peaks at
 * a fifth of PeakCm; by the measured maximum they reach it, and the cap
 * holds every S the proof allows below PeakCm, so MaxHeightCm is still
 * exactly PeakCm, by construction. The bands
 * are the GPU's twelve and, in C++ only, finer octaves down to BandLimitCm:
 * the GPU's float cannot hold them, and nothing finer than 5 m exists, so
 * the finest tiles' 0.6 m vertices never alias it. Since slice (b) S also
 * carries Cratering x the crater bands, each a sum of compact kernels so a
 * crater is a continuous height (decision 3; the shared file's
 * WR_CraterSum). S_max stays the detail bands' measured maximum: craters
 * carve into the ground the detail raised, and the cap takes what they add
 * past it.
 *
 * Every evaluation takes a footprint, cm: the material's own fade,
 * saturate(1 - footprint x frequency) per band, made explicit. 0 is every
 * band (the flight); a tile passes its vertex spacing; the parity test the
 * probe's. D is the unit direction from the body's centre in universe axes,
 * which are the body's: worlds do not spin.
 */
class DEEPSPACE_API FWorldRelief
{
public:
    /** Nothing in the ground is finer than this, cm. */
    static constexpr double BandLimitCm = 500.0;

    /** S's measured maximum, radius units: the largest |DetailSum(D, 0)| of
     *  262,144 samples -- 256 worlds, each a seed offset of three multiples of
     *  1/256 from RandRange(0, 65535), times 1,024 directions from
     *  GetUnitVector, all from one FRandomStream(20260927), in that order --
     *  on an Earth's radius (6.3781e8 cm: the GPU's twelve bands and four
     *  finer). DeepSpace.Surface.WorldRelief.MeasuredMax re-measures it and
     *  fails if the noise has moved. The proven bound (DetailBound) is 4.43
     *  times this; one world swept densely reaches about 0.97 of it. */
    static constexpr double SMaxMeasured = 0.029932993600784347;

    /** Where PeakCap stops being the identity: S under 0.8 S_max is drawn
     *  true, and only the top of the range bends toward the peak. */
    static constexpr double PeakCapKnee = 0.8;

    /** The smooth hard cap, odd in X: X itself up to the knee, then
     *  knee + (1 - knee) tanh((|X| - knee) / (1 - knee)), which has the
     *  identity's value, slope and curvature at the knee and never passes 1.
     *  At X = 1, the measured maximum, it is 0.952. Its slope into OutSlope,
     *  0..1, if given. */
    static double PeakCap(double X, double* OutSlope = nullptr);

    explicit FWorldRelief(const FWorldReliefParams& InParams);

    const FWorldReliefParams& GetParams() const { return Params; }

    /** cm above the datum. */
    double Height(const FVector3d& D, double FootprintCm = 0.0) const;

    /** The same, and Grad = dHeight/dD in cm per unit of D, not projected
     *  onto the ground: its part along D is the caller's to drop. */
    double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm = 0.0) const;

    /** M_SkyBody's raw face terms at D, rocky (stretch 1), at this footprint. */
    FFaceTerms Face(const FVector3d& D, double FootprintCm) const;

    /** At most how far Height(D, 0) and Height(D, FootprintCm) can differ,
     *  from the bound on what each band's fade removed. */
    double OmittedBoundCm(double FootprintCm) const;

    /** PeakCm on solid ground, 0 on a world with none. */
    double MaxHeightCm() const;
    double MinHeightCm() const;

    /** A Lipschitz bound on the ground's slope, cm of height per cm along
     *  it: for the ray march (slice b). Proven -- PeakCap's slope is at most
     *  1 -- and loose (the bounds above), craters in. */
    double MaxSlope() const;

    /** The finest band's wavelength, cm: RadiusCm over its frequency. */
    double FinestWavelengthCm() const;

    /** Every detail band, cycles per radius, coarsest first. */
    TConstArrayView<double> GetDetailFrequencies() const { return Frequencies; }

    /** The detail bands' sum S at D, radius units (each band its value over
     *  its frequency), before any PeakCm scaling, at a footprint in D units
     *  (radius units); its gradient with respect to D into Grad if given.
     *  Height is PeakCm x PeakCap((DetailSum + Cratering x CraterSum) / SMax()). */
    double DetailSum(const FVector3d& D, double FootprintRadius, FVector3d* Grad = nullptr) const;

    /** S's proven bound, radius units: |DetailSum| never exceeds it. */
    double DetailBound() const { return SumBound; }

    /** What S is normalised by, radius units: SMaxMeasured. */
    double SMax() const { return SMaxMeasured; }

    /** S's Lipschitz bound with respect to D: sum of weight x SimplexGradientBound. */
    double DetailSlopeBound() const;

    /** The most a footprint (D units) can have removed from S, radius units. */
    double DetailOmittedBound(double FootprintRadius) const;

    /** The crater bands' sum at D, radius units, every kept crater whole
     *  (Cratering is the caller's), at a footprint in D units; its gradient
     *  with respect to D into Grad if given. The shared file's WR_CraterSum
     *  at the orbit's VertexBandLimit, 1: no vertices carry any of it. */
    double CraterSum(const FVector3d& D, double FootprintRadius, FVector3d* Grad = nullptr) const;

    /** The crater bands' bound, slope bound and omitted bound, radius units:
     *  WR_CRATER_BOUND_COUNT kernels at the profile's largest magnitude (or
     *  its steepest wall), per band. Proven, like the detail's. */
    static double CraterBound();
    static double CraterSlopeBound();
    static double CraterOmittedBound(double FootprintRadius);

    /** PeakCm / (RadiusCm x SMax): the band sum's gradient times this is the
     *  height's slope in radius units under the cap's knee -- what
     *  M_SkyBody's ReliefScale becomes, so the orbit's relief shading is the
     *  ground's own slope (decision 3). 0 on a world with no ground. */
    double SlopeScale() const;

private:
    double FootprintOf(double FootprintCm) const;

    /** Whether this world has a ground to draw: solid, a peak, a radius. */
    bool HasGround() const;

    /** Cratering, never below 0. */
    double Kept() const;

    FWorldReliefParams Params;
    TArray<double> Frequencies;
    TArray<double> Weights;
    TArray<int32> Indices;
    double SumBound = 0.0;
};

/** What M_SkyBody and M_SkyGround shade a rocky world with, computed by the
 *  same shared file on the CPU, for the tests that hold them equal: the raw
 *  face terms (both materials compose the face from them with one graph,
 *  surface() in setup_sky_materials.py, so equal terms are an equal face),
 *  the slope that graph forms, ReliefScale x (detail + Cratering x craters),
 *  and the unit normal. */
namespace WorldReliefShading
{
    struct FSurface
    {
        FFaceTerms Terms;
        FVector3d Slope = FVector3d::ZeroVector;
        FVector3d Normal = FVector3d::UnitZ();
    };

    /** M_SkyBody's: every slope the footprint keeps. */
    DEEPSPACE_API FSurface Orbit(const FWorldReliefParams& Params, const FVector3d& D, double FootprintRadius);

    /** M_SkyGround's: the tile's VertexNormal, and the pixel's slope of the
     *  bands the vertices at VertexBandLimit (radius units) do not carry. */
    DEEPSPACE_API FSurface Ground(const FWorldReliefParams& Params, const FVector3d& D, const FVector3d& VertexNormal,
                                  double FootprintRadius, double VertexBandLimit);
}
