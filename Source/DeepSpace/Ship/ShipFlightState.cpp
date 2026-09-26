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
    if (Command.bDrive && !NewCommand.bDrive && Velocity.SizeSquared() > FMath::Square(Limits.MaxSpeed))
    {
        Velocity = Velocity.GetSafeNormal() * Limits.MaxSpeed;
    }
    Command.bDrive = NewCommand.bDrive;
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

void FShipFlightState::ChargeJumpDrive(double DeltaSeconds, double Satisfaction, double SecondsFromCold)
{
    if (DeltaSeconds <= 0.0)
    {
        return;
    }
    // A non-positive time from cold means "instantly", which is what a
    // tuning value of 0 would be asked to mean; never a division by zero.
    if (SecondsFromCold <= 0.0)
    {
        JumpCharge = Satisfaction > 0.0 ? 1.0 : JumpCharge;
        return;
    }
    const double Rate = FMath::Clamp(Satisfaction, 0.0, 1.0) / SecondsFromCold;
    JumpCharge = FMath::Clamp(JumpCharge + Rate * DeltaSeconds, 0.0, 1.0);
}

double FShipFlightState::GetJumpCharge() const
{
    return JumpCharge;
}

void FShipFlightState::SpendJumpCharge()
{
    JumpCharge = 0.0;
}

void FShipFlightState::JumpTo(const FUniversePosition& Arrival)
{
    Position = Arrival.Normalised();
}

void FShipFlightState::SetDriveRoom(double NearestSurfaceDistanceCm, const FVector& AwayFromSurface)
{
    DriveSurfaceDistance = FMath::Max(0.0, NearestSurfaceDistanceCm);
    DriveAwayFromSurface = AwayFromSurface.GetSafeNormal();
}

double FShipFlightState::GetDriveRoom() const
{
    return FMath::Max(0.0, DriveSurfaceDistance - Limits.DriveFloor);
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

    if (Command.bDrive)
    {
        // The drive. Speed is a fraction of the room per second, so distance
        // to the nearest surface falls exponentially with no easing curve, and
        // a world swells from a point to a disc with no moment of change.
        //
        // Closing on the surface is held to that rate and no more, even where
        // cruise would be faster: the room then falls by e every tau all the
        // way down, and the ship settles onto the floor rather than crossing
        // it. A lever left on is somewhere to come back to, never a course
        // that has gone wrong while you were in the galley. Any heading that
        // does not close -- along the surface, away from it, or in transit
        // with nothing to close on -- gets at least cruise, so the drive is
        // never a trap at the floor.
        //
        // The cap scales the whole velocity rather than removing its closing
        // part, so the ship still goes where the nose points, only slower.
        //
        // No inertia, deliberately: velocity is set, not chased. So it reports
        // no acceleration -- a ship going from 200 m/s to 34 c in one substep
        // would otherwise report an acceleration that would throw anything
        // that ever reads it through a bulkhead.
        const double Tau = FMath::Max(Limits.DriveTau, UE_DOUBLE_SMALL_NUMBER);
        const double RoomRate = GetDriveRoom() / Tau;
        Velocity = Orientation.GetForwardVector() * (Command.Throttle * FMath::Max(Limits.MaxSpeed, RoomRate));

        const double Closing = -(Velocity | DriveAwayFromSurface);
        const double MayClose = FMath::Abs(Command.Throttle) * RoomRate;
        if (Closing > MayClose)
        {
            Velocity *= MayClose / Closing;
        }
        LastLinearAcceleration = FVector::ZeroVector;
        Position += Velocity * FixedDelta;
        return;
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

FVector ShipDrive::AwayFromSurface(TFunctionRef<double(const FUniversePosition&)> SurfaceDistance,
                                   const FUniversePosition& Where)
{
    FVector Gradient = FVector::ZeroVector;
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FVector Probe = FVector::ZeroVector;
        Probe[Axis] = SurfaceProbeCm;
        Gradient[Axis] = SurfaceDistance(Where + Probe) - SurfaceDistance(Where + (-Probe));
    }
    return Gradient.GetSafeNormal();
}
