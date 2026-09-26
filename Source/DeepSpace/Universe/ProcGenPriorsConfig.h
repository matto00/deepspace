#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Universe/GenPriors.h"
#include "ProcGenPriorsConfig.generated.h"

/**
 * The ini mirror of FGenPriors: section [/Script/DeepSpace.ProcGenPriorsConfig]
 * of Config/DefaultGame.ini, one line per prior (procgen decision 14).
 *
 * The pure library never sees this class; it takes an FGenPriors. This is
 * the Unreal half of the seam, and the only thing that reads the section.
 * Its class default object is the one live copy: UUniverseSubsystem asks it
 * on every query rather than keeping its own, so a reload reaches every
 * world at once and nothing can be left generating with the old numbers.
 *
 * Only priors are here. The guarantees (GenGuarantees: the Hill floor, the
 * mass cap, the kind thresholds) are deliberately not, because an ini edit
 * must not be able to break an invariant.
 *
 * The field comments are in GenPriors.h, where the numbers mean something;
 * repeating them here would be a second place for them to go stale.
 */
UCLASS(Config = Game)
class DEEPSPACE_API UProcGenPriorsConfig : public UObject
{
    GENERATED_BODY()

public:
    /** Every property starts at FGenPriors{}'s value, so the code defaults
     *  have one source and a line missing from the ini falls back to it. */
    UProcGenPriorsConfig();

    /** The pure struct the generator takes. */
    FGenPriors ToPriors() const;

    /** Re-reads Config/DefaultGame.ini from disk into the config cache, then
     *  this class's section into the class default object. What
     *  ds.Universe.ReloadPriors does: a tune inside one running session, no
     *  rebuild and no restart. */
    static void ReloadFromIni();

    UPROPERTY(Config) double ClassWeightM;
    UPROPERTY(Config) double ClassWeightK;
    UPROPERTY(Config) double ClassWeightG;
    UPROPERTY(Config) double ClassWeightF;
    UPROPERTY(Config) double ClassWeightA;
    UPROPERTY(Config) double ClassWeightB;

    UPROPERTY(Config) double ClassBandBetaA;
    UPROPERTY(Config) double ClassBandBetaB;

    UPROPERTY(Config) double PlanetCountMean;

    UPROPERTY(Config) double InnermostMedianFactor;
    UPROPERTY(Config) double InnermostSigma;

    UPROPERTY(Config) double HillSpacingMedian;
    UPROPERTY(Config) double HillSpacingSigma;

    UPROPERTY(Config) double RockyMassMedianInner;
    UPROPERTY(Config) double RockyMassMedianOuter;
    UPROPERTY(Config) double RockyMassSigma;

    UPROPERTY(Config) double GiantChanceMax;
    UPROPERTY(Config) double GiantChancePerSolarMass;
    UPROPERTY(Config) double GiantMassMedian;
    UPROPERTY(Config) double GiantMassSigma;

    UPROPERTY(Config) double OceanFraction;

    UPROPERTY(Config) double InhabitedChance;

    UPROPERTY(Config) double PopulationMedian;
    UPROPERTY(Config) double PopulationSigma;
    UPROPERTY(Config) double PopulationMin;
    UPROPERTY(Config) double PopulationMax;

    UPROPERTY(Config) double SystemsPerSector;
};
