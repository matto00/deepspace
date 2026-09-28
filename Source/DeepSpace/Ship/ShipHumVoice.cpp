#include "Ship/ShipHumVoice.h"

namespace
{
    constexpr double TwoPi = 2.0 * UE_DOUBLE_PI;

    double Wrap(double Phase)
    {
        return Phase >= TwoPi ? Phase - TwoPi : Phase;
    }

    /** Coefficient of a one-pole low-pass at CutoffHz. */
    double LowPassCoefficient(double CutoffHz, double SampleRate)
    {
        return 1.0 - FMath::Exp(-TwoPi * CutoffHz / SampleRate);
    }

}

float ShipHum::EngineFeed(float EngineShareWatts, float WindingWantWatts)
{
    if (WindingWantWatts <= 0.0f)
    {
        return 0.0f;
    }
    return FMath::Clamp(EngineShareWatts / WindingWantWatts, 0.0f, 1.0f);
}

float ShipHum::Push(float AccelerationCmS2, float RatedAccelerationCmS2,
                    float Throttle, float ThrustFraction, float CruiseHiss)
{
    const float Changing = RatedAccelerationCmS2 > 0.0f
        ? FMath::Abs(AccelerationCmS2) / RatedAccelerationCmS2
        : 0.0f;
    const float Holding = FMath::Max(0.0f, CruiseHiss) * FMath::Abs(Throttle)
        * FMath::Clamp(ThrustFraction, 0.0f, 1.0f);
    return FMath::Clamp(FMath::Max(Changing, Holding), 0.0f, 1.0f);
}

float ShipHum::Push(float AccelerationCmS2, float RatedAccelerationCmS2,
                    float Throttle, float ThrustFraction, float CruiseHiss, float Holding)
{
    return FMath::Clamp(FMath::Max(Push(AccelerationCmS2, RatedAccelerationCmS2, Throttle, ThrustFraction, CruiseHiss), Holding), 0.0f, 1.0f);
}

float ShipHum::HoldTerm(float HoldWattsDelivered, float WattsPerG, float HoldHiss, float CruiseHiss)
{
    if (!(WattsPerG > 0.0f) || !(HoldWattsDelivered > 0.0f))
    {
        return 0.0f;
    }
    const float Term = FMath::Max(0.0f, HoldHiss) * HoldWattsDelivered / (3.0f * WattsPerG);
    return FMath::Min(Term, FMath::Max(0.0f, CruiseHiss));
}

FShipHumVoice::FSettings FShipHumVoice::Reactor()
{
    return FSettings();
}

FShipHumVoice::FSettings FShipHumVoice::Air()
{
    FSettings Settings;
    Settings.bTone = false;
    Settings.bNoise = true;
    Settings.bFollowsShip = false;
    return Settings;
}

FShipHumVoice::FShipHumVoice(float InSampleRate, uint32 Seed, const FSettings& InSettings)
    : Settings(InSettings)
    , SampleRate(InSampleRate > 0.0f ? InSampleRate : 48000.0f)
    , NoiseState(Seed != 0 ? Seed : 0x9E3779B9u)
{
    Glide = 1.0 - FMath::Exp(-1.0 / (SmoothingSeconds * SampleRate));
}

void FShipHumVoice::SetTargets(const FShipHumInputs& Targets)
{
    Target.EngineFeed = FMath::Clamp(Targets.EngineFeed, 0.0f, 1.0f);
    Target.Push = FMath::Clamp(Targets.Push, 0.0f, 1.0f);
}

FShipHumInputs FShipHumVoice::GetSmoothed() const
{
    FShipHumInputs Out;
    Out.EngineFeed = static_cast<float>(Feed);
    Out.Push = static_cast<float>(Push);
    return Out;
}

float FShipHumVoice::GetPresence() const
{
    return static_cast<float>(Presence);
}

float FShipHumVoice::GetSampleRate() const
{
    return SampleRate;
}

float FShipHumVoice::NextNoise()
{
    // xorshift32: white, cheap, and the same stream from the same seed. Its
    // draws are uniform, which for audio is fine -- the spectrum of white
    // noise does not depend on the draw's distribution, and the low-pass sums
    // enough draws to be near-Gaussian anyway, which is what air sounds like.
    NoiseState ^= NoiseState << 13;
    NoiseState ^= NoiseState >> 17;
    NoiseState ^= NoiseState << 5;
    return static_cast<float>(static_cast<double>(NoiseState) / 2147483648.0 - 1.0);
}

void FShipHumVoice::Advance()
{
    // The inputs glide, not each level and frequency separately: every term
    // in the table is affine in Feed or Push, and a one-pole commutes with an
    // affine map, so gliding the input glides every level, frequency and
    // cutoff that follows it with the same time constant.
    Feed += Glide * (Target.EngineFeed - Feed);
    Push += Glide * (Target.Push - Push);
    Presence += Glide * (1.0 - Presence);
}

void FShipHumVoice::Render(float* Out, int32 NumSamples)
{
    if (!Out || NumSamples <= 0)
    {
        return;
    }

    const double Rate = SampleRate;
    for (int32 Index = 0; Index < NumSamples; ++Index)
    {
        Advance();

        double Sample = 0.0;

        if (Settings.bTone)
        {
            const double Fundamental = FundamentalHz * (1.0 + PitchRise * Feed);
            const double Third = ThirdLevel + ThirdRise * Feed;

            Sample += FundamentalLevel * FMath::Sin(PhaseFundamental);
            // Two sines 0.14 Hz apart beat about once every seven seconds:
            // slow enough to be the ship breathing rather than a tremolo, and
            // incommensurate with the fundamental, so the drone never loops.
            Sample += 0.5 * SecondLevel * (FMath::Sin(PhaseSecondLow) + FMath::Sin(PhaseSecondHigh));
            Sample += Third * FMath::Sin(PhaseThird);

            PhaseFundamental = Wrap(PhaseFundamental + TwoPi * Fundamental / Rate);
            PhaseSecondLow = Wrap(PhaseSecondLow + TwoPi * (2.0 * Fundamental - BeatHz) / Rate);
            PhaseSecondHigh = Wrap(PhaseSecondHigh + TwoPi * (2.0 * Fundamental + BeatHz) / Rate);
            PhaseThird = Wrap(PhaseThird + TwoPi * 3.0 * Fundamental / Rate);
        }

        if (Settings.bNoise)
        {
            const double Cutoff = Settings.bFollowsShip ? NoiseCutoffHz + NoiseCutoffRise * Push : AirCutoffHz;
            const double Level = Settings.bFollowsShip ? NoiseLevel + NoiseRise * Push : AirLevel;
            // The level multiplies the filtered noise as it is, not its RMS:
            // the hiss is quiet at idle and louder as the filter opens, and
            // because a one-pole of draws in [-1, 1) never leaves [-1, 1),
            // each level is also a hard bound on that term's peak.
            NoiseLowPassed += LowPassCoefficient(Cutoff, Rate) * (NextNoise() - NoiseLowPassed);
            Sample += Level * NoiseLowPassed;
        }

        Out[Index] = static_cast<float>(MasterGain * Presence * Sample);
    }
}
