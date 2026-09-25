#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipScreen.h"
#include "ShipLaptop.generated.h"

class UStaticMeshComponent;

/**
 * The laptop on the galley table: a base, a lid, and a screen on the lid.
 *
 * Deliberately somewhere you sit down rather than somewhere you report for
 * duty. The allocation it edits is a preference, not a station -- there is
 * no reason to come here on any schedule, and nothing changes if you never
 * do (docs/vision.md, the anti-chore principle).
 *
 * The actor faces -X at yaw 0, the same convention every wall-mounted
 * fixture in the ship follows, so Tools/placement.py can yaw it like any
 * other panel.
 */
UCLASS()
class DEEPSPACE_API AShipLaptop : public AShipScreen
{
    GENERATED_BODY()

public:
    AShipLaptop();

    /**
     * Scales and places the base, the lid and the panel from the meshes
     * actually assigned. Callable from the editor because the build script
     * assigns the meshes after spawning, and nothing re-runs construction
     * for it.
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "Laptop")
    void FitParts();

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Laptop")
    TObjectPtr<UStaticMeshComponent> Base;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Laptop")
    TObjectPtr<UStaticMeshComponent> Lid;

private:
    /**
     * Scales a box mesh to a real size and puts its centre where we asked.
     *
     * Never assume a pivot: SM_Cube's is at its minimum corner, SM_ChamferCube's
     * at its centre. Measuring the bounds and correcting is what stops a
     * laptop hanging half off the table, which is the same bug that put every
     * wall in milestone 1 half its own size from its floor.
     */
    static void FitBox(UStaticMeshComponent* Component, const FVector& SizeCm, const FVector& Centre);
};
