#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipScreen.generated.h"

class USceneComponent;
class UWidgetComponent;

/**
 * A screen that is a surface in the room, not a menu.
 *
 * Real Slate is rendered onto a quad in the world and driven by the player's
 * pointer (ADeepSpaceCharacter's UWidgetInteractionComponent). There is no
 * fullscreen takeover and no pause: walking away from a screen mid-edit is
 * allowed and means nothing, because the whole register of the game is that
 * you are aboard somewhere rather than in an interface.
 *
 * Abstract: subclasses choose the widget and the physical size of the panel.
 */
UCLASS(Abstract)
class DEEPSPACE_API AShipScreen : public AActor
{
    GENERATED_BODY()

public:
    AShipScreen();

    /**
     * Sets a widget component up as a ship panel: world space, opaque, the
     * right way round, and traceable by the pointer.
     *
     * Static, because the engineering console is not an AShipScreen -- it is
     * a console that happens to carry one -- and these settings are the
     * difference between a screen that can be used and one that is merely
     * visible. There must be exactly one place they are written down.
     */
    static void ConfigurePanel(UWidgetComponent* Panel, float WidthCm, const FVector2D& DrawSizePixels);

    UWidgetComponent* GetScreen() const { return Screen; }

    /**
     * Sizes the panel: how many centimetres wide the quad is, at the widget's
     * fixed pixel resolution. Called from the constructor and again on
     * construction so a moved or rescaled screen stays legible.
     */
    void SetPanelWidthCm(float WidthCm);

protected:
    virtual void OnConstruction(const FTransform& Transform) override;

    /** A bare root, so the panel can be offset without moving the actor. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Screen")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Screen")
    TObjectPtr<UWidgetComponent> Screen;

    /** Width of the panel in the world, cm. Height follows the draw size. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen")
    float PanelWidthCm = 60.0f;

    /** Pixels across the widget. Text is sized against this and the panel
     *  width together, never against the editor preview. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen")
    FVector2D DrawSizePixels = FVector2D(600.0f, 400.0f);
};
