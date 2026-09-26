#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
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
     * The drive corner's line: the jump in words, the course, and its bearing
     * from the ship's nose -- not from a free-looking head, and not in the
     * universe's axes -- or a dash with no course. Static and asked of its
     * owners, so a headless test reads exactly what the corner draws.
     */
    static FText DriveLineText(const UShipSubsystem& Ship, const UUniverseSubsystem* Universe);

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
     * edge is nearer than any world. Within FloorBand of FloorCm either way
     * it adds "DRIVE FLOOR": the drive settles there and closes no further,
     * so the number has stopped falling because this is as close as the
     * drive goes, not because something is wrong. Pure.
     */
    static FString AltitudeLine(double AltitudeCm, const FString& Surface, bool bEdge, double FloorCm, double FloorBand);

    /**
     * The altitude corner's line, asked of its owners: the nearest surface
     * from LocalSystem::NearestSurface -- the measure the drive's room is --
     * against the drive floor the ship is flying with, or a dash between
     * stars and where there is nothing near. Stores nothing, so a headless
     * test reads exactly what the corner draws.
     */
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
    UPROPERTY() TObjectPtr<UTextBlock> DriveLine;
    UPROPERTY() TObjectPtr<UTextBlock> MotionLine;
    UPROPERTY() TObjectPtr<UTextBlock> AltitudeReadout;

    ETarget Target = ETarget::Nothing;

    /** Eased so the dot grows into a target rather than snapping. */
    float Emphasis = 0.0f;
};
