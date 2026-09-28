#include "GameFramework/Pawn.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Surface/GroundField.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Slice (b)'s done-when with the ship moving (landing decision 6: residency
 * never gates motion; the skim cap and the prefetch keep the ground ahead
 * of the ship). Over Baemsekai IV: from the drive floor at the full sink,
 * then cruising at the skim cap's top at 500 m and at 50 m. The builds run
 * on ds.Terrain.BuildTasks workers, uploaded ds.Terrain.UploadsPerFrame a
 * frame, never flushed; each frame is paced to the wall clock, as play is,
 * so the workers get the time they get in play -- which makes this the
 * slowest test in the suite, about two minutes, by design. Every frame
 * under the drive floor the ground draws the body and the proxy is hidden;
 * every frame under 1 km the drawn ground under the ship is within
 * GearClearance / 10 of the analytic ground.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundKeepsUpTest, "DeepSpace.Surface.GroundKeepsUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGroundKeepsUpTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FScopedCVar Tasks(TEXT("ds.Terrain.BuildTasks"), 2.0f);
    FScopedCVar Uploads(TEXT("ds.Terrain.UploadsPerFrame"), 4.0f);
    FSkyWorld Test(TEXT("GroundKeepsUpWorld"));
    if (!TestNotNull(TEXT("the ground spawns before play"), Test.Ground))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const FShipFlightState& Flight = Ship->GetFlightState();
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const int32 Index = 4;
    const FSkyBody& Fourth = Here.Bodies[Index];
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector Heading = FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
    const double DriveFloor = UShipSubsystem::FloorFor(Fourth);
    const double Tolerance = UShipSubsystem::GearClearance() / 10.0;
    constexpr float Dt = 1.0f / 60.0f;

    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    // At the drive floor, level, over the ship's own side of the world.
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + DriveFloor - 100.0), FRotationMatrix::MakeFromXZ(Heading, Out).ToQuat());

    int32 UnderFloor = 0;
    int32 ProxyShown = 0;
    int32 NotDrawing = 0;
    int32 Low = 0;
    int32 Missing = 0;
    double Worst = 0.0;
    // One frame as play has it: the wall clock paced to Dt, so the workers
    // build what they would in a real frame.
    const auto Frame = [&]()
    {
        const double Began = FPlatformTime::Seconds();
        Test.Step(Dt);
        while (FPlatformTime::Seconds() - Began < Dt)
        {
            FPlatformProcess::Sleep(0.001f);
        }
        const FVector Up = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
        if (Flight.GetUniversePosition().DistanceTo(Fourth.Position) - Fourth.Radius < DriveFloor)
        {
            ++UnderFloor;
            const UStaticMeshComponent* Proxy = Test.Sky->GetProxy(Index);
            ProxyShown += Proxy && Proxy->IsVisible() ? 1 : 0;
            NotDrawing += Test.Ground->IsDrawingBody() ? 0 : 1;
        }
        const TOptional<double> Agl = Flight.GetGroundAltitude();
        if (Agl && *Agl < 1.0e5)
        {
            ++Low;
            const TOptional<double> Drawn = Test.Ground->DrawnHeightUnderShip();
            if (!Drawn)
            {
                ++Missing;
            }
            else
            {
                Worst = FMath::Max(Worst, FMath::Abs(*Drawn - Field->Height(FVector3d(Up), 0.0)));
            }
        }
    };
    const auto Agl = [&]() { return Flight.GetGroundAltitude().Get(TNumericLimits<double>::Max()); };

    // Two seconds' hover at the floor: a placement is a teleport, not
    // motion, and the coarse cut comes first.
    for (int32 Tick = 0; Tick < 120; ++Tick)
    {
        Frame();
    }
    // The full sink, from the drive floor to 500 m.
    Ship->SetVerticalLever(Pilot, -1.0);
    double Fastest = 0.0;
    for (double Seconds = 0.0; Seconds < 200.0 && Agl() > 5.0e4; Seconds += Dt)
    {
        Frame();
        Fastest = FMath::Max(Fastest, -Flight.GetVerticalSpeed());
    }
    TestTrue(FString::Printf(TEXT("the descent reached the full sink, 200 m/s (%.1f m/s)"), Fastest / 100.0), Fastest >= 0.99 * 2.0e4);

    // The skim cap's top at 500 m, then at 50 m: HOVER, cruise full ahead.
    for (const double Height : { 5.0e4, 5.0e3 })
    {
        Ship->SetVerticalLever(Pilot, -1.0);
        for (double Seconds = 0.0; Seconds < 60.0 && Agl() > Height; Seconds += Dt)
        {
            Frame();
        }
        Ship->SetVerticalLever(Pilot, 0.0);
        for (int32 Tick = 0; Tick < 5 * 60; ++Tick)
        {
            Frame();
        }
        Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
        double Top = 0.0;
        bool bHeldOff = false;
        for (int32 Tick = 0; Tick < 10 * 60; ++Tick)
        {
            Frame();
            const FVector Up = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
            const FVector Along = Flight.GetVelocity() - Up * FVector::DotProduct(Flight.GetVelocity(), Up);
            Top = FMath::Max(Top, Along.Size() / ShipFlight::SkimCap(Agl(), ShipFlight::DefaultSkimSeconds, ShipFlight::DefaultSkimFloor));
            bHeldOff |= Flight.GetHold() != EFlightHold::Free && Top < 0.9;
        }
        Ship->SetFlightCommand(Pilot, 0.0f, FVector::ZeroVector);
        AddInfo(FString::Printf(TEXT("at %.0f m: cruise reached %.2f of the skim cap%s"), Height / 100.0, Top, bHeldOff ? TEXT(", held off a ridge") : TEXT("")));
        TestTrue(FString::Printf(TEXT("at %.0f m the ship flew at the skim cap's top, or a ridge held it off (%.2f)"), Height / 100.0, Top),
                 Top >= 0.9 || bHeldOff);
    }

    AddInfo(FString::Printf(TEXT("%d frames under the drive floor, %d under 1 km; worst drawn gap %.2f cm"), UnderFloor, Low, Worst));
    TestTrue(TEXT("the flight spent frames under the floor and under 1 km"), UnderFloor > 1000 && Low > 1000);
    TestEqual(TEXT("under the drive floor the proxy is never drawn"), ProxyShown, 0);
    TestEqual(TEXT("and the ground draws the body every frame"), NotDrawing, 0);
    TestEqual(TEXT("under 1 km there is always drawn ground under the ship"), Missing, 0);
    TestTrue(FString::Printf(TEXT("and it is within GearClearance / 10 of the analytic ground, moving (worst %.2f cm)"), Worst),
             Worst <= Tolerance);
    return true;
}

#endif
