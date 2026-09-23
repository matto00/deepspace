#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPilotedTest,
    "DeepSpace.Ship.Piloted",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPilotedTest::RunTest(const FString& Parameters)
{
    // A throwaway world: world subsystems are created with it, which is the
    // only way to get a real UShipSubsystem.
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PilotedTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    if (TestNotNull(TEXT("the world has a ship subsystem"), Ship))
    {
        TestFalse(TEXT("a new ship is not piloted"), Ship->IsPiloted());

        APawn* Pilot = World->SpawnActor<APawn>();
        TestNotNull(TEXT("a pilot pawn spawns"), Pilot);
        Ship->SetPilot(Pilot);
        TestTrue(TEXT("seating a pilot pilots the ship"), Ship->IsPiloted());
        TestEqual(TEXT("the ship knows who is flying"), Ship->GetPilot(), Pilot);

        Ship->ClearPilot();
        TestFalse(TEXT("vacating the seat un-pilots the ship"), Ship->IsPiloted());

        // The subsystem holds its pilot weakly: a destroyed pawn is no pilot.
        Ship->SetPilot(Pilot);
        Pilot->Destroy();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        TestFalse(TEXT("a destroyed pilot does not keep the ship piloted"), Ship->IsPiloted());
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
