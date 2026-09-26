#include "Ship/ShipScreen.h"

#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
    // Read where it is used and never cached, like every playtest knob: a
    // nudge is a console command, not a rebuild.
    //
    // 4% at each edge: the panel fills 92% of the frame on its tighter axis.
    // Enough that the bezel is plainly inside the view with some of the room
    // round it -- you are sitting at a screen, not looking through one -- and
    // little enough that the laptop, which read right at the old fixed 52
    // degrees, comes out at about 57 rather than somewhere else entirely.
    TAutoConsoleVariable<float> CVarFrameMargin(
        TEXT("ds.Screen.FrameMargin"), 0.04f,
        TEXT("Fraction of the view left clear at each edge of a screen you are sat at, on its tighter axis."),
        ECVF_Default);

    /** The widest a framing may go, degrees. Past this the view is a
     *  fisheye, not a screen; only a degenerate panel or distance asks. */
    constexpr float MaxFramedFov = 150.0f;
}

AShipScreen::AShipScreen()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Screen = CreateDefaultSubobject<UWidgetComponent>(TEXT("Screen"));
    Screen->SetupAttachment(Root);
    ConfigurePanel(Screen, PanelWidthCm, DrawSizePixels);
}

void AShipScreen::ConfigurePanel(UWidgetComponent* Panel, float WidthCm, const FVector2D& DrawSizePixels)
{
    if (!Panel || DrawSizePixels.X <= 0.0f)
    {
        return;
    }

    Panel->SetWidgetSpace(EWidgetSpace::World);
    Panel->SetDrawAtDesiredSize(false);
    Panel->SetDrawSize(DrawSizePixels);
    Panel->SetPivot(FVector2D(0.5f, 0.5f));
    Panel->SetTwoSided(false);
    Panel->SetBlendMode(EWidgetBlendMode::Opaque);
    Panel->SetBackgroundColor(FLinearColor(0.012f, 0.030f, 0.036f, 1.0f));

    // A widget component's quad has its normal along +X (FWidget3DSceneProxy
    // builds it with TangentZ = (1,0,0)), but every wall-mounted fixture in
    // this ship faces -X at yaw 0 -- that is the convention
    // placement.resolve_mount yaws things by. Turning the panel round here
    // means a screen is mounted with exactly the same yaw as the thing it
    // sits on, rather than everyone remembering to add 180.
    Panel->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));

    // The pointer finds screens by tracing Visibility, the same channel the
    // reach trace uses. Blocking only that channel keeps a screen out of the
    // way of movement and of anything else the ship traces for.
    Panel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Panel->SetCollisionResponseToAllChannels(ECR_Ignore);
    Panel->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Panel->SetGenerateOverlapEvents(false);

    // One draw-size pixel is one unreal unit before scaling, so the panel's
    // real size is entirely a matter of the component's scale. Uniform, so
    // the widget's aspect ratio survives.
    Panel->SetRelativeScale3D(FVector(WidthCm / static_cast<float>(DrawSizePixels.X)));
}

void AShipScreen::SetPanelWidthCm(float WidthCm)
{
    PanelWidthCm = WidthCm;
    ConfigurePanel(Screen, WidthCm, DrawSizePixels);
}

void AShipScreen::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    SetPanelWidthCm(PanelWidthCm);
}

FTransform AShipScreen::GetUseTransform() const
{
    // A widget quad's normal is its own +X, so that is the side a reader is
    // on. Yaw only: the body sits upright however the panel is tilted.
    const FVector Normal = Screen ? Screen->GetComponentTransform().GetUnitAxis(EAxis::X)
                                  : GetActorForwardVector();
    const FVector Flat = FVector(Normal.X, Normal.Y, 0.0).GetSafeNormal();
    const FVector Panel = Screen ? Screen->GetComponentLocation() : GetActorLocation();

    // Seat height is measured from the floor, which is Z = 0 throughout the
    // ship (Tools/hauler_layout.py).
    const FVector Seat(Panel.X + Flat.X * UseDistanceCm,
                       Panel.Y + Flat.Y * UseDistanceCm,
                       SeatHeightCm);

    return FTransform((-Flat).Rotation(), Seat);
}

FTransform AShipScreen::GetViewTransform() const
{
    const FTransform Panel = Screen ? Screen->GetComponentTransform() : GetActorTransform();
    const FVector Normal = Panel.GetUnitAxis(EAxis::X);

    // Square on to the panel, including its tilt: a laptop lid leans back, so
    // reading it squarely means looking slightly down.
    return FTransform((-Normal).Rotation(), Panel.GetLocation() + Normal * ViewDistanceCm);
}

float AShipScreen::FitFieldOfView(const FVector2D& FramedSizeCm, float DistanceCm, float ViewportAspect,
                                  EAspectRatioAxisConstraint Constraint, float CameraAspect, float MarginFraction)
{
    const double Distance = FMath::Max(static_cast<double>(DistanceCm), 1.0);
    const double Aspect = ViewportAspect > 0.0f ? ViewportAspect : 16.0 / 9.0;
    const double CamAspect = CameraAspect > 0.0f ? CameraAspect : 16.0 / 9.0;
    const double Fill = FMath::Clamp(1.0 - 2.0 * static_cast<double>(MarginFraction), 0.1, 1.0);

    // Tangents of the half-angles the frame needs, each way, for the panel
    // to fill no more than Fill of it.
    const double NeedX = 0.5 * FramedSizeCm.X / Distance / Fill;
    const double NeedY = 0.5 * FramedSizeCm.Y / Distance / Fill;

    // The engine's rule, FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle:
    // under MaintainXFOV the number is the viewport's horizontal angle and
    // the vertical follows from its aspect; otherwise the vertical is kept,
    // derived from the number read as horizontal at the *camera's* aspect.
    const bool bMaintainX = Constraint == AspectRatio_MaintainXFOV ||
                            (Constraint == AspectRatio_MajorAxisFOV && Aspect > 1.0);
    const double HalfTan = bMaintainX
        ? FMath::Max(NeedX, NeedY * Aspect)
        : FMath::Max(NeedY, NeedX / Aspect) * CamAspect;

    const double Fov = FMath::RadiansToDegrees(2.0 * FMath::Atan(HalfTan));
    return static_cast<float>(FMath::Min(Fov, static_cast<double>(MaxFramedFov)));
}

FVector2D AShipScreen::GetFramedSizeCm() const
{
    const double Height = DrawSizePixels.X > 0.0 ? PanelWidthCm * DrawSizePixels.Y / DrawSizePixels.X : 0.0;
    return FVector2D(PanelWidthCm + 2.0 * BezelCm, Height + 2.0 * BezelCm);
}

float AShipScreen::GetUseFieldOfView(float ViewportAspect, EAspectRatioAxisConstraint Constraint, float CameraAspect) const
{
    return FitFieldOfView(GetFramedSizeCm(), ViewDistanceCm, ViewportAspect, Constraint, CameraAspect,
                          CVarFrameMargin.GetValueOnGameThread());
}
