#pragma once

#include "CoreMinimal.h"
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

    /** Clamped on the way in. */
    void SetCommand(const FShipFlightCommand& NewCommand);
    const FShipFlightCommand& GetCommand() const;

    /** Zero the attitude command, leaving throttle alone. Called when the pilot
     *  leaves the seat: a ship nobody is flying does not keep turning, but a
     *  cruise the player set and walked away from is the point. */
    void ReleaseAttitude();

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

private:
    void SubStep(double FixedDelta);

    FUniversePosition Position;
    FQuat   Orientation = FQuat::Identity;
    FVector Velocity = FVector::ZeroVector;        // cm/s, universe frame
    FVector AngularVelocity = FVector::ZeroVector; // rad/s, body frame
    FVector LastAngularAcceleration = FVector::ZeroVector;
    FVector LastLinearAcceleration = FVector::ZeroVector;

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
