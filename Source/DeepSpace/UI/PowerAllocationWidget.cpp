#include "UI/PowerAllocationWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"

UWidget* UPowerAllocationWidget::BuildScreen()
{
    UVerticalBox* Column = MakeColumn();
    Column->AddChildToVerticalBox(MakeText(
        NSLOCTEXT("DeepSpace", "AllocationTitle", "POWER"), TitleSize, Accent));

    Rows = MakeColumn();
    UVerticalBoxSlot* RowsSlot = Column->AddChildToVerticalBox(Rows);
    RowsSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 0.0f));

    RefreshRows();
    return MakePanel(Column);
}

FText UPowerAllocationWidget::NameOf(FName ConsumerId)
{
    if (ConsumerId == ShipPower::Lights)
    {
        return NSLOCTEXT("DeepSpace", "ConsumerLights", "LIGHTS");
    }
    if (ConsumerId == ShipPower::Boosters)
    {
        return NSLOCTEXT("DeepSpace", "ConsumerBoosters", "BOOSTERS");
    }
    if (ConsumerId == ShipPower::Engine)
    {
        return NSLOCTEXT("DeepSpace", "ConsumerEngine", "ENGINE");
    }
    return FText::FromName(ConsumerId);
}

void UPowerAllocationWidget::RefreshRows()
{
    if (!Rows)
    {
        return;
    }

    Rows->ClearChildren();
    Consumers.Reset();

    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return;
    }

    for (const FName& ConsumerId : Subsystem->GetPowerConsumers())
    {
        FRow Row;
        Row.ConsumerId = ConsumerId;

        Row.Label = MakeText(FText::GetEmpty(), BodySize, Ink);
        UVerticalBoxSlot* LabelSlot = Rows->AddChildToVerticalBox(Row.Label);
        LabelSlot->SetPadding(FMargin(0.0f, 10.0f, 0.0f, 2.0f));

        Row.Weight = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
        Row.Weight->SetMinValue(0.0f);
        Row.Weight->SetMaxValue(MaxWeight);
        Row.Weight->SetValue(Subsystem->GetConsumerWeight(ConsumerId));
        Row.Weight->SetSliderHandleColor(Accent);
        Row.Weight->OnValueChanged.AddDynamic(this, &UPowerAllocationWidget::HandleWeightChanged);
        Rows->AddChildToVerticalBox(Row.Weight);

        Row.Feed = MakeBar();
        UVerticalBoxSlot* FeedSlot = Rows->AddChildToVerticalBox(Row.Feed);
        FeedSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));

        Consumers.Add(Row);
    }
}

void UPowerAllocationWidget::HandleWeightChanged(float Value)
{
    // Slate hands a value, not a sender, so the row is found by asking which
    // slider no longer agrees with the ship. That keeps the subsystem the
    // single source of truth: the sliders are a view of it, and a row that
    // has not moved has nothing to say.
    UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return;
    }
    for (const FRow& Row : Consumers)
    {
        if (Row.Weight && !FMath::IsNearlyEqual(
                Row.Weight->GetValue(), Subsystem->GetConsumerWeight(Row.ConsumerId)))
        {
            Subsystem->SetConsumerWeight(Row.ConsumerId, Row.Weight->GetValue());
        }
    }
}

void UPowerAllocationWidget::SetRowWeight(FName ConsumerId, float Weight)
{
    for (const FRow& Row : Consumers)
    {
        if (Row.ConsumerId == ConsumerId && Row.Weight)
        {
            Row.Weight->SetValue(FMath::Clamp(Weight, 0.0f, MaxWeight));
            HandleWeightChanged(Row.Weight->GetValue());
            return;
        }
    }
}

FText UPowerAllocationWidget::DescribeRow(FName ConsumerId) const
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return NameOf(ConsumerId);
    }

    // Watts, which is a fact about the ship, and nothing that implies a
    // score. "180 W of 300 W" describes the split; "60% efficient" would
    // invent a target the player is failing to hit.
    return FText::Format(
        NSLOCTEXT("DeepSpace", "AllocationRow", "{0}   {1} W of {2} W"),
        NameOf(ConsumerId),
        FText::AsNumber(FMath::RoundToInt(Subsystem->GetConsumerShare(ConsumerId))),
        FText::AsNumber(FMath::RoundToInt(Subsystem->GetConsumerWant(ConsumerId))));
}

FText UPowerAllocationWidget::GetRowText(FName ConsumerId) const
{
    return DescribeRow(ConsumerId);
}

float UPowerAllocationWidget::GetRowWeight(FName ConsumerId) const
{
    for (const FRow& Row : Consumers)
    {
        if (Row.ConsumerId == ConsumerId && Row.Weight)
        {
            return Row.Weight->GetValue();
        }
    }
    return 0.0f;
}

void UPowerAllocationWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    RefreshFromShip();
}

void UPowerAllocationWidget::RefreshFromShip()
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return;
    }

    for (const FRow& Row : Consumers)
    {
        if (Row.Label)
        {
            Row.Label->SetText(DescribeRow(Row.ConsumerId));
        }
        if (Row.Feed)
        {
            Row.Feed->SetPercent(Subsystem->GetConsumerSatisfaction(Row.ConsumerId));
        }
        // Another screen may have moved this weight. The slider follows the
        // ship rather than the other way round, except while it is being
        // dragged -- which is what makes two screens views of one value.
        if (Row.Weight && !Row.Weight->HasMouseCapture())
        {
            const float Authoritative = Subsystem->GetConsumerWeight(Row.ConsumerId);
            if (!FMath::IsNearlyEqual(Row.Weight->GetValue(), Authoritative))
            {
                Row.Weight->SetValue(Authoritative);
            }
        }
    }
}
