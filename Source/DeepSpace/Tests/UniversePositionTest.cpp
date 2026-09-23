#include "Misc/AutomationTest.h"
#include "Universe/UniversePosition.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUniversePositionTest,
    "DeepSpace.Universe.Position",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUniversePositionTest::RunTest(const FString& Parameters)
{
    const double Chunk = FUniversePosition::ChunkSize;

    // Construction keeps what it was given, once it is already in range.
    {
        const FUniversePosition Origin;
        TestEqual(TEXT("a fresh position is in chunk zero"), Origin.Chunk, FInt64Vector(0, 0, 0));
        TestEqual(TEXT("a fresh position has no offset"), Origin.Offset, FVector::ZeroVector);

        const FUniversePosition Near(FVector(100.0, 200.0, 300.0));
        TestEqual(TEXT("a small offset stays in chunk zero"), Near.Chunk, FInt64Vector(0, 0, 0));
        TestEqual(TEXT("a small offset is kept"), Near.Offset, FVector(100.0, 200.0, 300.0));
    }

    // Normalisation rebases an out-of-range offset and steps the index.
    {
        FUniversePosition Past(FInt64Vector(0, 0, 0), FVector(Chunk + 500.0, 0.0, 0.0));
        Past.Normalise();
        TestEqual(TEXT("an offset past the chunk steps the index"), Past.Chunk, FInt64Vector(1, 0, 0));
        TestEqual(TEXT("and rebases the offset exactly"), Past.Offset.X, 500.0);

        // Negative offsets step the other way; an offset is never negative.
        FUniversePosition Before(FInt64Vector(0, 0, 0), FVector(0.0, -500.0, 0.0));
        Before.Normalise();
        TestEqual(TEXT("a negative offset steps the index down"), Before.Chunk, FInt64Vector(0, -1, 0));
        TestEqual(TEXT("and comes back positive"), Before.Offset.Y, Chunk - 500.0);
    }

    // The one that matters: a difference across a chunk boundary. Subtracting
    // the two offsets directly gives nearly a whole chunk in the wrong
    // direction; going through the index gives 1000 cm.
    {
        const FUniversePosition Left(FInt64Vector(0, 0, 0), FVector(Chunk - 400.0, 0.0, 0.0));
        const FUniversePosition Right = FUniversePosition(FInt64Vector(0, 0, 0), FVector(Chunk + 600.0, 0.0, 0.0)).Normalised();

        TestEqual(TEXT("the two sit in different chunks"), Right.Chunk, FInt64Vector(1, 0, 0));
        TestEqual(TEXT("separation across a boundary is the true one"), (Right - Left).X, 1000.0);
        TestEqual(TEXT("and is antisymmetric"), (Left - Right).X, -1000.0);
        TestEqual(TEXT("distance across a boundary"), Right.DistanceTo(Left), 1000.0);
    }

    // Far apart: a separation of many chunks is still exact, and one
    // centimetre of it is still resolvable.
    {
        const FUniversePosition Here(FInt64Vector(1000000, 0, 0), FVector(250.0, 0.0, 0.0));
        const FUniversePosition There(FInt64Vector(1000000, 0, 0), FVector(251.0, 0.0, 0.0));
        TestEqual(TEXT("one centimetre is resolvable a million chunks out"), (There - Here).X, 1.0);

        const FUniversePosition Far(FInt64Vector(1000003, 0, 0), FVector(250.0, 0.0, 0.0));
        TestEqual(TEXT("a three-chunk separation is exact"), (Far - Here).X, 3.0 * Chunk);
    }

    // Adding a local delta crosses boundaries without being asked to.
    {
        FUniversePosition Position(FInt64Vector(5, 0, 0), FVector(Chunk - 100.0, 0.0, 0.0));
        Position += FVector(250.0, 0.0, 0.0);
        TestEqual(TEXT("adding across the boundary steps the index"), Position.Chunk, FInt64Vector(6, 0, 0));
        TestEqual(TEXT("and leaves a small offset"), Position.Offset.X, 150.0);

        const FUniversePosition Start(FInt64Vector(5, 0, 0), FVector(Chunk - 100.0, 0.0, 0.0));
        TestEqual(TEXT("the move is the delta it was given"), (Position - Start).X, 250.0);
    }

    // Round trip through a plain vector, for things near the universe origin.
    {
        const FVector Local(12345.5, -6789.25, 42.0);
        const FUniversePosition Position = FUniversePosition::FromVector(Local);
        TestEqual(TEXT("a negative component normalises into the chunk below"), Position.Chunk, FInt64Vector(0, -1, 0));
        TestEqual(TEXT("round trip through a plain vector"), Position.ToVector(), Local);
        TestTrue(TEXT("round trip compares equal"), FUniversePosition::FromVector(Local) == Position);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
