#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "UI/EngineeringConsoleWidget.h"
#include "UI/PowerAllocationWidget.h"
#include "UI/ShipHUDWidget.h"
#include "UI/SystemMapWidget.h"
#include "UI/TargetMarker.h"
#include "GameFramework/Pawn.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/UniverseSubsystem.h"

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

    // The target line: the HUD's readout and the map's band print one
    // string (system map decision 6), because both print the ship's own
    // view of the target and neither composes its own. Checked at rest, at
    // speed on a path that brings the ship down on the world (the live
    // ETA), and with the target let go.
    {
        SkyTestWorld::FSkyWorld Test(TEXT("ScreensAgreeTargetWorld"));
        UShipSubsystem* Aboard = Test.Ship;
        if (TestNotNull(TEXT("the target world has a ship"), Aboard) && TestNotNull(TEXT("and a universe"), Test.Universe))
        {
            Test.BeginPlay();
            APawn* Pilot = Test.World->SpawnActor<APawn>();
            Aboard->SetPilot(Pilot);
            USystemMapWidget* Map = MakeScreen<USystemMapWidget>(Test.World);
            const auto HereNow = [&]() { return Test.Universe->GetSystemAt(Aboard->GetFlightState().GetUniversePosition()); };
            const TOptional<FStarSystem> Home = HereNow();
            const auto Agree = [&](const TCHAR* When)
            {
                Map->RefreshFromShip();
                const FString Hud = UShipHUDWidget::TargetLineText(*Aboard, HereNow()).ToString();
                TestEqual(FString::Printf(TEXT("%s: the HUD and the map print one target line"), When), Hud,
                          Map->GetTargetText().ToString());
                return Hud;
            };

            if (TestTrue(TEXT("the ship starts among worlds"), Home.IsSet() && Home->Planets.Num() > 0))
            {
                TestTrue(TEXT("with no target both lines are empty"), Agree(TEXT("no target")).IsEmpty());
                const int32 Orbit = Home->Planets.Num() - 1;
                TestTrue(TEXT("a world is targeted"), Aboard->SetTarget(FBodyId{ Home->Stub.Id, Orbit, -1 }));
                const FString AtRest = Agree(TEXT("at rest"));
                TestTrue(TEXT("and the line names it"), AtRest.Contains(Home->Planets[Orbit].Designation));
                TestFalse(TEXT("at rest there is no time to arrival"), AtRest.Contains(TEXT("ETA")));

                // At the drive's top, nose on the world: a live ETA.
                const FUniversePosition Start = Aboard->GetFlightState().GetUniversePosition();
                const FVector ToWorld = (Home->PlanetPosition(Orbit) - Start).GetSafeNormal();
                Aboard->PlaceShip(Start, FRotationMatrix::MakeFromX(ToWorld).ToQuat());
                Aboard->SetDriveEngaged(Pilot, true);
                Aboard->SetDriveLever(Pilot, Aboard->GetFlightState().GetDriveNotchCount() - 1);
                for (int32 Tick = 0; Tick < 20; ++Tick)
                {
                    Aboard->Tick(0.05f);
                }
                const FString Flying = Agree(TEXT("flying at it"));
                TestTrue(FString::Printf(TEXT("under way onto it, the line has a live ETA (%s)"), *Flying), Flying.Contains(TEXT("ETA")));
                const TOptional<FTargetView> Before = Aboard->GetTargetView(*HereNow());
                Aboard->Tick(0.5f);
                Agree(TEXT("half a second on"));
                const TOptional<FTargetView> After = Aboard->GetTargetView(*HereNow());
                TestTrue(TEXT("and it is live: half a second on, the time to arrival has fallen"),
                         Before && After && Before->EtaSeconds && After->EtaSeconds && *After->EtaSeconds < *Before->EtaSeconds);

                Aboard->ClearTarget();
                TestTrue(TEXT("let go, both lines are empty"), Agree(TEXT("let go")).IsEmpty());
            }
        }
    }
    return true;
}

#endif
