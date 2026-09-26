#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipHumComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHumSpikeTest, "DeepSpace.Spike.HumTone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHumSpikeTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("HumSpikeWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.AudioDeviceID = GEngine->GetMainAudioDeviceID();
    Context.SetCurrentWorld(World);

    FAudioDevice* Device = World->GetAudioDeviceRaw();
    AddInfo(FString::Printf(TEXT("device: %s, rate %d"), Device ? TEXT("yes") : TEXT("NO"), Device ? Device->SampleRate : 0));

    AActor* Owner = World->SpawnActor<AActor>();
    UShipHumComponent* Hum = NewObject<UShipHumComponent>(Owner);
    Owner->SetRootComponent(Hum);
    Hum->bAllowSpatialization = false;
    Hum->RegisterComponent();
    AddInfo(FString::Printf(TEXT("allow %d, ac %p registered %d, synth device %p"), World->bAllowAudioPlayback, Hum->GetAudioComponent(), Hum->IsRegistered(), World->GetAudioDeviceRaw()));
    Hum->Start();
    UAudioComponent* AC = Hum->GetAudioComponent();
    AddInfo(FString::Printf(TEXT("ac sound %s, ac active %d, ac playing %d, state %d"), AC && AC->Sound ? *AC->Sound->GetName() : TEXT("none"), AC ? AC->IsActive() : -1, AC ? AC->IsPlaying() : -1, AC ? (int)AC->GetPlayState() : -1));
    AddInfo(FString::Printf(TEXT("active %d playing %d"), Hum->IsActive(), Hum->IsPlaying()));

    const double Until = FPlatformTime::Seconds() + 1.5;
    while (FPlatformTime::Seconds() < Until && Hum->GetSamplesRendered() < 48000)
    {
        if (Device) { Device->Update(true); }
        FPlatformProcess::Sleep(0.01f);
    }
    AddInfo(FString::Printf(TEXT("rendered %lld samples"), Hum->GetSamplesRendered()));
    TestTrue(TEXT("the mixer pulled samples"), Hum->GetSamplesRendered() > 0);

    Hum->Stop();
    if (Device) { Device->Update(true); }
    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
