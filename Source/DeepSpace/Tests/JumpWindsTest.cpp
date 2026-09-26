#include "Core/DeepSpaceGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

// The jump's wind-up measured on the ship the game actually flies: its real
// reactor and its real starting modules, not the bare reactor every other
// jump test runs on. That gap is how ds.Nav.ChargeSeconds came to be a number
// no player could reach -- the engine wanted 800 W while the stock hauler had
// 380 spare, so the fastest any split managed was 48% fed, and at the default
// split a jump took four and a half minutes. Every test checked the charging
// formula; none checked the ship could run it at full speed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FJumpWindsTest,
    "DeepSpace.Ship.JumpCanWindAtFullSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** The loadout the game starts with: the Blueprint game mode's, which is
     *  what play uses and may override the C++ list, falling back to C++. */
    TArray<UShipModuleDataAsset*> StartingModules()
    {
        const UClass* ModeClass = LoadClass<ADeepSpaceGameMode>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceGameMode.BP_DeepSpaceGameMode_C"));
        const ADeepSpaceGameMode* Mode = ModeClass
            ? ModeClass->GetDefaultObject<ADeepSpaceGameMode>()
            : GetDefault<ADeepSpaceGameMode>();

        TArray<UShipModuleDataAsset*> Modules;
        for (const TSoftObjectPtr<UShipModuleDataAsset>& Soft : Mode->GetStartingModules())
        {
            if (UShipModuleDataAsset* Module = Soft.LoadSynchronous())
            {
                Modules.Add(Module);
            }
        }
        return Modules;
    }

    /** Seconds to wind from cold at the current split, ticking as the game does. */
    double SecondsToWind(UShipSubsystem* Ship, double Limit)
    {
        constexpr double Step = 0.25;
        double Elapsed = 0.0;
        while (Elapsed < Limit && Ship->GetJumpCharge() < 1.0f)
        {
            Ship->Tick(static_cast<float>(Step));
            Elapsed += Step;
        }
        return Elapsed;
    }
}

bool FJumpWindsTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("JumpWindsWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    const TArray<UShipModuleDataAsset*> Modules = StartingModules();
    if (!TestNotNull(TEXT("the world has a ship"), Ship)
        || !TestTrue(TEXT("the game starts with modules installed"), Modules.Num() > 0))
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    double ModuleDraw = 0.0;
    for (UShipModuleDataAsset* Module : Modules)
    {
        TestTrue(FString::Printf(TEXT("%s installs"), *Module->GetName()), Ship->InstallModule(Module));
        ModuleDraw += Module->PowerDraw;
    }

    // The invariant itself. If a module is added, the reactor shrinks or the
    // want grows, this is the line that fails -- before a playtest has to.
    const double Spare = Ship->GetReactorOutput() - ModuleDraw;
    AddInfo(FString::Printf(TEXT("reactor %.0f W, modules %.0f W, spare %.0f W, winding wants %.0f W"),
                            Ship->GetReactorOutput(), ModuleDraw, Spare, UShipSubsystem::GetWindingWant()));
    TestTrue(TEXT("the stock ship has the watts to wind at full speed"),
             Spare >= UShipSubsystem::GetWindingWant());

    // A course, and the nose pointed away from it so the charged jump waits
    // rather than firing and spending what is being measured.
    {
        FOutputDeviceNull Quiet;
        if (IConsoleObject* Plot = IConsoleManager::Get().FindConsoleObject(TEXT("ds.Nav.Plot")))
        {
            Plot->AsCommand()->Execute({ TEXT("0") }, World, Quiet);
        }
    }
    const TOptional<FVector> CourseDir = Ship->GetCourseDirection();
    if (!TestTrue(TEXT("a course is plotted"), CourseDir.IsSet()))
    {
        World->EndPlay(EEndPlayReason::Quit);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(),
                    FRotationMatrix::MakeFromX(-*CourseDir).ToQuat());

    const IConsoleVariable* ChargeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Nav.ChargeSeconds"));
    const double ChargeSeconds = ChargeVar ? ChargeVar->GetFloat() : FShipFlightState::JumpChargeSeconds;

    // Everything to the engine: the advertised time must be the real one.
    Ship->SetConsumerWeight(ShipPower::Lights, 0.0f);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    Ship->SetConsumerWeight(ShipPower::Engine, 1.0f);
    TestTrue(TEXT("the jump engages"), Ship->SetJumpEngaged(true));
    Ship->Tick(0.01f);
    TestEqual(TEXT("engine-first, the engine is fully fed while it winds"),
              Ship->GetConsumerSatisfaction(ShipPower::Engine), 1.0f, 1e-3f);
    const double Fastest = SecondsToWind(Ship, 20.0 * ChargeSeconds);
    AddInfo(FString::Printf(TEXT("engine-first: %.1f s (ds.Nav.ChargeSeconds %.1f)"), Fastest, ChargeSeconds));
    TestTrue(TEXT("engine-first, it winds in ds.Nav.ChargeSeconds"),
             FMath::Abs(Fastest - ChargeSeconds) <= 0.5);
    TestEqual(TEXT("and is then ready, holding because it is not aimed"),
              static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Ready));

    // The default split: slower, because the split matters -- but bounded, so
    // it never becomes a thing the player sits out (the anti-chore principle).
    // Two and a half times the fully-fed time is the most it may take.
    // Wind again from cold, on a fresh ship with the same loadout, rather
    // than jumping the charge off: a jump is not what this measures.
    World->EndPlay(EEndPlayReason::Quit);
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);

    UWorld* Fresh = UWorld::CreateWorld(EWorldType::Game, false, TEXT("JumpWindsDefaultSplitWorld"));
    FWorldContext& FreshContext = GEngine->CreateNewWorldContext(EWorldType::Game);
    FreshContext.SetCurrentWorld(Fresh);
    Fresh->InitializeActorsForPlay(FURL());
    Fresh->BeginPlay();
    UShipSubsystem* Default = Fresh->GetSubsystem<UShipSubsystem>();
    for (UShipModuleDataAsset* Module : Modules)
    {
        Default->InstallModule(Module);
    }
    {
        FOutputDeviceNull Quiet;
        if (IConsoleObject* Plot = IConsoleManager::Get().FindConsoleObject(TEXT("ds.Nav.Plot")))
        {
            Plot->AsCommand()->Execute({ TEXT("0") }, Fresh, Quiet);
        }
    }
    if (const TOptional<FVector> Dir = Default->GetCourseDirection())
    {
        Default->PlaceShip(Default->GetFlightState().GetUniversePosition(),
                           FRotationMatrix::MakeFromX(-*Dir).ToQuat());
    }
    // What the split does to the lights, on the ship the game flies. At rest
    // the stock ship is whole -- the reactor is sized for it (developer's
    // ruling) -- and engaging the jump splits what is left three ways, so the
    // lights dim while it winds: the ship straining, never failing.
    Default->Tick(0.01f);
    const float LightsIdle = Default->GetConsumerSatisfaction(ShipPower::Lights);
    TestEqual(TEXT("at rest at the default split, the lights are whole"), LightsIdle, 1.0f, 1e-4f);
    TestEqual(TEXT("and so are the boosters"), Default->GetConsumerSatisfaction(ShipPower::Boosters), 1.0f, 1e-4f);
    TestTrue(TEXT("the jump engages at the default split"), Default->SetJumpEngaged(true));
    Default->Tick(0.01f);
    const float LightsWinding = Default->GetConsumerSatisfaction(ShipPower::Lights);
    AddInfo(FString::Printf(TEXT("lights fed at 1:1:1: %.2f idle, %.2f while winding"), LightsIdle, LightsWinding));
    TestTrue(TEXT("winding at the default split dims the lights"), LightsWinding < LightsIdle);
    TestTrue(TEXT("but never puts them out"), LightsWinding > 0.0f);
    Default->SetConsumerWeight(ShipPower::Engine, 4.0f);
    Default->Tick(0.01f);
    TestTrue(TEXT("leaning on the engine dims them further"),
             Default->GetConsumerSatisfaction(ShipPower::Lights) < LightsWinding);
    Default->SetConsumerWeight(ShipPower::Engine, 1.0f);
    Default->Tick(0.01f);
    const double AtDefault = SecondsToWind(Default, 20.0 * ChargeSeconds) + 0.02;
    AddInfo(FString::Printf(TEXT("default 1:1:1 split: %.1f s"), AtDefault));
    TestTrue(TEXT("at the default split it takes longer than engine-first"), AtDefault > Fastest + 1.0);
    TestTrue(TEXT("but never more than two and a half times ds.Nav.ChargeSeconds"),
             AtDefault <= 2.5 * ChargeSeconds);

    Fresh->EndPlay(EEndPlayReason::Quit);
    GEngine->DestroyWorldContext(Fresh);
    Fresh->DestroyWorld(false);
    return true;
}

#endif
