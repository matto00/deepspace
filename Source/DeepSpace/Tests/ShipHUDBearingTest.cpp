#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipSubsystem.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipHUDBearingTest,
    "DeepSpace.UI.HUDBearing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The HUD's jump corner gives the course's bearing from the ship's nose
 * (nav decision 3): the words the pilot aims by at the helm, where the mouse
 * looks freely and the universe's own axes mean nothing. The spec checks the
 * corner by eye; this checks the words it would draw, with the ship turned
 * three ways about the same course and rolled, so a bearing taken in any
 * other frame reads wrong in at least one of them.
 */
bool FShipHUDBearingTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("HUDBearingTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>();
    if (TestNotNull(TEXT("the world has a ship"), Ship) && TestNotNull(TEXT("and a universe"), Universe))
    {
        const FString Dash = UShipHUDWidget::JumpLineText(*Ship, Universe).ToString();
        TestFalse(TEXT("with no course the jump corner has no bearing"), Dash.Contains(TEXT("ahead")));

        const TArray<FStarSystemStub> Chart = Ship->GetChart();
        if (TestTrue(TEXT("the chart has somewhere to go"), Chart.Num() > 0)
            && TestTrue(TEXT("and it plots"), Ship->PlotCourse(Chart[0].Id)))
        {
            const FVector Course = Ship->GetCourseDirection().Get(FVector::ForwardVector);
            const FUniversePosition Here = Ship->GetFlightState().GetUniversePosition();
            const FString Prefix = NavText::Jump(Ship->GetJumpState()) + NavText::Separator + Chart[0].Name
                + NavText::Separator;

            // The nose on the course, rolled: rolling about the nose must not
            // move a bearing that lies along it.
            const FQuat OnCourse = FRotationMatrix::MakeFromX(Course).ToQuat() * FQuat(FVector::ForwardVector, 0.7);
            const auto LineFacing = [&](const FQuat& Orientation)
            {
                Ship->PlaceShip(Here, Orientation);
                return UShipHUDWidget::JumpLineText(*Ship, Universe).ToString();
            };

            TestEqual(TEXT("facing the course it is dead ahead"), LineFacing(OnCourse), Prefix + TEXT("dead ahead"));

            // Yawed so the course lies 30 degrees off the nose to port, in the
            // ship's own axes: -Y is port, and yawing the ship to starboard
            // puts the course on its left.
            const FQuat PortThirty = OnCourse * FQuat(FVector::UpVector, FMath::DegreesToRadians(30.0));
            TestEqual(TEXT("yawed 30 degrees to starboard, the course is 30 degrees to port"),
                      LineFacing(PortThirty), Prefix + TEXT("30° to port"));

            // And pitched down 20 degrees, it lies above the nose.
            const FQuat UpTwenty = OnCourse * FQuat(FVector::RightVector, FMath::DegreesToRadians(20.0));
            TestEqual(TEXT("pitched 20 degrees down, the course is 20 degrees up"),
                      LineFacing(UpTwenty), Prefix + TEXT("20° up"));

            const FQuat Away = FRotationMatrix::MakeFromX(-Course).ToQuat();
            TestTrue(TEXT("facing away it is astern"), LineFacing(Away).StartsWith(Prefix + TEXT("astern")));
            Ship->PlaceShip(Here, OnCourse);
        }

        // A course to a world of this system (system map decision 12): the
        // jump line names the world, in the jump's own cone words, and its
        // fold is IN THE FOLD in both top corners -- never BETWEEN STARS,
        // which it is not.
        const FUniversePosition Start = Ship->GetFlightState().GetUniversePosition();
        const TOptional<FStarSystem> Home = Universe->GetSystemAt(Start);
        Ship->ClearCourse();
        TOptional<FBodyId> WorldCourse;
        for (int32 Orbit = Home ? Home->Planets.Num() - 1 : -1; Orbit >= 0 && !WorldCourse; --Orbit)
        {
            if (Ship->SetTarget(FBodyId{ Home->Stub.Id, Orbit, -1 }) && Ship->PlotTarget())
            {
                WorldCourse = Ship->GetPlottedWorld();
            }
        }
        if (TestTrue(TEXT("a world of this system is far enough to jump to"), WorldCourse.IsSet()))
        {
            const FString Name = NavText::WorldName(Home->Planets[WorldCourse->Planet]);
            const FVector Course = Ship->GetCourseDirection().Get(FVector::ForwardVector);
            const FString Prefix = NavText::Jump(Ship->GetJumpState(), true) + NavText::Separator + Name + NavText::Separator;
            const FQuat OnCourse = FRotationMatrix::MakeFromX(Course).ToQuat() * FQuat(FVector::ForwardVector, 0.7);
            const auto LineFacing = [&](const FQuat& Orientation)
            {
                Ship->PlaceShip(Start, Orientation);
                return UShipHUDWidget::JumpLineText(*Ship, Universe).ToString();
            };
            TestEqual(TEXT("facing a world course it is named, dead ahead"), LineFacing(OnCourse), Prefix + TEXT("dead ahead"));
            TestEqual(TEXT("and 30 degrees off it, in the jump's whole degrees"),
                      LineFacing(OnCourse * FQuat(FVector::UpVector, FMath::DegreesToRadians(30.0))), Prefix + TEXT("30° to port"));
            TestEqual(TEXT("the place line names the system"), UShipHUDWidget::PlaceLineText(*Ship, Home).ToString(),
                      NavText::Place(Home->Stub.Name, Home->Star.Class));

            SkyTestWorld::FScopedCVar Instant(TEXT("ds.Nav.ChargeSeconds"), 0.0f);
            Ship->PlaceShip(Start, OnCourse);
            Ship->SetJumpEngaged(true);
            for (int32 Tick = 0; Tick < 4 && !Ship->IsInTransit(); ++Tick)
            {
                Ship->Tick(0.05f);
            }
            if (TestTrue(TEXT("aligned, engaged and charged, the fold to the world opens"), Ship->IsInTransit()))
            {
                const FString Jump = UShipHUDWidget::JumpLineText(*Ship, Universe).ToString();
                TestEqual(TEXT("the jump line says IN THE FOLD, and which world"), Jump,
                          FString(TEXT("IN THE FOLD")) + NavText::Separator + Name);
                // In the fold the HUD asks for no system, and hands none down.
                const FString Place = UShipHUDWidget::PlaceLineText(*Ship, {}).ToString();
                TestEqual(TEXT("and so does the place line"), Place, FString(TEXT("IN THE FOLD")));
                TestFalse(TEXT("neither says BETWEEN STARS"),
                          Jump.Contains(TEXT("BETWEEN STARS")) || Place.Contains(TEXT("BETWEEN STARS")));
            }
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
