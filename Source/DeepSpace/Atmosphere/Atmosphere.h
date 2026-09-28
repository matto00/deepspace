#pragma once

#include "CoreMinimal.h"
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
 * One world's air as the law takes it: the .ush's AT_Air, in double. Per
 * radius of the body, at the surface, three channels. Scatter is in the
 * star's own colour at unit luminance -- the star is in the coefficients,
 * so the shader never sees a temperature (decision 3); extinction is
 * relative to the star's own light, so a white surface seen through no air
 * stays white. The gas falls by e every GasH radii and the aerosol every
 * AerosolH; the air is drawn to Top radii above the surface. Airless when
 * Top is 0.
 */
struct FAtmosphereAir
{
    FVector3d GasScatter = FVector3d::ZeroVector;
    FVector3d GasExtinct = FVector3d::ZeroVector;
    FVector3d AerosolScatter = FVector3d::ZeroVector;
    FVector3d AerosolExtinct = FVector3d::ZeroVector;
    double GasH = 0.0;
    double AerosolH = 0.0;
    double AerosolG = 0.0;
    double Top = 0.0;

    bool IsAirless() const { return !(Top > 0.0); }
};

/**
 * One airy world's multiple-scattering table (decision 11): Size x Size
 * texels, row = altitude over the air's depth (0 at the surface), column =
 * the sun's zenith cosine from -1 to 1, each an RGB the GPU will sample as
 * RGBA16F -- which is why the values stored here have already been
 * through a half float. Texels[Row * Size + Column]. Empty until
 * FAtmosphere::Build fills it; an empty table reads as no multiple
 * scattering.
 */
struct DEEPSPACE_API FAtmosphereTable
{
    static constexpr int32 Size = 32;

    TArray<FVector3f> Texels;

    bool IsEmpty() const { return Texels.Num() != Size * Size; }

    /** Bilinear between texel centres, clamped at the edges: what the .ush's
     *  hook reads. Zeros when empty. */
    void Sample(double Altitude01, double CosSunZenith, double& OutR, double& OutG, double& OutB) const;
};

/** One world's air under one star, fitted for the law (decision 3). */
class DEEPSPACE_API FAtmosphere
{
public:
    /**
     * The per-channel coefficients from the star's spectrum through the
     * world's own spectral laws: scatter exact in the optically thin limit,
     * extinction exact at the nadir column (the aerosol's thin-limit share
     * on its own profile, the rest on the gas's). The star's temperature is
     * clamped as SkyColour::Blackbody clamps it.
     */
    static FAtmosphere Build(const FAirSpec& Spec, double StarTemperatureK);

    bool HasAir() const { return !Air.IsAirless(); }
    const FAtmosphereAir& GetAir() const { return Air; }

    /** The same air with the star's colour taken out of its scatter: what
     *  the multiple-scattering table is built from, so the colour enters
     *  once, where the table is read. */
    const FAtmosphereAir& GetWhiteAir() const { return White; }

    const FAtmosphereTable& GetTable() const { return Table; }

    /** The star's light in linear sRGB at unit luminance. */
    const FVector3d& GetStarColour() const { return StarColour; }

private:
    FAtmosphereAir Air;
    FAtmosphereAir White;
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
