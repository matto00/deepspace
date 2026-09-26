#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "UI/SystemMapLayout.h"
#include "SystemMapView.generated.h"

/**
 * The orrery itself: the star, the rings, the worlds, the target ring and the
 * ship, painted with lines and rounded boxes in NativePaint. No texture and
 * no child widgets, so what it draws is exactly SystemMap's arithmetic.
 *
 * A view and nothing more. It is handed what to draw by USystemMapWidget --
 * the layout when the system changes, the ship and the target every frame --
 * and asks the ship for nothing. A click is turned into a world by the pure
 * SystemMap::Pick and passed back through OnPicked, so the orrery and the
 * rows end at one seam, USystemMapWidget::SelectWorld.
 *
 * Its own space is the layout's: one unit a panel pixel, the centre at
 * FMapPixels::Centre. It must be given a 256 x 256 slot.
 */
UCLASS()
class DEEPSPACE_API USystemMapView : public UUserWidget
{
    GENERATED_BODY()

public:
    USystemMapView(const FObjectInitializer& ObjectInitializer);

    /** The drawing that does not move: rings and worlds, and each world's
     *  colour and the star's. Replaced whenever the map's cache is. */
    void SetDrawing(const SystemMap::FMapLayout& Layout, const FLinearColor& StarColour,
                    const TArray<FLinearColor>& WorldColours);

    /** Draw nothing at all: between stars. */
    void ClearDrawing();

    bool HasDrawing() const { return bHasDrawing; }
    const SystemMap::FMapLayout& GetLayout() const { return Layout; }

    /** This frame's ship glyph, or none. */
    void SetShip(const TOptional<SystemMap::FMapShip>& InShip) { Ship = InShip; }

    /** This frame's target, as an orbit of the system drawn, or none. */
    void SetTarget(const TOptional<int32>& Orbit) { TargetOrbit = Orbit; }

    /**
     * What a click at Point (the view's own pixels) does: picks the world
     * there, if any, and passes it to OnPicked. The whole of the orrery's
     * input once a press has been released, public so a test can click
     * without Slate -- headless, nothing is painted and the hit-test grid
     * is empty.
     */
    TOptional<int32> ClickAt(const FVector2D& Point);

    /** Where a click that picked a world goes: USystemMapWidget::SelectWorld. */
    TDelegate<void(int32)> OnPicked;

    /** The world colours as drawn: the window's hue, lifted to be seen on
     *  the panel. Pure, for the test that holds the map's world to the
     *  window's. */
    static FLinearColor PanelColour(const FLinearColor& SkyColour);

protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                              const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                              int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

    /** A click is a press and a release on the orrery, as a row's button
     *  is: the press arms it, the release picks where it lands, and leaving
     *  the orrery between the two lets it go. Picking on the press alone
     *  made the dots fire before the rows beside them did. */
    virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
    virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

    /** A control, so the pointer counts it as one and the HUD's dot turns
     *  teal over it. */
    virtual bool NativeIsInteractable() const override { return true; }

private:
    SystemMap::FMapLayout Layout;
    FLinearColor StarColour = FLinearColor::White;
    TArray<FLinearColor> WorldColours;
    bool bHasDrawing = false;

    TOptional<SystemMap::FMapShip> Ship;
    TOptional<int32> TargetOrbit;

    /** Pressed on the orrery and not yet released or left: input state,
     *  the pointer's and not the ship's. */
    bool bPressed = false;
};
