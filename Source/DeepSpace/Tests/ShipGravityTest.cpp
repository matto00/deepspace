#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipGravity.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 4: gravity is real -- inverse-square, from every body, at
 * real masses -- and the boosters hold it, so it never enters the velocity.
 * Known values from the constants actually used, and the proof that a lever
 * flies the same path beside a heavy world as in empty space.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipGravityTest, "DeepSpace.Ship.Gravity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipGravityTestLocal
{
    FUniversePosition Somewhere()
    {
        return FUniversePosition(FInt64Vector(3, -2, 0), FVector(1.0e12, 5.0e12, 0.0));
    }

    FVector Out()
    {
        return FVector(-0.8, 0.45, 0.4).GetSafeNormal();
    }
}

bool FShipGravityTest::RunTest(const FString& Parameters)
{
    using namespace ShipGravityTestLocal;
    const double Re = UniverseUnits::CmPerEarthRadius;

    const FGravityWell Earth{ Somewhere(), UniverseUnits::GMEarthCm3PerS2, Re };
    const FVector AtSurface = ShipFlight::GravityAt({ Earth }, Somewhere() + Out() * Re);
    // GMEarth over CmPerEarthRadius squared is 979.8398 cm/s^2 (the spec's
    // "979.85" rounds it; Task P1 pins the same number).
    TestTrue(FString::Printf(TEXT("an Earth pulls 979.840 cm/s^2 at its equatorial radius (%.4f)"), AtSurface.Size()),
             FMath::IsNearlyEqual(AtSurface.Size(), 979.8398, 1.0e-3));
    TestTrue(TEXT("toward its centre"), FVector::DotProduct(AtSurface.GetSafeNormal(), -Out()) > 1.0 - 1e-12);

    const FVector Halfway = ShipFlight::GravityAt({ Earth }, Somewhere() + Out() * (0.5 * Re));
    TestTrue(TEXT("inside, a uniform sphere: half the pull at half the radius, never a singularity"),
             FMath::IsNearlyEqual(Halfway.Size(), 0.5 * AtSurface.Size(), 1e-9 * AtSurface.Size()));
    TestTrue(TEXT("and none at the centre"), ShipFlight::GravityAt({ Earth }, Somewhere()).IsZero());

    const FGravityWell Sun{ Somewhere() + FVector(0.0, 0.0, 3.0e13), UniverseUnits::GMSunCm3PerS2, UniverseUnits::CmPerSolarRadius };
    const double AtAU = ShipFlight::GravityAt({ Sun }, Sun.Centre + Out() * UniverseUnits::CmPerAU).Size();
    TestTrue(FString::Printf(TEXT("the Sun pulls 0.593 cm/s^2 at 1 AU (%.5f)"), AtAU), FMath::IsNearlyEqual(AtAU, 0.593, 0.0005));

    const FUniversePosition Between = Somewhere() + Out() * (3.0 * Re);
    const FVector Both = ShipFlight::GravityAt({ Earth, Sun }, Between);
    const FVector Summed = ShipFlight::GravityAt({ Earth }, Between) + ShipFlight::GravityAt({ Sun }, Between);
    TestTrue(TEXT("every body's pull, summed (ruling 4)"), (Both - Summed).Size() <= 1e-12 * Both.Size());
    TestTrue(TEXT("no wells, no pull"), ShipFlight::GravityAt({}, Between).IsZero());

    // The velocity is unaffected by gravity (decision 4): a full lever beside
    // a 3 g world flies exactly the path it flies with no wells at all.
    const FGravityWell Heavy{ Somewhere(), 3.0 * ShipFlight::StandardGravityCmS2 * Re * Re, Re };
    FShipFlightState Free;
    FShipFlightState Pulled;
    const FUniversePosition Start = Somewhere() + Out() * (Re + 5.0e6);
    for (FShipFlightState* State : { &Free, &Pulled })
    {
        State->SetUniverseTransform(Start, FQuat::Identity);
        FShipFlightCommand Command;
        Command.Throttle = 1.0;
        State->SetCommand(Command);
    }
    Pulled.SetWells({ Heavy });
    for (int32 Frame = 0; Frame < 600; ++Frame)
    {
        Free.Step(1.0 / 60.0);
        Pulled.Step(1.0 / 60.0);
    }
    TestTrue(TEXT("a full-lever path beside a 3 g world is the empty-space path, exactly"),
             (Pulled.GetUniversePosition() - Free.GetUniversePosition()).IsZero() && Pulled.GetVelocity() == Free.GetVelocity());
    const FVector Expected = ShipFlight::GravityAt({ Heavy }, Pulled.GetUniversePosition());
    TestTrue(TEXT("the flight state reports the pull where the ship is"),
             (Pulled.GetLocalGravity() - Expected).IsNearlyZero(1e-9));
    TestTrue(TEXT("and the boosters' thrust is what the ship did less what gravity would have done"),
             (Pulled.GetThrustAcceleration() - (Pulled.GetLinearAcceleration() - Expected)).IsNearlyZero(1e-9));
    TestTrue(TEXT("a flight state with no wells feels none"), Free.GetLocalGravity().IsZero());
    TestEqual(TEXT("and keeps the wells it was handed"), Pulled.GetWells().Num(), 1);
    return true;
}

#endif
