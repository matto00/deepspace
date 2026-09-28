#include "Sky/SkyColour.h"

#include <cmath>

namespace SkyColourLocal
{
    using SkyColour::Spectral::Count;

    // CIE 1931 2-degree standard observer at 400, 420, ..., 700 nm.
    constexpr double CieX[Count] = {0.01431, 0.13438, 0.34828, 0.29080, 0.09564, 0.00490, 0.06327, 0.29040,
                                    0.59450, 0.91630, 1.06220, 0.85445, 0.44790, 0.16490, 0.04677, 0.01136};
    constexpr double CieY[Count] = {0.000396, 0.004000, 0.023000, 0.060000, 0.139020, 0.323000, 0.710000, 0.954000,
                                    0.995000, 0.870000, 0.631000, 0.381000, 0.175000, 0.061000, 0.017000, 0.004102};
    constexpr double CieZ[Count] = {0.06785, 0.64560, 1.74706, 1.66920, 0.81295, 0.27200, 0.07825, 0.02030,
                                    0.00390, 0.00165, 0.00080, 0.00019, 0.00002, 0.0, 0.0, 0.0};

    /** XYZ to linear sRGB, BT.709 primaries and D65: the matrix
     *  FLinearColor::MakeFromColorTemperature uses, so the fit and the
     *  integral land in one space. */
    FVector3d XyzToLinearSrgb(double X, double Y, double Z)
    {
        return FVector3d(
             3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z,
            -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z,
             0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z);
    }
}

FLinearColor SkyColour::Blackbody(double TemperatureK)
{
    // The engine's Planckian-locus fit (Krystek), which returns linear sRGB
    // at unit luminance. Wrapped rather than rewritten: one fit, maintained
    // by somebody else, and this function is the only place the project
    // asks for it. DeepSpace.Sky.OneBlackbody holds it to ThroughFilter.
    const float Clamped = static_cast<float>(FMath::Clamp(TemperatureK, Spectral::MinTemperatureK, Spectral::MaxTemperatureK));
    FLinearColor Colour = FLinearColor::MakeFromColorTemperature(Clamped);

    // Out of gamut below about 1,900 K the fit dips a channel negative;
    // no display can show less than none of a primary.
    Colour.R = FMath::Max(Colour.R, 0.0f);
    Colour.G = FMath::Max(Colour.G, 0.0f);
    Colour.B = FMath::Max(Colour.B, 0.0f);

    const float Peak = FMath::Max3(Colour.R, Colour.G, Colour.B);
    if (Peak > 0.0f)
    {
        Colour.R /= Peak;
        Colour.G /= Peak;
        Colour.B /= Peak;
    }
    Colour.A = 1.0f;
    return Colour;
}

double SkyColour::Spectral::Planck(double Nm, double TemperatureK)
{
    // 2 h c^2 / lambda^5 / (e^(h c / lambda k T) - 1), per metre of
    // wavelength, then per nanometre. expm1 keeps the short-wave tail of a
    // cool star, where the exponent is large, and the long-wave one of a hot
    // star, where it is small.
    constexpr double TwoHC2 = 2.0 * 6.62607015e-34 * 299792458.0 * 299792458.0;
    constexpr double SecondRadiation = 1.438776877e-2;   // h c / k, m K
    const double T = FMath::Clamp(TemperatureK, MinTemperatureK, MaxTemperatureK);
    const double Metres = Nm * 1.0e-9;
    return TwoHC2 / std::pow(Metres, 5.0) / std::expm1(SecondRadiation / (Metres * T)) * 1.0e-9;
}

FVector3d SkyColour::Spectral::ChannelWeights(int32 Index)
{
    using namespace SkyColourLocal;
    check(Index >= 0 && Index < Count);
    return XyzToLinearSrgb(CieX[Index], CieY[Index], CieZ[Index]) * StepNm;
}

double SkyColour::Spectral::Luminance(int32 Index)
{
    using namespace SkyColourLocal;
    check(Index >= 0 && Index < Count);
    return CieY[Index] * StepNm;
}

FVector3d SkyColour::Spectral::ToLinearSrgb(TConstArrayView<double> Spectrum)
{
    check(Spectrum.Num() == Count);
    FVector3d Sum = FVector3d::ZeroVector;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        Sum += ChannelWeights(Index) * Spectrum[Index];
    }
    return Sum;
}

FLinearColor SkyColour::ThroughFilter(double TemperatureK, TFunctionRef<double(double Nm)> Filter)
{
    double Spectrum[Spectral::Count];
    for (int32 Index = 0; Index < Spectral::Count; ++Index)
    {
        const double Nm = Spectral::Nm(Index);
        Spectrum[Index] = Spectral::Planck(Nm, TemperatureK) * Filter(Nm);
    }
    const FVector3d Rgb = Spectral::ToLinearSrgb(MakeArrayView(Spectrum, Spectral::Count));
    return FLinearColor(static_cast<float>(Rgb.X), static_cast<float>(Rgb.Y), static_cast<float>(Rgb.Z), 1.0f);
}
