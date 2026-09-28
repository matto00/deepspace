#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DeepSpaceGameMode.generated.h"

class UShipModuleDataAsset;

UCLASS()
class DEEPSPACE_API ADeepSpaceGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ADeepSpaceGameMode();

    /** The loadout the ship starts with, as play would install it. */
    const TArray<TSoftObjectPtr<UShipModuleDataAsset>>& GetStartingModules() const { return StartingModules; }

protected:
    virtual void BeginPlay() override;

    /**
     * The parts the ship starts with, one per core bay (wear and upgrades
     * decision 2), fitted through UShipSubsystem::FitPart. Data, not code:
     * Tools/setup_ship_parts.py writes the Blueprint's list, which overrides
     * this one. Soft pointers so an unloaded part costs nothing until the
     * level starts.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Ship")
    TArray<TSoftObjectPtr<UShipModuleDataAsset>> StartingModules;
};
