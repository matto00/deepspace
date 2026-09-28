#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Templates/Function.h"

/**
 * The one blackbody in the project (plan conflict 6). A star's colour is a
 * presentation concern, so it lives on the sky's side of the seam and
 * procgen's subsystem has no StarColour of its own.
 *
 * The sky's air integrates the same blackbody through its own scattering
 * (atmospheres decision 3), so the spectrum it integrates over lives here
 * too: one Planck function, one set of matching functions, one sRGB matrix.
 */
namespace SkyColour
{
    /**
     * The colour of a blackbody at this temperature, in linear sRGB, scaled
     * so its brightest channel is 1: a chromaticity, never a brightness. How
     * bright a star is comes from its luminosity and distance, and mixing the
     * two here would make a hot star brighter twice.
     *
     * Hot stars are blue-white and cool ones orange-red, against a D65 white,
     * so a Sun-like star reads faintly warm -- as the Sun does in a photograph
     * white-balanced for daylight. Clamped to the 1,000-15,000 K the
     * approximation covers; nothing drawn here is cooler, and above 15,000 K
     * the colour has stopped changing to the eye.
     */
    DEEPSPACE_API FLinearColor Blackbody(double TemperatureK);

    /**
     * Linear sRGB (Rec. 709 primaries, D65 white, the convention Blackbody
     * already uses) of a TemperatureK blackbody seen through Filter(lambda),
     * integrated against the CIE 1931 colour-matching functions at the 16
     * wavelengths of Spectral (atmospheres decision 3). Not normalised: a
     * brightness, in W sr^-1 m^-2 weighted by the matching functions. The
     * temperature is clamped as Blackbody clamps it. A channel can come back
     * negative for a light outside the sRGB gamut; clamping it is the
     * caller's choice, not this function's.
     */
    DEEPSPACE_API FLinearColor ThroughFilter(double TemperatureK, TFunctionRef<double(double Nm)> Filter);

    /**
     * The spectrum the sky's colour is integrated over: 16 samples, 400 to
     * 700 nm, 20 nm apart. Coarse, and deliberately so: the air's optical
     * depths vary smoothly across it, the reference integrates on the same
     * samples, and DeepSpace.Sky.OneBlackbody holds the result to the
     * engine's Planckian-locus fit.
     */
    namespace Spectral
    {
        inline constexpr int32 Count = 16;
        inline constexpr double FirstNm = 400.0;
        inline constexpr double StepNm = 20.0;
        inline constexpr double MinTemperatureK = 1000.0;
        inline constexpr double MaxTemperatureK = 15000.0;

        /** The wavelength of sample Index, nm: 400, 420, ..., 700. */
        constexpr double Nm(int32 Index) { return FirstNm + StepNm * Index; }

        /** Planck's spectral radiance, W sr^-1 m^-2 nm^-1, at a temperature
         *  clamped to [MinTemperatureK, MaxTemperatureK]. */
        DEEPSPACE_API double Planck(double Nm, double TemperatureK);

        /** The linear sRGB that one unit of spectral radiance at sample
         *  Index adds over its 20 nm: the XYZ-to-sRGB matrix applied to the
         *  matching functions there, times the step. Channels can be
         *  negative: the matching functions reach outside the sRGB gamut. */
        DEEPSPACE_API FVector3d ChannelWeights(int32 Index);

        /** What that unit adds to luminance, CIE Y: y-bar times the step. */
        DEEPSPACE_API double Luminance(int32 Index);

        /** The sum over the samples of Spectrum[i] ChannelWeights(i).
         *  Spectrum holds Count values. */
        DEEPSPACE_API FVector3d ToLinearSrgb(TConstArrayView<double> Spectrum);
    }
}
