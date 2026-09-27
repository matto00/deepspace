#include "UI/SystemMapWidget.h"

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
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Misc/Crc.h"
#include "Ship/NavStart.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Styling/SlateTypes.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "UI/ShipHUDWidget.h"
#include "UI/SystemMapView.h"
#include "UI/TargetMarker.h"
#include "Universe/UniverseSubsystem.h"

namespace
{
    // The panel's layout, in its 600 x 424 pixels, which at the helm are
    // screen pixels (decision 1). Sizes a person can read at 1.6 m are the
    // open question decision 11 puts to a render; these are its knobs.
    constexpr float TextSize = 14.0f;
    constexpr float FooterSize = 13.0f;

    constexpr float Edge = 6.0f;
    constexpr float TitleTop = 6.0f;
    constexpr float BodyTop = 36.0f;
    constexpr float OrrerySide = 256.0f;
    constexpr float ListLeft = 272.0f;
    constexpr float ListWidth = 322.0f;
    constexpr float RowHeight = 24.0f;
    constexpr float BandTop = 330.0f;
    constexpr float TargetHeight = 40.0f;
    constexpr float FooterTop = 374.0f;
    constexpr float PanelWidth = 600.0f;

    // The band's one button (decision 12): 24 px tall and at least 120
    // wide, at the right of the footer's row. At least, not exactly: "Near
    // enough to fly" at the footer's size is about 150 px, and a label cut
    // short on the one control that says why it cannot be pressed would be
    // worse than a wider button.
    constexpr float JumpHeight = 24.0f;
    constexpr float JumpMinWidth = 120.0f;

    // The footer shares that row, left of the button, and wraps rather than
    // run under it: "Inside <a given name>'s orbit. 34° above the plane." is
    // wider than the room the button leaves. The row has 44 px under
    // FooterTop, room for two lines at the footer's size.
    constexpr float FooterWidth = PanelWidth - 2.0f * Edge - USystemMapWidget::JumpReserve;

    // A row's columns, in the list's 322 px: the target's mark, the numeral
    // (or a given name), the kind, and the distance right-aligned. The
    // numeral's is 40 px, not the spec's 34: "VIII" at size 14 is 36 px wide,
    // and the first render check cut it to "V". The
    // distance is the widest thing the list prints, "1,496 THOUSAND KM",
    // which is why the orrery is 256 px and not larger.
    constexpr float MarkColumn = 12.0f;
    constexpr float NameColumn = 40.0f;
    constexpr float KindColumn = 94.0f;
    constexpr float DistanceColumn = 176.0f;

    // The chart's button colours, from the ship's palette: the map is the
    // chart's neighbour on the desk and its rows press the same way.
    const FLinearColor Hovered(0.030f, 0.105f, 0.110f, 1.0f);
    const FLinearColor Pressed(0.045f, 0.200f, 0.200f, 1.0f);

    // Raised off the panel, as the chart's Engage is: the one control on the
    // screen that does more than mark something.
    const FLinearColor Raised(0.022f, 0.062f, 0.068f, 1.0f);

    UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* Content, const FVector2D& Position, const FVector2D& Size)
    {
        UCanvasPanelSlot* Cell = Canvas->AddChildToCanvas(Content);
        Cell->SetPosition(Position);
        Cell->SetSize(Size);
        return Cell;
    }

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

UButton* USystemMapWidget::MakeRowButton(UWidget* Content)
{
    UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    FButtonStyle Style;
    Style.SetNormal(FSlateColorBrush(Panel));
    Style.SetHovered(FSlateColorBrush(Hovered));
    Style.SetPressed(FSlateColorBrush(Pressed));
    Style.SetDisabled(FSlateColorBrush(Panel));
    Style.SetNormalPadding(FMargin(2.0f, 0.0f));
    Style.SetPressedPadding(FMargin(2.0f, 0.0f));
    Button->SetStyle(Style);
    // Across the button's whole width: a button centres its content by
    // default, which lays each row's columns out at its own width and no
    // two rows line up.
    if (UButtonSlot* Cell = Cast<UButtonSlot>(Button->SetContent(Content)))
    {
        Cell->SetHorizontalAlignment(HAlign_Fill);
        Cell->SetVerticalAlignment(VAlign_Center);
    }
    return Button;
}

UWidget* USystemMapWidget::BuildScreen()
{
    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());

    // The title: what this is at the left, where at the right.
    UTextBlock* Title = MakeText(NSLOCTEXT("DeepSpace", "MapTitle", "SYSTEM"), TextSize, Accent);
    UCanvasPanelSlot* TitleCell = Place(Canvas, Title, FVector2D(Edge, TitleTop), FVector2D::ZeroVector);
    TitleCell->SetAutoSize(true);

    TitlePlace = MakeText(FText::GetEmpty(), TextSize, Ink);
    UCanvasPanelSlot* PlaceCell = Place(Canvas, TitlePlace, FVector2D(PanelWidth - Edge, TitleTop), FVector2D::ZeroVector);
    PlaceCell->SetAlignment(FVector2D(1.0, 0.0));
    PlaceCell->SetAutoSize(true);

    // The orrery.
    View = WidgetTree->ConstructWidget<USystemMapView>(USystemMapView::StaticClass());
    View->OnPicked.BindUObject(this, &USystemMapWidget::SelectWorld);
    Place(Canvas, View, FVector2D(Edge, BodyTop), FVector2D(OrrerySide, OrrerySide));

    // The list: a row per world, orbit order.
    UVerticalBox* List = MakeColumn();
    Place(Canvas, List, FVector2D(ListLeft, BodyTop), FVector2D(ListWidth, RowHeight * RowCount));
    RowButtons.Reset();
    RowMarks.Reset();
    RowNames.Reset();
    RowKinds.Reset();
    RowDistances.Reset();
    for (int32 Index = 0; Index < RowCount; ++Index)
    {
        UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
        UTextBlock* Mark = MakeText(FText::GetEmpty(), TextSize, Accent);
        UTextBlock* Name = MakeText(FText::GetEmpty(), TextSize, Ink);
        UTextBlock* Kind = MakeText(FText::GetEmpty(), TextSize, Dim);
        UTextBlock* Distance = MakeText(FText::GetEmpty(), TextSize, Ink);
        // A given name is wider than its column; it is cut at the column
        // rather than written over the kind.
        Name->SetClipping(EWidgetClipping::ClipToBounds);
        AddColumn(Columns, Mark, MarkColumn);
        AddColumn(Columns, Name, NameColumn);
        AddColumn(Columns, Kind, KindColumn);
        AddColumn(Columns, Distance, DistanceColumn, HAlign_Right);

        UButton* Row = MakeRowButton(Columns);
        USizeBox* Height = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
        Height->SetHeightOverride(RowHeight);
        Height->SetContent(Row);
        List->AddChildToVerticalBox(Height);

        RowButtons.Add(Row);
        RowMarks.Add(Mark);
        RowNames.Add(Name);
        RowKinds.Add(Kind);
        RowDistances.Add(Distance);
    }
    RowButtons[0]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow0);
    RowButtons[1]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow1);
    RowButtons[2]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow2);
    RowButtons[3]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow3);
    RowButtons[4]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow4);
    RowButtons[5]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow5);
    RowButtons[6]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow6);
    RowButtons[7]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow7);
    RowButtons[8]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow8);
    RowButtons[9]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow9);
    RowButtons[10]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow10);
    RowButtons[11]->OnClicked.AddDynamic(this, &USystemMapWidget::HandleRow11);
    static_assert(RowCount == 12, "one handler per row: add or remove them with RowCount");

    // The band: the target line, then the footer. The right of the footer's
    // row is kept for the one button stage 3 adds (decision 12).
    TargetLine = MakeText(FText::GetEmpty(), TextSize, Ink);
    TargetLine->SetAutoWrapText(true);
    Place(Canvas, TargetLine, FVector2D(Edge, BandTop), FVector2D(PanelWidth - 2.0f * Edge, TargetHeight));

    Footer = MakeText(FText::GetEmpty(), FooterSize, Dim);
    Footer->SetWrapTextAt(FooterWidth);
    UCanvasPanelSlot* FooterCell = Place(Canvas, Footer, FVector2D(Edge, FooterTop), FVector2D::ZeroVector);
    FooterCell->SetAutoSize(true);

    JumpLabel = MakeText(FText::GetEmpty(), FooterSize, Accent);
    JumpLabel->SetJustification(ETextJustify::Center);
    JumpButton = MakeRowButton(JumpLabel);
    {
        FButtonStyle Style = JumpButton->GetStyle();
        Style.SetNormal(FSlateColorBrush(Raised));
        Style.SetNormalPadding(FMargin(8.0f, 0.0f));
        Style.SetPressedPadding(FMargin(8.0f, 0.0f));
        JumpButton->SetStyle(Style);
    }
    JumpButton->OnClicked.AddDynamic(this, &USystemMapWidget::HandleJump);
    JumpButton->SetVisibility(ESlateVisibility::Collapsed);
    USizeBox* JumpSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    JumpSize->SetHeightOverride(JumpHeight);
    JumpSize->SetMinDesiredWidth(JumpMinWidth);
    JumpSize->SetContent(JumpButton);
    UCanvasPanelSlot* JumpCell = Place(Canvas, JumpSize, FVector2D(PanelWidth - Edge, FooterTop), FVector2D::ZeroVector);
    JumpCell->SetAlignment(FVector2D(1.0, 0.0));
    JumpCell->SetAutoSize(true);

    UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    Root->SetBrushColor(Panel);
    Root->SetPadding(FMargin(0.0f));
    Root->SetHorizontalAlignment(HAlign_Fill);
    Root->SetVerticalAlignment(VAlign_Fill);
    Root->SetContent(Canvas);

    RefreshFromShip();
    return Root;
}

void USystemMapWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    RefreshFromShip();
}

USystemMapWidget::FAskedAt USystemMapWidget::FAskedAt::Now(const UShipSubsystem& Ship)
{
    FAskedAt Asked;
    // A star jump's fold, and only that: an in-system jump's fold leaves the
    // ship in the system it is drawing, and the drawing stands.
    Asked.bBetweenStars = Ship.IsInTransit() && Ship.GetPlottedSystem().IsSet();
    // The stand-off the rim is fitted to, as tuned now.
    Asked.StandoffAU = UShipSubsystem::GetStandoffAU();

    if (const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(&Ship))
    {
        Asked.System = Universe->GetSystemIdAt(Ship.GetFlightState().GetUniversePosition());

        // ds.Universe.ReloadPriors changes every system without telling
        // anyone, as for the chart. Every prior is a double, so the struct
        // is its bytes.
        static_assert(sizeof(FGenPriors) % sizeof(double) == 0, "FGenPriors must be only doubles to be hashed as bytes");
        const FGenPriors Priors = Universe->GetPriors();
        Asked.Priors = FCrc::MemCrc32(&Priors, sizeof(Priors));
    }
    return Asked;
}

bool USystemMapWidget::FAskedAt::operator==(const FAskedAt& Then) const
{
    return System == Then.System && bBetweenStars == Then.bBetweenStars && Priors == Then.Priors
        && StandoffAU == Then.StandoffAU;
}

void USystemMapWidget::RefreshFromShip()
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem || !View)
    {
        return;
    }

    const FAskedAt Now = FAskedAt::Now(*Subsystem);
    if (!DrawnFor || !(Now == *DrawnFor))
    {
        Redraw(*Subsystem, Now);
        DrawnFor = Now;
    }

    TOptional<SystemMap::FMapShip> Glyph;
    if (Drawing)
    {
        const FShipFlightState& Flight = Subsystem->GetFlightState();
        Glyph = SystemMap::Ship(Drawing->Scale, Flight.GetUniversePosition(), Flight.GetUniverseOrientation());
        View->SetShip(Glyph);
        View->SetTarget(TargetOrbit(*Subsystem));
    }
    RefreshRows(*Subsystem);
    RefreshFooter(*Subsystem, Glyph);
    RefreshBand(*Subsystem);
}

void USystemMapWidget::Redraw(const UShipSubsystem& Subsystem, const FAskedAt& Now)
{
    ++LayoutAsked;
    Drawing.Reset();

    const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(&Subsystem);
    const TOptional<FStarSystem> System = (Universe && Now.System && !Now.bBetweenStars)
        ? Universe->GetSystem(*Now.System) : TOptional<FStarSystem>();
    if (!System)
    {
        View->ClearDrawing();
        TitlePlace->SetText(FText::GetEmpty());
        for (int32 Index = 0; Index < RowButtons.Num(); ++Index)
        {
            RowButtons[Index]->SetVisibility(ESlateVisibility::Collapsed);
            RowNames[Index]->SetText(FText::GetEmpty());
            RowKinds[Index]->SetText(FText::GetEmpty());
        }
        return;
    }

    FDrawing Drawn;
    Drawn.System = *System;
    Drawn.Scale = SystemMap::Fit(*System, Now.StandoffAU);
    Drawn.Layout = SystemMap::Layout(*System, Drawn.Scale);

    // Colours and sizes as the window draws them: FSkySystem puts the star
    // first, then the worlds innermost first, so world i is body i + 1.
    const FSkySystem Sky = LocalSystem::Here(System);
    TArray<FLinearColor> Colours;
    for (int32 Orbit = 0; Orbit < System->Planets.Num(); ++Orbit)
    {
        const FSkyBody* Body = Sky.Bodies.IsValidIndex(Orbit + 1) ? &Sky.Bodies[Orbit + 1] : nullptr;
        Drawn.WorldPositions.Add(Body ? Body->Position : System->PlanetPosition(Orbit));
        Drawn.WorldRadiiCm.Add(Body ? Body->Radius : System->Planets[Orbit].RadiusEarth * UniverseUnits::CmPerEarthRadius);
        Colours.Add(USystemMapView::PanelColour(Body ? Body->Colour : Ink));
    }
    View->SetDrawing(Drawn.Layout, Sky.Bodies.IsEmpty() ? Ink : Sky.Bodies[0].Colour, Colours);

    TitlePlace->SetText(FText::FromString(NavText::Place(System->Stub.Name, System->Star.Class)));

    for (int32 Index = 0; Index < RowButtons.Num(); ++Index)
    {
        const bool bShown = System->Planets.IsValidIndex(Index);
        RowButtons[Index]->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (!bShown)
        {
            RowNames[Index]->SetText(FText::GetEmpty());
            RowKinds[Index]->SetText(FText::GetEmpty());
            continue;
        }
        // The numeral always, because it is what the orrery labels the dot
        // with and the row is how a dot is read. An inhabited world's given
        // name takes the kind's column instead: who lives there says more
        // than the taxonomy, and an inhabited world is temperate, so its
        // kind is terrestrial or ocean all but always. The whole of it,
        // "Halden · Kessa II", is the target line's (NavText::WorldName).
        const FPlanet& Planet = System->Planets[Index];
        const bool bNamed = !Planet.GivenName.IsEmpty();
        RowNames[Index]->SetText(FText::FromString(Drawn.Layout.Dots[Index].Numeral));
        RowKinds[Index]->SetText(FText::FromString(bNamed ? Planet.GivenName : NavText::WorldKind(Planet.Kind)));
        RowKinds[Index]->SetColorAndOpacity(FSlateColor(bNamed ? Ink : Dim));
    }
    Drawing = MoveTemp(Drawn);
}

void USystemMapWidget::RefreshRows(const UShipSubsystem& Subsystem)
{
    const FUniversePosition Where = Subsystem.GetFlightState().GetUniversePosition();
    const TOptional<int32> Target = TargetOrbit(Subsystem);
    const int32 Worlds = Drawing ? Drawing->WorldPositions.Num() : 0;
    for (int32 Index = 0; Index < FMath::Min(Worlds, RowButtons.Num()); ++Index)
    {
        // To the surface, in the altitude line's own words: when a row is
        // the nearest world, the two agree to the digit.
        const double Surface = FMath::Max(0.0, Where.DistanceTo(Drawing->WorldPositions[Index]) - Drawing->WorldRadiiCm[Index]);
        RowDistances[Index]->SetText(FText::FromString(UShipHUDWidget::AltitudeWords(Surface)));

        const bool bTarget = Target && *Target == Index;
        RowMarks[Index]->SetText(bTarget ? FText::FromString(UNavigationWidget::PlottedMark) : FText::GetEmpty());
        RowNames[Index]->SetColorAndOpacity(FSlateColor(bTarget ? Accent : Ink));
    }
    for (int32 Index = Worlds; Index < RowDistances.Num(); ++Index)
    {
        RowDistances[Index]->SetText(FText::GetEmpty());
        RowMarks[Index]->SetText(FText::GetEmpty());
    }
}

void USystemMapWidget::RefreshFooter(const UShipSubsystem& Subsystem, const TOptional<SystemMap::FMapShip>& Glyph)
{
    TArray<FString> Lines;
    if (!Drawing || !Glyph)
    {
        // Between stars: placed there, or in a star jump's fold.
        Lines.Add(NavText::JumpWord(EJumpState::Transit) + TEXT("."));
    }
    else if (Subsystem.IsInTransit())
    {
        // An in-system fold: still here, drawn, and not between stars.
        Lines.Add(NavText::JumpWord(EJumpState::Transit, true) + TEXT("."));
    }
    else if (Drawing->System.Planets.IsEmpty())
    {
        Lines.Add(FString::Printf(TEXT("Nothing orbits %s."), *Drawing->System.Stub.Name));
    }
    else
    {
        if (Glyph->Pin == SystemMap::EMapPin::Inside)
        {
            Lines.Add(FString::Printf(TEXT("Inside %s's orbit."), *Drawing->System.Planets[0].Designation));
        }
        else if (Glyph->Pin == SystemMap::EMapPin::Beyond)
        {
            Lines.Add(TEXT("Beyond the map."));
        }
        if (FMath::Abs(Glyph->ElevationDeg) > SystemMap::ElevationShownPastDeg)
        {
            Lines.Add(FString::Printf(TEXT("%d° %s the plane."), FMath::RoundToInt32(FMath::Abs(Glyph->ElevationDeg)),
                                      Glyph->ElevationDeg > 0.0 ? TEXT("above") : TEXT("below")));
        }
    }
    Footer->SetText(FText::FromString(FString::Join(Lines, TEXT(" "))));
}

void USystemMapWidget::RefreshBand(const UShipSubsystem& Subsystem)
{
    // The line the HUD prints, from the ship's own view of the target: one
    // composition, so the two screens cannot disagree (ScreensAgree).
    const TOptional<FTargetView> Seen = Drawing ? Subsystem.GetTargetView(Drawing->System) : TOptional<FTargetView>();
    TargetLine->SetText(Seen ? FText::FromString(TargetMarker::Line(*Seen)) : FText::GetEmpty());

    // The button, for a target that resolves on this drawing, and not while
    // between stars, where there is no drawing and no target.
    const TOptional<int32> Orbit = TargetOrbit(Subsystem);
    if (!Orbit)
    {
        JumpButton->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    const TOptional<FBodyId> Target = Subsystem.GetTarget();
    const TOptional<FBodyId> Course = Subsystem.GetPlottedWorld();
    const bool bCourseIsTarget = Course && Target && *Course == *Target;
    const bool bNear = !bCourseIsTarget && Subsystem.IsNearEnoughToFly(Drawing->System, *Target);
    // Nothing can be plotted or cleared in the fold; the words stay as they
    // were when it opened.
    const bool bCanPress = !Subsystem.IsInTransit() && !bNear;

    JumpButton->SetVisibility(ESlateVisibility::Visible);
    JumpButton->SetIsEnabled(bCanPress);
    JumpLabel->SetText(bCourseIsTarget ? NSLOCTEXT("DeepSpace", "MapStandDown", "Stand down")
                     : bNear ? NSLOCTEXT("DeepSpace", "MapNearEnough", "Near enough to fly")
                             : NSLOCTEXT("DeepSpace", "MapJumpHere", "Jump here"));
    JumpLabel->SetColorAndOpacity(FSlateColor(bCanPress ? Accent : Dim));
}

TOptional<int32> USystemMapWidget::TargetOrbit(const UShipSubsystem& Subsystem) const
{
    const TOptional<FBodyId> Target = Subsystem.GetTarget();
    if (!Drawing || !Target || !ShipNav::TargetPlanet(Drawing->System, *Target))
    {
        return {};
    }
    return Target->Planet;
}

void USystemMapWidget::SelectWorld(int32 Orbit)
{
    // The drawing is brought up to date first, and a press made on a
    // drawing that did not survive it picks nothing: Orbit is an index into
    // what was on the glass when it was pressed, and a row or a dot is a
    // world of that system, never of the one drawn in its place.
    const int32 PressedOn = LayoutAsked;
    RefreshFromShip();
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem || !Drawing || LayoutAsked != PressedOn)
    {
        return;
    }
    const TOptional<SystemMap::FMapSelection> Selection = SystemMap::Select(Drawing->System, Orbit, Subsystem->GetTarget());
    if (!Selection)
    {
        return;
    }
    // Not the pilot's alone: anyone at the map marks the one target the ship
    // has, and everyone aboard sees it.
    UShipSubsystem* Marker = Ship();
    if (Selection->Action == SystemMap::EMapSelect::Clear)
    {
        Marker->ClearTarget();
    }
    else
    {
        Marker->SetTarget(Selection->Body);
    }
    // So the mark and the band move in the frame of the click.
    RefreshFromShip();
}

void USystemMapWidget::PressJump()
{
    UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return;
    }
    const TOptional<FBodyId> Target = Subsystem->GetTarget();
    const TOptional<FBodyId> Course = Subsystem->GetPlottedWorld();
    if (Course && Target && *Course == *Target)
    {
        // Stand down: clearing the course stands the jump down, as on the
        // chart. The target stays; it was chosen before the jump was.
        Subsystem->ClearCourse();
    }
    else if (Subsystem->PlotTarget())
    {
        // One press plots and engages: the in-system jump is chosen here, on
        // the map, and needs no second screen. It is still the one engage
        // lever, which the chart shows and can stand down.
        Subsystem->SetJumpEngaged(true);
    }
    RefreshFromShip();
}

void USystemMapWidget::PressRow(int32 Index)
{
    if (IsRowEnabled(Index))
    {
        RowButtons[Index]->OnClicked.Broadcast();
    }
}

void USystemMapWidget::HandleRow0() { SelectWorld(0); }
void USystemMapWidget::HandleRow1() { SelectWorld(1); }
void USystemMapWidget::HandleRow2() { SelectWorld(2); }
void USystemMapWidget::HandleRow3() { SelectWorld(3); }
void USystemMapWidget::HandleRow4() { SelectWorld(4); }
void USystemMapWidget::HandleRow5() { SelectWorld(5); }
void USystemMapWidget::HandleRow6() { SelectWorld(6); }
void USystemMapWidget::HandleRow7() { SelectWorld(7); }
void USystemMapWidget::HandleRow8() { SelectWorld(8); }
void USystemMapWidget::HandleRow9() { SelectWorld(9); }
void USystemMapWidget::HandleRow10() { SelectWorld(10); }
void USystemMapWidget::HandleRow11() { SelectWorld(11); }
void USystemMapWidget::HandleJump() { PressJump(); }

int32 USystemMapWidget::GetShownRowCount() const
{
    int32 Shown = 0;
    for (const TObjectPtr<UButton>& Row : RowButtons)
    {
        Shown += (Row && Row->GetVisibility() != ESlateVisibility::Collapsed) ? 1 : 0;
    }
    return Shown;
}

FText USystemMapWidget::GetRowText(int32 Index) const
{
    if (!RowButtons.IsValidIndex(Index) || !RowButtons[Index]
        || RowButtons[Index]->GetVisibility() == ESlateVisibility::Collapsed)
    {
        return FText::GetEmpty();
    }

    // Read back off the row, so a test of this is a test of the glass. The
    // mark and the name are one part, as the chart's are.
    const FString Mark = RowMarks[Index]->GetText().ToString();
    const FString Name = RowNames[Index]->GetText().ToString();
    TArray<FString> Parts;
    Parts.Add(Mark.IsEmpty() ? Name : Mark + TEXT(" ") + Name);
    for (const TArray<TObjectPtr<UTextBlock>>* Column : {&RowKinds, &RowDistances})
    {
        const FString Part = (*Column)[Index]->GetText().ToString();
        if (!Part.IsEmpty())
        {
            Parts.Add(Part);
        }
    }
    return FText::FromString(FString::Join(Parts, NavText::Separator));
}

bool USystemMapWidget::IsRowEnabled(int32 Index) const
{
    return RowButtons.IsValidIndex(Index) && RowButtons[Index]
        && RowButtons[Index]->GetVisibility() != ESlateVisibility::Collapsed
        && RowButtons[Index]->GetIsEnabled();
}

FText USystemMapWidget::GetTitleText() const
{
    return TitlePlace ? TitlePlace->GetText() : FText::GetEmpty();
}

namespace
{
    /** What Slate would lay Widget out at, measured by a prepass of it. */
    FVector2D Measure(UWidget* Widget)
    {
        if (!Widget)
        {
            return FVector2D::ZeroVector;
        }
        const TSharedRef<SWidget> Built = Widget->TakeWidget();
        Built->SlatePrepass(1.0f);
        return Built->GetDesiredSize();
    }
}

FVector2D USystemMapWidget::MeasureFooter() const
{
    return Measure(Footer.Get());
}

FVector2D USystemMapWidget::MeasureJumpButton() const
{
    return Measure(JumpButton.Get());
}

FText USystemMapWidget::GetFooterText() const
{
    return Footer ? Footer->GetText() : FText::GetEmpty();
}

FText USystemMapWidget::GetTargetText() const
{
    return TargetLine ? TargetLine->GetText() : FText::GetEmpty();
}

bool USystemMapWidget::IsJumpButtonShown() const
{
    return JumpButton && JumpButton->GetVisibility() != ESlateVisibility::Collapsed;
}

FText USystemMapWidget::GetJumpButtonText() const
{
    return IsJumpButtonShown() && JumpLabel ? JumpLabel->GetText() : FText::GetEmpty();
}

bool USystemMapWidget::IsJumpButtonEnabled() const
{
    return IsJumpButtonShown() && JumpButton->GetIsEnabled();
}

void USystemMapWidget::PressJumpButton()
{
    if (IsJumpButtonEnabled())
    {
        JumpButton->OnClicked.Broadcast();
    }
}

const SystemMap::FMapLayout* USystemMapWidget::GetLayout() const
{
    return Drawing ? &Drawing->Layout : nullptr;
}
