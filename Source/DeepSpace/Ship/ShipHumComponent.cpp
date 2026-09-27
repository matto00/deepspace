#include "Ship/ShipHumComponent.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundGenerator.h"

namespace
{
    // Read where they are used and never cached, like every tunable a
    // playtest moves: Linux has no Live Coding.

    TAutoConsoleVariable<float> CVarHumVolume(
        TEXT("ds.Hum.Volume"), 1.0f,
        TEXT("The ship's hum, all of it, as a multiple of its designed level. The first thing to turn down if it wears."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarCruiseHiss(
        TEXT("ds.Hum.CruiseHiss"), 0.35f,
        TEXT("How much of the boosters' hiss holding a cruise keeps, at full throttle (0: the hiss only while the speed changes)."),
        ECVF_Default);

    /** The voice on the audio thread. Owns its FShipHumVoice outright; the
     *  only thing it shares with the component is the mailbox. */
    class FShipHumGenerator final : public ISoundGenerator
    {
    public:
        FShipHumGenerator(float SampleRate, uint32 Seed, const FShipHumVoice::FSettings& Settings,
                          TSharedRef<FShipHumMailbox, ESPMode::ThreadSafe> InMailbox)
            : Voice(SampleRate, Seed, Settings)
            , Mailbox(MoveTemp(InMailbox))
        {
        }

        virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
        {
            FShipHumInputs Inputs;
            Inputs.EngineFeed = Mailbox->EngineFeed.load(std::memory_order_relaxed);
            Inputs.Push = Mailbox->Push.load(std::memory_order_relaxed);
            Voice.SetTargets(Inputs);
            Voice.Render(OutAudio, NumSamples);
            Mailbox->SamplesRendered.fetch_add(NumSamples, std::memory_order_relaxed);
            return NumSamples;
        }

    private:
        FShipHumVoice Voice;
        TSharedRef<FShipHumMailbox, ESPMode::ThreadSafe> Mailbox;
    };
}

UShipHumComponent::UShipHumComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , Mailbox(MakeShared<FShipHumMailbox, ESPMode::ThreadSafe>())
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    NumChannels = 1;
    bAllowSpatialization = true;
    bOverrideAttenuation = true;
    SetKind(EShipHumKind::Reactor);
}

void UShipHumComponent::SetKind(EShipHumKind NewKind)
{
    Kind = NewKind;

    // Linear falloff from a small inner sphere: a machine heard across a
    // room, not a point that screams at arm's length.
    FSoundAttenuationSettings& Settings = AttenuationOverrides;
    Settings.bAttenuate = true;
    Settings.bSpatialize = true;
    Settings.AttenuationShape = EAttenuationShape::Sphere;
    Settings.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
    Settings.AttenuationShapeExtents = FVector(100.0f, 0.0f, 0.0f);
    Settings.FalloffDistance = Kind == EShipHumKind::Reactor ? ReactorFalloff : AirFalloff;
}

EShipHumKind UShipHumComponent::GetKind() const
{
    return Kind;
}

FShipHumInputs UShipHumComponent::AskShip(const UShipSubsystem& Ship)
{
    FShipHumInputs Inputs;
    Inputs.EngineFeed = ShipHum::EngineFeed(Ship.GetConsumerShare(ShipPower::Engine), UShipSubsystem::GetWindingWant());

    // Rated, not current: the subsystem's GetLinearAcceleration is already
    // the rating scaled by the boosters' allocation, which is the fraction
    // the hiss thins by.
    const float Rated = static_cast<float>(FShipFlightLimits::Cruise().LinearAcceleration);
    const FShipFlightState& Flight = Ship.GetFlightState();

    // The live lever's travel, 0..1: how hard the ship is being asked to go.
    // Under the drive it is where the ship has eased to along the notches,
    // not the notch it is heading for -- the drive reports no acceleration,
    // so this is the whole of its hiss, and it should swell as the ship
    // spools up, not jump at the tap. Spooling down, it is the larger of the
    // drive's fading travel and cruise's lever, live from the press of F: a
    // ship still easing down from 1 c is not silent because cruise's lever
    // is at STOP, and the two meet as the spool hands the ship to cruise.
    const int32 LastNotch = Flight.GetDriveNotchCount() - 1;
    const double Travel = LastNotch > 0 ? FMath::Clamp(Flight.GetDrivePosition() / LastNotch, 0.0, 1.0) : 0.0;
    const double Cruising = FMath::Abs(Flight.GetCommand().Throttle);
    const EFlightMode Mode = Flight.GetMode();
    const double Lever = Mode == EFlightMode::Drive ? Travel
        : Mode == EFlightMode::SpoolingDown ? FMath::Max(Travel, Cruising)
        : Cruising;
    Inputs.Push = ShipHum::Push(static_cast<float>(Flight.GetLinearAcceleration().Size()), Rated,
                                static_cast<float>(Lever),
                                Rated > 0.0f ? Ship.GetLinearAcceleration() / Rated : 0.0f,
                                CVarCruiseHiss.GetValueOnGameThread());
    return Inputs;
}

FShipHumInputs UShipHumComponent::GetPostedInputs() const
{
    FShipHumInputs Inputs;
    Inputs.EngineFeed = Mailbox->EngineFeed.load(std::memory_order_relaxed);
    Inputs.Push = Mailbox->Push.load(std::memory_order_relaxed);
    return Inputs;
}

uint32 UShipHumComponent::GetSeed() const
{
    return Seed;
}

int64 UShipHumComponent::GetSamplesRendered() const
{
    return Mailbox->SamplesRendered.load(std::memory_order_relaxed);
}

void UShipHumComponent::BeginPlay()
{
    const FVector Where = GetComponentLocation();
    Seed = HashCombine(GetTypeHash(FIntVector(FMath::RoundToInt(Where.X), FMath::RoundToInt(Where.Y),
                                              FMath::RoundToInt(Where.Z))),
                       static_cast<uint32>(Kind));

    Super::BeginPlay();
    Start();
}

void UShipHumComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                      FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    const float Volume = FMath::Max(0.0f, CVarHumVolume.GetValueOnGameThread());
    if (Volume != AppliedVolume && GetAudioComponent())
    {
        SetVolumeMultiplier(Volume);
        AppliedVolume = Volume;
    }

    // The air follows nothing, so it asks nothing.
    if (Kind != EShipHumKind::Reactor)
    {
        return;
    }
    if (const UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        const FShipHumInputs Inputs = AskShip(*Ship);
        Mailbox->EngineFeed.store(Inputs.EngineFeed, std::memory_order_relaxed);
        Mailbox->Push.store(Inputs.Push, std::memory_order_relaxed);
    }
}

bool UShipHumComponent::Init(int32& SampleRate)
{
    NumChannels = 1;
    return true;
}

ISoundGeneratorPtr UShipHumComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
    const FShipHumVoice::FSettings Settings =
        Kind == EShipHumKind::Reactor ? FShipHumVoice::Reactor() : FShipHumVoice::Air();
    return ISoundGeneratorPtr(new FShipHumGenerator(InParams.SampleRate, Seed, Settings, Mailbox));
}
