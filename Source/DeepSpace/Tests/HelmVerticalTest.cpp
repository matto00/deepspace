#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Ship/ShipVerticalLever.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The helm and the third lever (landing decision 8): Space and C sweep it
 * whatever lever F has live, a sweep stops at HOVER, a key held from before
 * sitting down or through X moves nothing until let go, X and the fold's all
 * stop set HOVER, and after X a press the way the ship is moving catches it.
 * Under a solid world's drive floor, with the drive live, Shift and Ctrl
 * move cruise's lever, which is the one flying the ship.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelmVerticalTest, "DeepSpace.Ship.HelmVertical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHelmVerticalTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("HelmVerticalWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const FShipFlightState& Flight = Ship->GetFlightState();
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);

    // 2 km over Baemsekai IV, level: in the near regime, under the drive floor.
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Fourth = Here.Bodies[4];
    const FGroundFieldRef Ground = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector3d D(Out);
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + Ground->Height(D, 0.0) + 2.0e5),
                    FRotationMatrix::MakeFromXZ(FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal(), Out).ToQuat());
    const float Dt = 1.0f / 60.0f;
    const auto Hand = [&](bool bUp, bool bDown, int32 Ups, int32 Downs)
    {
        FHelmInput Input;
        Input.bVerticalUpHeld = bUp;
        Input.bVerticalDownHeld = bDown;
        Input.VerticalUpPresses = Ups;
        Input.VerticalDownPresses = Downs;
        Ship->SetHelmInput(Pilot, Input);
        Test.Step(Dt);
    };

    // The first hands: a Space held from before sitting down moves nothing.
    Hand(true, false, 0, 0);
    Hand(true, false, 0, 0);
    TestEqual(TEXT("Space held from before sitting down is spent"), Flight.GetCommand().Vertical, 0.0);
    Hand(false, false, 0, 0);

    // A fresh press and a hold: climbing.
    Hand(true, false, 1, 0);
    for (int32 Frame = 0; Frame < 60; ++Frame) { Hand(true, false, 0, 0); }
    TestTrue(FString::Printf(TEXT("a second held sweeps a quarter of the lever up (%.3f)"), Flight.GetCommand().Vertical),
             FMath::IsNearlyEqual(Flight.GetCommand().Vertical, 0.25, 0.02));
    Hand(false, false, 0, 0);
    TestTrue(TEXT("the ship climbs on it"), Flight.GetVerticalSpeed() > 0.0);

    // C held from a climb stops at HOVER.
    for (int32 Frame = 0; Frame < 120; ++Frame) { Hand(false, true, 0, Frame == 0 ? 1 : 0); }
    TestEqual(TEXT("C held from a climb stops at HOVER"), Flight.GetCommand().Vertical, 0.0);
    Hand(false, false, 0, 0);

    // A sink, then X with C still held: HOVER, and the held C moves nothing.
    Hand(false, true, 0, 1);
    for (int32 Frame = 0; Frame < 120; ++Frame) { Hand(false, true, 0, 0); }
    TestTrue(TEXT("a fresh C sinks"), Flight.GetCommand().Vertical < -0.4);
    Ship->AllStop(Pilot);
    TestEqual(TEXT("X sets the vertical lever to HOVER"), Flight.GetCommand().Vertical, 0.0);
    Hand(false, true, 0, 0);
    TestEqual(TEXT("C held through X moves nothing"), Flight.GetCommand().Vertical, 0.0);
    Hand(false, false, 0, 0);

    // Sinking again; X, and C pressed at once while the ship is still
    // sinking (the boosters stop it within a frame or two at 2 km/s^2): the
    // press catches it at the rate it has, the 2026-09-26 ruling.
    Hand(false, true, 0, 1);
    for (int32 Frame = 0; Frame < 120; ++Frame) { Hand(false, true, 0, 0); }
    Hand(false, false, 0, 0);
    const double Sinking = Flight.GetVerticalSpeed();
    TestTrue(FString::Printf(TEXT("sinking before X (%.1f cm/s)"), Sinking), Sinking < -ShipVerticalLever::FloorCmPerSecond);
    Ship->AllStop(Pilot);
    Hand(false, false, 0, 1);
    const double Asked = ShipVerticalLever::Rate(Flight.GetCommand().Vertical, Flight.GetLimits().VerticalTop);
    TestTrue(FString::Printf(TEXT("one C straight after X catches it where it is (asks %.1f, was %.1f cm/s)"), Asked, Sinking),
             Asked < 0.0 && FMath::Abs(Asked - Sinking) <= FMath::Abs(Sinking) * 0.05);

    // F under the floor: Shift moves cruise's lever, the one flying the ship.
    Ship->AllStop(Pilot);
    Hand(false, false, 0, 0);
    Ship->SetDriveEngaged(Pilot, true);
    Test.Step(Dt);
    TestTrue(TEXT("F under the floor: DriveBelowFloor"), Flight.GetMode() == EFlightMode::DriveBelowFloor);
    const int32 Notch = Flight.GetCommand().DriveNotch;
    FHelmInput Shift;
    Shift.bUpHeld = true;
    Shift.UpPresses = 1;
    Ship->SetHelmInput(Pilot, Shift);
    Test.Step(Dt);
    TestTrue(TEXT("Shift moves cruise's lever"), Flight.GetCommand().Throttle > 0.0);
    TestEqual(TEXT("and leaves the drive's notch"), Flight.GetCommand().DriveNotch, Notch);
    return true;
}

#endif
