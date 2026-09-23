#pragma once

#include "CoreMinimal.h"

/**
 * A position anywhere in the universe: an integer chunk index plus a small
 * local offset in centimetres (ADR 0007).
 *
 * A flat double cannot hold a galaxy. Doubles are exact on integers only to
 * about 9e15, which in centimetres is roughly 600 AU -- a comfortable solar
 * system and seven orders of magnitude short of a galaxy. Chunking keeps the
 * offset permanently small, so precision does not degrade with distance.
 *
 * Deliberately not a UObject and not tied to a UWorld, so it is trivially
 * testable headlessly, in the shape FShipPowerState established.
 */
struct DEEPSPACE_API FUniversePosition
{
public:
    /**
     * 2^44 cm, about 1.18 AU.
     *
     * The magnitude follows ADR 0007's illustrative 1 AU: offsets stay far
     * inside a double's exact range (2^44 / 2^53 leaves resolution of about
     * 20 micrometres) and an int64 index reaches far past a galaxy.
     *
     * The *power of two* is the part that matters. Normalisation multiplies a
     * chunk count by this size and subtracts it from an offset; with a power
     * of two that arithmetic is exact in binary floating point, so rebasing
     * introduces no error at all. Any other size would leak a rounding error
     * into every boundary crossing -- the one operation this type exists to
     * get right.
     */
    static constexpr double ChunkSize = 17592186044416.0;

    FInt64Vector Chunk = FInt64Vector(0, 0, 0);

    /** Centimetres within the chunk. Kept in [0, ChunkSize) by Normalise. */
    FVector Offset = FVector::ZeroVector;

    FUniversePosition() = default;

    explicit FUniversePosition(const FVector& InOffset);

    FUniversePosition(const FInt64Vector& InChunk, const FVector& InOffset);

    /** Rebase the offset into its own chunk, stepping the index to match. */
    void Normalise();

    FUniversePosition Normalised() const;

    /** Move by a local delta in centimetres. Renormalises. */
    FUniversePosition operator+(const FVector& LocalDelta) const;
    FUniversePosition& operator+=(const FVector& LocalDelta);

    /**
     * Separation in centimetres, computed through the chunk index.
     *
     * Subtracting two local offsets directly is silently wrong the moment the
     * two positions sit in different chunks, and looks fine until something is
     * far away. This is the only correct way to ask how far apart two things
     * are.
     */
    FVector operator-(const FUniversePosition& Other) const;

    bool operator==(const FUniversePosition& Other) const;
    bool operator!=(const FUniversePosition& Other) const;

    double DistanceTo(const FUniversePosition& Other) const;

    /**
     * Collapse to a plain vector. Only meaningful for things within a chunk or
     * so of the universe origin; far out, this is the precision loss the type
     * exists to avoid. Anything that could be distant must use operator-.
     */
    FVector ToVector() const;

    static FUniversePosition FromVector(const FVector& Vector);
};
