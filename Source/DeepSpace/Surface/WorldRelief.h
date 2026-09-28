#pragma once

#include "CoreMinimal.h"

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
     *  the relief's slope before ds.Sky.Relief. */
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
     *  float build performs the GPU's operations in the GPU's precision. */
    DEEPSPACE_API FFaceTerms FaceF64(const FVector3d& D, double FootprintD, const FVector3d& Offset, double Stretch);
    DEEPSPACE_API FFaceTerms FaceF32(const FVector3f& D, float FootprintD, const FVector3f& Offset, float Stretch);

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
