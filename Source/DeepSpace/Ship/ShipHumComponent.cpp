#include "Ship/ShipHumComponent.h"

#include "Sound/SoundGenerator.h"

namespace
{
    class FSpikeTone final : public ISoundGenerator
    {
    public:
        FSpikeTone(float InSampleRate, TSharedPtr<std::atomic<int64>, ESPMode::ThreadSafe> InRendered)
            : SampleRate(InSampleRate), Rendered(MoveTemp(InRendered)) {}

        virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
        {
            for (int32 i = 0; i < NumSamples; ++i)
            {
                OutAudio[i] = 0.05f * FMath::Sin(Phase);
                Phase += 2.0f * PI * 110.0f / SampleRate;
                if (Phase > 2.0f * PI) { Phase -= 2.0f * PI; }
            }
            Rendered->fetch_add(NumSamples);
            return NumSamples;
        }

    private:
        float SampleRate;
        float Phase = 0.0f;
        TSharedPtr<std::atomic<int64>, ESPMode::ThreadSafe> Rendered;
    };
}

UShipHumComponent::UShipHumComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
    , Rendered(MakeShared<std::atomic<int64>, ESPMode::ThreadSafe>(0))
{
    NumChannels = 1;
}

int64 UShipHumComponent::GetSamplesRendered() const
{
    return Rendered->load();
}

bool UShipHumComponent::Init(int32& SampleRate)
{
    NumChannels = 1;
    return true;
}

ISoundGeneratorPtr UShipHumComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
    UE_LOG(LogTemp, Display, TEXT("HUMSPIKE: generator created at %.0f Hz, %d channels"), InParams.SampleRate, InParams.NumChannels);
    return ISoundGeneratorPtr(new FSpikeTone(InParams.SampleRate, Rendered));
}
