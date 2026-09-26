#include "UI/NavigationWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipSubsystem.h"
#include "Styling/SlateTypes.h"
#include "UI/NavText.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

// A single right-pointing angle: in the font every screen uses, which a
// geometric triangle is not, and a missing glyph is a box on the glass.
const TCHAR* const UNavigationWidget::PlottedMark = TEXT("›");

namespace
{
    // Point sizes against the real panel: 12 px/cm read from 60 cm, the
    // laptop's density at the laptop's distance. Smaller than the console's
    // 28, because this screen has eleven lines where that one has four.
    constexpr float ChartTitleSize = 26.0f;
    constexpr float ChartSize = 22.0f;

    // A button's own colours, from the ship's palette: the panel at rest, a
    // teal wash under the cursor, and deeper while pressed. A default UMG
    // button is a grey slab that belongs to no ship.
    const FLinearColor Hovered(0.030f, 0.105f, 0.110f, 1.0f);
    const FLinearColor Pressed(0.045f, 0.200f, 0.200f, 1.0f);
    const FLinearColor Raised(0.022f, 0.062f, 0.068f, 1.0f);

    /** The system the ship is in, asked of its position; empty between
     *  stars. Nothing records an arrival (plan conflict 1), so neither does
     *  this screen. */
    TOptional<FStarSystem> SystemHere(const UShipSubsystem& Ship)
    {
        const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(&Ship);
        if (!Universe || Ship.IsInTransit())
        {
            return {};
        }
        return Universe->GetSystemAt(Ship.GetFlightState().GetUniversePosition());
    }

    TOptional<FStarSystem> PlottedSystem(const UShipSubsystem& Ship)
    {
        const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(&Ship);
        const TOptional<FSystemId> Plotted = Ship.GetPlottedSystem();
        return (Universe && Plotted) ? Universe->GetSystem(*Plotted) : TOptional<FStarSystem>();
    }

    void AddFill(UHorizontalBox* Box, UWidget* Content, float Fill, EHorizontalAlignment Align = HAlign_Left)
    {
        FSlateChildSize Size(ESlateSizeRule::Fill);
        Size.Value = Fill;
        UHorizontalBoxSlot* Cell = Box->AddChildToHorizontalBox(Content);
        Cell->SetSize(Size);
        Cell->SetHorizontalAlignment(Align);
        Cell->SetVerticalAlignment(VAlign_Center);
    }
}

UButton* UNavigationWidget::MakeButton(UWidget* Content)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    FButtonStyle Style;
    Style.SetNormal(FSlateColorBrush(Panel));
    Style.SetHovered(FSlateColorBrush(Hovered));
    Style.SetPressed(FSlateColorBrush(Pressed));
    Style.SetDisabled(FSlateColorBrush(Panel));
    Style.SetNormalPadding(FMargin(8.0f, 3.0f));
    Style.SetPressedPadding(FMargin(8.0f, 3.0f));
    Button->SetStyle(Style);
    Button->SetContent(Content);
    return Button;
}

UWidget* UNavigationWidget::BuildScreen()
{
    UVerticalBox* Column = MakeColumn();

    Column->AddChildToVerticalBox(MakeText(
        NSLOCTEXT("DeepSpace", "ChartTitle", "NAVIGATION"), ChartTitleSize, Accent));

    // A labelled line: the label dim, in a column of its own so the values
    // line up down the screen.
    const auto MakeLine = [this, Column](const FText& Label, UTextBlock* Value, UWidget* Trailing)
    {
        UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        AddFill(Line, MakeText(Label, ChartSize, Dim), 1.0f);
        AddFill(Line, Value, Trailing ? 3.6f : 5.0f);
        if (Trailing)
        {
            UHorizontalBoxSlot* TrailingCell = Line->AddChildToHorizontalBox(Trailing);
            TrailingCell->SetVerticalAlignment(VAlign_Center);
        }
        UVerticalBoxSlot* LineCell = Column->AddChildToVerticalBox(Line);
        LineCell->SetPadding(FMargin(0.0f, 6.0f));
    };

    const auto MakeRule = [this, Column]()
    {
        USpacer* Thin = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
        Thin->SetSize(FVector2D(1.0f, 2.0f));
        UBorder* Rule = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
        Rule->SetBrushColor(Dim);
        Rule->SetPadding(FMargin(0.0f));
        Rule->SetContent(Thin);
        UVerticalBoxSlot* RuleCell = Column->AddChildToVerticalBox(Rule);
        RuleCell->SetPadding(FMargin(0.0f, 10.0f));
    };

    HereLine = MakeText(FText::GetEmpty(), ChartSize, Ink);
    MakeLine(NSLOCTEXT("DeepSpace", "ChartHere", "Here"), HereLine, nullptr);
    MakeRule();

    RowButtons.Reset();
    RowNames.Reset();
    RowDistances.Reset();
    RowClasses.Reset();
    RowVisited.Reset();
    for (int32 Index = 0; Index < RowCount; ++Index)
    {
        UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        UTextBlock* Name = MakeText(FText::GetEmpty(), ChartSize, Ink);
        UTextBlock* Distance = MakeText(FText::GetEmpty(), ChartSize, Ink);
        UTextBlock* Class = MakeText(FText::GetEmpty(), ChartSize, Ink);
        UTextBlock* Visited = MakeText(FText::GetEmpty(), ChartSize, Dim);
        AddFill(Columns, Name, 3.0f);
        AddFill(Columns, Distance, 1.6f, HAlign_Right);
        AddFill(Columns, MakeText(FText::GetEmpty(), ChartSize, Dim), 0.3f);
        AddFill(Columns, Class, 3.2f);
        AddFill(Columns, Visited, 1.4f);

        UButton* Row = MakeButton(Columns);
        Column->AddChildToVerticalBox(Row);

        RowButtons.Add(Row);
        RowNames.Add(Name);
        RowDistances.Add(Distance);
        RowClasses.Add(Class);
        RowVisited.Add(Visited);
    }
    RowButtons[0]->OnClicked.AddDynamic(this, &UNavigationWidget::HandleRow0);
    RowButtons[1]->OnClicked.AddDynamic(this, &UNavigationWidget::HandleRow1);
    RowButtons[2]->OnClicked.AddDynamic(this, &UNavigationWidget::HandleRow2);
    RowButtons[3]->OnClicked.AddDynamic(this, &UNavigationWidget::HandleRow3);
    RowButtons[4]->OnClicked.AddDynamic(this, &UNavigationWidget::HandleRow4);
    RowButtons[5]->OnClicked.AddDynamic(this, &UNavigationWidget::HandleRow5);
    static_assert(RowCount == 6, "one handler per row: add or remove them with RowCount");

    MakeRule();

    JumpLine = MakeText(FText::GetEmpty(), ChartSize, Ink);
    MakeLine(NSLOCTEXT("DeepSpace", "ChartJump", "Jump"), JumpLine, nullptr);

    EngageLabel = MakeText(FText::GetEmpty(), ChartSize, Ink);
    EngageButton = MakeButton(EngageLabel);
    FButtonStyle EngageStyle = EngageButton->GetStyle();
    EngageStyle.SetNormal(FSlateColorBrush(Raised));
    EngageButton->SetStyle(EngageStyle);
    EngageButton->OnClicked.AddDynamic(this, &UNavigationWidget::HandleEngage);

    CourseLine = MakeText(FText::GetEmpty(), ChartSize, Ink);
    CourseLine->SetAutoWrapText(true);
    MakeLine(NSLOCTEXT("DeepSpace", "ChartCourse", "Course"), CourseLine, EngageButton);

    RefreshFromShip();
    return MakePanel(Column);
}

void UNavigationWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    RefreshFromShip();
}

void UNavigationWidget::RefreshFromShip()
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem || !HereLine)
    {
        return;
    }

    const bool bTransit = Subsystem->IsInTransit();

    // Here: the system's name and colour, and whether you have been before.
    const TOptional<FStarSystem> Here = SystemHere(*Subsystem);
    FString Place = NavText::JumpWord(EJumpState::Transit);
    if (Here)
    {
        Place = Here->Stub.Name + NavText::Separator + NavText::StarClass(Here->Star.Class);
        if (Subsystem->HasVisited(Here->Stub.Id))
        {
            Place += NavText::Separator;
            Place += TEXT("visited");
        }
    }
    HereLine->SetText(FText::FromString(Place));

    // The rows: the chart as the ship gives it, nearest first, without the
    // system the ship is in. Between stars there is nothing to choose, so
    // the rows stay readable and cannot be pressed.
    const FUniversePosition Position = Subsystem->GetFlightState().GetUniversePosition();
    const TArray<FStarSystemStub> Chart = Subsystem->GetChart();
    const TOptional<FSystemId> Plotted = Subsystem->GetPlottedSystem();
    for (int32 Index = 0; Index < RowCount; ++Index)
    {
        if (!RowButtons.IsValidIndex(Index) || !RowButtons[Index])
        {
            continue;
        }
        if (!Chart.IsValidIndex(Index))
        {
            RowButtons[Index]->SetVisibility(ESlateVisibility::Collapsed);
            RowNames[Index]->SetText(FText::GetEmpty());
            RowDistances[Index]->SetText(FText::GetEmpty());
            RowClasses[Index]->SetText(FText::GetEmpty());
            RowVisited[Index]->SetText(FText::GetEmpty());
            continue;
        }

        const FStarSystemStub& Stub = Chart[Index];
        const bool bPlotted = Plotted && *Plotted == Stub.Id;
        RowButtons[Index]->SetVisibility(ESlateVisibility::Visible);
        RowButtons[Index]->SetIsEnabled(!bTransit);

        // Three spaces where the mark would be, so a plotted name does not
        // shift its row's columns.
        RowNames[Index]->SetText(FText::FromString(
            (bPlotted ? FString(PlottedMark) + TEXT(" ") : FString(TEXT("   "))) + Stub.Name));
        RowNames[Index]->SetColorAndOpacity(FSlateColor(bPlotted ? Accent : Ink));
        RowDistances[Index]->SetText(FText::FromString(FString::Printf(
            TEXT("%.1f ly"), Position.DistanceTo(Stub.Position) / UniverseUnits::CmPerLightYear)));
        RowClasses[Index]->SetText(FText::FromString(NavText::StarClass(Stub.Class)));
        RowVisited[Index]->SetText(Subsystem->HasVisited(Stub.Id)
            ? NSLOCTEXT("DeepSpace", "ChartVisited", "visited") : FText::GetEmpty());
    }

    // The jump, as a word and never a number.
    JumpLine->SetText(FText::FromString(NavText::JumpWord(Subsystem->GetJumpState()) + TEXT(".")));

    // The course, as the same bearing words the helm reads, so the chair and
    // the helm can never describe one heading two ways.
    FString Course = TEXT("None.");
    if (const TOptional<FStarSystem> Star = PlottedSystem(*Subsystem))
    {
        Course = Star->Stub.Name;
        const TOptional<FVector> Bearing = Subsystem->GetCourseDirectionShipLocal();
        if (!bTransit && Bearing)
        {
            Course += TEXT(" — ");
            Course += NavText::Bearing(*Bearing, Subsystem->GetJumpConeRadians());
        }
        Course += TEXT(".");
    }
    CourseLine->SetText(FText::FromString(Course));

    if (EngageButton && EngageLabel)
    {
        const bool bCanPress = !bTransit && Plotted.IsSet();
        EngageButton->SetIsEnabled(bCanPress);
        EngageLabel->SetText(Subsystem->IsJumpEngaged()
            ? NSLOCTEXT("DeepSpace", "ChartStandDown", "STAND DOWN")
            : NSLOCTEXT("DeepSpace", "ChartEngage", "ENGAGE"));
        EngageLabel->SetColorAndOpacity(FSlateColor(bCanPress ? Accent : Dim));
    }
}

void UNavigationWidget::SelectRow(int32 Index)
{
    UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return;
    }

    // The chart is asked again rather than remembered from the last frame:
    // a row is a position in the ship's answer, not a copy of it.
    const TArray<FStarSystemStub> Chart = Subsystem->GetChart();
    if (Index < 0 || Index >= RowCount || !Chart.IsValidIndex(Index))
    {
        return;
    }
    const TOptional<FSystemId> Plotted = Subsystem->GetPlottedSystem();
    if (Plotted && *Plotted == Chart[Index].Id)
    {
        Subsystem->ClearCourse();
    }
    else
    {
        Subsystem->PlotCourse(Chart[Index].Id);
    }
    RefreshFromShip();
}

void UNavigationWidget::PressEngage()
{
    if (UShipSubsystem* Subsystem = Ship())
    {
        Subsystem->SetJumpEngaged(!Subsystem->IsJumpEngaged());
        RefreshFromShip();
    }
}

void UNavigationWidget::HandleRow0() { SelectRow(0); }
void UNavigationWidget::HandleRow1() { SelectRow(1); }
void UNavigationWidget::HandleRow2() { SelectRow(2); }
void UNavigationWidget::HandleRow3() { SelectRow(3); }
void UNavigationWidget::HandleRow4() { SelectRow(4); }
void UNavigationWidget::HandleRow5() { SelectRow(5); }
void UNavigationWidget::HandleEngage() { PressEngage(); }

int32 UNavigationWidget::GetShownRowCount() const
{
    int32 Shown = 0;
    for (const TObjectPtr<UButton>& Row : RowButtons)
    {
        Shown += (Row && Row->GetVisibility() != ESlateVisibility::Collapsed) ? 1 : 0;
    }
    return Shown;
}

FText UNavigationWidget::GetRowText(int32 Index) const
{
    if (!RowButtons.IsValidIndex(Index) || !RowButtons[Index]
        || RowButtons[Index]->GetVisibility() == ESlateVisibility::Collapsed)
    {
        return FText::GetEmpty();
    }

    // What the row shows, read back off the row: a test of this is a test
    // of the glass, not of a second description of it.
    TArray<FString> Parts;
    for (const TArray<TObjectPtr<UTextBlock>>* Column : {&RowNames, &RowDistances, &RowClasses, &RowVisited})
    {
        const FString Part = (*Column)[Index]->GetText().ToString().TrimStart();
        if (!Part.IsEmpty())
        {
            Parts.Add(Part);
        }
    }
    return FText::FromString(FString::Join(Parts, NavText::Separator));
}

FText UNavigationWidget::GetHereText() const
{
    return HereLine ? HereLine->GetText() : FText::GetEmpty();
}

FText UNavigationWidget::GetJumpText() const
{
    return JumpLine ? JumpLine->GetText() : FText::GetEmpty();
}

FText UNavigationWidget::GetCourseText() const
{
    return CourseLine ? CourseLine->GetText() : FText::GetEmpty();
}

bool UNavigationWidget::IsEngageEnabled() const
{
    return EngageButton && EngageButton->GetIsEnabled();
}

FText UNavigationWidget::GetEngageLabel() const
{
    return EngageLabel ? EngageLabel->GetText() : FText::GetEmpty();
}
