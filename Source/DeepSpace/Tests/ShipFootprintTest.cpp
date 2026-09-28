#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 11's footprint, built in slice (b) because the descent
 * cap and the ground's hard stop read it from the first hover: measured at
 * the origin alone, a 15-degree slope under a 26 m hull puts a corner 3 m
 * into the ground.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipFootprintTest, "DeepSpace.Ship.Landing.Footprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipFootprintTest::RunTest(const FString& Parameters)
{
    using namespace GroundFixtures;
    const double Gear = ShipLanding::DefaultGearClearanceCm;
    const FGroundFieldRef Flat = MakeShared<FCrossedSines, ESPMode::ThreadSafe>(UniverseUnits::CmPerEarthRadius, 0.0, 1.0e4);
    const FFlightSurface World = SurfaceOver(Flat, 1.0e6);
    const FVector3d Pole(0.0, 0.0, 1.0);

    TestEqual(TEXT("eight points: four feet and four belly corners"), ShipLanding::FootprintPoints(Gear).Num(), 8);
    TestTrue(FString::Printf(TEXT("the hull reaches 18.4 m from its origin (%.1f cm)"), ShipLanding::ReachCm(Gear)),
             FMath::IsNearlyEqual(ShipLanding::ReachCm(Gear), FMath::Sqrt(1770.0 * 1770.0 + 510.0 * 510.0 + 100.0), 0.01));

    const ShipLanding::FFootprintClearance Resting = ShipLanding::FootprintClearance(World, Above(World, Pole, Gear), Level(Pole), Gear);
    TestTrue(FString::Printf(TEXT("level at the gear's clearance, a foot touches (%.6f cm)"), Resting.Least),
             FMath::Abs(Resting.Least) < 1.0e-3 && Resting.Point >= 0 && Resting.Point < 4);
    // Up under the foot, not at the pole: a foot 7.6 m off the pole of an
    // Earth-sized sphere is 1.2e-6 rad round from it.
    TestTrue(FString::Printf(TEXT("and the ground's normal there is up (1 - cos = %.3g)"), 1.0 - (Resting.GroundNormal | FVector(Pole))),
             (Resting.GroundNormal | FVector(Pole)) > 1.0 - 1.0e-9);

    const ShipLanding::FFootprintClearance Higher = ShipLanding::FootprintClearance(World, Above(World, Pole, Gear + 50.0), Level(Pole), Gear);
    TestTrue(TEXT("half a metre higher, half a metre of clearance"), FMath::IsNearlyEqual(Higher.Least, 50.0, 1.0e-3));

    // Pitched 15 degrees nose up at 20 m: the aft corners are lowest, and
    // the least clearance is the lowest point's height, measured here
    // independently by projecting every point on the up axis.
    const FQuat Pitched = Level(Pole) * FQuat(FVector::RightVector, FMath::DegreesToRadians(-15.0));
    const ShipLanding::FFootprintClearance Tilted = ShipLanding::FootprintClearance(World, Above(World, Pole, 2000.0), Pitched, Gear);
    double Lowest = TNumericLimits<double>::Max();
    for (const FVector& Point : ShipLanding::FootprintPoints(Gear))
    {
        Lowest = FMath::Min(Lowest, 2000.0 + (Pitched.RotateVector(Point) | FVector(Pole)));
    }
    TestTrue(FString::Printf(TEXT("pitched, the lowest point sets it (%.2f vs %.2f cm)"), Tilted.Least, Lowest),
             FMath::IsNearlyEqual(Tilted.Least, Lowest, 0.05));
    const TArray<FVector, TFixedAllocator<8>> Points = ShipLanding::FootprintPoints(Gear);
    TestTrue(TEXT("and it is an aft point"), Points.IsValidIndex(Tilted.Point) && Points[Tilted.Point].X < 0.0);

    FFlightSurface Sphere = World;
    Sphere.Ground.Reset();
    TestFalse(TEXT("a surface with no ground has no footprint clearance"),
              ShipLanding::FootprintClearance(Sphere, Above(World, Pole, 100.0), Level(Pole), Gear).Point >= 0);
    return true;
}

#endif
