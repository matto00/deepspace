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
 * It stores nothing it could ask for. The ship's position and heading are
 * asked of UShipSubsystem every frame, and so, once it exists, is the target.
 * What costs a generated system -- the rings, the dots, each world's position
 * and radius -- is a cache of its own drawing, keyed on exactly what that
 * drawing depends on (FAskedAt): the id of the system the ship is in, whether
 * it is between stars, the priors, and the standoff the rim is fitted to. Not
 * the ship's position: the layout does not depend on where the ship is, and
 * nothing orbits, so the cache is exact. The ship's glyph, the row distances
 * and the footer are recomputed from it every frame, and none of that
 * generates anything.
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
     * or let it go if it is the target (SystemMap::Select). The one seam both
     * pick paths end at. Nothing for an orbit this system lacks, between
     * stars, or when the drawing the press was made on has just been
     * replaced: Orbit is an index into what was on the glass, and a click
     * that lands in the frame the system changes must not pick the same
     * index of a system the player never saw.
     *
     * The target it acts on does not exist yet (build order, stage 3): today
     * the selection is worked out and announced to OnSelectedForTest, and
     * nothing else happens. See "Stage 3 (3c)" at the foot of this class.
     */
    void SelectWorld(int32 Orbit);

#if WITH_DEV_AUTOMATION_TESTS
    /** Every selection SelectWorld works out, as it works it out: the seam a
     *  test holds both pick paths to before the target exists. Test-only so
     *  that no behaviour can collect on it; 3c deletes it once SelectWorld
     *  sets the target and the tests read GetTarget() instead. */
    TMulticastDelegate<void(const SystemMap::FMapSelection&)> OnSelectedForTest;
#endif

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

    /** The target line (decision 6). Empty until the target exists. */
    FText GetTargetText() const;

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

        /** In any transit, today: IsInTransit(), which is true for every
         *  fold. Stage 3 narrows it to a fold whose course is a star -- an
         *  in-system jump keeps its system on the map and says "In the
         *  fold." (map spec, decision 12). Until then there is no fold that
         *  is not between stars. */
        bool bBetweenStars = false;

        uint32 Priors = 0;

        /** ds.Nav.StandoffAU as the rim is fitted to it. */
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
    void RefreshFooter(const TOptional<SystemMap::FMapShip>& Glyph);

    /**
     * The target, as an orbit of the system drawn, or none. Always none
     * until stage 3: the ship holds no target yet. Stage 3 resolves
     * UShipSubsystem::GetTarget() against Drawing->System here
     * (ShipNav::TargetPlanet), so an id for another system draws nothing.
     */
    TOptional<int32> TargetOrbit(const UShipSubsystem& Ship) const;

    /** The target as held, for SystemMap::Select. None until stage 3. */
    TOptional<FBodyId> TargetHeld(const UShipSubsystem& Ship) const;

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

    /*
     * Stage 3 (3c), which owns this file then -- what 1b left for the
     * target to fill, all of it stubbed or defaulted today:
     *
     *  - SelectWorld: act on the selection, Ship->SetTarget(Selection->Body)
     *    or Ship->ClearTarget(), then RefreshFromShip so the mark moves this
     *    frame; delete OnSelectedForTest.
     *  - TargetOrbit / TargetHeld: resolve GetTarget() through
     *    ShipNav::TargetPlanet against Drawing->System; both return {} now.
     *  - FAskedAt::Now: StandoffAU from GetStandoffAU(), and bBetweenStars
     *    as IsInTransit() && the course is a star.
     *  - RefreshFooter: "In the fold." for an in-system fold, which keeps
     *    the drawing; "Between stars." only for a star course's.
     *  - The band: the target line with its live ETA (NavText::WorldName
     *    names the world there) and the Jump here / Stand down / Near
     *    enough to fly button.
     */
};
