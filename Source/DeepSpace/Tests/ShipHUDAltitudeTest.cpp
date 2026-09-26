#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "GameFramework/Pawn.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipHUDAltitudeTest,
    "DeepSpace.UI.HUDAltitude",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The altitude corner: how far the nearest surface is, whose it is, and
 * whether the drive has settled as close as it goes. The developer could
 * not tell whether they were near enough to land; this is the line that
 * says, so every unit is checked either side of where it hands over, and
 * the floor's words either side of its band.
 */
bool FShipHUDAltitudeTest::RunTest(const FString& Parameters)
{
    const double Km = UniverseUnits::CmPerKm;
    const double AU = UniverseUnits::CmPerAU;
    const auto Words = [](double Cm) { return UShipHUDWidget::AltitudeWords(Cm); };

    // -- The units, at each boundary ----------------------------------------
    TestEqual(TEXT("on the surface"), Words(0.0), FString(TEXT("0 M")));
    TestEqual(TEXT("never below it"), Words(-5.0e4), FString(TEXT("0 M")));
    TestEqual(TEXT("metres under a kilometre"), Words(999.4 * 100.0), FString(TEXT("999 M")));
    TestEqual(TEXT("a kilometre, never 1000 M"), Words(999.6 * 100.0), FString(TEXT("1.0 KM")));
    TestEqual(TEXT("tenths of a kilometre close in"), Words(42.26 * Km), FString(TEXT("42.3 KM")));
    TestEqual(TEXT("just under a hundred"), Words(99.94 * Km), FString(TEXT("99.9 KM")));
    TestEqual(TEXT("a hundred, never 100.0 KM"), Words(99.96 * Km), FString(TEXT("100 KM")));
    TestEqual(TEXT("the drive floor"), Words(100.0 * Km), FString(TEXT("100 KM")));
    TestEqual(TEXT("thousands grouped"), Words(4213.0 * Km), FString(TEXT("4,213 KM")));
    TestEqual(TEXT("just under ten thousand"), Words(9999.4 * Km), FString(TEXT("9,999 KM")));
    TestEqual(TEXT("ten thousand, in thousands"), Words(9999.6 * Km), FString(TEXT("10 THOUSAND KM")));
    TestEqual(TEXT("the Moon's distance"), Words(384400.0 * Km), FString(TEXT("384 THOUSAND KM")));
    TestEqual(TEXT("just under a hundredth of an AU"), Words(0.00949 * AU), FString(TEXT("1,420 THOUSAND KM")));
    TestEqual(TEXT("a hundredth of an AU, in AU"), Words(0.00951 * AU), FString(TEXT("0.010 AU")));
    TestEqual(TEXT("thousandths under one"), Words(0.4567 * AU), FString(TEXT("0.457 AU")));
    TestEqual(TEXT("just under one"), Words(0.9994 * AU), FString(TEXT("0.999 AU")));
    TestEqual(TEXT("one, never 1.000 AU"), Words(0.9996 * AU), FString(TEXT("1.00 AU")));
    TestEqual(TEXT("hundredths under a hundred"), Words(5.2 * AU), FString(TEXT("5.20 AU")));
    TestEqual(TEXT("just under a hundred"), Words(99.994 * AU), FString(TEXT("99.99 AU")));
    TestEqual(TEXT("a hundred, whole"), Words(99.996 * AU), FString(TEXT("100 AU")));
    TestEqual(TEXT("far out, grouped"), Words(7912.3 * AU), FString(TEXT("7,912 AU")));

    // -- The line ------------------------------------------------------------
    const FString Tag = FString(NavText::Separator) + TEXT("DRIVE FLOOR");
    const auto Line = [&](double Cm, bool bEdge, bool bFloor)
    {
        return UShipHUDWidget::AltitudeLine(Cm, TEXT("Kessa IV"), bEdge, bFloor);
    };
    TestEqual(TEXT("above a world, by name"), Line(212.0 * Km, false, false), FString(TEXT("212 KM ABOVE Kessa IV")));
    TestEqual(TEXT("the edge is not a world"), Line(3400.0 * AU, true, false), FString(TEXT("3,400 AU TO THE EDGE")));
    TestEqual(TEXT("held at the floor, it says so"), Line(104.0 * Km, false, true), FString(TEXT("104 KM ABOVE Kessa IV")) + Tag);
    TestEqual(TEXT("the edge has a floor too"), Line(100.0 * Km, true, true), FString(TEXT("100 KM TO THE EDGE")) + Tag);

    // -- Whether the drive holds the ship there ------------------------------
    // Not a height: the drive engaged, its lever toward the surface, and its
    // room within the band. A world below -Z, so the surface falls away +Z.
    {
        const double Floor = 100.0 * Km;
        const double Band = 0.05;
        const FVector Away = FVector::UpVector;
        const FQuat NoseDown = FRotationMatrix::MakeFromX(-FVector::UpVector).ToQuat();
        const FQuat NoseUp = FRotationMatrix::MakeFromX(FVector::UpVector).ToQuat();
        const auto Holds = [&](double SurfaceCm, bool bDrive, double Throttle, const FQuat& Nose)
        {
            FShipFlightState Flight;
            FShipFlightLimits Limits = Flight.GetLimits();
            Limits.DriveFloor = Floor;
            Flight.SetLimits(Limits);
            Flight.SetUniverseTransform(FUniversePosition(), Nose);
            FShipFlightCommand Command;
            Command.bDrive = bDrive;
            Command.Throttle = Throttle;
            Flight.SetCommand(Command);
            return UShipHUDWidget::DriveHoldsAtFloor(Flight, SurfaceCm, Away, Band);
        };
        TestTrue(TEXT("settling onto the floor, nose down, the drive holds it"), Holds(104.0 * Km, true, 1.0, NoseDown));
        TestTrue(TEXT("and at it"), Holds(Floor, true, 1.0, NoseDown));
        TestTrue(TEXT("and under it, where the drive has no room at all"), Holds(80.0 * Km, true, 1.0, NoseDown));
        TestFalse(TEXT("above the band the drive is still closing"), Holds(106.0 * Km, true, 1.0, NoseDown));
        TestFalse(TEXT("at the height with the drive off, nothing holds it"), Holds(Floor, false, 1.0, NoseDown));
        TestFalse(TEXT("the drive on and the lever at rest, nothing is pushing"), Holds(Floor, true, 0.0, NoseDown));
        TestFalse(TEXT("leaving, nose up, it is passing the floor, not held at it"), Holds(Floor, true, 1.0, NoseUp));
        TestTrue(TEXT("backing down onto it, nose up and the lever reversed, is held"), Holds(Floor, true, -1.0, NoseUp));
        TestTrue(TEXT("a heading that grazes the surface still closes, and is held"),
            Holds(Floor, true, 1.0, FRotationMatrix::MakeFromX(FVector(1.0, 0.0, -0.05)).ToQuat()));
        TestFalse(TEXT("with no floor there is no floor to be at"), [&]
        {
            FShipFlightState Flight;
            FShipFlightLimits Limits = Flight.GetLimits();
            Limits.DriveFloor = 0.0;
            Flight.SetLimits(Limits);
            Flight.SetUniverseTransform(FUniversePosition(), NoseDown);
            FShipFlightCommand Command;
            Command.bDrive = true;
            Command.Throttle = 1.0;
            Flight.SetCommand(Command);
            return UShipHUDWidget::DriveHoldsAtFloor(Flight, 0.0, Away, Band);
        }());
    }

    // -- Asked of the ship, in a real system ---------------------------------
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("HUDAltitudeTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    if (TestNotNull(TEXT("the world has a ship"), Ship))
    {
        const FSkySystem Here = LocalSystem::Current(World);
        const int32 World0 = Here.Bodies.IndexOfByPredicate([](const FSkyBody& Body) { return Body.Kind != ESkyBodyKind::Star; });
        if (TestTrue(TEXT("the ship opens in a system with a world in it"), World0 != INDEX_NONE))
        {
            const FString Name = Here.Bodies[World0].Id.ToString();
            const auto PlaceAt = [&](double AltitudeCm)
            {
                const TOptional<FNavPlacement> Placement = ShipSky::GotoPlacement(
                    Here, World0, AltitudeCm, Ship->GetFlightState().GetUniversePosition());
                if (Placement)
                {
                    Ship->PlaceShip(Placement->Position, Placement->Orientation);
                }
                return UShipHUDWidget::AltitudeLineText(*Ship).ToString();
            };

            TestEqual(TEXT("250 km over a world, the corner says so, by the world's name"),
                PlaceAt(250.0 * Km), FString::Printf(TEXT("250 KM ABOVE %s"), *Name));
            TestEqual(TEXT("it is the drive's own measure"),
                LocalSystem::NearestSurfaceDistance(LocalSystem::Current(World), Ship->GetFlightState().GetUniversePosition()) / Km,
                250.0, 1.0e-3);

            // The words follow the drive, not the height. At the floor with
            // the drive off, only the height; with a pilot's drive on and the
            // nose on the world, the drive is what holds the ship there.
            const double ShipFloor = Ship->GetFlightState().GetLimits().DriveFloor;
            const FString AtFloor = PlaceAt(ShipFloor + 1.0 * Km);
            TestFalse(TEXT("at the floor's height with the drive off, no floor words"), AtFloor.EndsWith(Tag));

            APawn* Pilot = World->SpawnActor<APawn>();
            Ship->SetPilot(Pilot);
            Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
            Ship->SetDriveEngaged(Pilot, true);
            TestTrue(TEXT("with the drive on and the nose on the world, the corner says it is the floor"),
                UShipHUDWidget::AltitudeLineText(*Ship).ToString().EndsWith(Tag));
            TestFalse(TEXT("but not from 250 km, where the drive is still closing"), PlaceAt(250.0 * Km).EndsWith(Tag));

            // Turned away from it at the floor, the ship is leaving.
            const TOptional<FNavPlacement> Placement = ShipSky::GotoPlacement(
                Here, World0, ShipFloor + 1.0 * Km, Ship->GetFlightState().GetUniversePosition());
            if (TestTrue(TEXT("a placement at the floor"), Placement.IsSet()))
            {
                Ship->PlaceShip(Placement->Position, Placement->Orientation * FQuat(FVector::UpVector, UE_DOUBLE_PI));
                TestFalse(TEXT("nose away from the world at the floor, it is leaving, not held"),
                    UShipHUDWidget::AltitudeLineText(*Ship).ToString().EndsWith(Tag));
            }

            // Asked for only its surfaces: the same line from the system
            // without its neighbours as from the one with them.
            TestEqual(TEXT("the neighbours change nothing the altitude says"),
                UShipHUDWidget::AltitudeLineText(*Ship, LocalSystem::Current(World)).ToString(),
                UShipHUDWidget::AltitudeLineText(*Ship).ToString());
            TestTrue(TEXT("and Here carries none of them"), LocalSystem::Here(World).Neighbours.IsEmpty()
                && LocalSystem::Here(World).Bodies.Num() == Here.Bodies.Num());
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
