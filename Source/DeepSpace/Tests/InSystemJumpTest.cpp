#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/NavStart.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipNavScreen.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkySystem.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "UI/SystemMapWidget.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// Three tests and no children under any of them: a test path with children
// becomes a group, and a group silently stops running its own body. That is
// why the chart's case is DeepSpace.UI.ChartInSystemCourse and not the map
// spec's DeepSpace.UI.NavigationScreen.InSystemCourse, which would have
// turned the chart's own test into a group that runs nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FInSystemJumpTest,
    "DeepSpace.Ship.InSystemJump",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLoopInSystemJumpTest,
    "DeepSpace.Loop.InSystemJump",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FChartInSystemCourseTest,
    "DeepSpace.UI.ChartInSystemCourse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace InSystemJumpTestLocal
{
    constexpr double Frame = 1.0 / 60.0;

    FSystemId MakeId(int64 X, int32 Slot)
    {
        FSystemId Id;
        Id.Sector = FInt64Vector(X, -7, 3);
        Id.Slot = Slot;
        return Id;
    }

    FBodyId World(const FSystemId& System, int32 Orbit)
    {
        return FBodyId{ System, Orbit, -1 };
    }

    /** Steps the nav state for Seconds with the inputs held, returning the
     *  first event other than None, or None. */
    ENavEvent StepFor(FShipNavState& Nav, double Seconds, double Charge, double OffBoresight)
    {
        for (int32 Index = 0; Index < FMath::RoundToInt32(Seconds / Frame); ++Index)
        {
            const ENavEvent Event = Nav.Step(Frame, Charge, OffBoresight, FNavTuning());
            if (Event != ENavEvent::None)
            {
                return Event;
            }
        }
        return ENavEvent::None;
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

    /** The world's centre, radius and floor, as the sky and the drive have
     *  them: body Orbit + 1 of the system, the star first. */
    struct FWorldSeen
    {
        FUniversePosition Centre;
        double Radius = 0.0;
        double Floor = 0.0;
    };

    FWorldSeen Seen(const FStarSystem& System, int32 Orbit)
    {
        const FSkySystem Sky = LocalSystem::Here(TOptional<FStarSystem>(System));
        const FSkyBody& Body = Sky.Bodies[Orbit + 1];
        return { Body.Position, Body.Radius, UShipSubsystem::FloorFor(Body) };
    }

    /** Every body's floor sphere in System but Orbit's. */
    TArray<FFlightSurface> OthersThan(const FStarSystem& System, int32 Orbit)
    {
        TArray<FFlightSurface> Others;
        const FSkySystem Sky = LocalSystem::Here(TOptional<FStarSystem>(System));
        for (int32 Index = 0; Index < Sky.Bodies.Num(); ++Index)
        {
            if (Index != Orbit + 1)
            {
                Others.Add({ Sky.Bodies[Index].Position, Sky.Bodies[Index].Radius, UShipSubsystem::FloorFor(Sky.Bodies[Index]), false });
            }
        }
        return Others;
    }

    /** The outermost world the ship is not already near enough to fly to:
     *  the opening shot frames the largest world, which is often the
     *  outermost, from inside its reach. */
    int32 FarWorld(const UShipSubsystem& Ship, const FStarSystem& Here)
    {
        for (int32 Orbit = Here.Planets.Num() - 1; Orbit >= 0; --Orbit)
        {
            if (!Ship.IsNearEnoughToFly(Here, World(Here.Stub.Id, Orbit)))
            {
                return Orbit;
            }
        }
        return INDEX_NONE;
    }

    /** Distance from Point to the line through A and B, cm. */
    double OffLine(const FUniversePosition& Point, const FUniversePosition& A, const FUniversePosition& B)
    {
        const FVector Along = (B - A).GetSafeNormal();
        return FVector::CrossProduct(Point - A, Along).Size();
    }
}

/**
 * The in-system jump (system map decision 12, ruling 1): the one jump, sent
 * to the target. The pure state first -- one course, always the target, let
 * go with it, arriving at the world with the star record untouched -- then
 * the standoff's geometry, then the whole of it on the stock ship: plotted,
 * refused near, flown near and let go, and folded, arriving at rest on the
 * line from where the fold opened, two degrees from the world.
 */
bool FInSystemJumpTest::RunTest(const FString& Parameters)
{
    using namespace InSystemJumpTestLocal;
    const double Km = UniverseUnits::CmPerKm;
    const FSystemId Kessa = MakeId(2, 5);
    const FSystemId Orvane = MakeId(8, 1);

    // -- FShipNavState: one course, and a world course is the target ---------
    {
        FShipNavState Nav;
        TestFalse(TEXT("a world with no target is refused"), Nav.PlotWorld(World(Kessa, 1)));
        Nav.SetTarget(World(Kessa, 1));
        TestFalse(TEXT("so is any world but the target"), Nav.PlotWorld(World(Kessa, 2)));
        TestFalse(TEXT("and there is no course"), Nav.HasCourse());
        TestTrue(TEXT("the target is plotted"), Nav.PlotWorld(World(Kessa, 1)));
        TestTrue(TEXT("as the world course"), Nav.GetPlottedWorld() == TOptional<FBodyId>(World(Kessa, 1)) && Nav.HasCourse());
        TestFalse(TEXT("which is no star course"), Nav.GetPlotted().IsSet());
        TestTrue(TEXT("a star replaces it"), Nav.Plot(Orvane) && Nav.GetPlotted().IsSet() && !Nav.GetPlottedWorld().IsSet());
        TestTrue(TEXT("and it replaces a star"), Nav.PlotWorld(World(Kessa, 1)) && !Nav.GetPlotted().IsSet() && Nav.GetPlottedWorld().IsSet());

        TestTrue(TEXT("a world course engages"), Nav.SetEngaged(true));
        TestTrue(TEXT("marking another world"), Nav.SetTarget(World(Kessa, 2)));
        TestFalse(TEXT("lets the course go"), Nav.HasCourse());
        TestFalse(TEXT("and stands the jump down"), Nav.IsEngaged());

        Nav.PlotWorld(World(Kessa, 2));
        Nav.SetEngaged(true);
        Nav.SetTarget(World(Kessa, 2));
        TestTrue(TEXT("marking the same world again keeps it"), Nav.HasCourse() && Nav.IsEngaged());
        Nav.ClearTarget();
        TestFalse(TEXT("clearing the target lets the course go"), Nav.HasCourse());
        TestFalse(TEXT("and stands the jump down"), Nav.IsEngaged());

        Nav.SetTarget(World(Kessa, 0));
        Nav.Plot(Orvane);
        Nav.SetTarget(World(Kessa, 1));
        TestTrue(TEXT("a star course is independent of the target"), Nav.GetPlotted() == TOptional<FSystemId>(Orvane));
        Nav.ClearTarget();
        TestTrue(TEXT("both ways"), Nav.GetPlotted() == TOptional<FSystemId>(Orvane));

        // Replacing the course's kind leaves the jump engaged, as a star
        // replotted over a star always has: a star row pressed on the chart
        // while the helm's in-system jump is engaged makes an engaged star
        // jump. Whether changing the kind should stand the jump down is an
        // open question put to the developer (map spec, Open questions);
        // this pins what stands until it is ruled.
        Nav.SetTarget(World(Kessa, 1));
        Nav.PlotWorld(World(Kessa, 1));
        Nav.SetEngaged(true);
        TestTrue(TEXT("a star over an engaged world course: still engaged, as ruled so far"), Nav.Plot(Orvane) && Nav.IsEngaged());
        TestTrue(TEXT("and the world back over the star: still engaged"), Nav.PlotWorld(World(Kessa, 1)) && Nav.IsEngaged());
    }

    // -- FShipNavState: the fold, and the arrival at a world ------------------
    {
        FShipNavState Nav;
        Nav.MarkVisited(Kessa);
        Nav.SetTarget(World(Kessa, 1));
        Nav.PlotWorld(World(Kessa, 1));
        const double Cone = FNavTuning().ConeRadians;
        TestEqual(TEXT("not engaged, it does not fire"), StepFor(Nav, 1.0, 1.0, 0.0), ENavEvent::None);
        Nav.SetEngaged(true);
        TestEqual(TEXT("uncharged, it does not fire"), StepFor(Nav, 1.0, 0.99, 0.0), ENavEvent::None);
        TestEqual(TEXT("outside the cone, it does not fire"), StepFor(Nav, 1.0, 1.0, Cone + 1e-6), ENavEvent::None);
        TestEqual(TEXT("on the cone's edge, it fires"), StepFor(Nav, 1.0, 1.0, Cone), ENavEvent::TransitBegan);
        TestEqual(TEXT("the word in the fold is the transit's"), Nav.GetJumpState(0.0), EJumpState::Transit);
        TestTrue(TEXT("the target is kept"), Nav.GetTarget() == TOptional<FBodyId>(World(Kessa, 1)));
        const int32 Serial = Nav.GetJumpSerial();
        TestEqual(TEXT("it arrives at the world"), StepFor(Nav, 10.0, 0.0, UE_DOUBLE_PI), ENavEvent::ArrivedAtWorld);
        TestFalse(TEXT("the course is cleared"), Nav.HasCourse());
        TestFalse(TEXT("engage returns to off"), Nav.IsEngaged());
        TestFalse(TEXT("out of the fold"), Nav.IsInTransit());
        TestEqual(TEXT("the serial counts it"), Nav.GetJumpSerial(), Serial + 1);
        TestFalse(TEXT("no star has been arrived at"), Nav.GetLastArrival().IsSet());
        TestTrue(TEXT("and the visited record is as it was"), Nav.HasVisited(Kessa) && !Nav.HasVisited(Orvane));
        TestTrue(TEXT("the target is still set on arrival"), Nav.GetTarget() == TOptional<FBodyId>(World(Kessa, 1)));
    }

    // -- NavStart: the standoff, and the point on the line --------------------
    const FUniversePosition Far(FInt64Vector(91234, -3310, 42), FVector(3.3e13, -2.1e12, 7.0e11));
    {
        const double Earth = UniverseUnits::CmPerEarthRadius;
        const double EarthFloor = 10.2 * Km;
        const double EarthStandoff = NavStart::WorldStandoffCm(Earth, EarthFloor, 2.0);
        TestEqual(TEXT("an Earth is met 2 degrees across: R / sin(1 degree)"), EarthStandoff, Earth / FMath::Sin(FMath::DegreesToRadians(1.0)), 1.0);
        TestTrue(FString::Printf(TEXT("which is about 365,000 km (%.0f km)"), EarthStandoff / Km),
                 FMath::Abs(EarthStandoff / Km - 365000.0) < 1000.0);
        const double Jupiter = 11.2 * Earth;
        TestTrue(TEXT("a Jupiter at about 4.0 million km"),
                 FMath::Abs(NavStart::WorldStandoffCm(Jupiter, 112.0 * Km, 2.0) / Km - 4.09e6) < 0.05e6);
        // Where the ten floors govern: a body under about 1.8 km across a
        // 10 km floor, since R / sin(1 degree) = 57.3 R. (The spec's "a body
        // of 100 km radius" is met at 5,700 km, where the angle still does.)
        TestEqual(TEXT("a 1 km rock over a 10 km floor: ten floors up"), NavStart::WorldStandoffCm(1.0 * Km, 10.0 * Km, 2.0),
                  1.0 * Km + 10.0 * 10.0 * Km, 1e-3);
        TestEqual(TEXT("a 100 km body: the angle still governs"), NavStart::WorldStandoffCm(100.0 * Km, 10.0 * Km, 2.0),
                  100.0 * Km / FMath::Sin(FMath::DegreesToRadians(1.0)), 1.0);
        TestTrue(TEXT("a wider angle is nearer"), NavStart::WorldStandoffCm(Earth, EarthFloor, 4.0) < EarthStandoff);

        for (const double Radius : { Earth, Jupiter, 100.0 * Km, 1.0 * Km })
        {
            const double Floor = FMath::Max(10.0 * Km, Radius > 5.0 * Earth ? 112.0 * Km : 10.2 * Km);
            const FUniversePosition Centre = Far + FVector(2.0e13, 1.0e13, -3.0e11);
            const FUniversePosition Arrival = NavStart::WorldArrivalPoint(Far, Centre, Radius, Floor, 2.0);
            const double Standoff = NavStart::WorldStandoffCm(Radius, Floor, 2.0);
            TestEqual(FString::Printf(TEXT("R %.0f km: the arrival is the standoff from the centre, to a centimetre"), Radius / Km),
                      Arrival.DistanceTo(Centre), Standoff, 1.0);
            TestTrue(FString::Printf(TEXT("R %.0f km: on the line from the departure, to a centimetre"), Radius / Km),
                     OffLine(Arrival, Far, Centre) < 1.0);
            TestTrue(FString::Printf(TEXT("R %.0f km: short of the world, not past it"), Radius / Km),
                     Arrival.DistanceTo(Far) < Centre.DistanceTo(Far));
            TestTrue(FString::Printf(TEXT("R %.0f km: outside its floor sphere"), Radius / Km),
                     Arrival.DistanceTo(Centre) > Radius + Floor);
        }

        // Another body on the line, its floor sphere over the standoff: the
        // arrival moves out along the line to its far side.
        const FUniversePosition Centre = Far + FVector(1.0e14, 0.0, 0.0);
        const FVector Back = (Far - Centre).GetSafeNormal();
        FFlightSurface Moon;
        Moon.Centre = Centre + Back * EarthStandoff + FVector(0.0, 1000.0 * Km, 0.0);
        Moon.Radius = 1700.0 * Km;
        Moon.Floor = 10.0 * Km;
        const FFlightSurface Others[] = { Moon };
        const FUniversePosition Pushed = NavStart::WorldArrivalPoint(Far, Centre, Earth, EarthFloor, 2.0, Others);
        TestTrue(TEXT("pushed: still on the line"), OffLine(Pushed, Far, Centre) < 1.0);
        TestTrue(TEXT("pushed: out of the other body's floor sphere"), Pushed.DistanceTo(Moon.Centre) > Moon.Radius + Moon.Floor);
        TestTrue(TEXT("pushed: farther out, never nearer"), Pushed.DistanceTo(Centre) > EarthStandoff);
        TestTrue(TEXT("pushed: and no farther than the sphere's far side and a metre"),
                 Pushed.DistanceTo(Moon.Centre) < Moon.Radius + Moon.Floor + 101.0);
        const FUniversePosition Unpushed = NavStart::WorldArrivalPoint(Far, Centre, Earth, EarthFloor, 2.0, {});
        TestEqual(TEXT("with nothing in the way, the standoff"), Unpushed.DistanceTo(Centre), EarthStandoff, 1.0);
    }

    // -- the subsystem, on the stock ship -------------------------------------
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("InSystemJumpWorld"));
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("and a universe"), Test.Universe))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const UUniverseSubsystem* Universe = Test.Universe;
    TestTrue(TEXT("the stock loadout installs"), StockShip::Install(Ship) > 0);
    FScopedCVar QuickTransit(TEXT("ds.Nav.TransitSeconds"), 6.0f);

    const auto Where = [Ship]() { return Ship->GetFlightState().GetUniversePosition(); };
    const TOptional<FStarSystem> Home = Universe->GetSystemAt(Where());
    if (!TestTrue(TEXT("the ship opens in a system of two worlds or more"), Home.IsSet() && Home->Planets.Num() >= 2))
    {
        return false;
    }
    const FSystemId HomeId = Home->Stub.Id;
    const int32 Orbit = Home->Planets.Num() - 1;
    const FWorldSeen Target = Seen(*Home, Orbit);
    const double Standoff = NavStart::WorldStandoffCm(Target.Radius, Target.Floor, UShipSubsystem::GetWorldStandoffDeg());
    const double Reach = NavStart::WorldReachFactor * Standoff;
    TestEqual(TEXT("ds.Nav.WorldStandoffDeg defaults to 2"), UShipSubsystem::GetWorldStandoffDeg(), 2.0, 1e-6);

    TestFalse(TEXT("with no target there is nothing to plot"), Ship->PlotTarget());
    TestTrue(TEXT("the outermost world is marked"), Ship->SetTarget(World(HomeId, Orbit)));

    // Inside the reach: refused, and the words say why.
    Ship->PlaceShip(Target.Centre + FVector(0.0, 0.0, 1.0) * (0.9 * Reach), FQuat::Identity);
    TestTrue(TEXT("inside twice the standoff, near enough to fly"), Ship->IsNearEnoughToFly(*Home, World(HomeId, Orbit)));
    TestFalse(TEXT("and an in-system jump is refused"), Ship->PlotTarget());
    Ship->PlaceShip(Target.Centre + FVector(0.0, 0.0, 1.0) * (1.1 * Reach), FQuat::Identity);
    TestFalse(TEXT("just outside it, not"), Ship->IsNearEnoughToFly(*Home, World(HomeId, Orbit)));

    // From well out, plotted by the console as a star is.
    const FVector Up(0.0, 0.0, 1.0);
    const FUniversePosition Start = Target.Centre + (Up * 0.6 + FVector(0.8, 0.0, 0.0)) * (20.0 * Reach);
    Ship->PlaceShip(Start, FQuat::Identity);
    Console(Test.World, TEXT("ds.Nav.Plot"), { TEXT("target") });
    TestTrue(TEXT("ds.Nav.Plot target plots the target as the course"), Ship->GetPlottedWorld() == TOptional<FBodyId>(World(HomeId, Orbit)));
    TestFalse(TEXT("which is no star course"), Ship->GetPlottedSystem().IsSet());
    const TOptional<FVector> Course = Ship->GetCourseDirection();
    if (TestTrue(TEXT("the course has a direction"), Course.IsSet()))
    {
        TestTrue(TEXT("toward the world's centre"), Course->Equals((Target.Centre - Start).GetSafeNormal(), 1e-9));
    }

    // A star replaces it, and the target replaces the star.
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (TestTrue(TEXT("the chart has somewhere to go"), Chart.Num() > 0))
    {
        TestTrue(TEXT("a star course replaces a world course"), Ship->PlotCourse(Chart[0].Id) && !Ship->GetPlottedWorld().IsSet());
        TestTrue(TEXT("and the target replaces the star"), Ship->PlotTarget() && !Ship->GetPlottedSystem().IsSet());
        TestTrue(TEXT("the star course let the target be"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, Orbit)));
    }

    // Cycling the target lets the course go; so does clearing it.
    TestTrue(TEXT("engaged"), Ship->SetJumpEngaged(true));
    Ship->CycleTarget();
    TestFalse(TEXT("cycling the target lets an in-system course go"), Ship->HasCourse());
    TestFalse(TEXT("and stands the jump down"), Ship->IsJumpEngaged());
    Ship->SetTarget(World(HomeId, Orbit));
    Ship->PlotTarget();
    Ship->ClearTarget();
    TestFalse(TEXT("clearing it too"), Ship->HasCourse());

    // A course flown inside the reach is let go, with the charge unspent.
    {
        Ship->SetTarget(World(HomeId, Orbit));
        Ship->PlotTarget();
        Ship->SetJumpEngaged(true);
        Console(Test.World, TEXT("ds.Nav.Charge"), {});
        // Nose away from the world, so it cannot fire while this is set up.
        Ship->PlaceShip(Start, FRotationMatrix::MakeFromX(-*Ship->GetCourseDirection()).ToQuat());
        Ship->Tick(0.1f);
        TestEqual(TEXT("charged"), Ship->GetJumpCharge(), 1.0f);
        TestTrue(TEXT("still plotted out here"), Ship->GetPlottedWorld().IsSet());
        Ship->PlaceShip(Target.Centre + Up * (0.9 * Reach), FRotationMatrix::MakeFromX(-Up).ToQuat());
        Ship->Tick(0.1f);
        TestFalse(TEXT("flown inside the reach, the course is let go"), Ship->HasCourse());
        TestFalse(TEXT("the jump with it"), Ship->IsJumpEngaged());
        TestFalse(TEXT("and it did not fold"), Ship->IsInTransit());
        TestEqual(TEXT("the charge is unspent"), Ship->GetJumpCharge(), 1.0f);
        TestTrue(TEXT("the target stays"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, Orbit)));
    }

    // The jump itself: spooled to 0.1 c on an open heading, then turned onto
    // the world 5 degrees off its centre -- inside the cone, and off the line
    // the ship coasts along in the fold, so an arrival worked out from where
    // the fold ended rather than where it opened would miss the line.
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->PlaceShip(Start, FQuat::Identity);
    TestTrue(TEXT("plotted again"), Ship->PlotTarget());
    TestTrue(TEXT("engaged"), Ship->SetJumpEngaged(true));
    const FVector ToWorld = (Target.Centre - Start).GetSafeNormal();
    // A heading whose path meets no floor, so nothing caps the spool-up.
    TOptional<FVector> Open;
    Ship->Tick(0.01f);
    for (const FVector& Candidate : { FVector::CrossProduct(ToWorld, FVector(0.0, 1.0, 0.0)).GetSafeNormal(), -ToWorld, Up, -Up,
                                      FVector(1.0, 0.0, 0.0), FVector(-1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), FVector(0.0, -1.0, 0.0) })
    {
        bool bMeetsNothing = true;
        for (const FFlightSurface& Surface : Ship->GetFlightState().GetSurfaces())
        {
            bMeetsNothing &= Surface.bInsideOut || !ShipFlight::RayToFloor(Surface, Start, Candidate).IsSet();
        }
        if (bMeetsNothing && !Open)
        {
            Open = Candidate;
        }
    }
    if (!TestTrue(TEXT("a heading from here meets nothing but the edge"), Open.IsSet()))
    {
        return false;
    }
    Ship->SetFlightCommand(Pilot, 0.25f, FVector::ZeroVector);
    Ship->SetDriveEngaged(Pilot, true);
    Ship->SetDriveLever(Pilot, Ship->GetFlightState().GetDriveNotchCount() - 1);
    for (int32 Tick = 0; Tick < 100; ++Tick)
    {
        Ship->PlaceShip(Start, FRotationMatrix::MakeFromX(*Open).ToQuat());
        Ship->Tick(0.1f);
    }
    TestFalse(TEXT("charged and misaligned, it holds"), Ship->IsInTransit());
    TestEqual(TEXT("ready"), Ship->GetJumpState(), EJumpState::Ready);
    TestTrue(FString::Printf(TEXT("at the drive's top (%.3f c)"), Ship->GetShipSpeed() / ShipDriveLever::LightCmPerSecond),
             Ship->GetShipSpeed() > 0.99 * 0.1 * ShipDriveLever::LightCmPerSecond);
    const FQuat Nose = FQuat(FVector(0.0, 1.0, 0.0).GetSafeNormal(), FMath::DegreesToRadians(5.0))
        * FRotationMatrix::MakeFromX(ToWorld).ToQuat();
    TestTrue(TEXT("5 degrees off the world's centre is inside the cone"),
             ShipNav::OffBoresight(Nose.UnrotateVector(ToWorld)) < Ship->GetJumpConeRadians());
    Ship->PlaceShip(Start, Nose);
    Ship->Tick(0.1f);
    if (!TestTrue(TEXT("aligned, engaged and charged, the fold opens by itself"), Ship->IsInTransit()))
    {
        return false;
    }
    const FUniversePosition Departure = Where();
    TestEqual(TEXT("opening it spends the charge: one charge for every jump"), Ship->GetJumpCharge(), 0.0f);
    TestTrue(TEXT("both levers to STOP"), Ship->GetFlightState().GetCommand().DriveNotch == 0
             && Ship->GetFlightState().GetCommand().Throttle == 0.0);
    TestTrue(TEXT("the target is kept"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, Orbit)));
    TestTrue(TEXT("and the course"), Ship->GetPlottedWorld().IsSet());
    TestEqual(TEXT("the jump's words: IN THE FOLD"), NavText::Jump(Ship->GetJumpState(), Ship->GetPlottedWorld().IsSet()),
              FString(TEXT("IN THE FOLD")));
    TestFalse(TEXT("the target cannot be changed in the fold"), Ship->SetTarget(World(HomeId, 0)));

    const int32 Serial = Ship->GetJumpSerial();
    const FQuat Held = Ship->GetFlightState().GetUniverseOrientation();
    for (int32 Tick = 0; Tick < 30 && Ship->GetTransitProgress() < 0.5; ++Tick)
    {
        Ship->Tick(0.25f);
    }
    AddInfo(FString::Printf(TEXT("half way through the fold the ship has coasted %.0f km"), Where().DistanceTo(Departure) / Km));
    TestTrue(FString::Printf(TEXT("the ship coasts in the fold (%.0f km from where it opened)"), Where().DistanceTo(Departure) / Km),
             Where().DistanceTo(Departure) > 1000.0 * Km);
    for (int32 Tick = 0; Tick < 100 && Ship->IsInTransit(); ++Tick)
    {
        Ship->Tick(0.25f);
    }
    TestFalse(TEXT("it arrives"), Ship->IsInTransit());
    TestEqual(TEXT("the serial counts it"), Ship->GetJumpSerial(), Serial + 1);

    const FUniversePosition Arrived = Where();
    TestTrue(FString::Printf(TEXT("on the line from where the fold opened to the world's centre, to a centimetre (%.3f cm off)"),
                             OffLine(Arrived, Departure, Target.Centre)),
             OffLine(Arrived, Departure, Target.Centre) < 1.0);
    TestEqual(TEXT("at the standoff from its centre, to a centimetre"), Arrived.DistanceTo(Target.Centre), Standoff, 1.0);
    const double Across = FMath::RadiansToDegrees(2.0 * FMath::Asin(Target.Radius / Arrived.DistanceTo(Target.Centre)));
    TestEqual(TEXT("where the world is two degrees across"), Across, 2.0, 1e-6);
    TestTrue(TEXT("the world within the cone of the nose"),
             ShipNav::OffBoresight(Ship->GetFlightState().GetUniverseOrientation().UnrotateVector(Target.Centre - Arrived))
                 <= Ship->GetJumpConeRadians());
    TestTrue(TEXT("nothing turned"), Ship->GetFlightState().GetUniverseOrientation().Equals(Held, 1e-12));
    TestTrue(TEXT("at rest, exactly"), Ship->GetFlightState().GetVelocity().IsZero());
    TestTrue(TEXT("both levers at STOP"), Ship->GetFlightState().GetCommand().DriveNotch == 0
             && Ship->GetFlightState().GetCommand().Throttle == 0.0);
    TestFalse(TEXT("the course is cleared"), Ship->HasCourse());
    TestFalse(TEXT("engage returns to off"), Ship->IsJumpEngaged());
    TestTrue(TEXT("the target is still set"), Ship->GetTarget() == TOptional<FBodyId>(World(HomeId, Orbit)));
    TestTrue(TEXT("in the same system"), Universe->GetSystemIdAt(Arrived) == TOptional<FSystemId>(HomeId));
    {
        bool bOutside = true;
        for (const FFlightSurface& Surface : OthersThan(*Home, Orbit))
        {
            bOutside &= Arrived.DistanceTo(Surface.Centre) > Surface.Radius + Surface.Floor;
        }
        TestTrue(TEXT("outside every other body's floor"), bOutside);
    }
    TestTrue(TEXT("near enough to fly now"), Ship->IsNearEnoughToFly(*Home, World(HomeId, Orbit)));
    TestFalse(TEXT("so it cannot be jumped to again from here"), Ship->PlotTarget());
    return true;
}

/**
 * The in-system jump as the player makes it, from the opening shot: the
 * outermost world marked through the map's own seam, Jump here pressed, the
 * charge filled, the nose turned onto it, and the fold opening by itself; out
 * of it at rest two degrees from the world, still marked, and from there the
 * drive from STOP to 0.1 c brings the ship down to its floor inside 85 s -- the
 * approach is still the player's, and it is about a minute.
 */
bool FLoopInSystemJumpTest::RunTest(const FString& Parameters)
{
    using namespace InSystemJumpTestLocal;
    using namespace SkyTestWorld;

    FSkyWorld Test(TEXT("LoopInSystemJumpWorld"));
    AShipMapScreen* Screen = Test.World ? Test.World->SpawnActor<AShipMapScreen>(FVector(1711.0, 0.0, 105.0), FRotator::ZeroRotator) : nullptr;
    if (!TestNotNull(TEXT("the map spawns before play"), Screen) || !TestNotNull(TEXT("the world has a ship"), Test.Ship))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    UWidgetComponent* Panel = Screen->GetScreen();
    Panel->TickComponent(0.016f, LEVELTICK_All, nullptr);
    USystemMapWidget* Map = Cast<USystemMapWidget>(Panel->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the map's widget"), Map))
    {
        return false;
    }
    const auto Where = [Ship]() { return Ship->GetFlightState().GetUniversePosition(); };
    const TOptional<FStarSystem> Home = Test.Universe->GetSystemAt(Where());
    if (!TestTrue(TEXT("the opening shot is in a system with worlds"), Home.IsSet() && !Home->Planets.IsEmpty()))
    {
        return false;
    }
    // The outermost world, unless the opening shot is already within its
    // reach, as it is when the largest world is the outermost.
    const int32 Orbit = FarWorld(*Ship, *Home);
    if (!TestTrue(TEXT("a world the opening shot is not near"), Orbit != INDEX_NONE))
    {
        return false;
    }
    const FWorldSeen Target = Seen(*Home, Orbit);

    Map->RefreshFromShip();
    Map->SelectWorld(Orbit);
    TestTrue(TEXT("the map marks the outermost world"), Ship->GetTarget() == TOptional<FBodyId>(World(Home->Stub.Id, Orbit)));
    if (!TestEqual(TEXT("from the opening shot it can be jumped to"), Map->GetJumpButtonText().ToString(), FString(TEXT("Jump here"))))
    {
        return false;
    }
    Map->PressJumpButton();
    TestTrue(TEXT("Jump here: plotted and engaged"), Ship->GetPlottedWorld().IsSet() && Ship->IsJumpEngaged());
    Console(Test.World, TEXT("ds.Nav.Charge"), {});
    Test.Step(0.1f);
    TestEqual(TEXT("ds.Nav.Charge fills it"), Ship->GetJumpState(), EJumpState::Ready);

    // Turned onto the world, and nothing else: the fold opens by itself.
    Ship->PlaceShip(Where(), FRotationMatrix::MakeFromX(*Ship->GetCourseDirection()).ToQuat());
    Test.Step(0.1f);
    TestTrue(TEXT("aligned, it folds with no confirm"), Ship->IsInTransit());
    TestEqual(TEXT("and the jump says IN THE FOLD"), NavText::Jump(Ship->GetJumpState(), Ship->GetPlottedWorld().IsSet()),
              FString(TEXT("IN THE FOLD")));
    Map->RefreshFromShip();
    TestEqual(TEXT("as the map does"), Map->GetFooterText().ToString(), FString(TEXT("In the fold.")));
    for (int32 Tick = 0; Tick < 200 && Ship->IsInTransit(); ++Tick)
    {
        Test.Step(0.1f);
    }
    TestFalse(TEXT("out of the fold"), Ship->IsInTransit());
    TestTrue(TEXT("at rest"), Ship->GetFlightState().GetVelocity().IsZero());
    const double Across = FMath::RadiansToDegrees(2.0 * FMath::Asin(Target.Radius / Where().DistanceTo(Target.Centre)));
    TestEqual(TEXT("two degrees from the world"), Across, 2.0, 1e-6);
    TestTrue(TEXT("with the target still set"), Ship->GetTarget() == TOptional<FBodyId>(World(Home->Stub.Id, Orbit)));
    Map->RefreshFromShip();
    TestTrue(TEXT("and the map naming it"), Map->GetTargetText().ToString().Contains(NavText::WorldName(Home->Planets[Orbit])));

    // The approach: the drive from STOP to 0.1 c, and the soft cap does the rest.
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetDriveEngaged(Pilot, true);
    Ship->SetDriveLever(Pilot, Ship->GetFlightState().GetDriveNotchCount() - 1);
    double Seconds = 0.0;
    constexpr float Step = 1.0f / 30.0f;
    const auto Room = [Ship, &Target, &Where]() { return Where().DistanceTo(Target.Centre) - Target.Radius - Target.Floor; };
    while (Seconds < 120.0 && Room() > 2.0 * FShipFlightState::AtFloorCm)
    {
        Ship->Tick(Step);
        Seconds += Step;
    }
    AddInfo(FString::Printf(TEXT("%s, %s: from the arrival to the floor at 0.1 c in %.1f s"), *NavText::WorldName(Home->Planets[Orbit]),
                            *NavText::WorldKind(Home->Planets[Orbit].Kind), Seconds));
    TestTrue(FString::Printf(TEXT("the drive at 0.1 c brings it to the world's floor within 85 s (%.1f s, %.1f m of room)"),
                             Seconds, Room() / 100.0),
             Seconds < 85.0 && Room() <= 2.0 * FShipFlightState::AtFloorCm);
    TestTrue(FString::Printf(TEXT("and it took about a minute, not a moment (%.1f s)"), Seconds), Seconds > 40.0);
    return true;
}

/**
 * The chart and an in-system course (decision 12): the chart lists and plots
 * only stars, and it is where the in-system jump is shown as the jump's
 * course -- named, "in this system", in the jump's bearing words -- with no
 * star row marked; its toggle stands that jump down and engages it again;
 * and a star row plotted there replaces it.
 */
bool FChartInSystemCourseTest::RunTest(const FString& Parameters)
{
    using namespace InSystemJumpTestLocal;
    using namespace SkyTestWorld;

    FSkyWorld Test(TEXT("ChartInSystemCourseWorld"));
    AShipNavScreen* Screen = Test.World ? Test.World->SpawnActor<AShipNavScreen>(FVector(1711.0, 85.0, 105.0), FRotator::ZeroRotator) : nullptr;
    if (!TestNotNull(TEXT("the chart spawns before play"), Screen) || !TestNotNull(TEXT("the world has a ship"), Test.Ship))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    Screen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);
    UNavigationWidget* Chart = Cast<UNavigationWidget>(Screen->GetScreen()->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the chart's widget"), Chart))
    {
        return false;
    }
    const TOptional<FStarSystem> Home = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
    if (!TestTrue(TEXT("a system with worlds"), Home.IsSet() && !Home->Planets.IsEmpty()))
    {
        return false;
    }
    const int32 Orbit = FarWorld(*Ship, *Home);
    Ship->SetTarget(World(Home->Stub.Id, Orbit));
    if (!TestTrue(TEXT("the target plotted as the course"), Orbit != INDEX_NONE && Ship->PlotTarget()))
    {
        return false;
    }
    Chart->RefreshFromShip();

    const FString Expected = FString(UNavigationWidget::PlottedMark) + TEXT(" ") + NavText::WorldName(Home->Planets[Orbit])
        + NavText::Separator + UNavigationWidget::InSystemWords + NavText::Separator
        + NavText::Bearing(*Ship->GetCourseDirectionShipLocal(), Ship->GetJumpConeRadians()) + TEXT(".");
    TestEqual(TEXT("the course line names the world, in this system, in the jump's bearing words"),
              Chart->GetCourseText().ToString(), Expected);
    bool bNoneMarked = true;
    for (int32 Row = 0; Row < Chart->GetShownRowCount(); ++Row)
    {
        bNoneMarked &= !Chart->GetRowText(Row).ToString().StartsWith(UNavigationWidget::PlottedMark);
    }
    TestTrue(TEXT("no star row carries the mark"), Chart->GetShownRowCount() > 0 && bNoneMarked);

    // Turned, the bearing follows, as a star course's does.
    Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(*Ship->GetCourseDirection()).ToQuat());
    Chart->RefreshFromShip();
    TestTrue(TEXT("aligned, the course line says dead ahead"), Chart->GetCourseText().ToString().EndsWith(TEXT("dead ahead.")));

    TestTrue(TEXT("the toggle can be pressed for a world course"), Chart->IsEngageEnabled());
    Chart->PressEngage();
    TestTrue(TEXT("and engages the in-system jump"), Ship->IsJumpEngaged());
    TestEqual(TEXT("it offers to stand it down"), Chart->GetEngageLabel().ToString(), FString(TEXT("STAND DOWN")));
    Chart->PressEngage();
    TestFalse(TEXT("which it does"), Ship->IsJumpEngaged());
    TestTrue(TEXT("leaving the course"), Ship->GetPlottedWorld().IsSet());
    Chart->PressEngage();
    TestTrue(TEXT("and engages it again"), Ship->IsJumpEngaged());

    Chart->SelectRow(0);
    TestTrue(TEXT("a star row plotted replaces the in-system course"), Ship->GetPlottedSystem().IsSet() && !Ship->GetPlottedWorld().IsSet());
    TestTrue(TEXT("and the row is marked"), Chart->GetRowText(0).ToString().StartsWith(UNavigationWidget::PlottedMark));
    TestFalse(TEXT("the course line no longer says in this system"), Chart->GetCourseText().ToString().Contains(UNavigationWidget::InSystemWords));
    TestTrue(TEXT("the target was left alone"), Ship->GetTarget() == TOptional<FBodyId>(World(Home->Stub.Id, Orbit)));
    TestTrue(TEXT("and the jump stays engaged, pending the developer's ruling (map spec, Open questions)"), Ship->IsJumpEngaged());

    // Through the fold (decision 12): an in-system fold is not between
    // stars. The chart keeps the system the ship is still in -- its here
    // line and its rows -- and its jump word is the fold's.
    Ship->SetJumpEngaged(false);
    const FString HereBefore = Chart->GetHereText().ToString();
    const int32 RowsBefore = Chart->GetShownRowCount();
    TestNotEqual(TEXT("before the fold the chart says where the ship is"), HereBefore, NavText::JumpWord(EJumpState::Transit));
    TestTrue(TEXT("the target plotted again"), Ship->PlotTarget());
    TestTrue(TEXT("and engaged"), Ship->SetJumpEngaged(true));
    Console(Test.World, TEXT("ds.Nav.Charge"), {});
    Test.Step(0.1f);
    Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(*Ship->GetCourseDirection()).ToQuat());
    Test.Step(0.1f);
    if (!TestTrue(TEXT("aligned and charged, the in-system fold opens"), Ship->IsInTransit()))
    {
        return false;
    }
    Chart->RefreshFromShip();
    TestEqual(TEXT("in the fold the here line still names the system"), Chart->GetHereText().ToString(), HereBefore);
    TestEqual(TEXT("its rows are still shown"), Chart->GetShownRowCount(), RowsBefore);
    TestTrue(TEXT("and there are some"), RowsBefore > 0);
    TestEqual(TEXT("and the jump word is the fold's, not between stars"), Chart->GetJumpText().ToString(), FString(TEXT("In the fold.")));
    return true;
}

#endif
