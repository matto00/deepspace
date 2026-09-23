#include "Universe/UniversePosition.h"

namespace
{
    /** Chunks to step so that Value lands back in [0, ChunkSize). */
    int64 ChunkStep(double Value)
    {
        return static_cast<int64>(FMath::FloorToDouble(Value / FUniversePosition::ChunkSize));
    }
}

FUniversePosition::FUniversePosition(const FVector& InOffset)
    : Offset(InOffset)
{
    Normalise();
}

FUniversePosition::FUniversePosition(const FInt64Vector& InChunk, const FVector& InOffset)
    : Chunk(InChunk)
    , Offset(InOffset)
{
}

void FUniversePosition::Normalise()
{
    const FInt64Vector Step(ChunkStep(Offset.X), ChunkStep(Offset.Y), ChunkStep(Offset.Z));
    Chunk += Step;

    // Exact: ChunkSize is a power of two, so scaling an integer by it and
    // subtracting introduces no rounding.
    Offset -= FVector(
        static_cast<double>(Step.X) * ChunkSize,
        static_cast<double>(Step.Y) * ChunkSize,
        static_cast<double>(Step.Z) * ChunkSize);
}

FUniversePosition FUniversePosition::Normalised() const
{
    FUniversePosition Result = *this;
    Result.Normalise();
    return Result;
}

FUniversePosition FUniversePosition::operator+(const FVector& LocalDelta) const
{
    FUniversePosition Result = *this;
    Result += LocalDelta;
    return Result;
}

FUniversePosition& FUniversePosition::operator+=(const FVector& LocalDelta)
{
    Offset += LocalDelta;
    Normalise();
    return *this;
}

FVector FUniversePosition::operator-(const FUniversePosition& Other) const
{
    // Through the index, in integers, before anything becomes a double: the
    // chunk difference is small even when the indices are not.
    const FInt64Vector ChunkDelta = Chunk - Other.Chunk;
    return FVector(
        static_cast<double>(ChunkDelta.X) * ChunkSize + (Offset.X - Other.Offset.X),
        static_cast<double>(ChunkDelta.Y) * ChunkSize + (Offset.Y - Other.Offset.Y),
        static_cast<double>(ChunkDelta.Z) * ChunkSize + (Offset.Z - Other.Offset.Z));
}

bool FUniversePosition::operator==(const FUniversePosition& Other) const
{
    return Chunk == Other.Chunk && Offset == Other.Offset;
}

bool FUniversePosition::operator!=(const FUniversePosition& Other) const
{
    return !(*this == Other);
}

double FUniversePosition::DistanceTo(const FUniversePosition& Other) const
{
    return (*this - Other).Size();
}

FVector FUniversePosition::ToVector() const
{
    return FVector(
        static_cast<double>(Chunk.X) * ChunkSize + Offset.X,
        static_cast<double>(Chunk.Y) * ChunkSize + Offset.Y,
        static_cast<double>(Chunk.Z) * ChunkSize + Offset.Z);
}

FUniversePosition FUniversePosition::FromVector(const FVector& Vector)
{
    return FUniversePosition(Vector);
}
