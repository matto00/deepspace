#include "Atmosphere/Atmosphere.h"

#include <cmath>
#include "Math/Float16.h"

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

namespace AtmosphereLocal
{
    /** FAtmosphereAir as one precision's AT_Air. */
    template <typename TAir, typename TReal>
    TAir ToAir(const FAtmosphereAir& In)
    {
        TAir A;
        A.GasScatterR = TReal(In.GasScatter.X);
        A.GasScatterG = TReal(In.GasScatter.Y);
        A.GasScatterB = TReal(In.GasScatter.Z);
        A.GasExtinctR = TReal(In.GasExtinct.X);
        A.GasExtinctG = TReal(In.GasExtinct.Y);
        A.GasExtinctB = TReal(In.GasExtinct.Z);
        A.AerosolScatterR = TReal(In.AerosolScatter.X);
        A.AerosolScatterG = TReal(In.AerosolScatter.Y);
        A.AerosolScatterB = TReal(In.AerosolScatter.Z);
        A.AerosolExtinctR = TReal(In.AerosolExtinct.X);
        A.AerosolExtinctG = TReal(In.AerosolExtinct.Y);
        A.AerosolExtinctB = TReal(In.AerosolExtinct.Z);
        A.GasH = TReal(In.GasH);
        A.AerosolH = TReal(In.AerosolH);
        A.AerosolG = TReal(In.AerosolG);
        A.Top = TReal(In.Top);
        return A;
    }

    template <typename TScatter>
    FAtmosphereScatter ToScatter(const TScatter& S)
    {
        FAtmosphereScatter Out;
        Out.InScatter = FVector3d(S.R, S.G, S.B);
        Out.Transmittance = FVector3d(S.TR, S.TG, S.TB);
        return Out;
    }

    template <typename TRgb>
    FVector3d ToVector(const TRgb& C)
    {
        return FVector3d(C.R, C.G, C.B);
    }
}

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

FAtmosphere FAtmosphere::Build(const FAirSpec& Spec, double StarTemperatureK, EAtmosphereTable Coverage)
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

    if (Coverage != EAtmosphereTable::None)
    {
        // Each texel from the .ush's own AT_MultiScatterCell in double, so
        // the table is the law's (decision 11), read while it is built from
        // an empty table -- single scattering only -- and stored through
        // half floats, as the GPU's RGBA16F texture will hold it.
        constexpr int32 Size = FAtmosphereTable::Size;
        const AtmosphereF64::AT_Air WhiteAir = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Out.White);
        const FAtmosphereTable Empty;
        Out.Table.Texels.SetNumZeroed(Size * Size);
        // NoonOnly: the two columns either side of the noon sun's cosine,
        // which every sample of a zenith view under that sun reads.
        const double NoonCos = AtmosphereLaw::NoonSun().Z;
        const int32 NoonColumn = FMath::Min(FMath::FloorToInt32((NoonCos + 1.0) * 0.5 * (Size - 1)), Size - 2);
        const int32 FirstColumn = Coverage == EAtmosphereTable::Full ? 0 : NoonColumn;
        const int32 LastColumn = Coverage == EAtmosphereTable::Full ? Size - 1 : NoonColumn + 1;
        for (int32 Row = 0; Row < Size; ++Row)
        {
            for (int32 Column = FirstColumn; Column <= LastColumn; ++Column)
            {
                const double Altitude01 = static_cast<double>(Row) / (Size - 1);
                const double Cos = -1.0 + 2.0 * Column / (Size - 1);
                const AtmosphereF64::AT_Rgb Cell = AtmosphereF64::AT_MultiScatterCell(WhiteAir, Altitude01, Cos, Empty);
                Out.Table.Texels[Row * Size + Column] = FVector3f(
                    FFloat16(static_cast<float>(Cell.R)).GetFloat(),
                    FFloat16(static_cast<float>(Cell.G)).GetFloat(),
                    FFloat16(static_cast<float>(Cell.B)).GetFloat());
            }
        }
    }
    return Out;
}

FAtmosphereScatter AtmosphereLaw::InScatterF64(const FAtmosphereAir& Air, const FAtmosphereTable& Table,
    const FVector3d& Eye, const FVector3d& Direction, double Length, const FVector3d& Sun)
{
    const AtmosphereF64::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Air);
    return AtmosphereLocal::ToScatter(AtmosphereF64::AT_InScatter(A, Eye.X, Eye.Y, Eye.Z, Direction.X, Direction.Y, Direction.Z,
        Length, Sun.X, Sun.Y, Sun.Z, Table));
}

FAtmosphereScatter AtmosphereLaw::InScatterF32(const FAtmosphereAir& Air, const FAtmosphereTable& Table,
    const FVector3f& Eye, const FVector3f& Direction, float Length, const FVector3f& Sun)
{
    const AtmosphereF32::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF32::AT_Air, float>(Air);
    return AtmosphereLocal::ToScatter(AtmosphereF32::AT_InScatter(A, Eye.X, Eye.Y, Eye.Z, Direction.X, Direction.Y, Direction.Z,
        Length, Sun.X, Sun.Y, Sun.Z, Table));
}

FVector3d AtmosphereLaw::TransmittanceF64(const FAtmosphereAir& Air, const FVector3d& From, const FVector3d& Direction, double Length)
{
    const AtmosphereF64::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Air);
    return AtmosphereLocal::ToVector(AtmosphereF64::AT_Transmittance(A, From.X, From.Y, From.Z, Direction.X, Direction.Y, Direction.Z, Length));
}

FVector3d AtmosphereLaw::TransmittanceF32(const FAtmosphereAir& Air, const FVector3f& From, const FVector3f& Direction, float Length)
{
    const AtmosphereF32::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF32::AT_Air, float>(Air);
    return AtmosphereLocal::ToVector(AtmosphereF32::AT_Transmittance(A, From.X, From.Y, From.Z, Direction.X, Direction.Y, Direction.Z, Length));
}

FVector3d AtmosphereLaw::SunThroughF64(const FAtmosphereAir& Air, const FVector3d& Point, const FVector3d& Sun)
{
    const AtmosphereF64::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Air);
    return AtmosphereLocal::ToVector(AtmosphereF64::AT_SunThrough(A, Point.X, Point.Y, Point.Z, Sun.X, Sun.Y, Sun.Z));
}

FVector3d AtmosphereLaw::SunThroughF32(const FAtmosphereAir& Air, const FVector3f& Point, const FVector3f& Sun)
{
    const AtmosphereF32::AT_Air A = AtmosphereLocal::ToAir<AtmosphereF32::AT_Air, float>(Air);
    return AtmosphereLocal::ToVector(AtmosphereF32::AT_SunThrough(A, Point.X, Point.Y, Point.Z, Sun.X, Sun.Y, Sun.Z));
}

double AtmosphereLaw::ImpactParameterF64(const FVector3d& Eye, const FVector3d& Direction)
{
    using namespace AtmosphereF64;
    const double WX = AT_DiffOfProducts(Eye.Y, Direction.Z, Eye.Z, Direction.Y);
    const double WY = AT_DiffOfProducts(Eye.Z, Direction.X, Eye.X, Direction.Z);
    const double WZ = AT_DiffOfProducts(Eye.X, Direction.Y, Eye.Y, Direction.X);
    return std::sqrt(WX * WX + WY * WY + WZ * WZ);
}

float AtmosphereLaw::ImpactParameterF32(const FVector3f& Eye, const FVector3f& Direction)
{
    using namespace AtmosphereF32;
    const float WX = AT_DiffOfProducts(Eye.Y, Direction.Z, Eye.Z, Direction.Y);
    const float WY = AT_DiffOfProducts(Eye.Z, Direction.X, Eye.X, Direction.Z);
    const float WZ = AT_DiffOfProducts(Eye.X, Direction.Y, Eye.Y, Direction.X);
    return std::sqrt(WX * WX + WY * WY + WZ * WZ);
}
