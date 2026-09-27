#pragma once

#include "CoreMinimal.h"
#include "UI/ShipScreenWidget.h"
#include "Universe/StarSystem.h"
#include "Universe/UniversePosition.h"
#include "NavigationWidget.generated.h"

class UButton;
class UShipSubsystem;
class UTextBlock;

/**
 * The chart's screen: where the ship is, the nearest systems, the jump as a
 * word, and the course as a bearing. Choosing and engaging, nothing else.
 *
 * It keeps nothing it could ask for. A row is a position in
 * UShipSubsystem::GetChart(), asked again when it is clicked, and the plotted
 * row is whichever one the ship says is plotted; so a course plotted from the
 * console, or from a second chart, shows here without this screen being
 * told. The jump's word is asked every frame. What costs a sector scan or a
 * generated system -- where the ship is, the rows, the course -- is asked
 * again only when something it depends on has changed (FAskedAt), since the
 * chart is in view from the helm and would otherwise pay for it every frame.
 *
 * The course may be a world in this system (the map's in-system jump, map
 * decision 12): the rows stay stars, none of them marked, and the course
 * line names the world "in this system"; the toggle engages or stands down
 * that jump as it would a star's, and plotting a star row replaces it.
 *
 * What is deliberately absent: the jump has no percentage, no bar, no
 * countdown and no ETA -- the charge is a wait, and a number that fills is a
 * clock to watch -- and no row is ranked or recommended. Distances are facts about the sky, and so
 * are bearings; nothing here is late and nothing gets worse.
 *
 * Laid out as the map beside it is, on a canvas with every size a constant
 * (NavigationWidget.cpp): the title with where the ship is at its right, a
 * row per system with the mark in a column of its own, and the band at the
 * bottom -- the jump's word with the one toggle at its right, then the
 * course. Its 816 x 576 is what the panel spans from its own chair (about
 * 970 x 660 screen pixels on the 4K display), so it is never minified where
 * it is read; and every size is the map's times 816 / 600, so the two desk
 * screens' text is one physical size.
 */
UCLASS()
class DEEPSPACE_API UNavigationWidget : public UShipScreenWidget
{
    GENERATED_BODY()

public:
    /** Six: enough to be a choice, few enough to read from the chair. */
    static constexpr int32 RowCount = 6;

    /** Pulls every line back into line with the ship. Called every frame;
     *  public so a test can drive the screen without Slate painting. */
    void RefreshFromShip();

    /** How many times the chart has asked for where the ship is and what is
     *  near, and for the course. A test's measure of what a frame costs;
     *  nothing on the glass. */
    int32 GetSystemsAsked() const { return SystemsAsked; }
    int32 GetCourseAsked() const { return CourseAsked; }

    /**
     * What clicking row Index does: plots that system, or clears the course
     * if it is the one already plotted. The seam each row's button ends at.
     */
    void SelectRow(int32 Index);

    /** What the Engage / Stand down toggle does. Engaging with no course, or
     *  between stars, is refused by the ship and changes nothing. */
    void PressEngage();

    /** How many rows are showing: the chart, up to RowCount. */
    int32 GetShownRowCount() const;

    /** What row Index shows, its columns joined by NavText::Separator; empty
     *  for a row that is not showing. The plotted row starts with a marker. */
    FText GetRowText(int32 Index) const;

    /** Whether row Index can be pressed: a showing row, and not between
     *  stars. What the glass offers, which the ship's own refusal to replot
     *  in transit does not show. */
    bool IsRowEnabled(int32 Index) const;

    FText GetHereText() const;

    /** The jump as the chart words it: NavText::JumpWord. */
    FText GetJumpText() const;

    FText GetCourseText() const;

    /** Whether the toggle can be pressed, and what it would do. */
    bool IsEngageEnabled() const;
    FText GetEngageLabel() const;

    /** The marker the plotted row carries. */
    static const TCHAR* const PlottedMark;

    /** What the course line says of a course to a world: "› Kessa II · in
     *  this system · 12° to port" (map decision 12). */
    static const TCHAR* const InSystemWords;

protected:
    virtual UWidget* BuildScreen() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:
    // UButton::OnClicked carries no payload, so each row has its own
    // handler. Crude, and it cannot go wrong (nav decision 7).
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
    void HandleEngage();

    UButton* MakeButton(UWidget* Content, const FMargin& Padding);

    /**
     * Everything the chart's costly answers depend on, asked of their owners
     * each frame, which is cheap. The here line and the rows are a function
     * of the jump's serial (every arrival, and the visited record with it),
     * whether the ship is between stars, the course (a star or a world), where the ship
     * is, ds.Nav.RangeLy and the universe's priors; the course line adds the
     * ship's heading and the cone. When none has changed, neither has any
     * answer. A cache key, never an answer: nothing reads it as ship state.
     */
    struct FAskedAt
    {
        int32 JumpSerial = 0;
        bool bInTransit = false;
        TOptional<FSystemId> Plotted;
        /** The in-system jump's course: the course line names it. */
        TOptional<FBodyId> PlottedWorld;
        FUniversePosition Position;
        FQuat Orientation = FQuat::Identity;
        float RangeLy = 0.0f;
        double ConeRadians = 0.0;
        uint32 Priors = 0;

        static FAskedAt Now(const UShipSubsystem& Ship);

        /** Whether the here line and the rows can read differently from Then. */
        bool SameSystems(const FAskedAt& Then) const;

        /** Whether the course line can. */
        bool SameCourse(const FAskedAt& Then) const;
    };

    void RefreshSystems(const UShipSubsystem& Ship);
    void RefreshCourse(const UShipSubsystem& Ship);

    /** What the lines on the glass were last asked from; unset until they
     *  have been. */
    TOptional<FAskedAt> SystemsAskedAt;
    TOptional<FAskedAt> CourseAskedAt;

    int32 SystemsAsked = 0;
    int32 CourseAsked = 0;

    /** The widgets, which are views; none of them holds ship state. */
    UPROPERTY()
    TArray<TObjectPtr<UButton>> RowButtons;

    /** Per row: the plotted mark, the name, the distance, the class, and
     *  whether visited. The mark has a column of its own, as on the map, so
     *  a plotted row's name does not shift. */
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowMarks;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowNames;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowDistances;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowClasses;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowVisited;

    /** The title's right-hand side: where the ship is, as the map's title
     *  names its system. */
    UPROPERTY()
    TObjectPtr<UTextBlock> HereLine;
    UPROPERTY()
    TObjectPtr<UTextBlock> JumpLine;
    UPROPERTY()
    TObjectPtr<UTextBlock> CourseLine;
    UPROPERTY()
    TObjectPtr<UButton> EngageButton;
    UPROPERTY()
    TObjectPtr<UTextBlock> EngageLabel;
};
