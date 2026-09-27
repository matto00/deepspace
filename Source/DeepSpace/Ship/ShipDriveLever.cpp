#include "Ship/ShipDriveLever.h"

namespace
{
    constexpr double KmPerSecond = 1.0e5;   // cm/s
    constexpr double Light = ShipDriveLever::LightCmPerSecond;

    /**
     * The lever's notches, cm/s, STOP not included (decision 3, as the
     * 2026-09-27 ruling re-ranged it). A 1-2-5 series in km/s from 20, which
     * is cruise's top, so the drive begins where cruise ends, up to 20,000,
     * and then 0.1 c, the drive's top: every step x2 or x2.5 -- the same felt
     * step anywhere on the lever -- but the last, 20,000 km/s to 0.1 c, which
     * is x1.499. Round numbers in the unit the readout uses, so a speed the
     * player sets is one they can come back to.
     */
    constexpr double Table[] = {
        20.0 * KmPerSecond, 50.0 * KmPerSecond,
        100.0 * KmPerSecond, 200.0 * KmPerSecond, 500.0 * KmPerSecond,
        1000.0 * KmPerSecond, 2000.0 * KmPerSecond, 5000.0 * KmPerSecond,
        10000.0 * KmPerSecond, 20000.0 * KmPerSecond,
        0.1 * Light,
    };

    constexpr int32 Notches = UE_ARRAY_COUNT(Table);

    /** A position within a billionth of a notch of one is on it: the eased
     *  position of a ship held at a notch's speed comes back from
     *  PositionOf a rounding error either side, and a tap must read it as
     *  the notch it is, not the one beside it. */
    constexpr double OnNotch = 1.0e-9;

    /** A top this close above a notch's speed keeps the notch: a CVar of
     *  0.5 c is meant to keep 0.5 c, whatever its float made of it. */
    constexpr double TopTolerance = 1.0e-6;

    /** Notch 1..Notches' speed; the caller has range-checked. */
    double Speed(int32 Notch)
    {
        return Table[Notch - 1];
    }
}

double ShipDriveLever::CruiseSpeed(double Throttle, double TopCmPerSecond, double AsternTopCmPerSecond)
{
    // Zero, either zero, is the detent; NaN is read as it, not as astern.
    if (!(FMath::Abs(Throttle) > 0.0) || !(TopCmPerSecond > 0.0))
    {
        return 0.0;
    }
    const double P = FMath::Min(FMath::Abs(Throttle), 1.0);
    const double Speed = TopCmPerSecond > CruiseFloorCmPerSecond
        ? CruiseFloorCmPerSecond * FMath::Pow(TopCmPerSecond / CruiseFloorCmPerSecond, P)
        : P * TopCmPerSecond;
    if (Throttle > 0.0)
    {
        return Speed;
    }
    return -FMath::Min(Speed, FMath::Max(AsternTopCmPerSecond, 0.0));
}

double ShipDriveLever::CruiseAsternLimit(double TopCmPerSecond, double AsternTopCmPerSecond)
{
    if (!(AsternTopCmPerSecond > 0.0) || !(TopCmPerSecond > 0.0))
    {
        return 0.0;
    }
    if (AsternTopCmPerSecond >= TopCmPerSecond)
    {
        return 1.0;
    }
    if (TopCmPerSecond <= CruiseFloorCmPerSecond)
    {
        // The linear lever of a top under the floor.
        return AsternTopCmPerSecond / TopCmPerSecond;
    }
    if (AsternTopCmPerSecond < CruiseFloorCmPerSecond)
    {
        return 0.0;
    }
    return FMath::Loge(AsternTopCmPerSecond / CruiseFloorCmPerSecond)
         / FMath::Loge(TopCmPerSecond / CruiseFloorCmPerSecond);
}

int32 ShipDriveLever::TableNotches()
{
    return Notches;
}

int32 ShipDriveLever::NotchCount(double TopCmPerSecond)
{
    int32 Kept = 1;
    for (int32 Notch = 2; Notch <= Notches; ++Notch)
    {
        if (Speed(Notch) <= TopCmPerSecond * (1.0 + TopTolerance))
        {
            Kept = Notch;
        }
    }
    return Kept + 1;
}

double ShipDriveLever::NotchSpeed(int32 Notch)
{
    if (Notch <= 0)
    {
        return 0.0;
    }
    return Speed(FMath::Min(Notch, Notches));
}

double ShipDriveLever::SpeedAt(double Position)
{
    if (!(Position > 0.0))
    {
        return 0.0;
    }
    if (Position >= Notches)
    {
        return Speed(Notches);
    }
    const int32 Below = FMath::FloorToInt32(Position);
    const double Fraction = Position - Below;
    if (Below == 0)
    {
        // Linear from rest: a geometric law has no zero to start from.
        return Fraction * Speed(1);
    }
    return Speed(Below) * FMath::Pow(Speed(Below + 1) / Speed(Below), Fraction);
}

double ShipDriveLever::PositionOf(double SpeedCmPerSecond)
{
    if (!(SpeedCmPerSecond > 0.0))
    {
        return 0.0;
    }
    if (SpeedCmPerSecond >= Speed(Notches))
    {
        return Notches;
    }
    if (SpeedCmPerSecond < Speed(1))
    {
        return SpeedCmPerSecond / Speed(1);
    }
    int32 Below = 1;
    while (Below + 1 < Notches && Speed(Below + 1) <= SpeedCmPerSecond)
    {
        ++Below;
    }
    return Below + FMath::Loge(SpeedCmPerSecond / Speed(Below)) / FMath::Loge(Speed(Below + 1) / Speed(Below));
}

double ShipDriveLever::Ease(double Position, double Target, double Dt, double MaxRate, double Thrust)
{
    // Thrust scales the whole law, so it is the same as time running slower:
    // the ease below runs on the ship's own clock, Dt x Thrust.
    const double Rate = FMath::Max(MaxRate, 0.0);
    Dt *= FMath::Clamp(Thrust, 0.0, 1.0);
    if (!(Dt > 0.0) || Rate <= 0.0 || Position == Target)
    {
        return Position == Target ? Target : Position;
    }

    // Solved, not stepped, in three parts: at the rate limit until the
    // error is small enough that the exponential is slower than it, then the
    // exponential down to ArriveNotches, then the exponential's own pace
    // there, held steady, to the notch. The error only ever shrinks toward
    // zero and the last part ends on it, so the position cannot pass Target
    // however long Dt is, and it arrives in a finite time with no snap.
    const double Start = FMath::Abs(Target - Position);
    const double Sign = Target > Position ? 1.0 : -1.0;
    const double Knee = Rate * EaseSeconds;
    const double Tail = FMath::Min(ArriveNotches, Knee);
    double Error = Start;
    double Left = Dt;
    if (Error > Knee)
    {
        const double ToKnee = (Error - Knee) / Rate;
        if (Left <= ToKnee)
        {
            return Target - Sign * (Error - Rate * Left);
        }
        Error = Knee;
        Left -= ToKnee;
    }
    if (Error > Tail)
    {
        const double ToTail = EaseSeconds * FMath::Loge(Error / Tail);
        if (Left <= ToTail)
        {
            return Target - Sign * Error * FMath::Exp(-Left / EaseSeconds);
        }
        Error = Tail;
        Left -= ToTail;
    }
    Error -= Tail / EaseSeconds * Left;
    return Error <= 0.0 ? Target : Target - Sign * Error;
}

int32 ShipDriveLever::TapDown(int32 Notch, double Position)
{
    const int32 BelowShip = FMath::CeilToInt32(FMath::Max(Position, 0.0) - OnNotch) - 1;
    return FMath::Max(0, FMath::Min(Notch - 1, BelowShip));
}

int32 ShipDriveLever::TapUp(int32 Notch, double Position, int32 TopNotch)
{
    const int32 AboveShip = FMath::FloorToInt32(FMath::Max(Position, 0.0) + OnNotch) + 1;
    return FMath::Max(0, FMath::Min(TopNotch, FMath::Max(Notch + 1, AboveShip)));
}

int32 ShipDriveLever::FNotchRepeat::Update(bool bHeld, double Dt, double Rate)
{
    if (!bHeld)
    {
        HeldSeconds = 0.0;
        Fired = 0;
        return 0;
    }
    HeldSeconds += FMath::Max(Dt, 0.0);
    const double Repeating = FMath::Max(HeldSeconds - RepeatDelaySeconds, 0.0);
    const int32 Due = FMath::FloorToInt32(Repeating * FMath::Max(Rate, 0.0));
    const int32 Now = FMath::Max(Due - Fired, 0);
    Fired = FMath::Max(Fired, Due);
    return Now;
}

double ShipDriveLever::SweepCruise(double Throttle, bool bHeldUp, bool bHeldDown,
                                   int32 PressesUp, int32 PressesDown, double Dt, double Rate,
                                   double AsternLimit)
{
    const double Astern = FMath::Clamp(AsternLimit, 0.0, 1.0);
    const double From = FMath::Clamp(Throttle, -Astern, 1.0);
    const int32 Direction = (bHeldUp ? 1 : 0) - (bHeldDown ? 1 : 0);
    if (Direction == 0 || !(Dt > 0.0) || Rate <= 0.0)
    {
        return From;
    }
    if (From == 0.0 && (Direction > 0 ? PressesUp : PressesDown) <= 0)
    {
        return 0.0;
    }
    const double To = FMath::Clamp(From + Direction * Rate * Dt, -Astern, 1.0);
    if ((From > 0.0 && To < 0.0) || (From < 0.0 && To > 0.0))
    {
        return 0.0;
    }
    return To;
}
