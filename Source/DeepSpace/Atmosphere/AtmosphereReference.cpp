#include "Atmosphere/AtmosphereReference.h"

#include <cmath>
#include <limits>

namespace AtmosphereReferenceLocal
{
    using AtmosphereReference::FSpectrum;
    using SkyColour::Spectral::Count;

    constexpr double LoschmidtPerM3 = 2.546899e25;
    constexpr double AtomicMassKg = 1.66053906660e-27;
    constexpr double FourPi = 4.0 * UE_DOUBLE_PI;

    /** Standard air's refractivity, n - 1, at 288.15 K and 1013.25 hPa:
     *  Peck and Reeder (1972). */
    double RefractivityAir(double Nm)
    {
        const double InvUm2 = 1.0 / FMath::Square(Nm * 1.0e-3);
        return (8060.51 + 2480990.0 / (132.274 - InvUm2) + 17455.7 / (39.32957 - InvUm2)) * 1.0e-8;
    }

    /** Air's King factor, by volume: N2 and O2 by Bates (1984), argon 1,
     *  carbon dioxide 1.15. */
    double KingFactorAir(double Nm)
    {
        const double InvUm2 = 1.0 / FMath::Square(Nm * 1.0e-3);
        const double N2 = 1.034 + 3.17e-4 * InvUm2;
        const double O2 = 1.096 + 1.385e-3 * InvUm2 + 1.448e-4 * InvUm2 * InvUm2;
        return (78.084 * N2 + 20.946 * O2 + 0.934 * 1.0 + 0.036 * 1.15) / (78.084 + 20.946 + 0.934 + 0.036);
    }

    double RayleighPhase(double Cos)
    {
        return 3.0 / (16.0 * UE_DOUBLE_PI) * (1.0 + Cos * Cos);
    }

    double HenyeyGreenstein(double Cos, double G)
    {
        const double Den = 1.0 + G * G - 2.0 * G * Cos;
        return (1.0 - G * G) / (4.0 * UE_DOUBLE_PI * Den * std::sqrt(Den));
    }

    /** The integral of F(t) for t = T0 + Sign L u^2, Simpson in u. */
    template <typename TF>
    double Mapped(TF F, double T0, double Sign, double L, int32 Intervals)
    {
        const double Step = 1.0 / Intervals;
        double Sum = 0.0;
        for (int32 I = 0; I <= Intervals; ++I)
        {
            const double U = I * Step;
            const double Weight = (I == 0 || I == Intervals) ? 1.0 : ((I % 2) == 1 ? 4.0 : 2.0);
            Sum += Weight * F(T0 + Sign * L * U * U) * 2.0 * L * U;
        }
        return Sum * Step / 3.0;
    }

    /** The ray's part in the air, measured along it from its closest
     *  approach to the centre: exactly the law's geometry, in double. */
    struct FPath
    {
        double B = 0.0;
        FVector3d C = FVector3d::ZeroVector;
        FVector3d D = FVector3d::ZeroVector;
        double S0 = 1.0;
        double S1 = 0.0;
        bool IsEmpty() const { return !(S1 > S0); }
    };

    FPath PathThroughAir(FVector3d E, const FVector3d& D, double Length, double Top)
    {
        FPath Path;
        Path.D = D;
        const double E2 = E.SizeSquared();
        if (E2 < 1.0)
        {
            if (E2 <= 0.0)
            {
                return Path;
            }
            E /= std::sqrt(E2);
        }
        const FVector3d W = FVector3d::CrossProduct(E, D);
        Path.C = FVector3d::CrossProduct(D, W);
        Path.B = W.Size();
        const double SE = FVector3d::DotProduct(E, D);
        const double TopR = 1.0 + Top;
        if (Path.B >= TopR || !(Length > 0.0))
        {
            return Path;
        }
        const double Half = std::sqrt((TopR - Path.B) * (TopR + Path.B));
        double S0 = FMath::Max(SE, -Half);
        double S1 = FMath::Min(SE + Length, Half);
        if (Path.B < 1.0)
        {
            // As the law decides it: by the direction, not by the eye's
            // distance against the chord, which rounding decides for an eye
            // on the surface looking along it.
            const double Ground = std::sqrt((1.0 - Path.B) * (1.0 + Path.B));
            if (SE < -Ground)
            {
                S1 = FMath::Min(S1, -Ground);
            }
            else if (SE < 0.0)
            {
                S1 = S0;
            }
        }
        Path.S0 = S0;
        Path.S1 = S1;
        return Path;
    }

    struct FStep
    {
        double S = 0.0;
        double DS = 0.0;
    };

    /** Samples along the path in travel order: split at the closest point,
     *  spaced as u^2 from it, N in all -- the law's placement, finer. */
    TArray<FStep> Steps(const FPath& Path, int32 N)
    {
        TArray<FStep> Out;
        if (Path.IsEmpty() || N < 2)
        {
            return Out;
        }
        const auto Piece = [&Out](double Low, double Far, int32 Samples, bool bTowardLow)
        {
            const double Span = Far - Low;
            const double Abs = std::fabs(Span);
            for (int32 K = 0; K < Samples; ++K)
            {
                const int32 I = bTowardLow ? Samples - 1 - K : K;
                const double U = (I + 0.5) / Samples;
                Out.Add({Low + Span * U * U, Abs * 2.0 * U / Samples});
            }
        };
        if (Path.S0 >= 0.0)
        {
            Piece(Path.S0, Path.S1, N, false);
        }
        else if (Path.S1 <= 0.0)
        {
            Piece(Path.S1, Path.S0, N, true);
        }
        else
        {
            const int32 Behind = FMath::Clamp(FMath::RoundToInt32(N * (-Path.S0) / (Path.S1 - Path.S0)), 1, N - 1);
            Piece(0.0, Path.S0, Behind, true);
            Piece(0.0, Path.S1, N - Behind, false);
        }
        return Out;
    }

    /** One direction of the product rule: ring I of Rings (cos(zenith) =
     *  T |T|, T at the ring's centre in [-1, 1]), segment J of Segments, about
     *  the local vertical Up; and its share of the whole sphere. */
    struct FDirection
    {
        FVector3d W = FVector3d::ZeroVector;
        double Share = 0.0;
    };

    FDirection SphereDirection(const FVector3d& Up, int32 I, int32 Rings, int32 J, int32 Segments)
    {
        const FVector3d Across = FMath::Abs(Up.Z) < 0.9 ? FVector3d(0.0, 0.0, 1.0) : FVector3d(1.0, 0.0, 0.0);
        const FVector3d East = FVector3d::CrossProduct(Across, Up).GetSafeNormal();
        const FVector3d North = FVector3d::CrossProduct(Up, East);
        const double T = -1.0 + (2.0 * I + 1.0) / Rings;
        const double Mu = T * std::fabs(T);
        const double Ring = std::sqrt(FMath::Max(1.0 - Mu * Mu, 0.0));
        const double Phi = 2.0 * UE_DOUBLE_PI * (J + 0.5) / Segments;
        FDirection Out;
        Out.W = East * (Ring * std::cos(Phi)) + North * (Ring * std::sin(Phi)) + Up * Mu;
        Out.Share = 2.0 * std::fabs(T) / (static_cast<double>(Rings) * Segments);
        return Out;
    }

    bool InShadow(double R, double Cos)
    {
        return Cos < 0.0 && R * R * FMath::Max(1.0 - Cos * Cos, 0.0) < 1.0;
    }
}

AtmosphereReference::FSpectralAir AtmosphereReference::Spectral(const FAirSpec& Spec)
{
    FSpectralAir Air;
    if (!Spec.HasAir())
    {
        return Air;
    }
    Air.bAir = true;
    Air.GasH = Spec.GasScaleHeightCm / Spec.RadiusCm;
    const bool bAerosol = Spec.AerosolTau550 > 0.0 && Spec.AerosolScaleHeightCm > 0.0;
    Air.AerosolH = bAerosol ? Spec.AerosolScaleHeightCm / Spec.RadiusCm : Air.GasH;
    Air.AerosolG = FMath::Clamp(Spec.AerosolAsymmetry, 0.0, 0.95);
    Air.Top = AirTopScaleHeights * Air.GasH;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        const double Nm = SkyColour::Spectral::Nm(I);
        Air.GasScatter.Value[I] = Spec.GasTau550 * std::pow(550.0 / Nm, 4.0);
        Air.GasAbsorb.Value[I] = Spec.OzoneTau600 * std::exp(-0.5 * FMath::Square((Nm - OzonePeakNm) / OzoneWidthNm));
        Air.AerosolExtinct.Value[I] = bAerosol ? Spec.AerosolTau550 * std::pow(Nm / 550.0, -Spec.AerosolAngstrom) : 0.0;
        const double Along = FMath::Clamp((Nm - 450.0) / 200.0, 0.0, 1.0);
        Air.AerosolScatter.Value[I] = Air.AerosolExtinct.Value[I] * FMath::Lerp(Spec.AerosolAlbedo450, Spec.AerosolAlbedo650, Along);
    }
    return Air;
}

AtmosphereReference::FSpectrum AtmosphereReference::StarSpectrum(double TemperatureK)
{
    FSpectrum Star;
    double Luminance = 0.0;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        Star.Value[I] = SkyColour::Spectral::Planck(SkyColour::Spectral::Nm(I), TemperatureK);
        Luminance += Star.Value[I] * SkyColour::Spectral::Luminance(I);
    }
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        Star.Value[I] /= Luminance;
    }
    return Star;
}

FVector3d AtmosphereReference::ToLinearSrgb(const FSpectrum& Spectrum)
{
    return SkyColour::Spectral::ToLinearSrgb(MakeArrayView(Spectrum.Value, SkyColour::Spectral::Count));
}

FVector3d AtmosphereReference::Colour(const FSpectrum& Star, const FSpectrum& Value)
{
    FSpectrum Product;
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        Product.Value[I] = Star.Value[I] * Value.Value[I];
    }
    return ToLinearSrgb(Product);
}

FVector3d AtmosphereReference::ChannelAverage(const FSpectrum& Star, const FSpectrum& Value)
{
    const FVector3d Own = ToLinearSrgb(Star);
    const double Floor = 1.0e-4 * FMath::Max3(Own.X, Own.Y, Own.Z);
    const FVector3d Seen = Colour(Star, Value);
    return FVector3d(Seen.X / FMath::Max(Own.X, Floor), Seen.Y / FMath::Max(Own.Y, Floor), Seen.Z / FMath::Max(Own.Z, Floor));
}

double AtmosphereReference::RayleighCrossSectionAirM2(double Nm)
{
    using namespace AtmosphereReferenceLocal;
    const double Metres = Nm * 1.0e-9;
    const double N = 1.0 + RefractivityAir(Nm);
    const double Ratio = (N * N - 1.0) / (N * N + 2.0);
    return 24.0 * FMath::Cube(UE_DOUBLE_PI) * Ratio * Ratio / (std::pow(Metres, 4.0) * LoschmidtPerM3 * LoschmidtPerM3) * KingFactorAir(Nm);
}

double AtmosphereReference::ColumnMoleculesPerM2(double PressureBar, double GravityMS2, double MeanMolecularWeight)
{
    using namespace AtmosphereReferenceLocal;
    return PressureBar * 1.0e5 / (MeanMolecularWeight * AtomicMassKg * GravityMS2);
}

double AtmosphereReference::ColumnExact(double R, double CosZenith, double H, int32 Intervals)
{
    using namespace AtmosphereReferenceLocal;
    const double Cos = FMath::Clamp(CosZenith, -1.0, 1.0);
    const double Sin2 = FMath::Max(1.0 - Cos * Cos, 0.0);
    if (InShadow(R, Cos))
    {
        return -1.0;
    }
    // The density relative to the ray's lowest point, so no term exceeds 1;
    // that point's own density is put back at the end.
    const double Lowest = Cos < 0.0 ? R * std::sqrt(Sin2) : R;
    const auto Density = [R, Cos, H, Lowest](double T) { return std::exp(-(std::sqrt(R * R + 2.0 * R * T * Cos + T * T) - Lowest) / H); };
    const double Far = Lowest + 80.0 * H;
    const int32 N = FMath::Max(2, Intervals + (Intervals & 1));
    double Sum = 0.0;
    if (Cos < 0.0)
    {
        const double Tangent = -R * Cos;
        Sum += Mapped(Density, Tangent, -1.0, Tangent, N);
        Sum += Mapped(Density, Tangent, 1.0, std::sqrt(Far * Far - Lowest * Lowest), N);
    }
    else
    {
        Sum += Mapped(Density, 0.0, 1.0, std::sqrt(Far * Far - R * R * Sin2) - R * Cos, N);
    }
    return std::exp(-(Lowest - 1.0) / H) * Sum;
}

FReferenceAir::FReferenceAir(const FAirSpec& Spec, double StarTemperatureK)
    : Air(AtmosphereReference::Spectral(Spec))
    , Star(AtmosphereReference::StarSpectrum(StarTemperatureK))
{
    if (!Air.bAir)
    {
        return;
    }
    // ln of the exact column over altitude and angle, for the second
    // order's sun paths; +infinity where the ray meets the ground, and never
    // below -700, where a column is nothing and a logarithm must stay a
    // number to be interpolated.
    const int32 Cells = TableAltitudes * TableCosines;
    LogGasColumn.SetNumUninitialized(Cells);
    LogAerosolColumn.SetNumUninitialized(Cells);
    for (int32 J = 0; J < TableAltitudes; ++J)
    {
        const double R = 1.0 + Air.Top * J / (TableAltitudes - 1);
        for (int32 I = 0; I < TableCosines; ++I)
        {
            const double Cos = -1.0 + 2.0 * I / (TableCosines - 1);
            const double Gas = AtmosphereReference::ColumnExact(R, Cos, Air.GasH, 400);
            const double Aerosol = AtmosphereReference::ColumnExact(R, Cos, Air.AerosolH, 400);
            LogGasColumn[J * TableCosines + I] = Gas < 0.0 ? std::numeric_limits<double>::infinity() : FMath::Max(std::log(FMath::Max(Gas, 1.0e-300)), -700.0);
            LogAerosolColumn[J * TableCosines + I] = Aerosol < 0.0 ? std::numeric_limits<double>::infinity() : FMath::Max(std::log(FMath::Max(Aerosol, 1.0e-300)), -700.0);
        }
    }
}

double FReferenceAir::LogColumnLookup(const TArray<double>& Table, double R, double Cos, double H) const
{
    const double FY = FMath::Clamp((R - 1.0) / Air.Top, 0.0, 1.0) * (TableAltitudes - 1);
    const double FX = FMath::Clamp((Cos + 1.0) * 0.5, 0.0, 1.0) * (TableCosines - 1);
    const int32 Y0 = FMath::Min(FMath::FloorToInt32(FY), TableAltitudes - 2);
    const int32 X0 = FMath::Min(FMath::FloorToInt32(FX), TableCosines - 2);
    const double TY = FY - Y0;
    const double TX = FX - X0;
    const double C00 = Table[Y0 * TableCosines + X0];
    const double C01 = Table[Y0 * TableCosines + X0 + 1];
    const double C10 = Table[(Y0 + 1) * TableCosines + X0];
    const double C11 = Table[(Y0 + 1) * TableCosines + X0 + 1];
    if (!std::isfinite(C00) || !std::isfinite(C01) || !std::isfinite(C10) || !std::isfinite(C11))
    {
        // Beside the ground's shadow the table has no neighbours to blend:
        // integrate this one exactly.
        const double Exact = AtmosphereReference::ColumnExact(R, Cos, H, 400);
        return Exact < 0.0 ? std::numeric_limits<double>::infinity() : FMath::Max(std::log(FMath::Max(Exact, 1.0e-300)), -700.0);
    }
    return FMath::Lerp(FMath::Lerp(C00, C01, TX), FMath::Lerp(C10, C11, TX), TY);
}

AtmosphereReference::FSpectrum FReferenceAir::SunExact(double R, double Cos) const
{
    AtmosphereReference::FSpectrum Out;
    if (AtmosphereReferenceLocal::InShadow(R, Cos))
    {
        return Out;
    }
    const double Gas = AtmosphereReference::ColumnExact(R, Cos, Air.GasH, 1000);
    const double Aerosol = AtmosphereReference::ColumnExact(R, Cos, Air.AerosolH, 1000);
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        const double Tau = (Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * Gas + Air.AerosolExtinct.Value[I] / Air.AerosolH * Aerosol;
        Out.Value[I] = std::exp(-Tau);
    }
    return Out;
}

AtmosphereReference::FSpectrum FReferenceAir::SunLookup(double R, double Cos) const
{
    AtmosphereReference::FSpectrum Out;
    if (AtmosphereReferenceLocal::InShadow(R, Cos))
    {
        return Out;
    }
    const double Gas = std::exp(LogColumnLookup(LogGasColumn, R, Cos, Air.GasH));
    const double Aerosol = std::exp(LogColumnLookup(LogAerosolColumn, R, Cos, Air.AerosolH));
    for (int32 I = 0; I < SkyColour::Spectral::Count; ++I)
    {
        const double Tau = (Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * Gas + Air.AerosolExtinct.Value[I] / Air.AerosolH * Aerosol;
        Out.Value[I] = std::exp(-Tau);
    }
    return Out;
}

FReferenceAir::FSecond FReferenceAir::SecondOrderAt(const FVector3d& Point, const FVector3d& View, const FVector3d& Sun, int32 Rings, int32 Segments, int32 StepsPerRay, bool bIsotropic) const
{
    using namespace AtmosphereReferenceLocal;
    FSecond Out;
    const FVector3d Up = Point.GetSafeNormal();
    for (int32 D = 0; D < Rings * Segments; ++D)
    {
        const FDirection Direction = SphereDirection(Up, D / Segments, Rings, D % Segments, Segments);
        const FVector3d& W = Direction.W;
        const double Solid = FourPi * Direction.Share;
        const FPath Ray = PathThroughAir(Point, W, 1.0e30, Air.Top);
        // Light from the sun, scattered once at Q toward Point: it travels
        // along -W, so its angle to the sunlight's -Sun is Sun . W.
        const double CosSun = FVector3d::DotProduct(Sun, W);
        const double PhaseGas = RayleighPhase(CosSun);
        const double PhaseAerosol = HenyeyGreenstein(CosSun, Air.AerosolG);
        FSpectrum Arriving;
        FSpectrum Transfer;
        FSpectrum Depth;
        for (const FStep& Step : Steps(Ray, StepsPerRay))
        {
            const FVector3d Q = Ray.C + Step.S * Ray.D;
            const double RQ = std::sqrt(Ray.B * Ray.B + Step.S * Step.S);
            const double GasDensity = std::exp(-(RQ - 1.0) / Air.GasH);
            const double AerosolDensity = std::exp(-(RQ - 1.0) / Air.AerosolH);
            const FSpectrum SunAtQ = SunLookup(RQ, FVector3d::DotProduct(Q, Sun) / RQ);
            for (int32 I = 0; I < Count; ++I)
            {
                const double GasScatter = Air.GasScatter.Value[I] / Air.GasH * GasDensity;
                const double AerosolScatter = Air.AerosolScatter.Value[I] / Air.AerosolH * AerosolDensity;
                const double DTau = ((Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * GasDensity
                    + Air.AerosolExtinct.Value[I] / Air.AerosolH * AerosolDensity) * Step.DS;
                const double Through = std::exp(-(Depth.Value[I] + 0.5 * DTau));
                Arriving.Value[I] += (GasScatter * PhaseGas + AerosolScatter * PhaseAerosol) * SunAtQ.Value[I] * Through * Step.DS;
                Transfer.Value[I] += (GasScatter + AerosolScatter) * Through * Step.DS;
                Depth.Value[I] += DTau;
            }
        }
        // Scattered again at Point into the view: light arriving from W
        // travels along -W, and leaves along -View toward the eye.
        const double CosView = FVector3d::DotProduct(W, View);
        // Isotropic, the second scattering sends light every way alike: the
        // law's own assumption for every order past the first.
        const double IntoGas = bIsotropic ? Direction.Share : RayleighPhase(CosView) * Solid;
        const double IntoAerosol = bIsotropic ? Direction.Share : HenyeyGreenstein(CosView, Air.AerosolG) * Solid;
        for (int32 I = 0; I < Count; ++I)
        {
            Out.IntoViewGas.Value[I] += IntoGas * Arriving.Value[I];
            Out.IntoViewAerosol.Value[I] += IntoAerosol * Arriving.Value[I];
            Out.Mean.Value[I] += Direction.Share * Arriving.Value[I];
            Out.Transfer.Value[I] += Direction.Share * Transfer.Value[I];
        }
    }
    return Out;
}

FReferenceAir::FResult FReferenceAir::Trace(const FRay& In) const
{
    return Trace(In, FOptions());
}

FReferenceAir::FResult FReferenceAir::Trace(const FRay& In, const FOptions& Options) const
{
    using namespace AtmosphereReferenceLocal;
    FResult Result;
    if (!Air.bAir)
    {
        return Result;
    }
    const FVector3d D = In.Direction.GetSafeNormal();
    const bool bSun = In.Sun.SizeSquared() > 0.25;
    const FVector3d S = bSun ? In.Sun.GetSafeNormal() : FVector3d::ZeroVector;
    const FPath Path = PathThroughAir(In.Eye, D, In.Length, Air.Top);
    if (Path.IsEmpty())
    {
        return Result;
    }

    const double CosView = FVector3d::DotProduct(S, D);
    const double PhaseGas = RayleighPhase(CosView);
    const double PhaseAerosol = HenyeyGreenstein(CosView, Air.AerosolG);
    FSpectrum Light;
    FSpectrum Depth;

    // First order: fine samples in travel order, the view's transmittance
    // accumulated, the sun's by exact integration at every sample.
    for (const FStep& Step : Steps(Path, Options.ViewSteps))
    {
        const FVector3d P = Path.C + Step.S * Path.D;
        const double R = std::sqrt(Path.B * Path.B + Step.S * Step.S);
        const double GasDensity = std::exp(-(R - 1.0) / Air.GasH);
        const double AerosolDensity = std::exp(-(R - 1.0) / Air.AerosolH);
        const FSpectrum SunAtP = bSun ? SunExact(R, FVector3d::DotProduct(P, S) / R) : FSpectrum();
        for (int32 I = 0; I < Count; ++I)
        {
            const double DTau = ((Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * GasDensity
                + Air.AerosolExtinct.Value[I] / Air.AerosolH * AerosolDensity) * Step.DS;
            const double Through = std::exp(-(Depth.Value[I] + 0.5 * DTau));
            Light.Value[I] += (Air.GasScatter.Value[I] / Air.GasH * GasDensity * PhaseGas
                + Air.AerosolScatter.Value[I] / Air.AerosolH * AerosolDensity * PhaseAerosol) * SunAtP.Value[I] * Through * Step.DS;
            Depth.Value[I] += DTau;
        }
    }

    // Second order by brute force, and the geometric tail beyond it with
    // the reference's own transfer fraction f, on a coarser path.
    if (bSun && Options.bSecondOrder)
    {
        FSpectrum Coarse;
        for (const FStep& Step : Steps(Path, Options.SecondOrderViewSteps))
        {
            const FVector3d P = Path.C + Step.S * Path.D;
            const double R = std::sqrt(Path.B * Path.B + Step.S * Step.S);
            const double GasDensity = std::exp(-(R - 1.0) / Air.GasH);
            const double AerosolDensity = std::exp(-(R - 1.0) / Air.AerosolH);
            const FSecond Second = SecondOrderAt(P, D, S, Options.SphereRings, Options.SphereSegments, Options.SecondarySteps, Options.bIsotropicSecondOrder);
            for (int32 I = 0; I < Count; ++I)
            {
                const double GasScatter = Air.GasScatter.Value[I] / Air.GasH * GasDensity;
                const double AerosolScatter = Air.AerosolScatter.Value[I] / Air.AerosolH * AerosolDensity;
                const double DTau = ((Air.GasScatter.Value[I] + Air.GasAbsorb.Value[I]) / Air.GasH * GasDensity
                    + Air.AerosolExtinct.Value[I] / Air.AerosolH * AerosolDensity) * Step.DS;
                const double Through = std::exp(-(Coarse.Value[I] + 0.5 * DTau));
                const double F = FMath::Min(Second.Transfer.Value[I], 0.999);
                const double Tail = (GasScatter + AerosolScatter) * Second.Mean.Value[I] * F / (1.0 - F);
                Light.Value[I] += (GasScatter * Second.IntoViewGas.Value[I] + AerosolScatter * Second.IntoViewAerosol.Value[I] + Tail) * Through * Step.DS;
                Coarse.Value[I] += DTau;
            }
        }
    }

    FSpectrum Through;
    for (int32 I = 0; I < Count; ++I)
    {
        Through.Value[I] = std::exp(-Depth.Value[I]);
    }
    Result.InScatter = UE_DOUBLE_PI * AtmosphereReference::Colour(Star, Light);
    Result.Transmittance = AtmosphereReference::ChannelAverage(Star, Through);
    return Result;
}

FVector3d FReferenceAir::SunThrough(const FVector3d& Point, const FVector3d& Sun) const
{
    if (!Air.bAir)
    {
        return FVector3d::OneVector;
    }
    if (Sun.SizeSquared() < 0.25)
    {
        return FVector3d::ZeroVector;
    }
    const double Length = Point.Size();
    const double R = FMath::Max(Length, 1.0);
    const double Cos = FVector3d::DotProduct(Point, Sun.GetSafeNormal()) / FMath::Max(Length, 1.0e-300);
    return AtmosphereReference::ChannelAverage(Star, SunExact(R, Cos));
}

AtmosphereReference::FSpectrum FReferenceAir::MultiScatterSpectrum(double Altitude01, double CosSunZenith, int32 Rings, int32 Segments, int32 StepsPerRay) const
{
    using namespace AtmosphereReferenceLocal;
    FSpectrum Psi;
    if (!Air.bAir)
    {
        return Psi;
    }
    const FVector3d Point(0.0, 0.0, 1.0 + FMath::Clamp(Altitude01, 0.0, 1.0) * Air.Top);
    const FVector3d Sun(std::sqrt(FMath::Max(1.0 - CosSunZenith * CosSunZenith, 0.0)), 0.0, CosSunZenith);
    // The View only weights IntoView*, which this does not read.
    const FSecond Second = SecondOrderAt(Point, FVector3d(0.0, 0.0, 1.0), Sun, Rings, Segments, StepsPerRay, false);
    for (int32 I = 0; I < Count; ++I)
    {
        const double F = FMath::Min(Second.Transfer.Value[I], 0.999);
        Psi.Value[I] = UE_DOUBLE_PI * Second.Mean.Value[I] / (1.0 - F);
    }
    return Psi;
}

FVector3d FReferenceAir::MultiScatterWhite(double Altitude01, double CosSunZenith, int32 Rings, int32 Segments, int32 StepsPerRay) const
{
    if (!Air.bAir)
    {
        return FVector3d::ZeroVector;
    }
    return AtmosphereReference::ChannelAverage(Star, MultiScatterSpectrum(Altitude01, CosSunZenith, Rings, Segments, StepsPerRay));
}
