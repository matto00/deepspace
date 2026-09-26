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

bool FShipNavState::Plot(const FSystemId& Id)
{
    if (bInTransit)
    {
        return false;
    }
    Plotted = Id;
    return true;
}

void FShipNavState::ClearPlot()
{
    if (bInTransit)
    {
        return;
    }
    Plotted.Reset();
    bEngaged = false;
}

const TOptional<FSystemId>& FShipNavState::GetPlotted() const
{
    return Plotted;
}

bool FShipNavState::SetEngaged(bool bOn)
{
    if (bInTransit || (bOn && !Plotted.IsSet()))
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
        const bool bFires = bEngaged && Plotted.IsSet() && JumpCharge >= 1.0
            && OffBoresightRadians <= Tuning.ConeRadians;
        if (bFires)
        {
            bInTransit = true;
            TransitElapsed = 0.0;
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
