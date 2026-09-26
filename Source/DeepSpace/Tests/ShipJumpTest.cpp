#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/NavStart.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkySystem.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// One test and no children: a test path with children becomes a group, and a
// group silently stops running its own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipJumpTest,
    "DeepSpace.Ship.Jump",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** A console variable moved for the length of a scope, then put back, so
     *  a test that tunes one cannot leak it into the next. */
    struct FScopedCVar
    {
        IConsoleVariable* Variable = nullptr;
        FString Previous;

        FScopedCVar(const TCHAR* Name, float Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name))
        {
            if (Variable)
            {
                Previous = Variable->GetString();
                Variable->Set(*FString::SanitizeFloat(Value), ECVF_SetByCode);
            }
        }

        ~FScopedCVar()
        {
            if (Variable)
            {
                Variable->Set(*Previous, ECVF_SetByCode);
            }
        }
    };

    UWorld* MakeWorld(const TCHAR* Name)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        return World;
    }

    void DestroyWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    void BeginPlay(UWorld* World)
    {
        World->InitializeActorsForPlay(FURL());
        World->BeginPlay();
    }

    /** Ticks until Done or Limit seconds, returning the seconds it took (Limit
     *  if it never happened). */
    template <typename FDone>
    double TickUntil(UShipSubsystem* Ship, double Limit, double Step, FDone Done)
    {
        double Elapsed = 0.0;
        while (Elapsed < Limit && !Done())
        {
            Ship->Tick(static_cast<float>(Step));
            Elapsed += Step;
        }
        return Elapsed;
    }

    /** Facing Dir, rolled, so a jump that turned the ship at all -- roll
     *  included -- would show. */
    FQuat FacingRolled(const FVector& Dir)
    {
        return FRotationMatrix::MakeFromX(Dir).ToQuat() * FQuat(FVector::ForwardVector, 0.7);
    }

    double OutermostOrbitAU(const FStarSystem& System)
    {
        double Outermost = 0.0;
        for (const FPlanet& Planet : System.Planets)
        {
            Outermost = FMath::Max(Outermost, Planet.SemiMajorAxisAU);
        }
        return Outermost;
    }
}

/**
 * The jump, end to end, in a world the way the game builds one: the ship
 * placed at the opening shot by begin-play, a course plotted from the chart,
 * the drive wound on power the split decides, aimed, and left to fire by
 * itself. What it checks is what the loop promises the player -- that it
 * never costs anything to stay, never leaks, always finishes, and arrives
 * somewhere without turning the ship.
 */
bool FShipJumpTest::RunTest(const FString& Parameters)
{
    const double Km = UniverseUnits::CmPerKm;
    const double AU = UniverseUnits::CmPerAU;

    // ds.Nav.PlaceAtStart 0 leaves the ship where it was.
    {
        FScopedCVar NoPlacement(TEXT("ds.Nav.PlaceAtStart"), 0.0f);
        UWorld* World = MakeWorld(TEXT("JumpTestUnplacedWorld"));
        BeginPlay(World);
        const UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
        if (TestNotNull(TEXT("an unplaced world has a ship"), Ship))
        {
            TestTrue(TEXT("with placement off the ship stays at the origin"),
                     Ship->GetFlightState().GetUniversePosition() == FUniversePosition());
        }
        DestroyWorld(World);
    }

    UWorld* World = MakeWorld(TEXT("JumpTestWorld"));
    BeginPlay(World);
    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>();
    if (!TestNotNull(TEXT("the world has a ship"), Ship) || !TestNotNull(TEXT("and a universe"), Universe))
    {
        DestroyWorld(World);
        return false;
    }

    // The opening shot: placed once, at begin-play, by the ship subsystem
    // and nothing else (plan conflict 3), in the start system.
    const FSystemId Start = Universe->GetStartSystem();
    const TOptional<FStarSystem> Home = Universe->GetSystem(Start);
    if (!TestTrue(TEXT("the universe has a start system"), Home.IsSet()))
    {
        DestroyWorld(World);
        return false;
    }
    {
        const FNavPlacement Opening = NavStart::OpeningPlacement(*Home);
        const FShipFlightState& Flight = Ship->GetFlightState();
        TestTrue(TEXT("begin-play puts the ship at the opening shot, to 1 cm"),
                 Flight.GetUniversePosition().DistanceTo(Opening.Position) < 1.0);
        TestTrue(TEXT("facing the way the opening shot faces"),
                 Flight.GetUniverseOrientation().Equals(Opening.Orientation, 1e-12));

        const TOptional<FStarSystem> Here = Universe->GetSystemAt(Flight.GetUniversePosition());
        TestTrue(TEXT("which is in the start system"), Here.IsSet() && Here->Stub.Id == Start);
        TestTrue(TEXT("and the start system is visited"), Ship->HasVisited(Start));

        // The sky's seam asks the same owners and holds nothing.
        const FSkySystem Sky = LocalSystem::Current(World);
        TestTrue(TEXT("LocalSystem's system is the start system"), Sky.SystemId == FName(*Home->Stub.Name));
        TestEqual(TEXT("with its star and every planet"), Sky.Bodies.Num(), 1 + Home->Planets.Num());
        TestTrue(TEXT("and neighbours to draw"), Sky.Neighbours.Num() > 0);
        TestEqual(TEXT("LocalSystem's serial is the jump serial"), LocalSystem::Serial(World), Ship->GetJumpSerial());
        TestFalse(TEXT("and it is not in transit"), LocalSystem::InTransit(World));
    }

    // Staying put costs nothing: an idle drive wants no power at all.
    Ship->Tick(0.1f);
    TestEqual(TEXT("an idle drive wants 0 W"), Ship->GetConsumerWant(ShipPower::Engine), 0.0f);
    TestEqual(TEXT("and is idle"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Idle));

    // The chart, and choosing from it.
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (!TestTrue(TEXT("the chart has somewhere to go"), Chart.Num() > 0))
    {
        DestroyWorld(World);
        return false;
    }
    {
        const FUniversePosition Here = Ship->GetFlightState().GetUniversePosition();
        bool bExcludesHere = true;
        bool bNearestFirst = true;
        for (int32 Index = 0; Index < Chart.Num(); ++Index)
        {
            bExcludesHere &= Chart[Index].Id != Start;
            if (Index > 0)
            {
                bNearestFirst &= Here.DistanceTo(Chart[Index - 1].Position) <= Here.DistanceTo(Chart[Index].Position);
            }
        }
        TestTrue(TEXT("the chart leaves out the system the ship is in"), bExcludesHere);
        TestTrue(TEXT("nearest first"), bNearestFirst);
    }
    TestFalse(TEXT("plotting the system the ship is in is refused"), Ship->PlotCourse(Start));
    TestFalse(TEXT("so is engaging with no course"), Ship->SetJumpEngaged(true));

    // Plotted through the console, as pass A's chart is.
    {
        FOutputDeviceNull Quiet;
        IConsoleObject* Plot = IConsoleManager::Get().FindConsoleObject(TEXT("ds.Nav.Plot"));
        if (TestNotNull(TEXT("ds.Nav.Plot exists"), Plot) && Plot->AsCommand())
        {
            Plot->AsCommand()->Execute({ TEXT("0") }, World, Quiet);
        }
    }
    const FSystemId Destination = Chart[0].Id;
    TestTrue(TEXT("ds.Nav.Plot 0 plots the nearest system"),
             Ship->GetPlottedSystem().IsSet() && *Ship->GetPlottedSystem() == Destination);
    const TOptional<FVector> CourseDir = Ship->GetCourseDirection();
    if (!TestTrue(TEXT("a plotted course has a direction"), CourseDir.IsSet()))
    {
        DestroyWorld(World);
        return false;
    }

    // Pointed well away from the course, so the charged drive has to wait.
    const FUniversePosition Parked = Ship->GetFlightState().GetUniversePosition();
    Ship->PlaceShip(Parked, FacingRolled(-*CourseDir));

    TestTrue(TEXT("with a course the jump engages"), Ship->SetJumpEngaged(true));
    Ship->Tick(0.1f);
    TestEqual(TEXT("and winds"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Winding));
    TestEqual(TEXT("wanting ds.Nav.WindingWant"), Ship->GetConsumerWant(ShipPower::Engine), 800.0f);

    // What the split does while it winds (nav decision 4, admitted): at the
    // default 1:1:1 the lights never notice, since their third of the
    // reactor is more than they want; leaning on the drive dims them.
    TestEqual(TEXT("at 1:1:1 the lights stay fully fed while it winds"),
              Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
    Ship->SetConsumerWeight(ShipPower::Engine, 4.0f);
    Ship->Tick(0.1f);
    TestTrue(TEXT("at 1:1:4 they dim while it winds"), Ship->GetConsumerSatisfaction(ShipPower::Lights) < 0.9f);
    Ship->SetConsumerWeight(ShipPower::Engine, 1.0f);
    {
        // The fold's draw, off the top, is the only lever that dims them at
        // the default split -- and it goes when the winding does.
        FScopedCVar Fold(TEXT("ds.Nav.FoldDraw"), 350.0f);
        Ship->Tick(0.1f);
        TestTrue(TEXT("ds.Nav.FoldDraw 350 dims the lights at 1:1:1"),
                 Ship->GetConsumerSatisfaction(ShipPower::Lights) < 0.9f);
    }
    Ship->Tick(0.1f);
    TestEqual(TEXT("and with no fold draw they are fed again"),
              Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);

    // A weight of zero is a legitimate way to live: the drive still finishes,
    // only slower -- ChargeSeconds / StarvedRate at worst.
    Ship->SetConsumerWeight(ShipPower::Engine, 0.0f);
    const double Starved = TickUntil(Ship, 600.0, 1.0, [Ship] { return Ship->GetJumpCharge() >= 1.0f; });
    TestTrue(TEXT("a zero-weight engine still reaches full charge"), Ship->GetJumpCharge() >= 1.0f);
    TestTrue(TEXT("slower than a fed one would"), Starved > 90.0);
    TestTrue(TEXT("and within ChargeSeconds / StarvedRate"), Starved <= 90.0 / 0.2 + 1.0);

    // Charged and misaligned, it holds at ready for as long as it takes, and
    // nothing escalates. Holding, it wants nothing, so the lights recover.
    TestEqual(TEXT("charged, it is ready"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Ready));
    Ship->SetConsumerWeight(ShipPower::Engine, 4.0f);
    TickUntil(Ship, 600.0, 5.0, [] { return false; });
    TestFalse(TEXT("misaligned, ten minutes on, it has not jumped"), Ship->IsInTransit());
    TestEqual(TEXT("it is still ready"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Ready));
    TestEqual(TEXT("and the serial has not moved"), Ship->GetJumpSerial(), 0);
    TestEqual(TEXT("at ready the drive wants nothing"), Ship->GetConsumerWant(ShipPower::Engine), 0.0f);
    TestEqual(TEXT("so at 1:1:4 the lights recover"), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
    Ship->SetConsumerWeight(ShipPower::Engine, 1.0f);

    // Aimed, and under way at a quarter throttle: the arrival must keep
    // everything but the position.
    APawn* Pilot = World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetFlightCommand(Pilot, 0.0f, FVector::ZeroVector);
    Ship->PlaceShip(Parked, FacingRolled(*Ship->GetCourseDirection()));

    // The fold opens on the tick it is aligned, with nobody at the helm and
    // no confirm: the throttle is set just after, as a pilot walking away
    // from a cruise would have left it.
    Ship->Tick(0.1f);
    TestTrue(TEXT("aligned, engaged and charged, the jump fires by itself"), Ship->IsInTransit());
    TestEqual(TEXT("opening the fold spends the charge"), Ship->GetJumpCharge(), 0.0f);
    TestTrue(TEXT("LocalSystem says so"), LocalSystem::InTransit(World));
    TestFalse(TEXT("a course cannot be plotted between stars"), Ship->PlotCourse(Chart.Last().Id));
    TestFalse(TEXT("nor the jump stood down"), Ship->SetJumpEngaged(false));

    Ship->SetFlightCommand(Pilot, 0.25f, FVector::ZeroVector);
    Ship->Tick(1.0f);
    TestEqual(TEXT("between stars the drive has no room"), Ship->GetFlightState().GetDriveRoom(), 0.0);
    TestTrue(TEXT("the transit is part way"), Ship->GetTransitProgress() > 0.0 && Ship->GetTransitProgress() < 1.0);

    // The helm does nothing between stars.
    Ship->SetFlightCommand(Pilot, 0.25f, FVector(0.0, 1.0, 0.0));
    Ship->Tick(0.5f);
    TestTrue(TEXT("attitude input between stars turns nothing"),
             Ship->GetFlightState().GetAngularVelocity().IsNearlyZero(1e-12));
    Ship->SetFlightCommand(Pilot, 0.25f, FVector::ZeroVector);

    // Steady at cruise now; this is what arrival must not touch.
    const FQuat Orientation = Ship->GetFlightState().GetUniverseOrientation();
    const FVector Velocity = Ship->GetFlightState().GetVelocity();
    TestTrue(TEXT("the ship is under way into the arrival"), Velocity.Size() > 1000.0);

    TickUntil(Ship, 30.0, 0.25, [Ship] { return Ship->GetJumpSerial() > 0; });
    TestEqual(TEXT("it arrives, and the serial counts it"), Ship->GetJumpSerial(), 1);
    TestEqual(TEXT("LocalSystem's serial follows"), LocalSystem::Serial(World), 1);
    TestFalse(TEXT("no longer in transit"), Ship->IsInTransit());

    const FShipFlightState& Flight = Ship->GetFlightState();
    const FUniversePosition Arrived = Flight.GetUniversePosition();

    // Which system the ship is in changed by itself: asked of the position,
    // recorded nowhere (plan conflict 1).
    const TOptional<FStarSystem> There = Universe->GetSystemAt(Arrived);
    TestTrue(TEXT("the ship is in the destination system"), There.IsSet() && There->Stub.Id == Destination);
    TestTrue(TEXT("and LocalSystem draws it"), LocalSystem::Current(World).SystemId == FName(*Chart[0].Name));

    // The standoff, against conflict 9's rule written out here rather than
    // asked of NavStart: constant irradiance, outside every orbit, and never
    // inside ten stellar radii.
    const TOptional<FStarSystem> Target = Universe->GetSystem(Destination);
    if (TestTrue(TEXT("the destination generates"), Target.IsSet()))
    {
        // The CVar as play has it: a float, so 2.4 is 2.4000001, which is ten
        // kilometres at two AU.
        const IConsoleVariable* StandoffAU = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Nav.StandoffAU"));
        const double Tuned = StandoffAU ? StandoffAU->GetFloat() : NavStart::DefaultStandoffAU;
        TestTrue(TEXT("ds.Nav.StandoffAU defaults to 2.4"), FMath::IsNearlyEqual(Tuned, 2.4, 1e-6));
        const double Rule = FMath::Max3(
            Tuned * FMath::Sqrt(Target->Star.LuminositySolar),
            1.5 * OutermostOrbitAU(*Target),
            10.0 * Target->Star.RadiusSolar * UniverseUnits::CmPerSolarRadius / AU);
        const double Standoff = Arrived.DistanceTo(Target->Stub.Position);
        TestTrue(FString::Printf(TEXT("the standoff is max(2.4 AU x sqrt(L), 1.5 x outermost orbit), to 1 km: %.4f AU against %.4f AU"),
                                 Standoff / AU, Rule),
                 FMath::Abs(Standoff - Rule * AU) <= 1.0 * Km);
        TestTrue(TEXT("arriving outside every planet's orbit"), Standoff > OutermostOrbitAU(*Target) * AU);

        const FVector ToStar = Flight.GetUniverseOrientation().UnrotateVector(Target->Stub.Position - Arrived);
        TestTrue(TEXT("the new star is inside the cone of the nose"),
                 ShipNav::OffBoresight(ToStar) <= Ship->GetJumpConeRadians());
    }

    // A translation and nothing else (nav decision 5).
    TestTrue(TEXT("the orientation is unchanged"), Flight.GetUniverseOrientation().Equals(Orientation, 1e-12));
    TestTrue(TEXT("so the up vector has not moved"),
             Flight.GetUniverseOrientation().GetUpVector().Equals(Orientation.GetUpVector(), 1e-12));
    TestTrue(TEXT("and the velocity is unchanged"), Flight.GetVelocity().Equals(Velocity, 1e-6));
    TestEqual(TEXT("the charge is spent"), Ship->GetJumpCharge(), 0.0f);

    // Engage was a one-shot "go", and the course clears because you are there.
    TestFalse(TEXT("engage returns to off"), Ship->IsJumpEngaged());
    TestFalse(TEXT("the course is cleared"), Ship->GetPlottedSystem().IsSet());
    TestTrue(TEXT("the destination is visited"), Ship->HasVisited(Destination));
    TestEqual(TEXT("and the drive is idle"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Idle));

    // Decision 3's claim: a full-rate turn released as the HUD first says
    // "dead ahead" stops inside the cone, because the overshoot is less than
    // the cone's full width.
    {
        const FShipFlightLimits Limits = FShipFlightLimits::Cruise();
        for (int32 Axis = 0; Axis < 2; ++Axis)
        {
            const double Overshoot = FMath::Square(Limits.MaxAngularRate[Axis]) / (2.0 * Limits.AngularAcceleration[Axis]);
            TestTrue(FString::Printf(TEXT("axis %d's release overshoot is less than the cone's full width"), Axis),
                     Overshoot < 2.0 * Ship->GetJumpConeRadians());
        }
    }

    // ds.Nav.Charge fills the drive -- on the next tick, through the tick.
    {
        FOutputDeviceNull Quiet;
        IConsoleObject* Charge = IConsoleManager::Get().FindConsoleObject(TEXT("ds.Nav.Charge"));
        if (TestNotNull(TEXT("ds.Nav.Charge exists"), Charge) && Charge->AsCommand())
        {
            Charge->AsCommand()->Execute({}, World, Quiet);
        }
        TestEqual(TEXT("the command itself writes nothing"), Ship->GetJumpCharge(), 0.0f);
        Ship->Tick(0.01f);
        TestEqual(TEXT("ds.Nav.Charge fills the drive on the next tick"), Ship->GetJumpCharge(), 1.0f);
    }

    DestroyWorld(World);
    return true;
}

#endif
