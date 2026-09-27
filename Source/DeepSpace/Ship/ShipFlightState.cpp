#include "Ship/ShipFlightState.h"

FShipFlightLimits FShipFlightLimits::Cruise()
{
    return FShipFlightLimits();
}

void FShipFlightState::SetLimits(const FShipFlightLimits& NewLimits)
{
    Limits = NewLimits;

    // A top lowered in play takes the notches above it away, the lever's
    // with them; the ease then brings the ship down to the new top. And
    // cruise's astern end-stop moves with cruise's two tops.
    Command.DriveNotch = FMath::Clamp(Command.DriveNotch, 0, GetDriveNotchCount() - 1);
    Command.Throttle = FMath::Clamp(Command.Throttle, -CruiseAsternLimit(), 1.0);
}

double FShipFlightState::CruiseAsternLimit() const
{
    return ShipDriveLever::CruiseAsternLimit(Limits.MaxSpeed, Limits.AsternSpeed);
}

double FShipFlightState::CruiseLeverSpeed() const
{
    return ShipDriveLever::CruiseSpeed(Command.Throttle, Limits.MaxSpeed, Limits.AsternSpeed);
}

const FShipFlightLimits& FShipFlightState::GetLimits() const
{
    return Limits;
}

void FShipFlightState::SetCommand(const FShipFlightCommand& NewCommand)
{
    const bool bWasDrive = Command.bDrive;

    Command.Throttle = FMath::Clamp(NewCommand.Throttle, -CruiseAsternLimit(), 1.0);
    Command.AttitudeRate = FVector(
        FMath::Clamp(NewCommand.AttitudeRate.X, -1.0, 1.0),
        FMath::Clamp(NewCommand.AttitudeRate.Y, -1.0, 1.0),
        FMath::Clamp(NewCommand.AttitudeRate.Z, -1.0, 1.0));
    Command.DriveNotch = FMath::Clamp(NewCommand.DriveNotch, 0, GetDriveNotchCount() - 1);
    Command.bDrive = NewCommand.bDrive;

    if (!bWasDrive && Command.bDrive)
    {
        // Engaging: from the ship's present forward speed, so the drive
        // takes over where cruise was and eases to its lever from there.
        // Mid-spool, the spool had the position already: F again resumes
        // the drive from wherever it had got to (decision 4's table).
        if (!bSpoolingDown)
        {
            DrivePosition = ShipDriveLever::PositionOf(FMath::Max(0.0, Velocity | Orientation.GetForwardVector()));
        }
        bSpoolingDown = false;
    }
    else if (bWasDrive && !Command.bDrive)
    {
        // Leaving: never a clamp. Above cruise's top the ship spools down on
        // the drive's own curve; at or under it, it is already a cruising
        // ship, and cruise's inertia takes it from exactly here.
        SpoolFromSpeed = ShipDriveLever::SpeedAt(DrivePosition);
        bSpoolingDown = SpoolFromSpeed > Limits.MaxSpeed;
        if (!bSpoolingDown)
        {
            DrivePosition = 0.0;
        }
    }
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

    // At rest, exactly: every jump's first moment is the pilot's to choose.
    Velocity = FVector::ZeroVector;
    LastLinearAcceleration = FVector::ZeroVector;
    DrivePosition = 0.0;
    bSpoolingDown = false;
    LastHold = EFlightHold::Free;
    LastHeldFraction = 0.0;
}

void FShipFlightState::SetSurfaces(TArray<FFlightSurface> NewSurfaces)
{
    Surfaces = MoveTemp(NewSurfaces);
}

TConstArrayView<FFlightSurface> FShipFlightState::GetSurfaces() const
{
    return Surfaces;
}

double FShipFlightState::GetRoom() const
{
    return ShipFlight::Room(Surfaces, Position);
}

TOptional<double> FShipFlightState::NearestOnPath(const FVector& Direction) const
{
    TOptional<double> Nearest;
    for (const FFlightSurface& Surface : Surfaces)
    {
        const TOptional<double> D = ShipFlight::RayToFloor(Surface, Position, Direction);
        if (D && (!Nearest || *D < *Nearest))
        {
            Nearest = D;
        }
    }
    return Nearest;
}

double FShipFlightState::MaySpeedAt(double D) const
{
    // The boosters' present acceleration, which the subsystem has already
    // scaled by their allocation: a starved ship brakes softer, starts
    // braking earlier, and arrives just the same.
    return ShipFlight::MaySpeed(D, Limits.LinearAcceleration, Limits.HoldSeconds, FixedStep);
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

    if ((Command.bDrive || bSpoolingDown) && DriveSubStep(FixedDelta))
    {
        return;
    }
    CruiseSubStep(FixedDelta);
}

bool FShipFlightState::DriveSubStep(double FixedDelta)
{
    // Leaving the drive: cruise's top reached, cruise takes it from here, at
    // exactly the velocity the spool left it with.
    if (bSpoolingDown && ShipDriveLever::SpeedAt(DrivePosition) <= Limits.MaxSpeed * (1.0 + 1e-12))
    {
        bSpoolingDown = false;
        DrivePosition = 0.0;
        return false;
    }

    // The lever, eased in notch space (decision 4): a tap is felt at once and
    // settles in a second, a hold climbs in step with the lever, and thrust
    // slows the whole ease, never the top. A spool-down is the drive's own
    // all stop, eased toward STOP exactly as X would ease it, and it ends the
    // substep it passes under cruise's top, the drive's first notch: about
    // 3.3 seconds from 0.1 c, the same as X's to there, so the two ways down
    // from the drive feel alike.
    const double Target = bSpoolingDown ? 0.0 : static_cast<double>(Command.DriveNotch);
    DrivePosition = ShipDriveLever::Ease(DrivePosition, Target, FixedDelta, Limits.DriveResponse, Limits.DriveThrust);

    // The soft cap (decision 5): only when the nose's own path meets a floor,
    // and then on the whole speed, along the nose. The drive's velocity is
    // along the nose, so the ray is where the ship is going: a path that
    // misses is not touched, and one that meets a world flies straight to
    // the point the nose is on, whatever the aim. Nothing is taken sideways,
    // so the ship goes where it points.
    const FVector Nose = Orientation.GetForwardVector();
    const double Eased = ShipDriveLever::SpeedAt(DrivePosition);
    double Speed = Eased;
    LastHold = EFlightHold::Free;
    LastHeldFraction = 0.0;
    if (const TOptional<double> D = NearestOnPath(Nose))
    {
        const double May = MaySpeedAt(*D);
        if (May < Eased)
        {
            Speed = May;

            // What the cap holds, the ease follows: when it lets go -- the
            // nose off the world, or a climb -- the speed rises from where
            // the ship actually was, at the lever's own pace, never in one
            // substep. The one fall faster than the ease is capture, the
            // substep the nose first comes onto a world too fast to allow.
            DrivePosition = ShipDriveLever::PositionOf(May);
            RecordHold(*D, May, bSpoolingDown ? SpoolFromSpeed : FMath::Abs(GetLeverSpeed()));
        }
    }

    // No inertia, deliberately: velocity is set, not chased. So it reports
    // no acceleration -- a capture dropping many notches in one substep would
    // otherwise report an acceleration that would throw anything that ever
    // reads it through a bulkhead (sky decision 8's fake, kept).
    Velocity = Nose * Speed;
    LastLinearAcceleration = FVector::ZeroVector;
    Position += Velocity * FixedDelta;
    return true;
}

void FShipFlightState::RecordHold(double D, double HeldSpeed, double LeverSpeed)
{
    const double Lever = FMath::Abs(LeverSpeed);
    LastHold = D <= AtFloorCm && Lever > 0.0 ? EFlightHold::AtFloor : EFlightHold::HoldingOff;
    LastHeldFraction = Lever > 0.0 ? FMath::Clamp(1.0 - HeldSpeed / Lever, 0.0, 1.0) : 0.0;
}

void FShipFlightState::CruiseSubStep(double FixedDelta)
{
    // The cap sets the assist's target, not the velocity (decision 5): along
    // the commanded direction -- the nose, or aft astern -- the target speed
    // is held to what that direction's path may have, so a cruising ship
    // brakes to rest on a floor under its own inertia. Far from anything
    // this is exactly the cruise there always was.
    const double Want = CruiseLeverSpeed();
    const FVector Along = Orientation.GetForwardVector() * (Want < 0.0 ? -1.0 : 1.0);
    double TargetSpeed = FMath::Abs(Want);
    LastHold = EFlightHold::Free;
    LastHeldFraction = 0.0;
    if (TargetSpeed > 0.0)
    {
        if (const TOptional<double> D = NearestOnPath(Along))
        {
            // The braking curve alone, not the hold: cruise has inertia, and
            // its boosters must deliver whatever slowing the target asks. The
            // hold's d / N falls at v / N, which above the knee is more than
            // the boosters have -- ten times more at a quarter thrust from
            // cruise's top -- and the ship would meet the hard stop at speed.
            // The braking curve asks for 80% of them and no more: a full-
            // thrust ship at cruise's 20 km/s starts braking 125 km up, a
            // starved one 500 km up (decision 5).
            const double May = ShipFlight::MaySpeed(*D, Limits.LinearAcceleration, 0.0, FixedStep);
            if (May < TargetSpeed)
            {
                TargetSpeed = May;
                RecordHold(*D, May, FMath::Abs(GetLeverSpeed()));
            }
        }
    }

    // Velocity, chasing the target. Limiting the change as a vector rather
    // than per axis means a turn cannot cheat extra acceleration out of the
    // model by changing direction.
    const FVector TargetVelocity = Along * TargetSpeed;
    const FVector VelocityError = TargetVelocity - Velocity;
    const double MaxVelocityChange = Limits.LinearAcceleration * FixedDelta;
    const FVector VelocityChange = VelocityError.SizeSquared() <= FMath::Square(MaxVelocityChange)
        ? VelocityError
        : VelocityError.GetSafeNormal() * MaxVelocityChange;

    Velocity += VelocityChange;
    LastLinearAcceleration = VelocityChange / FixedDelta;

    // The hard stop. Cruise can slide after a turn, so its velocity need not
    // be along the path the cap read; no substep may end inside a floor all
    // the same. A velocity that would carry the ship in loses its inward
    // part at the sphere, and the ship ends the substep on it and slides:
    // on it, not where it was, since it would have reached it inside the
    // substep, and at 20 km/s a substep is 167 m -- a ship left where it
    // was would slide along that far above its floor. Under a floor already
    // -- the floor raised in play -- it may climb and may not descend, and
    // is never lifted: a ship does not teleport because a number changed.
    FUniversePosition Next = Position + Velocity * FixedDelta;
    for (const FFlightSurface& Surface : Surfaces)
    {
        const double After = ShipFlight::FloorClearance(Surface, Next);
        if (After >= 0.0)
        {
            continue;
        }
        const FVector FromCentre = Next - Surface.Centre;
        const FVector Out = (Surface.bInsideOut ? -FromCentre : FromCentre).GetSafeNormal();
        const double Inward = Velocity | Out;
        if (Inward < 0.0)
        {
            Velocity -= Out * Inward;
        }
        Next = Position + Velocity * FixedDelta;
        if (ShipFlight::FloorClearance(Surface, Position) >= 0.0)
        {
            Next = Next + Out * (-ShipFlight::FloorClearance(Surface, Next));
        }
    }
    Position = Next;
}

FUniversePosition FShipFlightState::GetUniversePosition() const { return Position; }
FQuat FShipFlightState::GetUniverseOrientation() const { return Orientation; }
FVector FShipFlightState::GetVelocity() const { return Velocity; }
FVector FShipFlightState::GetAngularVelocity() const { return AngularVelocity; }
FVector FShipFlightState::GetAngularAcceleration() const { return LastAngularAcceleration; }
FVector FShipFlightState::GetLinearAcceleration() const { return LastLinearAcceleration; }
double FShipFlightState::GetSpeed() const { return Velocity.Size(); }

EFlightMode FShipFlightState::GetMode() const
{
    return Command.bDrive ? EFlightMode::Drive : bSpoolingDown ? EFlightMode::SpoolingDown : EFlightMode::Cruise;
}

EFlightHold FShipFlightState::GetHold() const { return LastHold; }
double FShipFlightState::GetHeldFraction() const { return LastHeldFraction; }

double FShipFlightState::GetLeverSpeed() const
{
    return Command.bDrive ? ShipDriveLever::NotchSpeed(Command.DriveNotch) : CruiseLeverSpeed();
}

double FShipFlightState::GetOtherLeverSpeed() const
{
    return Command.bDrive ? CruiseLeverSpeed() : ShipDriveLever::NotchSpeed(Command.DriveNotch);
}

double FShipFlightState::GetDrivePosition() const { return DrivePosition; }

int32 FShipFlightState::GetDriveNotchCount() const
{
    return ShipDriveLever::NotchCount(Limits.DriveTop);
}

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
