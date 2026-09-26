#pragma once

#include "CoreMinimal.h"
#include "Universe/GenPriors.h"
#include "Universe/StarSystem.h"

/**
 * A system seed in, a star system out. Pure, stateless and const: the same
 * (seed, priors) always gives the same system, bit for bit, and nothing is
 * kept between calls.
 *
 * Only free parameters are drawn; anything physics would determine, physics
 * determines (procgen decision 5). Every quantity draws from its own stream,
 * derived by label and index, so a fifth planet appends without moving the
 * first four, and a tune of one prior moves only what depends on it
 * (procgen decision 2).
 *
 * The stub's parts -- star and name -- are public so that FGalaxyGenerator
 * builds its stubs through exactly the streams Generate uses, which is what
 * makes a stub a strict prefix of its system (procgen decision 4).
 */
struct DEEPSPACE_API FStarSystemGenerator
{
    /** Class from the `star/class` stream, place in the class from
     *  `star/band`, and everything else derived from those two. */
    static FStar GenerateStar(uint64 SystemSeed, const FGenPriors& Priors);

    /** From the system's `name` stream. */
    static FString GenerateName(uint64 SystemSeed);

    /** From the `planets` stream, that and nothing else -- so a search for
     *  a system with planets draws one number per candidate. */
    static int32 GeneratePlanetCount(uint64 SystemSeed, const FGenPriors& Priors);

    /** The whole system. Its star and name agree with the stub's because
     *  they come from the same streams. */
    static FStarSystem Generate(const FStarSystemStub& Stub, const FGenPriors& Priors);

    /** The same with the planet count forced rather than drawn. For the test
     *  that holds a count one higher to leave the first n planets alone. */
    static FStarSystem GenerateWithPlanetCount(const FStarSystemStub& Stub, const FGenPriors& Priors, int32 PlanetCount);
};
