#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Ship/ShipFlightState.h"
#include "Sky/SkySystem.h"
#include "Universe/StarSystem.h"
#include "ShipHUDWidget.generated.h"

class ADeepSpaceCharacter;
class APawn;
class APlayerController;
class UBorder;
class UCanvasPanel;
class UShipSubsystem;
class UTextBlock;
class UShipTargetOverlay;
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
     * Everything NativeTick does, for Controller's view: the dot, the prompt,
     * every corner, the caret and the target's marks. NativeTick is this
     * with the owning player and nothing else, so a headless test -- which
     * cannot tick a widget that was never painted -- drives exactly the path
     * play does, and with no controller sees the overlay hide. The caret
     * still projects through the owning player, as PlaceNoseCaret always
     * does.
     */
    void Refresh(float DeltaSeconds, APlayerController* Controller);

    /**
     * The jump corner's line: the jump in words, the course, and its bearing
     * from the ship's nose -- not from a free-looking head, and not in the
     * universe's axes -- or a dash with no course. Static and asked of its
     * owners, so a headless test reads exactly what the corner draws.
     *
     * Named for the jump, not the drive (flight-feel decision 7): the
     * bottom-left corner now speaks for the drive, and a top-right member
     * called DriveLine showing the jump would be a swap waiting to happen.
     *
     * A course to a world in this system (system map decision 12) is named
     * as the world, "JUMP READY · Kessa II · dead ahead", in the jump's
     * cone words, since it is the jump's cone; its fold reads "IN THE FOLD
     * · Kessa II", never BETWEEN STARS, which it is not.
     */
    static FText JumpLineText(const UShipSubsystem& Ship, const UUniverseSubsystem* Universe);

    /** The power corner, top right: the reactor's watts, and no SPARE or
     *  total drawn (wear and upgrades, sign-off 11 and the plan's ruling).
     *  Static, so a test reads what the corner draws. */
    static FText PowerLineText(const UShipSubsystem& Ship);

    /**
     * The place line, top left: the system and its star's class, as
     * NavText::Place words them, from Here, the system the HUD asked for
     * this frame; in a star jump's fold BETWEEN STARS; in an in-system
     * fold IN THE FOLD, since the ship has not left the system and is not
     * between stars (decision 12). A dash where there is no system. Static
     * and asked of its owners, so a test reads what the corner draws.
     */
    static FText PlaceLineText(const UShipSubsystem& Ship, const TOptional<FStarSystem>& Here);

    /**
     * The target readout, under the jump line (system map decision 6):
     * TargetMarker::Line of the ship's own view of the target -- name,
     * bearing from the nose, distance to the surface, a live ETA or the
     * altitude it will pass at, NIGHT SIDE -- the one string the map prints
     * too (DeepSpace.Ship.ScreensAgree). Empty, not a dash, with no target,
     * one that names nothing in Here, and in the fold: the corner does not
     * grow a placeholder for something the player never asked for.
     */
    static FText TargetLineText(const UShipSubsystem& Ship, const TOptional<FStarSystem>& Here);

    /** The target readout and the overlay, built with the layout and found
     *  again by these names, as the caret is. */
    static const FName TargetLineName;
    static const FName TargetOverlayName;

    /**
     * A speed in the unit a person would say it in (flight-feel decision 7):
     * whole metres a second under a kilometre a second; kilometres a second
     * to a tenth under a hundred, then whole and grouped up to a tenth of
     * light, so every drive notch to 20,000 km/s reads in kilometres; then
     * fractions of light to a hundredth, "0.1 C" being as fast as the drive
     * goes (the 2026-09-27 ruling). Each unit takes over
     * exactly where the last would round up to its own threshold, as
     * AltitudeWords does, so "1000 M/S" and "100.0 KM/S" are never shown.
     *
     * This is a label's form: decimals that are all zero are dropped, "50
     * KM/S", "0.1 C", which is what makes every notch of the drive lever
     * read exactly as its label (decision 3). Levers are named in it. Pure.
     */
    static FString SpeedWords(double CmPerSecond);

    /**
     * The ship's own speed as the corner reads it: SpeedWords, but keeping
     * its decimals -- "13.0 KM/S", "0.10 C" -- so the reading does not
     * change length each time it passes a round number, and what is drawn
     * after it does not slide while the pilot aims by it. Only when it reads
     * what the lever asks for, LeverCmPerSecond, does it drop them: settled,
     * the ship and the lever say the same words, and a speed the player
     * sets is one they can come back to. Pure.
     */
    static FString SpeedReading(double CmPerSecond, double LeverCmPerSecond);

    /**
     * The motion line (decision 7), as the two parts the corner draws: in
     * ink, the ship's speed, the live lever and what it asks for, and
     * SPOOLING DOWN while the ship eases out of the drive; dim, after a
     * separator, the other lever -- what F would go to, on screen before F
     * is pressed. A lever is always named by the speed it asks for, never as
     * a notch or a fraction of its travel: a gauge is a thing to fill.
     *
     *   Ink: "7.1 KM/S · CRUISE 20 KM/S"           Dim: " · DRIVE 0.1 C"
     *   Ink: "14,142 KM/S · CRUISE 141 M/S · SPOOLING DOWN"   Dim: " · DRIVE 0.1 C"
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

    /** The motion line asked of the ship, which is what the corner draws: a
     *  dash in ink between stars, where the ship is folded, not flown, and
     *  otherwise MotionLine. Stores nothing. */
    static FMotionWords MotionLineOf(const UShipSubsystem& Ship);

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

    /** Whether Viewer sees the caret: only while they fly the ship, not in
     *  the fold, and whenever there is something to aim at -- a course, to
     *  a star or a world, or a target that resolves in Here, the system the
     *  HUD asked for this frame (system map decision 7). Without it a pilot
     *  in a system they have just arrived in has a bracket showing where
     *  the world is from their head, and nothing showing where the ship
     *  points. Asked of the ship every frame. */
    static bool ShowsNoseCaret(const UShipSubsystem& Ship, const APawn* Viewer, const TOptional<FStarSystem>& Here);

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
     * it: with nothing to aim at, no pilot, no camera to project through, or
     * the nose off the edge of the view. Called every frame by NativeTick,
     * with the system it has already asked for, and public so a headless
     * test can ask the built widget what it decided.
     */
    void PlaceNoseCaret(const UShipSubsystem* ShipState, const TOptional<FStarSystem>& Here);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    /** SpeedWords, with decimals that are all zero dropped or kept. */
    static FString SpeedWordsKept(double CmPerSecond, bool bDropZeros);

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

    UShipSubsystem* Ship() const;

    UPROPERTY() TObjectPtr<UBorder> Dot;
    UPROPERTY() TObjectPtr<UTextBlock> Prompt;
    UPROPERTY() TObjectPtr<UTextBlock> ShipLine;
    UPROPERTY() TObjectPtr<UTextBlock> PlaceLine;
    UPROPERTY() TObjectPtr<UTextBlock> PowerLine;
    UPROPERTY() TObjectPtr<UTextBlock> JumpLine;
    UPROPERTY() TObjectPtr<UTextBlock> TargetLine;
    UPROPERTY() TObjectPtr<UShipTargetOverlay> Overlay;
    UPROPERTY() TObjectPtr<UTextBlock> MotionInk;
    UPROPERTY() TObjectPtr<UTextBlock> MotionDim;
    UPROPERTY() TObjectPtr<UTextBlock> AltitudeReadout;

    ETarget Target = ETarget::Nothing;

    /** Eased so the dot grows into a target rather than snapping. */
    float Emphasis = 0.0f;
};
