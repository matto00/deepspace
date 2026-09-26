#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipSubsystem.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipHUDBearingTest,
    "DeepSpace.UI.HUDBearing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The HUD's drive corner gives the course's bearing from the ship's nose
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
        const FString Dash = UShipHUDWidget::DriveLineText(*Ship, Universe).ToString();
        TestFalse(TEXT("with no course the drive corner has no bearing"), Dash.Contains(TEXT("ahead")));

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
                return UShipHUDWidget::DriveLineText(*Ship, Universe).ToString();
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
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
