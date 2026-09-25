#include "UI/ShipHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipSubsystem.h"
#include "UI/ShipScreenWidget.h"

namespace
{
    TAutoConsoleVariable<int32> CVarHUD(
        TEXT("ds.HUD"), 1,
        TEXT("Draw the HUD (0 hides it entirely, for an unadorned view)."),
        ECVF_Default);

    // The dot at rest and at its largest, in slate units. Small on purpose:
    // it should be findable when looked for and invisible when not.
    constexpr float DotIdle = 3.0f;
    constexpr float DotFull = 7.0f;

    constexpr float Margin = 42.0f;

    /** A dash reads as "no reading", where a zero would read as a measurement. */
    const FText Blank = NSLOCTEXT("DeepSpace", "HUDBlank", "-----");
}

UShipHUDWidget::UShipHUDWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // The HUD is painted over the world and never takes input: the pointer
    // must reach the screens behind it, and nothing here is clickable.
    SetIsFocusable(false);
}

ADeepSpaceCharacter* UShipHUDWidget::Player() const
{
    return GetOwningPlayerPawn<ADeepSpaceCharacter>();
}

UShipSubsystem* UShipHUDWidget::Ship() const
{
    return UShipSubsystem::Get(this);
}

UTextBlock* UShipHUDWidget::MakeReadout(const FText& Content, const FLinearColor& Colour, float Size)
{
    UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Block->SetText(Content);
    FSlateFontInfo Font = Block->GetFont();
    Font.Size = static_cast<int32>(Size);
    Font.LetterSpacing = 160;   // airy, so a short line still reads as a label
    Block->SetFont(Font);
    Block->SetColorAndOpacity(FSlateColor(Colour));
    return Block;
}

void UShipHUDWidget::PlaceCorner(UWidget* Widget, const FVector2D& Anchor, const FVector2D& Offset)
{
    UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(Widget->Slot);
    if (!Slot)
    {
        return;
    }
    Slot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
    Slot->SetAlignment(Anchor);          // pin the widget's own matching corner
    Slot->SetAutoSize(true);
    Slot->SetPosition(Offset);
}

UCanvasPanel* UShipHUDWidget::BuildLayout()
{
    UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());

    // The dot. A rounded box with its radius at half its size is a circle,
    // which avoids needing a texture for four pixels of crosshair.
    Dot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    Brush.OutlineSettings = FSlateBrushOutlineSettings(DotFull);
    Brush.TintColor = FSlateColor(UShipScreenWidget::Ink);
    Dot->SetBrush(Brush);
    Canvas->AddChild(Dot);
    if (UCanvasPanelSlot* DotSlot = Cast<UCanvasPanelSlot>(Dot->Slot))
    {
        DotSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        DotSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        DotSlot->SetSize(FVector2D(DotIdle, DotIdle));
    }

    // The prompt sits under the dot rather than at the screen's edge, so
    // reading it does not mean looking away from the thing it describes.
    Prompt = MakeReadout(FText::GetEmpty(), UShipScreenWidget::Ink, 13.0f);
    Canvas->AddChild(Prompt);
    if (UCanvasPanelSlot* PromptSlot = Cast<UCanvasPanelSlot>(Prompt->Slot))
    {
        PromptSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        PromptSlot->SetAlignment(FVector2D(0.5f, 0.0f));
        PromptSlot->SetAutoSize(true);
        PromptSlot->SetPosition(FVector2D(0.0f, 34.0f));
    }

    // Corners: what ship, where, what it is running on, what it is doing.
    ShipLine  = MakeReadout(NSLOCTEXT("DeepSpace", "HUDShip", "HAULER"), UShipScreenWidget::Ink, 12.0f);
    PlaceLine = MakeReadout(Blank, UShipScreenWidget::Dim, 10.0f);
    PowerLine = MakeReadout(Blank, UShipScreenWidget::Ink, 12.0f);
    DriveLine = MakeReadout(Blank, UShipScreenWidget::Dim, 10.0f);
    MotionLine = MakeReadout(Blank, UShipScreenWidget::Ink, 12.0f);
    HoldLine  = MakeReadout(Blank, UShipScreenWidget::Dim, 10.0f);

    const TArray<UWidget*> Corners = {ShipLine, PlaceLine, PowerLine,
                                      DriveLine, MotionLine, HoldLine};
    for (UWidget* Widget : Corners)
    {
        Canvas->AddChild(Widget);
    }

    PlaceCorner(ShipLine,   FVector2D(0.0f, 0.0f), FVector2D(Margin, Margin));
    PlaceCorner(PlaceLine,  FVector2D(0.0f, 0.0f), FVector2D(Margin, Margin + 20.0f));
    PlaceCorner(PowerLine,  FVector2D(1.0f, 0.0f), FVector2D(-Margin, Margin));
    PlaceCorner(DriveLine,  FVector2D(1.0f, 0.0f), FVector2D(-Margin, Margin + 20.0f));
    PlaceCorner(MotionLine, FVector2D(0.0f, 1.0f), FVector2D(Margin, -Margin));
    PlaceCorner(HoldLine,   FVector2D(0.0f, 1.0f), FVector2D(Margin, -Margin - 20.0f));

    return Canvas;
}

TSharedRef<SWidget> UShipHUDWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        WidgetTree->RootWidget = BuildLayout();
    }
    return Super::RebuildWidget();
}

void UShipHUDWidget::SetTarget(ETarget NewTarget)
{
    Target = NewTarget;
}

void UShipHUDWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);

    const bool bShow = CVarHUD.GetValueOnGameThread() != 0;
    SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (!bShow)
    {
        return;
    }

    const ADeepSpaceCharacter* Character = Player();
    const UShipSubsystem* ShipState = Ship();

    // The dot: what is under it decides how much of it there is.
    // Sat at a screen there is a real cursor, and a crosshair behind it is
    // just a second thing to look at.
    const bool bCursor = Character && Character->IsUsingScreen();
    if (Dot)
    {
        Dot->SetVisibility(bCursor ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    }
    if (Character)
    {
        SetTarget(Character->GetFocusedInteractable() ? ETarget::Interactable
                  : Character->IsPointingAtScreen()   ? ETarget::Screen
                                                      : ETarget::Nothing);
    }
    const float Wanted = (Target == ETarget::Nothing) ? 0.0f : 1.0f;
    Emphasis = FMath::FInterpTo(Emphasis, Wanted, DeltaSeconds, 14.0f);

    if (Dot)
    {
        const float Size = FMath::Lerp(DotIdle, DotFull, Emphasis);
        if (UCanvasPanelSlot* DotSlot = Cast<UCanvasPanelSlot>(Dot->Slot))
        {
            DotSlot->SetSize(FVector2D(Size, Size));
        }
        const FLinearColor Colour = FMath::Lerp(
            UShipScreenWidget::Dim,
            Target == ETarget::Screen ? UShipScreenWidget::Accent : UShipScreenWidget::Ink,
            Emphasis);
        Dot->SetBrushColor(Colour);
    }

    if (Prompt && Character)
    {
        Prompt->SetText(Character->GetCurrentPrompt());
    }

    if (!ShipState)
    {
        return;
    }

    // Watts, not percentages: a proportion invites optimising, a quantity
    // just says what is so (docs/vision.md, the anti-chore principle).
    if (PowerLine)
    {
        PowerLine->SetText(FText::FromString(FString::Printf(
            TEXT("%.0f W  SPARE %.0f W"),
            ShipState->GetReactorOutput(), FMath::Max(0.0f, ShipState->GetPowerHeadroom()))));
    }

    if (MotionLine)
    {
        const float Speed = ShipState->GetShipSpeed();
        MotionLine->SetText(Speed > 1.0f
            ? FText::FromString(FString::Printf(TEXT("%.0f M/S"), Speed * 0.01f))
            : NSLOCTEXT("DeepSpace", "HUDStationary", "STATIONARY"));
    }
}
