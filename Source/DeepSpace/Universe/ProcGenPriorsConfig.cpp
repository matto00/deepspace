#include "Universe/ProcGenPriorsConfig.h"

#include "Core/GameIniReload.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverse, Log, All);

UProcGenPriorsConfig::UProcGenPriorsConfig()
{
    const FGenPriors Defaults;
#define DS_FROM_PRIORS(Name) Name = Defaults.Name;
    DS_GEN_PRIORS(DS_FROM_PRIORS)
#undef DS_FROM_PRIORS
}

FGenPriors UProcGenPriorsConfig::ToPriors() const
{
    FGenPriors Priors;
#define DS_TO_PRIORS(Name) Priors.Name = Name;
    DS_GEN_PRIORS(DS_TO_PRIORS)
#undef DS_TO_PRIORS
    return Priors;
}

void UProcGenPriorsConfig::PostInitProperties()
{
    Super::PostInitProperties();
    // The class default has read the ini by now (UObject construction loads
    // a CDO's config before this). This is the editor's start: a refusal
    // here must leave a universe, not a crash in the first system asked for.
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        AcceptOrRefuse();
    }
}

void UProcGenPriorsConfig::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
    Super::PostReloadConfig(PropertyThatWasLoaded);
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        AcceptOrRefuse();
    }
}

void UProcGenPriorsConfig::AcceptOrRefuse()
{
    const FGenPriors Read = ToPriors();
    Refusals = GenPriorDomain::Refusals(Read);
    if (Refusals.IsEmpty())
    {
        Accepted = Read;
        return;
    }

    // Refused whole, never clamped line by line: a clamped prior is a number
    // nobody typed, and a half-applied tune is a universe nobody asked for.
    for (const FString& Refusal : Refusals)
    {
        UE_LOG(LogUniverse, Warning, TEXT("DefaultGame.ini [%s] refused: %s"), *GetClass()->GetPathName(), *Refusal);
    }
#define DS_RESTORE_PRIOR(Name) Name = Accepted.Name;
    DS_GEN_PRIORS(DS_RESTORE_PRIOR)
#undef DS_RESTORE_PRIOR
}

TArray<FString> UProcGenPriorsConfig::ReloadFromIni()
{
    // The file, not the cache: the cache still holds the session's start.
    GameIniReload::RereadFromDisk(StaticClass());

    return ApplyConfigCache();
}

TArray<FString> UProcGenPriorsConfig::ApplyConfigCache()
{
    UProcGenPriorsConfig* Defaults = GetMutableDefault<UProcGenPriorsConfig>();

    // A line deleted from the ini goes back to its code default, as it would
    // at start-up, rather than keeping whatever the last reload left.
    const FGenPriors CodeDefaults;
#define DS_RESET_PRIOR(Name) Defaults->Name = CodeDefaults.Name;
    DS_GEN_PRIORS(DS_RESET_PRIOR)
#undef DS_RESET_PRIOR

    Defaults->ReloadConfig();
    return Defaults->GetRefusals();
}
