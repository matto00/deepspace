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

    /** 1 -> "I", 4 -> "IV", 12 -> "XII". Value must be positive. */
    DEEPSPACE_API FString RomanNumeral(int32 Value);

    /** "Kessa IV" for the fourth orbit: OrbitIndex is zero-based, the numeral
     *  is not. */
    DEEPSPACE_API FString Designation(const FString& SystemName, int32 OrbitIndex);
}
