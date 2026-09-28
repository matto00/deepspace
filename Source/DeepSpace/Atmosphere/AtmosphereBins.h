#pragma once

#include "CoreMinimal.h"
#include "Atmosphere/AtmosphereReference.h"

/**
 * The law's spectrum (atmosphere plan ruling 3): the reference's 16
 * wavelengths gathered into Count contiguous bins, the air one number per
 * bin, and each bin's light folded into linear sRGB only at the end, by the
 * star's own light in that bin. Pure: spectra in, bins out.
 * FAtmosphere::Build fills the law's air from these, and the .ush's AT_BINS
 * is Count.
 *
 * Why these bins: the air's optical depth runs as lambda^-4, so the bins
 * are narrow in the blue, where it changes fastest, and wide in the red.
 * They were chosen by measurement against the reference over decision 1's
 * grid (atmosphere optics plan, planning note 13): no partition into four,
 * five or six bins met decision 1's 5% or 1e-3 everywhere -- the best six
 * still missed a red dwarf's blue through a long horizontal CO2 path by 4.4
 * times its allowance. The first eight chosen, {0, 2, 3, 5, 6, 8, 10, 11,
 * 16}, missed .BinsCarryTheSpectrum at 2,400 K, procgen's coolest star
 * (1.012 of its allowance: CO2 at its ceiling, 88 degrees, green). Of all
 * 6,435 eight-bin partitions two pass at every star, and these are the
 * better, worst at 0.767 of the allowance across 2,400-15,000 K (atmosphere
 * plan ruling 6, the partition corrected by measurement).
 */
namespace AtmosphereBins
{
    inline constexpr int32 Count = 8;

    /** The index (SkyColour::Spectral::Nm) of each bin's first wavelength,
     *  and after the last the count of wavelengths: 400-440, 460-480, 500,
     *  520-540, 560, 580, 600 and 620-700 nm. */
    inline constexpr int32 First[Count + 1] = {0, 3, 5, 6, 8, 9, 10, 11, 16};

    /** A value per bin. */
    struct FBins
    {
        double Value[Count] = {};
    };

    /** What a wavelength counts for inside its bin: the star's light there
     *  times the length of the wavelength's linear sRGB vector
     *  (SkyColour::Spectral::ChannelWeights) -- how far it can move any
     *  channel, not only luminance, which would all but ignore the blue.
     *  Positive for every star in SkyColour's range. */
    DEEPSPACE_API double Weight(const AtmosphereReference::FSpectrum& Star, int32 Index);

    /** Value averaged over each bin by Weight: exact in the optically thin
     *  limit for anything linear in the spectrum -- scattering, an optical
     *  depth. */
    DEEPSPACE_API FBins Average(const AtmosphereReference::FSpectrum& Star, const AtmosphereReference::FSpectrum& Value);

    /** The star's light in bin Bin, in linear sRGB at the spectrum's own
     *  scale: the sum over the bin's wavelengths of the star times
     *  ChannelWeights. The Count folds sum to ToLinearSrgb(Star). A channel
     *  can be negative: the matching functions reach outside sRGB's gamut. */
    DEEPSPACE_API FVector3d Fold(const AtmosphereReference::FSpectrum& Star, int32 Bin);

    /** Light per bin, for unit light in each, folded into linear sRGB: the
     *  sum of Fold(Star, b) times Light[b]. */
    DEEPSPACE_API FVector3d FoldLight(const AtmosphereReference::FSpectrum& Star, const FBins& Light);

    /** A transmittance per bin, folded as the reference folds one
     *  (AtmosphereReference::ChannelAverage): the star's light it keeps over
     *  the star's light, channel by channel, the star's channel floored at
     *  1e-4 of its brightest. */
    DEEPSPACE_API FVector3d FoldThrough(const AtmosphereReference::FSpectrum& Star, const FBins& Through);
}
