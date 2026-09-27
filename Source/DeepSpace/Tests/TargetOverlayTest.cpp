#include "Blueprint/WidgetTree.h"
#include "Components/BoxComponent.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipSubsystem.h"
#include "Ship/ShipTags.h"
#include "Tests/SkyTestWorld.h"
#include "UI/ShipHUDWidget.h"
#include "UI/ShipTargetOverlay.h"
#include "UI/TargetMarker.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTargetOverlayTest,
    "DeepSpace.UI.TargetOverlay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TargetOverlayTestLocal
{
    /** A 1080p view, 90 degrees across: the pinhole the test projects
     *  through in place of a viewport, which a headless world has not got. */
    const FVector2D ViewSize(1920.0, 1080.0);
    const double HalfFov = FMath::DegreesToRadians(45.0);
    const double Focal = 0.5 * ViewSize.X / FMath::Tan(HalfFov);

    /** A camera at Eye looking along Look, projecting as a pinhole does:
     *  false behind it, and otherwise wherever the point lands, on the view
     *  or off it -- which is what UWidgetLayoutLibrary answers in play. */
    FTargetOverlayCamera Pinhole(const FVector& Eye, const FRotator& Look)
    {
        FTargetOverlayCamera Camera;
        Camera.Location = Eye;
        Camera.Rotation = Look.Quaternion();
        Camera.ViewSize = ViewSize;
        const FQuat Rotation = Camera.Rotation;
        Camera.Project = [Eye, Rotation](const FVector& World, FVector2D& Out)
        {
            const FVector InView = Rotation.UnrotateVector(World - Eye);
            if (InView.X <= 0.0)
            {
                return false;
            }
            Out = FVector2D(0.5 * ViewSize.X + Focal * InView.Y / InView.X, 0.5 * ViewSize.Y - Focal * InView.Z / InView.X);
            return true;
        };
        return Camera;
    }

    /** A slab of the hull: the engine cube scaled, on every channel, as the
     *  level's boxes are, and tagged as glass or not. */
    AStaticMeshActor* Slab(UWorld* World, UStaticMesh* Cube, const FVector& Centre, const FVector& Size, bool bGlass)
    {
        AStaticMeshActor* Box = World->SpawnActor<AStaticMeshActor>(Centre, FRotator::ZeroRotator);
        if (!Box)
        {
            return nullptr;
        }
        Box->SetMobility(EComponentMobility::Movable);
        UStaticMeshComponent* Mesh = Box->GetStaticMeshComponent();
        Mesh->SetStaticMesh(Cube);
        Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
        // The engine cube is corner-origin (CLAUDE.md): scale it, then put
        // its middle where asked.
        const FBox Bounds = Cube->GetBoundingBox();
        const FVector Scale = Size / Bounds.GetSize();
        Box->SetActorScale3D(Scale);
        Box->SetActorLocation(Centre - Bounds.GetCenter() * Scale);
        if (bGlass)
        {
            Box->Tags.Add(ShipTags::Glass);
        }
        return Box;
    }

    double AngleBetween(const FVector& A, const FVector& B)
    {
        return FMath::Atan2(FVector::CrossProduct(A, B).Size(), FVector::DotProduct(A, B));
    }
}

/**
 * The target's marks over the glass (system map decision 7). The built HUD
 * carries the overlay, hidden; who is shown what is asked of the ship; the
 * points it projects are the target's direction and the velocity's from the
 * camera, in ship axes. A headless world has no viewport, so what the
 * projection decides is checked through a pinhole camera handed in, against
 * a spawned cockpit -- glass ahead of the helm's eye, a wall aft -- because
 * the level cannot be loaded (HaulerDressingMarkers.h): the bracket is drawn
 * for anyone who can see the world through the glass, never on a wall, and
 * the viewer's own body never hides it; the chevron and the prograde mark
 * are the pilot's.
 */
bool FTargetOverlayTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace TargetOverlayTestLocal;

    FSkyWorld Test(TEXT("TargetOverlayTestWorld"));
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("and a universe"), Test.Universe))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    APawn* Walker = Test.World->SpawnActor<APawn>();
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("the pawns spawn"), Pilot) || !TestNotNull(TEXT("both"), Walker)
        || !TestNotNull(TEXT("the engine cube loads"), Cube))
    {
        return false;
    }

    // -- The HUD carries it: on the canvas, filling it, built hidden. -------
    UShipHUDWidget* HUD = NewObject<UShipHUDWidget>(Test.World);
    HUD->Initialize();
    HUD->TakeWidget();
    UShipTargetOverlay* Overlay = HUD->WidgetTree
        ? Cast<UShipTargetOverlay>(HUD->WidgetTree->FindWidget(UShipHUDWidget::TargetOverlayName)) : nullptr;
    if (!TestNotNull(TEXT("the HUD is built with a target overlay"), Overlay))
    {
        return false;
    }
    const UCanvasPanelSlot* OverlaySlot = Cast<UCanvasPanelSlot>(Overlay->Slot);
    TestTrue(TEXT("on the HUD's canvas"), OverlaySlot && Overlay->GetParent() == HUD->WidgetTree->RootWidget);
    TestTrue(TEXT("filling it, so its space is the caret's"),
             OverlaySlot && OverlaySlot->GetAnchors().Minimum == FVector2D(0.0, 0.0)
                 && OverlaySlot->GetAnchors().Maximum == FVector2D(1.0, 1.0));
    TestEqual(TEXT("built hidden"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);
    const UTextBlock* Readout = Cast<UTextBlock>(HUD->WidgetTree->FindWidget(UShipHUDWidget::TargetLineName));
    TestTrue(TEXT("and with a target readout, empty until there is a target"), Readout && Readout->GetText().IsEmpty());

    // -- Who is shown what, asked of the ship. -------------------------------
    const auto HereNow = [&]() { return Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition()); };
    const TOptional<FStarSystem> Home = HereNow();
    if (!TestTrue(TEXT("the ship starts in a system with worlds"), Home.IsSet() && Home->Planets.Num() > 0))
    {
        return false;
    }
    Ship->SetPilot(Pilot);
    TestFalse(TEXT("no target: no mark for the pilot"), UShipTargetOverlay::ShowsTargetMark(*Ship, Pilot, Home));
    TestFalse(TEXT("nor anyone"), UShipTargetOverlay::ShowsTargetMark(*Ship, Walker, Home));
    TestFalse(TEXT("and no prograde mark"), UShipTargetOverlay::ShowsPrograde(*Ship, Pilot, Home));

    // The world farthest from the ship, so it is a point: the bracket is
    // then its least size, whatever procgen made.
    const FUniversePosition Start = Ship->GetFlightState().GetUniversePosition();
    int32 Far = 0;
    for (int32 Orbit = 1; Orbit < Home->Planets.Num(); ++Orbit)
    {
        if (Start.DistanceTo(Home->PlanetPosition(Orbit)) > Start.DistanceTo(Home->PlanetPosition(Far)))
        {
            Far = Orbit;
        }
    }
    if (!TestTrue(TEXT("it can be targeted"), Ship->SetTarget(FBodyId{ Home->Stub.Id, Far, -1 })))
    {
        return false;
    }
    TestTrue(TEXT("a target: the pilot is shown its mark"), UShipTargetOverlay::ShowsTargetMark(*Ship, Pilot, Home));
    TestTrue(TEXT("and so is anyone aboard: the glass decides who sees a bracket"),
             UShipTargetOverlay::ShowsTargetMark(*Ship, Walker, Home));
    TestFalse(TEXT("but not from no pawn"), UShipTargetOverlay::ShowsTargetMark(*Ship, nullptr, Home));
    TestFalse(TEXT("nor with no system in hand: a target is resolved, never assumed"),
              UShipTargetOverlay::ShowsTargetMark(*Ship, Pilot, {}));
    TestTrue(TEXT("at rest there is no prograde mark"),
             Ship->GetShipSpeed() < TargetMarker::MinSpeed && !UShipTargetOverlay::ShowsPrograde(*Ship, Pilot, Home));

    // A target held from another system names nothing here.
    const FQuat Opening = Ship->GetFlightState().GetUniverseOrientation();
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (TestTrue(TEXT("there is another system to be placed in"), Chart.Num() > 0))
    {
        Ship->PlaceShip(Chart[0].Position, Opening);
        TestFalse(TEXT("placed in another system, the old target is marked for nobody"),
                  UShipTargetOverlay::ShowsTargetMark(*Ship, Pilot, HereNow()));
        Ship->PlaceShip(Start, Opening);
    }

    // -- The points it projects: directions from the camera, ship axes. -----
    const FVector ToWorld = (Home->PlanetPosition(Far) - Start).GetSafeNormal();
    const FVector Eye(-600.0, 260.0, 150.0);
    for (const FQuat& Attitude : { Opening, FQuat(FVector(0.3, -0.5, 0.8).GetSafeNormal(), 1.9) })
    {
        Ship->PlaceShip(Start, Attitude);
        const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
        if (!TestTrue(TEXT("the target is in view of the ship"), View.IsSet()))
        {
            return false;
        }
        const FVector Point = UShipTargetOverlay::TargetWorldPoint(Eye, View->ShipLocalDir);
        const double Off = AngleBetween(Point - Eye, Attitude.UnrotateVector(ToWorld));
        TestTrue(FString::Printf(TEXT("the target's point lies along its direction in ship axes (%.2g rad)"), Off),
                 Off < 1.0e-6);
    }
    TestFalse(TEXT("under 1 m/s the prograde has no point"),
              UShipTargetOverlay::ProgradeWorldPoint(Eye, FVector(50.0, 0.0, 0.0), Opening).IsSet());
    {
        const FVector Velocity(3.0e4, -1.0e4, 5.0e3);
        const FQuat Attitude(FVector(0.2, 0.9, -0.1).GetSafeNormal(), 0.8);
        const TOptional<FVector> Point = UShipTargetOverlay::ProgradeWorldPoint(Eye, Velocity, Attitude);
        TestTrue(TEXT("moving, the prograde's point lies along the velocity in ship axes"),
                 Point && AngleBetween(*Point - Eye, Attitude.UnrotateVector(Velocity)) < 1.0e-6);
    }

    // -- With no camera it stays hidden, whatever the ship says. ------------
    Ship->PlaceShip(Start, FRotationMatrix::MakeFromX(ToWorld).ToQuat());
    Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
    Overlay->PlaceFor(*Ship, Home, nullptr);
    TestEqual(TEXT("no player to own it: hidden"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);
    TestTrue(TEXT("and nothing decided"), Overlay->GetLastMark().Shape == ETargetMarkShape::None);
    Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
    Overlay->PlaceFrom(*Ship, Home, nullptr, Pilot);
    TestEqual(TEXT("no camera handed in: hidden"), Overlay->GetVisibility(), ESlateVisibility::Collapsed);

    // -- The HUD drives it: its refresh is NativeTick's whole body. ---------
    // A headless widget is never painted, so never ticked; Refresh is what
    // play runs every frame, here with no player to see through.
    {
        FScopedCVar Shown(TEXT("ds.HUD"), 1);
        Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
        HUD->Refresh(1.0f / 30.0f, nullptr);
        const FString Line = Readout ? Readout->GetText().ToString() : FString();
        TestFalse(TEXT("a target in hand: the HUD's refresh writes the target readout"), Line.IsEmpty());
        TestEqual(TEXT("and it is the target line, the one the map prints"), Line,
                  UShipHUDWidget::TargetLineText(*Ship, Home).ToString());
        TestEqual(TEXT("the refresh reaches the overlay, which with no player to see through hides"),
                  Overlay->GetVisibility(), ESlateVisibility::Collapsed);
        Ship->ClearTarget();
        HUD->Refresh(1.0f / 30.0f, nullptr);
        TestTrue(TEXT("let go, the next refresh empties the readout"), Readout && Readout->GetText().IsEmpty());
        TestTrue(TEXT("and the target comes back"), Ship->SetTarget(FBodyId{ Home->Stub.Id, Far, -1 }));
    }

    // -- The view is sized in slate units: a 4K display at a DPI scale of 2
    //    is a 1080p view, the space the projection answers in.
    TestEqual(TEXT("4K at a scale of 2 is a 1920 x 1080 view"),
              UShipTargetOverlay::SlateViewSize(FVector2D(3840.0, 2160.0), 2.0f), FVector2D(1920.0, 1080.0));
    TestEqual(TEXT("at a scale of 1 the pixels are the view"),
              UShipTargetOverlay::SlateViewSize(FVector2D(2560.0, 1440.0), 1.0f), FVector2D(2560.0, 1440.0));
    {
        const FVector2D Unscaled = UShipTargetOverlay::SlateViewSize(FVector2D(1920.0, 1080.0), 0.0f);
        TestTrue(TEXT("a scale of nothing is never a division by it"),
                 FMath::IsFinite(Unscaled.X) && FMath::IsFinite(Unscaled.Y) && Unscaled.X > 0.0);
    }

    // -- Through a pinhole, in a cockpit. ------------------------------------
    // The ship faces the world, so it lies along +X in the hull. Glass ahead
    // of the helm, 3 m out and 6 m wide; a wall 3 m aft of the helm, between
    // it and the galley; and the pilot's own body, a box just ahead of their
    // eye, which the trace must never take for a wall.
    Slab(Test.World, Cube, PilotEye + FVector(300.0, 72.0, 25.0), FVector(20.0, 600.0, 300.0), true);
    Slab(Test.World, Cube, PilotEye + FVector(-300.0, 72.0, 25.0), FVector(20.0, 600.0, 300.0), false);
    UBoxComponent* Body = NewObject<UBoxComponent>(Pilot);
    Body->SetBoxExtent(FVector(10.0));
    Body->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    Pilot->SetRootComponent(Body);
    Body->RegisterComponent();
    Pilot->SetActorLocation(PilotEye + FVector(30.0, 0.0, 0.0));

    const FVector AtGlass(1650.0, 150.0, 170.0);
    const FVector Galley(-600.0, 0.0, 150.0);
    const FRotator Ahead(0.0, 0.0, 0.0);
    const auto Decide = [&](const FVector& From, const FRotator& Look, const APawn* Viewer)
    {
        const FTargetOverlayCamera Camera = Pinhole(From, Look);
        Overlay->PlaceFrom(*Ship, Home, &Camera, Viewer);
        return Overlay->GetLastMark();
    };
    const FVector2D Middle = 0.5 * ViewSize;

    FTargetMark Mark = Decide(PilotEye, Ahead, Pilot);
    TestTrue(TEXT("the pilot looking out of the glass at it: a bracket"), Mark.Shape == ETargetMarkShape::Bracket);
    TestTrue(FString::Printf(TEXT("on the world, at the view's centre (%.2f, %.2f)"), Mark.Centre.X, Mark.Centre.Y),
             FVector2D::Distance(Mark.Centre, Middle) < 0.5);
    TestEqual(TEXT("a point far off gets the least bracket"), Mark.Size, TargetMarker::DefaultMinPixels);
    TestEqual(TEXT("and the overlay shows"), Overlay->GetVisibility(), ESlateVisibility::HitTestInvisible);

    // The helm's own seat, where the level has it. Its reach box, which lets
    // a standing player's E find the chair, blocks Visibility and envelops
    // the seated pilot's eye; the glass trace must not take it for a wall,
    // or the one person steering never sees the bracket. It stays for the
    // rest of the cockpit's cases, as it does aboard.
    APilotSeat* Helm = Test.World->SpawnActor<APilotSeat>(HelmSeat, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the helm seat spawns"), Helm))
    {
        return false;
    }
    {
        FHitResult Inside;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(TargetOverlayTestSeat), false, Pilot);
        const bool bHit = Test.World->LineTraceSingleByChannel(Inside, PilotEye, PilotEye + FVector(3000.0, 0.0, 0.0),
                                                               ECC_Visibility, Params);
        TestTrue(TEXT("the seated eye is inside the helm seat's reach box: a trace from it starts in the seat"),
                 bHit && Inside.bStartPenetrating && Inside.GetActor() == Helm);
    }
    Mark = Decide(PilotEye, Ahead, Pilot);
    TestTrue(TEXT("sat in the helm seat, the pilot still sees the target bracketed"),
             Mark.Shape == ETargetMarkShape::Bracket);
    TestTrue(TEXT("seen through the glass from the seated eye, the seat stepped past"),
             TargetMarker::SeenThroughGlass(Test.World, PilotEye, FVector::ForwardVector, Pilot));
    TestFalse(TEXT("and past the seat a wall still hides it: only what the eye is inside is stepped past"),
              TargetMarker::SeenThroughGlass(Test.World, PilotEye, -FVector::ForwardVector, Pilot));

    TestTrue(TEXT("someone else from the helm's eye is behind the pilot's body: nothing"),
             Decide(PilotEye, Ahead, Walker).Shape == ETargetMarkShape::None);
    TestTrue(TEXT("someone standing at the fore glass sees it bracketed"),
             Decide(AtGlass, Ahead, Walker).Shape == ETargetMarkShape::Bracket);
    TestTrue(TEXT("someone in the galley, behind the wall, does not: never a bracket on a wall"),
             Decide(Galley, Ahead, Walker).Shape == ETargetMarkShape::None);
    TestTrue(TEXT("nor does the pilot, from there: it is on the view, and behind a wall"),
             Decide(Galley, Ahead, Pilot).Shape == ETargetMarkShape::None);

    // Off the view: behind and to the left of a head turned 100 degrees to
    // starboard. The pilot gets a chevron at the left edge pointing left.
    const FRotator Turned(0.0, 100.0, 0.0);
    Mark = Decide(PilotEye, Turned, Pilot);
    TestTrue(TEXT("off the view, the pilot gets a chevron"), Mark.Shape == ETargetMarkShape::Chevron);
    TestTrue(FString::Printf(TEXT("pointing left (%.2f, %.2f)"), Mark.Pointing.X, Mark.Pointing.Y),
             Mark.Pointing.X < -0.99);
    TestTrue(FString::Printf(TEXT("at the inset left edge (%.1f)"), Mark.Centre.X),
             FMath::IsNearlyEqual(Mark.Centre.X, static_cast<double>(TargetMarker::DefaultEdgeInset), 0.5));
    TestTrue(TEXT("a walker gets nothing: a chevron following them round the galley would be noise"),
             Decide(AtGlass, Turned, Walker).Shape == ETargetMarkShape::None);

    // In front of the head but outside the view: 60 degrees to the left of
    // a 90-degree view. The projection works there, and it is what says
    // which way to turn.
    const FRotator Aside(0.0, 60.0, 0.0);
    Mark = Decide(PilotEye, Aside, Pilot);
    TestTrue(FString::Printf(TEXT("in front but off the view, a chevron pointing left (%.2f, %.2f)"), Mark.Pointing.X, Mark.Pointing.Y),
             Mark.Shape == ETargetMarkShape::Chevron && Mark.Pointing.X < -0.99);

    // A disc: the bracket is its projected diameter plus padding a side.
    {
        const double Radius = Home->Planets[Far].RadiusEarth * UniverseUnits::CmPerEarthRadius;
        const auto At = [&](double AngularRadiusDeg)
        {
            const double Distance = Radius / FMath::Sin(FMath::DegreesToRadians(AngularRadiusDeg));
            Ship->PlaceShip(Home->PlanetPosition(Far) + (-ToWorld * Distance), FRotationMatrix::MakeFromX(ToWorld).ToQuat());
        };
        At(2.0);
        const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
        Mark = Decide(PilotEye, Ahead, Pilot);
        const double Expected = View ? 2.0 * (Focal * FMath::Tan(View->AngularRadius) + TargetMarker::BracketPadding) : 0.0;
        TestTrue(FString::Printf(TEXT("two degrees across its radius, a bracket its diameter and padding (%.1f, %.1f)"), Mark.Size, Expected),
                 Mark.Shape == ETargetMarkShape::Bracket && FMath::IsNearlyEqual(Mark.Size, Expected, 1.0));
        TestTrue(TEXT("which is well over the least"), Mark.Size > 2.0f * TargetMarker::DefaultMinPixels);

        At(30.0);
        TestTrue(TEXT("filling the glass, no bracket: the world is its own marker"),
                 Decide(PilotEye, Ahead, Pilot).Shape == ETargetMarkShape::None);
        Ship->PlaceShip(Start, FRotationMatrix::MakeFromX(ToWorld).ToQuat());
    }

    // The prograde mark: the pilot's, along the velocity, hidden off the view.
    {
        Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
        for (int32 Tick = 0; Tick < 30; ++Tick)
        {
            Ship->Tick(1.0f / 30.0f);
        }
        TestTrue(FString::Printf(TEXT("the ship gets under way (%.1f m/s)"), Ship->GetShipSpeed() * 0.01),
                 Ship->GetShipSpeed() >= TargetMarker::MinSpeed);
        TestTrue(TEXT("moving with a target, the pilot is shown the prograde mark"),
                 UShipTargetOverlay::ShowsPrograde(*Ship, Pilot, Home));
        TestFalse(TEXT("nobody else is"), UShipTargetOverlay::ShowsPrograde(*Ship, Walker, Home));

        Decide(PilotEye, Ahead, Pilot);
        const TOptional<FVector2D> Prograde = Overlay->GetLastPrograde();
        TestTrue(TEXT("going where it points, the mark is at the view's centre"),
                 Prograde && FVector2D::Distance(*Prograde, Middle) < 1.0);
        Decide(AtGlass, Ahead, Walker);
        TestFalse(TEXT("a walker's HUD has none"), Overlay->GetLastPrograde().IsSet());
        Decide(PilotEye, Aside, Pilot);
        TestFalse(TEXT("off the view it is hidden, not pinned to the edge"), Overlay->GetLastPrograde().IsSet());
        Decide(PilotEye, Turned, Pilot);
        TestFalse(TEXT("and behind the head likewise"), Overlay->GetLastPrograde().IsSet());
        Ship->SetFlightCommand(Pilot, 0.0f, FVector::ZeroVector);
    }

    // -- In the fold nothing is marked: the sky is hidden. -------------------
    // An in-system jump keeps its target through the fold, so this is the
    // fold hiding the marks and not the target going.
    {
        FScopedCVar Instant(TEXT("ds.Nav.ChargeSeconds"), 0.0f);
        bool bPlotted = false;
        for (int32 Orbit = Home->Planets.Num() - 1; Orbit >= 0 && !bPlotted; --Orbit)
        {
            Ship->PlaceShip(Start, Opening);
            bPlotted = Ship->SetTarget(FBodyId{ Home->Stub.Id, Orbit, -1 }) && Ship->PlotTarget();
        }
        const TOptional<FVector> Course = Ship->GetCourseDirection();
        if (TestTrue(TEXT("some world here is far enough to jump to"), bPlotted && Course.IsSet()))
        {
            Ship->PlaceShip(Start, FRotationMatrix::MakeFromX(*Course).ToQuat());
            Ship->SetJumpEngaged(true);
            for (int32 Tick = 0; Tick < 4 && !Ship->IsInTransit(); ++Tick)
            {
                Ship->Tick(0.05f);
            }
            if (TestTrue(TEXT("aligned, engaged and charged, the fold opens"), Ship->IsInTransit()))
            {
                TestTrue(TEXT("the target is kept"), Ship->GetTarget().IsSet());
                TestFalse(TEXT("but in the fold nobody is shown its mark"),
                          UShipTargetOverlay::ShowsTargetMark(*Ship, Pilot, Home));
                TestFalse(TEXT("nor the prograde"), UShipTargetOverlay::ShowsPrograde(*Ship, Pilot, Home));
                TestTrue(TEXT("and the overlay hides, camera or no"),
                         Decide(PilotEye, Ahead, Pilot).Shape == ETargetMarkShape::None
                             && Overlay->GetVisibility() == ESlateVisibility::Collapsed);
            }
        }
    }
    return true;
}

#endif
