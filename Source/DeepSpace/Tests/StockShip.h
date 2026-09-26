#pragma once

#include "Core/DeepSpaceGameMode.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipSubsystem.h"
#include "UObject/SoftObjectPtr.h"

// The loadout play installs, for tests about what the power split does.
//
// A test world has no game mode, so a ship in one runs on its bare reactor
// with no modules drawing off the top -- far more headroom than the ship
// anybody flies. Three times a claim about the split held there and not in
// play: the jump could never wind at full speed, the lights sat at 63% on a
// quiet ship, and after the reactor was resized the split stopped doing
// anything at all. Any test that asserts what a split, a draw or a lean
// does should run on this.
namespace StockShip
{
    /** The Blueprint game mode's starting modules -- what play uses, which may
     *  override the C++ list -- falling back to C++. */
    inline TArray<UShipModuleDataAsset*> Modules()
    {
        const UClass* ModeClass = LoadClass<ADeepSpaceGameMode>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceGameMode.BP_DeepSpaceGameMode_C"));
        const ADeepSpaceGameMode* Mode = ModeClass
            ? ModeClass->GetDefaultObject<ADeepSpaceGameMode>()
            : GetDefault<ADeepSpaceGameMode>();
        TArray<UShipModuleDataAsset*> Loaded;
        for (const TSoftObjectPtr<UShipModuleDataAsset>& Soft : Mode->GetStartingModules())
        {
            if (UShipModuleDataAsset* Module = Soft.LoadSynchronous())
            {
                Loaded.Add(Module);
            }
        }
        return Loaded;
    }

    /** Installs the stock loadout; returns how many modules went in. */
    inline int32 Install(UShipSubsystem* Ship)
    {
        int32 Installed = 0;
        for (UShipModuleDataAsset* Module : Modules())
        {
            Installed += Ship->InstallModule(Module) ? 1 : 0;
        }
        return Installed;
    }
}
