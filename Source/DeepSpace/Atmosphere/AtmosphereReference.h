#pragma once

#include "CoreMinimal.h"
#include "Sky/SkyColour.h"

/**
 * One world's air as physics describes it: what the optics are built from.
 * Plain data, in centimetres and nadir optical depths. Procgen's facts
 * reach it through one adapter (PlanetAir::SpecOf, once the draws exist);
 * the tests write their airs out by hand. Airless when HasAir() is false.
 */
struct DEEPSPACE_API FAirSpec
{
    /** The body's radius, cm: every length the optics use is in these. */
    double RadiusCm = 0.0;

    /** The gas (Rayleigh) falls by e every this many cm. 0 is airless. */
    double GasScaleHeightCm = 0.0;

    /** Straight down through the whole gas, at 550 nm; (550 / lambda)^4
     *  elsewhere. */
    double GasTau550 = 0.0;

    /** The ozone-like absorber straight down at its Chappuis peak, 600 nm,
     *  shaped as a Gaussian 70 nm wide (sigma), carried on the gas's
     *  profile. 0 is none. */
    double OzoneTau600 = 0.0;

    /** The aerosol (Mie): its scale height, and its extinction straight
     *  down at 550 nm, going as (lambda / 550)^-Angstrom. */
    double AerosolScaleHeightCm = 0.0;
    double AerosolTau550 = 0.0;
    double AerosolAngstrom = 1.0;

    /** The aerosol's Henyey-Greenstein asymmetry g, forward-scattering
     *  above 0. */
    double AerosolAsymmetry = 0.0;

    /** Its single-scatter albedo at 450 and 650 nm, linear between and held
     *  beyond: CO2's iron-oxide dust absorbs more blue than red. */
    double AerosolAlbedo450 = 1.0;
    double AerosolAlbedo650 = 1.0;

    bool HasAir() const
    {
        return RadiusCm > 0.0 && GasScaleHeightCm > 0.0 && (GasTau550 > 0.0 || OzoneTau600 > 0.0 || AerosolTau550 > 0.0);
    }
};

/**
 * What "right" means for the air (atmospheres decision 1): double
 * precision, 16 wavelengths, exact columns by numerical integration, second
 * order by brute force. Slow, and never run in a frame. The only place on
 * the sky's side physics constants live -- the air's Rayleigh
 * cross-section, the ozone band, the gas's and the aerosol's spectral laws
 * -- so the law's channel fit (FAtmosphere::Build) takes its spectra from
 * here.
 *
 * Lengths in radii of the body (the surface at R = 1); radiance in the pi
 * convention of M_SkyBody's Lambert face, for a star of unit luminance.
 */
namespace AtmosphereReference
{
    /** The air's drawn top: ten of the gas's scale heights, where its
     *  density is e^-10 of the surface's. */
    inline constexpr double AirTopScaleHeights = 10.0;

    inline constexpr double OzonePeakNm = 600.0;
    inline constexpr double OzoneWidthNm = 70.0;

    /** A value at each of SkyColour::Spectral's 16 wavelengths. */
    struct FSpectrum
    {
        double Value[SkyColour::Spectral::Count] = {};
    };

    /** An air per wavelength: nadir optical depths, heights in radii. */
    struct FSpectralAir
    {
        FSpectrum GasScatter;
        FSpectrum GasAbsorb;
        FSpectrum AerosolExtinct;
        FSpectrum AerosolScatter;
        double GasH = 0.0;
        double AerosolH = 0.0;
        double AerosolG = 0.0;
        double Top = 0.0;
        bool bAir = false;
    };

    /** The spec's spectral laws applied to an air. An air with no aerosol
     *  is still given a profile for it -- the gas's -- so nothing downstream
     *  divides by a zero height. */
    DEEPSPACE_API FSpectralAir Spectral(const FAirSpec& Spec);

    /** A blackbody at the 16 wavelengths, scaled to unit luminance (CIE Y),
     *  its temperature clamped as SkyColour::Blackbody clamps it. */
    DEEPSPACE_API FSpectrum StarSpectrum(double TemperatureK);

    /** SkyColour::Spectral::ToLinearSrgb of a spectrum. */
    DEEPSPACE_API FVector3d ToLinearSrgb(const FSpectrum& Spectrum);

    /** The linear sRGB of the star's light times Value, wavelength by
     *  wavelength. */
    DEEPSPACE_API FVector3d Colour(const FSpectrum& Star, const FSpectrum& Value);

    /** Colour(Star, Value) over the star's own colour, channel by channel:
     *  what a three-channel renderer must multiply the star's light by to
     *  get Value's effect on it. The star's channel is floored at 1e-4 of
     *  its brightest, so a 1,000 K star's near-absent blue cannot divide
     *  anything into infinity. */
    DEEPSPACE_API FVector3d ChannelAverage(const FSpectrum& Star, const FSpectrum& Value);

    /** Rayleigh scattering cross-section of Earth's air per molecule, m^2:
     *  24 pi^3 / (lambda^4 N_s^2) ((n^2 - 1) / (n^2 + 2))^2 F_K, with Peck
     *  and Reeder's refractivity, Bates's King factors, and Loschmidt's
     *  number at 288.15 K. */
    DEEPSPACE_API double RayleighCrossSectionAirM2(double Nm);

    /** Molecules per m^2 above a surface: P / (mu m_u g). */
    DEEPSPACE_API double ColumnMoleculesPerM2(double PressureBar, double GravityMS2, double MeanMolecularWeight);

    /**
     * The column along a ray from a point R radii from the centre toward
     * CosZenith, per unit density at the surface, for a constituent falling
     * by e every H radii, in radii; by Simpson's rule on Intervals steps in a
     * variable crowded where the density peaks, out to 80 scale heights above
     * the ray's lowest point. -1 when the ray meets the ground first.
     */
    DEEPSPACE_API double ColumnExact(double R, double CosZenith, double H, int32 Intervals = 2000);
}

/** One air under one star, ready to be traced. Building it tabulates the
 *  exact columns once, for the second order's many sun paths. */
class DEEPSPACE_API FReferenceAir
{
public:
    struct FRay
    {
        FVector3d Eye = FVector3d(0.0, 0.0, 1.0);
        FVector3d Direction = FVector3d(0.0, 0.0, 1.0);
        double Length = 1.0e30;
        /** Unit, toward the star; the zero vector for no star. */
        FVector3d Sun = FVector3d(0.0, 0.0, 1.0);
    };

    /** The second order's sphere is SphereRings rings, equal steps in T
     *  with cos(zenith) = T |T| -- crowded at the horizon, where the long
     *  paths and most of the light are -- by SphereSegments of azimuth. */
    struct FOptions
    {
        int32 ViewSteps = 256;
        bool bSecondOrder = true;
        int32 SecondOrderViewSteps = 24;
        int32 SphereRings = 24;
        int32 SphereSegments = 12;
        int32 SecondarySteps = 32;
    };

    struct FResult
    {
        /** Linear sRGB, pi convention, a star of unit luminance. */
        FVector3d InScatter = FVector3d::ZeroVector;
        /** Per channel, relative to the star's own light. */
        FVector3d Transmittance = FVector3d::OneVector;
    };

    FReferenceAir(const FAirSpec& Spec, double StarTemperatureK);

    bool HasAir() const { return Air.bAir; }
    const AtmosphereReference::FSpectralAir& GetAir() const { return Air; }

    /** The light the air adds along the ray, first and second order and the
     *  geometric tail beyond, and what it lets through. Two overloads rather
     *  than a defaulted FOptions: a nested struct's member initializers are
     *  not usable in a default argument inside its own class. */
    FResult Trace(const FRay& Ray) const;
    FResult Trace(const FRay& Ray, const FOptions& Options) const;

    /** The star's light reaching Point, per channel, relative to its own:
     *  exact columns, and 0 in the ground's shadow. An airless world passes
     *  all of it; no star passes none. */
    FVector3d SunThrough(const FVector3d& Point, const FVector3d& Sun) const;

    /**
     * The reference's multiple-scattering source at Altitude01 of the air's
     * depth under a sun at CosSunZenith, for a sun of unit light at every
     * wavelength: pi times the mean first-order radiance over the sphere,
     * over 1 - f, weighted into channels by the star's spectrum
     * (ChannelAverage). What the law's table texel approximates.
     */
    FVector3d MultiScatterWhite(double Altitude01, double CosSunZenith, int32 Rings = 48, int32 Segments = 24, int32 Steps = 96) const;

    /** The same source per wavelength, before any weighting into channels:
     *  what the law's table is held to, bin by bin. */
    AtmosphereReference::FSpectrum MultiScatterSpectrum(double Altitude01, double CosSunZenith, int32 Rings = 48, int32 Segments = 24, int32 Steps = 96) const;

private:
    static constexpr int32 TableAltitudes = 64;
    static constexpr int32 TableCosines = 128;

    struct FSecond
    {
        AtmosphereReference::FSpectrum IntoViewGas;
        AtmosphereReference::FSpectrum IntoViewAerosol;
        AtmosphereReference::FSpectrum Mean;
        AtmosphereReference::FSpectrum Transfer;
    };

    AtmosphereReference::FSpectralAir Air;
    AtmosphereReference::FSpectrum Star;
    TArray<double> LogGasColumn;
    TArray<double> LogAerosolColumn;

    double LogColumnLookup(const TArray<double>& Table, double R, double Cos, double H) const;
    AtmosphereReference::FSpectrum SunExact(double R, double Cos) const;
    AtmosphereReference::FSpectrum SunLookup(double R, double Cos) const;
    FSecond SecondOrderAt(const FVector3d& Point, const FVector3d& View, const FVector3d& Sun, int32 Rings, int32 Segments, int32 Steps) const;
};
