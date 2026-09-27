#pragma once

#include "CoreMinimal.h"
#include "Universe/UniversePosition.h"

/**
 * A surface the ship may not pass, as the flight law sees it: a sphere at the
 * body's floor (flight-feel decision 6), or, inside out, the system's edge.
 *
 * Only geometry. What the floor is -- the sky's rendered floor over a world,
 * a stellar radius over a star, ds.Flight.Floor inside the edge -- is the
 * subsystem's FloorFor, which fills these in; the flight law reads nothing
 * but the spheres.
 */
struct DEEPSPACE_API FFlightSurface
{
    FUniversePosition Centre;

    /** The body's radius, cm; for the edge, the edge's radius about the star. */
    double Radius = 0.0;

    /** How far off the body the ship stops, cm: above it for a body, inside
     *  it for the edge. */
    double Floor = 0.0;

    /** The system's edge: the ship lives inside this sphere, not outside it. */
    bool bInsideOut = false;

    /** The radius of the floor sphere itself, cm. */
    double FloorRadius() const { return bInsideOut ? Radius - Floor : Radius + Floor; }
};

/**
 * The soft cap as pure arithmetic (flight-feel decision 5): the nose's ray to
 * a floor sphere, the speed the ship may have with that far to go, how much
 * room it has, and how long the cap's approach takes -- the live ETA's law
 * (ruling 3), so the ETA and the flight are the same sums.
 *
 * No UObject, no world, no console variable: every tunable is a parameter,
 * and its default is a named constant here.
 */
namespace ShipFlight
{
    /**
     * The share of the boosters the braking curve plans on: 80%. The last
     * part of every approach brakes as cruise does, and planning on all of
     * the thrust would leave nothing for a turn made while braking. One
     * constant for both modes, so the drive and cruise come to rest on a
     * floor alike.
     */
    inline constexpr double BrakingMargin = 0.8;

    /** ds.Drive.HoldSeconds' default: the cap binds when the path meets a
     *  floor within this many seconds at the present speed, and then lets
     *  the distance fall by e every this many seconds. "Within a few
     *  seconds" (ruling B): 3 or 5 would be a reasonable-person difference. */
    inline constexpr double DefaultHoldSeconds = 4.0;

    /** ds.Flight.Floor's default, cm: 10 km. Never under the sky's own
     *  rendered floor, which FloorFor takes the larger of. */
    inline constexpr double DefaultFloorCm = 1.0e6;

    /** ds.Flight.StarFloorRadii's default: one stellar radius over a star,
     *  where its disc fills 60 degrees and the rest of the sky is still
     *  there (decision 6). */
    inline constexpr double DefaultStarFloorRadii = 1.0;

    /**
     * The distance along a ray from From to Surface's floor sphere, cm.
     *
     * - Outside a body's floor: the near root, or unset when the ray misses
     *   or points away.
     * - On or under a body's floor: 0 heading in, unset heading out or along
     *   it -- the ship may always climb, and may not descend.
     * - Inside the edge: the far root; every ray from inside meets it.
     * - Beyond the edge's floor: 0 heading further out; heading back in, the
     *   far root, where the ray would leave again, or unset if it never
     *   re-enters.
     *
     * Unset for a zero direction. The separation is taken through the chunk
     * index (ADR 0007), and the roots in forms that do not cancel, so a ray
     * grazing a limb from 0.2 AU keeps its digits.
     */
    DEEPSPACE_API TOptional<double> RayToFloor(const FFlightSurface& Surface, const FUniversePosition& From,
                                               const FVector& Direction);

    /** How far From is outside Surface's floor sphere, cm: above it for a
     *  body and inside it for the edge. Negative under a floor. */
    DEEPSPACE_API double FloorClearance(const FFlightSurface& Surface, const FUniversePosition& From);

    /**
     * The fastest the ship may go with D cm to go to a floor on its path:
     * min(max(D / HoldSeconds, the braking curve), D / Step). Far out, D
     * falls by e every HoldSeconds; near in, the braking curve, on which the
     * ship comes to rest at the floor; and never so fast that one substep of
     * Step seconds would carry it past.
     *
     * The braking curve is the stepped one, v^2 / 2b + v Step / 2 = D with b
     * = BrakingMargin x BrakingAccel: on it the speed falls by exactly b x
     * Step a substep down to rest, so a ship with inertia can follow it to
     * the floor. With a Step of zero or less it is the continuous sqrt(2 b
     * D), which SecondsToFloor integrates; the two differ by b x Step / 2.
     *
     * BrakingAccel is the boosters' present acceleration, cm/s^2, of which
     * BrakingMargin is planned on. A HoldSeconds of zero or less drops the
     * hold, leaving the braking curve alone: still a cap, one that lets the
     * ship close at full lever until it must brake. A Step of zero or less
     * drops the substep bound. 0 at D of zero or less, and with neither a
     * hold nor any braking, which only a CVar can arrange: boosters degrade
     * to a quarter thrust and never to none.
     */
    DEEPSPACE_API double MaySpeed(double D, double BrakingAccel, double HoldSeconds, double Step);

    /**
     * The room: the least clearance over every surface, never negative, cm.
     * For the HUD and the tests; the cap reads the ray, not this. 0 with no
     * surfaces, as between stars, where there is nothing to have room from.
     */
    DEEPSPACE_API double Room(TConstArrayView<FFlightSurface> Surfaces, const FUniversePosition& From);

    /**
     * Seconds to the floor D cm ahead, for a ship at Speed cm/s under the cap
     * (ruling 3's live ETA), on the cap's law with its braking part the
     * continuous curve, sqrt(2 b d) -- MaySpeed with a Step of zero. The cap
     * flies the stepped curve, b x Step / 2 slower (under 7 m/s at full
     * boosters), so this is short by about half a substep; not worth the
     * stepped curve's integral. It holds Speed until MaySpeed falls to it, then
     * the distance falls by e every HoldSeconds down to the braking knee
     * (2 x BrakingMargin x BrakingAccel x HoldSeconds^2, 51.2 km at full
     * boosters), then it brakes, 2 x HoldSeconds from the knee. Above the
     * knee: (D - Speed N) / Speed + N ln(Speed N / knee) + 2N.
     *
     * With a HoldSeconds of zero or less, the continuous braking curve
     * alone, as MaySpeed with no hold and no Step: it holds Speed until d1 = Speed^2 / (2 x BrakingMargin x
     * BrakingAccel), then brakes, 2 d1 / Speed.
     *
     * At the present speed, which is what "live" means: while the lever is
     * still spooling up it overstates. 0 at D of zero or less; infinite at
     * rest, and with no braking at all, where the cap's exponential alone
     * never arrives.
     */
    DEEPSPACE_API double SecondsToFloor(double D, double Speed, double BrakingAccel, double HoldSeconds);
}
