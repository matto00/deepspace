#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyProjection.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FOnePixelAngleTest,
    "DeepSpace.Sky.OnePixelAngle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Everything drawn on the dome is sized by one pixel angle. The sky sizes
 * the neighbour stars by it and the counter-frame sizes the background stars
 * and the course marker by it; two formulas for it are two sizes for a star
 * the first time one of them is changed and the other is not.
 *
 * The formula is checked by its values, and both actors are checked to give
 * the one answer. Headless there is no viewport to be wide, but there is a
 * player's camera to zoom: it is zoomed away from the fallback's field of
 * view, so an actor that sized by anything but the live view would disagree.
 * The live width is a playtest question.
 */
bool FOnePixelAngleTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;

    // tan(45 degrees) is 1: a 90 degree view 1920 pixels wide.
    TestTrue(TEXT("90 degrees over 1920 pixels is 2 / 1920 radians a pixel"),
             FMath::IsNearlyEqual(ShipSky::PixelAngle(90.0, 1920.0), 2.0 / 1920.0, 1e-12));
    // A narrower view on a smaller screen: 2 tan(30 degrees) / 1000.
    TestTrue(TEXT("60 degrees over 1000 pixels is 2 tan(30 degrees) / 1000"),
             FMath::IsNearlyEqual(ShipSky::PixelAngle(60.0, 1000.0), 2.0 * FMath::Tan(UE_DOUBLE_PI / 6.0) / 1000.0, 1e-12));
    // Zooming in makes a pixel a smaller angle, and so does a wider screen.
    TestTrue(TEXT("a narrower field of view, a smaller angle"),
             ShipSky::PixelAngle(40.0, 1920.0) < ShipSky::PixelAngle(90.0, 1920.0));
    TestTrue(TEXT("a wider screen, a smaller angle"),
             ShipSky::PixelAngle(90.0, 3840.0) < ShipSky::PixelAngle(90.0, 1920.0));

    const double Fallback = FSkyViewParams().PixelAngle;
    constexpr double Tight = 1e-15;
    TestEqual(TEXT("no view at all: the projection's own default"), ShipSky::PixelAngle(0.0, 0.0), Fallback, Tight);
    TestEqual(TEXT("no world: the same"), ShipSky::ViewPixelAngle(nullptr), Fallback, Tight);
    // Only the missing half is assumed.
    TestEqual(TEXT("no width: the field of view over the fallback's width"), ShipSky::PixelAngle(60.0, 0.0),
              ShipSky::PixelAngle(60.0, ShipSky::FallbackWidthPixels), Tight);
    TestEqual(TEXT("no field of view: the fallback's over the width"), ShipSky::PixelAngle(0.0, 1000.0),
              ShipSky::PixelAngle(ShipSky::FallbackFovDegrees, 1000.0), Tight);

    FSkyWorld Test(TEXT("OnePixelAngleWorld"));
    APlayerController* Player = Test.World ? Test.World->SpawnActor<APlayerController>() : nullptr;
    if (!TestNotNull(TEXT("a counter-frame"), Test.Frame) || !TestNotNull(TEXT("and a sky"), Test.Sky)
        || !TestNotNull(TEXT("and a player"), Player))
    {
        return false;
    }
    // The player's camera is spawned as its actors are initialised for play.
    Test.BeginPlay();
    if (!TestNotNull(TEXT("the player has a camera"), Player->PlayerCameraManager.Get()))
    {
        return false;
    }
    // Zoomed in, as a pilot leaning toward the glass might be.
    Player->PlayerCameraManager->SetFOV(60.0f);

    const double Answer = ShipSky::ViewPixelAngle(Test.World);
    TestEqual(TEXT("the one answer is the zoomed camera's"), Answer, ShipSky::PixelAngle(60.0, 0.0), Tight);
    TestTrue(TEXT("which is not the fallback, so agreeing on it means something"),
             FMath::Abs(Answer / Fallback - 1.0) > 0.1);
    TestEqual(TEXT("the sky sizes by the one answer"), Test.Sky->GetPixelAngle(), Answer, Tight);
    TestEqual(TEXT("and so does the counter-frame"), Test.Frame->GetPixelAngle(), Answer, Tight);
    return true;
}

#endif
