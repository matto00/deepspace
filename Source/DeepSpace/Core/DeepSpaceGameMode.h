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

protected:
    virtual void BeginPlay() override;

    /**
     * Equipment the ship starts with. Data, not code: a Blueprint subclass can
     * change the loadout without touching C++, and a real ship-configuration
     * system later replaces this by writing the same list. Soft pointers so an
     * unloaded module costs nothing until the level starts.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Ship")
    TArray<TSoftObjectPtr<UShipModuleDataAsset>> StartingModules;
};
