#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/NavStart.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipSubsystem.h"
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
        Ship->PlaceShip(Centre + Out * (Radius + 60.0 * UniverseUnits::CmPerKm), FRotationMatrix::MakeFromX(-Out).ToQuat());
        Ship->SetDriveEngaged(Pilot, true);
        Ship->SetDriveLever(Pilot, 1);
        double Seconds = 0.0;
        for (; Seconds < 300.0 && Ship->GetFlightState().GetRoom() > 2.0 * FShipFlightState::AtFloorCm; Seconds += 0.1)
        {
            Ship->Tick(0.1f);
        }
        TestTrue(FString::Printf(TEXT("the drive brings the ship to the world's floor (%.1f m of room after %.0f s)"),
                                 Ship->GetFlightState().GetRoom() / 100.0, Seconds),
                 Ship->GetFlightState().GetRoom() <= 2.0 * FShipFlightState::AtFloorCm);
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
