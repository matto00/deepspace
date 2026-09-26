#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
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
     * The in-system drive's time constant, seconds (sky decision 8). At full
     * throttle the drive closes a tenth of the remaining room every 1.5 s,
     * so distance falls exponentially and a planet's disc grows by the same
     * factor every second: from 1 AU to a 40,000 km orbit is ln(3,740) x 15 s,
     * about two minutes -- "a minute or two to close with a world". Boosters
     * on a thin allocation stretch it; the subsystem divides it by their
     * thrust fraction, so a starved ship arrives slowly and always arrives.
     */
    double DriveTau = 15.0;

    /**
     * Where the drive's room runs out, cm above the nearest surface: 100 km.
     * The drive settles onto it and stops there: as the room runs out, so
     * does the speed at which it may close, so a lever left on toward a world
     * -- or toward the star a jump arrived at -- parks the ship 100 km up and
     * holds it there however long nobody is at the helm. Cruise speed is
     * still there for any heading that does not close, so at the floor the
     * ship can still turn along the surface or away from it at 200 m/s, the
     * edge of landing's regime.
     */
    double DriveFloor = 1.0e7;

    static FShipFlightLimits Cruise();
};

/**
 * The pilot's intent, normalised. Every field is clamped to its range on the
 * way in, so a bad caller cannot exceed the limits.
 */
struct DEEPSPACE_API FShipFlightCommand
{
    /** Fraction of MaxSpeed to hold, -1..1. Persistent: set and leave. */
    double Throttle = 0.0;

    /** Fraction of MaxAngularRate per body axis, -1..1: X pitch, Y yaw, Z roll.
     *  Held, not persistent. */
    FVector AttitudeRate = FVector::ZeroVector;

    /** The in-system drive's lever. Persistent, like the throttle: set the
     *  approach, walk to the galley, and watch the world arrive. A caller
     *  building a fresh command must carry this over from GetCommand(), or
     *  every attitude input would disengage the drive. */
    bool bDrive = false;
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

    /** Clamped on the way in. Disengaging the drive clamps the speed to
     *  MaxSpeed: the drive has no inertia, and a ship still doing 34 c after
     *  the lever is off would be a second drive nobody asked for. */
    void SetCommand(const FShipFlightCommand& NewCommand);
    const FShipFlightCommand& GetCommand() const;

    /** Zero the attitude command, leaving throttle alone. Called when the pilot
     *  leaves the seat: a ship nobody is flying does not keep turning, but a
     *  cruise the player set and walked away from is the point. */
    void ReleaseAttitude();

    /**
     * The drive's input: the distance to the nearest surface, cm, and the
     * universe-frame direction in which that distance grows, which the
     * subsystem reads once a frame from LocalSystem::NearestSurfaceDistance
     * and ShipDrive::AwayFromSurface. An input like the command, not
     * something the flight state works out: it knows nothing of what is out
     * there.
     *
     * The direction is what lets the drive tell closing from leaving: it
     * may close on the surface no faster than the room over tau, and leave
     * it as fast as it likes. A zero direction means nothing to close with
     * -- transit, where the subsystem passes 0 and zero, or an empty sky --
     * and the drive is then cruise along the nose.
     *
     * Read once a frame while the state substeps at 120 Hz. At full throttle
     * a 60 Hz frame closes 0.1% of the room, and even the two-second catch-up
     * cap closes 13%, so a stale room cannot carry the ship through a floor.
     */
    void SetDriveRoom(double NearestSurfaceDistanceCm, const FVector& AwayFromSurface);

    /** Room the drive has left to close, cm: the nearest surface less
     *  DriveFloor, never negative. 0 means the ship is at the floor, where
     *  the drive closes no further. */
    double GetDriveRoom() const;

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
     * after the command, the attitude release and the tick. A translation
     * and nothing else: orientation, velocity and angular velocity are left
     * exactly as they were, because the arrival point lies on the line to the
     * star, so the star is still where the nose was -- and turning the ship
     * would turn the distant dome, the one thing a jump must not move (nav
     * decision 5). Called from exactly one place in UShipSubsystem.
     */
    void JumpTo(const FUniversePosition& Arrival);

    /** Seconds from cold to ready with the engine fully fed. */
    static constexpr double JumpChargeSeconds = 90.0;

private:
    void SubStep(double FixedDelta);

    FUniversePosition Position;
    FQuat   Orientation = FQuat::Identity;
    FVector Velocity = FVector::ZeroVector;        // cm/s, universe frame
    FVector AngularVelocity = FVector::ZeroVector; // rad/s, body frame
    FVector LastAngularAcceleration = FVector::ZeroVector;
    FVector LastLinearAcceleration = FVector::ZeroVector;

    double JumpCharge = 0.0;

    /** Last nearest-surface distance the subsystem reported, cm, and the
     *  unit direction it grows in (zero: nothing to close with). */
    double DriveSurfaceDistance = 0.0;
    FVector DriveAwayFromSurface = FVector::ZeroVector;

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

namespace ShipDrive
{
    /** How far either side of the ship AwayFromSurface probes, cm: 1 km.
     *  Far below any distance the drive cares about -- the floor is a
     *  hundred times it -- and far above the few centimetres of rounding in
     *  a distance measured a quarter of a light year from the star. */
    inline constexpr double SurfaceProbeCm = 1.0e5;

    /**
     * The direction in which a surface distance grows at Where, universe
     * frame, unit length: its gradient, by central differences one probe
     * either side on each axis. For a sphere it is the outward normal; for
     * the system's edge it points at the star. Zero where the distance is
     * flat -- an empty sky, or deep inside a body where it reads 0 -- which
     * is what SetDriveRoom takes as nothing to close with.
     *
     * Pure: the subsystem passes LocalSystem::NearestSurfaceDistance on the
     * system it read this frame, so the six probes regenerate nothing.
     */
    DEEPSPACE_API FVector AwayFromSurface(TFunctionRef<double(const FUniversePosition&)> SurfaceDistance,
                                          const FUniversePosition& Where);
}
