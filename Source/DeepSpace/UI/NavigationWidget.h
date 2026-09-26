#pragma once

#include "CoreMinimal.h"
#include "UI/ShipScreenWidget.h"
#include "NavigationWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * The chart's screen: where the ship is, the nearest systems, the jump as a
 * word, and the course as a bearing. Choosing and engaging, nothing else.
 *
 * It asks the ship every frame and keeps nothing it could ask for. A row is
 * a position in UShipSubsystem::GetChart(), asked again when it is clicked,
 * and the plotted row is whichever one the ship says is plotted; so a course
 * plotted from the console, or from a second chart, shows here without this
 * screen being told.
 *
 * What is deliberately absent: the jump has no percentage, no bar, no
 * countdown and no ETA -- a number that fills is a clock to watch -- and no
 * row is ranked or recommended. Distances are facts about the sky, and so
 * are bearings; nothing here is late and nothing gets worse.
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

    FText GetHereText() const;

    /** The jump as the chart words it: NavText::JumpWord. */
    FText GetJumpText() const;

    FText GetCourseText() const;

    /** Whether the toggle can be pressed, and what it would do. */
    bool IsEngageEnabled() const;
    FText GetEngageLabel() const;

    /** The marker the plotted row carries. */
    static const TCHAR* const PlottedMark;

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

    UButton* MakeButton(UWidget* Content);

    /** The widgets, which are views; none of them holds ship state. */
    UPROPERTY()
    TArray<TObjectPtr<UButton>> RowButtons;

    /** Per row: the name, the distance, the class, and whether visited. */
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowNames;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowDistances;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowClasses;
    UPROPERTY()
    TArray<TObjectPtr<UTextBlock>> RowVisited;

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
