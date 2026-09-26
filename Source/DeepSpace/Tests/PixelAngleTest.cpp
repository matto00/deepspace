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
 * the one answer. Headless there is no viewport, so what they agree on here
 * is the fallback; the live field of view is a playtest question.
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
    TestEqual(TEXT("no width: the projection's own default"), ShipSky::PixelAngle(90.0, 0.0), Fallback);
    TestEqual(TEXT("no field of view: the same"), ShipSky::PixelAngle(0.0, 1920.0), Fallback);
    TestEqual(TEXT("no world: the same"), ShipSky::ViewPixelAngle(nullptr), Fallback);

    FSkyWorld Test(TEXT("OnePixelAngleWorld"));
    if (!TestNotNull(TEXT("a counter-frame"), Test.Frame) || !TestNotNull(TEXT("and a sky"), Test.Sky))
    {
        return false;
    }
    Test.BeginPlay();
    const double Answer = ShipSky::ViewPixelAngle(Test.World);
    TestEqual(TEXT("the sky sizes by the one answer"), Test.Sky->GetPixelAngle(), Answer);
    TestEqual(TEXT("and so does the counter-frame"), Test.Frame->GetPixelAngle(), Answer);
    return true;
}

#endif
