#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUniverseFrameTest,
    "DeepSpace.Ship.UniverseFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUniverseFrameTest::RunTest(const FString& Parameters)
{
    // A parked ship at the universe origin draws the universe as it is.
    {
        FShipFlightState State;
        const FUniversePosition Ahead = FUniversePosition::FromVector(FVector(100000.0, 0.0, 0.0));
        TestEqual(TEXT("at the origin the conversion is the identity map"),
                  State.UniverseToWorld(Ahead), FVector(100000.0, 0.0, 0.0));
    }

    // Flying toward something brings it closer, and nothing else.
    {
        FShipFlightState State;
        const FUniversePosition Ahead = FUniversePosition::FromVector(FVector(100000.0, 0.0, 0.0));
        State.SetUniverseTransform(FUniversePosition::FromVector(FVector(10000.0, 0.0, 0.0)), FQuat::Identity);
        TestEqual(TEXT("a point ahead draws nearer as the ship advances"),
                  State.UniverseToWorld(Ahead), FVector(90000.0, 0.0, 0.0));
    }

    // Turning the ship swings the universe the other way. Yawing the nose to
    // universe +Y puts a point on universe +X off the ship's port side.
    {
        FShipFlightState State;
        const FUniversePosition Ahead = FUniversePosition::FromVector(FVector(100000.0, 0.0, 0.0));
        State.SetUniverseTransform(FUniversePosition(), FQuat(FVector::UpVector, UE_DOUBLE_HALF_PI));
        TestTrue(TEXT("a quarter turn swings the universe onto the other axis"),
                 State.UniverseToWorld(Ahead).Equals(FVector(0.0, -100000.0, 0.0), 1e-4));

        // Direction-only conversion is the same rotation with no translation,
        // which is what things at effectively infinite distance need.
        TestTrue(TEXT("a direction rotates but does not translate"),
                 State.UniverseDirectionToWorld(FVector::ForwardVector).Equals(FVector(0.0, -1.0, 0.0), 1e-6));
    }

    // Round trip, for a spread of positions and attitudes.
    {
        const FVector Attitudes[] = { FVector(0.3, 0.7, -0.2), FVector(-1.0, 0.2, 0.5), FVector(0.0, 0.0, 1.0) };
        const FVector Points[] = { FVector(0.0, 0.0, 0.0), FVector(1234.5, -678.25, 9000.0), FVector(-5e7, 3e7, 2e7) };

        for (const FVector& Axis : Attitudes)
        {
            FShipFlightState State;
            State.SetUniverseTransform(FUniversePosition::FromVector(FVector(4.2e9, -1.7e9, 8e8)),
                                       FQuat(Axis.GetSafeNormal(), 1.1));
            for (const FVector& Point : Points)
            {
                const FUniversePosition Universe = FUniversePosition::FromVector(Point);
                const FUniversePosition Back = State.WorldToUniverse(State.UniverseToWorld(Universe));
                TestTrue(TEXT("world and universe round trip"), Back.DistanceTo(Universe) < 1e-4);
            }

            TestTrue(TEXT("the counter-frame is the inverse of the ship's frame"),
                     (State.GetCounterFrameTransform() * State.GetUniverseTransform()).Equals(FTransform::Identity, 1e-6));
        }
    }

    // Precision, far from the universe origin and across a chunk boundary.
    // A flat double would have lost the centimetre long before here; this is
    // the test to look at first if the position type is ever changed back.
    {
        FShipFlightState State;
        const FUniversePosition ShipAt(FInt64Vector(1000000, -500000, 250000), FVector::ZeroVector);
        State.SetUniverseTransform(ShipAt, FQuat::Identity);

        const FUniversePosition Near(FInt64Vector(1000000, -500000, 250000),
                                     FVector(FUniversePosition::ChunkSize - 0.5, 0.0, 0.0));
        const FUniversePosition Far = Near + FVector(1.0, 0.0, 0.0);

        TestEqual(TEXT("the pair straddles a chunk boundary"), Far.Chunk.X, static_cast<int64>(1000001));
        TestEqual(TEXT("one centimetre survives a million chunks from the origin"),
                  (State.UniverseToWorld(Far) - State.UniverseToWorld(Near)).X, 1.0);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
