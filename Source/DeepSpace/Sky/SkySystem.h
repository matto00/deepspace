#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Universe/StarSystem.h"
#include "Universe/UniversePosition.h"

/**
 * What the sky draws: a flat list of bodies and a list of neighbouring stars.
 *
 * A view type, deliberately smaller than what procgen makes, so the renderer
 * is not coupled to the generator's internals: FStarSystem can grow moons,
 * belts and stations and the sky changes only where FromSystem says so. It is
 * also the drive's view of what is near (sky *Fakes*), which is why it
 * carries the system's edge.
 *
 * There is no placeholder system and there is never going to be one (plan
 * conflict 4): a second generator, however small, is what ADR 0006 forbids.
 * Tests use a hand-written constant fixture, which is data, not a generator.
 *
 * Pure: no UObject, no UWorld. Centimetres and universe axes throughout.
 */

enum class ESkyBodyKind : uint8 { Star, Planet, Moon };

struct DEEPSPACE_API FSkyBody
{
    /** A label for logs and component names, not an identity. */
    FName Id;

    ESkyBodyKind Kind = ESkyBodyKind::Planet;
    FUniversePosition Position;

    /** cm. */
    double Radius = 0.0;

    /** Albedo colour for planets and moons; from temperature for stars. */
    FLinearColor Colour = FLinearColor::White;

    /** Planets and moons, 0..1. */
    double Albedo = 0.3;

    /** Stars, solar units. */
    double Luminosity = 1.0;

    /** Stars. */
    double TemperatureK = 5800.0;

    /** Atmosphere rim; black means none. */
    FLinearColor Rim = FLinearColor::Black;
};

/** A neighbouring star: only ever a point, so a direction and a distance are
 *  all there is to draw. */
struct DEEPSPACE_API FSkyNeighbour
{
    /** The system's name. A label, not an identity: names can repeat across
     *  the galaxy (procgen decision 8). */
    FName SystemId;

    /** Unit, universe axes, from this system's star. The ship is always far
     *  inside its own system, and from a few AU out the direction to a star
     *  light years away differs from this by microradians. */
    FVector Direction = FVector::ForwardVector;

    /** cm, from this system's star. */
    double Distance = 0.0;

    double Luminosity = 1.0;
    double TemperatureK = 5800.0;
};

struct DEEPSPACE_API FSkySystem
{
    /** The system's name, as a label. NAME_None for the empty system. */
    FName SystemId;

    /** The star first -- exactly one in the POC -- then planets innermost
     *  first, then moons. Empty means there is nothing here to draw: no world,
     *  no universe, or the ship between stars. */
    TArray<FSkyBody> Bodies;

    /** Nearest first. Never contains this system. */
    TArray<FSkyNeighbour> Neighbours;

    /** How far from the star this system reaches, cm; the drive slows into it
     *  like a surface, so the drive never carries the ship out of its system
     *  (plan conflict 10). 0 means no edge, for fixtures that have none. */
    double EdgeRadius = 0.0;

    bool IsEmpty() const { return Bodies.IsEmpty(); }

    /**
     * The one adapter from procgen's data to the sky's, and the only place
     * the sky's pure layer sees a procgen type. Bodies from System (radii and
     * positions through UniverseUnits; colour from SkyColour::Blackbody for
     * the star), EdgeRadius = FStarSystem::InSystemRadiusCm, and one
     * neighbour per stub in Neighbours other than System itself, in the order
     * given.
     */
    static FSkySystem FromSystem(const FStarSystem& System, TConstArrayView<FStarSystemStub> Neighbours);
};
