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
    const bool bWasBelowFloor = bDriveBelowFloor;

    Command.Throttle = FMath::Clamp(NewCommand.Throttle, -CruiseAsternLimit(), 1.0);
    Command.AttitudeRate = FVector(
        FMath::Clamp(NewCommand.AttitudeRate.X, -1.0, 1.0),
        FMath::Clamp(NewCommand.AttitudeRate.Y, -1.0, 1.0),
        FMath::Clamp(NewCommand.AttitudeRate.Z, -1.0, 1.0));
    Command.DriveNotch = FMath::Clamp(NewCommand.DriveNotch, 0, GetDriveNotchCount() - 1);
    Command.Vertical = FMath::Clamp(NewCommand.Vertical, -1.0, 1.0);
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
        // the drive's own curve, and held off a floor faster than cruise
        // could brake from, it goes on down the drive's cap until cruise
        // can; otherwise it is already a cruising ship, and cruise's inertia
        // takes it from exactly here.
        SpoolFromSpeed = ShipDriveLever::SpeedAt(DrivePosition);
        // From DriveBelowFloor cruise was already flying the ship: there is
        // nothing to spool down from. (CruiseCanTakeOver reads the floor
        // sphere, which a ship under it meets at once; asked here it would
        // start a spool that the drive's cap holds at a dead stop.)
        bSpoolingDown = !bWasBelowFloor && !CruiseCanTakeOver();
        bDriveBelowFloor = false;
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
    Command.Vertical = 0.0;
    bInRegime = false;
    RegimeWeight = 0.0;
    RegimeSurface = INDEX_NONE;
    bDriveBelowFloor = false;
    RayCache.Reset();
}

void FShipFlightState::SetSurfaces(TArray<FFlightSurface> NewSurfaces)
{
    Surfaces = MoveTemp(NewSurfaces);
    RayCache.Reset();
}

TConstArrayView<FFlightSurface> FShipFlightState::GetSurfaces() const
{
    return Surfaces;
}

double FShipFlightState::GetRoom() const
{
    return ShipFlight::Room(Surfaces, Position, Limits.GearClearanceCm);
}

void FShipFlightState::SetWells(TArray<FGravityWell> NewWells)
{
    Wells = MoveTemp(NewWells);
}

TConstArrayView<FGravityWell> FShipFlightState::GetWells() const
{
    return Wells;
}

FVector FShipFlightState::GetLocalGravity() const
{
    return ShipFlight::GravityAt(Wells, Position);
}

FVector FShipFlightState::GetThrustAcceleration() const
{
    return LastLinearAcceleration - GetLocalGravity();
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

bool FShipFlightState::CruiseCanTakeOver() const
{
    if (ShipDriveLever::SpeedAt(DrivePosition) > Limits.MaxSpeed * (1.0 + 1e-12))
    {
        return false;
    }
    // Cruise's own law, the braking curve alone (CruiseSubStep), with one
    // substep of the boosters' whole push to spare: along the curve the
    // allowed speed falls by 80% of that a substep, so a ship the drive has
    // brought down the curve is taken on it, and cruise, braking at full,
    // is back under it within a few substeps.
    const TOptional<double> D = NearestOnPath(Orientation.GetForwardVector());
    return !D || GetSpeed() <= ShipFlight::MaySpeed(*D, Limits.LinearAcceleration, 0.0, FixedStep)
        + Limits.LinearAcceleration * FixedStep;
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
    ++FrameCount;

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

    UpdateRegime();

    UpdateDriveBelowFloor();
    if (bDriveBelowFloor)
    {
        // Held, never eased while hidden: when the drive takes over it starts
        // from what the ship is doing -- after cruise's substep, so the
        // position is the speed the substep ended with, not one substep of
        // the boosters behind it.
        CruiseSubStep(FixedDelta);
        DrivePosition = ShipDriveLever::PositionOf(FMath::Max(0.0, Velocity | Orientation.GetForwardVector()));
        return;
    }
    if ((Command.bDrive || bSpoolingDown) && DriveSubStep(FixedDelta))
    {
        return;
    }
    CruiseSubStep(FixedDelta);
}

bool FShipFlightState::DriveSubStep(double FixedDelta)
{
    // Leaving the drive: cruise's top reached, and a speed cruise can brake
    // from on the path, cruise takes it from here, at exactly the velocity
    // the spool left it with.
    if (bSpoolingDown && CruiseCanTakeOver())
    {
        bSpoolingDown = false;
        DrivePosition = 0.0;
        return false;
    }

    // The lever, eased in notch space (decision 4): a tap is felt at once and
    // is on its notch in 2.5 s, a hold climbs in step with the lever, and
    // thrust slows the whole ease, never the top. A spool-down is the
    // drive's own all stop, eased toward STOP exactly as X would ease it
    // until it reaches cruise's top, the drive's first notch: about 3.3
    // seconds from 0.1 c, the same as X's to there, so the two ways down from
    // the drive feel alike. It eases no lower than that top, or than the
    // ship's own speed if it is under it already (a spool held on to the cap
    // until cruise can brake): the substep that would pass under lands on
    // it, so cruise takes the ship at the speed it holds, and a lever at
    // full sees no dip and no rise. Only the cap takes it lower.
    const double Target = bSpoolingDown ? 0.0 : static_cast<double>(Command.DriveNotch);
    DrivePosition = ShipDriveLever::Ease(DrivePosition, Target, FixedDelta, Limits.DriveResponse, Limits.DriveThrust);

    // The soft cap (decision 5): only when the nose's own path meets a floor,
    // and then on the whole speed, along the nose. The drive's velocity is
    // along the nose, so the ray is where the ship is going: a path that
    // misses is not touched, and one that meets a world flies straight to
    // the point the nose is on, whatever the aim. Nothing is taken sideways,
    // so the ship goes where it points.
    const FVector Nose = Orientation.GetForwardVector();
    double Eased = ShipDriveLever::SpeedAt(DrivePosition);
    if (bSpoolingDown)
    {
        const double Least = FMath::Min(Limits.MaxSpeed, GetSpeed());
        if (Eased < Least)
        {
            Eased = Least;
            DrivePosition = ShipDriveLever::PositionOf(Least);
        }
    }
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
    const FVector Nose = Orientation.GetForwardVector();
    const double Want = CruiseLeverSpeed();
    const FVector Along = Nose * (Want < 0.0 ? -1.0 : 1.0);
    const double Speed = FMath::Abs(Want);
    const double W = RegimeWeight;
    LastHold = EFlightHold::Free;
    LastHeldFraction = 0.0;

    // Above the regime, and fading out across its top: along the nose, held
    // to the braking curve by whatever the path meets -- the ground over a
    // solid world, the sphere otherwise (decision 5, unchanged far out).
    FVector Classic = FVector::ZeroVector;
    if (W < 1.0 && Speed > 0.0)
    {
        double Held = Speed;
        if (const TOptional<double> D = NearestOnCruisePath(Along, Speed))
        {
            const double May = ShipFlight::MaySpeed(*D, Limits.LinearAcceleration, 0.0, FixedStep);
            if (May < Held)
            {
                Held = May;
                RecordHold(*D, May, Speed);
            }
        }
        Classic = Along * Held;
    }

    // In the regime (decision 8): the nose's horizontal projection at the
    // cruise lever's speed, plus up at the vertical lever's rate, so looking
    // down never dives the ship and zero is hover for any cruise setting.
    // Over solid ground the horizontal part is held to the skim cap and to
    // the ground ahead at the ship's height.
    FVector Plan = FVector::ZeroVector;
    FVector Up = FVector::ZeroVector;
    const FFlightSurface* World = Surfaces.IsValidIndex(RegimeSurface) ? &Surfaces[RegimeSurface] : nullptr;
    if (W > 0.0 && World)
    {
        Up = (Position - World->Centre).GetSafeNormal();
        const FVector Flat = Along - Up * (Along | Up);
        const double FlatSize = Flat.Size();
        const FVector Heading = FlatSize > 1.0e-6 ? Flat / FlatSize : FVector::ZeroVector;
        double Horizontal = FlatSize > 1.0e-6 ? Speed : 0.0;
        if (World->HasGround() && Horizontal > 0.0)
        {
            const double Skim = ShipFlight::SkimCap(ShipFlight::GroundAt(*World, Position).Get(0.0), Limits.SkimSeconds, Limits.SkimFloor);
            if (Skim < Horizontal)
            {
                Horizontal = Skim;
                RecordHold(TNumericLimits<double>::Max(), Skim, Speed);
            }
            if (const TOptional<double> D = GroundAhead(RegimeSurface, Heading, Horizontal))
            {
                const double May = ShipFlight::GroundApproachSpeed(*D, Limits.LinearAcceleration, Limits.ApproachSeconds,
                                                                   Limits.TouchdownSpeed, FixedStep);
                if (May < Horizontal)
                {
                    Horizontal = May;
                    RecordHold(*D, May, Speed);
                }
            }
        }
        Plan = Heading * Horizontal + Up * AskedVerticalRate();
    }
    FVector Target = Classic * (1.0 - W) + Plan * W;

    // Down (decision 10): whatever the levers and the starved sink ask, the
    // descent is held to the approach law over the footprint's least
    // clearance, so the ground always catches, gently.
    if (W > 0.0 && World && World->HasGround())
    {
        const TArray<ShipLanding::FFootprintHeight, TFixedAllocator<8>> Heights =
            ShipLanding::FootprintHeights(*World, Position, Orientation, Limits.GearClearanceCm);
        double Clear = TNumericLimits<double>::Max();
        for (const ShipLanding::FFootprintHeight& Point : Heights)
        {
            Clear = FMath::Min(Clear, Point.Above);
        }
        const double Down = -(Target | Up);
        const double May = ShipFlight::GroundApproachSpeed(FMath::Max(Clear, 0.0), Limits.LinearAcceleration,
                                                           Limits.ApproachSeconds, Limits.TouchdownSpeed, FixedStep);
        if (Down > May)
        {
            Target += Up * (Down - May);
        }

        // Across a slope (the along-ground cap, at the footprint): the ground
        // under a point rises into it as the ship moves across it, faster
        // than the ship descends. Each point's closing along its ground's
        // normal is held to the approach law over its gap along that normal
        // by slowing the motion across the ground -- never by lifting, and
        // never the descent, held above. A point whose gap allows more than
        // the ship's whole speed, at the steepest lean the ground can have,
        // cannot bind, and its normal is not read.
        const FVector Across = Target - Up * (Target | Up);
        const double Asked = FMath::Max(Target.Size(), Velocity.Size());
        const double LeastLean = 1.0 / FMath::Sqrt(1.0 + FMath::Square(World->Ground->MaxSlope()));
        double Keep = 1.0;
        double KeptGap = 0.0;
        for (const ShipLanding::FFootprintHeight& Point : Heights)
        {
            const double Above = FMath::Max(Point.Above, 0.0);
            if (ShipFlight::GroundApproachSpeed(Above * LeastLean, Limits.LinearAcceleration, Limits.ApproachSeconds,
                                                Limits.TouchdownSpeed, FixedStep) >= Asked)
            {
                continue;
            }
            // Read where the point will be at the substep's end, which is
            // where its contact is met.
            const FVector Next = Point.FromCentre + Target * FixedDelta;
            const FVector Normal(ShipGround::NormalAt(*World->Ground, FVector3d(Next.GetSafeNormal())));
            const double Into = -(Across | Normal);
            if (Into <= 0.0)
            {
                continue;
            }
            const double Gap = Above * FMath::Clamp(Normal | Up, 0.0, 1.0);
            const double MayClose = ShipFlight::GroundApproachSpeed(Gap, Limits.LinearAcceleration, Limits.ApproachSeconds,
                                                                    Limits.TouchdownSpeed, FixedStep);
            const double Sinking = -((Up * (Target | Up)) | Normal);
            const double Allowed = FMath::Clamp((MayClose - Sinking) / Into, 0.0, 1.0);
            if (Allowed < Keep)
            {
                Keep = Allowed;
                KeptGap = Gap;
            }
        }
        if (Keep < 1.0)
        {
            Target -= Across * (1.0 - Keep);
            RecordHold(KeptGap, Across.Size() * Keep, Speed);
        }
    }

    // Over a world with no ground (an ocean, a giant) the floor sphere is
    // what the descent meets: cruise's own braking curve to it, as cruise has
    // always braked for a floor, so the vertical lever sinks onto it and
    // never into the sphere hard stop at speed.
    if (W > 0.0 && World && !World->HasGround())
    {
        const double Sinking = -(Target | Up);
        const double MaySink = ShipFlight::MaySpeed(FMath::Max(ShipFlight::FloorClearance(*World, Position), 0.0),
                                                    Limits.LinearAcceleration, 0.0, FixedStep);
        if (Sinking > MaySink)
        {
            Target += Up * (Sinking - MaySink);
        }
    }

    // Velocity, chasing the target, as a vector (a turn cannot cheat
    // acceleration out of the model by changing direction).
    const FVector VelocityError = Target - Velocity;
    const double MaxVelocityChange = Limits.LinearAcceleration * FixedDelta;
    const FVector VelocityChange = VelocityError.SizeSquared() <= FMath::Square(MaxVelocityChange)
        ? VelocityError
        : VelocityError.GetSafeNormal() * MaxVelocityChange;
    Velocity += VelocityChange;
    LastLinearAcceleration = VelocityChange / FixedDelta;

    // The sphere hard stop, as it always was, for surfaces with no ground.
    FUniversePosition Next = Position + Velocity * FixedDelta;
    for (const FFlightSurface& Surface : Surfaces)
    {
        if (Surface.HasGround())
        {
            continue;   // cruise's floor over a solid world is the ground, below
        }
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
    GroundHardStop();
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
    if (Command.bDrive)
    {
        return bDriveBelowFloor ? EFlightMode::DriveBelowFloor : EFlightMode::Drive;
    }
    return bSpoolingDown ? EFlightMode::SpoolingDown : EFlightMode::Cruise;
}

double FShipFlightState::GetLeverSpeed() const
{
    return GetMode() == EFlightMode::Drive ? ShipDriveLever::NotchSpeed(Command.DriveNotch) : CruiseLeverSpeed();
}

double FShipFlightState::GetOtherLeverSpeed() const
{
    return GetMode() == EFlightMode::Drive ? CruiseLeverSpeed() : ShipDriveLever::NotchSpeed(Command.DriveNotch);
}

bool FShipFlightState::IsVerticalLive() const
{
    const EFlightMode Mode = GetMode();
    return RegimeWeight > 0.0 && (Mode == EFlightMode::Cruise || Mode == EFlightMode::DriveBelowFloor);
}

EFlightHold FShipFlightState::GetHold() const { return LastHold; }
double FShipFlightState::GetHeldFraction() const { return LastHeldFraction; }

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

TOptional<double> FShipFlightState::NearestOnCruisePath(const FVector& Direction, double Speed)
{
    const double Braking = 2.0 * ShipFlight::BrakingMargin * FMath::Max(Limits.LinearAcceleration, 1.0);
    const double Reach = ShipLanding::ReachCm(Limits.GearClearanceCm);
    const double Lookahead = 1.5 * (Speed * Speed / Braking + Speed * FixedStep) + Reach + 1.0e5;
    TOptional<double> Nearest;
    for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
    {
        const FFlightSurface& Surface = Surfaces[Index];
        TOptional<double> D;
        if (Surface.HasGround())
        {
            D = CachedRay(0, Index, Direction, Limits.GearClearanceCm, Lookahead);
            if (D)
            {
                D = FMath::Max(0.0, *D - Reach);   // the hull reaches ahead of its origin
            }
        }
        else
        {
            D = ShipFlight::RayToFloor(Surface, Position, Direction);
        }
        if (D && (!Nearest || *D < *Nearest))
        {
            Nearest = D;
        }
    }
    return Nearest;
}

int32 FShipFlightState::NearestGround() const
{
    int32 Best = INDEX_NONE;
    double Least = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
    {
        if (const TOptional<double> Agl = ShipFlight::GroundAt(Surfaces[Index], Position))
        {
            if (*Agl < Least)
            {
                Least = *Agl;
                Best = Index;
            }
        }
    }
    return Best;
}

TOptional<double> FShipFlightState::GetGroundAltitude() const
{
    const int32 Index = NearestGround();
    return Index == INDEX_NONE ? TOptional<double>() : ShipFlight::GroundAt(Surfaces[Index], Position);
}

TOptional<double> FShipFlightState::GetFootprintClearance() const
{
    const int32 Index = NearestGround();
    if (Index == INDEX_NONE)
    {
        return {};
    }
    return ShipLanding::FootprintClearance(Surfaces[Index], Position, Orientation, Limits.GearClearanceCm).Least;
}

double FShipFlightState::GetDepthUnderDriveFloor() const
{
    double Depth = 0.0;
    for (const FFlightSurface& Surface : Surfaces)
    {
        if (Surface.HasGround())
        {
            Depth = FMath::Max(Depth, -ShipFlight::FloorClearance(Surface, Position));
        }
    }
    return Depth;
}

const FGroundLog& FShipFlightState::GetGroundLog() const
{
    return GroundLog;
}

void FShipFlightState::ResetGroundLog()
{
    GroundLog = FGroundLog();
    LastFootprintLeast = TNumericLimits<double>::Max();
}

void FShipFlightState::GroundHardStop()
{
    const double Reach = ShipLanding::ReachCm(Limits.GearClearanceCm);
    for (const FFlightSurface& Surface : Surfaces)
    {
        if (!Surface.HasGround())
        {
            continue;
        }
        const FVector Out = Position - Surface.Centre;
        if (Out.Size() > Surface.Radius + Surface.Ground->MaxHeightCm() + Reach)
        {
            continue;   // above every peak by more than the hull reaches
        }
        ShipLanding::FFootprintClearance Foot = ShipLanding::FootprintClearance(Surface, Position, Orientation, Limits.GearClearanceCm);
        if (Foot.Least < -HardStopToleranceCm)
        {
            ++GroundLog.HardStops;
            const double Into = Velocity | Foot.GroundNormal;
            if (Into < 0.0)
            {
                Velocity -= Foot.GroundNormal * Into;
            }
            const FVector Up = Out.GetSafeNormal();
            for (int32 Pass = 0; Pass < 4 && Foot.Least < 0.0; ++Pass)
            {
                Position += Up * -Foot.Least;
                Foot = ShipLanding::FootprintClearance(Surface, Position, Orientation, Limits.GearClearanceCm);
            }
            Position = Position.Normalised();
        }
        LogGround(Foot);
    }
}

void FShipFlightState::LogGround(const ShipLanding::FFootprintClearance& Foot)
{
    GroundLog.LeastClearance = FMath::Min(GroundLog.LeastClearance, Foot.Least);
    if (Foot.Least < 1.0 && LastFootprintLeast >= 1.0 && Foot.Point != INDEX_NONE)
    {
        // The lowest point's own velocity: the ship's, and the turn's lever
        // arm to it (body rates about body axes, turned into universe axes).
        const FVector Arm = ShipLanding::FootprintPoints(Limits.GearClearanceCm)[Foot.Point];
        const FVector PointVelocity = Velocity + Orientation.RotateVector(FVector::CrossProduct(AngularVelocity, Arm));
        GroundLog.WorstContactSpeed = FMath::Max(GroundLog.WorstContactSpeed, -(PointVelocity | Foot.GroundNormal));
        ++GroundLog.Contacts;
    }
    LastFootprintLeast = Foot.Least;
}

double FShipFlightState::CruiseFloorClearance(const FFlightSurface& Surface) const
{
    const TOptional<double> Ground = ShipFlight::GroundAt(Surface, Position);
    return Ground ? *Ground - Limits.GearClearanceCm : ShipFlight::FloorClearance(Surface, Position);
}

void FShipFlightState::UpdateRegime()
{
    int32 Best = INDEX_NONE;
    double Least = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
    {
        if (Surfaces[Index].bWorld && !Surfaces[Index].bInsideOut)
        {
            const double Clear = CruiseFloorClearance(Surfaces[Index]);
            if (Clear < Least)
            {
                Least = Clear;
                Best = Index;
            }
        }
    }
    const double Enter = FMath::Max(Limits.RegimeCm, 1.0);
    if (Best == INDEX_NONE)
    {
        bInRegime = false;
    }
    else if (!bInRegime && Least < Enter)
    {
        bInRegime = true;
    }
    else if (bInRegime && Least > Enter * ShipFlight::RegimeExitFactor)
    {
        bInRegime = false;
    }
    RegimeSurface = Best;
    RegimeWeight = bInRegime ? FMath::Clamp((Enter - Least) / (Enter * ShipFlight::RegimeBlendFraction), 0.0, 1.0) : 0.0;
}

double FShipFlightState::GetVerticalLeverRate() const
{
    const double Rate = ShipVerticalLever::Rate(Command.Vertical, Limits.VerticalTop);
    return FMath::Min(Rate, ShipVerticalLever::ClimbTop(Limits.VerticalTop, GetLocalGravity().Size(), Limits.VerticalHeavyFloor));
}

double FShipFlightState::AskedVerticalRate() const
{
    double Rate = GetVerticalLeverRate();
    if (Rate <= 0.0 && GetDepthUnderDriveFloor() > 0.0)
    {
        Rate -= FMath::Max(Limits.SinkBias, 0.0);
    }
    return Rate;
}

TOptional<double> FShipFlightState::GroundAhead(int32 SurfaceIndex, const FVector& Heading, double Speed)
{
    const double Reach = ShipLanding::ReachCm(Limits.GearClearanceCm);
    const double Braking = 2.0 * ShipFlight::BrakingMargin * FMath::Max(Limits.LinearAcceleration, 1.0);
    const double Lookahead = 1.5 * (Speed * Speed / Braking + Speed * FMath::Max(Limits.ApproachSeconds, ShipFlight::MinApproachSeconds))
                           + Reach + 1.0e3;
    const TOptional<double> D = CachedRay(1, SurfaceIndex, Heading, Limits.GearClearanceCm, Lookahead);
    return D ? TOptional<double>(FMath::Max(0.0, *D - Reach)) : TOptional<double>();
}

bool FShipFlightState::IsInNearRegime() const { return bInRegime; }
double FShipFlightState::GetRegimeWeight() const { return RegimeWeight; }

double FShipFlightState::GetVerticalSpeed() const
{
    int32 Index = RegimeSurface;
    if (!Surfaces.IsValidIndex(Index))
    {
        return 0.0;
    }
    return Velocity | (Position - Surfaces[Index].Centre).GetSafeNormal();
}

void FShipFlightState::UpdateDriveBelowFloor()
{
    if (!Command.bDrive)
    {
        bDriveBelowFloor = false;
        return;
    }
    const FVector Nose = Orientation.GetForwardVector();
    const double Speed = ShipDriveLever::SpeedAt(DrivePosition);
    const double Braking = 2.0 * ShipFlight::BrakingMargin * FMath::Max(Limits.LinearAcceleration, 1.0);
    const double Hold = FMath::Max(Speed * FMath::Max(Limits.HoldSeconds, 0.0), Speed * Speed / Braking);
    bool bUnder = false;
    bool bKeep = false;
    for (const FFlightSurface& Surface : Surfaces)
    {
        if (!Surface.HasGround())
        {
            continue;
        }
        const double Clear = ShipFlight::FloorClearance(Surface, Position);
        // The drive's own cap lands it on its floor to the rounding of a
        // distance a planet's radius long, a hair either side. That is AT
        // THE FLOOR, where every drive approach ends, never under it; only
        // a centimetre down is the ship really under.
        constexpr double UnderFloorToleranceCm = 1.0;
        bUnder |= Clear < -UnderFloorToleranceCm;
        if (Clear < Limits.DriveHandbackCm)
        {
            bKeep = true;
            continue;
        }
        const TOptional<double> D = ShipFlight::RayToFloor(Surface, Position, Nose);
        bKeep |= D.IsSet() && *D <= Hold;
    }
    bDriveBelowFloor = bDriveBelowFloor ? bKeep : bUnder;
}

TOptional<double> FShipFlightState::CachedRay(int32 Slot, int32 SurfaceIndex, const FVector& Direction, double Clearance, double Lookahead)
{
    const FVector U = Direction.GetSafeNormal();
    FGroundRayCache* Cache = RayCache.FindByPredicate([&](const FGroundRayCache& Entry)
    {
        return Entry.Slot == Slot && Entry.Surface == SurfaceIndex;
    });
    if (Cache && Cache->Frame == FrameCount && (Cache->Direction | U) >= FMath::Cos(FMath::DegreesToRadians(1.0)))
    {
        const double Flown = FMath::Max(0.0, (Position - Cache->From) | Cache->Direction);
        if (Cache->Hit)
        {
            return FMath::Max(0.0, *Cache->Hit - Flown);
        }
        if (Cache->SeenTo - Flown >= Lookahead)
        {
            return {};
        }
    }
    if (!Cache)
    {
        Cache = &RayCache.AddDefaulted_GetRef();
        Cache->Slot = Slot;
        Cache->Surface = SurfaceIndex;
    }
    Cache->Direction = U;
    Cache->From = Position;
    Cache->Frame = FrameCount;
    Cache->SeenTo = Lookahead;
    Cache->Hit = ShipFlight::RayToGround(Surfaces[SurfaceIndex], Position, U, Clearance, Lookahead);
    return Cache->Hit;
}
