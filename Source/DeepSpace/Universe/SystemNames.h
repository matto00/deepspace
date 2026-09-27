#pragma once

#include "CoreMinimal.h"

/**
 * Names, from syllable tables. Pure: a seed in, a string out.
 *
 * A system always has a name. A planet always has a designation -- the
 * system's name and its orbit's numeral, "Kessa IV" -- and only an inhabited
 * world has a given name as well (procgen decision 8). A name is a claim that
 * somebody cared about a place, and most places nobody has.
 *
 * Names can collide across the galaxy, and that is left alone: two Kessas
 * several hundred light years apart is what real naming looks like.
 */
namespace SystemNames
{
    /** Two or three syllables, from the system's own `name` stream. */
    DEEPSPACE_API FString MakeSystemName(uint64 Seed);

    /** A settled world's own name: the same tables under the planet's
     *  `givenname` stream, so it sounds as if the same people named it. */
    DEEPSPACE_API FString MakeGivenName(uint64 Seed);

    /**
     * The widest name the tables can make, under Width: every syllable count,
     * every sound, and the coda a joint drops, as MakeName builds them.
     * Width measures a piece of a name, and a name's width is taken as the
     * sum of its pieces' -- true of a per-letter measure, and of a font but
     * for kerning. A layout that must hold every name asks this rather than
     * trusting the corpus, which is one seed's ten thousand nearest systems
     * and not the limit of what the tables can say.
     */
    DEEPSPACE_API FString WidestName(TFunctionRef<double(const FString&)> Width);

    /** 1 -> "I", 4 -> "IV", 12 -> "XII". Value must be positive. */
    DEEPSPACE_API FString RomanNumeral(int32 Value);

    /** "Kessa IV" for the fourth orbit: OrbitIndex is zero-based, the numeral
     *  is not. */
    DEEPSPACE_API FString Designation(const FString& SystemName, int32 OrbitIndex);
}
