#pragma once

#include "CoreMinimal.h"
#include "Universe/StarSystem.h"

/**
 * What the jump is doing, as a word for screens (plan conflict 7: the fold
 * is "the jump"; "the drive" is the in-system one). Never a number: the
 * jump's charge has no percentage, no bar and no countdown, because a number
 * that fills while the player waits is a clock to watch. That rule is the
 * charge's (the system map spec's ruling 3): an approach the player is
 * flying has a live time to arrival, which is a different thing.
 */
enum class EJumpState : uint8
{
    /** Not engaged. The charge holds wherever it was left. */
    Idle,
    /** Engaged, charge below full. */
    Winding,
    /** Engaged and charged, holding for alignment for as long as it takes. */
    Ready,
    /** Between stars. */
    Transit,
};

/** What a step of the nav state asks its owner to do. */
enum class ENavEvent : uint8
{
    None,
    /** The fold opened this step: spend the charge. */
    TransitBegan,
    /** The transit ended this step: put the ship at the arrival point. */
    Arrived,
    /** An in-system jump's transit ended this step: put the ship at the
     *  standoff above the world it went to (system map decision 12). */
    ArrivedAtWorld,
};

/** Filled from the ds.Nav.* console variables every tick; the defaults are
 *  the starting values. Pure data, so the state machine never reads a CVar. */
struct FNavTuning
{
    /**
     * Alignment is a cone round the ship's +X. 8 degrees, because a released
     * full-rate turn at cruise limits carries on for w^2/2a = 4.6 degrees, and
     * the cone's full width has to be wider than that for "let go when it
     * says dead ahead" to land inside it (nav decision 3).
     */
    double ConeRadians = 8.0 * UE_DOUBLE_PI / 180.0;

    /** Long enough to register that you have gone somewhere, short enough
     *  that it is spectacle and not waiting. */
    double TransitSeconds = 6.0;
};

namespace ShipNav
{
    /** Angle between the ship's nose and a direction in ship axes, radians,
     *  0..pi. The one test of "aligned": the jump and the HUD's "dead ahead"
     *  both ask this, so they can never disagree about it. */
    DEEPSPACE_API double OffBoresight(const FVector& ShipLocalDir);

    /**
     * The world a target id names in Here, or null: an id for another
     * system -- a PlaceShip into a different one leaves the old id behind --
     * an orbit Here lacks, or a moon, which procgen does not make yet. Every
     * reader of the target resolves it through this before drawing it, so a
     * stale id draws nothing rather than the wrong world.
     */
    DEEPSPACE_API const FPlanet* TargetPlanet(const FStarSystem& Here, const FBodyId& Id);

    /**
     * What Tab targets (system map decision 13): the next world outward from
     * Target, wrapping from the outermost to the innermost; the innermost
     * when there is no target, or one that names nothing here. Nothing only
     * in a system with no worlds, so from a set target it never clears --
     * a click on the target is how that is done.
     */
    DEEPSPACE_API TOptional<FBodyId> NextTarget(const FStarSystem& Here, const TOptional<FBodyId>& Target);
}

/**
 * The jump's decisions, and nothing else: pure, headless, next to
 * FShipPowerState and FShipFlightState. It decides; the subsystem acts.
 *
 * Three levers, each of which stays where it is left (nav decision 2): the
 * course (Plot), the heading (the helm; an input here, not state), and
 * engage. Once all three are set and the charge is full, the jump fires by
 * itself. There is no confirm, because a final button would make the player
 * come back and service the drive on its schedule.
 *
 * The course is a star or, since the developer's ruling 1, a world in this
 * system (map decision 12): at most one of Plotted and PlottedWorld is set,
 * and the latest choice wins. A world course is always the target -- it is
 * the target the map's "Jump here" plots -- so changing or clearing the
 * target lets it go, and the bracket and the jump can never name different
 * worlds.
 *
 * The target is the world in this system the pilot has marked at the map
 * (map decision 5): an id, never a copy, beside the course. A star jump's
 * fold lets it go, because the ship leaves the system it names; an in-system
 * jump's keeps it, because it is where that jump is going.
 *
 * It holds no current system -- which system the ship is in is asked of its
 * position (plan conflict 1) -- and no charge, which is the flight state's.
 * Nothing in here changes with time except the transit itself: an engaged,
 * misaligned drive holds at ready indefinitely, and nothing escalates.
 */
struct DEEPSPACE_API FShipNavState
{
public:
    /** False in transit. Refusing the system the ship is already in is the
     *  subsystem's job, since only it can ask which system that is. Replaces
     *  a world course: one course, the latest choice. */
    bool Plot(const FSystemId& Id);

    /** A course to a world in this system, which must be the target: false
     *  in transit and for any other id. Replaces a star course. Refusing a
     *  world the ship is already near is the subsystem's, which has the
     *  geometry. */
    bool PlotWorld(const FBodyId& Id);

    /** Clears either course, and also stands the jump down: an engaged jump
     *  with nowhere to go would draw power for nothing. Ignored in transit. */
    void ClearPlot();

    /** The star course. Empty for a world course, so every reader of a star
     *  course reads exactly what it did before worlds could be plotted. */
    const TOptional<FSystemId>& GetPlotted() const;

    /** The world course, which is always the target while it is set. */
    const TOptional<FBodyId>& GetPlottedWorld() const;

    /** A course of either kind. */
    bool HasCourse() const;

    /** Marks a world: false, and nothing changes, in transit. Which system
     *  the ship is in, and whether it has that orbit, is the subsystem's to
     *  check. A world course to another world is let go, and the jump with
     *  it: the course is the target or nothing. */
    bool SetTarget(const FBodyId& Id);

    /** Lets the target go, and a world course with it. Ignored in transit,
     *  where an in-system jump is on its way to it. */
    void ClearTarget();

    /** As held: resolve it (ShipNav::TargetPlanet) before drawing it. */
    const TOptional<FBodyId>& GetTarget() const;

    /** False, and nothing changes, in transit, or engaging with no course
     *  plotted. Standing down is always allowed outside transit. */
    bool SetEngaged(bool bOn);
    bool IsEngaged() const;

    /**
     * Advance. Charge and alignment are inputs, not state: the flight state
     * owns the charge and the subsystem owns the geometry.
     *
     * Holding, returns TransitBegan exactly when engaged, plotted, charged
     * (>= 1) and within the cone; a star course's fold lets the target go,
     * a world course's keeps it. In transit, counts to TransitSeconds and
     * then, for a star, returns Arrived, having moved the course into
     * LastArrival, marked it visited, cleared the course and engage, and
     * bumped the serial; for a world, returns ArrivedAtWorld, having cleared
     * the course and engage and bumped the serial, and touched neither
     * LastArrival nor the visited set -- the ship has not been anywhere new.
     */
    ENavEvent Step(double DeltaSeconds, double JumpCharge, double OffBoresightRadians,
                   const FNavTuning& Tuning);

    /** The word for screens. Idle unless engaged; Winding until the charge
     *  is full; Ready from then until the fold opens. */
    EJumpState GetJumpState(double JumpCharge) const;

    bool IsInTransit() const;

    /** 0..1 through the transit, against the TransitSeconds of the latest
     *  step; 0 outside it. For the streaks, never for a screen. */
    double GetTransitProgress() const;

    /** Bumps on every arrival. What the sky and the counter-frame rebuild
     *  on: a cache key, never an answer. */
    int32 GetJumpSerial() const;

    const TOptional<FSystemId>& GetLastArrival() const;

    void MarkVisited(const FSystemId& Id);
    bool HasVisited(const FSystemId& Id) const;

private:
    TOptional<FSystemId> Plotted;
    TOptional<FBodyId> PlottedWorld;
    TOptional<FBodyId> Target;
    TOptional<FSystemId> LastArrival;
    TSet<FSystemId> Visited;
    bool bEngaged = false;
    bool bInTransit = false;
    double TransitElapsed = 0.0;
    double TransitSeconds = FNavTuning().TransitSeconds;
    int32 JumpSerial = 0;
};
