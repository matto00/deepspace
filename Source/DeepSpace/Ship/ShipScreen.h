#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
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

    /**
     * Where a player sits to use this screen, and which way they face:
     * square on, in front of the panel, on the floor under the seat. The
     * floor, not the cushion: the sitting idle is posed against a seat
     * anchored on the floor, as the helm's is, and lifts the hips onto the
     * chair itself -- so no screen has a seat height to tune. Derived from
     * the panel's own transform so a screen that moves takes its seat with
     * it.
     */
    FTransform GetUseTransform() const;

    /**
     * World Z of the floor under the seat, cm: the use transform's. What
     * the body sits on and what getting up from this screen stands on --
     * not the height the feet were at when the player sat, which is the top
     * of whatever they had climbed.
     */
    double GetUseFloorZ() const;

    /**
     * Where the camera goes while the screen is in use -- close enough that
     * the panel fills the view and its text is legible, which at a laptop's
     * size it is not from across a table.
     */
    FTransform GetViewTransform() const;

    /**
     * The field of view that shows a whole panel from a seat in front of it:
     * FramedSizeCm across and high, seen square on from DistanceCm, with
     * MarginFraction of the frame left clear at each edge of whichever axis
     * the panel fills first.
     *
     * Pure, so framing can be checked without a viewport. It takes the rest
     * of what the engine uses to turn a camera's field of view into a view,
     * because that is where the fit is won or lost: under MaintainYFOV (the
     * engine's default) the number is horizontal *at the camera's aspect*,
     * the vertical angle is what is kept, and a window narrower than the
     * camera loses width -- so a fixed angle that frames a panel at 16:9
     * cuts its sides off at 4:3. Returns degrees, for SetFieldOfView.
     */
    static float FitFieldOfView(const FVector2D& FramedSizeCm, float DistanceCm, float ViewportAspect,
                                EAspectRatioAxisConstraint Constraint, float CameraAspect, float MarginFraction);

    /** The panel's face with its bezel, cm: what a seated view must show whole. */
    FVector2D GetFramedSizeCm() const;

    /** How far the eyes sit from the panel while using it, cm. */
    float GetViewDistanceCm() const { return ViewDistanceCm; }

    /** The field of view to use this screen through, fitted to the view
     *  described, with the margin ds.Screen.FrameMargin. */
    float GetUseFieldOfView(float ViewportAspect, EAspectRatioAxisConstraint Constraint, float CameraAspect) const;

    /** True if a player can sit down at this screen at all. */
    UFUNCTION(BlueprintPure, Category = "Screen")
    bool IsUsable() const { return bUsable; }

    // What a seat may do with a screen (system map spec, decisions 2 and 13).
    // Each is a class decision, never a per-instance one: none is reflected,
    // so no edit to a placed actor in the level can change it, and the
    // character's Blueprint layout does not depend on them.

    /**
     * Whether the view-aimed pointer reaches this screen from a seat -- the
     * helm, or the chart chair while nothing is zoomed. Only a screen meant
     * to be glanced at and touched while flying says yes: the map. The chart
     * is two metres from the helm's eye, inside the hands' reach, and sized
     * to be read from 60 cm in its own chair; it says no, so nothing in the
     * level can make it drivable from the helm.
     */
    virtual bool IsDrivableSeated() const { return false; }

    /**
     * Whether sitting at this screen frames it at once. True by default: the
     * laptop has one screen and a bench in front of it, and nothing else to
     * look at from there. The chart says no -- its chair is a seat beside two
     * screens, and the seated player chooses which to zoom.
     */
    virtual bool ZoomsOnSit() const { return true; }

    /** Whether E, from the chart chair with this screen in view, frames it:
     *  the chart and the map. */
    virtual bool IsZoomableFromChartChair() const { return false; }

    /** What E's prompt names when it would zoom this screen from the chart
     *  chair: "Chart", "Map". Empty for a screen that is not zoomable. */
    virtual FText GetZoomPrompt() const { return FText::GetEmpty(); }

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

    /**
     * The frame round the glass, cm: X on each side, Y above and below. The
     * casing the panel is set in. Framing includes it, because a screen
     * whose edge is cut off by the view reads as a screen too big for the
     * room rather than one you are sitting at.
     *
     * Per axis because casings are not even: the laptop's lid is 2 cm wider
     * than its glass each side and 1.3 cm taller top and bottom. One figure
     * for both overstated the lid's height -- and height is the axis every
     * screen here fills first, being squarer than the window.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen")
    FVector2D BezelCm = FVector2D(1.0f, 1.0f);

    /** Whether E sits the player down at this screen. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen")
    bool bUsable = false;

    /** How far in front of the panel the player sits, cm. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen")
    float UseDistanceCm = 62.0f;

    /** How far the eyes sit from the panel while using it, cm. Close: this
     *  is leaning in to read something, not looking across a room. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Screen")
    float ViewDistanceCm = 38.0f;
};
