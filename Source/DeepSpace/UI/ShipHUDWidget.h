#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Ship/ShipFlightState.h"
#include "Sky/SkySystem.h"
#include "ShipHUDWidget.generated.h"

class ADeepSpaceCharacter;
class APawn;
class UBorder;
class UCanvasPanel;
class UShipSubsystem;
class UTextBlock;
class UUniverseSubsystem;

/**
 * The HUD: a dot at the centre and four quiet readouts at the corners.
 *
 * Deliberately sparse. `docs/vision.md` asks for solitude and for a ship you
 * live in rather than an interface you operate, so this never grows an
 * objective, a warning, a counter that fills, or anything that tells the
 * player they are behind. The corners state facts about the ship and stop.
 *
 * Fields with no data yet read as rules rather than zeroes, because a dash
 * is honestly empty while "0" is a claim.
 *
 * All of it can be switched off with `ds.HUD 0`, which is the eventual
 * immersive mode: nothing here is required to play, and the crosshair is a
 * convenience rather than a mechanism.
 */
UCLASS()
class DEEPSPACE_API UShipHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    UShipHUDWidget(const FObjectInitializer& ObjectInitializer);

    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;

    /**
     * The jump corner's line: the jump in words, the course, and its bearing
     * from the ship's nose -- not from a free-looking head, and not in the
     * universe's axes -- or a dash with no course. Static and asked of its
     * owners, so a headless test reads exactly what the corner draws.
     *
     * Named for the jump, not the drive (flight-feel decision 7): the
     * bottom-left corner now speaks for the drive, and a top-right member
     * called DriveLine showing the jump would be a swap waiting to happen.
     */
    static FText JumpLineText(const UShipSubsystem& Ship, const UUniverseSubsystem* Universe);

    /**
     * A speed in the unit a person would say it in (flight-feel decision 7):
     * whole metres a second under a kilometre a second; kilometres a second
     * to a tenth under a hundred, then whole and grouped up to a hundredth of
     * light; then fractions of light to a hundredth under one; and "1 C",
     * which is as fast as the drive goes (ruling 1). Each unit takes over
     * exactly where the last would round up to its own threshold, as
     * AltitudeWords does, so "1000 M/S" and "100.0 KM/S" are never shown.
     *
     * A reading whose decimals are all zero drops them: "50 KM/S", "0.1 C".
     * That is what makes every notch of the drive lever read exactly as its
     * label (decision 3), so the settled ship and the lever it was set to say
     * the same words, and a speed the player sets is one they can come back
     * to. Pure.
     */
    static FString SpeedWords(double CmPerSecond);

    /**
     * The motion line (decision 7), as the two parts the corner draws: in
     * ink, the ship's speed, the live lever and what it asks for, and
     * SPOOLING DOWN while the ship eases out of the drive; dim, after a
     * separator, the other lever -- what F would go to, on screen before F
     * is pressed. A lever is always named by the speed it asks for, never as
     * a notch or a fraction of its travel: a gauge is a thing to fill.
     *
     *   Ink: "142 M/S · CRUISE 200 M/S"            Dim: " · DRIVE 1 C"
     *   Ink: "0.42 C · CRUISE 100 M/S · SPOOLING DOWN"   Dim: " · DRIVE 1 C"
     *
     * Holds no time at all: the corner has no destination to count down
     * to, and the live ETA is the target's (ruling 3). Pure.
     */
    struct FMotionWords
    {
        FString Ink;
        FString Dim;
    };
    static FMotionWords MotionLine(const FShipFlightState& Flight);

    /**
     * A distance in the unit a person would say it in: metres under a
     * kilometre, kilometres to a tenth under a hundred and whole ones under
     * ten thousand, then thousands of kilometres, then astronomical units
     * from a hundredth of one. Each unit takes over exactly where the last
     * would round up to its own threshold, so "1000 M" and "100.0 KM" are
     * never shown. Pure.
     */
    static FString AltitudeWords(double Cm);

    /**
     * The altitude line: how far the nearest surface is, and whose it is --
     * "212 KM ABOVE Kessa IV", or "3,400 AU TO THE EDGE" when the system's
     * edge is nearer than any world -- and one word for what the soft cap
     * is doing (decision 7): HOLDING OFF while it takes speed away, AT THE
     * FLOOR (AT THE EDGE, at the edge) while the ship is as low as it goes.
     * Hold is the one to show, ShownHold's answer. No colour, nothing that
     * blinks: a fact about the ship, stated like the others. Pure.
     */
    static FString AltitudeLine(double AltitudeCm, const FString& Surface, bool bEdge, EFlightHold Hold);

    /**
     * Which of the cap's words to show, from what the flight state says it
     * did: HOLDING OFF only while the cap holds the ship more than
     * HoldingOffShown below the live lever's speed. Measured against the
     * lever, not the eased position -- which follows the cap and so sits a
     * hair under it every substep -- so the word does not flicker at the
     * threshold. Pure.
     */
    static EFlightHold ShownHold(EFlightHold Hold, double HeldFraction);

    /** How far below the lever the cap must hold the ship for the corner to
     *  say so: 5%, less than anyone can see as a difference in the speed. */
    static constexpr double HoldingOffShown = 0.05;

    /**
     * The altitude corner's line, asked of its owners: the nearest surface
     * in Here -- the system the ship is in, from LocalSystem::Here, the
     * measure the room is -- and what the cap is doing, or a dash between
     * stars and where there is nothing near.
     * Stores nothing, so a headless test reads exactly what the corner draws.
     */
    static FText AltitudeLineText(const UShipSubsystem& Ship, const FSkySystem& Here);

    /** The same, asking LocalSystem::Here itself. */
    static FText AltitudeLineText(const UShipSubsystem& Ship);

    /**
     * The nose caret (nav decision 3): a ring on the HUD where the ship's nose
     * meets the sky, which sits on the teal course marker exactly when the
     * ship is aligned, however the pilot's head is turned. Built with the
     * layout and found again by this name, so it is not a member.
     */
    static const FName NoseCaretName;

    /** Whether Viewer sees the caret: only while they fly the ship, with a
     *  course plotted, and not between stars, where there is no marker to
     *  put it on. Asked of the ship every frame. */
    static bool ShowsNoseCaret(const UShipSubsystem& Ship, const APawn* Viewer);

    /**
     * The world point the caret is projected from: along the ship's nose from
     * the camera. The ship is the origin and its transform is identity (ADR
     * 0005), so the nose is world +X wherever the ship points, and a point
     * taken from the camera has no parallax -- the caret is a direction, like
     * the marker on the dome it is put on.
     */
    static FVector NoseCaretWorldPoint(const FVector& CameraLocation);

    /**
     * Shows the caret where the nose projects into this HUD's view, or hides
     * it: with no course, no pilot, no camera to project through, or the
     * nose off the edge of the view. Called every frame by NativeTick, and
     * public so a headless test can ask the built widget what it decided.
     */
    void PlaceNoseCaret(const UShipSubsystem* ShipState);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    /** What the dot is currently over; it is the only thing that animates. */
    enum class ETarget : uint8
    {
        Nothing,
        Interactable,
        Screen,
    };

    UCanvasPanel* BuildLayout();
    UTextBlock* MakeReadout(const FText& Content, const FLinearColor& Colour, float Size);
    void PlaceCorner(UWidget* Widget, const FVector2D& Anchor, const FVector2D& Offset);
    void SetTarget(ETarget NewTarget);

    ADeepSpaceCharacter* Player() const;
    UShipSubsystem* Ship() const;

    UPROPERTY() TObjectPtr<UBorder> Dot;
    UPROPERTY() TObjectPtr<UTextBlock> Prompt;
    UPROPERTY() TObjectPtr<UTextBlock> ShipLine;
    UPROPERTY() TObjectPtr<UTextBlock> PlaceLine;
    UPROPERTY() TObjectPtr<UTextBlock> PowerLine;
    UPROPERTY() TObjectPtr<UTextBlock> JumpLine;
    UPROPERTY() TObjectPtr<UTextBlock> MotionInk;
    UPROPERTY() TObjectPtr<UTextBlock> MotionDim;
    UPROPERTY() TObjectPtr<UTextBlock> AltitudeReadout;

    ETarget Target = ETarget::Nothing;

    /** Eased so the dot grows into a target rather than snapping. */
    float Emphasis = 0.0f;
};
