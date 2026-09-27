#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightSurface.h"
#include "Universe/UniversePosition.h"

/**
 * What the ship can do. Cruise today; a combat upgrade is a different set of
 * numbers, not a different model (docs/vision.md, Flight modes).
 */
struct DEEPSPACE_API FShipFlightLimits
{
    /** Top speed under cruise assist, cm/s. */
    double MaxSpeed = 20000.0;

    /** How hard the ship changes velocity, cm/s^2. */
    double LinearAcceleration = 4000.0;

    /** Peak turn rate, radians/s, per body axis: X pitch, Y yaw, Z roll. */
    FVector MaxAngularRate = FVector(0.20, 0.20, 0.30);

    /** How hard the ship changes turn rate, radians/s^2, per body axis. */
    FVector AngularAcceleration = FVector(0.25, 0.25, 0.40);

    /**
     * The soft cap's hold, seconds (flight-feel decision 5): the cap binds
     * when the nose's path meets a floor within this many seconds at the
     * present speed, and then lets the distance fall by e every this many
     * seconds until the braking curve takes over. 0 or less is the braking
     * curve alone -- still a cap. ds.Drive.HoldSeconds, set by the subsystem.
     */
    double HoldSeconds = ShipFlight::DefaultHoldSeconds;

    /**
     * The drive lever's top, cm/s: 1 c, which is as fast as the drive ever
     * goes (ruling 1: anything faster is a jump). ds.Drive.Top, which may
     * shorten the lever and never lengthen it; notches above it are gone.
     */
    double DriveTop = ShipDriveLever::DefaultTopLight * ShipDriveLever::LightCmPerSecond;

    /**
     * How fast the drive's eased position may move, notches a second, at full
     * thrust (decision 4). Never pre-scaled by thrust: DriveThrust scales the
     * whole ease, and scaling this as well would slow a starved ship by
     * thrust squared. ds.Drive.Response.
     */
    double DriveResponse = ShipDriveLever::DefaultResponse;

    /**
     * The boosters' thrust fraction, 0..1, as the ease is handed it: a
     * quarter thrust takes four times as long to reach any notch, and gets
     * there. The top is never lowered by it (the anti-chore principle's
     * lost-potential case). The subsystem sets it from the allocation.
     */
    double DriveThrust = 1.0;

    static FShipFlightLimits Cruise();
};

/**
 * The pilot's intent, normalised. Every field is clamped to its range on the
 * way in, so a bad caller cannot exceed the limits.
 *
 * Two levers, and both are here, on the ship, rather than on whoever is
 * sitting at the helm (flight-feel decision 1): a lever is ship state, and a
 * second pilot sitting down must find it where the first one left it.
 */
struct DEEPSPACE_API FShipFlightCommand
{
    /** The cruise lever: fraction of MaxSpeed to hold, -1..1. Persistent: set
     *  and leave. */
    double Throttle = 0.0;

    /** Fraction of MaxAngularRate per body axis, -1..1: X pitch, Y yaw, Z roll.
     *  Held, not persistent. */
    FVector AttitudeRate = FVector::ZeroVector;

    /** Which lever is live: the drive's (F), or cruise's. Persistent, like
     *  both levers: set the approach, walk to the galley, and watch the world
     *  arrive. A caller building a fresh command must carry this and
     *  DriveNotch over from GetCommand(), or every attitude input would
     *  disengage the drive and zero its lever. */
    bool bDrive = false;

    /** The drive lever: 0 is STOP, 1..NotchCount(DriveTop) - 1 the notches of
     *  the 1-2-5 series (ShipDriveLever). Kept across F in both directions, so
     *  a drive set to 1 c, left for a look round in cruise, is at 1 c again
     *  the moment F is pressed. */
    int32 DriveNotch = 0;
};

/** Which lever the ship is answering. */
enum class EFlightMode : uint8
{
    /** Cruise's lever, under the boosters' inertia. */
    Cruise,

    /** The drive's lever, eased in notch space, along the nose, no inertia. */
    Drive,

    /**
     * F pressed with the ship above cruise's top: cruise's lever is live at
     * once, and the ship eases down along the nose on the drive's own curve
     * until it reaches MaxSpeed, where it becomes an ordinary cruising ship.
     * Leaving the drive never clamps (decision 4).
     */
    SpoolingDown,
};

/** What the soft cap did in the last substep (decision 5): the one thing that
 *  knows, so the HUD asks rather than working it out. */
enum class EFlightHold : uint8
{
    /** Nothing held the ship back: the lever is the speed. */
    Free,

    /** The nose's path meets a floor within the hold, and the cap took speed
     *  away. How much is GetHeldFraction(). */
    HoldingOff,

    /** On a floor, the nose into it, and the lever above STOP: as low as the
     *  ship goes, and it holds there. */
    AtFloor,
};

/**
 * Where the ship is in the universe, which way it points, and how it is
 * moving. The ship actor's transform is identity permanently (ADR 0005); this
 * struct holds everything that would otherwise be in it.
 *
 * Pure arithmetic: no UObject, no UWorld, unit-testable with no world at all,
 * exactly as FShipPowerState is.
 */
struct DEEPSPACE_API FShipFlightState
{
public:
    void SetLimits(const FShipFlightLimits& NewLimits);
    const FShipFlightLimits& GetLimits() const;

    /**
     * Clamped on the way in, the drive's notch to the lever's top included.
     *
     * The two toggles are the only places the mode changes, and neither has
     * a substep in which the speed jumps. Engaging starts the drive's eased
     * position at the ship's present forward speed. Disengaging above
     * cruise's top does not clamp: it starts SpoolingDown (EFlightMode), and
     * engaging again mid-spool carries on from where the spool had got to.
     */
    void SetCommand(const FShipFlightCommand& NewCommand);
    const FShipFlightCommand& GetCommand() const;

    /** Zero the attitude command, leaving both levers alone. Called when the
     *  pilot leaves the seat: a ship nobody is flying does not keep turning,
     *  but a cruise the player set and walked away from is the point. */
    void ReleaseAttitude();

    /**
     * Every surface in the system, as floor spheres (FFlightSurface): each
     * body at its floor, and the system's edge inside out. The flight law's
     * one input about what is out there; the subsystem builds the list once a
     * frame from LocalSystem::Here with its FloorFor, and passes none in
     * transit, where there is nothing to be near.
     *
     * Every substep casts the ship's own path against every one of them
     * (decision 5), so a moon beyond a giant caps the ship the moment the
     * nose is on it, and no substep can carry it through any of them. The
     * spheres are fixed across a frame, which is honest: nothing orbits yet.
     */
    void SetSurfaces(TArray<FFlightSurface> NewSurfaces);
    TConstArrayView<FFlightSurface> GetSurfaces() const;

    /** Room: the least clearance over every surface, less its own floor,
     *  never negative, cm (decision 6). For the HUD and the tests; the cap
     *  reads the ray, not this. 0 with no surfaces, as between stars. */
    double GetRoom() const;

    /** Advance by DeltaSeconds. Internally fixed-step; leftover time is carried
     *  to the next call, so the result depends on elapsed time and not on how
     *  it was chopped into frames. */
    void Step(double DeltaSeconds);

    FUniversePosition GetUniversePosition() const;
    FQuat   GetUniverseOrientation() const;
    FVector GetVelocity() const;              // universe frame, cm/s
    FVector GetAngularVelocity() const;       // body frame, rad/s
    FVector GetAngularAcceleration() const;   // body frame, rad/s^2, last step
    FVector GetLinearAcceleration() const;    // universe frame, cm/s^2
    double  GetSpeed() const;

    /** Which lever the ship is answering, and whether it is spooling down. */
    EFlightMode GetMode() const;

    /** What the cap did in the last substep. Steady under a steady hold:
     *  while the cap holds, the eased position follows it, so it is asked
     *  again every substep and answers the same. */
    EFlightHold GetHold() const;

    /** How far below the live lever's speed the cap holds the ship, as a
     *  fraction of that speed, 0..1: 0 when Free, 1 at a floor. Against the
     *  lever and not the eased position, which follows the cap and so would
     *  sit a hair under it every substep. */
    double GetHeldFraction() const;

    /** What the live lever asks for, cm/s: the drive's notch speed, or
     *  cruise's Throttle x MaxSpeed, signed, negative astern -- cruise's
     *  while spooling down, which is live from the press of F. */
    double GetLeverSpeed() const;

    /** What the other lever asks for, cm/s, the same way: the speed F would
     *  go to, which the HUD shows dim beside the live one. */
    double GetOtherLeverSpeed() const;

    /** The drive's eased position in notch space (decision 4): the ship's
     *  speed under the drive is ShipDriveLever::SpeedAt of it. Kept through
     *  the spool-down, which eases it; 0 in cruise. */
    double GetDrivePosition() const;

    /** Positions on the drive lever at the present top, STOP included. */
    int32 GetDriveNotchCount() const;

    /** Ship -> universe, rotation only. Translation is deliberately absent:
     *  a universe position does not fit in an FTransform (ADR 0007) and must
     *  not be applied to a scene root anyway (ADR 0005 / decision 5). */
    FTransform GetUniverseTransform() const;

    /** Universe -> ship, rotation only. What the counter-frame draws through. */
    FTransform GetCounterFrameTransform() const;

    /**
     * THE conversion. Universe position -> Unreal world position.
     * Everything outside the hull is placed through this and nothing else.
     */
    FVector UniverseToWorld(const FUniversePosition& UniversePosition) const;
    FUniversePosition WorldToUniverse(const FVector& WorldPosition) const;

    /** Direction only: for things at effectively infinite distance, where
     *  subtracting a finite ship position changes nothing. */
    FVector UniverseDirectionToWorld(const FVector& UniverseDirection) const;

    /** Placing the ship without flying there. Used by level setup and tests. */
    void SetUniverseTransform(const FUniversePosition& NewPosition, const FQuat& NewOrientation);

    /**
     * The jump winds up, 0..1, at a rate scaled by how well the engine is
     * being fed. Power affects *time to ready* and nothing else: there is no
     * discharge, no decay and no way to fail a charge, so an engine on a
     * thin allocation is slow to jump and never broken. Whether it winds at
     * all is the caller's: the subsystem calls this only while the jump is
     * engaged, and otherwise the charge simply holds.
     *
     * SecondsFromCold is a parameter so that ds.Nav.ChargeSeconds can move
     * it in play; this struct never reads a console variable.
     */
    void ChargeJumpDrive(double DeltaSeconds, double Satisfaction,
                         double SecondsFromCold = JumpChargeSeconds);
    double GetJumpCharge() const;

    /** The fold has opened: the charge is spent, all of it. */
    void SpendJumpCharge();

    /**
     * The jump's arrival, and the fourth write path into the flight state
     * after the command, the attitude release and the tick. A translation,
     * and the ship brought to rest: orientation and angular velocity are
     * left exactly as they were, because the arrival point lies on the line
     * to what the ship jumped to, so it is still where the nose was -- and
     * turning the ship would turn the distant dome, the one thing a jump must
     * not move (nav decision 5).
     *
     * At rest, exactly (flight-feel decision 4): the velocity and the drive's
     * eased position go to zero and any spool-down ends. The subsystem puts
     * both levers at STOP when the fold opens; this makes the arrival exact
     * rather than relying on the ease to finish inside the fold, which from
     * 1 c it does not. Without it a lever left at 1 c would fly the arrival
     * at the star, or straight down onto a world an in-system jump had just
     * framed. Every jump, interstellar or in-system, arrives through here,
     * and the first thing the pilot does after any of them is choose a
     * speed. Called from UShipSubsystem only.
     */
    void JumpTo(const FUniversePosition& Arrival);

    /**
     * Seconds from cold to ready with the engine fully fed. Short enough that
     * setting the split and walking to the galley is the whole wait: the first
     * playtest found 90 s, starved at the default split to four and a half
     * minutes, read as the game making you wait, which is the chore the
     * vision rules out. The split still matters -- starved it takes about
     * twice as long -- but it never asks the player to sit it out.
     */
    static constexpr double JumpChargeSeconds = 45.0;

    /**
     * Within this of a floor, cm, the nose into it, the ship is at it: a
     * metre, which the braking curve closes in a quarter of a second and no
     * one can see from the glass. What AtFloor means.
     */
    static constexpr double AtFloorCm = 100.0;

private:
    void SubStep(double FixedDelta);

    /** The drive and the spool-down: the eased position, along the nose,
     *  held to the cap. Returns false when a spool-down has just reached
     *  cruise's top, and cruise takes this substep instead. */
    bool DriveSubStep(double FixedDelta);

    /** Cruise under inertia, its target along the commanded direction held
     *  to the cap, and the hard stop at every floor. */
    void CruiseSubStep(double FixedDelta);

    /** The nearest meeting of a ray from Position along Direction with any
     *  floor sphere, cm; unset when it meets none. */
    TOptional<double> NearestOnPath(const FVector& Direction) const;

    /** MaySpeed of a distance to a floor, at the boosters' present thrust. */
    double MaySpeedAt(double D) const;

    /** The cap bound this substep, D cm from a floor, at HeldSpeed: record
     *  what it did for GetHold and GetHeldFraction. */
    void RecordHold(double D, double HeldSpeed);

    FUniversePosition Position;
    FQuat   Orientation = FQuat::Identity;
    FVector Velocity = FVector::ZeroVector;        // cm/s, universe frame
    FVector AngularVelocity = FVector::ZeroVector; // rad/s, body frame
    FVector LastAngularAcceleration = FVector::ZeroVector;
    FVector LastLinearAcceleration = FVector::ZeroVector;

    double JumpCharge = 0.0;

    /** The drive's eased position, notch space. */
    double DrivePosition = 0.0;

    /** Leaving the drive above cruise's top, until the spool reaches it. */
    bool bSpoolingDown = false;

    EFlightHold LastHold = EFlightHold::Free;
    double LastHeldFraction = 0.0;

    TArray<FFlightSurface> Surfaces;

    FShipFlightLimits Limits = FShipFlightLimits::Cruise();
    FShipFlightCommand Command;
    double Accumulator = 0.0;

public:
    static constexpr double FixedStep = 1.0 / 120.0;

    /**
     * Catch-up cap, in substeps: a long hitch integrates at most this much and
     * the rest of the elapsed time is dropped rather than spiralling.
     *
     * Deliberately generous (about two seconds). A tight cap would silently
     * discard time on any frame longer than it, which is precisely the
     * frame-rate dependence the fixed step exists to remove.
     */
    static constexpr int32 MaxSubStepsPerCall = 256;
};
