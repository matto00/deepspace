#include "Universe/ProcGenPriorsConfig.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"

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

void UProcGenPriorsConfig::ReloadFromIni()
{
    UProcGenPriorsConfig* Defaults = GetMutableDefault<UProcGenPriorsConfig>();

    // ReloadConfig alone reads the config cache, which was filled from disk
    // at start-up and would hand back the numbers the session began with.
    // The file on disk is the one the developer just edited, so the Game
    // branch is re-read first, as UObject::UpdateSingleSectionOfConfigFile
    // does after writing it. Nothing is written back: this only reads.
    FConfigContext Context = FConfigContext::ForceReloadIntoGConfig();
    Context.bWriteDestIni = false;
    Context.Load(*Defaults->GetClass()->ClassConfigName.ToString());

    // A line deleted from the ini goes back to its code default, as it would
    // at start-up, rather than keeping whatever the last reload left.
    const FGenPriors CodeDefaults;
#define DS_RESET_PRIOR(Name) Defaults->Name = CodeDefaults.Name;
    DS_GEN_PRIORS(DS_RESET_PRIOR)
#undef DS_RESET_PRIOR

    Defaults->ReloadConfig();
}
