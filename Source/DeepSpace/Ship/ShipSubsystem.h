#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipPowerState.h"
#include "ShipSubsystem.generated.h"

class APawn;
class UShipModuleDataAsset;
class UUniverseSubsystem;
struct FSkyBody;
struct FTargetView;

/**
 * What the helm's hands are doing this frame (flight-feel decision 1): held
 * attitude, whether each lever key is down, and how many times each was
 * pressed since the last hand-over. Not a lever position -- the levers are
 * the ship's, in FShipFlightCommand -- so a pawn keeps no copy of ship state,
 * and a tap pressed and released inside one frame still arrives as a press.
 */
struct DEEPSPACE_API FHelmInput
{
    /** -1..1 per body axis: X pitch, Y yaw, Z roll. */
    FVector Attitude = FVector::ZeroVector;

    /** Shift and Ctrl held: the live lever's up and down. */
    bool bUpHeld = false;
    bool bDownHeld = false;

    /** Presses since the last hand-over, each one notch under the drive and
     *  the fresh press cruise's detent at zero asks for. */
    int32 UpPresses = 0;
    int32 DownPresses = 0;
};

/**
 * Authoritative ship state. Knows nothing about meshes, rooms, or the player.
 * Everything visible reads from this; nothing else stores ship state.
 *
 * A subsystem rather than an actor: created and destroyed with the world
 * automatically, globally reachable without a singleton, and unable to
 * accidentally acquire a transform and become a god-actor.
 */
UCLASS()
class DEEPSPACE_API UShipSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Convenience accessor. Returns nullptr if there is no world. */
    static UShipSubsystem* Get(const UObject* WorldContext);

    /** Also starts UUniverseSubsystem first: the ship asks it what is out
     *  there, and asks from begin-play onward. */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * The one owner of where the ship starts (plan conflict 3): the start
     * system's largest planet dead ahead, its star to starboard
     * (NavStart::OpeningPlacement), and the start system marked visited.
     * Game worlds only, which includes the tests' worlds; ds.Nav.PlaceAtStart 0
     * leaves the ship wherever it was.
     */
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    /**
     * The subsystem owns the flight state, so it owns the clock that advances
     * it. A plain UWorldSubsystem does not tick; UTickableWorldSubsystem mixes
     * in FTickableGameObject to get a per-frame callback, and tickable
     * subsystems run early in the world tick, before actors -- which is what
     * the counter-frame needs.
     */
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    /** Returns false if the module is null or already installed. */
    UFUNCTION(BlueprintCallable, Category = "Ship")
    bool InstallModule(UShipModuleDataAsset* Module);

    /** What is installed, as installed; ask rather than keep a copy. */
    const TArray<TObjectPtr<UShipModuleDataAsset>>& GetInstalledModules() const { return InstalledModules; }

    /** Returns false if the module is null or was not installed. */
    UFUNCTION(BlueprintCallable, Category = "Ship")
    bool RemoveModule(UShipModuleDataAsset* Module);

    UFUNCTION(BlueprintPure, Category = "Ship")
    float GetPowerDraw() const;

    UFUNCTION(BlueprintPure, Category = "Ship")
    float GetPowerHeadroom() const;

    UFUNCTION(BlueprintPure, Category = "Ship")
    float GetReactorOutput() const;

    UFUNCTION(BlueprintPure, Category = "Ship")
    bool IsPowerOverloaded() const;

    /**
     * Power allocation. The subsystem is authoritative and every screen,
     * light and booster *asks* -- nothing stores a copy, which is what makes
     * two screens showing the same allocation unable to disagree.
     */
    UFUNCTION(BlueprintPure, Category = "Power")
    TArray<FName> GetPowerConsumers() const;

    /** The player's preference for this consumer. No scale, no correct
     *  value: it only means anything against the other weights. */
    UFUNCTION(BlueprintPure, Category = "Power")
    float GetConsumerWeight(FName ConsumerId) const;

    UFUNCTION(BlueprintCallable, Category = "Power")
    void SetConsumerWeight(FName ConsumerId, float Weight);

    UFUNCTION(BlueprintPure, Category = "Power")
    float GetConsumerShare(FName ConsumerId) const;

    UFUNCTION(BlueprintPure, Category = "Power")
    float GetConsumerWant(FName ConsumerId) const;

    /** 0..1. How well this consumer is fed, and therefore how well it works. */
    UFUNCTION(BlueprintPure, Category = "Power")
    float GetConsumerSatisfaction(FName ConsumerId) const;

    /**
     * The engineering console's switch. Lights off means the lights want
     * nothing and take part in no split, so the power genuinely goes
     * elsewhere -- and the ship is genuinely dark.
     */
    UFUNCTION(BlueprintPure, Category = "Power")
    bool AreLightsOn() const;

    UFUNCTION(BlueprintCallable, Category = "Power")
    void SetLightsOn(bool bOn);

    /** 0..1, wound up by the engine at a rate its allocation scales. */
    UFUNCTION(BlueprintPure, Category = "Flight")
    float GetJumpCharge() const;

    /**
     * How hard the ship pushes right now, cm/s^2. Boosters on a thin
     * allocation push softer; they never stop pushing.
     */
    UFUNCTION(BlueprintPure, Category = "Flight")
    float GetLinearAcceleration() const;

    /**
     * Pilot mode. The pilot seat reports who sits at the helm; anything that
     * cares whether the ship is being flown asks here rather than reaching
     * into the seat or the character.
     */
    UFUNCTION(BlueprintCallable, Category = "Ship")
    void SetPilot(APawn* NewPilot);

    UFUNCTION(BlueprintCallable, Category = "Ship")
    void ClearPilot();

    UFUNCTION(BlueprintPure, Category = "Ship")
    bool IsPiloted() const;

    UFUNCTION(BlueprintPure, Category = "Ship")
    APawn* GetPilot() const;

    /**
     * Flight. Ignored unless Commander is the current pilot; returns false if
     * refused. The gate lives here rather than in the character because this
     * is the only place that can answer "who is flying the ship"
     * authoritatively -- and because a named pawn commanding through one gated
     * function is already the shape of a client-to-server RPC, should netcode
     * ever arrive.
     *
     * The absolute setter: the cruise lever to Throttle and the attitude to
     * AttitudeRate, the drive lever and the mode left as they are. Tests and
     * tools; the helm itself goes through SetHelmInput. Refused in transit,
     * where the helm is inert and the levers do not move.
     */
    UFUNCTION(BlueprintCallable, Category = "Flight")
    bool SetFlightCommand(APawn* Commander, float Throttle, FVector AttitudeRate);

    /**
     * The helm, once a frame: the attitude goes straight into the command,
     * and the lever keys wait for this subsystem's tick, which moves
     * whichever lever is live -- a press one notch of the drive from what the
     * ship is doing (ShipDriveLever::TapUp and TapDown against the eased
     * position), a hold repeating after a moment at ds.Drive.Sweep; or
     * cruise swept at ds.Cruise.Sweep with its detent at zero. Presses
     * accumulate until the tick spends them, so none is lost however the
     * frames fall. Pilot-gated; refused in transit, where the ship drops
     * lever input as it drops attitude.
     */
    bool SetHelmInput(APawn* Commander, const FHelmInput& Input);

    /**
     * All stop (X): both levers to STOP at once, whichever is live, so the
     * ship comes to rest in a known few seconds and stays at rest if F is
     * pressed afterwards. A key still held from before the stop moves
     * nothing until it is let go: the next speed after a stop is a new
     * choice. Pilot-gated; refused in transit.
     */
    bool AllStop(APawn* Commander);

    /** The drive lever to Notch, absolute, clamped to the lever: for tests
     *  and tools, as SetFlightCommand is for cruise. Pilot-gated; refused in
     *  transit. */
    bool SetDriveLever(APawn* Commander, int32 Notch);

    /**
     * How low the ship may go over Body, cm (flight-feel decision 6): over a
     * planet or moon the larger of ds.Flight.Floor and the sky's own rendered
     * floor, SkyProjection::RenderedFloor, below which a proxy stops growing
     * and the picture stops being true -- 10.2 km over an Earth, 112 km over
     * a Jupiter; over a star ds.Flight.StarFloorRadii of its radius, where
     * its disc fills 60 degrees and the rest of the sky is still there.
     *
     * The one function that answers it. Landing, when it comes, replaces or
     * lowers this as it takes over drawing the ground; the in-system jump
     * asks it for its guard. Read at use, like every tunable here.
     */
    static double FloorFor(const FSkyBody& Body);

    /** How far inside the system's edge the ship stops, cm: ds.Flight.Floor. */
    static double EdgeFloor();

    UFUNCTION(BlueprintPure, Category = "Flight")
    FVector GetShipVelocity() const;

    UFUNCTION(BlueprintPure, Category = "Flight")
    float GetShipSpeed() const;

    UFUNCTION(BlueprintPure, Category = "Flight")
    FTransform GetCounterFrameTransform() const;

    /** THE conversion, for everything outside the hull. C++ only: a universe
     *  position is a chunked value type (ADR 0007), not a Blueprint type. */
    FVector UniverseToWorld(const FUniversePosition& UniversePosition) const;

    /** Placing the ship without flying there: level setup and tests. Not a
     *  gameplay path -- flight goes through SetFlightCommand and the tick. */
    void PlaceShip(const FUniversePosition& NewPosition, const FQuat& NewOrientation);

    /** Read-only. There is no non-const accessor: the only write paths are
     *  SetFlightCommand, SetHelmInput, AllStop, SetDriveLever,
     *  SetDriveEngaged, ClearPilot and this subsystem's own tick -- which is
     *  where the jump's JumpTo happens -- and that is what makes the state
     *  trustworthy. */
    const FShipFlightState& GetFlightState() const;

    // -- the drive: the in-system lever at the helm (flight-feel spec) -------
    //
    // Two levers, and two words that never swap (plan conflict 7): "the
    // drive" crosses a system on its own lever, "the jump" folds between
    // stars. All C++ only, so nothing a Blueprint inherits changes.

    /**
     * F: which lever is live. Gated on the pilot exactly as SetFlightCommand
     * is; false if refused, and refused in transit. Each lever keeps its
     * setting across F in both directions, and across the pilot standing up,
     * so an approach can be set and watched from the galley and a look round
     * in cruise costs nothing to come back from (decision 1).
     */
    bool SetDriveEngaged(APawn* Commander, bool bOn);
    bool IsDriveEngaged() const;

    // -- the jump: course, heading, engage; it fires by itself ---------------

    /**
     * What the chart shows: every system within ds.Nav.RangeLy of the ship,
     * nearest first, without the one the ship is in. Asked of the universe
     * every call and stored nowhere.
     */
    TArray<FStarSystemStub> GetChart() const;

    /** False in transit, for the system the ship is in, and for an id that
     *  names no system. The course is the one piece of universe data the
     *  ship holds, the way it holds a throttle setting. Replaces a course to
     *  a world: one course, the latest choice. */
    bool PlotCourse(const FSystemId& Id);

    /**
     * The in-system jump's course (system map decision 12): the target, as
     * the map's "Jump here" and ds.Nav.Plot target plot it. False in
     * transit, with no target, for a target that names no world of the
     * system the ship is in, and while the ship is near enough to fly
     * (IsNearEnoughToFly). Replaces a star course. Not pilot-gated, like
     * every plot.
     */
    bool PlotTarget();

    /** Clears either course, and also stands the jump down. Ignored in
     *  transit. */
    void ClearCourse();

    /** The star course; empty for a world course, so a reader of star
     *  courses reads what it always did. */
    TOptional<FSystemId> GetPlottedSystem() const;

    /** The world course, which while set is always the target. */
    TOptional<FBodyId> GetPlottedWorld() const;

    /** A course of either kind: what engaging needs. */
    bool HasCourse() const;

    /** False in transit, and engaging with no course. Not pilot-gated: the
     *  chart chair engages, and nobody need be at the helm for it. */
    bool SetJumpEngaged(bool bOn);
    bool IsJumpEngaged() const;

    /** A word, never a number (EJumpState). */
    EJumpState GetJumpState() const;

    /** Bumps on every arrival, in the same tick as the ship lands, before any
     *  actor ticks. LocalSystem::Serial: a cache key for the sky and the
     *  counter-frame, never an answer. */
    int32 GetJumpSerial() const;

    bool IsInTransit() const;

    /** 0..1 through the transit, 0 outside it. For the streaks, never for a
     *  screen. */
    double GetTransitProgress() const;

    /** Unit, universe axes, from the ship toward the plotted star, or the
     *  plotted world's centre, through FUniversePosition::operator-. Empty
     *  with no course. The jump's cone, the HUD's jump line and caret and
     *  the counter-frame's course point all follow it, so a world course
     *  needs nothing of theirs. */
    TOptional<FVector> GetCourseDirection() const;

    /** The same in ship axes, +X the nose: what the helm's bearing words and
     *  the alignment cone are measured in. */
    TOptional<FVector> GetCourseDirectionShipLocal() const;

    /** The alignment cone's half-angle, radians, from ds.Nav.ConeDeg: the
     *  jump and the HUD's "dead ahead" ask the same number. */
    double GetJumpConeRadians() const;

    /** Watts the engine asks for while the jump winds, ds.Nav.WindingWant as
     *  tuned now; never negative. What the hum measures the engine's share
     *  against (plan conflict 8), asked here rather than of the console by
     *  name, so the one tunable has one reader and no per-frame lookup. */
    static float GetWindingWant();

    /** How far the chart reaches, light years: ds.Nav.RangeLy as tuned now,
     *  never negative. GetChart's radius, for anything that must know when
     *  the chart's answer can have changed. */
    static float GetChartRangeLy();

    bool HasVisited(const FSystemId& Id) const;

    // -- the target: the world in this system the pilot has marked ----------
    //
    // System map decisions 4 and 5. An id, never a copy: its position,
    // radius, name and kind are procgen's, asked every time (ADR 0003). Not
    // pilot-gated -- anyone at the map picks, and the ship has one target --
    // and not the course: the course is where the jump folds to, a star or,
    // since ruling 1, the target itself.

    /** False in transit, for a body not in the system the ship is in (asked
     *  of its position), for an orbit that system does not have, and for a
     *  moon, which procgen does not make yet. Replaces the target, and lets
     *  a world course to the old one go. */
    bool SetTarget(const FBodyId& Id);

    /** Lets the target go, and a world course with it. Ignored in transit. */
    void ClearTarget();

    /** As held. Resolve it against the system in hand (ShipNav::TargetPlanet)
     *  before drawing it: a PlaceShip into another system leaves an id that
     *  names nothing here, and that must draw nothing rather than the wrong
     *  world. */
    TOptional<FBodyId> GetTarget() const;

    /** Tab on the zoomed map (decision 13): the next world outward
     *  (ShipNav::NextTarget), through SetTarget, as a click would. Never
     *  clears. False with no world to go to, and in transit. */
    bool CycleTarget();

    /**
     * The target as seen from the ship now (TargetMarker::View), against
     * Here, the system the caller has in hand -- the map's drawing, the
     * HUD's frame -- so nothing is generated for it. Everything the view
     * needs of the ship is filled in here, once: position, attitude,
     * velocity, the world's floor (FloorFor), the boosters' braking and
     * ds.Drive.HoldSeconds. The HUD's line and the map's are this, printed,
     * and so can never disagree. Empty with no target, one that names
     * nothing in Here, and in transit.
     */
    TOptional<FTargetView> GetTargetView(const FStarSystem& Here) const;

    /** Whether the ship is within NavStart::WorldReachFactor standoffs of
     *  World's centre, of the system Here: near enough to fly, so an
     *  in-system jump to it is refused. False for an id that names nothing
     *  in Here. */
    bool IsNearEnoughToFly(const FStarSystem& Here, const FBodyId& World) const;

    /** ds.Nav.StandoffAU as tuned now, never negative: the interstellar
     *  arrival's standoff from a Sun-like star, and the map's rim is fitted
     *  to it (NavStart::ArrivalStandoffAU), as GetChartRangeLy is the
     *  chart's. */
    static float GetStandoffAU();

    /** ds.Nav.WorldStandoffDeg as tuned now: how wide an in-system jump
     *  meets its world, degrees. */
    static double GetWorldStandoffDeg();

private:
    FShipPowerState PowerState;
    FShipFlightState FlightState;

    /** Weak: the subsystem must not keep a pawn alive. */
    TWeakObjectPtr<APawn> Pilot;

    UPROPERTY()
    TArray<TObjectPtr<UShipModuleDataAsset>> InstalledModules;

    /** Decides; holds no current system. */
    FShipNavState NavState;

    /** Applies this frame's allocation to the things it drives. Lights are
     *  the lighting subsystem's job; these are the ones that live here. */
    void ApplyAllocation(float DeltaSeconds);

    /** The flight law's input: every body in the system here and its edge,
     *  as floor spheres (FloorFor), from LocalSystem::Here once a frame --
     *  never Current, whose neighbours are never surfaces. None in transit. */
    void UpdateSurfaces();

    /** Moves the live lever from what the helm handed over since the last
     *  tick, then spends the presses. */
    void ApplyHelm(float DeltaSeconds);

    /** True for the pilot, outside transit: every helm write asks this. */
    bool MayCommand(const APawn* Commander) const;

    /** The helm's hands, as last handed over; presses accumulate. */
    FHelmInput Helm;

    /** The drive lever's hold repeat, one per key. */
    ShipDriveLever::FNotchRepeat UpRepeat;
    ShipDriveLever::FNotchRepeat DownRepeat;

    /** Set by AllStop against a key held through it; cleared when that key
     *  is let go. A held key never undoes a stop. */
    bool bUpHoldSpent = false;
    bool bDownHoldSpent = false;

    /** Set by SetPilot for a new pilot; the first hands it hands over mark
     *  whatever they already hold as spent. Shift is sprint as well as the
     *  lever, so a player who runs to the helm sits down holding it, and a
     *  key held from before sitting down is not a press at the helm. */
    bool bAwaitingFirstHands = false;

    /** NavState.Step, then act on what it asks for. */
    void StepNavigation(float DeltaSeconds);

    /** A world as the in-system jump needs it: where, how big, its floor,
     *  and every other body's floor sphere. */
    struct FWorldFix
    {
        FUniversePosition Centre;
        double Radius = 0.0;
        double Floor = 0.0;
        TArray<FFlightSurface> Others;
    };

    /** World, resolved in Here; empty for an id that names nothing there. */
    static TOptional<FWorldFix> FixWorld(const FStarSystem& Here, const FBodyId& World);

    /** The same, generating the system the id names. */
    TOptional<FWorldFix> FixWorld(const FBodyId& World) const;

    /** Lets a world course go once the ship is near enough to fly, or its
     *  world names nothing where the ship now is, with the charge unspent:
     *  it is as if the ship had arrived. Never in transit. */
    void LetGoOfNearWorldCourse();

    /** Where an in-system jump's fold opened: its arrival is on the line
     *  from here to the world. Taken when the fold opens, because the ship
     *  still coasts through the fold, and at 1 c that is a sizeable part of
     *  the world's standoff. Empty outside an in-system fold. */
    TOptional<FUniversePosition> FoldDeparture;

    /** The fold's draw off the top, while the jump winds (ds.Nav.FoldDraw). */
    void SetFoldDraw(float Watts);

    /** The id of the system the ship is in, asked of its position. */
    TOptional<FSystemId> SystemHere() const;

    const UUniverseSubsystem* Universe() const;

    /** What SetFoldDraw last put on the reactor; 0 is none. */
    float FoldDrawWatts = 0.0f;

    bool bLightsOn = true;

    /**
     * Placeholder reactor rating. Becomes a module later. Sized so the stock
     * ship is whole at rest: its modules (620 W) plus the lights (300) and
     * the boosters (450) come to 1370 W, so at the default split nothing is
     * dimmed while nothing is being asked of the ship. The split bites when
     * the jump winds or a module is added -- the first playtest found the
     * lights at 63% on a quiet ship under the old 1000 W, which read as
     * broken rather than strained (developer's ruling, 2026-09-26).
     * DeepSpace.Ship.JumpCanWindAtFullSpeed holds it.
     */
    static constexpr float DefaultReactorOutput = 1400.0f;

    /**
     * What each consumer would use given everything it asked for. They sum
     * to more than the reactor makes, deliberately: if everything could be
     * fed at once the split would never be a choice, and a choice with no
     * cost is not one.
     *
     * The engine is not here: it wants ds.Nav.WindingWant while the jump
     * winds and nothing otherwise, so an idle drive costs the ship nothing
     * and staying put is never taxed (nav decision 4). Because an idle want
     * of zero reads as full satisfaction, anything that wants to follow the
     * winding -- the hum, in slice 2 -- reads watts delivered,
     * GetConsumerShare(ShipPower::Engine) over ds.Nav.WindingWant, and never
     * satisfaction (plan conflict 8).
     */
    static constexpr float LightsWant = 300.0f;
    static constexpr float BoostersWant = 450.0f;

    /**
     * How hard a completely starved set of boosters still pushes, as a
     * fraction. Not zero: a ship that cannot move is a failure state, and
     * the whole model is that systems degrade instead of failing. A quarter
     * thrust is unmistakably sluggish and still gets you home.
     */
    static constexpr float StarvedBoosterThrust = 0.25f;
};
