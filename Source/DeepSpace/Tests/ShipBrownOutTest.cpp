#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLightingSubsystem.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipBrownOutTest,
    "DeepSpace.Ship.BrownOutRunsWarmer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A lamp browning out gets redder than it was, whatever it was rated at.
 *
 * The ship's lamps burn at their rooms' kelvin (Tools/hauler_layout.py,
 * ROOM_MOOD), from a 2700 K bunk to a 6200 K airlock. The brown-out once
 * lerped every lamp to one fixed amber, which is bluer than the bunk's own
 * light, so the warmest rooms went cold exactly when they should have gone
 * amber. Nothing else would show it: it needs a thin allocation at runtime.
 */
bool FShipBrownOutTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("BrownOutTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    UShipLightingSubsystem* Lighting = World->GetSubsystem<UShipLightingSubsystem>();

    if (TestNotNull(TEXT("the world has a ship"), Ship)
        && TestNotNull(TEXT("and a lighting subsystem"), Lighting))
    {
        // The bunk's 2700 K, and the airlock's near-white 6200 K, both as
        // sRGB, which is how Tools/build_hauler.py writes a light's colour.
        const FColor Rated[] = {FColor(255, 167, 87), FColor(255, 249, 242)};
        TArray<UPointLightComponent*> Bulbs;
        for (const FColor& Colour : Rated)
        {
            APointLight* Lamp = World->SpawnActor<APointLight>();
            Lamp->Tags.Add(ShipPower::Lights);
            UPointLightComponent* Bulb = Lamp->FindComponentByClass<UPointLightComponent>();
            Bulb->SetMobility(EComponentMobility::Movable);
            Bulb->SetLightColor(FLinearColor(Colour));
            Bulbs.Add(Bulb);
        }
        Lighting->Refresh();

        TArray<FLinearColor> Fed;
        Lighting->Tick(0.016f);
        for (const UPointLightComponent* Bulb : Bulbs)
        {
            Fed.Add(Bulb->GetLightColor());
        }

        Ship->SetConsumerWeight(ShipPower::Lights, 0.0f);
        Lighting->Tick(0.016f);

        for (int32 i = 0; i < Bulbs.Num(); ++i)
        {
            const FLinearColor Was = Fed[i];
            const FLinearColor Now = Bulbs[i]->GetLightColor();
            const FString Which = Rated[i].ToHex();
            TestTrue(FString::Printf(TEXT("%s: fed, it burns at its rating"), *Which),
                     Was.Equals(FLinearColor(Rated[i]), 0.01f));
            TestTrue(FString::Printf(TEXT("%s: starved, blue falls against red"), *Which),
                     Now.B / Now.R < 0.8f * (Was.B / Was.R));
            TestTrue(FString::Printf(TEXT("%s: and green against red"), *Which),
                     Now.G / Now.R < 0.9f * (Was.G / Was.R));
            TestTrue(FString::Printf(TEXT("%s: blue falls furthest, so it goes amber"), *Which),
                     Now.B / Now.G < Was.B / Was.G);
            TestTrue(FString::Printf(TEXT("%s: and never loses its colour altogether"), *Which),
                     Now.G > 0.0f && Now.B > 0.0f);
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
