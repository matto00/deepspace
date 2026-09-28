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

static_assert(AtmosphereF64::AT_BINS == AtmosphereBins::Count && AtmosphereF32::AT_BINS == AtmosphereBins::Count,
    "Atmosphere.ush's AT_BINS is AtmosphereBins::Count");

namespace AtmosphereLocal
{
    /** FAtmosphereAir as one precision's AT_Air. */
    template <typename TAir, typename TReal>
    TAir ToAir(const FAtmosphereAir& In)
    {
        TAir A;
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            A.GasScatter[K] = TReal(In.GasScatter.Value[K]);
            A.GasExtinct[K] = TReal(In.GasExtinct.Value[K]);
            A.AerosolScatter[K] = TReal(In.AerosolScatter.Value[K]);
            A.AerosolExtinct[K] = TReal(In.AerosolExtinct.Value[K]);
            A.FoldR[K] = TReal(In.Fold[K].X);
            A.FoldG[K] = TReal(In.Fold[K].Y);
            A.FoldB[K] = TReal(In.Fold[K].Z);
        }
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


double FAtmosphereTable::ColumnOf(double CosSunZenith)
{
    const double Cos = FMath::Clamp(CosSunZenith, -1.0, 1.0);
    const double T = Cos < 0.0 ? -std::sqrt(-Cos) : std::sqrt(Cos);
    return (T + 1.0) * 0.5 * (Size - 1);
}

double FAtmosphereTable::CosOfColumn(int32 Column)
{
    const double T = -1.0 + 2.0 * Column / (Size - 1);
    return T * std::fabs(T);
}

void FAtmosphereTable::Sample(double Altitude01, double CosSunZenith, double (&Out)[AtmosphereBins::Count]) const
{
    if (IsEmpty())
    {
        for (int32 K = 0; K < AtmosphereBins::Count; ++K)
        {
            Out[K] = 0.0;
        }
        return;
    }
    const double FX = ColumnOf(CosSunZenith);
    const double FY = FMath::Clamp(Altitude01, 0.0, 1.0) * (Size - 1);
    const int32 X0 = FMath::Min(FMath::FloorToInt32(FX), Size - 2);
    const int32 Y0 = FMath::Min(FMath::FloorToInt32(FY), Size - 2);
    const double TX = FX - X0;
    const double TY = FY - Y0;
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        Out[K] = FMath::Lerp(FMath::Lerp(Texel(Y0, X0, K), Texel(Y0, X0 + 1, K), TX),
                             FMath::Lerp(Texel(Y0 + 1, X0, K), Texel(Y0 + 1, X0 + 1, K), TX), TY);
    }
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

    // Each bin's air is its wavelengths' own, averaged by AtmosphereBins::
    // Weight. Not exact even thin: the weight is a scalar and the fold is per
    // channel (AtmosphereBins::Average). Deep, one number also stands for a
    // spread of depths. The bins are narrow enough that both hold to the
    // agreement grid's tolerance, thin airs included (planning note 13).
    FSpectrum GasExtinct;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        GasExtinct.Value[I] = Spectra.GasScatter.Value[I] + Spectra.GasAbsorb.Value[I];
    }
    const AtmosphereBins::FBins GasScatter = AtmosphereBins::Average(Star, Spectra.GasScatter);
    const AtmosphereBins::FBins GasTau = AtmosphereBins::Average(Star, GasExtinct);
    const AtmosphereBins::FBins AerosolScatter = AtmosphereBins::Average(Star, Spectra.AerosolScatter);
    const AtmosphereBins::FBins AerosolTau = AtmosphereBins::Average(Star, Spectra.AerosolExtinct);

    FAtmosphereAir& A = Out.Air;
    A.GasH = Spectra.GasH;
    A.AerosolH = Spectra.AerosolH;
    A.AerosolG = Spectra.AerosolG;
    A.Top = Spectra.Top;
    for (int32 K = 0; K < AtmosphereBins::Count; ++K)
    {
        A.GasScatter.Value[K] = GasScatter.Value[K] / Spectra.GasH;
        A.GasExtinct.Value[K] = GasTau.Value[K] / Spectra.GasH;
        A.AerosolScatter.Value[K] = AerosolScatter.Value[K] / Spectra.AerosolH;
        A.AerosolExtinct.Value[K] = AerosolTau.Value[K] / Spectra.AerosolH;
        A.Fold[K] = AtmosphereBins::Fold(Star, K);
    }

    if (Coverage != EAtmosphereTable::None)
    {
        // Each texel from the .ush's own AT_MultiScatterCell in double, so
        // the table is the law's (decision 11), read while it is built from
        // an empty table -- single scattering only -- and stored through
        // half floats, as the GPU's RGBA16F texture will hold it.
        constexpr int32 Size = FAtmosphereTable::Size;
        const AtmosphereF64::AT_Air Air64 = AtmosphereLocal::ToAir<AtmosphereF64::AT_Air, double>(Out.Air);
        const FAtmosphereTable Empty;
        Out.Table.Texels.SetNumZeroed(Size * Size * AtmosphereBins::Count);
        // NoonOnly: the two columns either side of the noon sun's, which
        // every sample of a zenith view under that sun reads.
        const int32 NoonColumn = FMath::Min(FMath::FloorToInt32(FAtmosphereTable::ColumnOf(AtmosphereLaw::NoonSun().Z)), Size - 2);
        const int32 FirstColumn = Coverage == EAtmosphereTable::Full ? 0 : NoonColumn;
        const int32 LastColumn = Coverage == EAtmosphereTable::Full ? Size - 1 : NoonColumn + 1;
        for (int32 Row = 0; Row < Size; ++Row)
        {
            for (int32 Column = FirstColumn; Column <= LastColumn; ++Column)
            {
                const double Altitude01 = static_cast<double>(Row) / (Size - 1);
                const AtmosphereF64::AT_Bins Cell = AtmosphereF64::AT_MultiScatterCell(Air64, Altitude01, FAtmosphereTable::CosOfColumn(Column), Empty);
                for (int32 K = 0; K < AtmosphereBins::Count; ++K)
                {
                    Out.Table.Texels[(Row * Size + Column) * AtmosphereBins::Count + K] = FFloat16(static_cast<float>(Cell.V[K])).GetFloat();
                }
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
