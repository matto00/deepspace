#include "Ship/ShipNavState.h"

double ShipNav::OffBoresight(const FVector& ShipLocalDir)
{
    const FVector Dir = ShipLocalDir.GetSafeNormal();
    if (Dir.IsZero())
    {
        // No direction is no alignment: a degenerate course must never fire.
        return UE_DOUBLE_PI;
    }
    return FMath::Acos(FMath::Clamp(Dir.X, -1.0, 1.0));
}

const FPlanet* ShipNav::TargetPlanet(const FStarSystem& Here, const FBodyId& Id)
{
    if (Id.System != Here.Stub.Id || Id.Moon != -1 || !Here.Planets.IsValidIndex(Id.Planet))
    {
        return nullptr;
    }
    return &Here.Planets[Id.Planet];
}

TOptional<FBodyId> ShipNav::NextTarget(const FStarSystem& Here, const TOptional<FBodyId>& Target)
{
    if (Here.Planets.IsEmpty())
    {
        return {};
    }
    // Orbit order is innermost first (procgen), so "outward" is the next
    // index. An id that names nothing here counts as none: Tab in a new
    // system starts from its innermost world, not from an old system's
    // orbit number.
    const bool bHeld = Target.IsSet() && TargetPlanet(Here, *Target) != nullptr;
    const int32 Next = bHeld ? (Target->Planet + 1) % Here.Planets.Num() : 0;
    return FBodyId{ Here.Stub.Id, Next, -1 };
}

bool FShipNavState::Plot(const FSystemId& Id)
{
    if (bInTransit)
    {
        return false;
    }
    Plotted = Id;
    PlottedWorld.Reset();
    return true;
}

bool FShipNavState::PlotWorld(const FBodyId& Id)
{
    // Only the target: the bracket and the jump name one world or the jump
    // names none.
    if (bInTransit || !Target.IsSet() || *Target != Id)
    {
        return false;
    }
    PlottedWorld = Id;
    Plotted.Reset();
    return true;
}

void FShipNavState::ClearPlot()
{
    if (bInTransit)
    {
        return;
    }
    Plotted.Reset();
    PlottedWorld.Reset();
    bEngaged = false;
}

const TOptional<FSystemId>& FShipNavState::GetPlotted() const
{
    return Plotted;
}

const TOptional<FBodyId>& FShipNavState::GetPlottedWorld() const
{
    return PlottedWorld;
}

bool FShipNavState::HasCourse() const
{
    return Plotted.IsSet() || PlottedWorld.IsSet();
}

bool FShipNavState::SetTarget(const FBodyId& Id)
{
    if (bInTransit)
    {
        return false;
    }
    if (PlottedWorld.IsSet() && *PlottedWorld != Id)
    {
        // The course was the old target; a jump engaged toward a world that
        // is no longer marked would fold somewhere the bracket is not.
        ClearPlot();
    }
    Target = Id;
    return true;
}

void FShipNavState::ClearTarget()
{
    if (bInTransit)
    {
        return;
    }
    if (PlottedWorld.IsSet())
    {
        ClearPlot();
    }
    Target.Reset();
}

const TOptional<FBodyId>& FShipNavState::GetTarget() const
{
    return Target;
}

bool FShipNavState::SetEngaged(bool bOn)
{
    if (bInTransit || (bOn && !HasCourse()))
    {
        return false;
    }
    bEngaged = bOn;
    return true;
}

bool FShipNavState::IsEngaged() const
{
    return bEngaged;
}

ENavEvent FShipNavState::Step(double DeltaSeconds, double JumpCharge, double OffBoresightRadians,
                              const FNavTuning& Tuning)
{
    TransitSeconds = FMath::Max(0.0, Tuning.TransitSeconds);

    if (!bInTransit)
    {
        const bool bFires = bEngaged && HasCourse() && JumpCharge >= 1.0
            && OffBoresightRadians <= Tuning.ConeRadians;
        if (bFires)
        {
            bInTransit = true;
            TransitElapsed = 0.0;
            // Leaving a system lets go of what was marked in it: the id
            // names a system the ship is no longer in. A jump within the
            // system keeps it, since the target is where it is going.
            if (Plotted.IsSet())
            {
                Target.Reset();
            }
            return ENavEvent::TransitBegan;
        }
        return ENavEvent::None;
    }

    TransitElapsed += FMath::Max(0.0, DeltaSeconds);
    if (TransitElapsed < TransitSeconds)
    {
        return ENavEvent::None;
    }

    // Engage was a one-shot "go", and the going is done; the course clears
    // because you are there. The throttle and the drive are not ours, and
    // stay wherever they were left.
    if (PlottedWorld.IsSet())
    {
        // Somewhere in the same system: nothing new has been visited, and
        // the last star arrived at is still the last one.
        PlottedWorld.Reset();
        bEngaged = false;
        bInTransit = false;
        TransitElapsed = 0.0;
        ++JumpSerial;
        return ENavEvent::ArrivedAtWorld;
    }
    LastArrival = Plotted;
    if (Plotted.IsSet())
    {
        Visited.Add(Plotted.GetValue());
    }
    Plotted.Reset();
    bEngaged = false;
    bInTransit = false;
    TransitElapsed = 0.0;
    ++JumpSerial;
    return ENavEvent::Arrived;
}

EJumpState FShipNavState::GetJumpState(double JumpCharge) const
{
    if (bInTransit)
    {
        return EJumpState::Transit;
    }
    if (!bEngaged)
    {
        return EJumpState::Idle;
    }
    return JumpCharge >= 1.0 ? EJumpState::Ready : EJumpState::Winding;
}

bool FShipNavState::IsInTransit() const
{
    return bInTransit;
}

double FShipNavState::GetTransitProgress() const
{
    if (!bInTransit)
    {
        return 0.0;
    }
    return TransitSeconds > 0.0 ? FMath::Clamp(TransitElapsed / TransitSeconds, 0.0, 1.0) : 1.0;
}

int32 FShipNavState::GetJumpSerial() const
{
    return JumpSerial;
}

const TOptional<FSystemId>& FShipNavState::GetLastArrival() const
{
    return LastArrival;
}

void FShipNavState::MarkVisited(const FSystemId& Id)
{
    Visited.Add(Id);
}

bool FShipNavState::HasVisited(const FSystemId& Id) const
{
    return Visited.Contains(Id);
}
