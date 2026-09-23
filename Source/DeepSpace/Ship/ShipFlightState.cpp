#include "Ship/ShipFlightState.h"

FShipFlightLimits FShipFlightLimits::Cruise()
{
    return FShipFlightLimits();
}

void FShipFlightState::SetLimits(const FShipFlightLimits& NewLimits)
{
    Limits = NewLimits;
}

const FShipFlightLimits& FShipFlightState::GetLimits() const
{
    return Limits;
}

void FShipFlightState::SetCommand(const FShipFlightCommand& NewCommand)
{
    Command.Throttle = FMath::Clamp(NewCommand.Throttle, -1.0, 1.0);
    Command.AttitudeRate = FVector(
        FMath::Clamp(NewCommand.AttitudeRate.X, -1.0, 1.0),
        FMath::Clamp(NewCommand.AttitudeRate.Y, -1.0, 1.0),
        FMath::Clamp(NewCommand.AttitudeRate.Z, -1.0, 1.0));
}

const FShipFlightCommand& FShipFlightState::GetCommand() const
{
    return Command;
}

void FShipFlightState::ReleaseAttitude()
{
    Command.AttitudeRate = FVector::ZeroVector;
}

void FShipFlightState::Step(double DeltaSeconds)
{
    if (DeltaSeconds <= 0.0)
    {
        return;
    }

    Accumulator = FMath::Min(Accumulator + DeltaSeconds, MaxSubStepsPerCall * FixedStep);

    while (Accumulator >= FixedStep)
    {
        SubStep(FixedStep);
        Accumulator -= FixedStep;
    }
}

void FShipFlightState::SubStep(double FixedDelta)
{
    // Attitude. The assist is a target the real integration chases, not a
    // fake: velocity is genuinely integrated and genuinely produces the
    // acceleration that thrown bodies will one day read.
    const FVector TargetAngularVelocity = Command.AttitudeRate * Limits.MaxAngularRate;
    FVector AngularChange = FVector::ZeroVector;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        const double MaxChange = Limits.AngularAcceleration[Axis] * FixedDelta;
        AngularChange[Axis] = FMath::Clamp(
            TargetAngularVelocity[Axis] - AngularVelocity[Axis], -MaxChange, MaxChange);
    }
    AngularVelocity += AngularChange;
    LastAngularAcceleration = AngularChange / FixedDelta;

    // Body axes, so the turn is pitch/yaw/roll of the ship rather than of the
    // universe: hence right-multiplication.
    const double Angle = AngularVelocity.Size() * FixedDelta;
    if (Angle > UE_DOUBLE_SMALL_NUMBER)
    {
        Orientation = Orientation * FQuat(AngularVelocity.GetSafeNormal(), Angle);

        // Every substep, not occasionally: quaternion integration walks off the
        // unit sphere and nothing downstream would notice until it was bad.
        Orientation.Normalize();
    }

    // Velocity, chasing the commanded cruise along the ship's nose. Limiting
    // the change as a vector rather than per axis means a turn cannot cheat
    // extra acceleration out of the model by changing direction.
    const FVector TargetVelocity = Orientation.GetForwardVector() * (Command.Throttle * Limits.MaxSpeed);
    const FVector VelocityError = TargetVelocity - Velocity;
    const double MaxVelocityChange = Limits.LinearAcceleration * FixedDelta;
    const FVector VelocityChange = VelocityError.SizeSquared() <= FMath::Square(MaxVelocityChange)
        ? VelocityError
        : VelocityError.GetSafeNormal() * MaxVelocityChange;

    Velocity += VelocityChange;
    LastLinearAcceleration = VelocityChange / FixedDelta;

    Position += Velocity * FixedDelta;
}

FUniversePosition FShipFlightState::GetUniversePosition() const { return Position; }
FQuat FShipFlightState::GetUniverseOrientation() const { return Orientation; }
FVector FShipFlightState::GetVelocity() const { return Velocity; }
FVector FShipFlightState::GetAngularVelocity() const { return AngularVelocity; }
FVector FShipFlightState::GetAngularAcceleration() const { return LastAngularAcceleration; }
FVector FShipFlightState::GetLinearAcceleration() const { return LastLinearAcceleration; }
double FShipFlightState::GetSpeed() const { return Velocity.Size(); }

FTransform FShipFlightState::GetUniverseTransform() const
{
    return FTransform(Orientation);
}

FTransform FShipFlightState::GetCounterFrameTransform() const
{
    return FTransform(Orientation.Inverse());
}

FVector FShipFlightState::UniverseToWorld(const FUniversePosition& UniversePosition) const
{
    // The subtraction happens first, through the chunk index and in doubles,
    // so what reaches the rotation is a small number however far out the ship
    // has flown. This is the whole reason the counter-frame carries no
    // translation (ADR 0005, decision 5).
    return Orientation.UnrotateVector(UniversePosition - Position);
}

FUniversePosition FShipFlightState::WorldToUniverse(const FVector& WorldPosition) const
{
    return Position + Orientation.RotateVector(WorldPosition);
}

FVector FShipFlightState::UniverseDirectionToWorld(const FVector& UniverseDirection) const
{
    return Orientation.UnrotateVector(UniverseDirection);
}

void FShipFlightState::SetUniverseTransform(const FUniversePosition& NewPosition, const FQuat& NewOrientation)
{
    Position = NewPosition.Normalised();
    Orientation = NewOrientation.GetNormalized();
}
