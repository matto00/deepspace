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
    UCanvasPanelSlot* FooterCell = Place(Canvas, Footer, FVector2D(Edge, FooterTop), FVector2D::ZeroVector);
    FooterCell->SetAutoSize(true);

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
    Asked.bBetweenStars = Ship.IsInTransit();
    // The stand-off the rim is fitted to. The default until stage 3 hands
    // the map UShipSubsystem::GetStandoffAU(), ds.Nav.StandoffAU as tuned.
    Asked.StandoffAU = NavStart::DefaultStandoffAU;

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

    if (Drawing)
    {
        const FShipFlightState& Flight = Subsystem->GetFlightState();
        View->SetShip(SystemMap::Ship(Drawing->Scale, Flight.GetUniversePosition(), Flight.GetUniverseOrientation()));
        View->SetTarget(TargetOrbit(*Subsystem));
    }
    RefreshRows(*Subsystem);
    RefreshFooter(*Subsystem);
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
        const FPlanet& Planet = System->Planets[Index];
        RowNames[Index]->SetText(FText::FromString(Planet.GivenName.IsEmpty() ? Drawn.Layout.Dots[Index].Numeral : Planet.GivenName));
        RowKinds[Index]->SetText(FText::FromString(NavText::WorldKind(Planet.Kind)));
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

void USystemMapWidget::RefreshFooter(const UShipSubsystem& Subsystem)
{
    TArray<FString> Lines;
    if (!Drawing)
    {
        Lines.Add(NavText::JumpWord(EJumpState::Transit) + TEXT("."));
    }
    else if (Drawing->System.Planets.IsEmpty())
    {
        Lines.Add(FString::Printf(TEXT("Nothing orbits %s."), *Drawing->System.Stub.Name));
    }
    else
    {
        const FShipFlightState& Flight = Subsystem.GetFlightState();
        const SystemMap::FMapShip Glyph = SystemMap::Ship(Drawing->Scale, Flight.GetUniversePosition(), Flight.GetUniverseOrientation());
        if (Glyph.Pin == SystemMap::EMapPin::Inside)
        {
            Lines.Add(FString::Printf(TEXT("Inside %s's orbit."), *Drawing->System.Planets[0].Designation));
        }
        else if (Glyph.Pin == SystemMap::EMapPin::Beyond)
        {
            Lines.Add(TEXT("Beyond the map."));
        }
        if (FMath::Abs(Glyph.ElevationDeg) > SystemMap::ElevationShownPastDeg)
        {
            Lines.Add(FString::Printf(TEXT("%d° %s the plane."), FMath::RoundToInt32(FMath::Abs(Glyph.ElevationDeg)),
                                      Glyph.ElevationDeg > 0.0 ? TEXT("above") : TEXT("below")));
        }
    }
    Footer->SetText(FText::FromString(FString::Join(Lines, TEXT(" "))));
}

TOptional<int32> USystemMapWidget::TargetOrbit(const UShipSubsystem& Subsystem) const
{
    return {};
}

TOptional<FBodyId> USystemMapWidget::TargetHeld(const UShipSubsystem& Subsystem) const
{
    return {};
}

void USystemMapWidget::SelectWorld(int32 Orbit)
{
    // The drawing is brought up to date first: a row is a world of the
    // system the ship is in now, never of the one it was last drawn for.
    RefreshFromShip();
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem || !Drawing)
    {
        return;
    }
    const TOptional<SystemMap::FMapSelection> Selection = SystemMap::Select(Drawing->System, Orbit, TargetHeld(*Subsystem));
    if (!Selection)
    {
        return;
    }
    OnSelected.Broadcast(*Selection);

    // Stage 3: act on it --
    //   Target: Ship()->SetTarget(Selection->Body)
    //   Clear:  Ship()->ClearTarget()
    // -- and refresh, so the mark moves this frame.
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

FText USystemMapWidget::GetFooterText() const
{
    return Footer ? Footer->GetText() : FText::GetEmpty();
}

FText USystemMapWidget::GetTargetText() const
{
    return TargetLine ? TargetLine->GetText() : FText::GetEmpty();
}

const SystemMap::FMapLayout* USystemMapWidget::GetLayout() const
{
    return Drawing ? &Drawing->Layout : nullptr;
}
