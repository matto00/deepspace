#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShipModuleDataAsset.generated.h"

/**
 * Describes one piece of ship equipment.
 *
 * Milestone 1 defines only a few of these and uses little of the data. The
 * shape existing is the point: upgrades and procedural generation should later
 * write data, not code.
 */
UCLASS(BlueprintType)
class DEEPSPACE_API UShipModuleDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Stable identifier used as the key in power bookkeeping. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FName ModuleId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FText DisplayName;

    /** Continuous power draw in watts while installed. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    float PowerDraw = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    TSoftObjectPtr<UStaticMesh> Mesh;
};
