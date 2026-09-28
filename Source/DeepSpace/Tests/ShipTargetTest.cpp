#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/NavStart.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkySystem.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "UI/TargetMarker.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// One test and no children: a test path with children becomes a group, and a
// group silently stops running its own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipTargetTest,
    "DeepSpace.Ship.Target",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipTargetTestLocal
{
    FSystemId MakeId(int64 X, int32 Slot)
    {
        FSystemId Id;
        Id.Sector = FInt64Vector(X, 5, -2);
        Id.Slot = Slot;
        return Id;
    }

    /** A system of Worlds worlds, written out: only the ids and the count
     *  matter to the arithmetic under test here. */
    FStarSystem Literal(const FSystemId& Id, int32 Worlds)
    {
        FStarSystem System;
        System.Stub.Id = Id;
        System.Stub.Name = TEXT("Kessa");
        for (int32 Orbit = 0; Orbit < Worlds; ++Orbit)
        {
            FPlanet& Planet = System.Planets.AddDefaulted_GetRef();
            Planet.Id = FBodyId{ Id, Orbit, -1 };
            Planet.SemiMajorAxisAU = 0.1 * (Orbit + 1);
            Planet.RadiusEarth = 1.0;
        }
        return System;
    }

    FBodyId World(const FSystemId& System, int32 Orbit)
    {
        return FBodyId{ System, Orbit, -1 };
    }

    constexpr double Frame = 1.0 / 60.0;

    /** Steps the nav state until Event comes back, or Seconds pass. */
    bool StepUntil(FShipNavState& Nav, ENavEvent Event, double Seconds, double OffBoresight = 0.0)
    {
        for (int32 Index = 0; Index < FMath::RoundToInt32(Seconds / Frame); ++Index)
        {
            if (Nav.Step(Frame, 1.0, OffBoresight, FNavTuning()) == Event)
            {
                return true;
            }
        }
        return false;
    }

    void Console(UWorld* World, const TCHAR* Name, const TArray<FString>& Args)
    {
        FOutputDeviceNull Quiet;
        if (IConsoleObject* Command = IConsoleManager::Get().FindConsoleObject(Name))
        {
            if (Command->AsCommand())
            {
                Command->AsCommand()->Execute(Args, World, Quiet);
            }
        }
    }
}

/**
 * The target (system map decisions 4 and 5): an id the ship holds, beside the
 * course, marked and let go at the map, kept through everything the helm does
 * and let go only when a star jump's fold leaves the system it names. First
 * the pure state, then the subsystem that checks the id against where the
 * ship is, then Tab's arithmetic and the console.
 */
bool FShipTargetTest::RunTest(const FString& Parameters)
{
    using namespace ShipTargetTestLocal;
    const FSystemId Kessa = MakeId(4, 1);
    const FSystemId Orvane = MakeId(9, 3);

    // -- FShipNavState: set, clear, set again ---------------------------------
    {
        FShipNavState Nav;
        TestFalse(TEXT("a new ship has no target"), Nav.GetTarget().IsSet());
        TestTrue(TEXT("a world can be marked"), Nav.SetTarget(World(Kessa, 1)));
        TestTrue(TEXT("and is held as its id"), Nav.GetTarget() == TOptional<FBodyId>(World(Kessa, 1)));
        TestTrue(TEXT("another replaces it"), Nav.SetTarget(World(Kessa, 2)) && Nav.GetTarget() == TOptional<FBodyId>(World(Kessa, 2)));
        Nav.ClearTarget();
        TestFalse(TEXT("cleared, there is none"), Nav.GetTarget().IsSet());
        TestTrue(TEXT("and one can be marked again"), Nav.SetTarget(World(Kessa, 0)));

        // The helm's other levers and the chart leave it alone.
        TestTrue(TEXT("a star course"), Nav.Plot(Orvane));
        TestTrue(TEXT("engaged"), Nav.SetEngaged(true));
        TestTrue(TEXT("stood down"), Nav.SetEngaged(false));
        TestTrue(TEXT("replotted"), Nav.Plot(MakeId(9, 4)));
        Nav.ClearPlot();
        TestTrue(TEXT("plotting, engaging, standing down and clearing a star course keep the target"),
                 Nav.GetTarget() == TOptional<FBodyId>(World(Kessa, 0)));
    }

    // -- a star jump lets it go as its fold opens; an in-system jump keeps it --
    {
        FShipNavState Nav;
        Nav.SetTarget(World(Kessa, 1));
        Nav.Plot(Orvane);
        Nav.SetEngaged(true);
        TestTrue(TEXT("the star fold opens"), StepUntil(Nav, ENavEvent::TransitBegan, 1.0));
        TestFalse(TEXT("and the target goes with the system it named"), Nav.GetTarget().IsSet());
        TestFalse(TEXT("nothing can be marked in the fold"), Nav.SetTarget(World(Orvane, 0)));
        TestFalse(TEXT("so nothing is"), Nav.GetTarget().IsSet());
        TestTrue(TEXT("it arrives"), StepUntil(Nav, ENavEvent::Arrived, 10.0));
        TestFalse(TEXT("and the new system arrives with no target: nothing picks one"), Nav.GetTarget().IsSet());
        TestTrue(TEXT("one can be picked there"), Nav.SetTarget(World(Orvane, 0)));
    }
    {
        FShipNavState Nav;
        Nav.SetTarget(World(Kessa, 1));
        TestTrue(TEXT("the target plotted as the course"), Nav.PlotWorld(World(Kessa, 1)));
        Nav.SetEngaged(true);
        TestTrue(TEXT("the in-system fold opens"), StepUntil(Nav, ENavEvent::TransitBegan, 1.0));
        TestTrue(TEXT("and keeps its target: it is where the jump is going"), Nav.GetTarget() == TOptional<FBodyId>(World(Kessa, 1)));
        Nav.ClearTarget();
        TestTrue(TEXT("which cannot be cleared in the fold"), Nav.GetTarget() == TOptional<FBodyId>(World(Kessa, 1)));
        TestFalse(TEXT("or replaced"), Nav.SetTarget(World(Kessa, 2)));
        TestTrue(TEXT("it arrives at the world"), StepUntil(Nav, ENavEvent::ArrivedAtWorld, 10.0));
        TestTrue(TEXT("with the world still marked"), Nav.GetTarget() == TOptional<FBodyId>(World(Kessa, 1)));
    }

    // -- resolving an id: TargetPlanet ------------------------------------------
    {
        const FStarSystem Here = Literal(Kessa, 3);
        const FPlanet* Second = ShipNav::TargetPlanet(Here, World(Kessa, 1));
        TestTrue(TEXT("an orbit of this system resolves to that world"), Second == &Here.Planets[1]);
        TestNull(TEXT("another system's id resolves to nothing"), ShipNav::TargetPlanet(Here, World(Orvane, 1)));
        TestNull(TEXT("an orbit the system lacks, nothing"), ShipNav::TargetPlanet(Here, World(Kessa, 3)));
        TestNull(TEXT("a negative orbit, nothing"), ShipNav::TargetPlanet(Here, World(Kessa, -1)));
        TestNull(TEXT("a moon, which procgen does not make, nothing"), ShipNav::TargetPlanet(Here, FBodyId{ Kessa, 1, 0 }));
    }

    // -- Tab's arithmetic: NextTarget -------------------------------------------
    {
        const FStarSystem Here = Literal(Kessa, 4);
        TestTrue(TEXT("from none, the innermost"), ShipNav::NextTarget(Here, {}) == TOptional<FBodyId>(World(Kessa, 0)));
        bool bOutward = true;
        for (int32 Orbit = 0; Orbit < 3; ++Orbit)
        {
            bOutward &= ShipNav::NextTarget(Here, World(Kessa, Orbit)) == TOptional<FBodyId>(World(Kessa, Orbit + 1));
        }
        TestTrue(TEXT("then outward one at a time"), bOutward);
        TestTrue(TEXT("the outermost wraps to the innermost"), ShipNav::NextTarget(Here, World(Kessa, 3)) == TOptional<FBodyId>(World(Kessa, 0)));
        TestTrue(TEXT("another system's target counts as none"), ShipNav::NextTarget(Here, World(Orvane, 2)) == TOptional<FBodyId>(World(Kessa, 0)));
        TestTrue(TEXT("and so does an orbit this system lacks"), ShipNav::NextTarget(Here, World(Kessa, 7)) == TOptional<FBodyId>(World(Kessa, 0)));
        TestFalse(TEXT("nothing in a system with no worlds"), ShipNav::NextTarget(Literal(Orvane, 0), {}).IsSet());
        TestFalse(TEXT("even from a target"), ShipNav::NextTarget(Literal(Orvane, 0), World(Orvane, 0)).IsSet());
        const FStarSystem Lone = Literal(Kessa, 1);
        TestTrue(TEXT("one world cycles to itself: never none from a set target"),
                 ShipNav::NextTarget(Lone, World(Kessa, 0)) == TOptional<FBodyId>(World(Kessa, 0)));
        bool bNeverNone = true;
        TOptional<FBodyId> At;
        for (int32 Press = 0; Press < 9; ++Press)
        {
            At = ShipNav::NextTarget(Here, At);
            bNeverNone &= At.IsSet();
        }
        TestTrue(TEXT("nine presses in four worlds never clear it"), bNeverNone);
        TestTrue(TEXT("and land where the count says"), At == TOptional<FBodyId>(World(Kessa, 0)));
    }

    // -- the subsystem: the id checked against where the ship is --------------
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("ShipTargetWorld"));
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("and a universe"), Test.Universe))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const UUniverseSubsystem* Universe = Test.Universe;
    StockShip::Install(Ship);
    const auto Where = [Ship]() { return Ship->GetFlightState().GetUniversePosition(); };
    const TOptional<FStarSystem> Home = Universe->GetSystemAt(Where());
    if (!TestTrue(TEXT("the ship opens in a system with worlds"), Home.IsSet() && Home->Planets.Num() >= 2))
    {
        return false;
    }
    const FSystemId HomeId = Home->Stub.Id;
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (!TestTrue(TEXT("the chart has somewhere to go"), Chart.Num() > 0))
    {
        return false;
    }

    TestFalse(TEXT("another system's world is refused"), Ship->SetTarget(World(Chart[0].Id, 0)));
    TestFalse(TEXT("an orbit this system lacks is refused"), Ship->SetTarget(World(HomeId, Home->Planets.Num())));
    TestFalse(TEXT("a moon is refused"), Ship->SetTarget(FBodyId{ HomeId, 0, 0 }));
    TestFalse(TEXT("and none of them was marked"), Ship->GetTarget().IsSet());
    TestTrue(TEXT("a world of the system here is marked"), Ship->SetTarget(World(HomeId, 1)));
    TestTrue(TEXT("as its id"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 1)));
    Ship->ClearTarget();
    TestFalse(TEXT("and let go"), Ship->GetTarget().IsSet());

    // Kept through everything the helm and the chart do.
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetTarget(World(HomeId, 1));
    const auto Kept = [this, Ship, HomeId](const TCHAR* What)
    {
        TestTrue(FString::Printf(TEXT("kept through %s"), What), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 1)));
    };
    Ship->SetDriveEngaged(Pilot, true);
    Ship->Tick(0.1f);
    Ship->SetDriveEngaged(Pilot, false);
    Ship->Tick(0.1f);
    Kept(TEXT("the drive toggled both ways"));
    Ship->SetFlightCommand(Pilot, 0.5f, FVector::ZeroVector);
    Ship->AllStop(Pilot);
    Ship->Tick(0.1f);
    Kept(TEXT("all stop"));
    TestTrue(TEXT("a star course"), Ship->PlotCourse(Chart[0].Id));
    TestTrue(TEXT("replotted"), Chart.Num() < 2 || Ship->PlotCourse(Chart[1].Id));
    Ship->SetJumpEngaged(true);
    Ship->Tick(0.1f);
    Ship->ClearCourse();
    Kept(TEXT("a star course plotted, replotted, engaged and cleared"));
    Ship->ClearPilot();
    Ship->Tick(0.1f);
    Kept(TEXT("the pilot standing up"));

    // Arrival at the floor: flown down to it on the drive's first notch.
    {
        Ship->SetPilot(Pilot);
        const FUniversePosition Centre = Home->PlanetPosition(1);
        const double Radius = Home->Planets[1].RadiusEarth * UniverseUnits::CmPerEarthRadius;
        const FVector Out = (Where() - Centre).GetSafeNormal();
        // The drive stops at its floor; GetRoom is cruise's, over the ground.
        const double DriveFloor = UShipSubsystem::FloorFor(LocalSystem::Here(Home).Bodies[2]);
        const auto FloorRoom = [&]() { return Where().DistanceTo(Centre) - Radius - DriveFloor; };
        Ship->PlaceShip(Centre + Out * (Radius + 60.0 * UniverseUnits::CmPerKm), FRotationMatrix::MakeFromX(-Out).ToQuat());
        Ship->SetDriveEngaged(Pilot, true);
        Ship->SetDriveLever(Pilot, 1);
        double Seconds = 0.0;
        for (; Seconds < 300.0 && FloorRoom() > 2.0 * FShipFlightState::AtFloorCm; Seconds += 0.1)
        {
            Ship->Tick(0.1f);
        }
        TestTrue(FString::Printf(TEXT("the drive brings the ship to the world's floor (%.1f m of room after %.0f s)"),
                                 FloorRoom() / 100.0, Seconds),
                 FloorRoom() <= 2.0 * FShipFlightState::AtFloorCm);
        for (int32 Tick = 0; Tick < 50; ++Tick)
        {
            Ship->Tick(0.1f);
        }
        Kept(TEXT("arrival at the floor, and resting on it"));
        Ship->AllStop(Pilot);
        Ship->SetDriveEngaged(Pilot, false);
    }

    // Tab: CycleTarget lands on the SetTarget a click does, the next world out.
    {
        const TOptional<FBodyId> Expected = ShipNav::NextTarget(*Home, Ship->GetTarget());
        TestTrue(TEXT("Tab cycles"), Ship->CycleTarget());
        TestTrue(TEXT("to the next world outward"), Ship->GetTarget() == Expected && Expected == TOptional<FBodyId>(World(HomeId, 2 % Home->Planets.Num())));
        Ship->ClearTarget();
        TestTrue(TEXT("from none"), Ship->CycleTarget());
        TestTrue(TEXT("to the innermost"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 0)));
    }

    // The console, as ds.Nav.Plot is for the course.
    {
        Console(Test.World, TEXT("ds.Nav.Target"), { TEXT("none") });
        TestFalse(TEXT("ds.Nav.Target none clears it"), Ship->GetTarget().IsSet());
        Console(Test.World, TEXT("ds.Nav.Target"), { TEXT("2") });
        TestTrue(TEXT("ds.Nav.Target 2 marks the second world, numbered as its numeral"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 1)));
        Console(Test.World, TEXT("ds.Nav.Target"), { TEXT("next") });
        TestTrue(TEXT("ds.Nav.Target next cycles as Tab does"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 2 % Home->Planets.Num())));
        TArray<FString> Designation;
        Home->Planets[0].Designation.ParseIntoArrayWS(Designation);
        Console(Test.World, TEXT("ds.Nav.Target"), Designation);
        TestTrue(TEXT("ds.Nav.Target <designation> marks that world"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 0)));
        Console(Test.World, TEXT("ds.Nav.Target"), { FString::FromInt(Home->Planets.Num() + 1) });
        TestTrue(TEXT("an orbit it lacks changes nothing"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 0)));
        Console(Test.World, TEXT("ds.Nav.Target"), {});
        TestTrue(TEXT("listing changes nothing"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 0)));
    }

    // The live ETA as the ship builds it (ruling 3): GetTargetView, which
    // hands TargetMarker::View the boosters' braking at this moment's power
    // and ds.Drive.HoldSeconds, held to the approach the soft cap actually
    // flies through Tick. DeepSpace.UI.TargetMarker.Eta steps the flight
    // state against its own rated limits; this is the one check that the
    // time the map and the HUD print is the moment this ship arrives. At
    // 0.1 c from 0.025 AU, sampled once the lever has settled, to within 0.5 s all
    // the way down -- on fed boosters, and again browned out, where the
    // braking is a quarter and so is the knee the approach brakes from.
    {
        const double C = ShipDriveLever::LightCmPerSecond;
        const double Top = 0.1 * C;     // the drive's top
        const double AU = UniverseUnits::CmPerAU;
        const double Out25 = 0.025 * AU; // 3.7 million km: 125 s at 0.1 c, and the cap's 40
        const FSkySystem Sky = LocalSystem::Here(Home);
        // A world, and a side of it from which the straight line in meets
        // its floor before any other body's, from a point still in home.
        int32 Orbit = INDEX_NONE;
        FVector Out = FVector::ZeroVector;
        FFlightSurface Own;
        const FVector Sides[] = { FVector(0.0, 0.0, 1.0), FVector(0.0, 0.0, -1.0), FVector(0.0, 1.0, 0.0),
                                  FVector(0.0, -1.0, 0.0), FVector(1.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0) };
        for (int32 Candidate = 0; Candidate < Home->Planets.Num() && Orbit == INDEX_NONE; ++Candidate)
        {
            const FSkyBody& Body = Sky.Bodies[Candidate + 1];
            const FFlightSurface Surface{ Body.Position, Body.Radius, UShipSubsystem::FloorFor(Body), false };
            for (const FVector& Side : Sides)
            {
                const FUniversePosition From = Body.Position + Side * Out25;
                if (Universe->GetSystemIdAt(From) != TOptional<FSystemId>(HomeId))
                {
                    continue;
                }
                Ship->PlaceShip(From, FRotationMatrix::MakeFromX(-Side).ToQuat());
                Ship->Tick(0.01f);
                const TOptional<double> ToOwn = ShipFlight::RayToFloor(Surface, From, -Side);
                bool bClear = ToOwn.IsSet();
                for (const FFlightSurface& Other : Ship->GetFlightState().GetSurfaces())
                {
                    if (!Other.bInsideOut && Other.Centre.DistanceTo(Body.Position) < 100.0)
                    {
                        continue;   // the world's own floor
                    }
                    const TOptional<double> Hit = ShipFlight::RayToFloor(Other, From, -Side);
                    bClear &= !Hit || (ToOwn && *Hit > *ToOwn);
                }
                if (bClear)
                {
                    Orbit = Candidate;
                    Out = Side;
                    Own = Surface;
                    break;
                }
            }
        }
        if (TestTrue(TEXT("eta: a world with a clear line in from 0.025 AU"), Orbit != INDEX_NONE))
        {
            Ship->SetTarget(World(HomeId, Orbit));
            const FUniversePosition From = Own.Centre + Out * Out25;
            const auto Fly = [&](float BoosterWeight, const TCHAR* How)
            {
                Ship->SetConsumerWeight(ShipPower::Boosters, BoosterWeight);
                Ship->PlaceShip(From, FRotationMatrix::MakeFromX(-Out).ToQuat());
                Ship->SetPilot(Pilot);
                Ship->SetDriveEngaged(Pilot, true);
                Ship->SetDriveLever(Pilot, Ship->GetFlightState().GetDriveNotchCount() - 1);
                constexpr float Step = 0.25f;
                const auto Room = [&]() { return Where().DistanceTo(Own.Centre) - Own.Radius - Own.Floor; };
                double Clock = 0.0;
                while (Clock < 120.0 && Ship->GetShipSpeed() < 0.999 * Top)
                {
                    Ship->Tick(Step);
                    Clock += Step;
                }
                const double Braking = Ship->GetFlightState().GetLimits().LinearAcceleration;
                AddInfo(FString::Printf(TEXT("eta, %s: at %.4f c after %.1f s, %.0f km out, braking %.0f cm/s^2"), How,
                                        Ship->GetShipSpeed() / C, Clock, Room() / UniverseUnits::CmPerKm, Braking));
                TestTrue(FString::Printf(TEXT("eta, %s: the lever settles at 0.1 c before the cap binds"), How),
                         Ship->GetShipSpeed() >= 0.999 * Top && Room() > 2.0 * Top * Ship->GetFlightState().GetLimits().HoldSeconds);

                TArray<TPair<double, double>> Predicted;   // (when, when it says it will arrive)
                TOptional<double> Arrived;
                // The last sample still short of the floor: the cap can land
                // the ship on it inside one tick from just over AtFloorCm,
                // and then there is no speed at the floor to time the last
                // metre from, so it is timed from here instead.
                double BeforeClock = Clock;
                double BeforeRoom = Room();
                double BeforeSpeed = Ship->GetShipSpeed();
                while (Clock < 600.0 && !Arrived)
                {
                    const TOptional<FTargetView> Now = Ship->GetTargetView(*Home);
                    if (Now && Now->EtaSeconds && Ship->GetShipSpeed() >= TargetMarker::MinSpeed)
                    {
                        Predicted.Emplace(Clock, Clock + *Now->EtaSeconds);
                    }
                    if (Room() <= FShipFlightState::AtFloorCm)
                    {
                        // The flight is at the floor a metre off it; the ETA
                        // is to the floor itself. The braking curve's last
                        // metre is 0.04 s on fed boosters and 0.07 s browned
                        // out, and it is added back, from the flight's own
                        // limits, not the view's.
                        const FShipFlightLimits& Flown = Ship->GetFlightState().GetLimits();
                        Arrived = Ship->GetShipSpeed() > 0.0
                            ? Clock + ShipFlight::SecondsToFloor(FMath::Max(0.0, Room()), Ship->GetShipSpeed(),
                                                                 Flown.LinearAcceleration, Flown.HoldSeconds)
                            : BeforeClock + ShipFlight::SecondsToFloor(BeforeRoom, BeforeSpeed,
                                                                       Flown.LinearAcceleration, Flown.HoldSeconds);
                    }
                    BeforeClock = Clock;
                    BeforeRoom = Room();
                    BeforeSpeed = Ship->GetShipSpeed();
                    Ship->Tick(Step);
                    Clock += Step;
                }
                if (TestTrue(FString::Printf(TEXT("eta, %s: the approach arrives at the floor"), How), Arrived.IsSet())
                    && TestTrue(FString::Printf(TEXT("eta, %s: with a time all the way"), How), Predicted.Num() > 200))
                {
                    double Worst = 0.0;
                    for (const TPair<double, double>& Sample : Predicted)
                    {
                        Worst = FMath::Max(Worst, FMath::Abs(Sample.Value - *Arrived));
                    }
                    AddInfo(FString::Printf(TEXT("eta, %s: arrived %.2f s after the first sample, the ETA at most %.3f s off"),
                                            How, *Arrived - Predicted[0].Key, Worst));
                    TestTrue(FString::Printf(TEXT("eta, %s: the ship's ETA is the flown arrival, to within 0.5 s (%.3f s)"), How, Worst),
                             Worst <= 0.5);
                }
                Ship->AllStop(Pilot);
                Ship->SetDriveEngaged(Pilot, false);
                return Braking;
            };
            const double Fed = Fly(1.0f, TEXT("fed boosters"));
            const double Starved = Fly(0.0f, TEXT("boosters browned out"));
            TestTrue(FString::Printf(TEXT("eta: browned out, the braking really is a quarter (%.0f of %.0f cm/s^2)"), Starved, Fed),
                     FMath::IsNearlyEqual(Starved, 0.25 * Fed, 1e-3 * Fed));
            Ship->SetConsumerWeight(ShipPower::Boosters, 1.0f);
        }
        Ship->SetTarget(World(HomeId, 0));
    }

    // Placed into another system without a jump: the id is held, and names
    // nothing there.
    {
        const TOptional<FStarSystem> Other = Universe->GetSystem(Chart[0].Id);
        if (TestTrue(TEXT("the neighbour generates"), Other.IsSet()))
        {
            Ship->PlaceShip(Other->Stub.Position + FVector(0.0, 0.0, 2.0 * UniverseUnits::CmPerAU), FQuat::Identity);
            TestTrue(TEXT("the target is held as it was"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, 0)));
            TestNull(TEXT("and resolves to nothing in the system the ship is now in"), ShipNav::TargetPlanet(*Other, *Ship->GetTarget()));
            TestFalse(TEXT("so the ship sees no target there"), Ship->GetTargetView(*Other).IsSet());
            TestFalse(TEXT("and home's world cannot be marked from here"), Ship->SetTarget(World(HomeId, 1)));
            if (!Other->Planets.IsEmpty())
            {
                TestTrue(TEXT("Tab starts from this system's innermost"), Ship->CycleTarget()
                         && Ship->GetTarget() == TOptional<FBodyId>(World(Other->Stub.Id, 0)));
            }
        }
    }
    return true;
}

#endif
