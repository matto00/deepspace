#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLightingSubsystem.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Universe/StarSystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPowerConsumersTest,
    "DeepSpace.Ship.PowerConsumers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The allocation has to be *felt*, not merely computed, so this checks that
 * each consumer actually changes when its share does -- and it checks the
 * tag contract, which is the seam most likely to rot: the level's lights are
 * found by the actor tag Tools/build_hauler.py applies, never by name or
 * index, and nothing else connects the two.
 */
bool FShipPowerConsumersTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PowerConsumersTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    UShipLightingSubsystem* Lighting = World->GetSubsystem<UShipLightingSubsystem>();

    if (TestNotNull(TEXT("the world has a ship"), Ship)
        && TestNotNull(TEXT("and a lighting subsystem"), Lighting))
    {
        // A light exactly as Tools/build_hauler.py builds one: tagged, and
        // movable, because a Static light is baked and can never dim.
        APointLight* Lamp = World->SpawnActor<APointLight>();
        Lamp->Tags.Add(ShipPower::Lights);
        UPointLightComponent* Bulb = Lamp->FindComponentByClass<UPointLightComponent>();
        Bulb->SetMobility(EComponentMobility::Movable);
        const float Rated = 42.0f;
        Bulb->SetIntensity(Rated);

        // An untagged light of the same kind, to prove the tag is doing the
        // work rather than "every light in the level" quietly doing it.
        APointLight* Bystander = World->SpawnActor<APointLight>();
        UPointLightComponent* BystanderBulb = Bystander->FindComponentByClass<UPointLightComponent>();
        BystanderBulb->SetMobility(EComponentMobility::Movable);
        BystanderBulb->SetIntensity(Rated);

        Lighting->Refresh();
        TestEqual(TEXT("only the tagged light is the ship's"), Lighting->GetLightCount(), 1);

        // An idle jump drive wants nothing, and so takes part in no split:
        // staying put costs the ship nothing (nav decision 4). Satisfied, not
        // starved -- a thing switched off is not a thing working badly.
        Ship->Tick(0.016f);
        TestEqual(TEXT("an idle engine wants 0 W"), Ship->GetConsumerWant(ShipPower::Engine), 0.0f);
        TestEqual(TEXT("and gets none"), Ship->GetConsumerShare(ShipPower::Engine), 0.0f);

        // Engaged, it winds, and wants power to do it.
        const TArray<FStarSystemStub> Chart = Ship->GetChart();
        if (!TestTrue(TEXT("there is somewhere to go"), Chart.Num() > 0))
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
            return false;
        }
        TestTrue(TEXT("a course plots"), Ship->PlotCourse(Chart[0].Id));
        TestTrue(TEXT("and engages"), Ship->SetJumpEngaged(true));
        Ship->Tick(0.016f);
        TestTrue(TEXT("a winding engine wants power"), Ship->GetConsumerWant(ShipPower::Engine) > 0.0f);

        // Fed, a light burns at its rating. The default even split leaves
        // the lights fully satisfied because they want the least.
        TestEqual(TEXT("an even split feeds the lights fully"),
                  Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
        Lighting->Tick(0.016f);
        TestEqual(TEXT("a fed light burns at its rating"), Bulb->Intensity, Rated);

        // Starved, it dims -- and keeps glowing, because there is no
        // failure state anywhere in this system.
        Ship->SetConsumerWeight(ShipPower::Lights, 0.0f);
        TestEqual(TEXT("no weight, no share"),
                  Ship->GetConsumerSatisfaction(ShipPower::Lights), 0.0f);
        Lighting->Tick(0.016f);
        TestTrue(TEXT("a starved light dims"), Bulb->Intensity < Rated * 0.5f);
        TestTrue(TEXT("but never goes out"), Bulb->Intensity > 0.0f);
        TestEqual(TEXT("and the untagged one is untouched"), BystanderBulb->Intensity, Rated);

        // The console's switch is a different thing from starvation: off is
        // off, and the power genuinely goes elsewhere.
        Ship->SetConsumerWeight(ShipPower::Lights, 1.0f);
        const float EngineWithLights = Ship->GetConsumerShare(ShipPower::Engine);
        Ship->SetLightsOn(false);
        Lighting->Tick(0.016f);
        TestEqual(TEXT("lights off is actually off"), Bulb->Intensity, 0.0f);
        TestTrue(TEXT("and the winding engine has what they were using"),
                 Ship->GetConsumerShare(ShipPower::Engine) > EngineWithLights);
        Ship->SetLightsOn(true);

        // Boosters push softer on a thin allocation, and never stop pushing.
        Ship->SetConsumerWeight(ShipPower::Boosters, 8.0f);
        Ship->SetConsumerWeight(ShipPower::Lights, 1.0f);
        Ship->SetConsumerWeight(ShipPower::Engine, 1.0f);
        Ship->Tick(0.016f);
        const float Fed = Ship->GetLinearAcceleration();
        Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
        Ship->Tick(0.016f);
        const float Starved = Ship->GetLinearAcceleration();
        TestTrue(TEXT("starved boosters push softer"), Starved < Fed);
        TestTrue(TEXT("but they still push"), Starved > 0.0f);

        // The engine's answer to a thin allocation is time, and only time --
        // and it winds only while engaged.
        Ship->SetConsumerWeight(ShipPower::Engine, 8.0f);
        Ship->SetConsumerWeight(ShipPower::Boosters, 1.0f);
        const float Before = Ship->GetJumpCharge();
        Ship->Tick(1.0f);
        const float Fast = Ship->GetJumpCharge() - Before;
        Ship->SetConsumerWeight(ShipPower::Engine, 0.0f);
        const float Middle = Ship->GetJumpCharge();
        Ship->Tick(1.0f);
        const float Slow = Ship->GetJumpCharge() - Middle;
        TestTrue(TEXT("a fed engine charges"), Fast > 0.0f);
        TestTrue(TEXT("a starved one charges slower"), Slow < Fast);
        TestTrue(TEXT("and still charges: a starved drive always finishes"), Slow > 0.0f);

        // Stood down, it holds exactly where it was and wants nothing: the
        // charge never leaks while the player is away.
        Ship->SetConsumerWeight(ShipPower::Engine, 8.0f);
        Ship->SetJumpEngaged(false);
        const float Held = Ship->GetJumpCharge();
        for (int32 Second = 0; Second < 60; ++Second)
        {
            Ship->Tick(1.0f);
        }
        TestEqual(TEXT("a disengaged drive does not wind"), Ship->GetJumpCharge(), Held);
        TestTrue(TEXT("and does not decay"), Held > 0.0f);
        TestEqual(TEXT("and wants nothing"), Ship->GetConsumerWant(ShipPower::Engine), 0.0f);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
