#include "Tests/StockShip.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipHumComponent.h"
#include "Ship/ShipHumSource.h"
#include "Ship/ShipHumVoice.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Universe/StarSystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHumComponentTest,
    "DeepSpace.Ship.HumComponent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** A console variable moved for one scope, then put back. */
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

    float CVarFloat(const TCHAR* Name)
    {
        const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        return Variable ? Variable->GetFloat() : -1.0f;
    }

    /**
     * A game world that can make sound. UWorld::CreateWorld gives a world no
     * audio device handle, and USynthComponent reads the world's handle
     * directly -- it does not fall back to the main device as UWorld does --
     * so without the id on the context a hum creates nothing, logs nothing,
     * and never plays.
     */
    UWorld* MakeWorld(const TCHAR* Name)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.AudioDeviceID = GEngine->GetMainAudioDeviceID();
        Context.SetCurrentWorld(World);
        return World;
    }

    /**
     * Play ends before the world goes, as it does in the game: EndPlay stops
     * every hum, and the flush waits on the audio thread until the mixer
     * holds nothing of this world. Destroying a world whose synths are still
     * live left the audio thread rendering into freed components, and the
     * editor process sometimes died there with no test result at all.
     */
    void DestroyWorld(UWorld* World)
    {
        World->EndPlay(EEndPlayReason::RemovedFromWorld);
        if (FAudioDevice* Device = World->GetAudioDeviceRaw())
        {
            Device->Flush(World);
            Device->Update(true);
        }
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /** Spawned before play begins, with its Kind set the way the level sets
     *  it: before PostInitializeComponents. */
    AShipHumSource* SpawnSource(UWorld* World, EShipHumKind Kind, const FVector& Where)
    {
        AShipHumSource* Source = World->SpawnActorDeferred<AShipHumSource>(
            AShipHumSource::StaticClass(), FTransform(Where));
        if (Source)
        {
            Source->Kind = Kind;
            Source->FinishSpawning(FTransform(Where));
        }
        return Source;
    }

    bool Near(float A, float B)
    {
        return FMath::IsNearlyEqual(A, B, 1e-4f);
    }
}

/**
 * The hum in a world: what it asks the ship, what it posts to the audio
 * thread, and that the mixer really pulls it.
 *
 * The feed follows the winding and only the winding (plan conflict 8): it is
 * 0 with the jump idle, the delivered watts over ds.Nav.WindingWant while it
 * winds, and 0 again once charged, so the drone settles back as the jump
 * waits to align. The push follows the boosters.
 */
bool FHumComponentTest::RunTest(const FString& Parameters)
{
    UWorld* World = MakeWorld(TEXT("HumComponentTestWorld"));

    AShipHumSource* Reactor = SpawnSource(World, EShipHumKind::Reactor, FVector(600.0, 280.0, 120.0));
    AShipHumSource* Air = SpawnSource(World, EShipHumKind::Air, FVector(1055.0, 280.0, 230.0));
    // A second room's air: the galley's and the bunk's, at their ceilings.
    AShipHumSource* OtherAir = SpawnSource(World, EShipHumKind::Air, FVector(800.0, -240.0, 230.0));

    // A test world has no game mode, and UWorld::BeginPlay reaches actors
    // only through one; the last call is the one its game state would make.
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    World->GetWorldSettings()->NotifyBeginPlay();

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    if (Ship)
    {
        TestTrue(TEXT("the stock loadout installs"), StockShip::Install(Ship) > 0);
    }
    if (!TestNotNull(TEXT("the world has a ship"), Ship)
        || !TestNotNull(TEXT("a reactor hum"), Reactor) || !TestNotNull(TEXT("an air hum"), Air)
        || !TestNotNull(TEXT("and another room's"), OtherAir))
    {
        DestroyWorld(World);
        return false;
    }
    UShipHumComponent* ReactorHum = Reactor->GetHum();
    UShipHumComponent* AirHum = Air->GetHum();

    // -- the source hands its Kind to its component, before play ------------
    TestTrue(TEXT("the hum is the source's root"), Reactor->GetRootComponent() == ReactorHum);
    TestEqual(TEXT("a reactor source speaks as the reactor"),
              static_cast<int32>(ReactorHum->GetKind()), static_cast<int32>(EShipHumKind::Reactor));
    TestEqual(TEXT("an air source as air"), static_cast<int32>(AirHum->GetKind()), static_cast<int32>(EShipHumKind::Air));
    TestEqual(TEXT("the reactor carries 15 m"), ReactorHum->AttenuationOverrides.FalloffDistance, UShipHumComponent::ReactorFalloff);
    TestEqual(TEXT("a room's air, 6 m"), AirHum->AttenuationOverrides.FalloffDistance, UShipHumComponent::AirFalloff);
    TestTrue(TEXT("both are spatialised"), ReactorHum->bAllowSpatialization && AirHum->AttenuationOverrides.bSpatialize);

    // -- every source hisses its own noise --------------------------------------
    //
    // Two air sources on one seed play the same noise from two points, and
    // walking between them combs it into a whistle. HumVoice proves a voice
    // honours its seed; this proves placed sources are given different ones.
    const uint32 AirSeed = AirHum->GetSeed();
    const uint32 OtherAirSeed = OtherAir->GetHum()->GetSeed();
    TestNotEqual(TEXT("two rooms' air never share a seed"), AirSeed, OtherAirSeed);
    TestNotEqual(TEXT("nor the air and the reactor"), AirSeed, ReactorHum->GetSeed());
    TestNotEqual(TEXT("nor the other room's and the reactor"), OtherAirSeed, ReactorHum->GetSeed());

    // -- the feed follows the winding, and nothing else -----------------------
    const auto Tick = [&](float Seconds)
    {
        Ship->Tick(Seconds);
        ReactorHum->TickComponent(Seconds, LEVELTICK_All, nullptr);
        AirHum->TickComponent(Seconds, LEVELTICK_All, nullptr);
        OtherAir->GetHum()->TickComponent(Seconds, LEVELTICK_All, nullptr);
    };

    Tick(0.1f);
    TestEqual(TEXT("idle, the jump draws nothing and the drone sits low"), UShipHumComponent::AskShip(*Ship).EngineFeed, 0.0f);
    TestEqual(TEXT("though an idle engine, wanting nothing, reads fully satisfied -- why the feed is watts"),
              Ship->GetConsumerSatisfaction(ShipPower::Engine), 1.0f);

    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    const TOptional<FVector> Course = Chart.Num() > 0 && Ship->PlotCourse(Chart[0].Id)
        ? Ship->GetCourseDirection() : TOptional<FVector>();
    if (!TestTrue(TEXT("a course can be plotted"), Course.IsSet()))
    {
        DestroyWorld(World);
        return false;
    }
    // Facing away, so a charged jump waits rather than firing.
    Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(-*Course).ToQuat());
    Ship->SetJumpEngaged(true);
    Tick(0.1f);
    TestEqual(TEXT("engaged, it winds"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Winding));

    const float WindingWant = CVarFloat(TEXT("ds.Nav.WindingWant"));
    const float Share = Ship->GetConsumerShare(ShipPower::Engine);
    const float Winding = UShipHumComponent::AskShip(*Ship).EngineFeed;
    TestTrue(FString::Printf(TEXT("winding, the drone rises (feed %.3f)"), Winding), Winding > 0.0f);
    TestTrue(TEXT("by the watts delivered over ds.Nav.WindingWant"), Near(Winding, Share / WindingWant));
    TestTrue(TEXT("and the tick posts exactly that to the audio thread"),
             Near(ReactorHum->GetPostedInputs().EngineFeed, Winding));
    TestEqual(TEXT("the air asks nothing and posts nothing"), AirHum->GetPostedInputs().EngineFeed, 0.0f);

    {
        // Leaning the split on the engine feeds the winding more, and the
        // drone brightens with it.
        Ship->SetConsumerWeight(ShipPower::Engine, 4.0f);
        Tick(0.1f);
        TestTrue(TEXT("weight moved to the engine raises the feed"), UShipHumComponent::AskShip(*Ship).EngineFeed > Winding);
        Ship->SetConsumerWeight(ShipPower::Engine, 1.0f);
        Tick(0.1f);
    }
    {
        // Nav's want is asked by name each time, never cached.
        FScopedCVar Want(TEXT("ds.Nav.WindingWant"), 2.0f * WindingWant);
        Tick(0.1f);
        const float Now = UShipHumComponent::AskShip(*Ship).EngineFeed;
        TestTrue(TEXT("the feed is measured against ds.Nav.WindingWant as tuned now"),
                 Near(Now, Ship->GetConsumerShare(ShipPower::Engine) / (2.0f * WindingWant)));
    }
    Tick(0.1f);

    {
        FScopedCVar Quick(TEXT("ds.Nav.ChargeSeconds"), 0.5f);
        for (int32 Step = 0; Step < 100 && Ship->GetJumpState() == EJumpState::Winding; ++Step)
        {
            Tick(0.1f);
        }
    }
    // The subsystem sets the engine's want from the jump state at the start
    // of its tick, so the want follows Ready one frame later.
    Tick(0.1f);
    TestEqual(TEXT("charged and unaligned, it waits"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Ready));
    TestEqual(TEXT("and the drone settles back: a charged jump draws nothing"),
              UShipHumComponent::AskShip(*Ship).EngineFeed, 0.0f);
    TestEqual(TEXT("which is what the tick posts"), ReactorHum->GetPostedInputs().EngineFeed, 0.0f);
    Ship->ClearCourse();
    Tick(0.1f);

    // -- the push follows the boosters -----------------------------------------
    APawn* Pilot = World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    const float CruiseHiss = CVarFloat(TEXT("ds.Hum.CruiseHiss"));
    TestTrue(TEXT("at rest the boosters are quiet"), Near(UShipHumComponent::AskShip(*Ship).Push, 0.0f));

    Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
    Tick(0.1f);
    TestTrue(FString::Printf(TEXT("throttle up: full effort while the speed climbs (push %.3f)"), UShipHumComponent::AskShip(*Ship).Push),
             Near(UShipHumComponent::AskShip(*Ship).Push, 1.0f));
    TestTrue(TEXT("posted to the audio thread"), Near(ReactorHum->GetPostedInputs().Push, 1.0f));

    for (int32 Step = 0; Step < 100; ++Step)
    {
        Tick(0.1f);
    }
    TestTrue(FString::Printf(TEXT("cruising, the lever still answers: ds.Hum.CruiseHiss (push %.3f)"), UShipHumComponent::AskShip(*Ship).Push),
             Near(UShipHumComponent::AskShip(*Ship).Push, CruiseHiss));

    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    Tick(0.1f);
    const float Starved = UShipHumComponent::AskShip(*Ship).Push;
    TestTrue(FString::Printf(TEXT("starved boosters hiss thinner at the same throttle (push %.3f)"), Starved),
             Starved > 0.0f && Starved < CruiseHiss);
    Ship->SetConsumerWeight(ShipPower::Boosters, 1.0f);
    Tick(0.1f);

    // Under the drive, which reports no acceleration, the hiss is the live
    // lever's travel: where the ship has eased to along the notches, over
    // the lever's length -- so it swells as the ship spools up, and does not
    // jump at the tap.
    {
        const FShipFlightState& Flight = Ship->GetFlightState();
        const float LastNotch = static_cast<float>(Flight.GetDriveNotchCount() - 1);
        Ship->SetDriveLever(Pilot, Flight.GetDriveNotchCount() - 1);
        Ship->SetDriveEngaged(Pilot, true);
        Tick(0.5f);
        const float Early = UShipHumComponent::AskShip(*Ship).Push;
        TestTrue(FString::Printf(TEXT("half a second after the lever went to 1 c, the hiss has only begun (push %.3f)"), Early),
                 Early > 0.0f && Early < 0.5f * CruiseHiss);
        TestTrue(TEXT("it is ds.Hum.CruiseHiss times the eased position over the lever's length"),
                 Near(Early, CruiseHiss * static_cast<float>(Flight.GetDrivePosition()) / LastNotch));
        for (int32 Step = 0; Step < 100; ++Step)
        {
            Tick(0.1f);
        }
        const float Spooled = UShipHumComponent::AskShip(*Ship).Push;
        TestTrue(FString::Printf(TEXT("spooled up, it has swelled (push %.3f at position %.2f)"), Spooled, Flight.GetDrivePosition()),
                 Spooled > Early && Near(Spooled, CruiseHiss * static_cast<float>(Flight.GetDrivePosition()) / LastNotch));

        // F at 1 c with cruise's lever at STOP: the ship spools down for
        // six seconds, and the hiss fades with it rather than cutting out.
        Ship->SetFlightCommand(Pilot, 0.0f, FVector::ZeroVector);
        Ship->SetDriveEngaged(Pilot, false);
        Tick(1.0f);
        const float Spooling = UShipHumComponent::AskShip(*Ship).Push;
        TestEqual(TEXT("F at 1 c spools down"), static_cast<int32>(Flight.GetMode()), static_cast<int32>(EFlightMode::SpoolingDown));
        TestTrue(FString::Printf(TEXT("spooling down with cruise's lever at STOP, the drive still hisses, fading (push %.3f at position %.2f)"),
                                 Spooling, Flight.GetDrivePosition()),
                 Spooling > 0.0f && Spooling < Spooled
                 && Near(Spooling, CruiseHiss * static_cast<float>(Flight.GetDrivePosition()) / LastNotch));
        Ship->SetDriveEngaged(Pilot, true);
        Ship->AllStop(Pilot);
        for (int32 Step = 0; Step < 150; ++Step)
        {
            Tick(0.1f);
        }
        TestTrue(TEXT("and at STOP, at rest, the drive is quiet"), Near(UShipHumComponent::AskShip(*Ship).Push, 0.0f));
        Ship->SetDriveEngaged(Pilot, false);
        Tick(0.1f);
    }

    // -- ds.Hum.Volume, the way out if the hum wears ---------------------------
    //
    // Lived-in *Risks* names it as the first thing to turn down, so it must
    // reach the sound, and be read each tick rather than once.
    {
        const auto Volume = [](UShipHumComponent* Hum)
        {
            const UAudioComponent* Audio = Hum->GetAudioComponent();
            return Audio ? Audio->VolumeMultiplier : -1.0f;
        };
        {
            FScopedCVar Down(TEXT("ds.Hum.Volume"), 0.25f);
            Tick(0.0f);
            TestTrue(FString::Printf(TEXT("ds.Hum.Volume 0.25 turns the reactor down (%.3f)"), Volume(ReactorHum)),
                     Near(Volume(ReactorHum), 0.25f));
            TestTrue(FString::Printf(TEXT("and the air (%.3f)"), Volume(AirHum)), Near(Volume(AirHum), 0.25f));
        }
        {
            FScopedCVar Further(TEXT("ds.Hum.Volume"), 0.1f);
            Tick(0.0f);
            TestTrue(FString::Printf(TEXT("moved again in play, the hum follows it (%.3f)"), Volume(ReactorHum)),
                     Near(Volume(ReactorHum), 0.1f));
        }
        Tick(0.0f);
        TestTrue(FString::Printf(TEXT("and put back, it is back at its designed level (%.3f)"), Volume(ReactorHum)),
                 Near(Volume(ReactorHum), 1.0f));
    }

    // -- the mixer pulls it ---------------------------------------------------
    //
    // Headless, the audio device is real (SDL over PulseAudio). Turned down
    // so the run is not heard, then the samples the mixer pulled are
    // counted: the component made a generator, the mixer started it, and
    // the audio thread is rendering the voice.
    FAudioDevice* Device = World->GetAudioDeviceRaw();
    if (!Device)
    {
        AddWarning(TEXT("no audio device in this run: the mixer half is unchecked"));
    }
    else
    {
        FScopedCVar Hushed(TEXT("ds.Hum.Volume"), 0.01f);
        Tick(0.0f);
        TestTrue(TEXT("the reactor hum is playing from BeginPlay"), ReactorHum->IsPlaying());
        TestTrue(TEXT("and so is the air"), AirHum->IsPlaying());
        TestTrue(TEXT("hushed for this run"), ReactorHum->GetAudioComponent()
                 && Near(ReactorHum->GetAudioComponent()->VolumeMultiplier, 0.01f));

        const double Until = FPlatformTime::Seconds() + 2.0;
        while (FPlatformTime::Seconds() < Until
               && (ReactorHum->GetSamplesRendered() < 4096 || AirHum->GetSamplesRendered() < 4096))
        {
            Device->Update(true);
            FPlatformProcess::Sleep(0.01f);
        }
        TestTrue(FString::Printf(TEXT("the mixer pulled the reactor's voice (%lld samples)"), ReactorHum->GetSamplesRendered()),
                 ReactorHum->GetSamplesRendered() >= 4096);
        TestTrue(FString::Printf(TEXT("and the air's (%lld samples)"), AirHum->GetSamplesRendered()),
                 AirHum->GetSamplesRendered() >= 4096);
    }

    DestroyWorld(World);
    return true;
}

#endif
