#pragma once

#include "CoreMinimal.h"
#include "UI/ShipScreenWidget.h"
#include "PointerTestWidget.generated.h"

class UButton;
class USlider;
class UTextBlock;
class UProgressBar;

/**
 * Scaffolding for the in-world pointer, and nothing else.
 *
 * A button and a slider that visibly respond, so that the question "does
 * pointing at a thing in the room feel like pointing at a thing in the room"
 * can be answered before any power UI exists to confuse it. Replaced by the
 * real allocation editor; it carries no ship state and nothing depends on it.
 */
UCLASS()
class DEEPSPACE_API UPointerTestWidget : public UShipScreenWidget
{
    GENERATED_BODY()

protected:
    virtual UWidget* BuildScreen() override;

private:
    UFUNCTION()
    void HandleClicked();

    UFUNCTION()
    void HandleSlid(float Value);

    UPROPERTY()
    TObjectPtr<UTextBlock> Readout;

    UPROPERTY()
    TObjectPtr<UProgressBar> Bar;

    int32 Clicks = 0;
    float Slid = 0.0f;
};
