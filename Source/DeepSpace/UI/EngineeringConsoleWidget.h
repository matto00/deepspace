#pragma once

#include "CoreMinimal.h"
#include "UI/ShipScreenWidget.h"
#include "EngineeringConsoleWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * The engineering console's screen: the light switch, and what the reactor
 * is doing.
 *
 * Figures, not judgements. It shows watts drawn and watts spare, because
 * those are facts about the ship. It shows no efficiency, no percentage of
 * potential and no target, because the moment a screen invents an optimum
 * the allocation stops being a way of living and becomes a puzzle with an
 * answer (docs/vision.md, the anti-chore principle).
 */
UCLASS()
class DEEPSPACE_API UEngineeringConsoleWidget : public UShipScreenWidget
{
    GENERATED_BODY()

public:
    /** The screen's light switch. Public because a test drives it, and
     *  because it is exactly what the console's E key does. */
    UFUNCTION()
    void ToggleLights();

    /** What the readout currently says. */
    FText GetReadoutText() const;

protected:
    virtual UWidget* BuildScreen() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:

    UPROPERTY()
    TObjectPtr<UTextBlock> LightsLabel;

    UPROPERTY()
    TObjectPtr<UTextBlock> Readout;
};
