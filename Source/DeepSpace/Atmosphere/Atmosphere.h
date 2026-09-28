#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AtmosphereBins.h"
#include "Atmosphere/AtmosphereReference.h"

/**
 * The air's optics as the game draws them (atmospheres decision 1):
 * Shaders/Private/Atmosphere.ush, the one law, compiled here twice -- in
 * double, what the game computes, and in float, the GPU's mirror, which the
 * rendered probe (orbital slice 1) holds the GPU to and the pure tests hold
 * to the double. AtmosphereReference is what "right" means; this is what
 * draws.
 */
namespace AtmosphereLaw
{
    /** ln of the Chapman function, the .ush's AT_LogChapman: X = R / H,
     *  CosZenith the ray against the local vertical. */
    DEEPSPACE_API double LogChapmanF64(double X, double CosZenith);
    DEEPSPACE_API float LogChapmanF32(float X, float CosZenith);
}

/**
 * One world's air as the law takes it: the .ush's AT_Air, in double, over
 * AtmosphereBins' Count spectral bins (atmosphere plan ruling 3). Per bin,
 * per radius of the body at the surface, for unit light in the bin: the
 * gas's and the aerosol's scattering and extinction, colourless. Fold[b] is
 * the star's light in bin b in linear sRGB at unit luminance -- the star is
 * in the fold, so the shader never sees a temperature (decision 3) -- and
 * the Count folds sum to the star's colour. The gas falls by e every GasH
 * radii and the aerosol every AerosolH; the air is drawn to Top radii above
 * the surface. Airless when Top is 0.
 */
struct FAtmosphereAir
{
    AtmosphereBins::FBins GasScatter;
    AtmosphereBins::FBins GasExtinct;
    AtmosphereBins::FBins AerosolScatter;
    AtmosphereBins::FBins AerosolExtinct;
    FVector3d Fold[AtmosphereBins::Count] = {};
    double GasH = 0.0;
    double AerosolH = 0.0;
    double AerosolG = 0.0;
    double Top = 0.0;

    bool IsAirless() const { return !(Top > 0.0); }
};

/**
 * One airy world's multiple-scattering table (decision 11): Size x Size
 * texels, row = altitude over the air's depth (0 at the surface), column =
 * the sun's zenith cosine from -1 to 1, each a value per bin
 * -- two RGBA16F texels on the GPU, a 64 x 32 texture with bins 0-3 in its
 * left half and 4-7 in its right, which is why the values stored here have
 * already been through a half float. Texels[(Row * Size + Column) * Count +
 * Bin]. Empty until FAtmosphere::Build fills it; an empty table reads as no
 * multiple scattering.
 */
struct DEEPSPACE_API FAtmosphereTable
{
    static constexpr int32 Size = 32;

    TArray<float> Texels;

    bool IsEmpty() const { return Texels.Num() != Size * Size * AtmosphereBins::Count; }

    float Texel(int32 Row, int32 Column, int32 Bin) const { return Texels[(Row * Size + Column) * AtmosphereBins::Count + Bin]; }

    /** Bilinear between texel centres, clamped at the edges: what the .ush's
     *  hook reads. Zeros when empty. */
    void Sample(double Altitude01, double CosSunZenith, double (&Out)[AtmosphereBins::Count]) const;
};

/** How much of the multiple-scattering table Build fills. NoonOnly fills
 *  the two columns either side of the noon sun's cosine -- every sample of
 *  a zenith view under that sun reads only those, so it is all the noon
 *  zenith needs and exactly what the full table holds there -- for the
 *  corpus's sky of every world; None fills nothing (single scattering). */
enum class EAtmosphereTable : uint8 { None, NoonOnly, Full };

namespace AtmosphereLaw
{
    /** The sky's reference view, what the corpus's sky_zenith_rgb and
     *  DeepSpace.Atmosphere.StarColour mean by "the noon zenith": straight
     *  up from the ground under a sun 45 degrees high. Not a sun at the
     *  zenith: the zenith would then be the star's own forward-scattered
     *  aureole, and wear the star's colour rather than the sky's. */
    inline constexpr double NoonSunElevationDeg = 45.0;

    /** That sun, the zenith being +Z: (cos 45, 0, sin 45). */
    inline FVector3d NoonSun()
    {
        const double Radians = FMath::DegreesToRadians(NoonSunElevationDeg);
        return FVector3d(FMath::Cos(Radians), 0.0, FMath::Sin(Radians));
    }
}

/** One world's air under one star, fitted for the law (decision 3). */
class DEEPSPACE_API FAtmosphere
{
public:
    /**
     * The per-bin coefficients from the star's spectrum through the world's
     * own spectral laws, each its wavelengths' average by AtmosphereBins::
     * Weight -- exact in the optically thin limit, the gas's extinction
     * carrying the ozone -- and the star's light in each bin as the fold. The star's temperature is
     * clamped as SkyColour::Blackbody clamps it. The multiple-scattering
     * table is built through the .ush's own AT_MultiScatterCell, and stored
     * through half floats.
     */
    static FAtmosphere Build(const FAirSpec& Spec, double StarTemperatureK, EAtmosphereTable Coverage = EAtmosphereTable::Full);

    bool HasAir() const { return !Air.IsAirless(); }
    const FAtmosphereAir& GetAir() const { return Air; }
    const FAtmosphereTable& GetTable() const { return Table; }

    /** The star's light in linear sRGB at unit luminance: the sum of the
     *  air's folds. */
    const FVector3d& GetStarColour() const { return StarColour; }

private:
    FAtmosphereAir Air;
    FAtmosphereTable Table;
    FVector3d StarColour = FVector3d::ZeroVector;
};

/** What one view gathers and lets through, per channel: linear sRGB in the
 *  pi convention for a star of unit luminance, and the fraction of the
 *  star's own light that survives the path. */
struct FAtmosphereScatter
{
    FVector3d InScatter = FVector3d::ZeroVector;
    FVector3d Transmittance = FVector3d::OneVector;
};

/**
 * The .ush's entry points, in double and in the GPU's float. Positions in
 * radii of the body, its centre at the origin; directions unit; Sun toward
 * the star, or the zero vector for no star. Length is how far the view goes
 * (NoEnd: to the air's top or the ground).
 */
namespace AtmosphereLaw
{
    inline constexpr int32 ViewSamples = 12;
    inline constexpr double NoEnd = 1.0e30;

    DEEPSPACE_API FAtmosphereScatter InScatterF64(const FAtmosphereAir& Air, const FAtmosphereTable& Table,
        const FVector3d& Eye, const FVector3d& Direction, double Length, const FVector3d& Sun);
    DEEPSPACE_API FAtmosphereScatter InScatterF32(const FAtmosphereAir& Air, const FAtmosphereTable& Table,
        const FVector3f& Eye, const FVector3f& Direction, float Length, const FVector3f& Sun);

    DEEPSPACE_API FVector3d TransmittanceF64(const FAtmosphereAir& Air, const FVector3d& From, const FVector3d& Direction, double Length);
    DEEPSPACE_API FVector3d TransmittanceF32(const FAtmosphereAir& Air, const FVector3f& From, const FVector3f& Direction, float Length);

    DEEPSPACE_API FVector3d SunThroughF64(const FAtmosphereAir& Air, const FVector3d& Point, const FVector3d& Sun);
    DEEPSPACE_API FVector3d SunThroughF32(const FAtmosphereAir& Air, const FVector3f& Point, const FVector3f& Sun);

    /** |Eye x Direction| as the law takes it: each component a difference
     *  of products kept to its own precision. */
    DEEPSPACE_API double ImpactParameterF64(const FVector3d& Eye, const FVector3d& Direction);
    DEEPSPACE_API float ImpactParameterF32(const FVector3f& Eye, const FVector3f& Direction);
}
