#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipFlightAuthorityTest,
    "DeepSpace.Ship.FlightAuthority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipFlightAuthorityTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("FlightAuthorityTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    if (TestNotNull(TEXT("the world has a ship subsystem"), Ship))
    {
        APawn* Pilot = World->SpawnActor<APawn>();
        APawn* Passenger = World->SpawnActor<APawn>();

        // Nobody is flying, so nobody may command.
        TestFalse(TEXT("an unpiloted ship refuses commands"),
                  Ship->SetFlightCommand(Pilot, 1.0f, FVector(0.0, 1.0, 0.0)));
        TestEqual(TEXT("and the command is unchanged"),
                  Ship->GetFlightState().GetCommand().Throttle, 0.0);

        Ship->SetPilot(Pilot);
        TestTrue(TEXT("the pilot may command"),
                 Ship->SetFlightCommand(Pilot, 0.5f, FVector(0.0, 1.0, 0.0)));
        TestEqual(TEXT("and the command lands"),
                  Ship->GetFlightState().GetCommand().Throttle, 0.5);

        // Someone standing behind the seat is not flying the ship.
        TestFalse(TEXT("a passenger is refused"),
                  Ship->SetFlightCommand(Passenger, -1.0f, FVector::ZeroVector));
        TestEqual(TEXT("and changes nothing"),
                  Ship->GetFlightState().GetCommand().Throttle, 0.5);
        TestEqual(TEXT("not even the attitude"),
                  Ship->GetFlightState().GetCommand().AttitudeRate, FVector(0.0, 1.0, 0.0));

        // The subsystem owns the clock: ticking it is what moves the ship.
        Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
        for (int32 Index = 0; Index < 60; ++Index)
        {
            Ship->Tick(1.0f / 60.0f);
        }
        TestTrue(TEXT("ticking the subsystem flies the ship"), Ship->GetShipSpeed() > 0.0f);
        TestTrue(TEXT("and gives it a velocity"), Ship->GetShipVelocity().Size() > 0.0);

        // Standing up stops the turn and keeps the cruise.
        Ship->SetFlightCommand(Pilot, 0.5f, FVector(0.0, 1.0, 0.0));
        Ship->ClearPilot();
        TestEqual(TEXT("leaving the seat releases the attitude"),
                  Ship->GetFlightState().GetCommand().AttitudeRate, FVector::ZeroVector);
        TestEqual(TEXT("but leaves the throttle where the pilot set it"),
                  Ship->GetFlightState().GetCommand().Throttle, 0.5);
        TestFalse(TEXT("and the former pilot may no longer command"),
                  Ship->SetFlightCommand(Pilot, 0.0f, FVector::ZeroVector));
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
