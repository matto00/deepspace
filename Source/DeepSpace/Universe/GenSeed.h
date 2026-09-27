#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"

/**
 * Seed derivation: how one root number becomes every seed in the universe
 * (ADR 0007). Integer hashing only -- no floating point anywhere in here,
 * because floating point is where cross-platform determinism goes to die.
 *
 * Header-only because it is constexpr: a Label has to be a compile-time
 * constant (DeepSpace.Universe.Seed static_asserts one), and a constexpr
 * function must be visible wherever it is called.
 *
 * Every value these produce is pinned by procgen's known-value table, in
 * DeepSpace.Universe.Seed and in Tools/rng_vectors.json. A change to any line
 * here changes every universe; the tests are there to make that a decision
 * somebody takes rather than an accident somebody commits.
 *
 * Tools/rng.py mirrors this file line for line.
 */
namespace GenSeed
{
    /** SplitMix64's increment: 2^64 over the golden ratio, odd. */
    inline constexpr uint64 Golden = 0x9E3779B97F4A7C15ull;

    /** SplitMix64's finaliser (Stafford's variant 13). On its own it maps 0
     *  to 0, which is why Mix adds the increment first. */
    constexpr uint64 Finalise(uint64 Z)
    {
        Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
        Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
        return Z ^ (Z >> 31);
    }

    /** SplitMix64's output for state Value: the finaliser applied to
     *  Value + Golden. The one mixing function in the project, and exactly
     *  FGenStream's first output from seed Value. */
    constexpr uint64 Mix(uint64 Value)
    {
        return Finalise(Value + Golden);
    }

    /** FNV-1a 64 over an ASCII literal, at compile time. Names a purpose.
     *  Bytes are read unsigned, so a label means the same thing whatever the
     *  signedness of char. */
    constexpr uint64 Label(const char* Text)
    {
        uint64 Hash = 0xCBF29CE484222325ull;
        for (; *Text != '\0'; ++Text)
        {
            Hash = (Hash ^ static_cast<uint64>(static_cast<unsigned char>(*Text))) * 0x100000001B3ull;
        }
        return Hash;
    }

    /** Label for text built at runtime -- a room or surface name out of an
     *  FName, say. The same hash as Label over the same characters, which
     *  must be plain ASCII: a label that depended on a text encoding would
     *  change every seed below it the day the encoding did. */
    inline uint64 LabelText(FStringView Text)
    {
        uint64 Hash = 0xCBF29CE484222325ull;
        for (const TCHAR Char : Text)
        {
            check(static_cast<uint32>(Char) < 128u);
            Hash = (Hash ^ static_cast<uint64>(static_cast<uint32>(Char) & 0x7Fu)) * 0x100000001B3ull;
        }
        return Hash;
    }

    /** A child seed: which parent, for what purpose, which one of them. Each
     *  input passes through a full Mix before the next is folded in, so two
     *  siblings that differ only in index are as unrelated as any two seeds. */
    constexpr uint64 Derive(uint64 Parent, uint64 Purpose, uint64 Index = 0)
    {
        return Mix(Parent ^ Mix(Purpose ^ Mix(Index)));
    }

    /** A world's surface seed: its face in M_SkyBody and, since landing, its
     *  ground (landing decision 2). By orbit index, which never renumbers, so
     *  a world keeps its face however many planets are added outside it. The
     *  derivation is the sky's since the face was first drawn; changing it
     *  moves every place anybody has been. */
    constexpr uint64 SurfaceSeed(uint64 SystemSeed, uint64 Index)
    {
        return Derive(SystemSeed, Label("sky.surface"), Index);
    }

    /** Signed coordinates folded one axis at a time, exactly:
     *    H = Mix(uint64(X)); H = Mix(H ^ uint64(Y)); H = Mix(H ^ uint64(Z));
     *  int64 -> uint64 is two's complement by definition in C++20, so
     *  negatives are safe, and (-1, 0, 0) is not (1, 0, 0). */
    constexpr uint64 HashCoord(int64 X, int64 Y, int64 Z)
    {
        uint64 Hash = Mix(static_cast<uint64>(X));
        Hash = Mix(Hash ^ static_cast<uint64>(Y));
        return Mix(Hash ^ static_cast<uint64>(Z));
    }

    /** The same fold for a sector index. Not constexpr only because
     *  FInt64Vector has no constexpr constructor; the arithmetic is above. */
    inline uint64 HashCoord(const FInt64Vector& Coord)
    {
        return HashCoord(Coord.X, Coord.Y, Coord.Z);
    }
}
