#pragma once

#include "CoreMinimal.h"

/**
 * The ship's power consumers, by name.
 *
 * The same strings are actor tags in the level (Tools/build_hauler.py), so
 * one identifier names both the consumer and the generated actors that
 * answer to it. Generated actors are addressed by tag and never by name or
 * index: a name is for humans and an index changes whenever the layout does,
 * but a tag is a contract the generator can keep.
 */
namespace ShipPower
{
    DEEPSPACE_API extern const FName Lights;
    DEEPSPACE_API extern const FName Boosters;
    DEEPSPACE_API extern const FName Engine;
}

/**
 * Pure power arithmetic for a ship.
 *
 * Deliberately not a UObject and not tied to a UWorld, so it can be unit-tested
 * headlessly. UShipSubsystem owns an instance of this and exposes it to
 * gameplay. This is the layer that will accumulate the most complexity as ship
 * systems grow, which is exactly why it is the layer that is trivially testable.
 *
 * Two kinds of load, deliberately different:
 *
 * - **Draws** are installed modules. They take what they take, off the top.
 * - **Consumers** divide what is left, in proportion to a weight the player
 *   sets. Each has a *want* and receives a *share*; satisfaction is share
 *   over want, and every consumer degrades proportionally rather than
 *   failing. There is no cutoff in here, no alarm, and no error state: a
 *   badly allocated ship is a dim, sluggish ship and nothing more.
 *
 * Nothing drifts. Weights are a preference the player set and they stay set;
 * no value in here changes on its own with time.
 */
struct DEEPSPACE_API FShipPowerState
{
public:
    void SetReactorOutput(float Watts);
    float GetReactorOutput() const;

    /** Returns false if ModuleId already draws power. */
    bool AddDraw(FName ModuleId, float Watts);

    /** Returns false if ModuleId was not drawing power. */
    bool RemoveDraw(FName ModuleId);

    /** Installed module draws plus everything allocated to consumers. */
    float GetTotalDraw() const;

    /** Reactor output minus total draw. Negative when overloaded. */
    float GetHeadroom() const;

    bool IsOverloaded() const;

    // -- consumers -----------------------------------------------------

    /** Adds or updates a consumer. Want in watts, weight as a bare ratio. */
    void SetConsumer(FName ConsumerId, float Want, float Weight);

    /** How much this consumer would use if it could have everything. Zero
     *  means switched off: it takes part in no split at all. */
    void SetWant(FName ConsumerId, float Want);
    float GetWant(FName ConsumerId) const;

    /** The player's preference. Relative to the other weights and nothing
     *  else; there is no scale and no correct value. */
    void SetWeight(FName ConsumerId, float Weight);
    float GetWeight(FName ConsumerId) const;

    /** Returns false if the consumer was not present. */
    bool RemoveConsumer(FName ConsumerId);

    /** Watts actually reaching this consumer. */
    float GetShare(FName ConsumerId) const;

    /**
     * Share over want, clamped to 0..1. One for a consumer that wants
     * nothing -- a thing that is switched off is not a thing working badly --
     * and one for a consumer that is unknown, so a screen outliving what it
     * showed reads as harmless rather than as a fault.
     */
    float GetSatisfaction(FName ConsumerId) const;

    /** Sum of every share. Never more than the reactor has spare. */
    float GetAllocatedPower() const;

    /** Reactor output less installed draws: what consumers divide. */
    float GetAvailablePower() const;

    TArray<FName> GetConsumers() const;

private:
    struct FConsumer
    {
        float Want = 0.0f;
        float Weight = 0.0f;
        float Share = 0.0f;
    };

    /**
     * Settles the split: proportional to weight, capped at each consumer's
     * want, with anything the caps free redistributed to whoever is still
     * short. Capping one consumer can push another past its own want, so
     * this iterates until a pass caps nobody -- a single pass would leave
     * power unallocated while a consumer went short, which the player would
     * read as the ship being broken.
     *
     * Called on every mutation rather than lazily: consumers number a
     * handful and readers are per-frame, so paying once on change and never
     * on read is both simpler and cheaper.
     */
    void Allocate();

    float ReactorOutput = 0.0f;
    TMap<FName, float> Draws;
    TMap<FName, FConsumer> Consumers;
};
