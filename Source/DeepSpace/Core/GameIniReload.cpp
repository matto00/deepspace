#include "Core/GameIniReload.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/ConfigContext.h"
#include "UObject/Class.h"

void GameIniReload::RereadFromDisk(const UClass* ConfigClass)
{
    if (!ConfigClass)
    {
        return;
    }
    // As UObject::UpdateSingleSectionOfConfigFile re-reads after writing,
    // less the write: the file on disk is the one the developer just edited.
    FConfigContext Context = FConfigContext::ForceReloadIntoGConfig();
    Context.bWriteDestIni = false;
    Context.Load(*ConfigClass->ClassConfigName.ToString());
}
