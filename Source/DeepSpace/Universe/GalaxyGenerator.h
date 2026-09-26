#pragma once

#include "CoreMinimal.h"
#include "Universe/GenPriors.h"
#include "Universe/StarSystem.h"
#include "Universe/UniversePosition.h"

/**
 * Position in, the systems near it out. The galaxy is a homogeneous Poisson
 * process on a grid of sectors: a Poisson count per sector, and each system
 * placed uniformly inside it (procgen decision 3). A system's identity is its
 * (sector, slot); its seed derives from that and nothing else.
 *
 * Holds the galaxy seed and the priors and is const throughout, so it can be
 * built per query, copied freely and never go stale. Nothing is cached:
 * every answer is generated when asked and returned by value.
 *
 * There is deliberately no start *position* (plan conflict 3). Where the ship
 * opens is navigation's, written once in UShipSubsystem; the galaxy says only
 * which system is home.
 */
struct DEEPSPACE_API FGalaxyGenerator
{
    /** Sector edge, in chunks. 2^18 chunks = 2^62 cm, about 4.9 light years. */
    static constexpr int32 SectorShift = 18;
    static constexpr int64 ChunksPerSector = int64(1) << SectorShift;
    static constexpr double SectorSizeCm = FUniversePosition::ChunkSize * double(ChunksPerSector);

    /** The widest search FindSystemsWithin will make, so a typo in a console
     *  command cannot enumerate a million sectors. Several times anything the
     *  chart or the sky asks for. */
    static constexpr double MaxSearchRadiusLy = 100.0;

    FGalaxyGenerator(uint64 RootSeed, const FGenPriors& InPriors);

    /** Which sector a position falls in. Floors, never truncates: chunk -1
     *  is in sector -1, not sector 0. */
    static FInt64Vector SectorOf(const FUniversePosition& Position);

    /** The sector's own corner, the least position inside it. */
    static FUniversePosition SectorOrigin(const FInt64Vector& Sector);

    uint64 GetGalaxySeed() const { return GalaxySeed; }
    const FGenPriors& GetPriors() const { return Priors; }

    uint64 SectorSeed(const FInt64Vector& Sector) const;
    uint64 SystemSeed(const FSystemId& Id) const;

    /** How many systems the sector holds, from its `count` stream alone. */
    int32 SystemCount(const FInt64Vector& Sector) const;

    /** Every stub in the sector, slot order. */
    TArray<FStarSystemStub> GenerateSector(const FInt64Vector& Sector) const;

    /** Empty if the sector has no such slot. */
    TOptional<FStarSystemStub> GenerateStub(const FSystemId& Id) const;

    /** Every stub whose star is within RadiusCm of Centre, nearest first
     *  (ties by sector, then slot, so the order is total). The radius is
     *  capped at MaxSearchRadiusLy. */
    TArray<FStarSystemStub> FindSystemsWithin(const FUniversePosition& Centre, double RadiusCm) const;

    /** The nearest system within RadiusCm of Where, if any. "Which system am
     *  I in" is this question, asked with the ship's position. */
    TOptional<FStarSystemStub> FindSystemAt(const FUniversePosition& Where, double RadiusCm) const;

    FStarSystem GenerateSystem(const FStarSystemStub& Stub) const;

    /** The nearest system to the universe origin with at least one planet:
     *  home is what the generator honestly makes, which is usually a red
     *  dwarf (the developer's ruling, 2026-09-25). Searches outward sector
     *  ring by sector ring and stops once nothing unsearched could be nearer,
     *  drawing only the planet count per candidate. The default id if there
     *  is none within MaxStartRings, which only a prior of no planets or no
     *  systems can cause. */
    FSystemId StartSystem() const;

    /** How far StartSystem looks before giving up: about 150 light years. */
    static constexpr int32 MaxStartRings = 32;

private:
    FStarSystemStub MakeStub(const FSystemId& Id) const;

    uint64 GalaxySeed = 0;
    FGenPriors Priors;
};
