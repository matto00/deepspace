#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FAtmosphereChapmanTest,
    "DeepSpace.Atmosphere.Chapman",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereChapmanTestLocal
{
    /** The integral of F(t) for t = T0 + Sign L u^2, u in [0, 1]: Simpson in
     *  u, which crowds the samples at T0, where every integrand here peaks. */
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

    /**
     * ln of the Chapman function by quadrature, in scale heights: the column
     * along the ray over the vertical column from the same point, the
     * integral of e^(X - r(t)). Below the horizon the density is taken
     * relative to the ray's lowest point, so no term exceeds 1 and nothing
     * overflows however deep the tangent; the planet is ignored, as the
     * function ignores it.
     */
    double LogChapmanByQuadrature(double X, double Cos)
    {
        const double Sin = std::sqrt(FMath::Max(1.0 - Cos * Cos, 0.0));
        const double Lowest = Cos < 0.0 ? X * Sin : X;
        const auto Density = [X, Cos, Lowest](double T) { return std::exp(Lowest - std::sqrt(X * X + 2.0 * X * T * Cos + T * T)); };
        const double Far = Lowest + 80.0;
        double Sum = 0.0;
        if (Cos < 0.0)
        {
            const double Tangent = -X * Cos;
            Sum += Mapped(Density, Tangent, -1.0, Tangent, 40000);
            Sum += Mapped(Density, Tangent, 1.0, std::sqrt(Far * Far - Lowest * Lowest), 40000);
        }
        else
        {
            Sum += Mapped(Density, 0.0, 1.0, std::sqrt(Far * Far - X * X * Sin * Sin) - X * Cos, 40000);
        }
        return (X - Lowest) + std::log(Sum);
    }
}

bool FAtmosphereChapmanTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereChapmanTestLocal;

    // X = R / H. Every world that holds its air has X >= 96 (X = 6 lambda^2,
    // and retention starts at lambda 4); an Earth is about 850 and a heavy
    // giant several thousand.
    const double Xs[] = {96.0, 200.0, 850.0, 4000.0, 10000.0};
    double Worst = 0.0;
    FString WorstAt;
    bool bFinite = true;
    double WorstFloat = 0.0;
    for (const double X : Xs)
    {
        // 0 to 89 degrees, the horizon, and 30 degrees below it.
        for (int32 Degrees = 0; Degrees <= 120; ++Degrees)
        {
            const double Cos = std::cos(FMath::DegreesToRadians(static_cast<double>(Degrees)));
            const double Law = AtmosphereLaw::LogChapmanF64(X, Cos);
            const double Quadrature = LogChapmanByQuadrature(X, Cos);
            const double Error = FMath::Abs(std::exp(Law - Quadrature) - 1.0);
            if (Error > Worst)
            {
                Worst = Error;
                WorstAt = FString::Printf(TEXT("X %.0f, %d degrees"), X, Degrees);
            }
            const float Float = AtmosphereLaw::LogChapmanF32(static_cast<float>(X), static_cast<float>(Cos));
            bFinite &= std::isfinite(Float);
            WorstFloat = FMath::Max(WorstFloat, FMath::Abs(std::exp(static_cast<double>(Float) - Law) - 1.0));
        }
    }
    AddInfo(FString::Printf(TEXT("Chapman against quadrature: worst %.4f%% at %s; float against double worst %.2e"),
        100.0 * Worst, *WorstAt, WorstFloat));
    TestTrue(TEXT("the law's Chapman function is the numerical one within 0.5%, to 89 degrees and 30 below the horizon"), Worst <= 0.005);
    TestTrue(TEXT("finite in float throughout"), bFinite);
    TestTrue(TEXT("float follows double to 1e-3"), WorstFloat <= 1.0e-3);

    // Straight up is the vertical column itself; the airmass is 1.
    TestTrue(TEXT("Ch(850, zenith) is 1 to 1e-3"), FMath::Abs(std::exp(AtmosphereLaw::LogChapmanF64(850.0, 1.0)) - 1.0) < 1.0e-3);

    // A sun deep below the horizon from a giant's air is e^thousands of the
    // vertical column: a logarithm in the thousands, never an infinity.
    const float Deep = AtmosphereLaw::LogChapmanF32(10000.0f, -1.0f);
    TestTrue(FString::Printf(TEXT("a sun straight below, X 10,000, float: a finite log (%.1f)"), Deep), std::isfinite(Deep) && Deep > 9000.0f);
    return true;
}

#endif
