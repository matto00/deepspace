#pragma once

#include "CoreMinimal.h"

/**
 * What the hum follows, each 0..1. Written by the game thread, read by the
 * audio thread; never ship state, only this frame's answer to two questions
 * asked of UShipSubsystem.
 */
struct FShipHumInputs
{
    /** Watts reaching the engine over what a winding jump asks for (plan
     *  conflict 8). 0 idle, rising as the split feeds a winding jump, back
     *  to 0 once it is charged and the engine wants nothing. */
    float EngineFeed = 0.0f;

    /** How hard the boosters are working: the hiss. */
    float Push = 0.0f;
};

/**
 * The two inputs, worked out from what the ship answers. Pure, so the rules
 * that turn watts and acceleration into a sound are tested with no world.
 */
namespace ShipHum
{
    /**
     * clamp(EngineShareWatts / WindingWantWatts, 0, 1), and 0 for a want of
     * zero or less. Watts delivered, never satisfaction: an idle engine wants
     * nothing, and nothing asked is fully satisfied, so a drone following
     * satisfaction would sit at full feed whenever the ship is idle and never
     * hear the split at all.
     */
    DEEPSPACE_API float EngineFeed(float EngineShareWatts, float WindingWantWatts);

    /**
     * The larger of two ways the boosters work, clamped to 0..1:
     *
     * - changing speed: |Acceleration| over the rated acceleration. That is
     *   the effort of a throttle move, and on a starved allocation the ship
     *   accelerates at a quarter of its rating, so the same move is a
     *   thinner hiss;
     * - holding a speed: CruiseHiss x |Throttle| x ThrustFraction. The
     *   throttle is a lever that stays where it was left, and a ship holding
     *   a cruise with its boosters audibly idle would not answer it at all
     *   once the speed was reached. ThrustFraction (the rated fraction the
     *   allocation lets the boosters push, 0.25..1) keeps that thinner when
     *   starved too.
     *
     * CruiseHiss 0 is the pure-acceleration reading: silence once cruising.
     */
    DEEPSPACE_API float Push(float AccelerationCmS2, float RatedAccelerationCmS2,
                             float Throttle, float ThrustFraction, float CruiseHiss);
}

/**
 * The ship's hum, synthesised: mono float samples from two inputs, with no
 * sound asset and no graph (lived-in decision 9).
 *
 * Deliberately not a UObject and not tied to a UWorld or an audio device, in
 * the style of FShipPowerState, so every property of the sound that can be
 * stated is tested headlessly. UShipHumComponent owns one per sound source,
 * on the audio thread.
 *
 * | Term                         | Frequency                  | Level               |
 * |------------------------------|----------------------------|---------------------|
 * | Fundamental                  | 48 Hz x (1 + 0.04 x Feed)  | 0.30                |
 * | 2nd partial, a pair +-0.07 Hz| 2 x fundamental            | 0.15 across the pair|
 * | 3rd partial                  | 3 x fundamental            | 0.04 + 0.20 x Feed  |
 * | noise, one-pole low-pass     | cutoff 400 + 1200 x Push   | 0.02 + 0.20 x Push  |
 *
 * Starved is a register, not a fault. At Feed 0 the drone is lower and
 * darker and still whole: no term falls silent, nothing detunes into
 * dissonance, and there is no beep. Nothing here rises on its own with time
 * or pulses to be noticed, because a hum that summoned the player would be
 * the anti-chore principle's failure told by ear.
 */
struct DEEPSPACE_API FShipHumVoice
{
public:
    /** Which terms sound, and whether they follow the inputs. */
    struct FSettings
    {
        bool bTone = true;
        bool bNoise = true;

        /** False: the noise holds AirLevel through an AirCutoffHz filter
         *  whatever the inputs say. */
        bool bFollowsShip = true;
    };

    /** The whole voice, at the reactor. */
    static FSettings Reactor();

    /**
     * Air handling: noise only, quiet, and constant. Life support is not a
     * power consumer (interactable-ship spec, decision 2), so the air has
     * nothing to follow, and a hiss that changed would be a signal that
     * meant nothing.
     */
    static FSettings Air();

    /** A zero seed is replaced: xorshift has a fixed point there. */
    FShipHumVoice(float InSampleRate, uint32 Seed, const FSettings& InSettings = Reactor());

    /** Clamped to 0..1. The voice glides to them; it never jumps. */
    void SetTargets(const FShipHumInputs& Targets);

    /** Overwrites Out with NumSamples samples. */
    void Render(float* Out, int32 NumSamples);

    /** Where the glide has got to. The parameter trajectory, which is what
     *  the smoothing promises -- tested on this, not on the audio. */
    FShipHumInputs GetSmoothed() const;

    /** 0 when the voice is made, rising to 1 with the same glide, so play
     *  beginning is the ship settling into its hum rather than a click. */
    float GetPresence() const;

    float GetSampleRate() const;

    // -- the table, and the numbers that bound it ------------------------

    static constexpr float FundamentalHz = 48.0f;
    static constexpr float PitchRise = 0.04f;        // at Feed 1
    static constexpr float BeatHz = 0.07f;           // each side of the 2nd partial
    static constexpr float FundamentalLevel = 0.30f;
    static constexpr float SecondLevel = 0.15f;      // the pair together
    static constexpr float ThirdLevel = 0.04f;
    static constexpr float ThirdRise = 0.20f;        // at Feed 1
    static constexpr float NoiseLevel = 0.02f;       // RMS
    static constexpr float NoiseRise = 0.20f;        // at Push 1
    static constexpr float NoiseCutoffHz = 400.0f;
    static constexpr float NoiseCutoffRise = 1200.0f; // at Push 1
    static constexpr float AirLevel = 0.04f;         // RMS; the quietest thing aboard
    static constexpr float AirCutoffHz = 600.0f;

    /** Every level, frequency and cutoff glides with this time constant, so
     *  moving a slider makes the ship settle into a new note. */
    static constexpr float SmoothingSeconds = 0.8f;

    /** Worst-case tonal peak 0.69 x this, about 0.086 (-21 dBFS); the hiss
     *  adds its RMS on top. */
    static constexpr float MasterGain = 0.125f;

private:
    /** One step of every glide, and the noise generator. */
    void Advance();
    float NextNoise();

    FSettings Settings;
    float SampleRate = 48000.0f;

    /** Per-sample one-pole coefficient for SmoothingSeconds. */
    double Glide = 0.0;

    FShipHumInputs Target;
    double Feed = 0.0;
    double Push = 0.0;
    double Presence = 0.0;

    /** Accumulated, never recomputed from time, so changing a frequency
     *  cannot move the waveform. Radians, wrapped. */
    double PhaseFundamental = 0.0;
    double PhaseSecondLow = 0.0;
    double PhaseSecondHigh = 0.0;
    double PhaseThird = 0.0;

    uint32 NoiseState = 0;
    double NoiseLowPassed = 0.0;
};
