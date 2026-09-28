#pragma once

#include "CoreMinimal.h"
#include "UI/ShipScreenWidget.h"
#include "Ship/ShipParts.h"
#include "EngineeringConsoleWidget.generated.h"

class UButton;
class UShipModuleDataAsset;
class UTextBlock;

/**
 * The engineering console's screen: the light switch, and one nameplate
 * per fitted part (wear and upgrades decision 9).
 *
 * Figures, never judgements. Each plate is BAY, the part's name, the one
 * number its bay's row names -- read from the part itself, never from a
 * console variable, the flight's limits, the live power split or wear -- and
 * its words, which are character and history, never quality. Nothing here
 * totals, compares or ranks: no percentage, no tier, no "upgraded", no total
 * drawn and no headroom (the lived-in spec's decision 11), and no line for
 * an empty slot, which would be a gap to fill. A screen that invents an
 * optimum turns living with the ship into a puzzle with an answer
 * (docs/vision.md, the anti-chore principle).
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

    /** Every fitted part's nameplate, one line each, in bay order. */
    FText GetReadoutText() const;

    /** Puts the plates and the switch's words on the screen: what NativeTick
     *  does every frame, public so a test renders without painting. */
    void RefreshFromShip();

    /** What the plates' text block shows, as last refreshed. */
    FText GetShownText() const;

    /** One part's plate: its bay's label, its name, its one figure and unit
     *  (none for an aux part), its words, in columns two spaces apart. */
    static FString Nameplate(EShipBay Bay, const UShipModuleDataAsset& Part);

    /** Six plates across a 600-pixel panel: smaller than the body text.
     *  Whether it reads at the console is a playtest question. */
    static constexpr float PlateSize = 13.0f;

protected:
    virtual UWidget* BuildScreen() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:
    UPROPERTY()
    TObjectPtr<UTextBlock> LightsLabel;

    UPROPERTY()
    TObjectPtr<UTextBlock> Readout;
};
