#pragma once

#include "CoreMinimal.h"
#include "Surface/GroundField.h"
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

    /** A planet or moon -- never a star, never the edge: what the near regime
     *  and "up" are measured from (landing decision 8). */
    bool bWorld = false;

    /**
     * Solid ground (landing decision 10), set only for EGround::Solid worlds.
     * Two floors, and each substep reads exactly one: Floor is the drive's,
     * the sphere; with a Ground, cruise, the vertical lever, the room and the
     * HUD read the ground, at every altitude, and never the sphere. Fixtures
     * without one behave exactly as before.
     */
    FGroundFieldRef Ground;

    bool HasGround() const { return Ground.IsValid(); }

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
     * Over a surface with a ground, the room is the height above it less
     * GroundClearanceCm: the cruise floor.
     */
    DEEPSPACE_API double Room(TConstArrayView<FFlightSurface> Surfaces, const FUniversePosition& From,
                              double GroundClearanceCm = 0.0);

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

    /** ds.Land.TouchdownSpeed's default, cm/s: contact at half a metre a second. */
    inline constexpr double DefaultTouchdownSpeed = 50.0;

    /** ds.Land.ApproachSeconds' default: the ground approach's exponential,
     *  its own CVar and never ds.Drive.HoldSeconds (whose "0 or less" means
     *  the braking curve alone). Clamped to MinApproachSeconds where read. */
    inline constexpr double DefaultApproachSeconds = 4.0;
    inline constexpr double MinApproachSeconds = 0.5;

    /** ds.Land.SkimSeconds and ds.Land.SkimFloor's defaults: in the near
     *  regime horizontal speed is at most max(SkimFloor, AGL / SkimSeconds),
     *  so the ground flows past at one apparent rate at every height. */
    inline constexpr double DefaultSkimSeconds = 2.5;
    inline constexpr double DefaultSkimFloor = 2000.0;

    /** From under the ground a ray may leave only climbing by more than
     *  this sine of the horizon (about 0.06 degrees): a level one is a hit
     *  at 0, never "no hit" by the sign of a rounding. */
    inline constexpr double UnderClimbSine = 1.0e-3;

    /** The ground march's step budget: an exhausted march is a hit. */
    inline constexpr int32 GroundMarchSteps = 64;

    /** ds.Land.Regime's default, cm: the vertical lever is live within 50 km
     *  of the nearest world's cruise floor, leaves over RegimeExitFactor of
     *  it, and both levers blend across its top RegimeBlendFraction. */
    inline constexpr double DefaultRegimeCm = 5.0e6;
    inline constexpr double RegimeExitFactor = 1.1;
    inline constexpr double RegimeBlendFraction = 0.2;

    /** ds.Land.DriveHandback's default, cm: the drive takes the ship back
     *  only this far above a solid world's drive floor. */
    inline constexpr double DefaultDriveHandbackCm = 5.0e4;

    /** How far From is above Surface's ground, cm, radially: the origin
     *  above the rock, clearance included. Unset without a ground. */
    DEEPSPACE_API TOptional<double> GroundAt(const FFlightSurface& Surface, const FUniversePosition& From);

    /**
     * The distance along a ray from From to Height + ClearanceCm, cm, by
     * sphere tracing: each step is the clearance the slope bound proves free,
     * Above / sqrt(1 + MaxSlope^2). Far steps may be band-limited -- a step
     * that follows one of length s evaluates Height(D, s / 2) and widens the
     * clearance by OmittedBoundCm(s / 2), cheaper and still conservative.
     * Skipped entirely unless the ray enters the sphere of R + MaxHeight +
     * Clearance.
     *
     * - Unset: proven clear to MaxDistanceCm, or out of the shell, or, from
     *   under the ground, heading up (a ship under the ground may always climb).
     * - 0: under the ground heading down.
     * - An exhausted march (GroundMarchSteps) is a hit at its last proven-clear
     *   distance, never "no hit": the cap always brakes for what it could not
     *   see past. OutSteps, if given, gets the steps used.
     *
     * Pure and deterministic, independent of what the mesh has streamed.
     *
     * Proof, if given, carries what earlier marches proved into this one:
     * each of their samples is a fact about the ground, not the ship -- a
     * ball of radius Above / sqrt(1 + MaxSlope^2) about the sampled point
     * inside which nothing is under ground + ClearanceCm -- so wherever this
     * ray runs inside one, it passes to the ball's far side for nothing, and
     * only its fresh samples count against GroundMarchSteps. A level ray at
     * a low hover proves only about an eighth of its clearance a sample on
     * the real relief; without the proof 64 samples see a few metres ahead,
     * and with it a march that runs out picks up, next frame, where the last
     * stopped. On return Proof holds the balls this march passed through.
     * A proof for another ground, centre or clearance is discarded.
     */
    struct FGroundRayProof;
    DEEPSPACE_API TOptional<double> RayToGround(const FFlightSurface& Surface, const FUniversePosition& From,
                                                const FVector& Direction, double ClearanceCm, double MaxDistanceCm,
                                                int32* OutSteps = nullptr, FGroundRayProof* Proof = nullptr);

    /** What marches along a line proved clear of the ground, for the next
     *  march along about the same line (RayToGround). */
    struct FGroundRayProof
    {
        /** The most balls a proof keeps; a march that would need more is
         *  exhausted there, a hit. */
        static constexpr int32 MaxBalls = 4096;

        FGroundFieldRef Ground;
        FUniversePosition Centre;
        double Radius = 0.0;
        double ClearanceCm = 0.0;
        /** Centre-relative sample points, and the radius each is proven
         *  clear about, cm, in the order the march met them. */
        TArray<FVector> Points;
        TArray<double> Radii;

        void Reset()
        {
            Ground.Reset();
            Points.Reset();
            Radii.Reset();
        }
    };

    /**
     * The approach law (decision 10), cm/s, D cm from the ground:
     * D1 = 0.8 A N^2; D / N under D1, sqrt((D1 / N)^2 + 1.6 A (D - D1)) above
     * it; never under TouchdownSpeed; never more than D / Step. Its demanded
     * deceleration never exceeds 0.8 A, so an inertial ship follows it all the
     * way down, and the last metres are an exponential ease to contact at
     * TouchdownSpeed. ApproachSeconds is clamped to MinApproachSeconds. 0 at
     * D of zero or less.
     */
    DEEPSPACE_API double GroundApproachSpeed(double D, double BrakingAccel, double ApproachSeconds,
                                             double TouchdownSpeed, double Step);

    /** The skim cap, cm/s: max(SkimFloor, AGL / SkimSeconds). */
    DEEPSPACE_API double SkimCap(double AglCm, double SkimSeconds, double SkimFloor);

    /** The laws the ETA to the ground integrates. */
    struct FGroundLaw
    {
        double BrakingAccel = 0.0;
        double ApproachSeconds = DefaultApproachSeconds;
        double TouchdownSpeed = DefaultTouchdownSpeed;
        double SkimSeconds = DefaultSkimSeconds;
        double SkimFloor = DefaultSkimFloor;
    };

    /**
     * Seconds to the ground PathCm ahead along a straight path PathSine below
     * the horizon, for a ship at Speed under the laws it is really flying
     * (decision 12): along the path the ship may have min(Speed, the approach
     * law of the height left over PathSine, the skim cap of the height left
     * over the path's cosine). Integrated on a geometric grid, where the
     * exponential parts carry equal time per step, so it is exact to well
     * under a tenth of a percent and counts down a second a second. 0 at
     * PathCm of zero or less; infinite at rest.
     */
    DEEPSPACE_API double SecondsToGround(double PathCm, double Speed, double PathSine, const FGroundLaw& Law);
}
