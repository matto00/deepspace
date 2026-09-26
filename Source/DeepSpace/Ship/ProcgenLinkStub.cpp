// TEMPORARY -- delete once procgen's Universe/UniverseSubsystem.cpp is merged.
//
// UUniverseSubsystem is declared as a UCLASS in the Wave 0 contract header,
// so UHT emits its vtable into this module, and the vtable needs Initialize.
// Until procgen's definition lands the module does not link, and nothing in
// this branch can be built or tested.
//
// Two guards, so the merge cannot break the build even if this file is
// forgotten: __has_include removes the definition wherever procgen's file
// exists (a unity build puts both in one translation unit, where a second
// definition is an error however it is marked), and weak lets procgen's
// strong definition win over a stale object compiled before it arrived.
#include "Universe/UniverseSubsystem.h"

#if !__has_include("Universe/UniverseSubsystem.cpp")
__attribute__((weak)) void UUniverseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}
#endif
