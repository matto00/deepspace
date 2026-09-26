#pragma once

#include "Components/SynthComponent.h"
#include "CoreMinimal.h"
#include <atomic>
#include "ShipHumComponent.generated.h"

/** SPIKE: one sine, to prove a USynthComponent sounds on Linux at all. */
UCLASS(ClassGroup = (Ship), meta = (BlueprintSpawnableComponent))
class DEEPSPACE_API UShipHumComponent : public USynthComponent
{
    GENERATED_BODY()

public:
    UShipHumComponent(const FObjectInitializer& ObjectInitializer);

    /** Samples the audio thread has pulled from this synth, all time. */
    int64 GetSamplesRendered() const;

protected:
    virtual bool Init(int32& SampleRate) override;
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;

private:
    TSharedPtr<std::atomic<int64>, ESPMode::ThreadSafe> Rendered;
};
