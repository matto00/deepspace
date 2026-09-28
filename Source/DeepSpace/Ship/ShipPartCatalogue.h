#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShipPartCatalogue.generated.h"

class UShipModuleDataAsset;

/**
 * Every part there is, in Tools/ship_parts.json's order (wear and upgrades
 * decision 5). UShipSubsystem finds a part by id through this one list,
 * named by its CatalogueAsset ini line. That needs no asset-registry scan,
 * so it works the same under -nullrhi and in a bare test world, with no
 * game mode.
 *
 * Written by Tools/setup_ship_parts.py; never edit by hand.
 */
UCLASS(BlueprintType)
class DEEPSPACE_API UShipPartCatalogue : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts")
    TArray<TSoftObjectPtr<UShipModuleDataAsset>> Parts;
};
