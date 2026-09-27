#include "Universe/GenPriors.h"

#include "Universe/GenStream.h"

#include <cmath>
#include <limits>

namespace GenPriorsLocal
{
    constexpr double Unbounded = std::numeric_limits<double>::max();

    /** One prior against its interval. Every bound is finite, so a NaN or an
     *  infinity typed into the ini fails every one of these comparisons. */
    void Within(TArray<FString>& Out, const TCHAR* Name, double Value, double Min, bool bMinOpen, double Max, const TCHAR* Why)
    {
        const bool bAboveMin = bMinOpen ? Value > Min : Value >= Min;
        if (std::isfinite(Value) && bAboveMin && Value <= Max)
        {
            return;
        }
        const FString Bound = Max == Unbounded
            ? FString::Printf(TEXT("finite and %s %g"), bMinOpen ? TEXT("above") : TEXT("at least"), Min)
            : FString::Printf(TEXT("in %s%g, %g]"), bMinOpen ? TEXT("(") : TEXT("["), Min, Max);
        Out.Add(FString::Printf(TEXT("%s=%g must be %s: %s"), Name, Value, *Bound, Why));
    }
}

TArray<FString> GenPriorDomain::Refusals(const FGenPriors& P)
{
    using namespace GenPriorsLocal;
    TArray<FString> Out;

    const TCHAR* const Weight = TEXT("a class weight is how often that class occurs, relative to the others");
    Within(Out, TEXT("ClassWeightM"), P.ClassWeightM, 0.0, false, Unbounded, Weight);
    Within(Out, TEXT("ClassWeightK"), P.ClassWeightK, 0.0, false, Unbounded, Weight);
    Within(Out, TEXT("ClassWeightG"), P.ClassWeightG, 0.0, false, Unbounded, Weight);
    Within(Out, TEXT("ClassWeightF"), P.ClassWeightF, 0.0, false, Unbounded, Weight);
    Within(Out, TEXT("ClassWeightA"), P.ClassWeightA, 0.0, false, Unbounded, Weight);
    Within(Out, TEXT("ClassWeightB"), P.ClassWeightB, 0.0, false, Unbounded, Weight);
    const double Total = P.ClassWeightM + P.ClassWeightK + P.ClassWeightG + P.ClassWeightF + P.ClassWeightA + P.ClassWeightB;
    if (Out.IsEmpty() && !(std::isfinite(Total) && Total > 0.0))
    {
        Out.Add(FString::Printf(TEXT("ClassWeightM..ClassWeightB total %g: some class must be possible, and the sum finite"), Total));
    }

    const TCHAR* const Shape = TEXT("a Beta shape, which below a tenth draws stars of no mass");
    Within(Out, TEXT("ClassBandBetaA"), P.ClassBandBetaA, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
    Within(Out, TEXT("ClassBandBetaB"), P.ClassBandBetaB, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);

    const TCHAR* const Count = TEXT("a Poisson mean, which the sampler takes only up to its ceiling");
    Within(Out, TEXT("PlanetCountMean"), P.PlanetCountMean, 0.0, false, FGenStream::MaxPoissonMean, Count);
    Within(Out, TEXT("SystemsPerSector"), P.SystemsPerSector, 0.0, false, FGenStream::MaxPoissonMean, Count);

    const TCHAR* const Median = TEXT("a log-normal median, a magnitude, so above zero");
    const TCHAR* const Sigma = TEXT("a log-normal sigma, a spread, so not below zero");
    Within(Out, TEXT("InnermostMedianFactor"), P.InnermostMedianFactor, 0.0, true, GenPriorDomain::MaxInnermostMedianFactor,
        TEXT("the innermost orbit's median, above zero and low enough that every orbit beyond it is a finite number"));
    Within(Out, TEXT("InnermostSigma"), P.InnermostSigma, 0.0, false, Unbounded, Sigma);
    Within(Out, TEXT("HillSpacingMedian"), P.HillSpacingMedian, 0.0, true, Unbounded, Median);
    Within(Out, TEXT("HillSpacingSigma"), P.HillSpacingSigma, 0.0, false, Unbounded, Sigma);
    Within(Out, TEXT("RockyMassMedianInner"), P.RockyMassMedianInner, 0.0, true, Unbounded, Median);
    Within(Out, TEXT("RockyMassMedianOuter"), P.RockyMassMedianOuter, 0.0, true, Unbounded, Median);
    Within(Out, TEXT("RockyMassSigma"), P.RockyMassSigma, 0.0, false, Unbounded, Sigma);

    const TCHAR* const Probability = TEXT("a probability");
    Within(Out, TEXT("GiantChanceMax"), P.GiantChanceMax, 0.0, false, 1.0, Probability);
    Within(Out, TEXT("GiantChancePerSolarMass"), P.GiantChancePerSolarMass, 0.0, false, Unbounded,
        TEXT("a chance per solar mass, so not below zero"));
    Within(Out, TEXT("GiantMassMedian"), P.GiantMassMedian, 0.0, true, Unbounded, Median);
    Within(Out, TEXT("GiantMassSigma"), P.GiantMassSigma, 0.0, false, Unbounded, Sigma);

    Within(Out, TEXT("OceanFraction"), P.OceanFraction, 0.0, false, 1.0, Probability);
    Within(Out, TEXT("InhabitedChance"), P.InhabitedChance, 0.0, false, 1.0, Probability);

    Within(Out, TEXT("PopulationMedian"), P.PopulationMedian, 0.0, true, Unbounded, Median);
    Within(Out, TEXT("PopulationSigma"), P.PopulationSigma, 0.0, false, Unbounded, Sigma);
    Within(Out, TEXT("PopulationMin"), P.PopulationMin, 1.0, false, Unbounded, TEXT("the smallest settlement, so at least somebody"));
    Within(Out, TEXT("PopulationMax"), P.PopulationMax, 0.0, true, Unbounded, TEXT("the largest settlement, a finite number of people"));
    if (std::isfinite(P.PopulationMin) && std::isfinite(P.PopulationMax) && P.PopulationMin > P.PopulationMax)
    {
        Out.Add(FString::Printf(TEXT("PopulationMin=%g is above PopulationMax=%g: the smallest settlement must not be larger than the largest"),
            P.PopulationMin, P.PopulationMax));
    }

    const TCHAR* const Strength = TEXT("a crust's strength, km at 1 g, so above zero");
    Within(Out, TEXT("ReliefStrengthRockKm"), P.ReliefStrengthRockKm, 0.0, true, Unbounded, Strength);
    Within(Out, TEXT("ReliefStrengthIceKm"), P.ReliefStrengthIceKm, 0.0, true, Unbounded, Strength);
    Within(Out, TEXT("ReliefTerrestrialFactor"), P.ReliefTerrestrialFactor, 0.0, true, Unbounded,
        TEXT("what weather leaves of a crust's strength, so above zero"));
    Within(Out, TEXT("ReliefBetaA"), P.ReliefBetaA, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
    Within(Out, TEXT("ReliefBetaB"), P.ReliefBetaB, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
    Within(Out, TEXT("ReliefTerrestrialBetaA"), P.ReliefTerrestrialBetaA, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
    Within(Out, TEXT("ReliefTerrestrialBetaB"), P.ReliefTerrestrialBetaB, GenPriorDomain::MinBetaShape, false, Unbounded, Shape);
    return Out;
}
