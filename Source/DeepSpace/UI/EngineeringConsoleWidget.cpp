#include "UI/EngineeringConsoleWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Ship/ShipSubsystem.h"

UWidget* UEngineeringConsoleWidget::BuildScreen()
{
    UVerticalBox* Column = MakeColumn();

    Column->AddChildToVerticalBox(MakeText(
        NSLOCTEXT("DeepSpace", "ConsoleTitle", "ENGINEERING"), TitleSize, Accent));

    Readout = MakeText(FText::GetEmpty(), BodySize, Ink);
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

FText UEngineeringConsoleWidget::GetReadoutText() const
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return FText::GetEmpty();
    }

    // Read live, from the authority. The screen holds no copy of any of this
    // -- which is the whole reason two screens showing the same allocation
    // cannot drift apart.
    return FText::Format(
        NSLOCTEXT("DeepSpace", "ConsoleReadoutLive",
                  "REACTOR   {0} W\nDRAWN     {1} W\nSPARE     {2} W"),
        FText::AsNumber(FMath::RoundToInt(Subsystem->GetReactorOutput())),
        FText::AsNumber(FMath::RoundToInt(Subsystem->GetPowerDraw())),
        FText::AsNumber(FMath::RoundToInt(Subsystem->GetPowerHeadroom())));
}

void UEngineeringConsoleWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);

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
