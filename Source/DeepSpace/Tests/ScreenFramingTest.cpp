#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "SceneView.h"
#include "Ship/ShipLaptop.h"
#include "Ship/ShipNavScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScreenFramingTest,
    "DeepSpace.Ship.ScreenFraming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /**
     * How much of the frame a panel fills, on each axis, as the engine
     * would draw it: 1 is the frame's edge. The projection is the engine's
     * own (FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle),
     * not a re-derivation of it, so the framing is checked against what the
     * renderer does rather than against the same arithmetic that made it.
     */
    FVector2D ProjectedExtent(const FVector2D& SizeCm, float DistanceCm, float Fov, float CameraAspect,
                              EAspectRatioAxisConstraint Constraint, const FIntPoint& Viewport)
    {
        FMinimalViewInfo View;
        View.Location = FVector::ZeroVector;
        View.Rotation = FRotator::ZeroRotator;
        View.FOV = Fov;
        View.AspectRatio = CameraAspect;
        View.bConstrainAspectRatio = false;
        View.ProjectionMode = ECameraProjectionMode::Perspective;

        const FIntRect Rect(0, 0, Viewport.X, Viewport.Y);
        FSceneViewProjectionData Projection;
        Projection.SetViewRectangle(Rect);
        Projection.ViewOrigin = View.Location;
        // Unreal's world axes (X forward, Y right, Z up) into view space.
        Projection.ViewRotationMatrix = FInverseRotationMatrix(View.Rotation) * FMatrix(
            FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
        FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, Constraint, Rect, Projection);
        const FMatrix ViewProjection = Projection.ComputeViewProjectionMatrix();

        FVector2D Extent(0.0, 0.0);
        for (const double Sx : {-1.0, 1.0})
        {
            for (const double Sz : {-1.0, 1.0})
            {
                const FVector Corner(DistanceCm, Sx * SizeCm.X * 0.5, Sz * SizeCm.Y * 0.5);
                const FVector4 Clip = ViewProjection.TransformFVector4(FVector4(Corner, 1.0));
                Extent.X = FMath::Max(Extent.X, FMath::Abs(Clip.X / Clip.W));
                Extent.Y = FMath::Max(Extent.Y, FMath::Abs(Clip.Y / Clip.W));
            }
        }
        return Extent;
    }

    float Margin()
    {
        const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Screen.FrameMargin"));
        return Var ? Var->GetFloat() : 0.02f;
    }
}

/**
 * Sat at a screen, the whole of it is in view. The developer, at the chart:
 * "it's too big for the screen" -- a fixed 52 degrees framed the 26 cm laptop
 * and cut the 68 cm chart off on every side. The field of view is now fitted
 * to each panel, bezel included, from its size, the view distance and the
 * window's shape, and this holds that for both real screens at the aspects a
 * window is likely to have -- including one narrower than the camera's own,
 * which is where the engine's default constraint takes width away.
 */
bool FScreenFramingTest::RunTest(const FString& Parameters)
{
    const float Fill = 1.0f - 2.0f * Margin();
    const float CameraAspect = GetDefault<UCameraComponent>()->AspectRatio;
    const EAspectRatioAxisConstraint Configured = GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint;

    struct FScreenCase
    {
        const TCHAR* Name;
        const AShipScreen* Screen;
        float DistanceCm;
    };
    // The class defaults are the real screens: build_hauler.py sets the
    // chart's view_distance_cm to the class's own 60, and nothing on the
    // laptop at all.
    const FScreenCase Screens[] = {
        {TEXT("laptop"), GetDefault<AShipLaptop>(), 38.0f},
        {TEXT("chart"), GetDefault<AShipNavScreen>(), 60.0f},
    };
    const FIntPoint Viewports[] = {{1920, 1080}, {1920, 1200}, {1440, 1080}};
    const EAspectRatioAxisConstraint Constraints[] = {
        Configured, AspectRatio_MaintainXFOV, AspectRatio_MaintainYFOV, AspectRatio_MajorAxisFOV};

    for (const FScreenCase& Case : Screens)
    {
        TestEqual(FString::Printf(TEXT("the %s's view distance is the one the level uses"), Case.Name),
                  Case.Screen->GetViewDistanceCm(), Case.DistanceCm);

        // The framed size is the quad as drawn plus the bezel, not a number
        // kept beside it that could drift.
        const UWidgetComponent* Panel = Case.Screen->GetScreen();
        const FVector2D Glass = Panel->GetDrawSize() * Panel->GetRelativeScale3D().X;
        const FVector2D Framed = Case.Screen->GetFramedSizeCm();
        TestTrue(FString::Printf(TEXT("the %s frames its glass and a bezel round it"), Case.Name),
                 Framed.X > Glass.X && Framed.Y > Glass.Y);

        for (const EAspectRatioAxisConstraint Constraint : Constraints)
        {
            for (const FIntPoint& Viewport : Viewports)
            {
                const float Aspect = static_cast<float>(Viewport.X) / static_cast<float>(Viewport.Y);
                const float Fov = AShipScreen::FitFieldOfView(Framed, Case.DistanceCm, Aspect, Constraint,
                                                              CameraAspect, Margin());
                const FVector2D Extent = ProjectedExtent(Framed, Case.DistanceCm, Fov, CameraAspect,
                                                         Constraint, Viewport);
                const FString Where = FString::Printf(TEXT("the %s at %dx%d, constraint %d (%.1f deg)"),
                                                      Case.Name, Viewport.X, Viewport.Y,
                                                      static_cast<int32>(Constraint), Fov);

                TestTrue(Where + TEXT(": the whole panel is inside the frame, with the margin"),
                         Extent.X <= Fill + 1e-3 && Extent.Y <= Fill + 1e-3);
                // Fitted, not merely inside: the tighter axis is filled to
                // the margin, so the screen is still something you read.
                TestTrue(Where + TEXT(": and fills the frame up to it on its tighter axis"),
                         FMath::Max(Extent.X, Extent.Y) >= Fill - 1e-3);
            }
        }
    }

    // The casing framed is the one each screen has. The laptop's lid is 30 x
    // 20 cm (ShipLaptop.cpp); one bezel for both axes framed it 21.3 cm high,
    // and height is the axis a 3:2 lid fills first in a 16:9 window. The
    // chart's face is the desk screen's 70 x 50.
    const AShipScreen* Laptop = GetDefault<AShipLaptop>();
    const FVector2D LaptopFramed = Laptop->GetFramedSizeCm();
    TestTrue(FString::Printf(TEXT("the laptop frames its lid, 30 x 20 cm (%.2f x %.2f)"),
                             LaptopFramed.X, LaptopFramed.Y),
             FMath::IsNearlyEqual(LaptopFramed.X, 30.0, 0.01) && FMath::IsNearlyEqual(LaptopFramed.Y, 20.0, 0.01));
    const FVector2D ChartFramed = GetDefault<AShipNavScreen>()->GetFramedSizeCm();
    TestTrue(FString::Printf(TEXT("the chart frames the desk screen's face, 70 x 50 cm (%.2f x %.2f)"),
                             ChartFramed.X, ChartFramed.Y),
             FMath::IsNearlyEqual(ChartFramed.X, 70.0, 0.01) && FMath::IsNearlyEqual(ChartFramed.Y, 50.0, 0.01));

    // The laptop read right at the old fixed 52 degrees, and the developer
    // said so; the fitted framing must give it back, in the window it was
    // played in. A degree is about 2% of the frame: past that it is a
    // different view of the same machine.
    const float LaptopFov = Laptop->GetUseFieldOfView(16.0f / 9.0f, Configured, CameraAspect);
    TestTrue(FString::Printf(TEXT("the laptop frames at the 52 degrees that looked right (%.2f)"), LaptopFov),
             FMath::Abs(LaptopFov - 52.0f) < 1.0f);

    // And the complaint itself: at the old 52 degrees the chart overflowed.
    const AShipScreen* Chart = GetDefault<AShipNavScreen>();
    const FVector2D OldChart = ProjectedExtent(Chart->GetFramedSizeCm(), 60.0f, 52.0f, CameraAspect,
                                               Configured, FIntPoint(1920, 1080));
    TestTrue(TEXT("the chart did not fit at the old fixed angle"), FMath::Max(OldChart.X, OldChart.Y) > 1.0);

    // What the game does: zooming the chart from its chair sets the camera
    // to the fitted angle, not the laptop's and not a constant. Headless
    // there is no viewport, and the character falls back to 16:9. The player
    // has a controller, because the chair's view is the player's own and E
    // zooms what that view is on (system map spec, decision 13).
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ScreenFramingTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    {
        AShipNavScreen* Screen = World->SpawnActor<AShipNavScreen>(FVector(200.0, 0.0, 105.0), FRotator::ZeroRotator);
        ADeepSpaceCharacter* Player = World->SpawnActor<ADeepSpaceCharacter>(FVector(0.0, 0.0, 90.0), FRotator::ZeroRotator);
        UCameraComponent* Camera = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
        if (TestNotNull(TEXT("the chart spawns"), Screen) && TestNotNull(TEXT("the player spawns"), Player) &&
            TestNotNull(TEXT("with a camera"), Camera))
        {
            // The window's shape is what fitting turns on, and headless there is no
            // window: the character below only ever sees the 16:9 fallback. So the
            // step from a real viewport's size to the aspect framed for is checked
            // here, on the player's own camera, at the 4:3 window where the chart
            // fits least well.
            TestFalse(TEXT("the player's camera does not letterbox, so the window's shape is what counts"),
                      Camera->bConstrainAspectRatio);

            const ADeepSpaceCharacter::FFramingView Narrow =
                ADeepSpaceCharacter::ResolveFramingView(FIntPoint(1440, 1080), Configured, *Camera);
            TestTrue(FString::Printf(TEXT("a 1440x1080 window is framed as 4:3 (%.3f)"), Narrow.Aspect),
                     FMath::IsNearlyEqual(Narrow.Aspect, 4.0f / 3.0f, 1e-4f));
            const ADeepSpaceCharacter::FFramingView Wide =
                ADeepSpaceCharacter::ResolveFramingView(FIntPoint(1920, 1200), Configured, *Camera);
            TestTrue(FString::Printf(TEXT("a 1920x1200 window is framed as 16:10 (%.3f)"), Wide.Aspect),
                     FMath::IsNearlyEqual(Wide.Aspect, 16.0f / 10.0f, 1e-4f));
            const ADeepSpaceCharacter::FFramingView None =
                ADeepSpaceCharacter::ResolveFramingView(FIntPoint(0, 0), Configured, *Camera);
            TestTrue(TEXT("no viewport falls back to 16:9"), FMath::IsNearlyEqual(None.Aspect, 16.0f / 9.0f, 1e-4f));

            const EAspectRatioAxisConstraint Expected = Camera->bOverrideAspectRatioAxisConstraint
                ? Camera->AspectRatioAxisConstraint.GetValue()
                : Configured;
            TestEqual(TEXT("the axis kept is the one the player's settings or the camera choose"),
                      static_cast<int32>(Narrow.Constraint), static_cast<int32>(Expected));

            // A camera that letterboxes draws its own shape in any window,
            // and one that keeps its own axis overrides the player's.
            UCameraComponent* Letterboxed = NewObject<UCameraComponent>(Player);
            Letterboxed->bConstrainAspectRatio = true;
            Letterboxed->AspectRatio = 2.0f;
            TestTrue(TEXT("a letterboxing camera is framed at its own aspect"),
                     FMath::IsNearlyEqual(ADeepSpaceCharacter::ResolveFramingView(FIntPoint(1440, 1080), Configured,
                                                                                  *Letterboxed).Aspect,
                                          2.0f, 1e-4f));
            UCameraComponent* OwnAxis = NewObject<UCameraComponent>(Player);
            OwnAxis->bOverrideAspectRatioAxisConstraint = true;
            OwnAxis->AspectRatioAxisConstraint = AspectRatio_MaintainYFOV;
            TestEqual(TEXT("a camera keeping its own axis is framed on it"),
                      static_cast<int32>(ADeepSpaceCharacter::ResolveFramingView(
                          FIntPoint(1440, 1080), AspectRatio_MaintainXFOV, *OwnAxis).Constraint),
                      static_cast<int32>(AspectRatio_MaintainYFOV));

            // And what that means for the chart in that window: whole, and
            // filling it, as the engine draws it.
            const AShipScreen* NarrowChart = GetDefault<AShipNavScreen>();
            const float NarrowFov = NarrowChart->GetUseFieldOfView(Narrow.Aspect, Narrow.Constraint,
                                                                   Camera->AspectRatio);
            const FVector2D NarrowSeen = ProjectedExtent(NarrowChart->GetFramedSizeCm(),
                                                         NarrowChart->GetViewDistanceCm(), NarrowFov,
                                                         Camera->AspectRatio, Narrow.Constraint,
                                                         FIntPoint(1440, 1080));
            TestTrue(FString::Printf(TEXT("the chart is whole in a 1440x1080 window (%.3f x %.3f)"),
                                     NarrowSeen.X, NarrowSeen.Y),
                     NarrowSeen.X <= Fill + 1e-3 && NarrowSeen.Y <= Fill + 1e-3 &&
                         FMath::Max(NarrowSeen.X, NarrowSeen.Y) >= Fill - 1e-3);

            // BeginPlay's setup, which is what gives the walking view its angle.
            Player->ConfigureFirstPersonBody();
            const float Walking = Camera->FieldOfView;
            APlayerController* Controller = World->SpawnActor<APlayerController>();
            if (Controller)
            {
                Controller->Possess(Player);
            }
            Player->UseScreen(Screen);
            Player->PlaceCamera(0.016f, Player->GetViewRotation());
            TestTrue(FString::Printf(TEXT("sat in the chart's chair, unzoomed, the view keeps the walking angle (%.1f)"),
                                     Camera->FieldOfView),
                     FMath::IsNearlyEqual(Camera->FieldOfView, Walking, 0.01f));
            Player->PressInteract();
            TestTrue(TEXT("E, looking at the chart, zooms it"), Player->GetZoomedScreen() == Screen);
            Player->PlaceCamera(0.016f, Player->GetViewRotation());
            const float Wanted = Screen->GetUseFieldOfView(16.0f / 9.0f, Configured, Camera->AspectRatio);
            TestTrue(FString::Printf(TEXT("zoomed on the chart the view is fitted to it (%.1f, want %.1f)"),
                                     Camera->FieldOfView, Wanted),
                     FMath::IsNearlyEqual(Camera->FieldOfView, Wanted, 0.01f));
            const FVector2D Seen = ProjectedExtent(Screen->GetFramedSizeCm(), Screen->GetViewDistanceCm(),
                                                   Camera->FieldOfView, Camera->AspectRatio, Configured,
                                                   FIntPoint(1920, 1080));
            TestTrue(TEXT("and the chart is whole in it"), Seen.X <= Fill + 1e-3 && Seen.Y <= Fill + 1e-3);

            Player->StopUsingScreen();
            TestTrue(FString::Printf(TEXT("standing up restores the walking view (%.1f, want %.1f)"),
                                     Camera->FieldOfView, Walking),
                     FMath::IsNearlyEqual(Camera->FieldOfView, Walking, 0.01f));
            if (Controller)
            {
                Controller->UnPossess();
                Controller->Destroy();
            }
        }
    }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
