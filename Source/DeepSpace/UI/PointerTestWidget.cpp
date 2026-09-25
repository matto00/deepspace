#include "UI/PointerTestWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

UWidget* UPointerTestWidget::BuildScreen()
{
    UVerticalBox* Column = MakeColumn();

    Column->AddChildToVerticalBox(MakeText(
        NSLOCTEXT("DeepSpace", "PointerTestTitle", "POINTER TEST"), TitleSize, Accent));

    Readout = MakeText(
        NSLOCTEXT("DeepSpace", "PointerTestIdle", "no clicks"), BodySize, Ink);
    Column->AddChildToVerticalBox(Readout);

    UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    Button->OnClicked.AddDynamic(this, &UPointerTestWidget::HandleClicked);
    Button->SetContent(MakeText(
        NSLOCTEXT("DeepSpace", "PointerTestPress", "  PRESS  "), BodySize, Ink));
    UVerticalBoxSlot* ButtonSlot = Column->AddChildToVerticalBox(Button);
    ButtonSlot->SetPadding(FMargin(0.0f, 18.0f));

    USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
    Slider->SetSliderHandleColor(Accent);
    Slider->OnValueChanged.AddDynamic(this, &UPointerTestWidget::HandleSlid);
    Column->AddChildToVerticalBox(Slider);

    Bar = MakeBar();
    UVerticalBoxSlot* BarSlot = Column->AddChildToVerticalBox(Bar);
    BarSlot->SetPadding(FMargin(0.0f, 14.0f, 0.0f, 0.0f));

    return MakePanel(Column);
}

void UPointerTestWidget::HandleClicked()
{
    ++Clicks;
    if (Readout)
    {
        Readout->SetText(FText::Format(
            NSLOCTEXT("DeepSpace", "PointerTestClicks", "{0} clicks"), FText::AsNumber(Clicks)));
    }
}

void UPointerTestWidget::HandleSlid(float Value)
{
    Slid = Value;
    if (Bar)
    {
        Bar->SetPercent(Value);
    }
}
