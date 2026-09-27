#include "Ship/ShipSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Ship/NavStart.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "UI/NavText.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

namespace
{
    // Every tunable the playtest moves is a console variable, read where it
    // is used and never cached: Linux has no Live Coding, and a header
    // constant would make each nudge a rebuild (nav decision 6). The defaults
    // are the starting values, to be written back once play has settled them.

    TAutoConsoleVariable<float> CVarChargeSeconds(
        TEXT("ds.Nav.ChargeSeconds"), static_cast<float>(FShipFlightState::JumpChargeSeconds),
        TEXT("Seconds for the jump to wind from cold with the engine fully fed."),
        ECVF_Default);

    // 380 W: small enough that an engine-first split winds at full speed on
    // the stock hauler, which has 780 W left once its modules draw off the
    // top. At the old 800 W against a 1000 W reactor the best any split
    // reached was 48% fed, and ds.Nav.ChargeSeconds was a number no player
    // could ever see. DeepSpace.Ship.JumpCanWindAtFullSpeed holds it there.
    TAutoConsoleVariable<float> CVarWindingWant(
        TEXT("ds.Nav.WindingWant"), 380.0f,
        TEXT("Watts the engine asks for while the jump winds. It asks for nothing otherwise."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarStarvedRate(
        TEXT("ds.Nav.StarvedRate"), 0.2f,
        TEXT("Fraction of the full winding rate a completely starved engine still manages."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarFoldDraw(
        TEXT("ds.Nav.FoldDraw"), 0.0f,
        TEXT("Watts taken off the top of the reactor while the jump winds (0: none)."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarTransitSeconds(
        TEXT("ds.Nav.TransitSeconds"), static_cast<float>(FNavTuning().TransitSeconds),
        TEXT("Seconds between stars."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarConeDeg(
        TEXT("ds.Nav.ConeDeg"), 8.0f,
        TEXT("Half-angle, degrees, of the cone round the nose the course must be in for the jump to fire."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarStandoffAU(
        TEXT("ds.Nav.StandoffAU"), static_cast<float>(NavStart::DefaultStandoffAU),
        TEXT("Arrival distance from a Sun-like star, AU; scaled by the square root of the star's luminosity."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarRangeLy(
        TEXT("ds.Nav.RangeLy"), 12.0f,
        TEXT("How far the chart reaches, light years."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarPlaceAtStart(
        TEXT("ds.Nav.PlaceAtStart"), 1,
        TEXT("Place the ship at the opening shot when the world begins play (0: leave it where it is)."),
        ECVF_Default);

    // The drive and the soft cap (flight-feel decisions 3-6). Each default
    // is the pure layer's named constant, so a test and a CVar can never
    // disagree about what "the default" is.

    TAutoConsoleVariable<float> CVarDriveTop(
        TEXT("ds.Drive.Top"), static_cast<float>(ShipDriveLever::DefaultTopLight),
        TEXT("The drive lever's top, in c: 1 at most (anything faster is a jump), 1 km/s at least. Shortens the lever; never lengthens it."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarDriveResponse(
        TEXT("ds.Drive.Response"), static_cast<float>(ShipDriveLever::DefaultResponse),
        TEXT("Notches a second the drive's speed may move at full thrust. Thin boosters slow the whole ease, never this."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarDriveSweep(
        TEXT("ds.Drive.Sweep"), static_cast<float>(ShipDriveLever::DefaultSweep),
        TEXT("Notches a second a held lever key repeats at under the drive, after a moment."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarHoldSeconds(
        TEXT("ds.Drive.HoldSeconds"), static_cast<float>(ShipFlight::DefaultHoldSeconds),
        TEXT("The soft cap: the nose's path meeting a floor within this many seconds is held off, the distance falling by e each. 0 or less: the braking curve alone."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarFlightFloorKm(
        TEXT("ds.Flight.Floor"), static_cast<float>(ShipFlight::DefaultFloorCm / UniverseUnits::CmPerKm),
        TEXT("The lowest the ship goes over a world, km, and how far inside the system's edge it stops. Never under the sky's own rendered floor."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarStarFloorRadii(
        TEXT("ds.Flight.StarFloorRadii"), static_cast<float>(ShipFlight::DefaultStarFloorRadii),
        TEXT("The lowest the ship goes over a star, in its radii."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarCruiseSweep(
        TEXT("ds.Cruise.Sweep"), static_cast<float>(ShipDriveLever::DefaultCruiseSweep),
        TEXT("How fast a held lever key sweeps the cruise lever, fraction a second."),
        ECVF_Default);

    /** The name the fold's draw goes on the reactor under. Not a module: a
     *  draw that comes and goes, which the power model's draws are not meant
     *  to be (nav *Fakes*). */
    const FName FoldDrawId(TEXT("Nav.Fold"));

    /**
     * ds.Nav.Charge's request: the world whose drive should be full on its
     * next tick. The command only asks; the charge is still written by the
     * tick, through ChargeJumpDrive, so a debug tool opens no write path of
     * its own into the flight state.
     */
    TWeakObjectPtr<UWorld> ChargeRequestedFor;

    UShipSubsystem* ShipIn(UWorld* World, FOutputDevice& Out, const TCHAR* Command)
    {
        UShipSubsystem* Ship = UShipSubsystem::Get(World);
        if (!Ship)
        {
            Out.Logf(TEXT("%s: no ship in this world"), Command);
        }
        return Ship;
    }

    void NavNear(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        const UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Nav.Near"));
        if (!Ship)
        {
            return;
        }
        const FUniversePosition Here = Ship->GetFlightState().GetUniversePosition();
        const TArray<FStarSystemStub> Chart = Ship->GetChart();
        const TOptional<FSystemId> Plotted = Ship->GetPlottedSystem();
        Out.Logf(TEXT("%d systems within %.1f ly. Plot one with ds.Nav.Plot <n>."),
                 Chart.Num(), UShipSubsystem::GetChartRangeLy());
        for (int32 Index = 0; Index < Chart.Num(); ++Index)
        {
            const FStarSystemStub& Stub = Chart[Index];
            // Worded by NavText, as the chart words the same row; only the
            // columns are the console's.
            FString Line = FString::Printf(TEXT("%2d  %-12s %8s  %s"), Index, *Stub.Name,
                *NavText::Distance(Here.DistanceTo(Stub.Position)), *NavText::StarClass(Stub.Class));
            if (Ship->HasVisited(Stub.Id))
            {
                Line += TEXT("  ") + NavText::Visited(true);
            }
            if (Plotted && *Plotted == Stub.Id)
            {
                Line += TEXT("  <- course");
            }
            Out.Log(Line);
        }
    }

    void NavPlot(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Nav.Plot"));
        if (!Ship)
        {
            return;
        }
        const TArray<FStarSystemStub> Chart = Ship->GetChart();
        const int32 Index = Args.IsEmpty() ? INDEX_NONE : FCString::Atoi(*Args[0]);
        if (Args.IsEmpty() || !Chart.IsValidIndex(Index))
        {
            Out.Logf(TEXT("ds.Nav.Plot <n>: n is a row of ds.Nav.Near, 0 to %d"), Chart.Num() - 1);
            return;
        }
        if (Ship->PlotCourse(Chart[Index].Id))
        {
            Out.Logf(TEXT("Course plotted: %s. Engage with ds.Nav.Engage."), *Chart[Index].Name);
        }
        else
        {
            Out.Logf(TEXT("Cannot plot %s now."), *Chart[Index].Name);
        }
    }

    void NavClear(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        if (UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Nav.Clear")))
        {
            Ship->ClearCourse();
            Out.Log(Ship->GetPlottedSystem() ? TEXT("Between stars: the course stands.") : TEXT("Course cleared."));
        }
    }

    void NavEngage(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        if (UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Nav.Engage")))
        {
            const bool bOn = Args.IsEmpty() || FCString::Atoi(*Args[0]) != 0;
            if (Ship->SetJumpEngaged(bOn))
            {
                Out.Log(NavText::Jump(Ship->GetJumpState()));
            }
            else
            {
                Out.Log(bOn ? TEXT("Nothing to engage: plot a course first.") : TEXT("Between stars."));
            }
        }
    }

    void NavCharge(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        if (ShipIn(World, Out, TEXT("ds.Nav.Charge")))
        {
            ChargeRequestedFor = World;
            Out.Log(TEXT("The jump is charged on the next tick."));
        }
    }

    // Pass A's chart is these commands (nav decision 6); once the chart chair
    // exists they stay as debug tools, ungated, like ds.Sky.*.
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavNearCommand(
        TEXT("ds.Nav.Near"), TEXT("The chart: every system in range, numbered, nearest first."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavNear));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavPlotCommand(
        TEXT("ds.Nav.Plot"), TEXT("'ds.Nav.Plot <n>': plot a course to row n of ds.Nav.Near."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavPlot));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavClearCommand(
        TEXT("ds.Nav.Clear"), TEXT("Clear the course, which also stands the jump down."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavClear));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavEngageCommand(
        TEXT("ds.Nav.Engage"), TEXT("'ds.Nav.Engage [0|1]': engage the jump (default) or stand it down."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavEngage));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavChargeCommand(
        TEXT("ds.Nav.Charge"), TEXT("Fill the jump's charge on the next tick, for running the loop in seconds."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavCharge));
}

UShipSubsystem* UShipSubsystem::Get(const UObject* WorldContext)
{
    if (!WorldContext)
    {
        return nullptr;
    }
    const UWorld* World = WorldContext->GetWorld();
    return World ? World->GetSubsystem<UShipSubsystem>() : nullptr;
}

void UShipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UUniverseSubsystem>();
    Super::Initialize(Collection);
    PowerState.SetReactorOutput(DefaultReactorOutput);

    // An even split to start with, which is a starting point and not a
    // recommendation: every split is viable and none is correct. The engine
    // starts idle, wanting nothing, and so takes part in no split until the
    // jump is engaged.
    PowerState.SetConsumer(ShipPower::Lights, LightsWant, 1.0f);
    PowerState.SetConsumer(ShipPower::Boosters, BoostersWant, 1.0f);
    PowerState.SetConsumer(ShipPower::Engine, 0.0f, 1.0f);
}

void UShipSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    if (!InWorld.IsGameWorld() || CVarPlaceAtStart.GetValueOnGameThread() == 0)
    {
        return;
    }
    const UUniverseSubsystem* Cosmos = InWorld.GetSubsystem<UUniverseSubsystem>();
    if (!Cosmos)
    {
        return;
    }

    // Before any actor's BeginPlay, so the counter-frame scatters its motes
    // and the sky builds its first system around where the ship opens, not
    // around the origin it was at a moment before.
    const FSystemId Start = Cosmos->GetStartSystem();
    if (const TOptional<FStarSystem> Home = Cosmos->GetSystem(Start))
    {
        const FNavPlacement Opening = NavStart::OpeningPlacement(*Home);
        PlaceShip(Opening.Position, Opening.Orientation);
        NavState.MarkVisited(Start);
    }
}

const UUniverseSubsystem* UShipSubsystem::Universe() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UUniverseSubsystem>() : nullptr;
}

TArray<FName> UShipSubsystem::GetPowerConsumers() const
{
    return PowerState.GetConsumers();
}

float UShipSubsystem::GetConsumerWeight(FName ConsumerId) const
{
    return PowerState.GetWeight(ConsumerId);
}

void UShipSubsystem::SetConsumerWeight(FName ConsumerId, float Weight)
{
    PowerState.SetWeight(ConsumerId, Weight);
}

float UShipSubsystem::GetConsumerShare(FName ConsumerId) const
{
    return PowerState.GetShare(ConsumerId);
}

float UShipSubsystem::GetConsumerWant(FName ConsumerId) const
{
    return PowerState.GetWant(ConsumerId);
}

float UShipSubsystem::GetConsumerSatisfaction(FName ConsumerId) const
{
    return PowerState.GetSatisfaction(ConsumerId);
}

bool UShipSubsystem::AreLightsOn() const
{
    return bLightsOn;
}

void UShipSubsystem::SetLightsOn(bool bOn)
{
    bLightsOn = bOn;

    // Want, not weight: the player's weight for the lights is a preference
    // and survives them being switched off and back on.
    PowerState.SetWant(ShipPower::Lights, bOn ? LightsWant : 0.0f);
}

float UShipSubsystem::GetJumpCharge() const
{
    return static_cast<float>(FlightState.GetJumpCharge());
}

float UShipSubsystem::GetLinearAcceleration() const
{
    return static_cast<float>(FlightState.GetLimits().LinearAcceleration);
}

void UShipSubsystem::ApplyAllocation(float DeltaSeconds)
{
    // The engine asks for power only while the jump winds. Idle, holding
    // disengaged, or charged and waiting on alignment it wants nothing, so
    // it takes part in no split and an idle drive costs the ship nothing.
    const bool bWinding = GetJumpState() == EJumpState::Winding;
    const float EngineWant = bWinding ? GetWindingWant() : 0.0f;
    if (PowerState.GetWant(ShipPower::Engine) != EngineWant)
    {
        PowerState.SetWant(ShipPower::Engine, EngineWant);
    }
    SetFoldDraw(bWinding ? FMath::Max(0.0f, CVarFoldDraw.GetValueOnGameThread()) : 0.0f);

    // Asked for fresh every frame and never stored. A cached satisfaction is
    // how two things that read the same allocation start disagreeing.
    const float BoosterFeed = PowerState.GetSatisfaction(ShipPower::Boosters);
    const float EngineFeed = PowerState.GetSatisfaction(ShipPower::Engine);
    const float Thrust = StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * BoosterFeed;

    FShipFlightLimits Limits = FlightState.GetLimits();
    const FShipFlightLimits Rated = FShipFlightLimits::Cruise();
    Limits.LinearAcceleration = Rated.LinearAcceleration * Thrust;

    // Thin boosters slow the drive's whole ease by the fraction they soften
    // cruise (decision 4): a quarter thrust takes four times as long to reach
    // any notch, and reaches it. The response is handed over unscaled --
    // scaling it as well would slow a starved ship sixteen times -- and the
    // top is never touched, so nothing ever reads as lost potential.
    Limits.DriveThrust = Thrust;
    Limits.DriveResponse = FMath::Max(0.0f, CVarDriveResponse.GetValueOnGameThread());
    Limits.HoldSeconds = CVarHoldSeconds.GetValueOnGameThread();

    // In c, and never above it: the ruled top (ruling 1). Never below the
    // lever's first notch either, which the lever always keeps, so the CVar
    // and the lever can never disagree about where it ends.
    const double TopLight = FMath::Clamp(static_cast<double>(CVarDriveTop.GetValueOnGameThread()),
        ShipDriveLever::NotchSpeed(1) / ShipDriveLever::LightCmPerSecond, 1.0);
    Limits.DriveTop = TopLight * ShipDriveLever::LightCmPerSecond;
    FlightState.SetLimits(Limits);

    // The charge winds only while engaged, and otherwise holds exactly where
    // it is: nothing decays while the player is away. A starved engine still
    // winds at StarvedRate, because a drive that cannot finish is a failure
    // state and systems here degrade rather than fail.
    const double ChargeSeconds = CVarChargeSeconds.GetValueOnGameThread();
    if (NavState.IsEngaged() && !NavState.IsInTransit())
    {
        const double Starved = FMath::Clamp(CVarStarvedRate.GetValueOnGameThread(), 0.0f, 1.0f);
        FlightState.ChargeJumpDrive(DeltaSeconds, Starved + (1.0 - Starved) * EngineFeed, ChargeSeconds);
    }

    if (ChargeRequestedFor.Get() == GetWorld())
    {
        ChargeRequestedFor.Reset();
        FlightState.ChargeJumpDrive(FMath::Max(ChargeSeconds, 1.0), 1.0, ChargeSeconds);
    }
}

void UShipSubsystem::SetFoldDraw(float Watts)
{
    if (Watts == FoldDrawWatts)
    {
        return;
    }
    if (FoldDrawWatts > 0.0f)
    {
        PowerState.RemoveDraw(FoldDrawId);
    }
    if (Watts > 0.0f)
    {
        PowerState.AddDraw(FoldDrawId, Watts);
    }
    FoldDrawWatts = Watts;
}

double UShipSubsystem::FloorFor(const FSkyBody& Body)
{
    if (Body.Kind == ESkyBodyKind::Star)
    {
        return FMath::Max(0.0f, CVarStarFloorRadii.GetValueOnGameThread()) * Body.Radius;
    }
    // The sky's floor is the one the picture is true to, so the flight's is
    // never under it: the drive takes the ship exactly as low as the world
    // keeps getting nearer, and no lower.
    return FMath::Max(EdgeFloor(), SkyProjection::RenderedFloor(Body.Radius, FSkyViewParams()));
}

double UShipSubsystem::EdgeFloor()
{
    return FMath::Max(0.0f, CVarFlightFloorKm.GetValueOnGameThread()) * UniverseUnits::CmPerKm;
}

void UShipSubsystem::UpdateSurfaces()
{
    // Between stars there is nothing to be near (plan conflict 10).
    if (NavState.IsInTransit())
    {
        FlightState.SetSurfaces({});
        return;
    }

    // Once a tick, the system here: its bodies and its edge, never the
    // thirty neighbours Current would generate and throw away.
    const FSkySystem Here = LocalSystem::Here(GetWorld());
    TArray<FFlightSurface> Surfaces;
    Surfaces.Reserve(Here.Bodies.Num() + 1);
    for (const FSkyBody& Body : Here.Bodies)
    {
        FFlightSurface Surface;
        Surface.Centre = Body.Position;
        Surface.Radius = Body.Radius;
        Surface.Floor = FloorFor(Body);
        Surfaces.Add(Surface);
    }

    // The edge is a surface on exactly the same rule, inside out about the
    // star: the drive settles just inside it and never flies the ship out of
    // its system, so the sky never draws a system the ship has left. You
    // leave by jumping.
    const FSkyBody* Star = Here.Bodies.FindByPredicate([](const FSkyBody& Body) { return Body.Kind == ESkyBodyKind::Star; });
    if (Star && Here.EdgeRadius > 0.0)
    {
        FFlightSurface Edge;
        Edge.Centre = Star->Position;
        Edge.Radius = Here.EdgeRadius;
        Edge.Floor = EdgeFloor();
        Edge.bInsideOut = true;
        Surfaces.Add(Edge);
    }
    FlightState.SetSurfaces(MoveTemp(Surfaces));
}

void UShipSubsystem::ApplyHelm(float DeltaSeconds)
{
    // A frame with no time in it moves nothing, and spends nothing: the
    // presses wait for the next, or a fresh press would be used up leaving
    // the detent by zero seconds' worth of sweep.
    if (!(DeltaSeconds > 0.0f))
    {
        return;
    }

    // Spent whatever happens next: presses are for this tick or none.
    const int32 UpPresses = Helm.UpPresses;
    const int32 DownPresses = Helm.DownPresses;
    Helm.UpPresses = 0;
    Helm.DownPresses = 0;

    // A key held through an all stop moves nothing until it is let go.
    bUpHoldSpent &= Helm.bUpHeld;
    bDownHoldSpent &= Helm.bDownHeld;
    const bool bUpHeld = Helm.bUpHeld && !bUpHoldSpent;
    const bool bDownHeld = Helm.bDownHeld && !bDownHoldSpent;

    FShipFlightCommand Command = FlightState.GetCommand();
    if (Command.bDrive)
    {
        // A press is one notch from what the ship is doing, not from where
        // the lever was (decision 3): Ctrl always slows the ship and Shift
        // always speeds it, from the first tap, under the cap, spooling up,
        // or stopping. A hold repeats the same tap after a moment.
        const double P = FlightState.GetDrivePosition();
        const int32 Top = FlightState.GetDriveNotchCount() - 1;
        const double Sweep = FMath::Max(0.0f, CVarDriveSweep.GetValueOnGameThread());
        const int32 Ups = UpPresses + UpRepeat.Update(bUpHeld, DeltaSeconds, Sweep);
        const int32 Downs = DownPresses + DownRepeat.Update(bDownHeld, DeltaSeconds, Sweep);
        for (int32 Tap = 0; Tap < Ups; ++Tap)
        {
            Command.DriveNotch = ShipDriveLever::TapUp(Command.DriveNotch, P, Top);
        }
        for (int32 Tap = 0; Tap < Downs; ++Tap)
        {
            Command.DriveNotch = ShipDriveLever::TapDown(Command.DriveNotch, P);
        }
    }
    else
    {
        // Cruise's lever, live from the press of F even while the drive
        // spools down. Once a frame with this frame's presses, which is what
        // lets a fresh press, and only a fresh press, leave the detent.
        UpRepeat.Update(false, 0.0, 0.0);
        DownRepeat.Update(false, 0.0, 0.0);
        Command.Throttle = ShipDriveLever::SweepCruise(Command.Throttle, bUpHeld, bDownHeld, UpPresses, DownPresses,
            DeltaSeconds, FMath::Max(0.0f, CVarCruiseSweep.GetValueOnGameThread()));
    }
    FlightState.SetCommand(Command);
}

void UShipSubsystem::StepNavigation(float DeltaSeconds)
{
    FNavTuning Tuning;
    Tuning.ConeRadians = GetJumpConeRadians();
    Tuning.TransitSeconds = FMath::Max(0.0f, CVarTransitSeconds.GetValueOnGameThread());

    const TOptional<FVector> Course = GetCourseDirectionShipLocal();
    const double OffBoresight = Course ? ShipNav::OffBoresight(*Course) : UE_DOUBLE_PI;

    switch (NavState.Step(DeltaSeconds, FlightState.GetJumpCharge(), OffBoresight, Tuning))
    {
    case ENavEvent::TransitBegan:
    {
        // The fold has opened, and the helm does nothing between stars. It is
        // an all stop, too (flight-feel decision 4): both levers to STOP, so
        // the ship comes out of every jump at rest -- JumpTo makes that exact
        // -- and the first speed in the new place is the pilot's to choose.
        FlightState.SpendJumpCharge();
        FlightState.ReleaseAttitude();
        FShipFlightCommand Stopped = FlightState.GetCommand();
        Stopped.Throttle = 0.0;
        Stopped.DriveNotch = 0;
        FlightState.SetCommand(Stopped);
        Helm = FHelmInput();
        break;
    }

    case ENavEvent::Arrived:
    {
        // A translation and nothing else, onto the line from here to the
        // star: the new sun is where the nose was, and the distant dome does
        // not move (nav decision 5). Which system the ship is in then
        // changes by itself, because GetSystemAt is asked of the position
        // (plan conflict 1); nothing records an arrival anywhere else.
        const UUniverseSubsystem* Cosmos = Universe();
        const TOptional<FSystemId>& Destination = NavState.GetLastArrival();
        const TOptional<FStarSystem> Star = (Cosmos && Destination) ? Cosmos->GetSystem(*Destination) : TOptional<FStarSystem>();
        if (Star)
        {
            FlightState.JumpTo(NavStart::ArrivalPoint(FlightState.GetUniversePosition(), *Star,
                                                      CVarStandoffAU.GetValueOnGameThread()));
        }
        break;
    }

    case ENavEvent::None:
        break;
    }
}

bool UShipSubsystem::InstallModule(UShipModuleDataAsset* Module)
{
    if (!Module)
    {
        return false;
    }
    if (!PowerState.AddDraw(Module->ModuleId, Module->PowerDraw))
    {
        return false;
    }
    InstalledModules.Add(Module);
    return true;
}

bool UShipSubsystem::RemoveModule(UShipModuleDataAsset* Module)
{
    if (!Module)
    {
        return false;
    }
    if (!PowerState.RemoveDraw(Module->ModuleId))
    {
        return false;
    }
    InstalledModules.Remove(Module);
    return true;
}

float UShipSubsystem::GetPowerDraw() const
{
    return PowerState.GetTotalDraw();
}

float UShipSubsystem::GetPowerHeadroom() const
{
    return PowerState.GetHeadroom();
}

float UShipSubsystem::GetReactorOutput() const
{
    return PowerState.GetReactorOutput();
}

bool UShipSubsystem::IsPowerOverloaded() const
{
    return PowerState.IsOverloaded();
}

void UShipSubsystem::SetPilot(APawn* NewPilot)
{
    // Marked at the first handover rather than here, because the pawn has
    // handed nothing over yet: the ship may tick between the seat and the
    // pawn's next frame, and a spent flag set now would be cleared by a
    // frame with no key in it before the held key ever arrived.
    if (NewPilot != Pilot.Get())
    {
        bAwaitingFirstHands = true;
    }
    Pilot = NewPilot;
}

void UShipSubsystem::ClearPilot()
{
    Pilot.Reset();

    // A ship nobody is flying does not keep turning, and no key is held at an
    // empty helm. Both levers stay: a cruise the player set and then walked
    // away from is the point.
    FlightState.ReleaseAttitude();
    Helm = FHelmInput();
}

bool UShipSubsystem::IsPiloted() const
{
    return Pilot.IsValid();
}

APawn* UShipSubsystem::GetPilot() const
{
    return Pilot.Get();
}

void UShipSubsystem::Tick(float DeltaTime)
{
    ApplyAllocation(DeltaTime);
    if (NavState.IsInTransit())
    {
        // Between stars the helm is inert: attitude and lever input alike are
        // dropped before they can move anything.
        FlightState.ReleaseAttitude();
        Helm = FHelmInput();
    }
    ApplyHelm(DeltaTime);
    UpdateSurfaces();
    FlightState.Step(DeltaTime);
    StepNavigation(DeltaTime);
}

TStatId UShipSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UShipSubsystem, STATGROUP_Tickables);
}

bool UShipSubsystem::MayCommand(const APawn* Commander) const
{
    return Commander && Commander == Pilot.Get() && !NavState.IsInTransit();
}

bool UShipSubsystem::SetFlightCommand(APawn* Commander, float Throttle, FVector AttitudeRate)
{
    if (!MayCommand(Commander))
    {
        return false;
    }

    // From the command in force, not a fresh one: a fresh command would
    // disengage the drive and zero its lever on every input.
    FShipFlightCommand Command = FlightState.GetCommand();
    Command.Throttle = Throttle;
    Command.AttitudeRate = AttitudeRate;
    FlightState.SetCommand(Command);
    return true;
}

bool UShipSubsystem::SetHelmInput(APawn* Commander, const FHelmInput& Input)
{
    if (!MayCommand(Commander))
    {
        return false;
    }
    FShipFlightCommand Command = FlightState.GetCommand();
    Command.AttitudeRate = Input.Attitude;
    FlightState.SetCommand(Command);

    if (bAwaitingFirstHands)
    {
        // A key held from before sitting down moves nothing until it is
        // let go and pressed again -- the same rule as a key held through X.
        bAwaitingFirstHands = false;
        bUpHoldSpent = Input.bUpHeld;
        bDownHoldSpent = Input.bDownHeld;
    }
    Helm.Attitude = Input.Attitude;
    Helm.bUpHeld = Input.bUpHeld;
    Helm.bDownHeld = Input.bDownHeld;
    Helm.UpPresses += FMath::Max(0, Input.UpPresses);
    Helm.DownPresses += FMath::Max(0, Input.DownPresses);
    return true;
}

bool UShipSubsystem::AllStop(APawn* Commander)
{
    if (!MayCommand(Commander))
    {
        return false;
    }
    FShipFlightCommand Command = FlightState.GetCommand();
    Command.Throttle = 0.0;
    Command.DriveNotch = 0;
    FlightState.SetCommand(Command);

    // Nothing pressed before the stop survives it, and nothing held through
    // it moves a lever until it is let go and pressed again.
    Helm.UpPresses = 0;
    Helm.DownPresses = 0;
    bUpHoldSpent = Helm.bUpHeld;
    bDownHoldSpent = Helm.bDownHeld;
    return true;
}

bool UShipSubsystem::SetDriveLever(APawn* Commander, int32 Notch)
{
    if (!MayCommand(Commander))
    {
        return false;
    }
    FShipFlightCommand Command = FlightState.GetCommand();
    Command.DriveNotch = Notch;
    FlightState.SetCommand(Command);
    return true;
}

bool UShipSubsystem::SetDriveEngaged(APawn* Commander, bool bOn)
{
    if (!MayCommand(Commander))
    {
        return false;
    }
    FShipFlightCommand Command = FlightState.GetCommand();
    Command.bDrive = bOn;
    FlightState.SetCommand(Command);
    return true;
}

bool UShipSubsystem::IsDriveEngaged() const
{
    return FlightState.GetCommand().bDrive;
}

TOptional<FSystemId> UShipSubsystem::SystemHere() const
{
    const UUniverseSubsystem* Cosmos = Universe();
    if (!Cosmos)
    {
        return {};
    }
    if (const TOptional<FStarSystem> Here = Cosmos->GetSystemAt(FlightState.GetUniversePosition()))
    {
        return Here->Stub.Id;
    }
    return {};
}

TArray<FStarSystemStub> UShipSubsystem::GetChart() const
{
    const UUniverseSubsystem* Cosmos = Universe();
    if (!Cosmos)
    {
        return {};
    }
    TArray<FStarSystemStub> Chart = Cosmos->GetSystemsNear(
        FlightState.GetUniversePosition(), GetChartRangeLy() * UniverseUnits::CmPerLightYear);
    if (const TOptional<FSystemId> Here = SystemHere())
    {
        Chart.RemoveAll([&Here](const FStarSystemStub& Stub) { return Stub.Id == *Here; });
    }
    return Chart;
}

bool UShipSubsystem::PlotCourse(const FSystemId& Id)
{
    const UUniverseSubsystem* Cosmos = Universe();
    if (NavState.IsInTransit() || !Cosmos || !Cosmos->GetSystem(Id).IsSet())
    {
        return false;
    }
    // You are already there; there is nothing to fold toward.
    const TOptional<FSystemId> Here = SystemHere();
    if (Here && *Here == Id)
    {
        return false;
    }
    return NavState.Plot(Id);
}

void UShipSubsystem::ClearCourse()
{
    NavState.ClearPlot();
}

TOptional<FSystemId> UShipSubsystem::GetPlottedSystem() const
{
    return NavState.GetPlotted();
}

bool UShipSubsystem::SetJumpEngaged(bool bOn)
{
    return NavState.SetEngaged(bOn);
}

bool UShipSubsystem::IsJumpEngaged() const
{
    return NavState.IsEngaged();
}

EJumpState UShipSubsystem::GetJumpState() const
{
    return NavState.GetJumpState(FlightState.GetJumpCharge());
}

int32 UShipSubsystem::GetJumpSerial() const
{
    return NavState.GetJumpSerial();
}

bool UShipSubsystem::IsInTransit() const
{
    return NavState.IsInTransit();
}

double UShipSubsystem::GetTransitProgress() const
{
    return NavState.GetTransitProgress();
}

TOptional<FVector> UShipSubsystem::GetCourseDirection() const
{
    const UUniverseSubsystem* Cosmos = Universe();
    const TOptional<FSystemId>& Plotted = NavState.GetPlotted();
    if (!Cosmos || !Plotted)
    {
        return {};
    }
    const TOptional<FStarSystem> Star = Cosmos->GetSystem(*Plotted);
    if (!Star)
    {
        return {};
    }
    const FVector Direction = (Star->Stub.Position - FlightState.GetUniversePosition()).GetSafeNormal();
    return Direction.IsZero() ? TOptional<FVector>() : TOptional<FVector>(Direction);
}

TOptional<FVector> UShipSubsystem::GetCourseDirectionShipLocal() const
{
    const TOptional<FVector> Direction = GetCourseDirection();
    return Direction ? TOptional<FVector>(FlightState.GetUniverseOrientation().UnrotateVector(*Direction))
                     : TOptional<FVector>();
}

double UShipSubsystem::GetJumpConeRadians() const
{
    return FMath::DegreesToRadians(FMath::Max(0.0, static_cast<double>(CVarConeDeg.GetValueOnGameThread())));
}

float UShipSubsystem::GetWindingWant()
{
    return FMath::Max(0.0f, CVarWindingWant.GetValueOnGameThread());
}

float UShipSubsystem::GetChartRangeLy()
{
    return FMath::Max(0.0f, CVarRangeLy.GetValueOnGameThread());
}

bool UShipSubsystem::HasVisited(const FSystemId& Id) const
{
    return NavState.HasVisited(Id);
}

FVector UShipSubsystem::GetShipVelocity() const
{
    return FlightState.GetVelocity();
}

float UShipSubsystem::GetShipSpeed() const
{
    return static_cast<float>(FlightState.GetSpeed());
}

FTransform UShipSubsystem::GetCounterFrameTransform() const
{
    return FlightState.GetCounterFrameTransform();
}

FVector UShipSubsystem::UniverseToWorld(const FUniversePosition& UniversePosition) const
{
    return FlightState.UniverseToWorld(UniversePosition);
}

void UShipSubsystem::PlaceShip(const FUniversePosition& NewPosition, const FQuat& NewOrientation)
{
    FlightState.SetUniverseTransform(NewPosition, NewOrientation);
}

const FShipFlightState& UShipSubsystem::GetFlightState() const
{
    return FlightState;
}
