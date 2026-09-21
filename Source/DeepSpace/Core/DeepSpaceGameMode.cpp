#include "Core/DeepSpaceGameMode.h"

#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipSubsystem.h"

namespace
{
    // The hauler's milestone 1 loadout. Paths rather than hard references so
    // the game mode does not force these assets to load with the class.
    const TCHAR* DefaultModulePaths[] = {
        TEXT("/Game/Ship/Modules/DA_LifeSupport.DA_LifeSupport"),
        TEXT("/Game/Ship/Modules/DA_Lights.DA_Lights"),
        TEXT("/Game/Ship/Modules/DA_Sensors.DA_Sensors"),
    };
}

ADeepSpaceGameMode::ADeepSpaceGameMode()
{
    DefaultPawnClass = ADeepSpaceCharacter::StaticClass();

    for (const TCHAR* Path : DefaultModulePaths)
    {
        StartingModules.Emplace(FSoftObjectPath(Path));
    }
}

void ADeepSpaceGameMode::BeginPlay()
{
    Super::BeginPlay();

    UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        UE_LOG(LogTemp, Warning, TEXT("DeepSpaceGameMode: no ship subsystem; modules not installed."));
        return;
    }

    for (const TSoftObjectPtr<UShipModuleDataAsset>& SoftModule : StartingModules)
    {
        UShipModuleDataAsset* Module = SoftModule.LoadSynchronous();
        if (!Module)
        {
            UE_LOG(LogTemp, Warning, TEXT("DeepSpaceGameMode: could not load module %s."),
                *SoftModule.ToString());
            continue;
        }

        if (!Ship->InstallModule(Module))
        {
            UE_LOG(LogTemp, Warning, TEXT("DeepSpaceGameMode: module %s was rejected (duplicate id?)."),
                *Module->ModuleId.ToString());
        }
    }
}
