#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ShipScreenWidget.generated.h"

class UBorder;
class UProgressBar;
class UShipSubsystem;
class UTextBlock;
class UVerticalBox;
class UWidget;

/**
 * Base for every screen aboard the ship.
 *
 * The widget tree is built in C++, in BuildScreen, rather than in a Widget
 * Blueprint: screens carry gameplay-visible logic, and logic in a .uasset is
 * invisible to git and to review (ADR 0002). Nothing here reads a stored copy
 * of ship state -- Ship() is asked afresh, every frame, so that two screens
 * showing the same allocation cannot disagree.
 */
UCLASS(Abstract)
class DEEPSPACE_API UShipScreenWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /**
     * The palette every surface in the ship shares -- screens and the HUD
     * alike -- so one change re-tints all of them. Public because the HUD is
     * not a screen but must not invent its own colours.
     */
    static const FLinearColor Panel;
    static const FLinearColor Ink;
    static const FLinearColor Dim;
    static const FLinearColor Accent;

    UShipScreenWidget(const FObjectInitializer& ObjectInitializer);

    /** The ship, or nullptr outside a world. Never cached. */
    UFUNCTION(BlueprintPure, Category = "Screen")
    UShipSubsystem* Ship() const;

protected:
    /**
     * Builds the tree and returns its root. Called once, lazily, the first
     * time the widget is realised.
     */
    virtual UWidget* BuildScreen();

    virtual TSharedRef<SWidget> RebuildWidget() override;

    // -- palette -------------------------------------------------------
    // The ship's own colours: teal accent on a near-black panel, matching
    // the emissive trim Tools/build_hauler.py paints the hull with.


    /**
     * Point sizes are chosen against the *real* panel: a 60 cm console
     * rendering 600 px shows 10 px per cm, so 28 px of text is 2.8 cm tall
     * and legible from the 100-150 cm a player actually stands at. Sizing
     * against the editor preview is how world screens end up unreadable.
     */
    static constexpr float TitleSize = 34.0f;
    static constexpr float BodySize = 28.0f;

    UTextBlock* MakeText(const FText& Content, float Size, const FLinearColor& Colour);
    UBorder* MakePanel(UWidget* Content);
    UVerticalBox* MakeColumn();
    UProgressBar* MakeBar();
};
