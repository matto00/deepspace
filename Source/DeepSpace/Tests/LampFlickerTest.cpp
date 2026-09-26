#include "Components/PointLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLightingSubsystem.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLampFlickerAfterDaysTest,
    "DeepSpace.Ship.FlickerAfterDays",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A starved light still flickers the way it did on the first evening after
 * eleven days of play. The flicker is a function of a phase that advances
 * with time; left to grow, a float phase at a million seconds cannot take a
 * sixtieth of a second at all, and the brown-out that should read as a
 * fixture running unwell freezes into a plain dim light.
 *
 * Measured the way the eye would: over two seconds at sixty frames a
 * second, the light must change every frame, swing as far as a starved
 * light does, and never jump further in one frame than the wobble's own
 * rates allow.
 */
bool FLampFlickerAfterDaysTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("LampFlickerTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    APointLight* Lamp = World->SpawnActor<APointLight>();
    Lamp->Tags.Add(ShipPower::Lights);
    UPointLightComponent* Bulb = Lamp->FindComponentByClass<UPointLightComponent>();
    Bulb->SetMobility(EComponentMobility::Movable);
    const float Rated = Bulb->Intensity;

    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    World->GetWorldSettings()->NotifyBeginPlay();

    const auto TearDown = [World]()
    {
        World->EndPlay(EEndPlayReason::RemovedFromWorld);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    UShipLightingSubsystem* Lighting = World->GetSubsystem<UShipLightingSubsystem>();
    if (!TestNotNull(TEXT("the world has a ship"), Ship) || !TestNotNull(TEXT("and a lighting subsystem"), Lighting)
        || !TestEqual(TEXT("which finds the one lamp"), Lighting->GetLightCount(), 1))
    {
        TearDown();
        return false;
    }

    // Starved, the flicker is at its deepest.
    Ship->SetConsumerWeight(ShipPower::Lights, 0.0f);

    // Eleven and a half days, a thousand seconds at a time.
    for (int32 Step = 0; Step < 1000; ++Step)
    {
        Lighting->Tick(1000.0f);
    }

    constexpr float Frame = 1.0f / 60.0f;
    int32 Unchanged = 0;
    float Dimmest = TNumericLimits<float>::Max();
    float Brightest = 0.0f;
    float Steepest = 0.0f;
    Lighting->Tick(Frame);
    float Previous = Bulb->Intensity / Rated;
    for (int32 Step = 0; Step < 120; ++Step)
    {
        Lighting->Tick(Frame);
        const float Now = Bulb->Intensity / Rated;
        Unchanged += Now == Previous ? 1 : 0;
        Steepest = FMath::Max(Steepest, FMath::Abs(Now - Previous));
        Dimmest = FMath::Min(Dimmest, Now);
        Brightest = FMath::Max(Brightest, Now);
        Previous = Now;
    }

    // The steepest a frame can move: a starved light is StarvedGlow of its
    // rating, wobbling by 22% of that at no more than the sum of the two
    // rates, 50.7 radians a second.
    const float Allowed = UShipLightingSubsystem::StarvedGlow * 0.22f * (37.0f + 13.7f) * Frame;

    TestEqual(TEXT("after eleven days, the starved light still moves every frame"), Unchanged, 0);
    TestTrue(FString::Printf(TEXT("and swings as far as it did on the first day (%.4f to %.4f of its rating)"),
                             Dimmest, Brightest),
             Dimmest > 0.0f && Brightest / Dimmest > 1.3f);
    TestTrue(FString::Printf(TEXT("smoothly: no frame jumps further than the wobble can move (%.5f, at most %.5f)"),
                             Steepest, Allowed),
             Steepest <= 1.05f * Allowed);

    TearDown();
    return true;
}

#endif
