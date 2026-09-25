#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "ShipHUDWidget.generated.h"

class ADeepSpaceCharacter;
class UBorder;
class UCanvasPanel;
class UShipSubsystem;
class UTextBlock;

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
    UPROPERTY() TObjectPtr<UTextBlock> HoldLine;

    ETarget Target = ETarget::Nothing;

    /** Eased so the dot grows into a target rather than snapping. */
    float Emphasis = 0.0f;
};
