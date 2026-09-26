#pragma once

#include "CoreMinimal.h"
#include "Universe/GenSeed.h"
#include "Universe/UniversePosition.h"
#include "Universe/UniverseUnits.h"

/**
 * What a star system is, as data. Every field is a value and nothing points
 * at anything, so a system can be generated, copied and thrown away freely --
 * which is all anything ever does with one (procgen decision 12: queries
 * return values, and nothing is cached).
 *
 * Natural units (AU, solar and Earth masses and radii, Kelvin); centimetres
 * only in the FUniversePosition fields.
 */

/** Which system: the sector it falls in and its slot there. The identity --
 *  the only piece of universe data anything outside the generator holds
 *  (UShipSubsystem's course is one of these and nothing more). */
struct FSystemId
{
    FInt64Vector Sector = FInt64Vector(0, 0, 0);
    int32 Slot = 0;

    bool operator==(const FSystemId& Other) const
    {
        return Sector == Other.Sector && Slot == Other.Slot;
    }
    bool operator!=(const FSystemId& Other) const { return !(*this == Other); }
};

/** For TSet and TMap (navigation's visited set). Through GenSeed, the
 *  project's one mixing function, rather than a second one. */
inline uint32 GetTypeHash(const FSystemId& Id)
{
    return static_cast<uint32>(GenSeed::Mix(GenSeed::HashCoord(Id.Sector) ^ static_cast<uint64>(static_cast<uint32>(Id.Slot))));
}

/** Which body. Moon is -1 until moons exist; the shape has room for them so
 *  that it does not change when they arrive. */
struct FBodyId
{
    FSystemId System;
    int32 Planet = 0;
    int32 Moon = -1;

    bool operator==(const FBodyId& Other) const
    {
        return System == Other.System && Planet == Other.Planet && Moon == Other.Moon;
    }
    bool operator!=(const FBodyId& Other) const { return !(*this == Other); }
};

/** Main-sequence classes, coolest first -- which is also commonest first. */
enum class EStarClass : uint8 { M, K, G, F, A, B };
inline constexpr int32 NumStarClasses = 6;

/** Derived from mass and temperature, never drawn (procgen decision 5), so
 *  an ice world skimming its star cannot be expressed. */
enum class EPlanetKind : uint8 { Barren, Terrestrial, Ocean, Ice, GasGiant };

struct FStar
{
    EStarClass Class = EStarClass::M;
    double MassSolar = 0.0;
    double RadiusSolar = 0.0;
    double LuminositySolar = 0.0;
    double TemperatureK = 0.0;

    // Derived from luminosity.
    double HabitableInnerAU = 0.0;
    double HabitableOuterAU = 0.0;
    double FrostLineAU = 0.0;
};

struct FPlanet
{
    FBodyId Id;

    /** "Kessa IV": the system's name and the orbit's numeral. Always. */
    FString Designation;

    /** Empty unless inhabited. A name is a claim that somebody cared about a
     *  place, so an empty rock does not get one (procgen decision 8). */
    FString GivenName;

    EPlanetKind Kind = EPlanetKind::Barren;
    double MassEarth = 0.0;
    double RadiusEarth = 0.0;
    double EquilibriumK = 0.0;

    /** Circular and coplanar in the POC, in the universe XY plane. */
    double SemiMajorAxisAU = 0.0;

    /** Where along the circle, radians. Frozen: nothing orbits yet. */
    double PhaseRad = 0.0;

    /** 0 means nobody. */
    double Population = 0.0;

    /** How long a day is, hours: one turn on its axis. Giants only, for now
     *  -- 0 for rock, whose spin tides and impacts decide and nothing yet
     *  reads. A giant's day sets how many belts it wears. */
    double DayHours = 0.0;
};

/** What a star chart and a sky need, and nothing more: a dozen draws. The
 *  full system is generated from the same seed through the same streams, so
 *  a stub is a strict prefix of its system and the two cannot disagree
 *  (procgen decision 4). */
struct FStarSystemStub
{
    FSystemId Id;
    uint64 Seed = 0;

    /** The star's position. */
    FUniversePosition Position;

    FString Name;
    EStarClass Class = EStarClass::M;
    double LuminositySolar = 0.0;
    double TemperatureK = 0.0;
};

struct DEEPSPACE_API FStarSystem
{
    FStarSystemStub Stub;
    FStar Star;

    /** In orbit order, innermost first. Index i is orbit i, so a fifth
     *  planet appends and never renumbers the first four. */
    TArray<FPlanet> Planets;

    /** Star position + a (cos phase, sin phase, 0), converted through
     *  UniverseUnits. No time argument: nothing moves in the POC. Index must
     *  be a valid planet. */
    FUniversePosition PlanetPosition(int32 Index) const;

    /** "In a system" means within this of its star. 0.25 ly: a hundred times
     *  the widest planetary orbit, a tenth of the nearest-neighbour distance.
     *  Which system the ship is in is asked of its position against this,
     *  and never stored (procgen decision 13). The system's edge is also a
     *  surface the drive slows into (plan conflict 10). */
    static constexpr double InSystemRadiusLy = 0.25;
    static constexpr double InSystemRadiusCm = InSystemRadiusLy * UniverseUnits::CmPerLightYear;
};
