#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UI/TargetMarker.h"
#include "Universe/StarSystem.h"
#include "ShipTargetOverlay.generated.h"

class APawn;
class APlayerController;
class UShipSubsystem;

/**
 * The camera the overlay projects through: where the eye is, which way it
 * looks, how big the view is in slate units, and the projection itself. In
 * play it is the player's camera and UWidgetLayoutLibrary's projection; a
 * headless test, which has no viewport, hands in a pinhole of its own, so
 * everything the overlay decides from a projection can be checked without
 * a renderer.
 *
 * Project answers false behind the camera, where a projection means
 * nothing, and otherwise writes the point's widget position (+X right, +Y
 * down), which may lie off the view.
 */
struct DEEPSPACE_API FTargetOverlayCamera
{
    FVector Location = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    FVector2D ViewSize = FVector2D::ZeroVector;
    TFunction<bool(const FVector& WorldPoint, FVector2D& OutWidget)> Project;
};

/**
 * The target's marks over the glass (system map spec, decision 7): teal
 * corner ticks round the marked world, wherever anyone aboard can see it
 * through the glass; an edge chevron, for the pilot, when it is off the
 * view; and, for the pilot, the prograde mark, an ink ring with three
 * ticks along the ship's velocity -- bracket on caret is *pointed at it*,
 * bracket on prograde is *going to it*.
 *
 * Drawn in HUD space from geometry, never from brightness, so a black disc
 * on the night side is bracketed like a lit one. What is drawn and where is
 * TargetMarker::Place's, pure and tested headless; this widget only asks the
 * ship, projects, traces the glass and paints. It holds nothing between
 * frames but what it last decided to paint.
 *
 * A child of the HUD's canvas, filling it, never hit-testable: the pointer
 * must reach the screens behind it.
 */
UCLASS()
class DEEPSPACE_API UShipTargetOverlay : public UUserWidget
{
    GENERATED_BODY()

public:
    UShipTargetOverlay(const FObjectInitializer& ObjectInitializer);

    /**
     * Whether Viewer is shown the target's mark at all: there is a viewer, a
     * target that resolves in Here, the system the HUD has in hand, and the
     * ship is not in the fold, when the sky is hidden. For anyone aboard:
     * who actually sees a bracket is then the glass's business, and who sees
     * a chevron the pilot's (TargetMarker::Place).
     */
    static bool ShowsTargetMark(const UShipSubsystem& Ship, const APawn* Viewer, const TOptional<FStarSystem>& Here);

    /**
     * Whether Viewer is shown the prograde mark: the pilot, with a target
     * that resolves in Here, out of the fold, moving at
     * TargetMarker::MinSpeed or more. Only with a target, because its
     * question is "will I get to what I marked" (ruling 5, the map spec's
     * sign-off 6).
     */
    static bool ShowsPrograde(const UShipSubsystem& Ship, const APawn* Viewer, const TOptional<FStarSystem>& Here);

    /** The world point the target is projected from: along its direction
     *  from the camera, as the nose caret is. Ship axes are world axes (ADR
     *  0005) and the proxy is scaled about the ship's origin, so a point
     *  taken from the camera lands on the drawn disc to under half a pixel
     *  (FSkyViewParams::NearProxy). */
    static FVector TargetWorldPoint(const FVector& CameraLocation, const FVector& ShipLocalDir);

    /** The same for the prograde mark: along the ship's velocity, taken into
     *  ship axes. Empty under TargetMarker::MinSpeed, where it is hidden. */
    static TOptional<FVector> ProgradeWorldPoint(const FVector& CameraLocation, const FVector& Velocity,
                                                 const FQuat& Orientation);

    /**
     * Once a frame, from the HUD's tick, with the system it has already
     * asked for: projects through Controller's camera and places the marks,
     * or hides them all -- with no controller, no camera, no viewport, or
     * nothing to show.
     */
    void PlaceFor(const UShipSubsystem& Ship, const TOptional<FStarSystem>& Here, APlayerController* Controller);

    /**
     * PlaceFor's decision with the camera handed in: the target's view from
     * the ship, projected, the glass traced from the camera along it
     * (TargetMarker::SeenThroughGlass, ignoring Viewer), and
     * TargetMarker::Place; the prograde mark projected. Camera null hides
     * everything. Public so a headless test can hand in a camera.
     */
    void PlaceFrom(const UShipSubsystem& Ship, const TOptional<FStarSystem>& Here, const FTargetOverlayCamera* Camera,
                   const APawn* Viewer);

    /** What was last decided for the target: a bracket, a chevron or none. */
    const FTargetMark& GetLastMark() const { return LastMark; }

    /** Where the prograde mark was last placed, widget space; empty when it
     *  was hidden. */
    const TOptional<FVector2D>& GetLastPrograde() const { return LastPrograde; }

protected:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                              const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                              int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
    /** Nothing to draw: the mark cleared and the widget collapsed. */
    void Hide();

    FTargetMark LastMark;
    TOptional<FVector2D> LastPrograde;
};
