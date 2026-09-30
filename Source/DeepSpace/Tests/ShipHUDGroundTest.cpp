#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Tests/GroundFixtures.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 12: below the floor the corner says how high the ship is
 * over the rock and how it is moving vertically, and the motion line names
 * the third lever -- words from getters, never recomputed in the widget. No
 * colour, nothing that blinks, no percentages, no time to the ground.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipHUDGroundTest, "DeepSpace.UI.HUDGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipHUDGroundTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    const FString Sep = NavText::Separator;
    TestEqual(TEXT("tenths under ten metres"), UShipHUDWidget::AltitudeWords(150.0), FString(TEXT("1.5 M")));
    TestEqual(TEXT("whole metres from ten"), UShipHUDWidget::AltitudeWords(84000.0), FString(TEXT("840 M")));
    TestEqual(TEXT("a sink"), UShipHUDWidget::VerticalWords(-300.0), FString(TEXT("SINKING 3 M/S")));
    TestEqual(TEXT("a slow sink, in tenths"), UShipHUDWidget::VerticalWords(-50.0), FString(TEXT("SINKING 0.5 M/S")));
    TestEqual(TEXT("a climb"), UShipHUDWidget::VerticalWords(1200.0), FString(TEXT("CLIMBING 12 M/S")));
    TestEqual(TEXT("under a tenth either way is hovering"), UShipHUDWidget::VerticalWords(4.0), FString(TEXT("HOVERING")));
    TestEqual(TEXT("the corner hovering"), UShipHUDWidget::GroundLine(150.0, 0.0, EFlightHold::Free),
              FString(TEXT("1.5 M ABOVE GROUND")) + Sep + TEXT("HOVERING"));
    TestEqual(TEXT("the corner sinking"), UShipHUDWidget::GroundLine(84000.0, -300.0, EFlightHold::Free),
              FString(TEXT("840 M ABOVE GROUND")) + Sep + TEXT("SINKING 3 M/S"));
    TestEqual(TEXT("and held off a ridge, or by the skim cap"), UShipHUDWidget::GroundLine(84000.0, 0.0, EFlightHold::HoldingOff),
              FString(TEXT("840 M ABOVE GROUND")) + Sep + TEXT("HOVERING") + Sep + TEXT("HOLDING OFF"));
    TestFalse(TEXT("no time to the ground in the corner"), UShipHUDWidget::GroundLine(84000.0, -300.0, EFlightHold::Free).Contains(TEXT("ETA")));
    TestEqual(TEXT("the lever at zero"), UShipHUDWidget::VerticalLeverWords(0.0), FString(TEXT("HOVER")));
    TestEqual(TEXT("the lever climbing"), UShipHUDWidget::VerticalLeverWords(500.0), FString(TEXT("CLIMB 5 M/S")));
    TestEqual(TEXT("the lever sinking"), UShipHUDWidget::VerticalLeverWords(-300.0), FString(TEXT("SINK 3 M/S")));

    // A flight state, in the regime, hovering: the motion line names HOVER.
    const FGroundFieldRef Swells = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(UniverseUnits::CmPerEarthRadius, 100.0, 1.0e5);
    const FFlightSurface World = SurfaceOver(Swells, 1.02e6 + 100.0);
    const FVector3d D(0.0, 0.0, 1.0);
    FShipFlightState Flight;
    Flight.SetSurfaces({ World });
    Flight.SetWells({ { World.Centre, ShipFlight::StandardGravityCmS2 * World.Radius * World.Radius, World.Radius } });
    Flight.SetUniverseTransform(Above(World, D, 2.0e5), Level(D));
    Flight.Step(1.0 / 60.0);
    TestTrue(TEXT("in the regime the motion line names HOVER, in ink"), UShipHUDWidget::MotionLine(Flight).Ink.EndsWith(Sep + TEXT("HOVER")));

    // F there: cruise in ink, and the corner's words for the mode.
    FShipFlightCommand Command = Flight.GetCommand();
    Command.bDrive = true;
    Command.DriveNotch = 3;
    Flight.SetCommand(Command);
    Flight.Step(1.0 / 60.0);
    const UShipHUDWidget::FMotionWords Words = UShipHUDWidget::MotionLine(Flight);
    TestTrue(FString::Printf(TEXT("DRIVE ABOVE THE FLOOR, with cruise in ink (\"%s\" / \"%s\")"), *Words.Ink, *Words.Dim),
             Words.Ink.Contains(TEXT("CRUISE")) && Words.Ink.Contains(TEXT("DRIVE ABOVE THE FLOOR")) && Words.Dim.Contains(TEXT("DRIVE")));

    // Above the regime the lever is dim, and says why.
    Command.bDrive = false;
    Command.Vertical = 1.0;
    Flight.SetCommand(Command);
    Flight.SetUniverseTransform(Above(World, D, 5.2e6), Level(D));
    Flight.Step(1.0 / 60.0);
    Flight.SetUniverseTransform(Above(World, D, 4.95e6 + 150.0), Level(D));
    Flight.Step(1.0 / 60.0);
    Flight.SetUniverseTransform(Above(World, D, 5.2e6), Level(D));
    Flight.Step(1.0 / 60.0);
    TestTrue(FString::Printf(TEXT("in the regime's hysteresis band, the lever dim and ABOVE THE GROUND'S REACH (\"%s\")"), *UShipHUDWidget::MotionLine(Flight).Dim),
             UShipHUDWidget::MotionLine(Flight).Dim.Contains(TEXT("CLIMB 200 M/S")) && UShipHUDWidget::MotionLine(Flight).Dim.Contains(TEXT("ABOVE THE GROUND'S REACH")));

    // At a giant's floor sphere, in cruise, with a solid world a hundred
    // million km off: the regime is the giant's (decision 8), and the corner
    // never reads the other world's rock -- nor does the flight state call it
    // the ground below (GetGroundAltitude is the nearest world's, if solid).
    FFlightSurface Giant;
    Giant.Centre = Somewhere();
    Giant.Radius = 7.0e9;
    Giant.Floor = 1.12e7;
    Giant.bWorld = true;
    const FFlightSurface Rock = SurfaceOver(Swells, 1.02e6 + 100.0, Somewhere() + FVector(1.0e13, 0.0, 0.0));
    FShipFlightState OverGiant;
    OverGiant.SetSurfaces({ Giant, Rock });
    OverGiant.SetUniverseTransform(Giant.Centre + FVector(D) * (Giant.Radius + Giant.Floor + 100.0), Level(D));
    OverGiant.Step(1.0 / 60.0);
    const FFlightSurface* RegimeWorld = OverGiant.GetRegimeSurface();
    TestTrue(TEXT("at the giant's floor, cruising, the ship is in the giant's regime"),
             OverGiant.IsInNearRegime() && OverGiant.GetMode() == EFlightMode::Cruise && RegimeWorld && !RegimeWorld->HasGround());
    TestFalse(TEXT("and the far rock is not the ground below it"), OverGiant.GetGroundAltitude().IsSet());
    const FString Corner = UShipHUDWidget::AltitudeLineText(OverGiant, FSkySystem()).ToString();
    TestFalse(FString::Printf(TEXT("and the corner does not read that rock (\"%s\")"), *Corner), Corner.Contains(TEXT("ABOVE GROUND")));
    return true;
}

#endif
