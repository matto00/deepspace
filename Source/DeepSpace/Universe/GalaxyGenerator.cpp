#include "Universe/GalaxyGenerator.h"

#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/UniverseUnits.h"

namespace
{
    /** Sectors on one axis whose span [i S, (i + 1) S) the interval
     *  [Low, High] touches, as offsets from the sector the interval is
     *  measured from. */
    void SpanOf(double Low, double High, int64& OutFirst, int64& OutLast)
    {
        OutFirst = static_cast<int64>(FMath::FloorToDouble(Low / FGalaxyGenerator::SectorSizeCm));
        OutLast = static_cast<int64>(FMath::FloorToDouble(High / FGalaxyGenerator::SectorSizeCm));
    }

    /** How many whole sectors lie between the origin and sector index I on
     *  one axis. The origin is a sector corner, so sector 0 and sector -1
     *  both touch it. */
    int64 RingOf(int64 I)
    {
        return I >= 0 ? I : -I - 1;
    }

    bool Precedes(const FStarSystemStub& A, double DistanceA, const FStarSystemStub& B, double DistanceB)
    {
        if (DistanceA != DistanceB)
        {
            return DistanceA < DistanceB;
        }
        const FInt64Vector& SA = A.Id.Sector;
        const FInt64Vector& SB = B.Id.Sector;
        if (SA.X != SB.X) { return SA.X < SB.X; }
        if (SA.Y != SB.Y) { return SA.Y < SB.Y; }
        if (SA.Z != SB.Z) { return SA.Z < SB.Z; }
        return A.Id.Slot < B.Id.Slot;
    }
}

FGalaxyGenerator::FGalaxyGenerator(uint64 RootSeed, const FGenPriors& InPriors)
    : GalaxySeed(GenSeed::Derive(RootSeed, GenSeed::Label("galaxy")))
    , Priors(InPriors)
{
}

FInt64Vector FGalaxyGenerator::SectorOf(const FUniversePosition& Position)
{
    // An arithmetic shift floors for negatives in C++20; a division would
    // truncate towards zero and put chunk -1 in sector 0.
    const FInt64Vector Chunk = Position.Normalised().Chunk;
    return FInt64Vector(Chunk.X >> SectorShift, Chunk.Y >> SectorShift, Chunk.Z >> SectorShift);
}

FUniversePosition FGalaxyGenerator::SectorOrigin(const FInt64Vector& Sector)
{
    return FUniversePosition(
        FInt64Vector(Sector.X * ChunksPerSector, Sector.Y * ChunksPerSector, Sector.Z * ChunksPerSector),
        FVector::ZeroVector);
}

uint64 FGalaxyGenerator::SectorSeed(const FInt64Vector& Sector) const
{
    return GenSeed::Derive(GalaxySeed, GenSeed::Label("sector"), GenSeed::HashCoord(Sector));
}

uint64 FGalaxyGenerator::SystemSeed(const FSystemId& Id) const
{
    return GenSeed::Derive(SectorSeed(Id.Sector), GenSeed::Label("system"), static_cast<uint64>(Id.Slot));
}

int32 FGalaxyGenerator::SystemCount(const FInt64Vector& Sector) const
{
    // Stars in a neighbourhood are, to first order, a homogeneous Poisson
    // point process, so the count in a cell is Poisson.
    FGenStream Stream(GenSeed::Derive(SectorSeed(Sector), GenSeed::Label("count")));
    return Stream.Poisson(Priors.SystemsPerSector, GenGuarantees::MaxSystemsPerSector);
}

FStarSystemStub FGalaxyGenerator::MakeStub(const FSystemId& Id) const
{
    FStarSystemStub Stub;
    Stub.Id = Id;
    Stub.Seed = SystemSeed(Id);

    // A Poisson process places its points uniformly -- the other place
    // uniform is the distribution that describes the thing. Built in integers
    // and a small offset, so nothing passes through a huge double; and from
    // the system's own stream, so a change to the sector's count moves no
    // star that survives it.
    FGenStream Place(GenSeed::Derive(Stub.Seed, GenSeed::Label("place")));
    const FUniversePosition Origin = SectorOrigin(Id.Sector);
    FInt64Vector Chunk = Origin.Chunk;
    Chunk.X += Place.UniformInt(0, ChunksPerSector - 1);
    Chunk.Y += Place.UniformInt(0, ChunksPerSector - 1);
    Chunk.Z += Place.UniformInt(0, ChunksPerSector - 1);
    const FVector Offset(
        Place.Unit() * FUniversePosition::ChunkSize,
        Place.Unit() * FUniversePosition::ChunkSize,
        Place.Unit() * FUniversePosition::ChunkSize);
    Stub.Position = FUniversePosition(Chunk, Offset);

    const FStar Star = FStarSystemGenerator::GenerateStar(Stub.Seed, Priors);
    Stub.Name = FStarSystemGenerator::GenerateName(Stub.Seed);
    Stub.Class = Star.Class;
    Stub.LuminositySolar = Star.LuminositySolar;
    Stub.TemperatureK = Star.TemperatureK;
    return Stub;
}

TArray<FStarSystemStub> FGalaxyGenerator::GenerateSector(const FInt64Vector& Sector) const
{
    const int32 Count = SystemCount(Sector);
    TArray<FStarSystemStub> Stubs;
    Stubs.Reserve(Count);
    for (int32 Slot = 0; Slot < Count; ++Slot)
    {
        Stubs.Add(MakeStub(FSystemId{Sector, Slot}));
    }
    return Stubs;
}

TOptional<FStarSystemStub> FGalaxyGenerator::GenerateStub(const FSystemId& Id) const
{
    if (Id.Slot < 0 || Id.Slot >= SystemCount(Id.Sector))
    {
        return {};
    }
    return MakeStub(Id);
}

TArray<FStarSystemStub> FGalaxyGenerator::FindSystemsWithin(const FUniversePosition& Centre, double RadiusCm) const
{
    const double Radius = FMath::Clamp(RadiusCm, 0.0, MaxSearchRadiusLy * UniverseUnits::CmPerLightYear);

    const FInt64Vector Home = SectorOf(Centre);
    const FVector Within = Centre - SectorOrigin(Home);   // [0, SectorSizeCm) per axis

    int64 X0, X1, Y0, Y1, Z0, Z1;
    SpanOf(Within.X - Radius, Within.X + Radius, X0, X1);
    SpanOf(Within.Y - Radius, Within.Y + Radius, Y0, Y1);
    SpanOf(Within.Z - Radius, Within.Z + Radius, Z0, Z1);

    struct FFound
    {
        FStarSystemStub Stub;
        double Distance;
    };
    TArray<FFound> Found;

    for (int64 DX = X0; DX <= X1; ++DX)
    {
        for (int64 DY = Y0; DY <= Y1; ++DY)
        {
            for (int64 DZ = Z0; DZ <= Z1; ++DZ)
            {
                for (FStarSystemStub& Stub : GenerateSector(Home + FInt64Vector(DX, DY, DZ)))
                {
                    const double Distance = Centre.DistanceTo(Stub.Position);
                    if (Distance <= Radius)
                    {
                        Found.Add(FFound{MoveTemp(Stub), Distance});
                    }
                }
            }
        }
    }

    Found.Sort([](const FFound& A, const FFound& B) { return Precedes(A.Stub, A.Distance, B.Stub, B.Distance); });

    TArray<FStarSystemStub> Result;
    Result.Reserve(Found.Num());
    for (FFound& Each : Found)
    {
        Result.Add(MoveTemp(Each.Stub));
    }
    return Result;
}

TOptional<FStarSystemStub> FGalaxyGenerator::FindSystemAt(const FUniversePosition& Where, double RadiusCm) const
{
    TArray<FStarSystemStub> Near = FindSystemsWithin(Where, RadiusCm);
    if (Near.IsEmpty())
    {
        return {};
    }
    return MoveTemp(Near[0]);
}

FStarSystem FGalaxyGenerator::GenerateSystem(const FStarSystemStub& Stub) const
{
    return FStarSystemGenerator::Generate(Stub, Priors);
}

FSystemId FGalaxyGenerator::StartSystem() const
{
    const FUniversePosition Origin;

    TOptional<FStarSystemStub> Best;
    double BestDistance = TNumericLimits<double>::Max();

    for (int64 Ring = 0; Ring < MaxStartRings; ++Ring)
    {
        // Every sector whose furthest axis is exactly Ring whole sectors from
        // the origin: the shell of the cube [-Ring - 1, Ring].
        for (int64 X = -Ring - 1; X <= Ring; ++X)
        {
            for (int64 Y = -Ring - 1; Y <= Ring; ++Y)
            {
                for (int64 Z = -Ring - 1; Z <= Ring; ++Z)
                {
                    if (FMath::Max3(RingOf(X), RingOf(Y), RingOf(Z)) != Ring)
                    {
                        continue;
                    }
                    for (const FStarSystemStub& Stub : GenerateSector(FInt64Vector(X, Y, Z)))
                    {
                        const double Distance = Origin.DistanceTo(Stub.Position);
                        if ((!Best.IsSet() || Precedes(Stub, Distance, *Best, BestDistance))
                            && FStarSystemGenerator::GeneratePlanetCount(Stub.Seed, Priors) > 0)
                        {
                            Best = Stub;
                            BestDistance = Distance;
                        }
                    }
                }
            }
        }

        // Nothing in a later ring is nearer than this.
        if (Best.IsSet() && BestDistance <= static_cast<double>(Ring + 1) * SectorSizeCm)
        {
            return Best->Id;
        }
    }

    return Best.IsSet() ? Best->Id : FSystemId{};
}
