#pragma once

#include "GameFramework/Pawn.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Surface/GroundField.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * DeepSpace.Surface.GroundKeepsUp's flight, shared so Eyes.ShadowBakeCost
 * flies the same one with the cast shadow on: over Baemsekai IV, from the
 * drive floor at the full sink, then the skim cap's top at 500 m and at 50
 * m, each frame paced to the wall clock so the workers get the time they get
 * in play. It counts; the callers judge.
 */
namespace GroundKeepsUpScenario
{
    struct FResult
    {
        int32 UnderFloor = 0;    // frames under the drive floor
        int32 ProxyShown = 0;    // of those, the proxy drawn
        int32 NotDrawing = 0;    // of those, the ground not drawing the body
        int32 Low = 0;           // frames under 1 km
        int32 Missing = 0;       // of those, no drawn ground under the ship
        double Worst = 0.0;      // the worst drawn gap under 1 km, cm
        double Fastest = 0.0;    // the descent's fastest sink, cm/s
        double Tolerance = 0.0;  // GearClearance / 10, cm
        TArray<double> Top;      // each skim leg's best share of the cap (500 m, 50 m)
        TArray<bool> HeldOff;    // and whether a ridge held it off
        bool bValid = false;
    };

    /** The sun the flight starts under at IV's drive floor, degrees above
     *  the level: 10 is the dusk the cast shadow costs most at (the tile's
     *  build about 9x its heights, where the opening side's is about 1x,
     *  over the day exit). */
    inline constexpr double DuskDegrees = 10.0;

    /** Put the ship 5,000 km over IV's dusk, DuskDegrees, so the Fly that
     *  follows starts from there; false if there is no IV. Fly places the
     *  ship itself, over whichever side of the world it is on. */
    inline bool PlaceOverDusk(SkyTestWorld::FSkyWorld& Test)
    {
        const FSkySystem Here = LocalSystem::Here(Test.World);
        const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, 4, 0.0, Test.Ship->GetFlightState().GetUniversePosition(),
                                                                     ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(DuskDegrees));
        if (!Here.Bodies.IsValidIndex(4) || !Dusk)
        {
            return false;
        }
        const FVector Up = (Dusk->Position - Here.Bodies[4].Position).GetSafeNormal();
        Test.Ship->PlaceShip(Here.Bodies[4].Position + Up * (Here.Bodies[4].Radius + 5.0e8), FRotationMatrix::MakeFromX(-Up).ToQuat());
        Test.Step(1.0f / 60.0f);
        return true;
    }

    /** Fly it in Test, already begun: ds.Terrain.BuildTasks and
     *  UploadsPerFrame as the caller set them. */
    inline FResult Fly(SkyTestWorld::FSkyWorld& Test)
    {
        FResult R;
        UShipSubsystem* Ship = Test.Ship;
        const FShipFlightState& Flight = Ship->GetFlightState();
        const FSkySystem Here = LocalSystem::Here(Test.World);
        const int32 Index = 4;
        if (!Here.Bodies.IsValidIndex(Index) || !Test.Ground)
        {
            return R;
        }
        const FSkyBody& Fourth = Here.Bodies[Index];
        const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
        const FVector Out = (Flight.GetUniversePosition() - Fourth.Position).GetSafeNormal();
        const FVector Heading = FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
        const double DriveFloor = UShipSubsystem::FloorFor(Fourth);
        R.Tolerance = UShipSubsystem::GearClearance() / 10.0;
        constexpr float Dt = 1.0f / 60.0f;

        APawn* Pilot = Test.World->SpawnActor<APawn>();
        Ship->SetPilot(Pilot);
        // At the drive floor, level, over the ship's own side of the world.
        Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + DriveFloor - 100.0), FRotationMatrix::MakeFromXZ(Heading, Out).ToQuat());

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
                ++R.UnderFloor;
                const UStaticMeshComponent* Proxy = Test.Sky->GetProxy(Index);
                R.ProxyShown += Proxy && Proxy->IsVisible() ? 1 : 0;
                R.NotDrawing += Test.Ground->IsDrawingBody() ? 0 : 1;
            }
            const TOptional<double> Agl = Flight.GetGroundAltitude();
            if (Agl && *Agl < 1.0e5)
            {
                ++R.Low;
                const TOptional<double> Drawn = Test.Ground->DrawnHeightUnderShip();
                if (!Drawn)
                {
                    ++R.Missing;
                }
                else
                {
                    R.Worst = FMath::Max(R.Worst, FMath::Abs(*Drawn - Field->Height(FVector3d(Up), 0.0)));
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
        for (double Seconds = 0.0; Seconds < 200.0 && Agl() > 5.0e4; Seconds += Dt)
        {
            Frame();
            R.Fastest = FMath::Max(R.Fastest, -Flight.GetVerticalSpeed());
        }

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
            R.Top.Add(Top);
            R.HeldOff.Add(bHeldOff);
        }
        R.bValid = true;
        return R;
    }
}

#endif
