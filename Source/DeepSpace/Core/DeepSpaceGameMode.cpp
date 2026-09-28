#include "Core/DeepSpaceGameMode.h"

#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipSubsystem.h"

namespace
{
    // The stock ship, one part per core bay (wear and upgrades decision 2).
    // Paths rather than hard references so the game mode does not force these
    // assets to load with the class. BP_DeepSpaceGameMode's saved list
    // overrides this one, and Tools/setup_ship_parts.py writes that list;
    // DeepSpace.Ship.Parts.StockIsToday holds the two equal.
    const TCHAR* DefaultModulePaths[] = {
        TEXT("/Game/Ship/Parts/DA_Reactor_Stock.DA_Reactor_Stock"),
        TEXT("/Game/Ship/Parts/DA_Drive_Stock.DA_Drive_Stock"),
        TEXT("/Game/Ship/Parts/DA_Boosters_Stock.DA_Boosters_Stock"),
        TEXT("/Game/Ship/Parts/DA_Lights_Stock.DA_Lights_Stock"),
        TEXT("/Game/Ship/Parts/DA_LifeSupport_Stock.DA_LifeSupport_Stock"),
        TEXT("/Game/Ship/Parts/DA_Sensors_Stock.DA_Sensors_Stock"),
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
        UE_LOG(LogTemp, Warning, TEXT("DeepSpaceGameMode: no ship subsystem; parts not fitted."));
        return;
    }

    for (const TSoftObjectPtr<UShipModuleDataAsset>& SoftPart : StartingModules)
    {
        UShipModuleDataAsset* Part = SoftPart.LoadSynchronous();
        if (!Part)
        {
            UE_LOG(LogTemp, Warning, TEXT("DeepSpaceGameMode: could not load part %s."), *SoftPart.ToString());
            continue;
        }
        if (!Ship->FitPart(Part))
        {
            UE_LOG(LogTemp, Warning, TEXT("DeepSpaceGameMode: part %s was refused (no bay, or no id?)."),
                   *Part->ModuleId.ToString());
        }
    }
}
