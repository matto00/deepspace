#include "UI/ShipHUDWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "UI/NavText.h"
#include "UI/ShipScreenWidget.h"
#include "UI/ShipTargetOverlay.h"
#include "UI/TargetMarker.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

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

    /** The caret's ring, slate units: wide enough that the marker's six
     *  pixels sit inside it with room to see they are centred, and thin, so
     *  it frames the marker rather than covering it. */
    constexpr float CaretSize = 16.0f;
    constexpr float CaretLine = 1.5f;

    /** Any distance projects to the same place from a point taken relative to
     *  the camera; 100 km keeps it far inside float range after the view
     *  transform. */
    constexpr double CaretDistance = 1.0e7;

    /** A dash reads as "no reading", where a zero would read as a measurement. */
    const FText Blank = NSLOCTEXT("DeepSpace", "HUDBlank", "-----");
}

const FName UShipHUDWidget::NoseCaretName(TEXT("NoseCaret"));
const FName UShipHUDWidget::TargetLineName(TEXT("TargetLine"));
const FName UShipHUDWidget::TargetOverlayName(TEXT("TargetOverlay"));

UShipHUDWidget::UShipHUDWidget(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // The HUD is painted over the world and never takes input: the pointer
    // must reach the screens behind it, and nothing here is clickable.
    SetIsFocusable(false);
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

    // The target's marks, under everything else: the bracket, the chevron
    // and the prograde mark are drawn over the glass, and the corners' text
    // and the caret read over them. It fills the canvas, so its own space is
    // the one the caret is placed in, and it is built hidden until there is
    // a target to mark (system map decision 7).
    Overlay = WidgetTree->ConstructWidget<UShipTargetOverlay>(UShipTargetOverlay::StaticClass(), TargetOverlayName);
    Overlay->SetVisibility(ESlateVisibility::Collapsed);
    Canvas->AddChild(Overlay);
    if (UCanvasPanelSlot* OverlaySlot = Cast<UCanvasPanelSlot>(Overlay->Slot))
    {
        OverlaySlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
        OverlaySlot->SetOffsets(FMargin(0.0f));
    }

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

    // The nose caret, hidden until there is a course to put it on. A ring
    // rather than a chevron: it has a centre, and aligned is "the teal point
    // is in the ring". Ink, not the accent, so it never reads as a second
    // marker. Found again by name in NativeTick; it is not a member.
    UBorder* Caret = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), NoseCaretName);
    FSlateBrush Ring;
    Ring.DrawAs = ESlateBrushDrawType::RoundedBox;
    Ring.TintColor = FSlateColor(FLinearColor::Transparent);
    Ring.OutlineSettings = FSlateBrushOutlineSettings(FSlateColor(UShipScreenWidget::Ink), CaretLine);
    Caret->SetBrush(Ring);
    Caret->SetVisibility(ESlateVisibility::Collapsed);
    Canvas->AddChild(Caret);
    if (UCanvasPanelSlot* CaretSlot = Cast<UCanvasPanelSlot>(Caret->Slot))
    {
        CaretSlot->SetAnchors(FAnchors(0.0f, 0.0f));
        CaretSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        CaretSlot->SetSize(FVector2D(CaretSize, CaretSize));
    }

    // The prompt lives in a corner, not under the dot. The dot already says
    // "there is something here"; what the key is belongs somewhere the eye
    // learns to find, and text next to the thing you are looking at competes
    // with the thing you are looking at -- at a laptop it landed squarely on
    // top of the screen it was describing.
    Prompt = MakeReadout(FText::GetEmpty(), UShipScreenWidget::Ink, 13.0f);
    Canvas->AddChild(Prompt);
    PlaceCorner(Prompt, FVector2D(1.0f, 1.0f), FVector2D(-Margin, -Margin));

    // Corners: what ship, where, what it is running on, what it is doing.
    ShipLine  = MakeReadout(NSLOCTEXT("DeepSpace", "HUDShip", "HAULER"), UShipScreenWidget::Ink, 12.0f);
    PlaceLine = MakeReadout(Blank, UShipScreenWidget::Dim, 10.0f);
    PowerLine = MakeReadout(Blank, UShipScreenWidget::Ink, 12.0f);
    JumpLine = MakeReadout(Blank, UShipScreenWidget::Dim, 10.0f);
    // Empty rather than a dash with no target: the corner does not grow a
    // placeholder for something the player never asked for.
    TargetLine = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TargetLineName);
    {
        FSlateFontInfo Font = TargetLine->GetFont();
        Font.Size = 10;
        Font.LetterSpacing = 160;
        TargetLine->SetFont(Font);
        TargetLine->SetColorAndOpacity(FSlateColor(UShipScreenWidget::Dim));
    }
    AltitudeReadout = MakeReadout(Blank, UShipScreenWidget::Dim, 10.0f);

    // The motion line is two blocks side by side, the live lever in ink and
    // the other one dim, so the speed F would go to is on screen and plainly
    // not the one the ship is answering (decision 7). Same size, so they read
    // as one line.
    UHorizontalBox* Motion = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    MotionInk = MakeReadout(Blank, UShipScreenWidget::Ink, 12.0f);
    MotionDim = MakeReadout(FText::GetEmpty(), UShipScreenWidget::Dim, 12.0f);
    Motion->AddChildToHorizontalBox(MotionInk)->SetVerticalAlignment(VAlign_Bottom);
    Motion->AddChildToHorizontalBox(MotionDim)->SetVerticalAlignment(VAlign_Bottom);

    const TArray<UWidget*> Corners = {ShipLine, PlaceLine, PowerLine,
                                      JumpLine, TargetLine, Motion, AltitudeReadout};
    for (UWidget* Widget : Corners)
    {
        Canvas->AddChild(Widget);
    }

    PlaceCorner(ShipLine,   FVector2D(0.0f, 0.0f), FVector2D(Margin, Margin));
    PlaceCorner(PlaceLine,  FVector2D(0.0f, 0.0f), FVector2D(Margin, Margin + 20.0f));
    PlaceCorner(PowerLine,  FVector2D(1.0f, 0.0f), FVector2D(-Margin, Margin));
    PlaceCorner(JumpLine,   FVector2D(1.0f, 0.0f), FVector2D(-Margin, Margin + 20.0f));
    // Under the jump: the course and the target, the two things chosen,
    // read together -- and apart, since they are two lines.
    PlaceCorner(TargetLine, FVector2D(1.0f, 0.0f), FVector2D(-Margin, Margin + 40.0f));
    PlaceCorner(Motion,     FVector2D(0.0f, 1.0f), FVector2D(Margin, -Margin));
    // Over the speed: how fast and how far from anything read together.
    PlaceCorner(AltitudeReadout, FVector2D(0.0f, 1.0f), FVector2D(Margin, -Margin - 20.0f));

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
    Refresh(DeltaSeconds, GetOwningPlayer());
}

void UShipHUDWidget::Refresh(float DeltaSeconds, APlayerController* Controller)
{
    const bool bShow = CVarHUD.GetValueOnGameThread() != 0;
    SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (!bShow)
    {
        return;
    }

    const ADeepSpaceCharacter* Character = Controller ? Cast<ADeepSpaceCharacter>(Controller->GetPawn()) : nullptr;
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
        const FText What = Character->GetCurrentPrompt();
        Prompt->SetText(What.IsEmpty()
            ? FText::GetEmpty()
            : FText::Format(NSLOCTEXT("DeepSpace", "HUDPrompt", "(E)  {0}"), What));
    }

    if (!ShipState)
    {
        PlaceNoseCaret(nullptr, {});
        if (Overlay)
        {
            Overlay->SetVisibility(ESlateVisibility::Collapsed);
        }
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

    if (MotionInk && MotionDim)
    {
        // Both levers are persistent and invisible, so both are said here,
        // beside the speed they make: a lever set an hour ago is never a
        // surprise one F away.
        const FMotionWords Motion = MotionLineOf(*ShipState);
        MotionInk->SetText(FText::FromString(Motion.Ink));
        MotionDim->SetText(FText::FromString(Motion.Dim));
    }

    // Where the ship is: the system asked of its position, never remembered,
    // and asked once this frame for everything that needs it -- the lines,
    // the caret and the target's marks. The altitude wants only its
    // surfaces, so nothing out to the neighbours is searched.
    const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(this);
    const TOptional<FStarSystem> Here = (Universe && !ShipState->IsInTransit())
        ? Universe->GetSystemAt(ShipState->GetFlightState().GetUniversePosition())
        : TOptional<FStarSystem>();

    PlaceNoseCaret(ShipState, Here);
    if (Overlay)
    {
        Overlay->PlaceFor(*ShipState, Here, Controller);
    }

    if (AltitudeReadout)
    {
        AltitudeReadout->SetText(AltitudeLineText(*ShipState, LocalSystem::Here(Here)));
    }

    if (PlaceLine)
    {
        PlaceLine->SetText(PlaceLineText(*ShipState, Here));
    }

    // The jump, in words and never a number, with the bearing to the course
    // relative to the ship rather than to a free-looking head: everything
    // aiming needs, from the pilot's seat alone (nav decision 3).
    if (JumpLine)
    {
        JumpLine->SetText(JumpLineText(*ShipState, Universe));
    }

    // The target: where it is from the nose, how far, and when the ship's
    // path brings it down on it -- live, at the present speed (ruling 3).
    if (TargetLine)
    {
        TargetLine->SetText(TargetLineText(*ShipState, Here));
    }
}

FText UShipHUDWidget::PlaceLineText(const UShipSubsystem& ShipState, const TOptional<FStarSystem>& Here)
{
    if (ShipState.IsInTransit())
    {
        // Folding to a world of this system is not between stars: the ship
        // has not left, and saying so would tell the pilot it had.
        return FText::FromString(NavText::Jump(EJumpState::Transit, ShipState.GetPlottedWorld().IsSet()));
    }
    return Here ? FText::FromString(NavText::Place(Here->Stub.Name, Here->Star.Class)) : Blank;
}

FText UShipHUDWidget::TargetLineText(const UShipSubsystem& ShipState, const TOptional<FStarSystem>& Here)
{
    // The ship's own view of it, printed: the map prints the same view, so
    // the two can never disagree (ScreensAgree).
    const TOptional<FTargetView> View = Here ? ShipState.GetTargetView(*Here) : TOptional<FTargetView>();
    return View ? FText::FromString(TargetMarker::Line(*View)) : FText::GetEmpty();
}

void UShipHUDWidget::PlaceNoseCaret(const UShipSubsystem* ShipState, const TOptional<FStarSystem>& Here)
{
    UWidget* Caret = WidgetTree ? WidgetTree->FindWidget(NoseCaretName) : nullptr;
    if (!Caret)
    {
        return;
    }
    APlayerController* Controller = GetOwningPlayer();
    const APlayerCameraManager* Camera = Controller ? Controller->PlayerCameraManager.Get() : nullptr;

    // Projected rather than drawn at the screen's centre: at the helm the
    // mouse keeps looking, so the view is not the nose, and the caret must
    // say where the nose is wherever the head has turned.
    FVector2D Position = FVector2D::ZeroVector;
    bool bShow = ShipState && Camera && ShowsNoseCaret(*ShipState, GetOwningPlayerPawn(), Here)
        && UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
               Controller, NoseCaretWorldPoint(Camera->GetCameraLocation()), Position, false);
    if (bShow)
    {
        // Off the edge of the view it is hidden, not pinned to the border: a
        // caret at the edge would claim the nose is there.
        const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), UE_KINDA_SMALL_NUMBER);
        const FVector2D Size = UWidgetLayoutLibrary::GetViewportSize(this) / Scale;
        bShow = Position.X >= 0.0 && Position.Y >= 0.0 && Position.X <= Size.X && Position.Y <= Size.Y;
    }

    Caret->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (bShow)
    {
        if (UCanvasPanelSlot* CaretSlot = Cast<UCanvasPanelSlot>(Caret->Slot))
        {
            CaretSlot->SetPosition(Position);
        }
    }
}

bool UShipHUDWidget::ShowsNoseCaret(const UShipSubsystem& ShipState, const APawn* Viewer,
                                    const TOptional<FStarSystem>& Here)
{
    if (!Viewer || ShipState.GetPilot() != Viewer || ShipState.IsInTransit())
    {
        return false;
    }
    // Something to aim at: a course of either kind, or a target that names
    // a world of the system in hand -- one held from another system, after
    // a PlaceShip, names nothing here and gives the nose nothing to meet.
    const TOptional<FBodyId> Target = ShipState.GetTarget();
    return ShipState.HasCourse() || (Here && Target && ShipNav::TargetPlanet(*Here, *Target) != nullptr);
}

FVector UShipHUDWidget::NoseCaretWorldPoint(const FVector& CameraLocation)
{
    return CameraLocation + FVector::ForwardVector * CaretDistance;
}

FText UShipHUDWidget::JumpLineText(const UShipSubsystem& ShipState, const UUniverseSubsystem* Universe)
{
    const TOptional<FVector> Bearing = ShipState.GetCourseDirectionShipLocal();
    if (!Universe || !Bearing)
    {
        return Blank;
    }
    // A world course is named as the world, and its system is the world's
    // own id's -- asked of that, not of where the ship is, so the name holds
    // through the fold, when the HUD asks for no system at all.
    if (const TOptional<FBodyId> World = ShipState.GetPlottedWorld())
    {
        const TOptional<FStarSystem> System = Universe->GetSystem(World->System);
        const FPlanet* Planet = System ? ShipNav::TargetPlanet(*System, *World) : nullptr;
        return Planet
            ? FText::FromString(NavText::Jump(ShipState.GetJumpState(), true, NavText::WorldName(*Planet), *Bearing,
                                              ShipState.GetJumpConeRadians()))
            : Blank;
    }
    const TOptional<FSystemId> Course = ShipState.GetPlottedSystem();
    const TOptional<FStarSystem> Star = Course ? Universe->GetSystem(*Course) : TOptional<FStarSystem>();
    return Star
        ? FText::FromString(NavText::Jump(ShipState.GetJumpState(), Star->Stub.Name, *Bearing,
                                          ShipState.GetJumpConeRadians()))
        : Blank;
}

namespace
{
    /** 1234567 as "1,234,567": digits a pilot can read at a glance. */
    FString Grouped(int64 Value)
    {
        FString Digits = FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::Abs(Value)));
        for (int32 At = Digits.Len() - 3; At > 0; At -= 3)
        {
            Digits.InsertAt(At, TEXT(','));
        }
        return Value < 0 ? TEXT("-") + Digits : Digits;
    }
}

FString UShipHUDWidget::AltitudeWords(double Cm)
{
    // Every unit is chosen on the rounded value it would print, so a reading
    // never shows its own unit's ceiling -- "1000 M" -- before handing over.
    const double Metres = FMath::Max(Cm, 0.0) * 0.01;
    const int64 WholeMetres = FMath::RoundToInt64(Metres);
    if (WholeMetres < 1000)
    {
        return FString::Printf(TEXT("%lld M"), static_cast<long long>(WholeMetres));
    }
    const double Km = Metres * 0.001;
    const int64 TenthsKm = FMath::RoundToInt64(Km * 10.0);
    if (TenthsKm < 1000)
    {
        return FString::Printf(TEXT("%lld.%lld KM"), static_cast<long long>(TenthsKm / 10), static_cast<long long>(TenthsKm % 10));
    }
    const int64 WholeKm = FMath::RoundToInt64(Km);
    if (WholeKm < 10000)
    {
        return Grouped(WholeKm) + TEXT(" KM");
    }
    const double AU = Metres * 100.0 / UniverseUnits::CmPerAU;
    if (FMath::RoundToInt64(AU * 1000.0) < 10)
    {
        return Grouped(FMath::RoundToInt64(Km * 0.001)) + TEXT(" THOUSAND KM");
    }
    const int64 ThousandthsAU = FMath::RoundToInt64(AU * 1000.0);
    if (ThousandthsAU < 1000)
    {
        return FString::Printf(TEXT("0.%03lld AU"), static_cast<long long>(ThousandthsAU));
    }
    const int64 HundredthsAU = FMath::RoundToInt64(AU * 100.0);
    if (HundredthsAU < 10000)
    {
        return FString::Printf(TEXT("%lld.%02lld AU"), static_cast<long long>(HundredthsAU / 100), static_cast<long long>(HundredthsAU % 100));
    }
    return Grouped(FMath::RoundToInt64(AU)) + TEXT(" AU");
}

FString UShipHUDWidget::AltitudeLine(double AltitudeCm, const FString& Surface, bool bEdge, EFlightHold Hold)
{
    FString Line = AltitudeWords(AltitudeCm) + (bEdge ? FString(TEXT(" TO THE EDGE")) : TEXT(" ABOVE ") + Surface);
    switch (Hold)
    {
    case EFlightHold::HoldingOff:
        // The cap's word refers to whatever surface the nose is on, which
        // near a moon may not be the one named: it says what the ship is
        // doing, not which world is doing it.
        Line += NavText::Separator;
        Line += TEXT("HOLDING OFF");
        break;
    case EFlightHold::AtFloor:
        Line += NavText::Separator;
        Line += bEdge ? TEXT("AT THE EDGE") : TEXT("AT THE FLOOR");
        break;
    case EFlightHold::Free:
        break;
    }
    return Line;
}

EFlightHold UShipHUDWidget::ShownHold(EFlightHold Hold, double HeldFraction)
{
    return Hold == EFlightHold::HoldingOff && HeldFraction <= HoldingOffShown ? EFlightHold::Free : Hold;
}

FText UShipHUDWidget::AltitudeLineText(const UShipSubsystem& ShipState)
{
    return AltitudeLineText(ShipState, ShipState.IsInTransit() ? FSkySystem() : LocalSystem::Here(ShipState.GetWorld()));
}

FText UShipHUDWidget::AltitudeLineText(const UShipSubsystem& ShipState, const FSkySystem& Here)
{
    if (ShipState.IsInTransit())
    {
        return Blank;
    }
    const FShipFlightState& Flight = ShipState.GetFlightState();
    const FUniversePosition Where = Flight.GetUniversePosition();
    const FSkyNearestSurface Nearest = LocalSystem::NearestSurface(Here, Where);
    if (!Nearest.bEdge && !Here.Bodies.IsValidIndex(Nearest.Body))
    {
        return Blank;
    }
    // Asked of the flight state, the one thing that knows what the cap did
    // this step. Nothing here works it out again from the heading and the
    // lever.
    const EFlightHold Hold = ShownHold(Flight.GetHold(), Flight.GetHeldFraction());
    const FString Surface = Nearest.bEdge ? FString() : Here.Bodies[Nearest.Body].Id.ToString();
    return FText::FromString(AltitudeLine(Nearest.Distance, Surface, Nearest.bEdge, Hold));
}

namespace
{
    /**
     * A reading counted in tenths or hundredths, as "12.4" or "0.37", and
     * with bDropZeros, decimals that are all zero dropped: "50", "0.1". The
     * drive's notches are round numbers, and dropping them is what lets a
     * settled ship read exactly the label its lever was set to.
     */
    FString Decimal(int64 Counted, int32 Places, bool bDropZeros)
    {
        int64 Scale = 1;
        for (int32 Place = 0; Place < Places; ++Place)
        {
            Scale *= 10;
        }
        const int64 Whole = Counted / Scale;
        FString Fraction = FString::Printf(TEXT("%lld"), static_cast<long long>(Counted % Scale));
        while (Fraction.Len() < Places)
        {
            Fraction.InsertAt(0, TEXT('0'));
        }
        while (bDropZeros && Fraction.EndsWith(TEXT("0")))
        {
            Fraction.LeftChopInline(1);
        }
        const FString Front = Grouped(Whole);
        return Fraction.IsEmpty() ? Front : Front + TEXT(".") + Fraction;
    }

    /**
     * How close under a tenth of light still counts as one: the table's
     * 0.1 c is 0.1 x c in floating point, and read back as a fraction it
     * can come out a rounding error short. A billionth is far below anything
     * the readout can show and far above that error.
     */
    constexpr double OnLightThreshold = 1.0e-9;

    /** Nothing, as SpeedWords prints it. */
    const TCHAR* const NoSpeed = TEXT("0 M/S");

    /**
     * A lever named by the speed it asks for: STOP at rest, ASTERN behind.
     * STOP whenever what it asks for prints as nothing, not only at exactly
     * zero: cruise's lever is swept, and a hair of Shift in one short frame
     * can leave it a fraction of a metre a second above the detent, which
     * "CRUISE 0 M/S" would name as a setting when it is none.
     */
    FString LeverWords(const TCHAR* Name, double CmPerSecond)
    {
        const FString Asks = UShipHUDWidget::SpeedWords(FMath::Abs(CmPerSecond));
        if (Asks == NoSpeed)
        {
            return FString::Printf(TEXT("%s STOP"), Name);
        }
        return FString::Printf(TEXT("%s%s %s"), Name, CmPerSecond < 0.0 ? TEXT(" ASTERN") : TEXT(""), *Asks);
    }
}

FString UShipHUDWidget::SpeedWords(double CmPerSecond)
{
    return SpeedWordsKept(CmPerSecond, true);
}

FString UShipHUDWidget::SpeedReading(double CmPerSecond, double LeverCmPerSecond)
{
    // A moving reading keeps its decimals, so it does not change length as
    // it passes a round number -- "12.9", "13.0", "13.1" -- and nothing
    // drawn after it slides under the eye while the pilot aims by it. It
    // drops them only when it reads what its lever asks for: settled, the
    // ship and the lever say the same words (decision 3).
    const FString Label = SpeedWords(CmPerSecond);
    return Label == SpeedWords(FMath::Abs(LeverCmPerSecond)) ? Label : SpeedWordsKept(CmPerSecond, false);
}

FString UShipHUDWidget::SpeedWordsKept(double CmPerSecond, bool bDropZeros)
{
    // As AltitudeWords: every unit chosen on the rounded value it would
    // print, so a reading never shows its own unit's ceiling first.
    const double Metres = FMath::Max(CmPerSecond, 0.0) * 0.01;
    const int64 WholeMetres = FMath::RoundToInt64(Metres);
    if (WholeMetres < 1000)
    {
        return FString::Printf(TEXT("%lld M/S"), static_cast<long long>(WholeMetres));
    }
    const double Km = Metres * 0.001;
    const int64 TenthsKm = FMath::RoundToInt64(Km * 10.0);
    if (TenthsKm < 1000)
    {
        return Decimal(TenthsKm, 1, bDropZeros) + TEXT(" KM/S");
    }
    // Light takes over at a tenth of itself, the drive's top and the one
    // notch not in kilometres: 20,000 KM/S is the last notch in kilometres
    // and 0.1 C the first in light (the 2026-09-27 ruling; the seam was 0.01
    // c when the lever ran to 1 c). Below it every notch reads as its own
    // round label -- 5,000 KM/S, never "0.02 C" -- and a fraction of light
    // says what the top is.
    const double Light = FMath::Max(CmPerSecond, 0.0) / ShipDriveLever::LightCmPerSecond;
    if (Light < 0.1 * (1.0 - OnLightThreshold))
    {
        return Grouped(FMath::RoundToInt64(Km)) + TEXT(" KM/S");
    }
    const int64 HundredthsLight = FMath::RoundToInt64(Light * 100.0);
    if (HundredthsLight < 100)
    {
        return Decimal(HundredthsLight, 2, bDropZeros) + TEXT(" C");
    }
    // 0.1 C is as fast as the drive goes; hundredths above it to 1 C, and
    // whole multiples past that only so nothing can print as a fraction.
    return Grouped(FMath::RoundToInt64(Light)) + TEXT(" C");
}

UShipHUDWidget::FMotionWords UShipHUDWidget::MotionLineOf(const UShipSubsystem& ShipState)
{
    // Between stars the ship is folded, not flown: the flight state goes on
    // stepping with both levers at STOP, and would ease down to STATIONARY
    // while the ship crosses light years. The dash, as the altitude has.
    if (ShipState.IsInTransit())
    {
        FMotionWords Folded;
        Folded.Ink = Blank.ToString();
        return Folded;
    }
    return MotionLine(ShipState.GetFlightState());
}

UShipHUDWidget::FMotionWords UShipHUDWidget::MotionLine(const FShipFlightState& Flight)
{
    // "0 M/S" would claim a measurement of nothing; STATIONARY says what is so.
    const FString Moving = SpeedReading(Flight.GetSpeed(), Flight.GetLeverSpeed());
    const FString Now = Moving == NoSpeed ? FString(TEXT("STATIONARY")) : Moving;

    // Both levers asked of the ship, which holds them: the live one is the
    // one it is answering, the other is what F would go to. Cruise's is live
    // from the press of F, so while spooling down it is cruise's that is in
    // ink, and the words say why the speed is not yet what it asks.
    const EFlightMode Mode = Flight.GetMode();
    const TCHAR* Live = Mode == EFlightMode::Drive ? TEXT("DRIVE") : TEXT("CRUISE");
    const TCHAR* Other = Mode == EFlightMode::Drive ? TEXT("CRUISE") : TEXT("DRIVE");

    FMotionWords Words;
    Words.Ink = Now + NavText::Separator + LeverWords(Live, Flight.GetLeverSpeed());
    if (Mode == EFlightMode::SpoolingDown)
    {
        Words.Ink += NavText::Separator;
        Words.Ink += TEXT("SPOOLING DOWN");
    }
    Words.Dim = NavText::Separator + LeverWords(Other, Flight.GetOtherLeverSpeed());
    return Words;
}
