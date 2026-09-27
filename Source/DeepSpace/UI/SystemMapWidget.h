#pragma once

#include "CoreMinimal.h"
#include "UI/ShipScreenWidget.h"
#include "UI/SystemMapLayout.h"
#include "Universe/StarSystem.h"
#include "Universe/UniversePosition.h"
#include "SystemMapWidget.generated.h"

class UButton;
class UShipSubsystem;
class USystemMapView;
class UTextBlock;

/**
 * The system map, on the middle cockpit screen: the system the ship is in,
 * seen from above -- the star, every world on its orbit, the ship -- and a
 * row per world with what it is and how far its surface is. Read and clicked
 * from the helm without getting up (system map spec, decisions 1-4).
 *
 * It stores nothing it could ask for. The ship's position and heading, the
 * target and the course are asked of UShipSubsystem every frame. What costs a
 * generated system -- the rings, the dots, each world's position and radius
 * -- is a cache of its own drawing, keyed on exactly what that drawing
 * depends on (FAskedAt): the id of the system the ship is in, whether it is
 * between stars, the priors, and the standoff the rim is fitted to. Not the
 * ship's position: the layout does not depend on where the ship is, and
 * nothing orbits, so the cache is exact. The ship's glyph, the row
 * distances, the target line and the footer are recomputed from it every
 * frame, and none of that generates anything.
 *
 * Its band carries the target line (decision 6, with the live ETA) and one
 * button, the in-system jump's (decision 12): "Jump here" plots the target
 * as the course and engages, in one press, so the in-system jump needs no
 * second screen; "Stand down" while the course is the target;
 * disabled as "Near enough to fly" inside the target's reach; absent with
 * no target.
 *
 * Laid out for its 600 x 424 draw size, which is what the panel spans at the
 * helm: every size here is a helm pixel (decision 1).
 */
UCLASS()
class DEEPSPACE_API USystemMapWidget : public UShipScreenWidget
{
    GENERATED_BODY()

public:
    /** A row per world, as many as procgen can make. */
    static constexpr int32 RowCount = GenGuarantees::MaxPlanets;

    /** Pulls the map back into line with the ship. Called every frame;
     *  public so a test can drive the screen without Slate painting. */
    void RefreshFromShip();

    /**
     * What pressing world Orbit does, on the orrery or on its row: target it,
     * or let it go if it is the target (SystemMap::Select, the chart's rule
     * for worlds). The one seam both pick paths end at. Nothing for an orbit
     * this system lacks, between stars, or when the drawing the press was
     * made on has just been replaced: Orbit is an index into what was on the
     * glass, and a click that lands in the frame the system changes must not
     * pick the same index of a system the player never saw.
     */
    void SelectWorld(int32 Orbit);

    /** What the band's button does: "Jump here" plots the target and
     *  engages; "Stand down" clears the course. The button's handler. */
    void PressJump();

    /** How many times the map has generated its drawing. A test's measure of
     *  what a frame costs; nothing on the glass. */
    int32 GetLayoutAsked() const { return LayoutAsked; }

    /** The orrery, for a test to click and read. */
    USystemMapView* GetView() const { return View; }

    /** How many rows are showing: one per world, none between stars. */
    int32 GetShownRowCount() const;

    /** What row Index shows, its columns joined by NavText::Separator; empty
     *  for a row that is not showing. The target's row starts with the
     *  chart's mark. */
    FText GetRowText(int32 Index) const;

    /** Whether row Index can be pressed. */
    bool IsRowEnabled(int32 Index) const;

    /** Presses row Index as the pointer would: its button's click. */
    void PressRow(int32 Index);

    /** The title's right-hand side: the system, as the chart names it. */
    FText GetTitleText() const;

    /** The footer: between stars, an empty system, a ship held at an edge of
     *  the map or off the plane; empty otherwise. */
    FText GetFooterText() const;

    /** The target line (decision 6): TargetMarker::Line of the ship's own
     *  view of it, the string the HUD prints. Empty with no target, one
     *  that names nothing here, and in the fold. */
    FText GetTargetText() const;

    /** The band's button, as the glass shows it: whether it is there, its
     *  words, and whether it can be pressed. */
    bool IsJumpButtonShown() const;
    FText GetJumpButtonText() const;
    bool IsJumpButtonEnabled() const;

    /** Presses the band's button as the pointer would: its click, if it is
     *  there and can be pressed. */
    void PressJumpButton();

    /**
     * The room the footer's row keeps at its right for the band's button,
     * slate units: the widest button, "Near enough to fly" at the footer's
     * size with 8 px of padding a side, which Slate measures at 168 px, and
     * a 10 px gap, rounded up. The footer wraps at the rest of the row
     * (600 - 2 x 6 - JumpReserve, 408 px).
     */
    static constexpr float JumpReserve = 180.0f;

    /** The least gap kept between the footer and the button, slate units. */
    static constexpr float JumpGap = 10.0f;

    /** For tests: what Slate lays the footer and the band's button out at,
     *  slate units, measured by a prepass. Fonts are measured without a
     *  renderer, so this holds under -nullrhi, which cannot paint. */
    FVector2D MeasureFooter() const;
    FVector2D MeasureJumpButton() const;

    /** The layout as drawn, for a test to hold the orrery to it. */
    const SystemMap::FMapLayout* GetLayout() const;

protected:
    virtual UWidget* BuildScreen() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:
    // UButton::OnClicked carries no payload, so each row has its own
    // handler, as on the chart. Crude, and it cannot go wrong.
    UFUNCTION()
    void HandleRow0();
    UFUNCTION()
    void HandleRow1();
    UFUNCTION()
    void HandleRow2();
    UFUNCTION()
    void HandleRow3();
    UFUNCTION()
    void HandleRow4();
    UFUNCTION()
    void HandleRow5();
    UFUNCTION()
    void HandleRow6();
    UFUNCTION()
    void HandleRow7();
    UFUNCTION()
    void HandleRow8();
    UFUNCTION()
    void HandleRow9();
    UFUNCTION()
    void HandleRow10();
    UFUNCTION()
    void HandleRow11();
    UFUNCTION()
    void HandleJump();

    UButton* MakeRowButton(UWidget* Content);

    /**
     * Everything the drawing depends on, asked of its owners each frame,
     * which is cheap. A cache key, never an answer: nothing reads it as ship
     * state.
     */
    struct FAskedAt
    {
        /** The system the ship is in, asked of its position without
         *  generating it (UUniverseSubsystem::GetSystemIdAt). */
        TOptional<FSystemId> System;

        /** In a star jump's fold: the system is being left, and there is
         *  nothing to draw. An in-system jump's fold is not between stars
         *  -- it keeps its system on the map and says "In the fold." --
         *  so this is IsInTransit() with a star course (decision 12). */
        bool bBetweenStars = false;

        uint32 Priors = 0;

        /** ds.Nav.StandoffAU as the rim is fitted to it
         *  (UShipSubsystem::GetStandoffAU). */
        double StandoffAU = 0.0;

        static FAskedAt Now(const UShipSubsystem& Ship);

        bool operator==(const FAskedAt& Then) const;
    };

    /** The map's own drawing of one system: what the key above decides. */
    struct FDrawing
    {
        FStarSystem System;
        SystemMap::FMapScale Scale;
        SystemMap::FMapLayout Layout;

        /** Per world, orbit order: where it is and how big, as the window
         *  draws it (LocalSystem::Here), for the row distances. */
        TArray<FUniversePosition> WorldPositions;
        TArray<double> WorldRadiiCm;
    };

    void Redraw(const UShipSubsystem& Ship, const FAskedAt& Now);
    void RefreshRows(const UShipSubsystem& Ship);
    void RefreshFooter(const UShipSubsystem& Ship, const TOptional<SystemMap::FMapShip>& Glyph);
    void RefreshBand(const UShipSubsystem& Ship);

    /** The target, as an orbit of the system drawn, or none: GetTarget()
     *  resolved against the drawing (ShipNav::TargetPlanet), so an id for
     *  another system draws nothing. */
    TOptional<int32> TargetOrbit(const UShipSubsystem& Ship) const;

    TOptional<FAskedAt> DrawnFor;
    TOptional<FDrawing> Drawing;
    int32 LayoutAsked = 0;

    /** The widgets, which are views; none of them holds ship state. */
    UPROPERTY()
    TObjectPtr<USystemMapView> View;

    UPROPERTY()
    TObjectPtr<UTextBlock> TitlePlace;

    UPROPERTY()
    TArray<TObjectPtr<UButton>> RowButtons;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowMarks;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowNames;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowKinds;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowDistances;

    UPROPERTY()
    TObjectPtr<UTextBlock> TargetLine;
    UPROPERTY()
    TObjectPtr<UTextBlock> Footer;
    UPROPERTY()
    TObjectPtr<UButton> JumpButton;
    UPROPERTY()
    TObjectPtr<UTextBlock> JumpLabel;
};
