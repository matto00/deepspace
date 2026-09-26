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
 * It holds the root seed and nothing else -- not the priors, which it asks
 * of UProcGenPriorsConfig's class default on every query, not a start
 * system, not a current system, not a cache, not even a generator, which is
 * two numbers and is built per query. So nothing here can go stale, dangle,
 * or disagree with the ship (procgen decisions 12 and 13), and
 * ds.Universe.ReloadPriors reaches every world without telling any of them.
 * Consumers ask and never keep a copy, the discipline ADR 0003 set for ship
 * state.
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

    /** Resolves the root seed with ResolveSeed(UniverseSeed, FCommandLine::Get()).
     *  Every query below works from here on: none needs begin-play, so
     *  another subsystem's OnWorldBeginPlay may ask in any order. */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** World-level, per docs/vision.md: shareable, and a bug report is a
     *  number. */
    uint64 GetRootSeed() const;

    /** The ini's priors as they are now: [/Script/DeepSpace.ProcGenPriorsConfig]
     *  in DefaultGame.ini, through UProcGenPriorsConfig. Asked, never kept, so
     *  ds.Universe.ReloadPriors changes the next answer and every one after. */
    FGenPriors GetPriors() const;

    // Every query returns a value the caller owns. Nothing here hands out a
    // reference, so nothing can be left holding one.

    /** The nearest system whose star is within FStarSystem::InSystemRadiusCm
     *  of Where; empty between stars. "Which system am I in" is this
     *  question, asked with the ship's position. */
    TOptional<FStarSystem> GetSystemAt(const FUniversePosition& Where) const;

    /** Which system GetSystemAt(Where) would generate, without generating
     *  it: the same stub search, stopped before the system. Empty between
     *  stars. For a consumer that must know every frame whether the system
     *  it drew is still the one the ship is in (the map's cache key), which
     *  a generated system is far too dear to answer. */
    TOptional<FSystemId> GetSystemIdAt(const FUniversePosition& Where) const;

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

    /** The universe is DefaultGame.ini's, never this default's. Zero is
     *  nobody's universe: a default equal to the ini's seed would make a
     *  section that never loads indistinguishable from one that does. */
    UPROPERTY(Config)
    int64 UniverseSeed = 0;

    uint64 RootSeed = 0;
};
