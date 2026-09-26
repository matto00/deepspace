#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipDressingKeepOut.generated.h"

/**
 * A box of the ship no clutter may ever touch: a door's keep-clear zone,
 * the console's, the corridor's slide run, the crawlway.
 *
 * validate_hauler.py checked these for furniture, but it cannot see clutter,
 * which is spawned at runtime. So the guard moved into the generator:
 * Tools/build_hauler.py exports each zone as one of these, tagged
 * Dress.KeepOut, and ShipDressing::Dress refuses any candidate that touches
 * one. The actor's location is the box's centre, unturned; Size its full
 * extent in world axes.
 */
UCLASS()
class DEEPSPACE_API AShipDressingKeepOut : public AActor
{
    GENERATED_BODY()

public:
    AShipDressingKeepOut();

    /** Which zone, for a human reading ds.Dress.Describe. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    FName Reason;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    FVector Size = FVector::ZeroVector;

    /** The zone, world. */
    FBox GetBox() const;
};
