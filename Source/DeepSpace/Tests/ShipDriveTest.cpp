#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkySystem.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDriveTest,
    "DeepSpace.Ship.Drive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    struct FScopedCVar
    {
        IConsoleVariable* Variable = nullptr;
        FString Previous;

        FScopedCVar(const TCHAR* Name, float Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name))
        {
            if (Variable)
            {
                Previous = Variable->GetString();
                Variable->Set(*FString::SanitizeFloat(Value), ECVF_SetByCode);
            }
        }

        ~FScopedCVar()
        {
            if (Variable)
            {
                Variable->Set(*Previous, ECVF_SetByCode);
            }
        }
    };
}

/**
 * The in-system drive as the subsystem wires it (sky decision 8): a lever the
 * pilot alone may move, which stays where it is left; its room read from what
 * is really out there each tick; and thin boosters stretching its time
 * constant rather than stopping it. FShipFlightState's own tests cover the
 * arithmetic; this covers everything the subsystem feeds it.
 */
bool FShipDriveTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DriveTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>();
    if (TestNotNull(TEXT("the world has a ship"), Ship) && TestNotNull(TEXT("and a universe"), Universe))
    {
        APawn* Pilot = World->SpawnActor<APawn>();
        APawn* Passenger = World->SpawnActor<APawn>();

        // The pilot's lever and nobody else's.
        TestFalse(TEXT("an unpiloted ship refuses the drive"), Ship->SetDriveEngaged(Pilot, true));
        Ship->SetPilot(Pilot);
        TestFalse(TEXT("a passenger cannot engage it"), Ship->SetDriveEngaged(Passenger, true));
        TestFalse(TEXT("and it stays off"), Ship->IsDriveEngaged());
        TestTrue(TEXT("the pilot can"), Ship->SetDriveEngaged(Pilot, true));
        TestTrue(TEXT("and it is on"), Ship->IsDriveEngaged());

        // Flying does not touch it: every attitude and throttle input is a
        // new command, and each must carry the lever over.
        Ship->SetFlightCommand(Pilot, 1.0f, FVector(0.0, 1.0, 0.0));
        Ship->SetFlightCommand(Pilot, 0.5f, FVector::ZeroVector);
        TestTrue(TEXT("attitude and throttle inputs leave the drive engaged"), Ship->IsDriveEngaged());
        TestEqual(TEXT("and the throttle is what was asked"), Ship->GetFlightState().GetCommand().Throttle, 0.5);

        // It persists when the pilot stands up.
        Ship->ClearPilot();
        TestTrue(TEXT("standing up leaves the drive engaged"), Ship->IsDriveEngaged());
        Ship->SetPilot(Pilot);
        TestTrue(TEXT("and the pilot can switch it off"), Ship->SetDriveEngaged(Pilot, false));
        TestFalse(TEXT("off"), Ship->IsDriveEngaged());

        // Thin boosters stretch the time constant by the fraction they
        // soften the throttle: a quarter thrust takes four times as long.
        const double Tau = FShipFlightLimits::Cruise().DriveTau;
        Ship->Tick(0.01f);
        TestEqual(TEXT("fed boosters give the drive its rated tau"), Ship->GetFlightState().GetLimits().DriveTau, Tau);
        Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
        Ship->Tick(0.01f);
        TestTrue(TEXT("starved boosters stretch it fourfold"),
                 FMath::IsNearlyEqual(Ship->GetFlightState().GetLimits().DriveTau, 4.0 * Tau, 1e-4));
        Ship->SetConsumerWeight(ShipPower::Boosters, 1.0f);

        {
            FScopedCVar LongTau(TEXT("ds.Drive.Tau"), 30.0f);
            FScopedCVar LowFloor(TEXT("ds.Drive.Floor"), 50.0f);
            Ship->Tick(0.01f);
            TestEqual(TEXT("ds.Drive.Tau is read at use"), Ship->GetFlightState().GetLimits().DriveTau, 30.0);
            TestEqual(TEXT("ds.Drive.Floor is kilometres, read at use"),
                      Ship->GetFlightState().GetLimits().DriveFloor, 50.0 * UniverseUnits::CmPerKm);
        }
        Ship->Tick(0.01f);
        TestEqual(TEXT("and let go of"), Ship->GetFlightState().GetLimits().DriveTau, Tau);

        // Its room is the nearest surface of what is really out there: at
        // the opening shot, the framed planet's.
        const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem());
        if (TestTrue(TEXT("there is a start system"), Home.IsSet()))
        {
            const FNavPlacement Opening = NavStart::OpeningPlacement(*Home);
            Ship->PlaceShip(Opening.Position, Opening.Orientation);
            Ship->Tick(0.01f);

            // Measured where the tick read it: the ship is still under way
            // from the throttle above, and moves on after the room is read.
            const FShipFlightState& Flight = Ship->GetFlightState();
            const double Floor = Flight.GetLimits().DriveFloor;
            const double Expected = LocalSystem::NearestSurfaceDistance(
                LocalSystem::Current(World), Opening.Position) - Floor;
            TestTrue(TEXT("the drive's room is LocalSystem's nearest surface, less the floor"),
                     FMath::IsNearlyEqual(Flight.GetDriveRoom(), Expected, 1.0));

            int32 Largest = 0;
            for (int32 Index = 1; Index < Home->Planets.Num(); ++Index)
            {
                Largest = Home->Planets[Index].RadiusEarth > Home->Planets[Largest].RadiusEarth ? Index : Largest;
            }
            const double ToSurface = Opening.Position.DistanceTo(Home->PlanetPosition(Largest))
                - Home->Planets[Largest].RadiusEarth * UniverseUnits::CmPerEarthRadius;
            TestTrue(TEXT("which at the opening shot is the framed planet's surface, to 1 km"),
                     FMath::Abs(Flight.GetDriveRoom() - (ToSurface - Floor)) <= UniverseUnits::CmPerKm);

            // Engaged at full throttle toward it, the room falls by e every
            // tau -- which it can only do if it is re-read every tick.
            const double Room = Flight.GetDriveRoom();
            Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
            Ship->SetDriveEngaged(Pilot, true);
            for (int32 Step = 0; Step < 150; ++Step)
            {
                Ship->Tick(static_cast<float>(Tau / 150.0));
            }
            Ship->Tick(0.0f);
            TestTrue(FString::Printf(TEXT("after tau the room is 1/e of what it was, to 2%%: %.4f"), Flight.GetDriveRoom() / Room),
                     FMath::IsNearlyEqual(Flight.GetDriveRoom() / Room, 1.0 / UE_DOUBLE_EULERS_NUMBER, 0.02 / UE_DOUBLE_EULERS_NUMBER));
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
