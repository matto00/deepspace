#include "Misc/AutomationTest.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/StarSystemGenerator.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGalaxyGeneratorTest,
    "DeepSpace.Universe.Galaxy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    constexpr uint64 Root = 20260925;
    constexpr int64 PerSector = FGalaxyGenerator::ChunksPerSector;

    bool SameStub(const FStarSystemStub& A, const FStarSystemStub& B)
    {
        return A.Id == B.Id && A.Seed == B.Seed && A.Position == B.Position && A.Name == B.Name
            && A.Class == B.Class && A.LuminositySolar == B.LuminositySolar && A.TemperatureK == B.TemperatureK;
    }

    FUniversePosition AtChunk(int64 X, int64 Y, int64 Z)
    {
        return FUniversePosition(FInt64Vector(X, Y, Z), FVector(1.0, 1.0, 1.0));
    }
}

bool FGalaxyGeneratorTest::RunTest(const FString& Parameters)
{
    const FGalaxyGenerator Galaxy(Root, FGenPriors{});
    const double Ly = UniverseUnits::CmPerLightYear;

    // -- SectorOf floors, never truncates, across the origin on every axis ----
    {
        TestEqual(TEXT("chunk 0 is in sector 0"), FGalaxyGenerator::SectorOf(AtChunk(0, 0, 0)), FInt64Vector(0, 0, 0));
        TestEqual(TEXT("the last chunk of sector 0"), FGalaxyGenerator::SectorOf(AtChunk(PerSector - 1, 0, 0)), FInt64Vector(0, 0, 0));
        TestEqual(TEXT("the first chunk of sector 1"), FGalaxyGenerator::SectorOf(AtChunk(PerSector, 0, 0)), FInt64Vector(1, 0, 0));
        TestEqual(TEXT("chunk -1 on X is in sector -1, not 0"), FGalaxyGenerator::SectorOf(AtChunk(-1, 0, 0)), FInt64Vector(-1, 0, 0));
        TestEqual(TEXT("chunk -1 on Y is in sector -1, not 0"), FGalaxyGenerator::SectorOf(AtChunk(0, -1, 0)), FInt64Vector(0, -1, 0));
        TestEqual(TEXT("chunk -1 on Z is in sector -1, not 0"), FGalaxyGenerator::SectorOf(AtChunk(0, 0, -1)), FInt64Vector(0, 0, -1));
        TestEqual(TEXT("the first chunk of sector -1"), FGalaxyGenerator::SectorOf(AtChunk(-PerSector, 0, 0)), FInt64Vector(-1, 0, 0));
        TestEqual(TEXT("one chunk further is sector -2"), FGalaxyGenerator::SectorOf(AtChunk(-PerSector - 1, 0, 0)), FInt64Vector(-2, 0, 0));

        // An offset that has not been normalised still counts where it lands.
        const FUniversePosition Behind(FInt64Vector(0, 0, 0), FVector(-10.0, 0.0, 0.0));
        TestEqual(TEXT("a negative offset from the origin is in sector -1"), FGalaxyGenerator::SectorOf(Behind), FInt64Vector(-1, 0, 0));
    }

    // -- every stub lies inside its own sector; sectors are order-free --------
    {
        TArray<FInt64Vector> Sectors;
        for (int64 X = -3; X <= 3; ++X)
        {
            for (int64 Y = -3; Y <= 3; ++Y)
            {
                for (int64 Z = -3; Z <= 3; ++Z)
                {
                    Sectors.Add(FInt64Vector(X, Y, Z));
                }
            }
        }

        int32 Outside = 0;
        int32 Total = 0;
        TMap<FSystemId, FStarSystemStub> Forward;
        for (const FInt64Vector& Sector : Sectors)
        {
            for (const FStarSystemStub& Stub : Galaxy.GenerateSector(Sector))
            {
                ++Total;
                Outside += FGalaxyGenerator::SectorOf(Stub.Position) == Sector ? 0 : 1;
                Forward.Add(Stub.Id, Stub);
            }
        }
        TestTrue(TEXT("a 7^3 block of sectors holds systems"), Total > 50);
        TestEqual(TEXT("every stub lies inside its own sector"), Outside, 0);

        int32 Differs = 0;
        for (int32 I = Sectors.Num() - 1; I >= 0; --I)
        {
            for (const FStarSystemStub& Stub : Galaxy.GenerateSector(Sectors[I]))
            {
                const FStarSystemStub* Before = Forward.Find(Stub.Id);
                Differs += (Before && SameStub(*Before, Stub)) ? 0 : 1;
            }
        }
        TestEqual(TEXT("generating sectors in another order gives identical stubs"), Differs, 0);

        // A stub by id is the sector's stub; a slot the sector lacks is empty.
        int32 Mismatch = 0;
        for (const TPair<FSystemId, FStarSystemStub>& Pair : Forward)
        {
            const TOptional<FStarSystemStub> ById = Galaxy.GenerateStub(Pair.Key);
            Mismatch += (ById && SameStub(*ById, Pair.Value)) ? 0 : 1;
        }
        TestEqual(TEXT("GenerateStub agrees with GenerateSector"), Mismatch, 0);
        const FInt64Vector Some = Sectors[0];
        TestFalse(TEXT("a slot past the count is empty"), Galaxy.GenerateStub(FSystemId{Some, Galaxy.SystemCount(Some)}).IsSet());
        TestFalse(TEXT("a negative slot is empty"), Galaxy.GenerateStub(FSystemId{Some, -1}).IsSet());
    }

    // -- the density moves counts, never a surviving star ----------------------
    {
        FGenPriors Denser;
        Denser.SystemsPerSector = 2.0;
        const FGalaxyGenerator Other(Root, Denser);
        int32 Compared = 0;
        int32 Moved = 0;
        for (int64 X = -4; X <= 4; ++X)
        {
            for (int64 Y = -4; Y <= 4; ++Y)
            {
                const FInt64Vector Sector(X, Y, 1);
                const TArray<FStarSystemStub> A = Galaxy.GenerateSector(Sector);
                const TArray<FStarSystemStub> B = Other.GenerateSector(Sector);
                for (int32 I = 0; I < FMath::Min(A.Num(), B.Num()); ++I)
                {
                    ++Compared;
                    Moved += SameStub(A[I], B[I]) ? 0 : 1;
                }
            }
        }
        TestTrue(TEXT("some slots survive the change"), Compared > 10);
        TestEqual(TEXT("changing SystemsPerSector moves no surviving star"), Moved, 0);
    }

    // -- FindSystemsWithin: sorted, bounded, and blind to sector boundaries ----
    {
        const FUniversePosition Origin;
        const TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(Origin, 12.0 * Ly);
        AddInfo(FString::Printf(TEXT("%d systems within 12 ly of the origin"), Near.Num()));
        TestTrue(TEXT("the neighbourhood has neighbours"), Near.Num() >= 10 && Near.Num() <= 120);

        bool bSorted = true;
        bool bInside = true;
        for (int32 I = 0; I < Near.Num(); ++I)
        {
            const double D = Origin.DistanceTo(Near[I].Position);
            bInside &= D <= 12.0 * Ly;
            if (I > 0)
            {
                bSorted &= Origin.DistanceTo(Near[I - 1].Position) <= D;
            }
        }
        TestTrue(TEXT("nearest first"), bSorted);
        TestTrue(TEXT("nothing past the radius"), bInside);

        // Brute force over the same sectors: nothing within range is missed.
        int32 Expected = 0;
        for (int64 X = -4; X <= 3; ++X)
        {
            for (int64 Y = -4; Y <= 3; ++Y)
            {
                for (int64 Z = -4; Z <= 3; ++Z)
                {
                    for (const FStarSystemStub& Stub : Galaxy.GenerateSector(FInt64Vector(X, Y, Z)))
                    {
                        Expected += Origin.DistanceTo(Stub.Position) <= 12.0 * Ly ? 1 : 0;
                    }
                }
            }
        }
        TestEqual(TEXT("the search finds everything a brute force does"), Near.Num(), Expected);

        // A system planted across a sector face from the centre is found.
        const FStarSystemStub& Target = Near[0];
        const FUniversePosition Corner = FGalaxyGenerator::SectorOrigin(Target.Id.Sector);
        const double ToFace = (Target.Position - Corner).X;              // the star's distance from its sector's -X face
        const FUniversePosition Across = Target.Position + FVector(-(ToFace + 0.1 * Ly), 0.0, 0.0);
        TestNotEqual(TEXT("the centre is in the neighbouring sector"), FGalaxyGenerator::SectorOf(Across), Target.Id.Sector);
        bool bFound = false;
        for (const FStarSystemStub& Stub : Galaxy.FindSystemsWithin(Across, ToFace + 0.2 * Ly))
        {
            bFound |= Stub.Id == Target.Id;
        }
        TestTrue(TEXT("a system on the far side of a sector boundary is found"), bFound);
    }

    // -- FindSystemAt: the nearest within range, or nothing between stars -------
    {
        const TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(FUniversePosition(), 12.0 * Ly);
        const FStarSystemStub& Star = Near[0];
        const TOptional<FStarSystemStub> AtStar = Galaxy.FindSystemAt(Star.Position, 0.25 * Ly);
        TestTrue(TEXT("a star's own position is in its system"), AtStar.IsSet() && AtStar->Id == Star.Id);
        const TOptional<FStarSystemStub> Close = Galaxy.FindSystemAt(Star.Position + FVector(0.0, 0.2 * Ly, 0.0), 0.25 * Ly);
        TestTrue(TEXT("0.2 ly out is still in it"), Close.IsSet() && Close->Id == Star.Id);

        // Half-way to the nearest neighbour is between stars, since stars here
        // are a light year or more apart.
        const TArray<FStarSystemStub> Around = Galaxy.FindSystemsWithin(Star.Position, 20.0 * Ly);
        const FVector ToNext = Around[1].Position - Star.Position;
        if (ToNext.Size() > 1.0 * Ly)
        {
            TestFalse(TEXT("half-way between two stars is in neither system"),
                Galaxy.FindSystemAt(Star.Position + ToNext * 0.5, 0.25 * Ly).IsSet());
        }
    }

    // -- the start: deterministic, has a planet, nothing nearer does ------------
    {
        const FSystemId Start = Galaxy.StartSystem();
        TestTrue(TEXT("StartSystem is deterministic"), Start == FGalaxyGenerator(Root, FGenPriors{}).StartSystem());

        const TOptional<FStarSystemStub> Stub = Galaxy.GenerateStub(Start);
        if (TestTrue(TEXT("the start system exists"), Stub.IsSet()))
        {
            const FStarSystem System = Galaxy.GenerateSystem(*Stub);
            TestTrue(TEXT("the start system has a planet"), System.Planets.Num() > 0);

            const double Distance = FUniversePosition().DistanceTo(Stub->Position);
            int32 NearerWithPlanets = 0;
            for (const FStarSystemStub& Other : Galaxy.FindSystemsWithin(FUniversePosition(), Distance))
            {
                if (Other.Id != Start && FStarSystemGenerator::GeneratePlanetCount(Other.Seed, FGenPriors{}) > 0)
                {
                    ++NearerWithPlanets;
                }
            }
            TestEqual(TEXT("nothing with a planet is nearer the origin"), NearerWithPlanets, 0);
            TestTrue(TEXT("the start star is in the start system"),
                Galaxy.FindSystemAt(Stub->Position, FStarSystem::InSystemRadiusCm).IsSet()
                && Galaxy.FindSystemAt(Stub->Position, FStarSystem::InSystemRadiusCm)->Id == Start);

            AddInfo(FString::Printf(TEXT("start: %s, %.2f ly from the origin, %d planets"),
                *Stub->Name, Distance / Ly, System.Planets.Num()));
        }

        // Other universes have other homes, each with a planet.
        int32 WithoutPlanets = 0;
        for (uint64 Seed = 1; Seed <= 12; ++Seed)
        {
            const FGalaxyGenerator Other(Seed, FGenPriors{});
            const TOptional<FStarSystemStub> Home = Other.GenerateStub(Other.StartSystem());
            WithoutPlanets += (Home && Other.GenerateSystem(*Home).Planets.Num() > 0) ? 0 : 1;
        }
        TestEqual(TEXT("seeds 1-12 all start at a system with a planet"), WithoutPlanets, 0);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
