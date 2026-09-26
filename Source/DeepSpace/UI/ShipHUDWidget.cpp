#include "UI/ShipHUDWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Border.h"
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

    /**
     * How little room the drive may have left, as a fraction of its floor,
     * to be said to be at it. The drive closes a tenth of its room every
     * 1.5 s and never reaches the floor exactly, so without a band the words
     * would arrive at the heat death of the universe; with 5% they arrive at
     * "105 KM" over a 100 km floor, a minute and a half after 1,000 km.
     */
    TAutoConsoleVariable<float> CVarFloorBand(
        TEXT("ds.HUD.FloorBand"), 0.05f,
        TEXT("How close to the drive floor, as a fraction of it, the drive must have settled for the altitude line to say DRIVE FLOOR."),
        ECVF_Default);

    /** A dash reads as "no reading", where a zero would read as a measurement. */
    const FText Blank = NSLOCTEXT("DeepSpace", "HUDBlank", "-----");

    /**
     * A speed in the unit a person would say it in: metres a second at
     * cruise, kilometres a second as the drive opens, and fractions of light
     * once it is past a hundredth of it -- "34 C" at 1 AU says what the drive
     * is doing, where eleven digits of metres say nothing.
     */
    FString SpeedWords(double CmPerSecond)
    {
        const double MetresPerSecond = CmPerSecond * 0.01;
        const double Light = UniverseUnits::CmPerLightYear / (365.25 * 86400.0) * 0.01;
        if (MetresPerSecond < 1.0e4)
        {
            return FString::Printf(TEXT("%.0f M/S"), MetresPerSecond);
        }
        if (MetresPerSecond < 0.01 * Light)
        {
            return FString::Printf(TEXT("%.0f KM/S"), MetresPerSecond * 0.001);
        }
        const double Fraction = MetresPerSecond / Light;
        return Fraction < 1.0 ? FString::Printf(TEXT("%.2f C"), Fraction) : FString::Printf(TEXT("%.0f C"), Fraction);
    }
}

const FName UShipHUDWidget::NoseCaretName(TEXT("NoseCaret"));

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
    DriveLine = MakeReadout(Blank, UShipScreenWidget::Dim, 10.0f);
    MotionLine = MakeReadout(Blank, UShipScreenWidget::Ink, 12.0f);
    AltitudeReadout = MakeReadout(Blank, UShipScreenWidget::Dim, 10.0f);

    const TArray<UWidget*> Corners = {ShipLine, PlaceLine, PowerLine,
                                      DriveLine, MotionLine, AltitudeReadout};
    for (UWidget* Widget : Corners)
    {
        Canvas->AddChild(Widget);
    }

    PlaceCorner(ShipLine,   FVector2D(0.0f, 0.0f), FVector2D(Margin, Margin));
    PlaceCorner(PlaceLine,  FVector2D(0.0f, 0.0f), FVector2D(Margin, Margin + 20.0f));
    PlaceCorner(PowerLine,  FVector2D(1.0f, 0.0f), FVector2D(-Margin, Margin));
    PlaceCorner(DriveLine,  FVector2D(1.0f, 0.0f), FVector2D(-Margin, Margin + 20.0f));
    PlaceCorner(MotionLine, FVector2D(0.0f, 1.0f), FVector2D(Margin, -Margin));
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
        const FText What = Character->GetCurrentPrompt();
        Prompt->SetText(What.IsEmpty()
            ? FText::GetEmpty()
            : FText::Format(NSLOCTEXT("DeepSpace", "HUDPrompt", "(E)  {0}"), What));
    }

    PlaceNoseCaret(ShipState);

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
        // The drive is a lever the pilot can leave on and walk away from, so
        // whether it is on is said here, beside the speed it makes.
        const double Speed = ShipState->GetFlightState().GetSpeed();
        FString Motion = Speed > 1.0 ? SpeedWords(Speed) : FString(TEXT("STATIONARY"));
        if (ShipState->IsDriveEngaged())
        {
            Motion += NavText::Separator;
            Motion += TEXT("DRIVE");
        }
        MotionLine->SetText(FText::FromString(Motion));
    }

    // Where the ship is: the system asked of its position, never remembered,
    // and asked once this frame for both lines that need it. The altitude
    // wants only its surfaces, so nothing out to the neighbours is searched.
    const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(this);
    const TOptional<FStarSystem> Here = (Universe && !ShipState->IsInTransit())
        ? Universe->GetSystemAt(ShipState->GetFlightState().GetUniversePosition())
        : TOptional<FStarSystem>();

    if (AltitudeReadout)
    {
        AltitudeReadout->SetText(AltitudeLineText(*ShipState, LocalSystem::Here(Here)));
    }

    if (PlaceLine)
    {
        PlaceLine->SetText(ShipState->IsInTransit() ? FText::FromString(NavText::Jump(EJumpState::Transit))
                           : Here ? FText::FromString(NavText::Place(Here->Stub.Name, Here->Star.Class))
                                  : Blank);
    }

    // The jump, in words and never a number, with the bearing to the course
    // relative to the ship rather than to a free-looking head: everything
    // aiming needs, from the pilot's seat alone (nav decision 3).
    if (DriveLine)
    {
        DriveLine->SetText(DriveLineText(*ShipState, Universe));
    }
}

void UShipHUDWidget::PlaceNoseCaret(const UShipSubsystem* ShipState)
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
    bool bShow = ShipState && Camera && ShowsNoseCaret(*ShipState, GetOwningPlayerPawn())
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

bool UShipHUDWidget::ShowsNoseCaret(const UShipSubsystem& ShipState, const APawn* Viewer)
{
    return Viewer && ShipState.GetPilot() == Viewer && ShipState.GetPlottedSystem().IsSet() && !ShipState.IsInTransit();
}

FVector UShipHUDWidget::NoseCaretWorldPoint(const FVector& CameraLocation)
{
    return CameraLocation + FVector::ForwardVector * CaretDistance;
}

FText UShipHUDWidget::DriveLineText(const UShipSubsystem& ShipState, const UUniverseSubsystem* Universe)
{
    const TOptional<FSystemId> Course = ShipState.GetPlottedSystem();
    const TOptional<FStarSystem> Star = (Universe && Course) ? Universe->GetSystem(*Course) : TOptional<FStarSystem>();
    const TOptional<FVector> Bearing = ShipState.GetCourseDirectionShipLocal();
    return Star && Bearing
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

FString UShipHUDWidget::AltitudeLine(double AltitudeCm, const FString& Surface, bool bEdge, bool bAtDriveFloor)
{
    FString Line = AltitudeWords(AltitudeCm) + (bEdge ? FString(TEXT(" TO THE EDGE")) : TEXT(" ABOVE ") + Surface);
    if (bAtDriveFloor)
    {
        Line += NavText::Separator;
        Line += TEXT("DRIVE FLOOR");
    }
    return Line;
}

bool UShipHUDWidget::DriveHoldsAtFloor(const FShipFlightState& Flight, double SurfaceCm, const FVector& AwayFromSurface,
                                       double FloorBand)
{
    const FShipFlightCommand& Command = Flight.GetCommand();
    const double Floor = Flight.GetLimits().DriveFloor;
    if (!Command.bDrive || Command.Throttle == 0.0 || Floor <= 0.0)
    {
        return false;
    }
    // Where the lever sends the ship, not where it is going this instant: at
    // the floor the drive has cut the closing speed to nothing, so the
    // velocity says nothing about which way it is being held. A heading that
    // grazes the surface still closes, and the drive still holds it.
    const FVector Pushed = Flight.GetUniverseOrientation().GetForwardVector() * FMath::Sign(Command.Throttle);
    const bool bTowardSurface = (Pushed | AwayFromSurface) < 0.0;

    // Room, not height either side of the floor: under the floor the drive
    // has none, closes no further, and is as much at its floor as above it.
    const double Room = FMath::Max(SurfaceCm - Floor, 0.0);
    return bTowardSurface && Room <= FMath::Max(FloorBand, 0.0) * Floor;
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
    // Which way the surface falls away is asked only where the answer can
    // matter -- inside the band, with the drive on -- so a HUD far from
    // anything pays for no probes.
    const double Band = CVarFloorBand.GetValueOnGameThread();
    const double Floor = Flight.GetLimits().DriveFloor;
    bool bAtFloor = false;
    if (Flight.GetCommand().bDrive && Nearest.Distance - Floor <= FMath::Max(Band, 0.0) * Floor)
    {
        const FVector Away = ShipDrive::AwayFromSurface(
            [&Here](const FUniversePosition& At) { return LocalSystem::NearestSurfaceDistance(Here, At); }, Where);
        bAtFloor = DriveHoldsAtFloor(Flight, Nearest.Distance, Away, Band);
    }
    const FString Surface = Nearest.bEdge ? FString() : Here.Bodies[Nearest.Body].Id.ToString();
    return FText::FromString(AltitudeLine(Nearest.Distance, Surface, Nearest.bEdge, bAtFloor));
}
