#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Surface/WorldRelief.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 10: over a solid world the drive's floor is 10 km (or the
 * sky's rendered floor) above the world's highest peak, so no summit is ever
 * within 10 km of the drive; oceans, giants and stars are unchanged. And the
 * subsystem hands the flight both floors and every body's pull once a
 * frame, with the landing tunables read at use.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipFloorOverPeaksTest, "DeepSpace.Ship.FloorOverPeaks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipFloorOverPeaksTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    constexpr double Km = UniverseUnits::CmPerKm;
    const FSkyViewParams View;

    FSkyBody Earth;
    Earth.Kind = ESkyBodyKind::Planet;
    Earth.Radius = UniverseUnits::CmPerEarthRadius;
    FSkyBody Rock = Earth;
    Rock.Ground = EGround::Solid;
    Rock.Relief.RadiusCm = Earth.Radius;
    Rock.Relief.PeakCm = 8.0 * Km;
    Rock.Relief.Cratering = 1.0;
    Rock.Relief.Ground = EGround::Solid;
    const double Peak = FWorldRelief(Rock.Relief).MaxHeightCm();
    TestEqual(TEXT("an ocean's floor is the sky's, as before"), UShipSubsystem::FloorFor(Earth), SkyProjection::RenderedFloor(Earth.Radius, View));
    TestEqual(TEXT("a solid world's is that above its highest peak"), UShipSubsystem::FloorFor(Rock),
              SkyProjection::RenderedFloor(Earth.Radius, View) + Peak);
    TestTrue(FString::Printf(TEXT("so an 8 km peak puts it at 18.2 km (%.2f)"), UShipSubsystem::FloorFor(Rock) / Km),
             FMath::IsNearlyEqual(UShipSubsystem::FloorFor(Rock) / Km, 18.2, 0.05));
    {
        FScopedCVar Floor(TEXT("ds.Flight.Floor"), 20.0f);
        TestEqual(TEXT("ds.Flight.Floor 20 moves it with the ocean's"), UShipSubsystem::FloorFor(Rock), 20.0 * Km + Peak);
    }

    FSkyWorld Test(TEXT("FloorOverPeaksWorld"));
    Test.BeginPlay();
    Test.Step(1.0f / 60.0f);
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FShipFlightState& Flight = Test.Ship->GetFlightState();
    TestEqual(TEXT("one surface per body and the edge"), Flight.GetSurfaces().Num(), Here.Bodies.Num() + 1);
    TestEqual(TEXT("one well per body"), Flight.GetWells().Num(), Here.Bodies.Num());
    int32 Solid = 0;
    for (int32 Index = 0; Index < Here.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = Here.Bodies[Index];
        const FFlightSurface& Surface = Flight.GetSurfaces()[Index];
        const bool bSolid = Body.Ground == EGround::Solid;
        Solid += bSolid ? 1 : 0;
        TestEqual(FString::Printf(TEXT("%s has a ground exactly when it is solid"), *Body.Id.ToString()), Surface.HasGround(), bSolid);
        TestEqual(FString::Printf(TEXT("%s is a world unless it is the star"), *Body.Id.ToString()), Surface.bWorld, Body.Kind != ESkyBodyKind::Star);
        TestEqual(FString::Printf(TEXT("%s pulls with its GM"), *Body.Id.ToString()), Flight.GetWells()[Index].Mu, Body.GravParam);
        if (bSolid)
        {
            TestTrue(FString::Printf(TEXT("%s's drive floor is at most 20.2 km over its datum"), *Body.Id.ToString()),
                     Surface.Floor <= FMath::Max(10.0 * Km, SkyProjection::RenderedFloor(Body.Radius, View)) + 10.0 * Km + 1.0);
        }
    }
    TestTrue(TEXT("home has solid worlds to land on"), Solid > 0);
    TestTrue(TEXT("and the ship feels the pull of home"), Flight.GetLocalGravity().Size() > 0.0);

    {
        FScopedCVar Gear(TEXT("ds.Land.GearClearance"), 200.0f);
        FScopedCVar Regime(TEXT("ds.Land.Regime"), 30.0f);
        FScopedCVar Top(TEXT("ds.Vertical.Top"), 100.0f);
        FScopedCVar Touch(TEXT("ds.Land.TouchdownSpeed"), 1.0f);
        FScopedCVar Approach(TEXT("ds.Land.ApproachSeconds"), 0.0f);
        FScopedCVar Skim(TEXT("ds.Land.SkimSeconds"), 5.0f);
        FScopedCVar SkimFloor(TEXT("ds.Land.SkimFloor"), 10.0f);
        FScopedCVar Handback(TEXT("ds.Land.DriveHandback"), 250.0f);
        FScopedCVar Heavy(TEXT("ds.Vertical.HeavyFloor"), 0.5f);
        Test.Step(1.0f / 60.0f);
        const FShipFlightLimits& Limits = Flight.GetLimits();
        TestEqual(TEXT("ds.Land.GearClearance reaches the flight, cm"), Limits.GearClearanceCm, 200.0);
        TestEqual(TEXT("ds.Land.Regime, km"), Limits.RegimeCm, 30.0 * Km);
        TestEqual(TEXT("ds.Vertical.Top, m/s"), Limits.VerticalTop, 1.0e4);
        TestEqual(TEXT("ds.Land.TouchdownSpeed, m/s"), Limits.TouchdownSpeed, 100.0);
        TestEqual(TEXT("ds.Land.ApproachSeconds 0 is clamped to half a second"), Limits.ApproachSeconds, ShipFlight::MinApproachSeconds);
        TestEqual(TEXT("ds.Land.SkimSeconds"), Limits.SkimSeconds, 5.0);
        TestEqual(TEXT("ds.Land.SkimFloor, m/s"), Limits.SkimFloor, 1.0e3);
        TestEqual(TEXT("ds.Land.DriveHandback, m"), Limits.DriveHandbackCm, 2.5e4);
        TestEqual(TEXT("ds.Vertical.HeavyFloor"), Limits.VerticalHeavyFloor, 0.5);
        TestEqual(TEXT("and GearClearance() asks the same"), UShipSubsystem::GearClearance(), 200.0);
    }
    return true;
}

#endif
