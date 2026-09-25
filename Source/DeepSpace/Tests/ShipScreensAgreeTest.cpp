#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "UI/EngineeringConsoleWidget.h"
#include "UI/PowerAllocationWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    /**
     * A screen without an actor to hang it on. Widget components build their
     * widget through the game instance, which a hand-made test world has
     * not got, so the widget is made directly -- which is fine, because what
     * is under test is the screen's relationship with the subsystem and not
     * Unreal's plumbing for getting it onto a quad.
     */
    template <typename TWidget>
    TWidget* MakeScreen(UWorld* World)
    {
        TWidget* Widget = NewObject<TWidget>(World);
        Widget->Initialize();
        Widget->TakeWidget();
        return Widget;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipScreensAgreeTest,
    "DeepSpace.Ship.ScreensAgree",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * "Screens are views" is the property most likely to rot: the cheap thing to
 * write is a screen that remembers what it last showed, and the day a second
 * screen shows the same value they start disagreeing. Nothing aboard stores
 * ship state -- everything asks the subsystem -- and this is what says so.
 */
bool FShipScreensAgreeTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ScreensAgreeTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    if (TestNotNull(TEXT("the world has a ship"), Ship))
    {
        // Two laptops, as though the player had one on the galley table and
        // another somewhere else. Neither is special.
        UPowerAllocationWidget* Galley = MakeScreen<UPowerAllocationWidget>(World);
        UPowerAllocationWidget* Elsewhere = MakeScreen<UPowerAllocationWidget>(World);

        if (TestTrue(TEXT("both screens built their rows"),
                     Galley->GetRowText(ShipPower::Engine).ToString().Contains(TEXT("ENGINE"))))
        {
            // Moving a weight on one screen moves the ship, and the other
            // screen follows -- because the slider is a view of the weight
            // and not a second copy of it.
            Galley->SetRowWeight(ShipPower::Engine, 4.0f);
            TestEqual(TEXT("the ship took the new weight"),
                      Ship->GetConsumerWeight(ShipPower::Engine), 4.0f);

            Elsewhere->RefreshFromShip();
            TestEqual(TEXT("the other screen's slider followed"),
                      Elsewhere->GetRowWeight(ShipPower::Engine), 4.0f);
            TestEqual(TEXT("and both screens read the same"),
                      Galley->GetRowText(ShipPower::Engine).ToString(),
                      Elsewhere->GetRowText(ShipPower::Engine).ToString());

            // And the other way round, so neither screen is the authority.
            Elsewhere->SetRowWeight(ShipPower::Engine, 1.0f);
            Galley->RefreshFromShip();
            TestEqual(TEXT("neither screen is in charge"),
                      Galley->GetRowWeight(ShipPower::Engine), 1.0f);
        }

        // Two different *kinds* of screen agree too: the console's switch
        // and the laptop's readout are views of one value.
        UEngineeringConsoleWidget* Console = MakeScreen<UEngineeringConsoleWidget>(World);
        TestTrue(TEXT("the ship starts lit"), Ship->AreLightsOn());

        Console->ToggleLights();
        TestFalse(TEXT("the console's switch reaches the ship"), Ship->AreLightsOn());

        Galley->RefreshFromShip();
        TestTrue(TEXT("and the laptop shows the lights wanting nothing"),
                 Galley->GetRowText(ShipPower::Lights).ToString().Contains(TEXT("0 W of 0 W")));

        // The console's own readout is recomputed, never remembered: the
        // freed power shows up as it is reallocated.
        const FString Lit = Console->GetReadoutText().ToString();
        Console->ToggleLights();
        TestTrue(TEXT("the ship is lit again"), Ship->AreLightsOn());
        TestNotEqual(TEXT("and the readout changed with it"),
                     Console->GetReadoutText().ToString(), Lit);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
