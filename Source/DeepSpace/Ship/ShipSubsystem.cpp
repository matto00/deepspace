#include "Ship/ShipSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Ship/NavStart.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipPartCatalogue.h"
#include "Ship/ShipVerticalLever.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Surface/GroundField.h"
#include "Surface/WorldRelief.h"
#include "UI/NavText.h"
#include "UI/TargetMarker.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

namespace
{
    // Every tunable the playtest moves is a console variable, read where it
    // is used and never cached: Linux has no Live Coding, and a header
    // constant would make each nudge a rebuild (nav decision 6). The defaults
    // are the starting values, to be written back once play has settled them.

    // The four numbers a fitted part rates (wear and upgrades decision 6):
    // -1, the default, means "the part's", and 0 or more overrides it for
    // the session, so a playtest still moves each with no rebuild. The
    // settled 45 s and 380 W live in the catalogue now (Tools/ship_parts.json,
    // FShipRatings::Stock), and the ship's four getters are the one place the
    // rule is applied (ShipParts::Effective). Unlike the rest of this block,
    // a value play settles for these four is never written back here: -1
    // stays their default. A settled *stock* number lives in four places,
    // moved together, since an empty bay reads the constant and the tests
    // hold them equal: the stock row in Tools/ship_parts.json (then re-run
    // Tools/setup_ship_parts.py), its ShipParts::Stock* constant in
    // ShipParts.h (DeepSpace.Ship.Parts.Contract), and, for the charge and
    // the response, FShipFlightState::JumpChargeSeconds and
    // ShipDriveLever::DefaultResponse (DeepSpace.Ship.Parts.Arithmetic). An
    // upgrade's number is its own row in the JSON alone.
    TAutoConsoleVariable<float> CVarChargeSeconds(
        TEXT("ds.Nav.ChargeSeconds"), -1.0f,
        TEXT("Seconds for the jump to wind from cold with the engine fully fed. -1: the drive part's."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarWindingWant(
        TEXT("ds.Nav.WindingWant"), -1.0f,
        TEXT("Watts the engine asks for while the jump winds; it asks for nothing otherwise. -1: the drive part's."),
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

    TAutoConsoleVariable<float> CVarWorldStandoffDeg(
        TEXT("ds.Nav.WorldStandoffDeg"), static_cast<float>(NavStart::DefaultWorldStandoffDeg),
        TEXT("How wide, degrees, an in-system jump meets the world it went to: its standoff is the distance that shows it this wide."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarRangeLy(
        TEXT("ds.Nav.RangeLy"), -1.0f,
        TEXT("How far the chart reaches, light years. -1: the sensors part's."),
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
        TEXT("The drive lever's top, in c: 0.1 at most (anything faster is a jump), 20 km/s at least. Shortens the lever; never lengthens it."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarDriveResponse(
        TEXT("ds.Drive.Response"), -1.0f,
        TEXT("Notches a second the drive's speed may move at full thrust. Thin boosters slow the whole ease, never this. -1: the drive part's."),
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
        TEXT("How fast a held lever key sweeps the cruise lever, fraction of its travel a second (the lever reads on a log scale, 1 m/s to cruise's top)."),
        ECVF_Default);

    // Landing (spec 2026-09-27). Each default is the pure layer's constant.

    TAutoConsoleVariable<float> CVarGearClearance(
        TEXT("ds.Land.GearClearance"), static_cast<float>(ShipLanding::DefaultGearClearanceCm),
        TEXT("The ship's origin over flat ground at rest, cm: 10 cm of slab and the gear's feet under it."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarTouchdownSpeed(
        TEXT("ds.Land.TouchdownSpeed"), static_cast<float>(ShipFlight::DefaultTouchdownSpeed / 100.0),
        TEXT("Contact speed, m/s: the ground approach's floor."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarApproachSeconds(
        TEXT("ds.Land.ApproachSeconds"), static_cast<float>(ShipFlight::DefaultApproachSeconds),
        TEXT("The ground approach's ease, seconds: the last hundreds of metres fall by e every this many. Clamped to at least 0.5."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarSkimSeconds(
        TEXT("ds.Land.SkimSeconds"), static_cast<float>(ShipFlight::DefaultSkimSeconds),
        TEXT("The skim cap: near a world, horizontal speed is at most the height above the ground over this many seconds."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarSkimFloor(
        TEXT("ds.Land.SkimFloor"), static_cast<float>(ShipFlight::DefaultSkimFloor / 100.0),
        TEXT("The skim cap's floor, m/s: it never holds the ship slower than this."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarRegime(
        TEXT("ds.Land.Regime"), static_cast<float>(ShipFlight::DefaultRegimeCm / UniverseUnits::CmPerKm),
        TEXT("Within this many km of a world's cruise floor the vertical lever is live (leaving over 1.1 x it)."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarDriveHandback(
        TEXT("ds.Land.DriveHandback"), static_cast<float>(ShipFlight::DefaultDriveHandbackCm / 100.0),
        TEXT("How far over a solid world's drive floor, m, the drive takes the ship back from cruise."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarVerticalTop(
        TEXT("ds.Vertical.Top"), static_cast<float>(ShipVerticalLever::DefaultTopCmPerSecond / 100.0),
        TEXT("The vertical lever's top, m/s, either way."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarVerticalSweep(
        TEXT("ds.Vertical.Sweep"), static_cast<float>(ShipVerticalLever::DefaultSweep),
        TEXT("How fast Space and C held sweep the vertical lever, fraction of its travel a second."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarHeavyFloor(
        TEXT("ds.Vertical.HeavyFloor"), static_cast<float>(ShipVerticalLever::DefaultHeavyFloor),
        TEXT("On heavy worlds the climb top is Top x max(this, g_E / g): never below this fraction of it."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarHoldWatts(
        TEXT("ds.Boosters.HoldWatts"), ShipPower::DefaultHoldWattsPerG,
        TEXT("Watts per g the boosters want to hold the ship, only under a solid world's drive floor, airborne (capped at 3 g)."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarStarvedSink(
        TEXT("ds.Boosters.StarvedSink"), 2.0f,
        TEXT("m/s the ship sinks at when the hold gets nothing, only under a solid world's drive floor, never while climbing."),
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
                 Chart.Num(), Ship->GetChartRangeLy());
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
        // The target as the course: the in-system jump (map decision 12).
        if (!Args.IsEmpty() && Args[0].Equals(TEXT("target"), ESearchCase::IgnoreCase))
        {
            if (Ship->PlotTarget())
            {
                Out.Log(TEXT("Course plotted to the target, in this system. Engage with ds.Nav.Engage."));
            }
            else
            {
                Out.Log(Ship->GetTarget()
                    ? TEXT("Cannot plot the target now: in the fold, or near enough to fly.")
                    : TEXT("Nothing targeted: ds.Nav.Target <n> first."));
            }
            return;
        }
        const TArray<FStarSystemStub> Chart = Ship->GetChart();
        const int32 Index = Args.IsEmpty() ? INDEX_NONE : FCString::Atoi(*Args[0]);
        if (Args.IsEmpty() || !Chart.IsValidIndex(Index))
        {
            Out.Logf(TEXT("ds.Nav.Plot <n|target>: n is a row of ds.Nav.Near, 0 to %d"), Chart.Num() - 1);
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

    /**
     * ds.Nav.Target: the worlds of the system here, numbered by orbit as
     * their numerals are (I is 1), the target marked; with an argument,
     * target one by number, designation, given name or numeral, "next" as
     * Tab does, or "none".
     */
    void NavTarget(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Nav.Target"));
        const UUniverseSubsystem* Cosmos = World ? World->GetSubsystem<UUniverseSubsystem>() : nullptr;
        if (!Ship || !Cosmos)
        {
            return;
        }
        const TOptional<FStarSystem> Here = Ship->IsInTransit()
            ? TOptional<FStarSystem>() : Cosmos->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
        if (!Here)
        {
            Out.Log(Ship->IsInTransit() ? TEXT("In the fold: nothing to target.") : TEXT("Between stars: nothing to target."));
            return;
        }
        const TOptional<FBodyId> Target = Ship->GetTarget();
        if (Args.IsEmpty())
        {
            Out.Logf(TEXT("%d worlds orbit %s. Target one with ds.Nav.Target <n|name|next|none>."),
                     Here->Planets.Num(), *Here->Stub.Name);
            const FUniversePosition Where = Ship->GetFlightState().GetUniversePosition();
            for (int32 Orbit = 0; Orbit < Here->Planets.Num(); ++Orbit)
            {
                const FPlanet& Planet = Here->Planets[Orbit];
                const double Surface = FMath::Max(0.0, Where.DistanceTo(Here->PlanetPosition(Orbit))
                    - Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius);
                FString Line = FString::Printf(TEXT("%2d  %-24s %-12s %s"), Orbit + 1, *NavText::WorldName(Planet),
                    *NavText::WorldKind(Planet.Kind), *NavText::Distance(Surface));
                if (Target && ShipNav::TargetPlanet(*Here, *Target) == &Planet)
                {
                    Line += TEXT("  <- target");
                }
                Out.Log(Line);
            }
            return;
        }

        const FString& Arg = Args[0];
        if (Arg.Equals(TEXT("none"), ESearchCase::IgnoreCase))
        {
            Ship->ClearTarget();
            Out.Log(Ship->GetTarget() ? TEXT("In the fold: the target stands.") : TEXT("Target cleared."));
            return;
        }
        if (Arg.Equals(TEXT("next"), ESearchCase::IgnoreCase))
        {
            if (!Ship->CycleTarget())
            {
                Out.Log(TEXT("Nothing to target."));
                return;
            }
        }
        else
        {
            int32 Orbit = INDEX_NONE;
            if (Arg.IsNumeric())
            {
                Orbit = FCString::Atoi(*Arg) - 1;
            }
            else
            {
                // The whole of any name a world goes by; the args are split
                // on spaces, so "Kessa II" arrives as two.
                const FString Name = FString::Join(Args, TEXT(" "));
                for (int32 Index = 0; Index < Here->Planets.Num() && Orbit == INDEX_NONE; ++Index)
                {
                    const FPlanet& Planet = Here->Planets[Index];
                    const FString Numeral = Planet.Designation.Mid(Here->Stub.Name.Len()).TrimStartAndEnd();
                    if (Name.Equals(Planet.Designation, ESearchCase::IgnoreCase) || Name.Equals(Numeral, ESearchCase::IgnoreCase)
                        || (!Planet.GivenName.IsEmpty() && Name.Equals(Planet.GivenName, ESearchCase::IgnoreCase)))
                    {
                        Orbit = Index;
                    }
                }
            }
            if (!Here->Planets.IsValidIndex(Orbit) || !Ship->SetTarget(FBodyId{ Here->Stub.Id, Orbit, -1 }))
            {
                Out.Logf(TEXT("ds.Nav.Target <n|name|next|none>: n is an orbit, 1 to %d."), Here->Planets.Num());
                return;
            }
        }
        if (const FPlanet* Planet = Ship->GetTarget() ? ShipNav::TargetPlanet(*Here, *Ship->GetTarget()) : nullptr)
        {
            Out.Logf(TEXT("Target: %s."), *NavText::WorldName(*Planet));
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
        TEXT("ds.Nav.Plot"), TEXT("'ds.Nav.Plot <n>': plot a course to row n of ds.Nav.Near. 'ds.Nav.Plot target': to the target, in this system."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavPlot));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavTargetCommand(
        TEXT("ds.Nav.Target"), TEXT("'ds.Nav.Target': the worlds here. 'ds.Nav.Target <n|name|next|none>': mark one, the next, or none."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavTarget));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavClearCommand(
        TEXT("ds.Nav.Clear"), TEXT("Clear the course, which also stands the jump down."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavClear));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavEngageCommand(
        TEXT("ds.Nav.Engage"), TEXT("'ds.Nav.Engage [0|1]': engage the jump (default) or stand it down."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavEngage));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice NavChargeCommand(
        TEXT("ds.Nav.Charge"), TEXT("Fill the jump's charge on the next tick, for running the loop in seconds."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&NavCharge));

    // -- parts, from the console, until landing and a sourcing spec exist ------
    // (wear and upgrades ruling 8). Developer's lines, not screens in the ship:
    // ds.Ship.Describe may print what no player sees.

    /** A catalogue part by id or display name, case-blind; the args are split
     *  on spaces, so "Twin-core reactor" arrives as two. */
    UShipModuleDataAsset* PartNamed(const UShipSubsystem& Ship, const TArray<FString>& Args)
    {
        const FString Wanted = FString::Join(Args, TEXT(" "));
        for (UShipModuleDataAsset* Part : Ship.GetCatalogue())
        {
            if (Wanted.Equals(Part->ModuleId.ToString(), ESearchCase::IgnoreCase)
                || Wanted.Equals(Part->DisplayName.ToString(), ESearchCase::IgnoreCase))
            {
                return Part;
            }
        }
        return nullptr;
    }

    FString CatalogueIds(const UShipSubsystem& Ship)
    {
        TArray<FString> Ids;
        for (const UShipModuleDataAsset* Part : Ship.GetCatalogue())
        {
            Ids.Add(Part->ModuleId.ToString());
        }
        return FString::Join(Ids, TEXT(", "));
    }

    void ShipInstall(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Ship.Install"));
        if (!Ship)
        {
            return;
        }
        const UShipModuleDataAsset* Part = Args.IsEmpty() ? nullptr : PartNamed(*Ship, Args);
        if (!Part)
        {
            Out.Logf(TEXT("ds.Ship.Install <part>: an id or a name, one of %s"), *CatalogueIds(*Ship));
            return;
        }
        if (Ship->FitPartById(Part->ModuleId))
        {
            Out.Logf(TEXT("%s is fitted (%s)."), *Part->DisplayName.ToString(), *Part->ModuleId.ToString());
        }
        else
        {
            Out.Logf(TEXT("%s cannot be fitted."), *Part->ModuleId.ToString());
        }
    }

    void ShipSpares(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Ship.Spares"));
        if (!Ship)
        {
            return;
        }
        if (!Args.IsEmpty() && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
        {
            Ship->ClearSpares();
            Out.Log(TEXT("No spares aboard."));
            return;
        }
        if (!Args.IsEmpty() && Args[0].Equals(TEXT("give"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Rest = Args;
            Rest.RemoveAt(0);
            const UShipModuleDataAsset* Part = Rest.IsEmpty() ? nullptr : PartNamed(*Ship, Rest);
            if (!Part || !Ship->AddSpare(Part->ModuleId))
            {
                Out.Logf(TEXT("ds.Ship.Spares give <part>: an id or a name, one of %s"), *CatalogueIds(*Ship));
                return;
            }
            Out.Logf(TEXT("A spare %s is aboard."), *Part->DisplayName.ToString());
            return;
        }
        const TArray<FShipPartState>& Spares = Ship->GetSpares();
        Out.Logf(TEXT("%d spares aboard. ds.Ship.Install <part> fits one; ds.Ship.Spares give <part> | clear."), Spares.Num());
        for (int32 Index = 0; Index < Spares.Num(); ++Index)
        {
            Out.Logf(TEXT("%2d  %s"), Index, *Spares[Index].PartId.ToString());
        }
    }

    void ShipDescribe(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        const UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Ship.Describe"));
        if (!Ship)
        {
            return;
        }
        for (const EShipBay Bay : ShipBay::All())
        {
            FString Line = FString::Printf(TEXT("%-12s "), *ShipBay::Name(Bay).ToString());
            if (const UShipModuleDataAsset* Part = Ship->GetFittedPart(Bay))
            {
                Line += FString::Printf(TEXT("%s  draw %.0f W"), *Part->ModuleId.ToString(), Part->PowerDraw);
                for (const TPair<EShipRating, double>& Rated : Part->Ratings)
                {
                    Line += FString::Printf(TEXT("  %s %s"), *ShipParts::RatingName(Rated.Key).ToString(), *FString::SanitizeFloat(Rated.Value));
                }
            }
            else if (ShipBay::IsCore(Bay))
            {
                Line += TEXT("empty: reads the stock part, draws nothing");
            }
            else
            {
                // An aux slot has no stock part (decision 2): empty is empty.
                Line += TEXT("empty: no part, rates nothing, draws nothing");
            }
            Out.Log(Line);
        }
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice ShipInstallCommand(
        TEXT("ds.Ship.Install"), TEXT("'ds.Ship.Install <part>': fit a part by id or name; a spare with that id first, else a new one. The displaced part becomes a spare."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ShipInstall));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice ShipSparesCommand(
        TEXT("ds.Ship.Spares"), TEXT("'ds.Ship.Spares': the spares aboard. 'give <part>' adds one; 'clear' empties them."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ShipSpares));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice ShipDescribeCommand(
        TEXT("ds.Ship.Describe"), TEXT("Every bay: its part, draw and ratings. A developer's line, not a screen in the ship."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ShipDescribe));
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

    // Every bay, empty: each reads the stock part until something is fitted.
    Loadout = ShipParts::EmptyLoadout();

    // Today's ship until a part says otherwise: an empty bay reads the stock
    // part (wear and upgrades decision 3), so a bare test world is exactly
    // the bare test world it always was.
    const FShipRatings Stock = FShipRatings::Stock();
    PowerState.SetReactorOutput(static_cast<float>(Stock.ReactorWatts));

    // An even split to start with, which is a starting point and not a
    // recommendation: every split is viable and none is correct. The engine
    // starts idle, wanting nothing, and so takes part in no split until the
    // jump is engaged.
    PowerState.SetConsumer(ShipPower::Lights, static_cast<float>(Stock.LightsWant), 1.0f);
    PowerState.SetConsumer(ShipPower::Boosters, static_cast<float>(Stock.BoostersWant), 1.0f);
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
    PowerState.SetWant(ShipPower::Lights, bOn ? static_cast<float>(GetRatings().LightsWant) : 0.0f);
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
    // The fitted parts' numbers, derived now and never stored (wear decision 2).
    const FShipRatings Ratings = GetRatings();

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

    // The hold (landing decision 5): only under a solid world's drive floor,
    // airborne -- landed is slice (c)'s, always airborne here. Recomputed only
    // when it moves by more than a watt, so the split is not re-solved every
    // frame for nothing.
    const float WantNow = ShipPower::HoldWant(FlightState.GetLocalGravity().Size(), FlightState.GetDepthUnderDriveFloor(),
                                              FMath::Max(0.0f, CVarHoldWatts.GetValueOnGameThread()), true);
    if (FMath::Abs(WantNow - HoldWant) > 1.0f || (WantNow == 0.0f && HoldWant != 0.0f))
    {
        HoldWant = WantNow;
    }

    // The boosters' want has one writer, here (wear sign-off 29): the fitted
    // part's rating plus the hold. A fit changes the rating and this pass
    // writes the sum, so a fit and the hold never write it from two places.
    const float ManoeuvreWant = static_cast<float>(Ratings.BoostersWant);
    const float BoostersWantNow = ManoeuvreWant + HoldWant;
    if (PowerState.GetWant(ShipPower::Boosters) != BoostersWantNow)
    {
        PowerState.SetWant(ShipPower::Boosters, BoostersWantNow);
    }

    // Asked for fresh every frame and never stored beyond it.
    LastSplit = ShipPower::SplitBoosters(PowerState.GetShare(ShipPower::Boosters), HoldWant, ManoeuvreWant);
    const float EngineFeed = PowerState.GetSatisfaction(ShipPower::Engine);
    const float Thrust = StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * LastSplit.ManoeuvreFeed;

    FShipFlightLimits Limits = FlightState.GetLimits();
    Limits.LinearAcceleration = Ratings.LinearAcceleration * Thrust;

    // Thin boosters slow the drive's whole ease by the fraction they soften
    // cruise (decision 4): a quarter thrust takes four times as long to reach
    // any notch, and reaches it. The response is handed over unscaled --
    // scaling it as well would slow a starved ship sixteen times -- and the
    // top is never touched, so nothing ever reads as lost potential.
    Limits.DriveThrust = Thrust;
    Limits.DriveResponse = GetDriveResponse();
    Limits.HoldSeconds = CVarHoldSeconds.GetValueOnGameThread();

    // In c, and never above 0.1 c: the ruled top (the 2026-09-27 ruling).
    // Never below the lever's first notch either, 20 km/s, which the lever
    // always keeps, so the CVar and the lever can never disagree about where
    // it ends.
    const double TopLight = FMath::Clamp(static_cast<double>(CVarDriveTop.GetValueOnGameThread()),
        ShipDriveLever::NotchSpeed(1) / ShipDriveLever::LightCmPerSecond, ShipDriveLever::DefaultTopLight);
    Limits.DriveTop = TopLight * ShipDriveLever::LightCmPerSecond;

    // Landing's tunables, read at use and converted from the units they are
    // tuned in.
    Limits.GearClearanceCm = GearClearance();
    Limits.TouchdownSpeed = FMath::Max(0.0f, CVarTouchdownSpeed.GetValueOnGameThread()) * 100.0;
    Limits.ApproachSeconds = FMath::Max(static_cast<double>(CVarApproachSeconds.GetValueOnGameThread()), ShipFlight::MinApproachSeconds);
    Limits.SkimSeconds = FMath::Max(0.01f, CVarSkimSeconds.GetValueOnGameThread());
    Limits.SkimFloor = FMath::Max(0.0f, CVarSkimFloor.GetValueOnGameThread()) * 100.0;
    Limits.RegimeCm = FMath::Max(0.0f, CVarRegime.GetValueOnGameThread()) * UniverseUnits::CmPerKm;
    Limits.DriveHandbackCm = FMath::Max(0.0f, CVarDriveHandback.GetValueOnGameThread()) * 100.0;
    Limits.VerticalTop = FMath::Max(0.0f, CVarVerticalTop.GetValueOnGameThread()) * 100.0;
    Limits.VerticalHeavyFloor = FMath::Clamp(CVarHeavyFloor.GetValueOnGameThread(), 0.0f, 1.0f);

    // The starved sink: a bias the flight applies only under the floor, and
    // only to HOVER or a sink -- a starved ship always lifts.
    Limits.SinkBias = FMath::Max(0.0f, CVarStarvedSink.GetValueOnGameThread()) * 100.0 * (1.0 - LastSplit.HoldFed);
    FlightState.SetLimits(Limits);

    // The charge winds only while engaged, and otherwise holds exactly where
    // it is: nothing decays while the player is away. A starved engine still
    // winds at StarvedRate, because a drive that cannot finish is a failure
    // state and systems here degrade rather than fail.
    const double ChargeSeconds = GetChargeSeconds();
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

float UShipSubsystem::GetHoldWant() const
{
    return HoldWant;
}

float UShipSubsystem::GetHoldWatts() const
{
    return LastSplit.HoldWatts;
}

float UShipSubsystem::GetHoldWattsPerG()
{
    return FMath::Max(0.0f, CVarHoldWatts.GetValueOnGameThread());
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
    // never under it; over solid ground it is taken above the highest peak.
    const double Above = FMath::Max(EdgeFloor(), SkyProjection::RenderedFloor(Body.Radius, FSkyViewParams()));
    return Body.Ground == EGround::Solid ? Above + FWorldRelief(Body.Relief).MaxHeightCm() : Above;
}

double UShipSubsystem::EdgeFloor()
{
    return FMath::Max(0.0f, CVarFlightFloorKm.GetValueOnGameThread()) * UniverseUnits::CmPerKm;
}

double UShipSubsystem::GearClearance()
{
    return FMath::Max(0.0f, CVarGearClearance.GetValueOnGameThread());
}

namespace
{
    bool SameRelief(const FWorldReliefParams& A, const FWorldReliefParams& B)
    {
        return A.SeedOffset == B.SeedOffset && A.RadiusCm == B.RadiusCm && A.PeakCm == B.PeakCm
            && A.Cratering == B.Cratering && A.Ground == B.Ground;
    }
}

FGroundFieldRef UShipSubsystem::GroundFor(const FSkyBody& Body)
{
    if (GroundCache.Num() > 64)
    {
        GroundCache.Reset();   // a few jumps' worth; the next frame refills what is near
    }
    FGroundCacheEntry* Entry = GroundCache.Find(Body.Id);
    if (!Entry || !SameRelief(Entry->Params, Body.Relief))
    {
        Entry = &GroundCache.Add(Body.Id, FGroundCacheEntry{ Body.Relief, ShipGround::FromRelief(Body.Relief) });
    }
    return Entry->Ground;
}

void UShipSubsystem::UpdateSurfaces()
{
    // Between stars there is nothing to be near (plan conflict 10), and
    // nothing pulls.
    if (NavState.IsInTransit())
    {
        FlightState.SetSurfaces({});
        FlightState.SetWells({});
        return;
    }

    // Once a tick, the system here: its bodies and its edge, never the
    // thirty neighbours Current would generate and throw away.
    const FSkySystem Here = LocalSystem::Here(GetWorld());
    TArray<FFlightSurface> Surfaces;
    TArray<FGravityWell> Wells;
    Surfaces.Reserve(Here.Bodies.Num() + 1);
    Wells.Reserve(Here.Bodies.Num());
    for (const FSkyBody& Body : Here.Bodies)
    {
        FFlightSurface Surface;
        Surface.Centre = Body.Position;
        Surface.Radius = Body.Radius;
        Surface.Floor = FloorFor(Body);
        Surface.bWorld = Body.Kind != ESkyBodyKind::Star;
        if (Body.Ground == EGround::Solid)
        {
            Surface.Ground = GroundFor(Body);
        }
        Surfaces.Add(Surface);
        // Every body pulls, the star included (landing ruling 4).
        Wells.Add(FGravityWell{ Body.Position, Body.GravParam, Body.Radius });
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
    Wells.Append(TestWells);
    FlightState.SetSurfaces(MoveTemp(Surfaces));
    FlightState.SetWells(MoveTemp(Wells));
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
    const int32 VerticalUps = Helm.VerticalUpPresses;
    const int32 VerticalDowns = Helm.VerticalDownPresses;
    Helm.VerticalUpPresses = 0;
    Helm.VerticalDownPresses = 0;
    bVerticalUpHoldSpent &= Helm.bVerticalUpHeld;
    bVerticalDownHoldSpent &= Helm.bVerticalDownHeld;

    // A key held through an all stop moves nothing until it is let go.
    bUpHoldSpent &= Helm.bUpHeld;
    bDownHoldSpent &= Helm.bDownHeld;
    const bool bUpHeld = Helm.bUpHeld && !bUpHoldSpent;
    const bool bDownHeld = Helm.bDownHeld && !bDownHoldSpent;

    FShipFlightCommand Command = FlightState.GetCommand();
    // The drive's notches only while the drive takes the ship: under a solid
    // world's floor (DriveBelowFloor) the ship flies cruise, and Shift and
    // Ctrl move cruise's lever.
    if (FlightState.GetMode() == EFlightMode::Drive)
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
            DeltaSeconds, FMath::Max(0.0f, CVarCruiseSweep.GetValueOnGameThread()), FlightState.CruiseAsternLimit());
    }

    // The vertical lever (landing decision 8), whichever lever F has live: a
    // press the way the ship is already moving catches it there when the
    // lever is at HOVER (after X, the 2026-09-26 ruling), then the sweep,
    // which stops at HOVER and leaves it only on a fresh press.
    const bool bVerticalUp = Helm.bVerticalUpHeld && !bVerticalUpHoldSpent;
    const bool bVerticalDown = Helm.bVerticalDownHeld && !bVerticalDownHoldSpent;
    const double Top = FlightState.GetLimits().VerticalTop;
    Command.Vertical = ShipVerticalLever::Catch(Command.Vertical, VerticalUps, VerticalDowns, FlightState.GetVerticalSpeed(), Top);
    Command.Vertical = ShipVerticalLever::Sweep(Command.Vertical, bVerticalUp, bVerticalDown, VerticalUps, VerticalDowns,
                                                DeltaSeconds, FMath::Max(0.0f, CVarVerticalSweep.GetValueOnGameThread()));
    FlightState.SetCommand(Command);
}

void UShipSubsystem::StepNavigation(float DeltaSeconds)
{
    FNavTuning Tuning;
    Tuning.ConeRadians = GetJumpConeRadians();
    Tuning.TransitSeconds = FMath::Max(0.0f, CVarTransitSeconds.GetValueOnGameThread());

    // Before the step, so a fold never opens inside the reach it would
    // carry the ship backward from.
    LetGoOfNearWorldCourse();

    const TOptional<FVector> Course = GetCourseDirectionShipLocal();
    const double OffBoresight = Course ? ShipNav::OffBoresight(*Course) : UE_DOUBLE_PI;

    // Which world an in-system fold is for, asked before the step that
    // clears the course on arrival.
    const TOptional<FBodyId> WorldCourse = NavState.GetPlottedWorld();

    switch (NavState.Step(DeltaSeconds, FlightState.GetJumpCharge(), OffBoresight, Tuning))
    {
    case ENavEvent::TransitBegan:
    {
        FoldDeparture.Reset();
        if (WorldCourse)
        {
            FoldDeparture = FlightState.GetUniversePosition();
        }
        // The fold has opened, and the helm does nothing between stars. It is
        // an all stop, too (flight-feel decision 4): both levers to STOP, so
        // the ship comes out of every jump at rest -- JumpTo makes that exact
        // -- and the first speed in the new place is the pilot's to choose.
        FlightState.SpendJumpCharge();
        FlightState.ReleaseAttitude();
        FShipFlightCommand Stopped = FlightState.GetCommand();
        Stopped.Throttle = 0.0;
        Stopped.DriveNotch = 0;
        Stopped.Vertical = 0.0;   // HOVER: all stop means the ship holds where it is (sign-off 17)
        FlightState.SetCommand(Stopped);
        Helm = FHelmInput();

        // And, like every other stop, nothing held through it moves a lever
        // until it is let go and pressed again. Marked for the first hands
        // after arrival, not spent here: the helm is emptied every frame in
        // transit, which would clear a spent flag before the key came back.
        bAwaitingFirstHands = true;
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
            FlightState.JumpTo(NavStart::ArrivalPoint(FlightState.GetUniversePosition(), *Star, GetStandoffAU()));
        }
        break;
    }

    case ENavEvent::ArrivedAtWorld:
    {
        // The same translation, to a world (map decision 12): on the line
        // from where the fold opened to its centre, at the standoff that
        // shows it ds.Nav.WorldStandoffDeg across, outside every floor. The
        // ship is at rest (JumpTo) with both levers at STOP since the fold
        // opened, and still facing the world it aligned with.
        const TOptional<FWorldFix> World = WorldCourse ? FixWorld(*WorldCourse) : TOptional<FWorldFix>();
        const FUniversePosition From = FoldDeparture.Get(FlightState.GetUniversePosition());
        FoldDeparture.Reset();
        if (World)
        {
            FlightState.JumpTo(NavStart::WorldArrivalPoint(From, World->Centre, World->Radius, World->Floor,
                                                           GetWorldStandoffDeg(), World->Others));
        }
        break;
    }

    case ENavEvent::None:
        break;
    }
}

TOptional<UShipSubsystem::FWorldFix> UShipSubsystem::FixWorld(const FStarSystem& Here, const FBodyId& World)
{
    if (!ShipNav::TargetPlanet(Here, World))
    {
        return {};
    }
    // The world as the sky and the flight law see it -- body i + 1, the star
    // first -- so the standoff is from the disc the window draws and the
    // floor is the one the drive stops at.
    const FSkySystem Sky = LocalSystem::Here(TOptional<FStarSystem>(Here));
    const int32 Index = World.Planet + 1;
    if (!Sky.Bodies.IsValidIndex(Index))
    {
        return {};
    }
    FWorldFix Fix;
    Fix.Centre = Sky.Bodies[Index].Position;
    Fix.Radius = Sky.Bodies[Index].Radius;
    Fix.Floor = FloorFor(Sky.Bodies[Index]);
    Fix.Ground = Sky.Bodies[Index].Ground;
    Fix.Relief = Sky.Bodies[Index].Relief;
    for (int32 Other = 0; Other < Sky.Bodies.Num(); ++Other)
    {
        if (Other != Index)
        {
            FFlightSurface Surface;
            Surface.Centre = Sky.Bodies[Other].Position;
            Surface.Radius = Sky.Bodies[Other].Radius;
            Surface.Floor = FloorFor(Sky.Bodies[Other]);
            // The in-system jump's guard is the drive's sphere: Others carry
            // no ground, as the drive reads none.
            Surface.bWorld = Sky.Bodies[Other].Kind != ESkyBodyKind::Star;
            Fix.Others.Add(Surface);
        }
    }
    return Fix;
}

TOptional<UShipSubsystem::FWorldFix> UShipSubsystem::FixWorld(const FBodyId& World) const
{
    const UUniverseSubsystem* Cosmos = Universe();
    const TOptional<FStarSystem> System = Cosmos ? Cosmos->GetSystem(World.System) : TOptional<FStarSystem>();
    return System ? FixWorld(*System, World) : TOptional<FWorldFix>();
}

void UShipSubsystem::LetGoOfNearWorldCourse()
{
    const TOptional<FBodyId>& World = NavState.GetPlottedWorld();
    if (!World || NavState.IsInTransit())
    {
        return;
    }
    // Resolved against the system the ship is in, not the one the id names:
    // a PlaceShip into another system leaves a course to a world that is no
    // longer here, and a jump to it would cross between stars.
    const UUniverseSubsystem* Cosmos = Universe();
    const FUniversePosition Where = FlightState.GetUniversePosition();
    const TOptional<FSystemId> Here = Cosmos ? Cosmos->GetSystemIdAt(Where) : TOptional<FSystemId>();
    const TOptional<FWorldFix> Fix = (Here && *Here == World->System) ? FixWorld(*World) : TOptional<FWorldFix>();
    const bool bNear = Fix && Where.DistanceTo(Fix->Centre)
        < NavStart::WorldReachFactor * NavStart::WorldStandoffCm(Fix->Radius, Fix->Floor, GetWorldStandoffDeg());
    if (!Fix || bNear)
    {
        // As if arrived, with the charge unspent: ClearPlot stands the jump
        // down, and the charge holds wherever it had wound to.
        NavState.ClearPlot();
    }
}

bool UShipSubsystem::Register(UShipModuleDataAsset* Part)
{
    if (!Part || Part->Bay == EShipBay::None || Part->ModuleId.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("Ship: refused %s: a part needs a bay and an id."),
               Part ? *Part->GetName() : TEXT("nothing"));
        return false;
    }
    const TObjectPtr<UShipModuleDataAsset>* Known = KnownParts.Find(Part->ModuleId);
    if (Known && Known->Get() != Part)
    {
        UE_LOG(LogTemp, Warning, TEXT("Ship: refused %s: %s already names %s."),
               *Part->GetName(), *Part->ModuleId.ToString(), *(*Known)->GetName());
        return false;
    }
    KnownParts.Add(Part->ModuleId, Part);
    return true;
}

TOptional<EShipBay> UShipSubsystem::SlotFor(const UShipModuleDataAsset& Part) const
{
    if (ShipBay::IsCore(Part.Bay))
    {
        return Part.Bay;
    }
    if (!ShipBay::IsAux(Part.Bay))
    {
        return {};
    }
    // One of a kind (decision 8, rule 3): the slot already holding this part
    // is the one it goes in, so the two slots never hold it twice.
    for (const EShipBay Aux : { EShipBay::Aux1, EShipBay::Aux2 })
    {
        if (ShipParts::FindBay(Loadout, Aux)->Part.PartId == Part.ModuleId)
        {
            return Aux;
        }
    }
    for (const EShipBay Aux : { EShipBay::Aux1, EShipBay::Aux2 })
    {
        if (ShipParts::FindBay(Loadout, Aux)->Part.PartId.IsNone())
        {
            return Aux;
        }
    }
    return EShipBay::Aux1;
}

bool UShipSubsystem::FitPart(UShipModuleDataAsset* Part)
{
    if (!Register(Part))
    {
        return false;
    }
    const TOptional<EShipBay> Slot = SlotFor(*Part);
    if (!Slot)
    {
        return false;
    }
    if (ShipParts::FindBay(Loadout, *Slot)->Part.PartId == Part->ModuleId)
    {
        // Already fitted: nothing to swap, and no spare to make of it.
        return true;
    }
    FShipPartState Fresh;
    Fresh.PartId = Part->ModuleId;
    FitState(*Slot, Fresh);
    return true;
}

void UShipSubsystem::FitState(EShipBay Bay, const FShipPartState& Part)
{
    const FShipPartState Displaced = ShipParts::FindBay(Loadout, Bay)->Part;
    if (!Displaced.PartId.IsNone())
    {
        Loadout.Spares.Add(Displaced);
    }
    SetBayPart(Bay, Part);
}

bool UShipSubsystem::RemovePart(EShipBay Bay)
{
    if (!ShipBay::IsAux(Bay))
    {
        return false;
    }
    const FShipPartState Removed = ShipParts::FindBay(Loadout, Bay)->Part;
    if (Removed.PartId.IsNone())
    {
        return false;
    }
    Loadout.Spares.Add(Removed);
    SetBayPart(Bay, FShipPartState());
    return true;
}

void UShipSubsystem::SetBayPart(EShipBay Bay, const FShipPartState& Part)
{
    ShipParts::FindBay(Loadout, Bay)->Part = Part;

    // Booked by bay, never by part (decision 4): a swap is one key's
    // RemoveDraw and AddDraw, so two parts in one bay can never both draw.
    const FName Key = ShipBay::DrawKey(Bay);
    PowerState.RemoveDraw(Key);
    const UShipModuleDataAsset* Fitted = GetFittedPart(Bay);
    if (Fitted && Fitted->PowerDraw > 0.0f)
    {
        PowerState.AddDraw(Key, Fitted->PowerDraw);
    }
    PushRatings();
}

void UShipSubsystem::PushRatings()
{
    const FShipRatings Rated = GetRatings();
    PowerState.SetReactorOutput(static_cast<float>(Rated.ReactorWatts));
    if (bLightsOn)
    {
        PowerState.SetWant(ShipPower::Lights, static_cast<float>(Rated.LightsWant));
    }
    // Not the boosters' want: ApplyAllocation is its one writer (sign-off
    // 29), and writes it from these ratings on its next pass.
}

UShipModuleDataAsset* UShipSubsystem::GetFittedPart(EShipBay Bay) const
{
    const FShipBayState* Entry = ShipParts::FindBay(Loadout, Bay);
    const TObjectPtr<UShipModuleDataAsset>* Part = Entry ? KnownParts.Find(Entry->Part.PartId) : nullptr;
    return Part ? Part->Get() : nullptr;
}

TArray<UShipModuleDataAsset*> UShipSubsystem::GetInstalledModules() const
{
    TArray<UShipModuleDataAsset*> Fitted;
    for (const EShipBay Bay : ShipBay::All())
    {
        if (UShipModuleDataAsset* Part = GetFittedPart(Bay))
        {
            Fitted.Add(Part);
        }
    }
    return Fitted;
}

TArray<UShipModuleDataAsset*> UShipSubsystem::GetCatalogue() const
{
    TArray<UShipModuleDataAsset*> Parts;
    const UShipPartCatalogue* Catalogue = Cast<UShipPartCatalogue>(CatalogueAsset.TryLoad());
    if (!Catalogue)
    {
        return Parts;
    }
    for (const TSoftObjectPtr<UShipModuleDataAsset>& Soft : Catalogue->Parts)
    {
        if (UShipModuleDataAsset* Part = Soft.LoadSynchronous())
        {
            Parts.Add(Part);
        }
    }
    return Parts;
}

UShipModuleDataAsset* UShipSubsystem::FindPart(FName PartId) const
{
    if (PartId.IsNone())
    {
        return nullptr;
    }
    for (UShipModuleDataAsset* Part : GetCatalogue())
    {
        if (Part->ModuleId == PartId)
        {
            return Part;
        }
    }
    return nullptr;
}

UShipModuleDataAsset* UShipSubsystem::PartFor(FName PartId) const
{
    if (const TObjectPtr<UShipModuleDataAsset>* Known = KnownParts.Find(PartId))
    {
        return Known->Get();
    }
    return FindPart(PartId);
}

bool UShipSubsystem::FitPartById(FName PartId)
{
    UShipModuleDataAsset* Part = PartFor(PartId);
    if (!Part || !Register(Part))
    {
        return false;
    }
    const TOptional<EShipBay> Slot = SlotFor(*Part);
    if (!Slot)
    {
        return false;
    }
    const int32 Spare = Loadout.Spares.IndexOfByPredicate([PartId](const FShipPartState& State) { return State.PartId == PartId; });
    if (Spare == INDEX_NONE)
    {
        return FitPart(Part);
    }
    // A spare is one particular part, fitted as it is (decision 10): it keeps
    // its own state, and the part it displaces keeps its.
    const FShipPartState State = Loadout.Spares[Spare];
    Loadout.Spares.RemoveAt(Spare);
    FitState(*Slot, State);
    return true;
}

bool UShipSubsystem::AddSpare(FName PartId)
{
    UShipModuleDataAsset* Part = PartFor(PartId);
    if (!Part || !Register(Part))
    {
        return false;
    }
    FShipPartState Spare;
    Spare.PartId = PartId;
    Loadout.Spares.Add(Spare);
    return true;
}

void UShipSubsystem::ClearSpares()
{
    Loadout.Spares.Reset();
}

int32 UShipSubsystem::RestoreLoadout(const FShipLoadoutState& Given)
{
    // A copy, because Given may be this ship's own loadout
    // (RestoreLoadout(GetLoadoutState())): the spares are reset below, and
    // through the alias they would be read back empty.
    const FShipLoadoutState State = Given;
    int32 Fallbacks = 0;
    TSet<FName> BaysSeen;
    for (const FShipBayState& Entry : State.Bays)
    {
        if (!ShipBay::FromName(Entry.Bay))
        {
            UE_LOG(LogTemp, Warning, TEXT("Ship: a loadout names a bay this ship has not got, %s; ignored."), *Entry.Bay.ToString());
            ++Fallbacks;
        }
        else if (BaysSeen.Contains(Entry.Bay))
        {
            // The first entry of a bay is the one restored, below.
            UE_LOG(LogTemp, Warning, TEXT("Ship: a loadout names the %s bay twice; the second, %s, is ignored."),
                   *Entry.Bay.ToString(), *Entry.Part.PartId.ToString());
            ++Fallbacks;
        }
        BaysSeen.Add(Entry.Bay);
    }

    Loadout.Spares.Reset();
    TSet<FName> AuxRestored;
    for (int32 Index = 0; Index < ShipBay::All().Num(); ++Index)
    {
        const EShipBay Bay = ShipBay::All()[Index];
        // By name, never by position: a bay added to EShipBay later shifts
        // nothing saved (decision 11).
        const FShipBayState* Entry = State.Bays.FindByPredicate([Bay](const FShipBayState& Candidate) { return Candidate.Bay == ShipBay::Name(Bay); });
        FShipPartState Part = Entry ? Entry->Part : FShipPartState();
        bool bFellBack = !Entry && ShipBay::IsCore(Bay);
        if (!Part.PartId.IsNone())
        {
            UShipModuleDataAsset* Asset = PartFor(Part.PartId);
            const bool bFits = Asset && Register(Asset)
                && (ShipBay::IsAux(Bay) ? ShipBay::IsAux(Asset->Bay) : Asset->Bay == Bay);
            // One of a kind (decision 8, rule 3), as SlotFor keeps it for a
            // fit: the aux slot restored first keeps the part, and the other
            // is left empty.
            const bool bTwice = bFits && ShipBay::IsAux(Bay) && AuxRestored.Contains(Part.PartId);
            if (!bFits)
            {
                UE_LOG(LogTemp, Warning, TEXT("Ship: %s cannot be in the %s bay; its stock part is fitted instead."),
                       *Part.PartId.ToString(), *ShipBay::Name(Bay).ToString());
                bFellBack = true;
            }
            else if (bTwice)
            {
                UE_LOG(LogTemp, Warning, TEXT("Ship: %s is in both aux slots; the %s slot is left empty."),
                       *Part.PartId.ToString(), *ShipBay::Name(Bay).ToString());
                bFellBack = true;
            }
        }
        if (bFellBack)
        {
            ++Fallbacks;
            Part = FShipPartState();
            UShipModuleDataAsset* Stock = ShipBay::IsCore(Bay) ? PartFor(ShipBay::StockPartId(Bay)) : nullptr;
            if (Stock && Register(Stock))
            {
                Part.PartId = Stock->ModuleId;
            }
        }
        SetBayPart(Bay, Part);
        if (ShipBay::IsAux(Bay) && !Part.PartId.IsNone())
        {
            AuxRestored.Add(Part.PartId);
        }
        ShipParts::FindBay(Loadout, Bay)->LivesDrawn = Entry ? Entry->LivesDrawn : 0;
    }

    for (const FShipPartState& Spare : State.Spares)
    {
        UShipModuleDataAsset* Asset = PartFor(Spare.PartId);
        if (Asset && Register(Asset))
        {
            Loadout.Spares.Add(Spare);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Ship: a spare names no part, %s; dropped."), *Spare.PartId.ToString());
            ++Fallbacks;
        }
    }
    return Fallbacks;
}

const FShipLoadoutState& UShipSubsystem::GetLoadoutState() const
{
    return Loadout;
}

const TArray<FShipPartState>& UShipSubsystem::GetSpares() const
{
    return Loadout.Spares;
}

void UShipSubsystem::AddLoad(FName Name, float Watts)
{
    const FName Key(*(FString(TEXT("Load.")) + Name.ToString()));
    PowerState.RemoveDraw(Key);
    PowerState.AddDraw(Key, FMath::Max(0.0f, Watts));
}

bool UShipSubsystem::RemoveLoad(FName Name)
{
    return PowerState.RemoveDraw(FName(*(FString(TEXT("Load.")) + Name.ToString())));
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
        bVerticalUpHoldSpent = Input.bVerticalUpHeld;
        bVerticalDownHoldSpent = Input.bVerticalDownHeld;
    }
    Helm.Attitude = Input.Attitude;
    Helm.bUpHeld = Input.bUpHeld;
    Helm.bDownHeld = Input.bDownHeld;
    Helm.UpPresses += FMath::Max(0, Input.UpPresses);
    Helm.DownPresses += FMath::Max(0, Input.DownPresses);
    Helm.bVerticalUpHeld = Input.bVerticalUpHeld;
    Helm.bVerticalDownHeld = Input.bVerticalDownHeld;
    Helm.VerticalUpPresses += FMath::Max(0, Input.VerticalUpPresses);
    Helm.VerticalDownPresses += FMath::Max(0, Input.VerticalDownPresses);
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
    Command.Vertical = 0.0;
    FlightState.SetCommand(Command);

    // Nothing pressed before the stop survives it, and nothing held through
    // it moves a lever until it is let go and pressed again.
    Helm.UpPresses = 0;
    Helm.DownPresses = 0;
    bUpHoldSpent = Helm.bUpHeld;
    bDownHoldSpent = Helm.bDownHeld;
    Helm.VerticalUpPresses = 0;
    Helm.VerticalDownPresses = 0;
    bVerticalUpHoldSpent = Helm.bVerticalUpHeld;
    bVerticalDownHoldSpent = Helm.bVerticalDownHeld;
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

bool UShipSubsystem::SetVerticalLever(APawn* Commander, double Lever)
{
    if (!MayCommand(Commander))
    {
        return false;
    }
    FShipFlightCommand Command = FlightState.GetCommand();
    Command.Vertical = Lever;
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

bool UShipSubsystem::PlotTarget()
{
    const TOptional<FBodyId>& Target = NavState.GetTarget();
    const UUniverseSubsystem* Cosmos = Universe();
    if (NavState.IsInTransit() || !Target || !Cosmos)
    {
        return false;
    }
    const TOptional<FStarSystem> Here = Cosmos->GetSystemAt(FlightState.GetUniversePosition());
    if (!Here || !ShipNav::TargetPlanet(*Here, *Target) || IsNearEnoughToFly(*Here, *Target))
    {
        return false;
    }
    return NavState.PlotWorld(*Target);
}

TOptional<FBodyId> UShipSubsystem::GetPlottedWorld() const
{
    return NavState.GetPlottedWorld();
}

bool UShipSubsystem::HasCourse() const
{
    return NavState.HasCourse();
}

bool UShipSubsystem::SetTarget(const FBodyId& Id)
{
    const UUniverseSubsystem* Cosmos = Universe();
    if (NavState.IsInTransit() || !Cosmos)
    {
        return false;
    }
    // In the system the ship is in, asked of its position without
    // generating it; then the orbit, which only the system itself knows.
    const TOptional<FSystemId> Here = Cosmos->GetSystemIdAt(FlightState.GetUniversePosition());
    if (!Here || *Here != Id.System)
    {
        return false;
    }
    const TOptional<FStarSystem> System = Cosmos->GetSystem(*Here);
    if (!System || !ShipNav::TargetPlanet(*System, Id))
    {
        return false;
    }
    return NavState.SetTarget(Id);
}

void UShipSubsystem::ClearTarget()
{
    NavState.ClearTarget();
}

TOptional<FBodyId> UShipSubsystem::GetTarget() const
{
    return NavState.GetTarget();
}

bool UShipSubsystem::CycleTarget()
{
    const UUniverseSubsystem* Cosmos = Universe();
    if (NavState.IsInTransit() || !Cosmos)
    {
        return false;
    }
    const TOptional<FStarSystem> Here = Cosmos->GetSystemAt(FlightState.GetUniversePosition());
    const TOptional<FBodyId> Next = Here ? ShipNav::NextTarget(*Here, NavState.GetTarget()) : TOptional<FBodyId>();
    return Next && SetTarget(*Next);
}

TOptional<FTargetView> UShipSubsystem::GetTargetView(const FStarSystem& Here) const
{
    const TOptional<FBodyId>& Target = NavState.GetTarget();
    const TOptional<FWorldFix> Fix = Target ? FixWorld(Here, *Target) : TOptional<FWorldFix>();
    if (!Fix)
    {
        return {};
    }
    // The braking the boosters have now and the cap's law as the flight
    // state is running it, so the time is the flight's own at this moment's
    // power, not a copy of it at full thrust. With both laws at once, a
    // starved cruise's ETA ran about a sixth short, and counted down faster
    // than the clock. Which law depends on the lever flying, not on the
    // mode's name: the drive (and its spool-down) holds 4 s off its floor
    // before it brakes; cruise -- and DriveBelowFloor, which flies cruise --
    // brakes on the curve alone, and over solid ground flies to the ground
    // itself.
    const EFlightMode Mode = FlightState.GetMode();
    const bool bCruiseFlies = Mode == EFlightMode::Cruise || Mode == EFlightMode::DriveBelowFloor;
    const FShipFlightLimits& Limits = FlightState.GetLimits();
    TOptional<FTargetView> View = TargetMarker::View(Here, *Target, FlightState.GetUniversePosition(), FlightState.GetUniverseOrientation(),
                                                     FlightState.GetVelocity(), Fix->Floor, Limits.LinearAcceleration,
                                                     bCruiseFlies ? 0.0 : Limits.HoldSeconds, NavState.IsInTransit());
    const double Speed = FlightState.GetSpeed();
    if (View && bCruiseFlies && Fix->Ground == EGround::Solid && Speed >= TargetMarker::MinSpeed)
    {
        // Decision 12: to the ground under the law the ship is flying,
        // measured along the velocity.
        FFlightSurface Surface;
        Surface.Centre = Fix->Centre;
        Surface.Radius = Fix->Radius;
        Surface.Floor = Fix->Floor;
        Surface.bWorld = true;
        Surface.Ground = ShipGround::FromRelief(Fix->Relief);
        const FVector Along = FlightState.GetVelocity() / Speed;
        const double Reach = FlightState.GetUniversePosition().DistanceTo(Fix->Centre);
        if (const TOptional<double> Hit = ShipFlight::RayToGround(Surface, FlightState.GetUniversePosition(), Along, Limits.GearClearanceCm, 2.0 * Reach))
        {
            if (FlightState.IsInNearRegime())
            {
                // Inside the regime: the approach law with its knee, and the
                // skim cap where the path is shallow.
                const FVector Up = (FlightState.GetUniversePosition() - Fix->Centre).GetSafeNormal();
                const ShipFlight::FGroundLaw Law{ Limits.LinearAcceleration, Limits.ApproachSeconds, Limits.TouchdownSpeed,
                                                  Limits.SkimSeconds, Limits.SkimFloor };
                const double Sine = FMath::Max(-(Along | Up), 1.0e-6);
                // What arrives is the footprint's least point, which the
                // approach law holds (FootprintClearance 0), not the origin:
                // over uneven ground a foot meets the rock sooner than the ray
                // under the origin says, by the shortfall between the two
                // heights. Without it the ETA's last ten seconds ran slow.
                double Path = *Hit;
                const TOptional<double> Agl = FlightState.GetGroundAltitude();
                const TOptional<double> Foot = FlightState.GetFootprintClearance();
                if (Agl && Foot)
                {
                    const double Shortfall = FMath::Max(0.0, (*Agl - Limits.GearClearanceCm) - *Foot);
                    Path = FMath::Max(0.0, Path - Shortfall / Sine);
                }
                View->EtaSeconds = ShipFlight::SecondsToGround(Path, Speed, Sine, Law);
            }
            else
            {
                // Above it: cruise's braking curve alone, to where it stops --
                // the hull's reach short of the ray, as the flight state's
                // NearestOnCruisePath has it. The approach law here named a
                // time up to half a minute off, and counted seconds of five.
                const double Stop = FMath::Max(0.0, *Hit - ShipLanding::ReachCm(Limits.GearClearanceCm));
                const double Seconds = ShipFlight::SecondsToFloor(Stop, Speed, Limits.LinearAcceleration, 0.0);
                if (FMath::IsFinite(Seconds))
                {
                    View->EtaSeconds = Seconds;
                }
            }
            View->PassingCm.Reset();
        }
    }
    return View;
}

bool UShipSubsystem::IsNearEnoughToFly(const FStarSystem& Here, const FBodyId& World) const
{
    const TOptional<FWorldFix> Fix = FixWorld(Here, World);
    return Fix && FlightState.GetUniversePosition().DistanceTo(Fix->Centre)
        < NavStart::WorldReachFactor * NavStart::WorldStandoffCm(Fix->Radius, Fix->Floor, GetWorldStandoffDeg());
}

float UShipSubsystem::GetStandoffAU()
{
    return FMath::Max(0.0f, CVarStandoffAU.GetValueOnGameThread());
}

double UShipSubsystem::GetWorldStandoffDeg()
{
    return FMath::Max(0.0, static_cast<double>(CVarWorldStandoffDeg.GetValueOnGameThread()));
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
    const TOptional<FBodyId>& World = NavState.GetPlottedWorld();
    if (!Cosmos || !(Plotted || World))
    {
        return {};
    }
    const TOptional<FStarSystem> System = Cosmos->GetSystem(Plotted ? *Plotted : World->System);
    if (!System)
    {
        return {};
    }
    // The star, or the world's centre: the cone is round the direction to
    // the middle of the disc, which is where the bracket is.
    FUniversePosition Toward = System->Stub.Position;
    if (World)
    {
        if (!ShipNav::TargetPlanet(*System, *World))
        {
            return {};
        }
        Toward = System->PlanetPosition(World->Planet);
    }
    const FVector Direction = (Toward - FlightState.GetUniversePosition()).GetSafeNormal();
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

FShipRatings UShipSubsystem::GetRatings() const
{
    FShipRatings Ratings = FShipRatings::Stock();
    for (const FShipBayState& Entry : Loadout.Bays)
    {
        if (const TObjectPtr<UShipModuleDataAsset>* Part = KnownParts.Find(Entry.Part.PartId))
        {
            ShipParts::Apply(Ratings, (*Part)->Ratings);
        }
    }
    return Ratings;
}

float UShipSubsystem::GetWindingWant() const
{
    return static_cast<float>(ShipParts::Effective(GetRatings().WindingWant, CVarWindingWant.GetValueOnGameThread()));
}

float UShipSubsystem::GetChargeSeconds() const
{
    return static_cast<float>(ShipParts::Effective(GetRatings().ChargeSeconds, CVarChargeSeconds.GetValueOnGameThread()));
}

float UShipSubsystem::GetDriveResponse() const
{
    return static_cast<float>(ShipParts::Effective(GetRatings().DriveResponse, CVarDriveResponse.GetValueOnGameThread()));
}

float UShipSubsystem::GetChartRangeLy() const
{
    return static_cast<float>(ShipParts::Effective(GetRatings().RangeLy, CVarRangeLy.GetValueOnGameThread()));
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
