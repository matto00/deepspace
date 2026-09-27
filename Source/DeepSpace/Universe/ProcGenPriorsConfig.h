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

    /** The pure struct the generator takes. On the class default object it
     *  is always a set GenPriorDomain accepted: a refused read never lands. */
    FGenPriors ToPriors() const;

    /** Re-reads Config/DefaultGame.ini from disk into the config cache, then
     *  applies it as ApplyConfigCache does. What ds.Universe.ReloadPriors
     *  does: a tune inside one running session, no rebuild and no restart.
     *  Returns the refusals, empty if the file was taken. */
    static TArray<FString> ReloadFromIni();

    /** Reads this class's section of the config cache, as it stands, into
     *  the class default object: every prior back to its code default first,
     *  so a line deleted from the ini falls back as it would at start-up, then
     *  the lines present. If the result is outside GenPriorDomain the whole
     *  read is refused and the priors in use before it stay, each wrong line
     *  logged. Returns the refusals, empty if the read was taken. */
    static TArray<FString> ApplyConfigCache();

    /** Why the class default's last read of the ini was refused, one line per
     *  wrong prior; empty if it was taken. A refusal at start-up leaves the
     *  code defaults in use, and this is where it says so. */
    const TArray<FString>& GetRefusals() const { return Refusals; }

    virtual void PostInitProperties() override;
    virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;

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

    UPROPERTY(Config) double ReliefStrengthRockKm;
    UPROPERTY(Config) double ReliefStrengthIceKm;
    UPROPERTY(Config) double ReliefTerrestrialFactor;
    UPROPERTY(Config) double ReliefBetaA;
    UPROPERTY(Config) double ReliefBetaB;
    UPROPERTY(Config) double ReliefTerrestrialBetaA;
    UPROPERTY(Config) double ReliefTerrestrialBetaB;

    UPROPERTY(Config) double SystemsPerSector;

private:
    /** Take what the ini just put into the properties, or put back the last
     *  set that was taken. Only the class default reads the ini. */
    void AcceptOrRefuse();

    /** The last set taken: code defaults until the ini is. Not a UPROPERTY,
     *  so nothing but AcceptOrRefuse writes it -- not the ini, and not a
     *  reset to code defaults ahead of a read. */
    FGenPriors Accepted;

    TArray<FString> Refusals;
};
