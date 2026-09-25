#include "UI/ShipScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Ship/ShipSubsystem.h"

const FLinearColor UShipScreenWidget::Panel(0.012f, 0.030f, 0.036f, 1.0f);
const FLinearColor UShipScreenWidget::Ink(0.82f, 0.94f, 0.95f, 1.0f);
const FLinearColor UShipScreenWidget::Dim(0.38f, 0.52f, 0.54f, 1.0f);
const FLinearColor UShipScreenWidget::Accent(0.05f, 0.75f, 0.72f, 1.0f);

UShipScreenWidget::UShipScreenWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // World screens are not the player's focus and must never steal it: the
    // pointer drives them through a virtual user instead.
    SetIsFocusable(false);
}

UShipSubsystem* UShipScreenWidget::Ship() const
{
    return UShipSubsystem::Get(this);
}

UWidget* UShipScreenWidget::BuildScreen()
{
    return nullptr;
}

TSharedRef<SWidget> UShipScreenWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        WidgetTree->RootWidget = BuildScreen();
    }
    return Super::RebuildWidget();
}

UTextBlock* UShipScreenWidget::MakeText(const FText& Content, float Size, const FLinearColor& Colour)
{
    UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Block->SetText(Content);
    FSlateFontInfo Font = Block->GetFont();
    Font.Size = static_cast<int32>(Size);
    Block->SetFont(Font);
    Block->SetColorAndOpacity(FSlateColor(Colour));
    return Block;
}

UBorder* UShipScreenWidget::MakePanel(UWidget* Content)
{
    UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    Border->SetBrushColor(Panel);
    Border->SetPadding(FMargin(28.0f, 22.0f));
    Border->SetHorizontalAlignment(HAlign_Fill);
    Border->SetVerticalAlignment(VAlign_Fill);
    if (Content)
    {
        Border->SetContent(Content);
    }
    return Border;
}

UVerticalBox* UShipScreenWidget::MakeColumn()
{
    return WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
}

UProgressBar* UShipScreenWidget::MakeBar()
{
    UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
    Bar->SetFillColorAndOpacity(Accent);
    Bar->SetPercent(0.0f);
    return Bar;
}
