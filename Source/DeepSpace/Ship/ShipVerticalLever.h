#pragma once

#include "CoreMinimal.h"

/**
 * The vertical lever as arithmetic (landing decision 8): a climb or sink
 * rate the boosters hold against gravity, ship state like the other two
 * levers. At zero the ship hovers. Pure: the subsystem reads the CVars and
 * passes them in, and registers them with the defaults below.
 */
namespace ShipVerticalLever
{
    /** The slowest rate off zero, cm/s: 0.1 m/s. */
    inline constexpr double FloorCmPerSecond = 10.0;

    /** ds.Vertical.Top's default, cm/s: 200 m/s either way. */
    inline constexpr double DefaultTopCmPerSecond = 2.0e4;

    /** ds.Vertical.Sweep's default, lever a second: rest to full in 4 s, a
     *  decade of rate every 1.3 s. */
    inline constexpr double DefaultSweep = 0.25;

    /** ds.Vertical.HeavyFloor's default: the climb top never falls below a
     *  quarter of Top, however heavy the world. */
    inline constexpr double DefaultHeavyFloor = 0.25;

    /** The lever's rate, cm/s, signed, + climbing: sign(p) x Floor x
     *  (Top / Floor)^|p|, the cruise lever's law mirrored; 0 exactly at 0.
     *  A Top at or under the floor reads linearly. */
    DEEPSPACE_API double Rate(double Lever, double TopCmPerSecond);

    /** The inverse: the lever that reads RateCmPerSecond; 0 for a rate under
     *  half the floor, full for one past the top. */
    DEEPSPACE_API double LeverOf(double RateCmPerSecond, double TopCmPerSecond);

    /** Space and C held: the sweep, -1..1, stopping at HOVER from either
     *  side, and leaving it only in a frame with a fresh press of that key
     *  (ShipDriveLever::SweepCruise with the full travel both ways). */
    DEEPSPACE_API double Sweep(double Lever, bool bUpHeld, bool bDownHeld, int32 UpPresses, int32 DownPresses,
                               double Dt, double SweepRate);

    /** After X (the 2026-09-26 ruling): the lever at HOVER and a fresh press
     *  the way the ship is already moving catches it at the lever nearest
     *  its rate; otherwise the lever as it was. */
    DEEPSPACE_API double Catch(double Lever, int32 UpPresses, int32 DownPresses, double VerticalSpeedCmPerSecond,
                               double TopCmPerSecond);

    /** The climb top on a world pulling GravityCmS2 (decision 5): Top x
     *  max(HeavyFloor, min(1, g_E / g)). By gravity alone, never the booster
     *  share: thrust changes how fast the lever is reached, never its top. */
    DEEPSPACE_API double ClimbTop(double TopCmPerSecond, double GravityCmS2, double HeavyFloor);
}
