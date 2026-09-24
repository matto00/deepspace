#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFlightInputTest,
    "DeepSpace.Player.FlightInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The throttle is a lever, not a button: input sweeps it and it stays where it
 * is left. That is what makes a cruise something the player sets and walks
 * away from, rather than something they hold down.
 */
bool FFlightInputTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("FlightInputTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    ADeepSpaceCharacter* Player = World->SpawnActor<ADeepSpaceCharacter>();

    if (TestNotNull(TEXT("the world has a ship subsystem"), Ship) &&
        TestNotNull(TEXT("the character spawns"), Player))
    {
        // The sweep rate is a tuning value; this test pins the behaviour
        // around whatever it is by sweeping for exactly one second.
        const float Rate = 0.5f;

        // Nobody is flying: input goes nowhere.
        Player->SetFlightInput(FVector::ZeroVector, 1.0f);
        Player->Tick(1.0f);
        TestEqual(TEXT("a player who is not the pilot moves no throttle"),
                  Player->GetThrottle(), 0.0f);

        Ship->SetPilot(Player);

        // Held open for a second: one second of sweep, no more.
        Player->SetFlightInput(FVector::ZeroVector, 1.0f);
        Player->Tick(1.0f);
        TestEqual(TEXT("holding the throttle sweeps it at the sweep rate"),
                  Player->GetThrottle(), Rate, KINDA_SMALL_NUMBER);
        TestEqual(TEXT("and the ship has the command"),
                  Ship->GetFlightState().GetCommand().Throttle,
                  static_cast<double>(Rate), static_cast<double>(KINDA_SMALL_NUMBER));

        // Released: the lever stays put. This is the whole point.
        Player->SetFlightInput(FVector::ZeroVector, 0.0f);
        Player->Tick(5.0f);
        TestEqual(TEXT("releasing the throttle leaves it where it was"),
                  Player->GetThrottle(), Rate, KINDA_SMALL_NUMBER);

        // Attitude is held, not swept: it passes straight through.
        Player->SetFlightInput(FVector(0.0, 1.0, 0.0), 0.0f);
        Player->Tick(0.1f);
        TestEqual(TEXT("attitude reaches the ship as given"),
                  Ship->GetFlightState().GetCommand().AttitudeRate.Y, 1.0, static_cast<double>(KINDA_SMALL_NUMBER));

        // The lever has stops.
        Player->SetFlightInput(FVector::ZeroVector, 1.0f);
        Player->Tick(100.0f);
        TestEqual(TEXT("the throttle stops wide open"), Player->GetThrottle(), 1.0f);
        Player->SetFlightInput(FVector::ZeroVector, -1.0f);
        Player->Tick(100.0f);
        TestEqual(TEXT("and stops full astern"), Player->GetThrottle(), -1.0f);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
