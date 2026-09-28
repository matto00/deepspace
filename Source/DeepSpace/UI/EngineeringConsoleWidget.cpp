#include "UI/EngineeringConsoleWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipSubsystem.h"
#include "Universe/UniverseUnits.h"

UWidget* UEngineeringConsoleWidget::BuildScreen()
{
    UVerticalBox* Column = MakeColumn();

    Column->AddChildToVerticalBox(MakeText(
        NSLOCTEXT("DeepSpace", "ConsoleTitle", "ENGINEERING"), TitleSize, Accent));

    Readout = MakeText(FText::GetEmpty(), PlateSize, Ink);
    UVerticalBoxSlot* ReadoutSlot = Column->AddChildToVerticalBox(Readout);
    ReadoutSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 24.0f));

    UButton* Switch = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    Switch->OnClicked.AddDynamic(this, &UEngineeringConsoleWidget::ToggleLights);
    LightsLabel = MakeText(FText::GetEmpty(), BodySize, Ink);
    Switch->SetContent(LightsLabel);
    Column->AddChildToVerticalBox(Switch);

    return MakePanel(Column);
}

void UEngineeringConsoleWidget::ToggleLights()
{
    if (UShipSubsystem* Subsystem = Ship())
    {
        Subsystem->SetLightsOn(!Subsystem->AreLightsOn());
    }
}

FString UEngineeringConsoleWidget::Nameplate(EShipBay Bay, const UShipModuleDataAsset& Part)
{
    // The part's own numbers, over stock for any it lacks (decision 3) --
    // never the ship's, which a console variable or wear could move.
    FShipRatings Rated = FShipRatings::Stock();
    ShipParts::Apply(Rated, Part.Ratings);
    FNumberFormattingOptions Tenths;
    Tenths.SetMaximumFractionalDigits(1);
    const auto Watts = [](double Value) { return FText::AsNumber(FMath::RoundToInt(Value)).ToString() + TEXT(" W"); };

    FString Figure;
    switch (Bay)
    {
    case EShipBay::Reactor:     Figure = Watts(Rated.ReactorWatts); break;
    case EShipBay::Drive:       Figure = FText::AsNumber(Rated.DriveResponse, &Tenths).ToString() + TEXT(" notches/s"); break;
    case EShipBay::Boosters:    Figure = FText::AsNumber(Rated.LinearAcceleration / UniverseUnits::CmPerKm, &Tenths).ToString() + TEXT(" km/s²"); break;
    case EShipBay::Lights:      Figure = Watts(Rated.LightsWant); break;
    case EShipBay::LifeSupport: Figure = Watts(Part.PowerDraw); break;
    case EShipBay::Sensors:     Figure = FText::AsNumber(Rated.RangeLy, &Tenths).ToString() + TEXT(" ly"); break;
    default:                    break;
    }

    const FString Label = ShipBay::PlateLabel(Bay);
    const FString Name = Part.DisplayName.ToString();
    const FString Words = Part.Words.ToString();
    return Figure.IsEmpty()
        ? FString::Printf(TEXT("%-12s  %-24s  %s"), *Label, *Name, *Words)
        : FString::Printf(TEXT("%-12s  %-24s  %-14s  %s"), *Label, *Name, *Figure, *Words);
}

FText UEngineeringConsoleWidget::GetReadoutText() const
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return FText::GetEmpty();
    }

    // Read live, from the authority. The screen holds no copy: which part is
    // in which bay is the ship's to say, every frame.
    TArray<FString> Lines;
    for (const EShipBay Bay : ShipBay::All())
    {
        if (const UShipModuleDataAsset* Part = Subsystem->GetFittedPart(Bay))
        {
            Lines.Add(Nameplate(Bay, *Part));
        }
    }
    return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

void UEngineeringConsoleWidget::RefreshFromShip()
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return;
    }
    if (Readout)
    {
        Readout->SetText(GetReadoutText());
    }
    if (LightsLabel)
    {
        LightsLabel->SetText(Subsystem->AreLightsOn()
            ? NSLOCTEXT("DeepSpace", "LightsOff", "  LIGHTS OFF  ")
            : NSLOCTEXT("DeepSpace", "LightsOn", "  LIGHTS ON  "));
    }
}

FText UEngineeringConsoleWidget::GetShownText() const
{
    return Readout ? Readout->GetText() : FText::GetEmpty();
}

void UEngineeringConsoleWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    RefreshFromShip();
}
