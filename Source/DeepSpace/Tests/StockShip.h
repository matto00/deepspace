#pragma once

#include "Core/DeepSpaceGameMode.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipSubsystem.h"
#include "UObject/SoftObjectPtr.h"

// The loadout play fits, for tests about what the power split does.
//
// A test world has no game mode, so a ship in one has every bay empty: stock
// ratings and nothing drawing off the top -- far more headroom than the ship
// anybody flies. Three times a claim about the split held there and not in
// play: the jump could never wind at full speed, the lights sat at 63% on a
// quiet ship, and after the reactor was resized the split stopped doing
// anything at all. Any test that asserts what a split, a draw or a lean
// does should run on this.
namespace StockShip
{
    /** BP_DeepSpaceGameMode, the class play runs; null if it fails to load.
     *  A test that claims to read the Blueprint's list asserts this first,
     *  since Modules() quietly reads C++ without it. */
    inline UClass* BlueprintMode()
    {
        return LoadClass<ADeepSpaceGameMode>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceGameMode.BP_DeepSpaceGameMode_C"));
    }

    /** The Blueprint game mode's starting parts -- what play uses, which may
     *  override the C++ list -- falling back to C++. */
    inline TArray<UShipModuleDataAsset*> Modules()
    {
        const UClass* ModeClass = BlueprintMode();
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

    /** Fits the stock loadout, one part per core bay; returns how many went in. */
    inline int32 Install(UShipSubsystem* Ship)
    {
        int32 Fitted = 0;
        for (UShipModuleDataAsset* Part : Modules())
        {
            Fitted += Ship->FitPart(Part) ? 1 : 0;
        }
        return Fitted;
    }
}
