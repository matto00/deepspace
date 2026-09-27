#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipScreen.h"
#include "ShipNavScreen.generated.h"

class UBoxComponent;
class UInteractableComponent;

/**
 * The chart: the starboard desk screen in the cockpit, in front of the chair
 * beside the helm. Choosing and engaging happen here; aiming happens at the
 * helm, which carries everything aiming needs (nav decisions 3 and 7).
 *
 * Its chair is a seat, not a lock (system map spec, decision 13): E at the
 * chart sits the player down with the view still their own, and E looking at
 * the chart, or at the map beside it, zooms that one.
 *
 * Visited once per jump, to choose. Two chairs side by side look like two
 * stations and are not: nothing here needs, or benefits from, someone in the
 * other chair (docs/vision.md, shared presence, not division of labour).
 *
 * It faces -X at yaw 0, like every fixture placement.resolve_mount yaws, and
 * its origin is the centre of the panel's face, so Tools/hauler_layout.py
 * places the panel itself rather than a casing it has to know the size of.
 */
UCLASS()
class DEEPSPACE_API AShipNavScreen : public AShipScreen
{
    GENERATED_BODY()

public:
    AShipNavScreen();

    UInteractableComponent* GetInteractable() const { return Interactable; }
    UBoxComponent* GetReach() const { return Reach; }

    /** Sitting here no longer frames the chart (decision 13): the chair sits
     *  beside the map too, and the seated player chooses. E, looking at
     *  either, zooms it. Drivable seated, as the map is: looked at and
     *  clicked unzoomed from its own chair and from the helm (ruling,
     *  2026-09-27). */
    virtual bool ZoomsOnSit() const override { return false; }
    virtual bool IsDrivableSeated() const override { return true; }
    virtual bool IsZoomableFromChartChair() const override { return true; }
    virtual FText GetZoomPrompt() const override;

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

    /**
     * What the reach trace finds when the eye is on the chart's rim rather
     * than its glass. Behind the panel's face and never in front of it: it
     * blocks the channel the pointer traces on, so a volume that enclosed the
     * panel would leave a screen that draws perfectly and cannot be clicked.
     * It also does not depend on the widget component, whose own collision
     * body exists only after BeginPlay.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chart")
    TObjectPtr<UBoxComponent> Reach;

    /** E sits the player down at it. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chart")
    TObjectPtr<UInteractableComponent> Interactable;

private:
    UFUNCTION()
    void HandleInteracted(AActor* InteractInstigator);

    /** Sizes the reach volume to the panel as it is now, so a chart widened
     *  per instance keeps a rim that matches it. */
    void FitReach();
};
