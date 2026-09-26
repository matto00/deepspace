#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Universe/GenPriors.h"
#include "Universe/StarSystem.h"
#include "Universe/UniversePosition.h"
#include "UniverseSubsystem.generated.h"

struct FGalaxyGenerator;

/**
 * The only authority on what exists, and on which system a position is in.
 * Gameplay asks here; nothing generates for itself (ADR 0006: exactly one
 * generator).
 *
 * Everything that touches Unreal lives on this side of the seam: config, the
 * command line, the console. The generator behind it is a pure function of
 * (root seed, priors, id) and knows none of that.
 *
 * It holds the root seed and the priors and nothing else -- not a start
 * system, not a current system, not a cache, not even a generator, which is
 * two numbers and is built per query. So nothing here can go stale, dangle,
 * or disagree with the ship (procgen decisions 12 and 13). Consumers ask and
 * never keep a copy, the discipline ADR 0003 set for ship state.
 *
 * It places nothing. Where the ship starts is UShipSubsystem's, written once
 * (plan conflict 3). A plain UWorldSubsystem, not a tickable one: nothing
 * here happens per frame.
 */
UCLASS(Config = Game)
class DEEPSPACE_API UUniverseSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Convenience accessor. Returns nullptr if there is no world. */
    static UUniverseSubsystem* Get(const UObject* WorldContext);

    /** Resolves the root seed with ResolveSeed(UniverseSeed, FCommandLine::Get())
     *  and takes the priors. Every query below works from here on: none needs
     *  begin-play, so another subsystem's OnWorldBeginPlay may ask in any
     *  order.
     *
     *  Slice 1: the priors are FGenPriors{}, compile-time constants. Slice 2
     *  reads them from GetDefault<UProcGenPriorsConfig>() here instead, and
     *  adds ReloadPriors and ds.Universe.ReloadPriors; no caller changes. */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** World-level, per docs/vision.md: shareable, and a bug report is a
     *  number. */
    uint64 GetRootSeed() const;

    FGenPriors GetPriors() const;

    // Every query returns a value the caller owns. Nothing here hands out a
    // reference, so nothing can be left holding one.

    /** The nearest system whose star is within FStarSystem::InSystemRadiusCm
     *  of Where; empty between stars. "Which system am I in" is this
     *  question, asked with the ship's position. */
    TOptional<FStarSystem> GetSystemAt(const FUniversePosition& Where) const;

    /** Empty if the id names a slot its sector does not have. */
    TOptional<FStarSystem> GetSystem(const FSystemId& Id) const;

    /** Every stub whose star is within RadiusCm of Where, nearest first,
     *  including the system Where is in. Callers that want only the others
     *  (the chart) filter by id. */
    TArray<FStarSystemStub> GetSystemsNear(const FUniversePosition& Where, double RadiusCm) const;

    /** The nearest system to the universe origin with at least one planet
     *  (the developer's ruling, 2026-09-25: home is what the generator
     *  honestly makes, which is usually a red dwarf). */
    FSystemId GetStartSystem() const;

    /** -UniverseSeed= from CommandLine if present, else ConfigSeed.
     *  UPROPERTY(Config) does not read the command line by itself, so
     *  Initialize does it here, and the test exercises the same function. */
    static uint64 ResolveSeed(int64 ConfigSeed, const TCHAR* CommandLine);

    static constexpr double InSystemRadiusLy = FStarSystem::InSystemRadiusLy;

private:
    /** Built per query from the two numbers below; see the class comment. */
    FGalaxyGenerator MakeGalaxy() const;

    UPROPERTY(Config)
    int64 UniverseSeed = 20260925;

    uint64 RootSeed = 20260925;
    FGenPriors Priors;
};
