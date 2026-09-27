#include "UI/ShipTargetOverlay.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/DrawElements.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipSubsystem.h"
#include "UI/ShipScreenWidget.h"

namespace
{
    TAutoConsoleVariable<float> CVarTargetMinPixels(
        TEXT("ds.HUD.TargetMinPixels"), TargetMarker::DefaultMinPixels,
        TEXT("The smallest target bracket, slate units: what a sub-pixel world, or four black pixels on the night ")
        TEXT("side, is marked with so it can be found across the glass."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarTargetEdgeInset(
        TEXT("ds.HUD.TargetEdgeInset"), TargetMarker::DefaultEdgeInset,
        TEXT("How far inside the view's edge the pilot's chevron sits when the target is off the view, slate units."),
        ECVF_Default);

    /** As the nose caret's: any distance projects to the same place from a
     *  point taken relative to the camera, and 100 km keeps it far inside
     *  float range after the view transform. */
    constexpr double MarkDistance = 1.0e7;

    /** A disc's angular radius is taken no nearer a right angle than this
     *  for its projected size: tan runs away there, and a world that big is
     *  past the bracket's hide long before. */
    const double MaxSizedRadius = FMath::DegreesToRadians(80.0);

    /** The bracket's corner ticks: a quarter of its side, held between a
     *  length that still reads as a corner on the smallest bracket and one
     *  that does not become a frame round a large disc. */
    constexpr float CornerMin = 5.0f;
    constexpr float CornerMax = 14.0f;
    constexpr float CornerShare = 0.25f;
    constexpr float MarkLine = 1.5f;

    /** The chevron: a V this long from tip to arms and this wide, a little
     *  heavier than the bracket, since it sits alone at the edge. */
    constexpr float ChevronLength = 10.0f;
    constexpr float ChevronHalfWidth = 7.0f;
    constexpr float ChevronLine = 2.0f;

    /** The prograde mark: a ring smaller than the caret's 16, so the two
     *  nest rather than overlap when the ship goes where it points, with
     *  three ticks out from it -- top, left and right, the flight-sim
     *  convention and a shape unlike the caret's plain ring. */
    constexpr float ProgradeRadius = 6.0f;
    constexpr float ProgradeTick = 5.0f;
    constexpr int32 RingSegments = 24;

    /** The camera as the player has it, or nothing: no controller, no
     *  camera manager, or no viewport to size the view by. */
    TOptional<FTargetOverlayCamera> PlayerCamera(APlayerController* Controller, const UObject* WorldContext)
    {
        const APlayerCameraManager* Manager = Controller ? Controller->PlayerCameraManager.Get() : nullptr;
        if (!Manager)
        {
            return {};
        }
        const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(WorldContext), UE_KINDA_SMALL_NUMBER);
        FTargetOverlayCamera Camera;
        Camera.Location = Manager->GetCameraLocation();
        Camera.Rotation = Manager->GetCameraRotation().Quaternion();
        Camera.ViewSize = UWidgetLayoutLibrary::GetViewportSize(WorldContext) / Scale;
        if (!(Camera.ViewSize.X > 0.0) || !(Camera.ViewSize.Y > 0.0))
        {
            return {};
        }
        Camera.Project = [Controller](const FVector& World, FVector2D& Out)
        {
            return UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Controller, World, Out, false);
        };
        return Camera;
    }

    /** A projection that is in front of the camera and succeeded. The
     *  engine's own answer behind the camera is not trusted alone: the view
     *  space direction says it without a projection. */
    bool ProjectAhead(const FTargetOverlayCamera& Camera, const FVector& World, FVector2D& Out)
    {
        const FVector InView = Camera.Rotation.UnrotateVector(World - Camera.Location);
        return InView.X > 0.0 && Camera.Project && Camera.Project(World, Out);
    }
}

UShipTargetOverlay::UShipTargetOverlay(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SetIsFocusable(false);
}

bool UShipTargetOverlay::ShowsTargetMark(const UShipSubsystem& Ship, const APawn* Viewer,
                                         const TOptional<FStarSystem>& Here)
{
    const TOptional<FBodyId> Target = Ship.GetTarget();
    return Viewer && Here && Target && !Ship.IsInTransit() && ShipNav::TargetPlanet(*Here, *Target) != nullptr;
}

bool UShipTargetOverlay::ShowsPrograde(const UShipSubsystem& Ship, const APawn* Viewer,
                                       const TOptional<FStarSystem>& Here)
{
    return ShowsTargetMark(Ship, Viewer, Here) && Ship.GetPilot() == Viewer
        && Ship.GetFlightState().GetVelocity().Size() >= TargetMarker::MinSpeed;
}

FVector UShipTargetOverlay::TargetWorldPoint(const FVector& CameraLocation, const FVector& ShipLocalDir)
{
    return CameraLocation + ShipLocalDir.GetSafeNormal() * MarkDistance;
}

TOptional<FVector> UShipTargetOverlay::ProgradeWorldPoint(const FVector& CameraLocation, const FVector& Velocity,
                                                          const FQuat& Orientation)
{
    const FVector Prograde = TargetMarker::ProgradeShipLocal(Velocity, Orientation);
    return Prograde.IsZero() ? TOptional<FVector>() : TOptional<FVector>(CameraLocation + Prograde * MarkDistance);
}

void UShipTargetOverlay::PlaceFor(const UShipSubsystem& Ship, const TOptional<FStarSystem>& Here,
                                  APlayerController* Controller)
{
    const TOptional<FTargetOverlayCamera> Camera = PlayerCamera(Controller, this);
    PlaceFrom(Ship, Here, Camera.GetPtrOrNull(), Controller ? Controller->GetPawn() : nullptr);
}

void UShipTargetOverlay::Hide()
{
    LastMark = FTargetMark();
    LastPrograde.Reset();
    SetVisibility(ESlateVisibility::Collapsed);
}

void UShipTargetOverlay::PlaceFrom(const UShipSubsystem& Ship, const TOptional<FStarSystem>& Here,
                                   const FTargetOverlayCamera* Camera, const APawn* Viewer)
{
    if (!Camera || !ShowsTargetMark(Ship, Viewer, Here))
    {
        Hide();
        return;
    }
    const TOptional<FTargetView> View = Ship.GetTargetView(*Here);
    if (!View)
    {
        Hide();
        return;
    }
    const bool bPilot = Ship.GetPilot() == Viewer;

    // The target: its direction from the camera, and a second point off it
    // by the disc's angular radius at the same depth, along the camera's
    // right (or up, looking along the right), so perspective off the view's
    // centre is in the disc's size.
    const FVector Dir = View->ShipLocalDir.GetSafeNormal();
    const FVector Centre = TargetWorldPoint(Camera->Location, Dir);
    FVector2D At = FVector2D::ZeroVector;
    const bool bProjected = ProjectAhead(*Camera, Centre, At);
    float RadiusPx = 0.0f;
    if (bProjected)
    {
        FVector Side = Camera->Rotation.GetRightVector();
        Side = (Side - Dir * FVector::DotProduct(Side, Dir)).GetSafeNormal();
        if (Side.IsZero())
        {
            const FVector Up = Camera->Rotation.GetUpVector();
            Side = (Up - Dir * FVector::DotProduct(Up, Dir)).GetSafeNormal();
        }
        FVector2D Edge;
        const double Offset = MarkDistance * FMath::Tan(FMath::Min(View->AngularRadius, MaxSizedRadius));
        if (!Side.IsZero() && ProjectAhead(*Camera, Centre + Side * Offset, Edge))
        {
            RadiusPx = static_cast<float>(FVector2D::Distance(At, Edge));
        }
    }

    // The glass, from the eye along the target: the bracket is shown to
    // anyone who can see the world, and never drawn on a wall.
    const bool bSeen = TargetMarker::SeenThroughGlass(GetWorld(), Camera->Location, Dir, Viewer);
    LastMark = TargetMarker::Place(bProjected, At, RadiusPx, Camera->Rotation.UnrotateVector(Dir), Camera->ViewSize,
                                   FMath::Max(0.0f, CVarTargetMinPixels.GetValueOnGameThread()),
                                   FMath::Max(0.0f, CVarTargetEdgeInset.GetValueOnGameThread()), bPilot, bSeen);

    // The prograde mark: the pilot's aiming aid, like the caret, so not
    // occluded -- the helm looks out of the glass by construction -- and,
    // like the caret, hidden off the view rather than pinned to its edge.
    LastPrograde.Reset();
    if (ShowsPrograde(Ship, Viewer, Here))
    {
        const FShipFlightState& Flight = Ship.GetFlightState();
        const TOptional<FVector> Along = ProgradeWorldPoint(Camera->Location, Flight.GetVelocity(),
                                                            Flight.GetUniverseOrientation());
        FVector2D Prograde;
        if (Along && ProjectAhead(*Camera, *Along, Prograde) && Prograde.X >= 0.0 && Prograde.Y >= 0.0
            && Prograde.X <= Camera->ViewSize.X && Prograde.Y <= Camera->ViewSize.Y)
        {
            LastPrograde = Prograde;
        }
    }

    const bool bAnything = LastMark.Shape != ETargetMarkShape::None || LastPrograde.IsSet();
    SetVisibility(bAnything ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

int32 UShipTargetOverlay::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                                      const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                                      int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId,
                                           InWidgetStyle, bParentEnabled) + 1;
    const auto Line = [&](const TArray<FVector2D>& Points, const FLinearColor& Colour, float Thickness)
    {
        FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Points,
                                     ESlateDrawEffect::None, Colour, true, Thickness);
    };

    if (LastMark.Shape == ETargetMarkShape::Bracket)
    {
        // Four corners, each an L opening inward: a square the eye closes
        // round the world without a frame covering it.
        const float Half = 0.5f * LastMark.Size;
        const float Tick = FMath::Clamp(LastMark.Size * CornerShare, CornerMin, CornerMax);
        for (const FVector2D Corner : { FVector2D(-1.0, -1.0), FVector2D(1.0, -1.0), FVector2D(1.0, 1.0), FVector2D(-1.0, 1.0) })
        {
            const FVector2D At = LastMark.Centre + Corner * Half;
            Line({ At - FVector2D(Corner.X * Tick, 0.0), At, At - FVector2D(0.0, Corner.Y * Tick) },
                 UShipScreenWidget::Accent, MarkLine);
        }
    }
    else if (LastMark.Shape == ETargetMarkShape::Chevron)
    {
        // A V whose tip points the way to look.
        const FVector2D Way = LastMark.Pointing;
        const FVector2D Across(-Way.Y, Way.X);
        const FVector2D Tip = LastMark.Centre + Way * (0.5f * ChevronLength);
        const FVector2D Back = LastMark.Centre - Way * (0.5f * ChevronLength);
        Line({ Back + Across * ChevronHalfWidth, Tip, Back - Across * ChevronHalfWidth }, UShipScreenWidget::Accent,
             ChevronLine);
    }

    if (LastPrograde)
    {
        const FVector2D Centre = *LastPrograde;
        TArray<FVector2D> Ring;
        Ring.Reserve(RingSegments + 1);
        for (int32 Index = 0; Index <= RingSegments; ++Index)
        {
            const double Angle = 2.0 * UE_DOUBLE_PI * Index / RingSegments;
            Ring.Add(Centre + ProgradeRadius * FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)));
        }
        Line(Ring, UShipScreenWidget::Ink, MarkLine);
        for (const FVector2D Out : { FVector2D(0.0, -1.0), FVector2D(-1.0, 0.0), FVector2D(1.0, 0.0) })
        {
            Line({ Centre + Out * ProgradeRadius, Centre + Out * (ProgradeRadius + ProgradeTick) },
                 UShipScreenWidget::Ink, MarkLine);
        }
    }
    return Layer;
}
