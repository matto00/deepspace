#include "Ship/ShipVerticalLever.h"

#include "Ship/ShipDriveLever.h"
#include "Ship/ShipGravity.h"

double ShipVerticalLever::Rate(double Lever, double TopCmPerSecond)
{
    const double P = FMath::Clamp(Lever, -1.0, 1.0);
    if (P == 0.0 || !(TopCmPerSecond > 0.0))
    {
        return 0.0;
    }
    const double Magnitude = TopCmPerSecond <= FloorCmPerSecond
        ? FMath::Abs(P) * TopCmPerSecond
        : FloorCmPerSecond * FMath::Pow(TopCmPerSecond / FloorCmPerSecond, FMath::Abs(P));
    return P > 0.0 ? Magnitude : -Magnitude;
}

double ShipVerticalLever::LeverOf(double RateCmPerSecond, double TopCmPerSecond)
{
    const double Magnitude = FMath::Abs(RateCmPerSecond);
    if (Magnitude < 0.5 * FloorCmPerSecond || !(TopCmPerSecond > FloorCmPerSecond))
    {
        return 0.0;
    }
    // Off the detent by a hair at the floor itself: 1e-12 reads the floor
    // to 1e-11 of itself, so LeverOf inverts Rate there too.
    const double P = FMath::Clamp(FMath::Loge(FMath::Max(Magnitude, FloorCmPerSecond) / FloorCmPerSecond)
                                  / FMath::Loge(TopCmPerSecond / FloorCmPerSecond), 1.0e-12, 1.0);
    return RateCmPerSecond > 0.0 ? P : -P;
}

double ShipVerticalLever::Sweep(double Lever, bool bUpHeld, bool bDownHeld, int32 UpPresses, int32 DownPresses,
                                double Dt, double SweepRate)
{
    return ShipDriveLever::SweepCruise(Lever, bUpHeld, bDownHeld, UpPresses, DownPresses, Dt, SweepRate, 1.0);
}

double ShipVerticalLever::Catch(double Lever, int32 UpPresses, int32 DownPresses, double VerticalSpeedCmPerSecond,
                                double TopCmPerSecond)
{
    if (Lever != 0.0 || FMath::Abs(VerticalSpeedCmPerSecond) < FloorCmPerSecond)
    {
        return Lever;
    }
    const bool bWithTheShip = VerticalSpeedCmPerSecond < 0.0 ? DownPresses > 0 : UpPresses > 0;
    return bWithTheShip ? LeverOf(VerticalSpeedCmPerSecond, TopCmPerSecond) : Lever;
}

double ShipVerticalLever::ClimbTop(double TopCmPerSecond, double GravityCmS2, double HeavyFloor)
{
    if (!(GravityCmS2 > 0.0))
    {
        return TopCmPerSecond;
    }
    const double Light = FMath::Min(1.0, ShipFlight::StandardGravityCmS2 / GravityCmS2);
    return TopCmPerSecond * FMath::Max(FMath::Clamp(HeavyFloor, 0.0, 1.0), Light);
}
