#include "Universe/GenStream.h"

#include <cmath>

// Each body below is the form its declaration's comment gives, in the same
// order, and Tools/rng.py is the same again in Python. They are written out
// rather than tidied: an algebraically equal rearrangement can round
// differently, and DeepSpace.Universe.Stream holds every one of them to
// Tools/rng_vectors.json bit for bit.

FGenStream::FGenStream(uint64 Seed)
    : State(Seed)
{
}

uint64 FGenStream::NextU64()
{
    State += GenSeed::Golden;
    return GenSeed::Finalise(State);
}

double FGenStream::Unit()
{
    return static_cast<double>(NextU64() >> 11) * 0x1.0p-53;
}

double FGenStream::UnitOpen()
{
    return (static_cast<double>(NextU64() >> 12) + 0.5) * 0x1.0p-52;
}

int64 FGenStream::UniformInt(int64 Min, int64 MaxInclusive)
{
    check(Min <= MaxInclusive);

    using uint128 = unsigned __int128;

    const uint64 Span = static_cast<uint64>(MaxInclusive) - static_cast<uint64>(Min) + 1;
    if (Span == 0)
    {
        return static_cast<int64>(NextU64());
    }

    uint128 M = static_cast<uint128>(NextU64()) * Span;
    uint64 Low = static_cast<uint64>(M);
    if (Low < Span)
    {
        const uint64 Threshold = (0 - Span) % Span;
        while (Low < Threshold)
        {
            M = static_cast<uint128>(NextU64()) * Span;
            Low = static_cast<uint64>(M);
        }
    }
    return static_cast<int64>(static_cast<uint64>(Min) + static_cast<uint64>(M >> 64));
}

bool FGenStream::Chance(double P)
{
    return Unit() < P;
}

int32 FGenStream::Poisson(double Mean, int32 Max)
{
    check(Mean >= 0.0 && Mean <= MaxPoissonMean);

    const double L = std::exp(-Mean);
    int32 K = 0;
    double P = Unit();
    while (P > L && K < Max)
    {
        ++K;
        P *= Unit();
    }
    return K;
}

double FGenStream::Normal(double Mean, double StdDev)
{
    const double U1 = UnitOpen();
    const double U2 = Unit();
    return Mean + StdDev * (std::sqrt(-2.0 * std::log(U1)) * std::cos(2.0 * UE_DOUBLE_PI * U2));
}

double FGenStream::LogNormal(double Median, double Sigma)
{
    return Median * std::exp(Sigma * Normal(0.0, 1.0));
}

double FGenStream::LogNormalBounded(double Median, double Sigma, double Min, double Max)
{
    check(Min > 0.0 && Min <= Max);

    double X = 0.0;
    for (int32 Attempt = 0; Attempt < MaxBoundedAttempts; ++Attempt)
    {
        X = LogNormal(Median, Sigma);
        if (Min <= X && X <= Max)
        {
            return X;
        }
    }
    return FMath::Clamp(X, Min, Max);
}

double FGenStream::Exponential(double Mean)
{
    return -Mean * std::log(UnitOpen());
}

double FGenStream::ParetoBounded(double Alpha, double Min, double Max)
{
    check(Alpha > 0.0 && Min > 0.0 && Min < Max);

    const double U = UnitOpen();
    const double Tail = std::pow(Min / Max, Alpha);
    const double X = Min * std::pow(1.0 - U * (1.0 - Tail), -1.0 / Alpha);
    return FMath::Clamp(X, Min, Max);
}

double FGenStream::Gamma(double Shape)
{
    check(Shape > 0.0);

    if (Shape < 1.0)
    {
        const double G = Gamma(Shape + 1.0);
        return G * std::pow(UnitOpen(), 1.0 / Shape);
    }

    const double D = Shape - 1.0 / 3.0;
    const double C = 1.0 / std::sqrt(9.0 * D);
    for (;;)
    {
        double X = 0.0;
        double V = 0.0;
        do
        {
            X = Normal(0.0, 1.0);
            V = 1.0 + C * X;
        }
        while (V <= 0.0);

        V = V * V * V;
        const double U = UnitOpen();
        if (U < 1.0 - 0.0331 * (X * X) * (X * X))
        {
            return D * V;
        }
        if (std::log(U) < 0.5 * X * X + D * (1.0 - V + std::log(V)))
        {
            return D * V;
        }
    }
}

double FGenStream::Beta(double A, double B)
{
    const double X = Gamma(A);
    const double Y = Gamma(B);
    return X / (X + Y);
}

int32 FGenStream::Categorical(TConstArrayView<double> Weights)
{
    double Total = 0.0;
    for (const double Weight : Weights)
    {
        check(std::isfinite(Weight) && Weight >= 0.0);
        Total += Weight;
    }
    check(Total > 0.0);

    const double Target = Unit() * Total;
    double Acc = 0.0;
    for (int32 Index = 0; Index < Weights.Num(); ++Index)
    {
        Acc += Weights[Index];
        if (Target < Acc)
        {
            return Index;
        }
    }

    // Only reachable through rounding in the running sum.
    for (int32 Index = Weights.Num() - 1; Index >= 0; --Index)
    {
        if (Weights[Index] > 0.0)
        {
            return Index;
        }
    }
    return 0;
}
