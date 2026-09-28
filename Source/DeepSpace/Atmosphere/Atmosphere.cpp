#include "Atmosphere/Atmosphere.h"

#include <cmath>

// The shared file, twice. AT_CPP selects its C++ halves and AT_REAL its
// precision; each copy lives in its own namespace, so the two sets of AT_
// symbols never meet. The relative path is the file the GPU will include as
// /Project/Private/Atmosphere.ush: a change to it that C++ cannot compile
// fails ./build.sh.
#define AT_CPP 1
namespace AtmosphereF64
{
#define AT_REAL double
#include "../../../Shaders/Private/Atmosphere.ush"
#undef AT_REAL
}
namespace AtmosphereF32
{
#define AT_REAL float
#include "../../../Shaders/Private/Atmosphere.ush"
#undef AT_REAL
}
#undef AT_CPP

double AtmosphereLaw::LogChapmanF64(double X, double CosZenith)
{
    return AtmosphereF64::AT_LogChapman(X, CosZenith);
}

float AtmosphereLaw::LogChapmanF32(float X, float CosZenith)
{
    return AtmosphereF32::AT_LogChapman(X, CosZenith);
}


void FAtmosphereTable::Sample(double Altitude01, double CosSunZenith, double& OutR, double& OutG, double& OutB) const
{
    if (IsEmpty())
    {
        OutR = 0.0;
        OutG = 0.0;
        OutB = 0.0;
        return;
    }
    const double FX = FMath::Clamp((CosSunZenith + 1.0) * 0.5, 0.0, 1.0) * (Size - 1);
    const double FY = FMath::Clamp(Altitude01, 0.0, 1.0) * (Size - 1);
    const int32 X0 = FMath::Min(FMath::FloorToInt32(FX), Size - 2);
    const int32 Y0 = FMath::Min(FMath::FloorToInt32(FY), Size - 2);
    const double TX = FX - X0;
    const double TY = FY - Y0;
    const FVector3f& C00 = Texels[Y0 * Size + X0];
    const FVector3f& C01 = Texels[Y0 * Size + X0 + 1];
    const FVector3f& C10 = Texels[(Y0 + 1) * Size + X0];
    const FVector3f& C11 = Texels[(Y0 + 1) * Size + X0 + 1];
    const auto Blend = [TX, TY](double A, double B, double C, double D) { return FMath::Lerp(FMath::Lerp(A, B, TX), FMath::Lerp(C, D, TX), TY); };
    OutR = Blend(C00.X, C01.X, C10.X, C11.X);
    OutG = Blend(C00.Y, C01.Y, C10.Y, C11.Y);
    OutB = Blend(C00.Z, C01.Z, C10.Z, C11.Z);
}

FAtmosphere FAtmosphere::Build(const FAirSpec& Spec, double StarTemperatureK)
{
    using namespace AtmosphereReference;
    FAtmosphere Out;
    const FSpectrum Star = StarSpectrum(StarTemperatureK);
    Out.StarColour = ToLinearSrgb(Star);
    const FSpectralAir Spectra = Spectral(Spec);
    if (!Spectra.bAir)
    {
        return Out;
    }

    FSpectrum Through;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        Through.Value[I] = FMath::Exp(-(Spectra.GasScatter.Value[I] + Spectra.GasAbsorb.Value[I] + Spectra.AerosolExtinct.Value[I]));
    }
    const FVector3d Nadir = ChannelAverage(Star, Through);
    const FVector3d AerosolTau = ChannelAverage(Star, Spectra.AerosolExtinct);
    const FVector3d GasScatterColour = Colour(Star, Spectra.GasScatter);
    const FVector3d AerosolScatterColour = Colour(Star, Spectra.AerosolScatter);
    const FVector3d GasScatterWhite = ChannelAverage(Star, Spectra.GasScatter);
    const FVector3d AerosolScatterWhite = ChannelAverage(Star, Spectra.AerosolScatter);

    FAtmosphereAir& A = Out.Air;
    A.GasH = Spectra.GasH;
    A.AerosolH = Spectra.AerosolH;
    A.AerosolG = Spectra.AerosolG;
    A.Top = Spectra.Top;
    for (int32 C = 0; C < 3; ++C)
    {
        const double TotalTau = -FMath::Loge(FMath::Clamp(Nadir[C], 1.0e-6, 1.0));
        const double Aerosol = FMath::Max(AerosolTau[C], 0.0);
        A.GasExtinct[C] = FMath::Max(TotalTau - Aerosol, 0.0) / Spectra.GasH;
        A.AerosolExtinct[C] = Aerosol / Spectra.AerosolH;
        A.GasScatter[C] = FMath::Max(GasScatterColour[C], 0.0) / Spectra.GasH;
        A.AerosolScatter[C] = FMath::Max(AerosolScatterColour[C], 0.0) / Spectra.AerosolH;
    }
    Out.White = A;
    for (int32 C = 0; C < 3; ++C)
    {
        Out.White.GasScatter[C] = FMath::Max(GasScatterWhite[C], 0.0) / Spectra.GasH;
        Out.White.AerosolScatter[C] = FMath::Max(AerosolScatterWhite[C], 0.0) / Spectra.AerosolH;
    }
    return Out;
}
