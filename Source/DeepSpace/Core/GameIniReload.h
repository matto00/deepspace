#pragma once

#include "CoreMinimal.h"

class UClass;

/**
 * The one way a tune reaches a running session: ds.Universe.ReloadPriors and
 * ds.Dress.Reload both come through here (lived-in decision 1c's risk, "the
 * reload has to read the file, not the cache").
 */
namespace GameIniReload
{
    /** Re-reads the config branch ConfigClass is read from (Game, for both
     *  callers) from disk into GConfig. ReloadConfig alone reads the cache,
     *  which was filled at start-up and would hand back the numbers the
     *  session began with. Nothing is written back: this only reads. The
     *  caller then applies the cache to its class default. */
    DEEPSPACE_API void RereadFromDisk(const UClass* ConfigClass);
}
