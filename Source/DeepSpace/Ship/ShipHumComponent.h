#pragma once

#include "Components/SynthComponent.h"
#include "CoreMinimal.h"
#include "Ship/ShipHumVoice.h"
#include <atomic>
#include "ShipHumComponent.generated.h"

class UShipSubsystem;

/** Which voice a hum source speaks with (lived-in decision 9). */
UENUM(BlueprintType)
enum class EShipHumKind : uint8
{
    /** The whole voice, at the reactor: the drone follows the watts reaching
     *  the jump drive, the hiss follows the boosters. */
    Reactor,

    /** A room's air handling: quiet noise that never changes. */
    Air,
};

/**
 * The only thing the game thread and the audio thread share: this frame's
 * answer, and a count the tests read. Not ship state -- overwritten every
 * tick from what the ship answers, and never read back by gameplay.
 */
struct FShipHumMailbox
{
    std::atomic<float> EngineFeed{0.0f};
    std::atomic<float> Push{0.0f};
    std::atomic<int64> SamplesRendered{0};
};

/**
 * The ship's hum: an FShipHumVoice rendered on the audio thread, spatialised
 * at a point in the ship.
 *
 * On the game thread it asks UShipSubsystem, every tick, for the two things
 * the voice follows, and posts them to the audio thread; it keeps no copy of
 * the ship (ADR 0003). The voice itself lives in an ISoundGenerator on the
 * audio thread, so no UObject is touched there.
 *
 * It plays from BeginPlay until the world ends, and only in play: an editor
 * world never begins play, so the level is silent while it is being built.
 *
 * Headless, the mixer is real -- SDL over PulseAudio, on this machine the
 * actual output device -- so the tests prove samples are pulled. Whether it
 * sounds right is a playtest question, the same split the screen pointer
 * makes (lived-in *Risks*).
 */
UCLASS(ClassGroup = (Ship), meta = (BlueprintSpawnableComponent))
class DEEPSPACE_API UShipHumComponent : public USynthComponent
{
    GENERATED_BODY()

public:
    UShipHumComponent(const FObjectInitializer& ObjectInitializer);

    /** Which voice, and how far it carries. Before play begins: the voice
     *  and the attenuation are fixed when the synth starts. */
    void SetKind(EShipHumKind NewKind);
    EShipHumKind GetKind() const;

    /** What the voice follows, asked of the ship now. Static and public so a
     *  test asks exactly what the tick asks. */
    static FShipHumInputs AskShip(const UShipSubsystem& Ship);

    /** What was last posted to the audio thread. */
    FShipHumInputs GetPostedInputs() const;

    /** The seed its noise was started with, fixed at BeginPlay. */
    uint32 GetSeed() const;

    /** Samples the mixer has pulled from this hum, all told. */
    int64 GetSamplesRendered() const;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    /** Distance, cm, at which each voice has faded to nothing. The reactor
     *  carries through engineering and into the corridor; a room's air is
     *  heard in that room. */
    static constexpr float ReactorFalloff = 1500.0f;
    static constexpr float AirFalloff = 600.0f;

protected:
    virtual void BeginPlay() override;
    virtual bool Init(int32& SampleRate) override;
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
    EShipHumKind Kind = EShipHumKind::Reactor;

    /** The noise's seed, from where the source stands, so two air sources
     *  never hiss in step -- identical noise from two points combs into a
     *  whistle as the player walks between them. */
    uint32 Seed = 1;

    TSharedRef<FShipHumMailbox, ESPMode::ThreadSafe> Mailbox;

    /** The volume last handed to the audio component, so an unchanged
     *  ds.Hum.Volume sends nothing across. */
    float AppliedVolume = -1.0f;
};
