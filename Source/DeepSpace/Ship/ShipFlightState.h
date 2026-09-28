#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipVerticalLever.h"
#include "Universe/UniversePosition.h"

/**
 * What the ship can do. Cruise today; a combat upgrade is a different set of
 * numbers, not a different model (docs/vision.md, Flight modes).
 */
struct DEEPSPACE_API FShipFlightLimits
{
    /** Top speed under cruise assist, cm/s: 20 km/s (the 2026-09-27 ruling),
     *  which is also the drive's first notch, so leaving the drive hands the
     *  ship to cruise at the speed cruise can hold. */
    double MaxSpeed = 2.0e6;

    /** The fastest cruise goes astern, cm/s: 200 m/s. Backing off a thing is
     *  a manoeuvre, not a way to travel; the lever's astern travel ends here
     *  (ShipDriveLever::CruiseAsternLimit). */
    double AsternSpeed = 2.0e4;

    /** How hard the ship changes velocity, cm/s^2: 2 km/s^2, rest to cruise's
     *  top in ten seconds. The braking curve plans on 80% of it
     *  (ShipFlight::BrakingMargin), so from 20 km/s it stops in 125 km. */
    double LinearAcceleration = 2.0e5;

    /** Peak turn rate, radians/s, about each body axis: X roll, Y pitch, Z
     *  yaw. The 0.3 was meant for roll when the axes were misnamed (X pitch,
     *  Y yaw, Z roll) and has always turned yaw; kept there, pending a
     *  playtest, since yaw is the turn a pilot makes most. */
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
     * The drive lever's top, cm/s: 0.1 c, which is as fast as the drive ever
     * goes (the 2026-09-27 ruling: anything faster is a jump). ds.Drive.Top,
     * which may shorten the lever and never lengthen it; notches above it are
     * gone.
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

    /** ds.Land.GearClearance, cm: the origin's height over flat ground at
     *  rest, and how far below the origin the gear's feet are. */
    double GearClearanceCm = ShipLanding::DefaultGearClearanceCm;

    /** ds.Land.TouchdownSpeed, cm/s: the approach law's floor, contact speed. */
    double TouchdownSpeed = ShipFlight::DefaultTouchdownSpeed;

    /** ds.Land.ApproachSeconds: the approach law's ease; clamped where read. */
    double ApproachSeconds = ShipFlight::DefaultApproachSeconds;

    /** ds.Land.SkimSeconds and .SkimFloor (cm/s): the skim cap. */
    double SkimSeconds = ShipFlight::DefaultSkimSeconds;
    double SkimFloor = ShipFlight::DefaultSkimFloor;

    /** ds.Land.Regime, cm: the near regime's reach over a world's cruise floor. */
    double RegimeCm = ShipFlight::DefaultRegimeCm;

    /** ds.Land.DriveHandback, cm: how far over a solid world's drive floor
     *  the drive takes the ship back. */
    double DriveHandbackCm = ShipFlight::DefaultDriveHandbackCm;

    /** ds.Vertical.Top (cm/s) and .HeavyFloor: the lever's top and the climb
     *  top's floor on heavy worlds. */
    double VerticalTop = ShipVerticalLever::DefaultTopCmPerSecond;
    double VerticalHeavyFloor = ShipVerticalLever::DefaultHeavyFloor;

    /**
     * The starved sink, cm/s (decision 5): ds.Boosters.StarvedSink x (1 -
     * HoldFed), computed by the subsystem from the split. Added to the
     * vertical lever's asked rate only under a solid world's drive floor and
     * only while the lever asks HOVER or a sink -- a starved ship always
     * lifts. The one sanctioned change with time in the power model, bounded,
     * ending at rest on the ground.
     */
    double SinkBias = 0.0;

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
    /** The cruise lever's position, -1..1: what it asks for is read on a log
     *  scale, ShipDriveLever::CruiseSpeed, 1 m/s just off zero to MaxSpeed at
     *  full, and mirrored astern to AsternSpeed, where the lever's astern
     *  travel ends (clamped to it on the way in). Persistent: set and
     *  leave. */
    double Throttle = 0.0;

    /** Fraction of MaxAngularRate about each body axis, -1..1: X roll, Y
     *  pitch, Z yaw -- the axes a rotation vector turns about, so +Y puts the
     *  nose down, +Z swings it to starboard and -X rolls right
     *  (DeepSpace.Playtest.KeysTurnTheShip holds the keys to it).
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
     *  a drive set to 0.1 c, left for a look round in cruise, is at 0.1 c
     *  again the moment F is pressed. */
    int32 DriveNotch = 0;

    /** The vertical lever, -1..1 (landing decision 8): a climb or sink rate
     *  on ShipVerticalLever's log scale, the boosters holding it against
     *  gravity. At zero the ship hovers, and keeps hovering with nobody at
     *  the helm. Persistent, like both other levers: carry it over from
     *  GetCommand() when building a command. */
    double Vertical = 0.0;
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

    /**
     * F live under a solid world's drive floor (landing decision 10): the
     * drive does not take the ship. It flies cruise and the vertical lever,
     * Shift and Ctrl move cruise, the notch keeps its setting and the drive's
     * position is held at the ship's forward speed, so the drive takes over
     * from what the ship is doing -- once the ship is DriveHandbackCm over the
     * floor with the nose's ray clear of it within the drive's hold. F is
     * never refused.
     */
    DriveBelowFloor,
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

/** What the ground's laws did, since the log was last reset: for the
 *  invariant's tests (DeepSpace.Ship.Landing.GroundAlwaysCatches*) and the
 *  playtests, never read by the flight itself. */
struct DEEPSPACE_API FGroundLog
{
    /** The least footprint clearance any substep ended with, cm. */
    double LeastClearance = TNumericLimits<double>::Max();

    /** The fastest a lowest point met the ground along its normal, cm/s, at
     *  the substep it first came within a centimetre (decision 10). */
    double WorstContactSpeed = 0.0;

    int32 Contacts = 0;

    /** How often the ground's hard stop fired: the caps missed a contact. */
    int32 HardStops = 0;
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
     * The two toggles are the only places the mode changes, and in forward
     * flight neither has a substep in which the speed jumps. Engaging starts
     * the drive's eased position at the ship's present forward speed, and
     * sets the velocity along the nose: whatever cruise had astern or
     * sideways is gone in the first substep. A known edge, recorded in the
     * flight-feel spec -- the drive is engaged from a forward cruise. Disengaging above
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

    /**
     * Every body's pull, as wells (landing decision 4): the subsystem hands
     * them over once a frame beside the surfaces, and none in transit. The
     * flight law never adds g dt to the velocity -- the boosters hold every
     * lever against gravity, so the levers mean what they say anywhere --
     * and gravity is felt only as effort, which the subsystem reads here.
     */
    void SetWells(TArray<FGravityWell> NewWells);
    TConstArrayView<FGravityWell> GetWells() const;

    /** The pull where the ship is, cm/s^2, universe axes: the sum over every
     *  well. Asked, never stored. */
    FVector GetLocalGravity() const;

    /** The boosters' proper acceleration, cm/s^2: what the ship did
     *  (GetLinearAcceleration, kinematic, which the hum's "changing" term
     *  keeps reading) less what gravity would have done. */
    FVector GetThrustAcceleration() const;

    /** The ship's origin above the ground directly below it, cm, radially,
     *  clearance included -- the HUD's number, "1.5 M ABOVE GROUND" at rest
     *  on flat ground -- over the nearest world (the regime's measure), if
     *  it is solid. Unset when that world has no ground: over an ocean or a
     *  giant there is nothing below to measure, however near another
     *  world's ground is. */
    TOptional<double> GetGroundAltitude() const;

    /** The least height of any footprint point above that ground, cm: 0 when
     *  a foot touches. What the descent cap, the hard stop and contact read;
     *  the HUD never prints it. Unset as GetGroundAltitude is. */
    TOptional<double> GetFootprintClearance() const;

    /** How far under a solid world's drive floor the ship is, cm; 0 when it
     *  is not under one. The scope of effort (decision 5). */
    double GetDepthUnderDriveFloor() const;

    const FGroundLog& GetGroundLog() const;
    void ResetGroundLog();

    /** In the near regime (decision 8): within Limits.RegimeCm of the
     *  nearest world's cruise floor -- the ground over a solid world, the
     *  floor sphere otherwise -- entering under it and leaving over 1.1 x it. */
    bool IsInNearRegime() const;

    /** 1 at 40 km and under, 0 at 50 km and over: how far cruise flies the
     *  plan view and the vertical lever counts. 0 outside the regime. */
    double GetRegimeWeight() const;

    /** The vertical lever moves the ship: in the regime with the weight
     *  above 0, and cruise's lever flying (Cruise, or DriveBelowFloor). */
    bool IsVerticalLive() const;

    /** What the vertical lever asks, cm/s, + climbing, after the climb top:
     *  the HUD's CLIMB / SINK / HOVER. The starved sink is not in it. */
    double GetVerticalLeverRate() const;

    /** The ship's radial speed, cm/s, + climbing, over the regime's world
     *  (or the nearest world); 0 with none. */
    double GetVerticalSpeed() const;

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
     *  sit a hair under it every substep. While spooling down, against the
     *  speed the spool began from: the live lever is then cruise's, often
     *  STOP, and says nothing about what the cap is holding back. */
    double GetHeldFraction() const;

    /** What the live lever asks for, cm/s: the drive's notch speed, or
     *  cruise's CruiseSpeed of its Throttle, signed, negative astern --
     *  cruise's while spooling down, which is live from the press of F. */
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

    /** How far astern the cruise lever travels under the present limits,
     *  0..1 (ShipDriveLever::CruiseAsternLimit): Throttle's lower end. */
    double CruiseAsternLimit() const;

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
     * 0.1 c it does not. Without it a lever left at 0.1 c would fly the arrival
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
     * metre, which the braking curve closes in a thirtieth of a second and no
     * one can see from the glass. What AtFloor means.
     */
    static constexpr double AtFloorCm = 100.0;

    /** Penetration the ground's hard stop ignores, cm: a tenth of a
     *  millimetre, the rounding of a substep that ends exactly on the
     *  ground, which the approach law's D / Step bound already keeps out. */
    static constexpr double HardStopToleranceCm = 0.01;

private:
    /** What cruise's lever asks for, cm/s, signed: CruiseSpeed of Throttle. */
    double CruiseLeverSpeed() const;

    void SubStep(double FixedDelta);

    /** The drive and the spool-down: the eased position, along the nose,
     *  held to the cap. Returns false when a spool-down has just reached
     *  where cruise can take the ship, and cruise takes this substep
     *  instead. */
    bool DriveSubStep(double FixedDelta);

    /** Cruise under inertia, its target along the commanded direction held
     *  to the cap's braking curve, and the hard stop at every floor. */
    void CruiseSubStep(double FixedDelta);

    /** The nearest meeting of a ray from Position along Direction with any
     *  floor sphere, cm; unset when it meets none. */
    TOptional<double> NearestOnPath(const FVector& Direction) const;

    /** MaySpeed of a distance to a floor, at the boosters' present thrust. */
    double MaySpeedAt(double D) const;

    /**
     * Whether cruise can take the ship from the drive here: the drive's
     * eased position at or under cruise's top, and the speed along the nose
     * within what cruise's braking curve allows on the nose's path, give or
     * take the one substep of braking the boosters can shed at once. The
     * drive's cap holds off at d / HoldSeconds, which above the knee is more
     * than cruise can brake from -- 20 km/s 80 km up, where cruise needs 100
     * km -- so a ship handed over there would meet the hard stop at km/s.
     */
    bool CruiseCanTakeOver() const;

    /** The cap bound this substep, D cm from a floor, at HeldSpeed, below
     *  what the ship was being asked for, LeverSpeed: record what it did for
     *  GetHold and GetHeldFraction. */
    void RecordHold(double D, double HeldSpeed, double LeverSpeed);

    /** The nearest meeting of a ray along Direction with any surface as
     *  cruise sees it -- the ground (plus clearance, less the hull's reach)
     *  over a solid world, the sphere otherwise -- looking far enough to
     *  brake from Speed. */
    TOptional<double> NearestOnCruisePath(const FVector& Direction, double Speed);

    /** The nearest world, by the regime's measure (CruiseFloorClearance),
     *  into Surfaces, if it has a ground; INDEX_NONE otherwise. */
    int32 NearestGround() const;

    /** After the translation: if any footprint point is under the ground,
     *  take the velocity into the ground's normal away and lift the origin
     *  along up by the deepest penetration. The ground only: sphere floors
     *  are never lifted. */
    void GroundHardStop();

    /** Log the footprint after the substep: least clearance, and a contact's
     *  speed the substep a lowest point first comes within a centimetre. */
    void LogGround(const ShipLanding::FFootprintClearance& Foot);

    /** Once a substep: which world is near, the regime with its hysteresis,
     *  and the blend weight. */
    void UpdateRegime();

    /** Once a substep, after the regime: whether a live drive is under a
     *  solid world's drive floor (DriveBelowFloor), entering under the floor
     *  and leaving DriveHandbackCm over it with the nose clear. */
    void UpdateDriveBelowFloor();

    /** The cruise floor's clearance over one surface, cm. */
    double CruiseFloorClearance(const FFlightSurface& Surface) const;

    /** What the plan asks radially, cm/s: the lever's rate, the climb top,
     *  and the starved sink under the floor at HOVER or sinking. */
    double AskedVerticalRate() const;

    /** The ground ahead of the ship's origin along a horizontal Heading at
     *  its own height, less the hull's reach, for the along-ground cap. */
    TOptional<double> GroundAhead(int32 SurfaceIndex, const FVector& Heading, double Speed);

    /**
     * The ground rays, marched once a frame per direction (decision 10's
     * cost): each later substep of the frame takes the distance flown along
     * the ray off the proven-clear distance -- a true lower bound only for
     * motion along the ray -- and marches again as soon as the clearance the
     * new ray could have lost against the old one, (offset across it + the
     * distance left x the turn) x (1 + MaxSlope), passes
     * ShipFlight::RayReuseToleranceCm: a sinking or sliding ship, or a turn,
     * marches each substep. Slot 0 is cruise's path along the nose, slot 1
     * the horizontal heading in the regime.
     */
    struct FGroundRayCache
    {
        int32 Slot = INDEX_NONE;
        int32 Surface = INDEX_NONE;
        FVector Direction = FVector::ZeroVector;
        FUniversePosition From;
        TOptional<double> Hit;
        double SeenTo = 0.0;
        int32 Frame = -1;
    };
    TArray<FGroundRayCache, TInlineAllocator<4>> RayCache;
    int32 FrameCount = 0;

    TOptional<double> CachedRay(int32 Slot, int32 SurfaceIndex, const FVector& Direction, double Clearance, double Lookahead);

    FUniversePosition Position;
    FQuat   Orientation = FQuat::Identity;
    FVector Velocity = FVector::ZeroVector;        // cm/s, universe frame
    FVector AngularVelocity = FVector::ZeroVector; // rad/s, body frame
    FVector LastAngularAcceleration = FVector::ZeroVector;
    FVector LastLinearAcceleration = FVector::ZeroVector;

    double JumpCharge = 0.0;

    /** The drive's eased position, notch space. */
    double DrivePosition = 0.0;

    /** Leaving the drive where cruise cannot take the ship -- above cruise's
     *  top, or faster than cruise could brake from on the path -- until it
     *  can (CruiseCanTakeOver). */
    bool bSpoolingDown = false;

    /** The speed the spool began from, cm/s: what a hold during the spool
     *  is measured against. */
    double SpoolFromSpeed = 0.0;

    EFlightHold LastHold = EFlightHold::Free;
    double LastHeldFraction = 0.0;

    TArray<FFlightSurface> Surfaces;
    TArray<FGravityWell> Wells;

    FGroundLog GroundLog;
    double LastFootprintLeast = TNumericLimits<double>::Max();

    bool bInRegime = false;
    double RegimeWeight = 0.0;
    int32 RegimeSurface = INDEX_NONE;

    /** F live under a solid world's drive floor (EFlightMode::DriveBelowFloor). */
    bool bDriveBelowFloor = false;

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
