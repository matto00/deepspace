#include "UI/NavigationWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Misc/Crc.h"
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

const TCHAR* const UNavigationWidget::InSystemWords = TEXT("in this system");

namespace
{
    // The panel's layout, in its 816 x 576 pixels: the map's (SystemMapWidget.cpp)
    // times 816 / 600. The two desk screens are the same 68 cm panel, so at
    // these sizes their text is the same physical size, and a reader in the
    // chart chair sees one type across the desk. From the chair's eye the
    // panel spans about 940 x 650 screen pixels on the 4K display, so these
    // are close to chair pixels (1.15 screen pixels each): magnified a
    // little, never minified, as the map is at the helm.
    //
    // The first cut stacked eleven lines of 22-26 pt in a vertical box, with
    // each row's columns in a button that centred them. The rows' columns
    // were laid out at their own widths and no two rows lined up; the
    // distance and the class were given less room than their words and ran
    // into each other; and a course that wrapped pushed the band off the
    // bottom of the glass. DeepSpace.UI.ChartLayout lays the tree out as
    // Slate does and holds every word to the room it is given.
    constexpr float TextSize = 19.0f;
    constexpr float ButtonSize = 18.0f;

    constexpr float PanelWidth = 816.0f;
    constexpr float PanelHeight = 576.0f;
    constexpr float Edge = 8.0f;
    constexpr float TitleTop = 8.0f;
    constexpr float BodyTop = 49.0f;
    constexpr float ListWidth = PanelWidth - 2.0f * Edge;

    // Six rows, taller than the map's 33 would be: they are the chart's
    // controls, and the pointer aims them with the head from about a metre.
    constexpr float RowHeight = 44.0f;

    // The band, from the bottom up: the course, wrapping to at most two
    // lines, and above it the jump's word with the toggle at its right. The
    // toggle sits by the jump because it is the jump's; the course then has
    // the band's whole width to wrap in.
    constexpr float LabelWidth = 96.0f;
    // A line at TextSize is 30 px; centred in the jump's 44 it sits 7 px
    // down, and the course's first line is set the same 7 px into its own
    // band so the two lines keep the rows' 44 px rhythm. Two lines, 60 px,
    // and the inset: 72.
    constexpr float LineInset = 7.0f;
    constexpr float CourseHeight = 72.0f;
    constexpr float CourseTop = PanelHeight - Edge - CourseHeight;
    constexpr float JumpHeight = 44.0f;
    constexpr float JumpTop = CourseTop - JumpHeight;
    constexpr float CourseWidth = ListWidth - LabelWidth;

    // The toggle: the map's jump button, 24 x 120, times 816 / 600, and at
    // least: "STAND DOWN" is the widest label, and a label cut short on the
    // one control that engages would be worse than a wider button.
    constexpr float EngageHeight = 33.0f;
    constexpr float EngageMinWidth = 164.0f;
    constexpr float EngageGap = 16.0f;

    // A row's columns, in the list's 800 px, as the map's are in its 322:
    // the mark, the name, the distance right-aligned, a gap, the class, and
    // whether visited. Each is wider than the widest it prints at TextSize:
    // a thirteen-letter name ("Sharsathhaith", the longest in the corpus),
    // "12.0 ly", "yellow-white star", "visited".
    constexpr float MarkColumn = 16.0f;
    constexpr float NameColumn = 232.0f;
    constexpr float DistanceColumn = 112.0f;
    constexpr float GapColumn = 32.0f;
    constexpr float ClassColumn = 272.0f;
    constexpr float VisitedColumn = 136.0f;
    static_assert(MarkColumn + NameColumn + DistanceColumn + GapColumn + ClassColumn + VisitedColumn == ListWidth,
                  "the columns fill the list");

    // A button's own colours, from the ship's palette: the panel at rest, a
    // teal wash under the cursor, and deeper while pressed. A default UMG
    // button is a grey slab that belongs to no ship. The map's are these.
    const FLinearColor Hovered(0.030f, 0.105f, 0.110f, 1.0f);
    const FLinearColor Pressed(0.045f, 0.200f, 0.200f, 1.0f);
    const FLinearColor Raised(0.022f, 0.062f, 0.068f, 1.0f);

    /** The system the ship is in, asked of its position; empty between
     *  stars. Nothing records an arrival (plan conflict 1), so neither does
     *  this screen. An in-system jump's fold is not between stars: the ship
     *  is still here, and the chart says so. */
    TOptional<FStarSystem> SystemHere(const UShipSubsystem& Ship)
    {
        const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(&Ship);
        if (!Universe || (Ship.IsInTransit() && Ship.GetPlottedSystem().IsSet()))
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

    UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* Content, const FVector2D& Position, const FVector2D& Size)
    {
        UCanvasPanelSlot* Cell = Canvas->AddChildToCanvas(Content);
        Cell->SetPosition(Position);
        Cell->SetSize(Size);
        return Cell;
    }

    /** A column Width px wide, as a fill share: the columns' shares are
     *  their widths, so they take exactly those widths across the list. */
    void AddColumn(UHorizontalBox* Box, UWidget* Content, float Width, EHorizontalAlignment Align = HAlign_Left)
    {
        FSlateChildSize Size(ESlateSizeRule::Fill);
        Size.Value = Width;
        UHorizontalBoxSlot* Cell = Box->AddChildToHorizontalBox(Content);
        Cell->SetSize(Size);
        Cell->SetHorizontalAlignment(Align);
        Cell->SetVerticalAlignment(VAlign_Center);
    }
}

UButton* UNavigationWidget::MakeButton(UWidget* Content, const FMargin& Padding)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    FButtonStyle Style;
    Style.SetNormal(FSlateColorBrush(Panel));
    Style.SetHovered(FSlateColorBrush(Hovered));
    Style.SetPressed(FSlateColorBrush(Pressed));
    Style.SetDisabled(FSlateColorBrush(Panel));
    Style.SetNormalPadding(Padding);
    Style.SetPressedPadding(Padding);
    Button->SetStyle(Style);
    // Across the button's whole width. A button centres its content by
    // default, which lays each row's columns out at their own desired width,
    // squeezed and centred, so no two rows lined up -- the chart's spacing
    // fault, which the map had already met and fixed.
    if (UButtonSlot* Cell = Cast<UButtonSlot>(Button->SetContent(Content)))
    {
        Cell->SetHorizontalAlignment(HAlign_Fill);
        Cell->SetVerticalAlignment(VAlign_Center);
    }
    return Button;
}

UWidget* UNavigationWidget::BuildScreen()
{
    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());

    // The title: what this is at the left, where the ship is at the right,
    // as the map's title names its system.
    UTextBlock* Title = MakeText(NSLOCTEXT("DeepSpace", "ChartTitle", "CHART"), TextSize, Accent);
    Place(Canvas, Title, FVector2D(Edge, TitleTop), FVector2D::ZeroVector)->SetAutoSize(true);

    HereLine = MakeText(FText::GetEmpty(), TextSize, Ink);
    UCanvasPanelSlot* HereCell = Place(Canvas, HereLine, FVector2D(PanelWidth - Edge, TitleTop), FVector2D::ZeroVector);
    HereCell->SetAlignment(FVector2D(1.0, 0.0));
    HereCell->SetAutoSize(true);

    // The list: a row per system, nearest first.
    UVerticalBox* List = MakeColumn();
    Place(Canvas, List, FVector2D(Edge, BodyTop), FVector2D(ListWidth, RowHeight * RowCount));
    RowButtons.Reset();
    RowMarks.Reset();
    RowNames.Reset();
    RowDistances.Reset();
    RowClasses.Reset();
    RowVisited.Reset();
    for (int32 Index = 0; Index < RowCount; ++Index)
    {
        UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        UTextBlock* Mark = MakeText(FText::GetEmpty(), TextSize, Accent);
        UTextBlock* Name = MakeText(FText::GetEmpty(), TextSize, Ink);
        UTextBlock* Distance = MakeText(FText::GetEmpty(), TextSize, Ink);
        // The class dim, as the map's kind column is: the name and the
        // distance are what a row is chosen by.
        UTextBlock* Class = MakeText(FText::GetEmpty(), TextSize, Dim);
        UTextBlock* Visited = MakeText(FText::GetEmpty(), TextSize, Dim);
        // A name wider than its column is cut at it, never written over the
        // distance.
        Name->SetClipping(EWidgetClipping::ClipToBounds);
        AddColumn(Columns, Mark, MarkColumn);
        AddColumn(Columns, Name, NameColumn);
        AddColumn(Columns, Distance, DistanceColumn, HAlign_Right);
        AddColumn(Columns, WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()), GapColumn);
        AddColumn(Columns, Class, ClassColumn);
        AddColumn(Columns, Visited, VisitedColumn);

        UButton* Row = MakeButton(Columns, FMargin(0.0f));
        USizeBox* Height = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        Height->SetHeightOverride(RowHeight);
        Height->SetContent(Row);
        List->AddChildToVerticalBox(Height);

        RowButtons.Add(Row);
        RowMarks.Add(Mark);
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

    // The band. A label dim in a column of its own, so the two values line
    // up. The jump's line is one row -- the label, the word, and the toggle
    // at its right -- centred on one line, so the word and the button sit
    // level; the course has the band's whole width to wrap in below it.
    const auto MakeLabel = [this](const FText& Label)
    {
        USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        Column->SetWidthOverride(LabelWidth);
        Column->SetContent(MakeText(Label, TextSize, Dim));
        return Column;
    };

    UHorizontalBox* JumpRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    Place(Canvas, JumpRow, FVector2D(Edge, JumpTop), FVector2D(ListWidth, JumpHeight));
    UHorizontalBoxSlot* JumpLabelCell = JumpRow->AddChildToHorizontalBox(MakeLabel(NSLOCTEXT("DeepSpace", "ChartJump", "Jump")));
    JumpLabelCell->SetVerticalAlignment(VAlign_Center);
    JumpLine = MakeText(FText::GetEmpty(), TextSize, Ink);
    AddColumn(JumpRow, JumpLine, 1.0f);

    EngageLabel = MakeText(FText::GetEmpty(), ButtonSize, Ink);
    EngageLabel->SetJustification(ETextJustify::Center);
    EngageButton = MakeButton(EngageLabel, FMargin(11.0f, 0.0f));
    {
        FButtonStyle Style = EngageButton->GetStyle();
        Style.SetNormal(FSlateColorBrush(Raised));
        EngageButton->SetStyle(Style);
    }
    EngageButton->OnClicked.AddDynamic(this, &UNavigationWidget::HandleEngage);
    USizeBox* EngageSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    EngageSize->SetHeightOverride(EngageHeight);
    EngageSize->SetMinDesiredWidth(EngageMinWidth);
    EngageSize->SetContent(EngageButton);
    UHorizontalBoxSlot* EngageCell = JumpRow->AddChildToHorizontalBox(EngageSize);
    EngageCell->SetVerticalAlignment(VAlign_Center);
    EngageCell->SetPadding(FMargin(EngageGap, 0.0f, 0.0f, 0.0f));

    UHorizontalBox* CourseRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    Place(Canvas, CourseRow, FVector2D(Edge, CourseTop), FVector2D(ListWidth, CourseHeight));
    UHorizontalBoxSlot* CourseLabelCell = CourseRow->AddChildToHorizontalBox(MakeLabel(NSLOCTEXT("DeepSpace", "ChartCourse", "Course")));
    CourseLabelCell->SetVerticalAlignment(VAlign_Top);
    CourseLabelCell->SetPadding(FMargin(0.0f, LineInset, 0.0f, 0.0f));
    CourseLine = MakeText(FText::GetEmpty(), TextSize, Ink);
    // Wrapped at a width, not automatically: an automatic wrap takes its
    // width from the last frame painted, and the layout would then depend on
    // having been drawn.
    CourseLine->SetWrapTextAt(CourseWidth);
    UHorizontalBoxSlot* CourseCell = CourseRow->AddChildToHorizontalBox(CourseLine);
    CourseCell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    CourseCell->SetVerticalAlignment(VAlign_Top);
    CourseCell->SetPadding(FMargin(0.0f, LineInset, 0.0f, 0.0f));

    UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    Root->SetBrushColor(Panel);
    Root->SetPadding(FMargin(0.0f));
    Root->SetHorizontalAlignment(HAlign_Fill);
    Root->SetVerticalAlignment(VAlign_Fill);
    Root->SetContent(Canvas);

    RefreshFromShip();
    return Root;
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

    const FAskedAt Now = FAskedAt::Now(*Subsystem);
    if (!SystemsAskedAt || !Now.SameSystems(*SystemsAskedAt))
    {
        RefreshSystems(*Subsystem);
        SystemsAskedAt = Now;
    }
    if (!CourseAskedAt || !Now.SameCourse(*CourseAskedAt))
    {
        RefreshCourse(*Subsystem);
        CourseAskedAt = Now;
    }

    // The jump, as a word and never a number. Asked every frame: it winds.
    // An in-system fold is "In the fold", not between stars.
    JumpLine->SetText(FText::FromString(
        NavText::JumpWord(Subsystem->GetJumpState(), Now.PlottedWorld.IsSet()) + TEXT(".")));

    if (EngageButton && EngageLabel)
    {
        // Either course: the toggle is the one engage lever, and it engages
        // or stands down the in-system jump as it would a star's.
        const bool bCanPress = !Now.bInTransit && (Now.Plotted.IsSet() || Now.PlottedWorld.IsSet());
        EngageButton->SetIsEnabled(bCanPress);
        EngageLabel->SetText(Subsystem->IsJumpEngaged()
            ? NSLOCTEXT("DeepSpace", "ChartStandDown", "STAND DOWN")
            : NSLOCTEXT("DeepSpace", "ChartEngage", "ENGAGE"));
        EngageLabel->SetColorAndOpacity(FSlateColor(bCanPress ? Accent : Dim));
    }
}

UNavigationWidget::FAskedAt UNavigationWidget::FAskedAt::Now(const UShipSubsystem& Ship)
{
    FAskedAt Asked;
    Asked.JumpSerial = Ship.GetJumpSerial();
    Asked.bInTransit = Ship.IsInTransit();
    Asked.Plotted = Ship.GetPlottedSystem();
    Asked.PlottedWorld = Ship.GetPlottedWorld();
    Asked.Position = Ship.GetFlightState().GetUniversePosition();
    Asked.Orientation = Ship.GetFlightState().GetUniverseOrientation();
    Asked.RangeLy = UShipSubsystem::GetChartRangeLy();
    Asked.ConeRadians = Ship.GetJumpConeRadians();

    // ds.Universe.ReloadPriors changes every system without telling anyone,
    // and the chart must show the new universe on its next frame. Every
    // prior is a double, so the struct is its bytes.
    static_assert(sizeof(FGenPriors) % sizeof(double) == 0, "FGenPriors must be only doubles to be hashed as bytes");
    if (const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(&Ship))
    {
        const FGenPriors Priors = Universe->GetPriors();
        Asked.Priors = FCrc::MemCrc32(&Priors, sizeof(Priors));
    }
    return Asked;
}

bool UNavigationWidget::FAskedAt::SameSystems(const FAskedAt& Then) const
{
    // A thousandth of a light year, 63 AU: a hundredth of the tenth the rows
    // are read to, so they are never visibly behind, and far more than a
    // cruise covers in a session, so a ship cruising between planets asks
    // nothing. Only the drive, running out toward the system's edge, moves
    // the ship far enough to be asked again as it goes.
    constexpr double MovedFarEnoughCm = 1.0e-3 * UniverseUnits::CmPerLightYear;
    return JumpSerial == Then.JumpSerial && bInTransit == Then.bInTransit && Plotted == Then.Plotted
        && PlottedWorld == Then.PlottedWorld && RangeLy == Then.RangeLy && Priors == Then.Priors
        && Position.DistanceTo(Then.Position) < MovedFarEnoughCm;
}

bool UNavigationWidget::FAskedAt::SameCourse(const FAskedAt& Then) const
{
    // The bearing turns with the ship; it is read to a whole degree, and any
    // turn at all may cross one.
    return SameSystems(Then) && Orientation == Then.Orientation && ConeRadians == Then.ConeRadians;
}

void UNavigationWidget::RefreshSystems(const UShipSubsystem& Subsystem)
{
    ++SystemsAsked;
    const bool bTransit = Subsystem.IsInTransit();

    // Here: the system's name and colour, and whether you have been before.
    const TOptional<FStarSystem> Here = SystemHere(Subsystem);
    HereLine->SetText(FText::FromString(Here
        ? NavText::Place(Here->Stub.Name, Here->Star.Class, Subsystem.HasVisited(Here->Stub.Id))
        : NavText::JumpWord(EJumpState::Transit)));

    // The rows: the chart as the ship gives it, nearest first, without the
    // system the ship is in. Between stars there is nothing to choose, so
    // the rows stay readable and cannot be pressed.
    const FUniversePosition Position = Subsystem.GetFlightState().GetUniversePosition();
    const TArray<FStarSystemStub> Chart = Subsystem.GetChart();
    const TOptional<FSystemId> Plotted = Subsystem.GetPlottedSystem();
    for (int32 Index = 0; Index < RowCount; ++Index)
    {
        if (!RowButtons.IsValidIndex(Index) || !RowButtons[Index])
        {
            continue;
        }
        if (!Chart.IsValidIndex(Index))
        {
            RowButtons[Index]->SetVisibility(ESlateVisibility::Collapsed);
            RowMarks[Index]->SetText(FText::GetEmpty());
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

        // The mark in its own column, so a plotted name does not shift.
        RowMarks[Index]->SetText(bPlotted ? FText::FromString(PlottedMark) : FText::GetEmpty());
        RowNames[Index]->SetText(FText::FromString(Stub.Name));
        RowNames[Index]->SetColorAndOpacity(FSlateColor(bPlotted ? Accent : Ink));
        RowDistances[Index]->SetText(FText::FromString(NavText::Distance(Position.DistanceTo(Stub.Position))));
        RowClasses[Index]->SetText(FText::FromString(NavText::StarClass(Stub.Class)));
        RowVisited[Index]->SetText(FText::FromString(NavText::Visited(Subsystem.HasVisited(Stub.Id))));
    }
}

void UNavigationWidget::RefreshCourse(const UShipSubsystem& Subsystem)
{
    ++CourseAsked;

    // The course, in the same bearing words the helm reads, so the chair and
    // the helm can never describe one heading two ways.
    FString Course = NavText::NoCourse();
    const TOptional<FVector> Bearing = Subsystem.IsInTransit()
        ? TOptional<FVector>() : Subsystem.GetCourseDirectionShipLocal();
    if (const TOptional<FStarSystem> Star = PlottedSystem(Subsystem))
    {
        Course = NavText::Course(Star->Stub.Name, Bearing, Subsystem.GetJumpConeRadians());
    }
    else if (const TOptional<FBodyId> World = Subsystem.GetPlottedWorld())
    {
        // The in-system jump's course (map decision 12): the chart lists and
        // plots only stars, and this is where the jump it engages is shown
        // to go -- marked as the map marks its target, which it is, and said
        // to be in this system, since no row is. The bearing is the jump's
        // cone words, like a star's, because it is the jump's cone.
        const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(&Subsystem);
        const TOptional<FStarSystem> System = Universe ? Universe->GetSystem(World->System) : TOptional<FStarSystem>();
        if (const FPlanet* Planet = System ? ShipNav::TargetPlanet(*System, *World) : nullptr)
        {
            Course = FString(PlottedMark) + TEXT(" ") + NavText::WorldName(*Planet) + NavText::Separator
                + InSystemWords;
            if (Bearing)
            {
                Course += NavText::Separator + NavText::Bearing(*Bearing, Subsystem.GetJumpConeRadians());
            }
        }
    }
    CourseLine->SetText(FText::FromString(Course + TEXT(".")));
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
    // of the glass, not of a second description of it. The mark and the
    // name are one part, as on the map.
    const FString Mark = RowMarks[Index]->GetText().ToString();
    const FString Name = RowNames[Index]->GetText().ToString();
    TArray<FString> Parts;
    Parts.Add(Mark.IsEmpty() ? Name : Mark + TEXT(" ") + Name);
    for (const TArray<TObjectPtr<UTextBlock>>* Column : {&RowDistances, &RowClasses, &RowVisited})
    {
        const FString Part = (*Column)[Index]->GetText().ToString();
        if (!Part.IsEmpty())
        {
            Parts.Add(Part);
        }
    }
    return FText::FromString(FString::Join(Parts, NavText::Separator));
}

bool UNavigationWidget::IsRowEnabled(int32 Index) const
{
    return RowButtons.IsValidIndex(Index) && RowButtons[Index]
        && RowButtons[Index]->GetVisibility() != ESlateVisibility::Collapsed
        && RowButtons[Index]->GetIsEnabled();
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
