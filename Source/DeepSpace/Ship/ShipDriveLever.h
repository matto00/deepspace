#pragma once

#include "CoreMinimal.h"

/**
 * The two helm levers as arithmetic (flight-feel decisions 2-4): the drive's
 * row of notches and the ease the ship answers it with, and the cruise
 * lever's sweep with its detent at zero.
 *
 * Pure: no UObject, no world, no console variable. The subsystem reads the
 * CVars and passes their values in; the defaults it registers them with are
 * the named constants here, so a test and a CVar can never disagree about
 * what "the default" is.
 *
 * Positions are in notch space. Position 0 is STOP and positions 1..11 are
 * the notches of the 1-2-5 series, 20 km/s to 0.1 c (the 2026-09-27
 * ruling); a fractional position is the ship's eased place between two
 * notches (decision 4), and its speed is read from the table there: linear
 * from STOP to the first notch, geometric between notches, so a step
 * anywhere on the lever is the same felt step.
 *
 * The cruise lever is a position too, -1..1, and its speed is read on a log
 * scale (CruiseSpeed): fine at the bottom, where a metre a second matters,
 * and coarse at the top, where a kilometre a second does not.
 */
namespace ShipDriveLever
{
    /** The speed of light, cm/s, exact by the SI definition of the metre. */
    inline constexpr double LightCmPerSecond = 2.99792458e10;

    /**
     * The ease's time constant, seconds: a one-notch step settles to 95% of
     * the way in three of these, 1.2 s, and never overshoots. Short enough
     * that a tap is felt at once, long enough that it is felt as motion and
     * not as a cut.
     */
    inline constexpr double EaseSeconds = 0.4;

    /**
     * Five hundred-thousandths of a notch: where the ease stops approaching
     * and arrives. 1 m/s off STOP, the first notch being 20 km/s, and a
     * hundredth of a percent of speed anywhere above it, so nothing on screen
     * can tell; without it STOP would be approached forever and a ship "at
     * rest" would still be creeping. It was a thousandth when the first notch
     * was 1 km/s: the same metre a second, and a thousandth of 20 km/s would
     * snap the last 20 m/s to rest in one substep, which the readout shows.
     */
    inline constexpr double SettleNotches = 5.0e-5;

    /** How long a held lever key waits before it repeats, seconds: a tap is
     *  over well inside it, so a tap is never read as a hold. */
    inline constexpr double RepeatDelaySeconds = 0.3;

    /** ds.Drive.Top's default, in c. 0.1 c is the ruled top (the 2026-09-27
     *  ruling, which replaced ruling 1's 1 c): anything faster is a jump. */
    inline constexpr double DefaultTopLight = 0.1;

    /** ds.Drive.Response's default, notches a second at full thrust. Equal
     *  to the hold's repeat rate, so a held key and the ship move together. */
    inline constexpr double DefaultResponse = 3.0;

    /** ds.Drive.Sweep's default, notches a second while a key is held past
     *  the repeat delay: STOP to 0.1 c is the press and ten repeats, 3.7
     *  seconds held. */
    inline constexpr double DefaultSweep = 3.0;

    /** ds.Cruise.Sweep's default, lever fraction a second: five seconds from
     *  rest to full ahead, which on the log scale is a decade of speed about
     *  every 1.2 seconds (the 2026-09-27 ruling; it was 0.5 on the linear
     *  lever). */
    inline constexpr double DefaultCruiseSweep = 0.2;

    /**
     * The slowest a cruise lever off zero asks for, cm/s: 1 m/s, the bottom
     * of the log scale (CruiseSpeed). A lever a hair off zero asks for this,
     * not for nothing, so the first frame of a fresh press moves the ship.
     */
    inline constexpr double CruiseFloorCmPerSecond = 100.0;

    /**
     * The cruise lever's speed, cm/s, signed, negative astern.
     *
     * Ahead, Throttle p in (0, 1] asks for Floor x (Top / Floor)^p: 1 m/s
     * just off zero, Top at full, and each tenth of the lever the same
     * factor of speed, so it is as fine at 3 m/s as it is coarse at 15
     * km/s. Zero is rest, exactly: the detent is a place, not a speed.
     *
     * Astern is the same law mirrored, position for position, so a lever at
     * -p asks for exactly what +p asks for ahead -- but the lever's astern
     * travel ends where that reaches AsternTop (CruiseAsternLimit), so a
     * position past it asks for AsternTop and no more. The pilot never
     * sweeps through lever that does nothing.
     *
     * A Top at or under the floor is linear, p x Top, since there is no
     * decade to spread; NaN reads as rest.
     */
    DEEPSPACE_API double CruiseSpeed(double Throttle, double TopCmPerSecond, double AsternTopCmPerSecond);

    /**
     * How far astern the cruise lever travels, 0..1: the position at which
     * CruiseSpeed's mirrored law reaches AsternTop, ln(AsternTop / Floor) /
     * ln(Top / Floor). 0.535 for 200 m/s under a 20 km/s top. 1 when astern
     * is as fast as ahead; 0 when AsternTop is under the floor, where the
     * lever has no astern at all.
     */
    DEEPSPACE_API double CruiseAsternLimit(double TopCmPerSecond, double AsternTopCmPerSecond);

    /** The notches in the table, STOP not counted: 11. */
    DEEPSPACE_API int32 TableNotches();

    /**
     * Positions on a lever that tops out at TopCmPerSecond, STOP included:
     * 12 at 0.1 c. Notches above the top are dropped; a top above 0.1 c adds
     * nothing, because the table ends there. Never fewer than two positions,
     * STOP and 20 km/s: a drive lever with only STOP on it is not a lever.
     */
    DEEPSPACE_API int32 NotchCount(double TopCmPerSecond);

    /** A notch's speed, cm/s: 0 at STOP, the table's speed above, clamped
     *  to the table's ends outside them. */
    DEEPSPACE_API double NotchSpeed(int32 Notch);

    /** The speed at an eased position, cm/s: linear from STOP to notch 1,
     *  geometric between notches, the top's speed above the top. */
    DEEPSPACE_API double SpeedAt(double Position);

    /** The inverse of SpeedAt: the position at which the lever reads Speed.
     *  0 at or below rest, the table's top at or above its speed. */
    DEEPSPACE_API double PositionOf(double Speed);

    /**
     * The eased position one Dt on, toward Target (decision 4): it moves at
     * Thrust x clamp((Target - Position) / EaseSeconds, -MaxRate, +MaxRate),
     * never past Target, and arrives within SettleNotches.
     *
     * Thrust is the boosters' thrust fraction, 0..1, and it scales the whole
     * law: the rate limit and the time constant alike, so a quarter thrust
     * takes exactly four times as long to make any change, and makes it.
     * MaxRate is therefore the full-thrust rate, ds.Drive.Response, and must
     * never be pre-scaled by thrust: that would slow the ship by thrust
     * squared, sixteen times at a quarter. Scaling the rate alone would slow
     * only the rate-limited part, and a one-notch tap, which never reaches the
     * limit, not at all.
     *
     * The linear part is solved exactly, not stepped, so a long Dt cannot
     * overshoot and the result does not depend on how time was chopped.
     */
    DEEPSPACE_API double Ease(double Position, double Target, double Dt, double MaxRate, double Thrust);

    /**
     * Ctrl's tap (decision 3): the notch below the ship's present speed,
     * when that is lower than one notch below the lever. Ctrl always slows
     * the ship from the first tap, including under the soft cap and while
     * spooling up, where the lever is above what the ship is doing. No
     * reverse: at STOP it stays at STOP.
     */
    DEEPSPACE_API int32 TapDown(int32 Notch, double Position);

    /** Shift's tap, the mirror of TapDown: the notch above the ship's present
     *  speed when that is higher than one notch above the lever, never past
     *  TopNotch. Stops a spool-down where it is, from the first tap. */
    DEEPSPACE_API int32 TapUp(int32 Notch, double Position, int32 TopNotch);

    /**
     * A held lever key's repeat: nothing for RepeatDelaySeconds, then Rate
     * notches a second, counted as whole repeats due since the hold began.
     *
     * Fed whether the key is held, never a difference of levels: a tap is a
     * press the pawn counts (Started), and a press released inside one frame
     * is still a tap, which a level read once a frame would lose. This
     * counts only the repeats after it, so the press and its hold are one
     * notch plus these, whatever the frame rate.
     */
    struct DEEPSPACE_API FNotchRepeat
    {
        /** Advance by Dt; returns how many repeats fell due in it. Letting
         *  go resets the hold. */
        int32 Update(bool bHeld, double Dt, double Rate);

        double HeldSeconds = 0.0;
        int32 Fired = 0;
    };

    /**
     * The cruise lever's sweep (decision 2): Shift and Ctrl held move it at
     * Rate a second, -AsternLimit..1, and it stays where it is left.
     *
     * The detent: a sweep that reaches zero from either side stops there,
     * and a held key moves the lever off zero only in a frame with a fresh
     * press of that key. A held Ctrl that carried a cruising ship through
     * rest and into reverse would be the lever acting past what was asked;
     * going astern is a second, deliberate press.
     *
     * Stateless: "fresh" is the press count the pawn hands in with the held
     * flags, so a press that lands while the lever is above zero cannot be
     * spent on crossing it. Both keys held cancel.
     *
     * AsternLimit is CruiseAsternLimit's, the lever's astern end-stop, 0..1.
     */
    DEEPSPACE_API double SweepCruise(double Throttle, bool bHeldUp, bool bHeldDown,
                                     int32 PressesUp, int32 PressesDown, double Dt, double Rate,
                                     double AsternLimit);
}
