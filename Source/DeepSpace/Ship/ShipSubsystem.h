#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipParts.h"
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
    /** -1..1 about each body axis: X roll, Y pitch, Z yaw
     *  (FShipFlightCommand::AttitudeRate). */
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
UCLASS(Config = Game)
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

    /** Every fitted part, in bay order: a read-only view, for tests. The
     *  loadout itself is GetLoadoutState. */
    TArray<UShipModuleDataAsset*> GetInstalledModules() const;

    // -- parts: one per bay (the wear and upgrades spec) -----------------------

    /**
     * Fits a new Part into the bay it declares -- an aux part into the first
     * free auxiliary slot, or Aux1 when both are full -- and the part it
     * displaces joins the spares, so every swap can be undone (decision 1).
     * The supply and the lights' want follow at once; the boosters' want on
     * the next ApplyAllocation, its one writer (sign-off 29).
     *
     * True with nothing changed when the bay already holds this part, and
     * when an aux part is already in the other slot (one of a kind). False,
     * and nothing changed, for null, a part whose Bay is None, a part with no
     * id (None in a bay means empty), and a different asset under an id this
     * ship already knows.
     */
    bool FitPart(UShipModuleDataAsset* Part);

    /** An auxiliary slot's part to the spares. Refused for a core bay: a core
     *  part is only ever swapped, so there is never a frame without a
     *  reactor (decision 3). */
    bool RemovePart(EShipBay Bay);

    /** The catalogue's part with this id, loaded on first ask through the
     *  CatalogueAsset ini line (decision 5); null for an id it does not
     *  hold. Works in a bare world with no game mode and no asset scan. */
    UShipModuleDataAsset* FindPart(FName PartId) const;

    /** Every part the catalogue holds, in its order. */
    TArray<UShipModuleDataAsset*> GetCatalogue() const;

    /**
     * ds.Ship.Install's path (decision 10): the first spare with this id,
     * fitted as it is, else a new one from the catalogue (FitPart). True with
     * nothing changed when the bay already holds it and no spare of it is
     * aboard; false for an id that names no part.
     */
    bool FitPartById(FName PartId);

    /** The part in Bay; null for an empty bay, which reads the stock part's
     *  ratings and draws nothing. */
    UShipModuleDataAsset* GetFittedPart(EShipBay Bay) const;

    /** Every bay and every spare, plain and serialisable (decision 11): what
     *  the save will write. Ask rather than keep a copy. */
    const FShipLoadoutState& GetLoadoutState() const;

    /** The spares aboard, each one particular part. */
    const TArray<FShipPartState>& GetSpares() const;

    /** A new spare of the part with this id, aboard (ds.Ship.Spares give).
     *  False for an id that names no part. */
    bool AddSpare(FName PartId);

    /** No spares aboard (ds.Ship.Spares clear). */
    void ClearSpares();

    /**
     * Sets the whole loadout from State (decision 11), as slice 3's save
     * will. Bays are found by name, never position. Whatever State cannot
     * name falls back and is counted:
     * - an unknown part, or a part in the wrong bay: the bay's stock part;
     * - a core bay State lacks: its stock part;
     * - a bay this ship has not got: ignored;
     * - a bay named twice: the first entry is restored, the rest ignored;
     * - an aux part in both aux slots: the first slot keeps it, the second
     *   is left empty (one of a kind, decision 8);
     * - a spare with an unknown id: dropped.
     * Returns how many entries fell back, each also logged by name. State
     * may be this ship's own GetLoadoutState(): it is copied first.
     */
    int32 RestoreLoadout(const FShipLoadoutState& State);

    /**
     * A seam for tests, not a part (decision 8): a standing draw off the top
     * under "Load.<Name>", replaced if Name already draws. No console
     * command, no nameplate, no save -- as ds.Nav.FoldDraw books a draw
     * that is not a part. The hog tests starve the ship through it, because
     * a standing draw is exactly what an aux part may not have.
     */
    void AddLoad(FName Name, float Watts);

    /** False if Name was not drawing. */
    bool RemoveLoad(FName Name);

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

    /** Watts the boosters want to hold the ship against gravity: 150 W a g
     *  under a solid world's drive floor, airborne, and nothing anywhere a
     *  ship can be parked (landing decision 5). */
    float GetHoldWant() const;

    /** Watts actually reaching the hold, paid first inside the boosters'
     *  share: what the hum's hold term reads, never satisfaction. */
    float GetHoldWatts() const;

    /** ds.Boosters.HoldWatts as read now, never below 0: watts per g of the
     *  hold's want. The hum's hold term divides by it. */
    static float GetHoldWattsPerG();

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
     * The one function that answers it; the in-system jump asks it for its
     * guard. Read at use, like every tunable here.
     *
     * Over a solid world (landing decision 10) that floor is taken above the
     * world's highest peak, WorldRelief's MaxHeightCm: 10.2 km over a flat
     * Earth, up to about 20 km over one at the 10 km cap, so no summit is
     * ever within 10 km of the drive. It is the drive's floor only: cruise
     * and the vertical lever read the ground below it (FFlightSurface::Ground).
     */
    static double FloorFor(const FSkyBody& Body);

    /** How far inside the system's edge the ship stops, cm: ds.Flight.Floor. */
    static double EdgeFloor();

    /** The ship's origin over flat ground at rest, cm: ds.Land.GearClearance,
     *  read at use. Cruise's floor over a solid world is the ground plus this. */
    static double GearClearance();

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

    /** The ship's rated values: every fitted part's ratings over the stock
     *  part's (wear and upgrades decision 2), derived on every call and
     *  stored nowhere. An empty bay reads stock (decision 3). */
    FShipRatings GetRatings() const;

    /** Watts the engine asks for while the jump winds: the drive part's
     *  WindingWant, or ds.Nav.WindingWant when that is 0 or more (decision
     *  6); never negative. What the hum measures the engine's share against
     *  (plan conflict 8), asked here so the one number has one reader. */
    float GetWindingWant() const;

    /** Seconds for a full charge from cold at full feed: the drive part's
     *  ChargeSeconds, or ds.Nav.ChargeSeconds when set. Never on a screen:
     *  no screen shows the jump's charge. */
    float GetChargeSeconds() const;

    /** Notches a second the drive's ease may move at full thrust: the drive
     *  part's DriveResponse, or ds.Drive.Response when set. */
    float GetDriveResponse() const;

    /** How far the chart reaches, light years: the sensors part's RangeLy, or
     *  ds.Nav.RangeLy when set. GetChart's radius, for anything that must know
     *  when the chart's answer can have changed. */
    float GetChartRangeLy() const;

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

    /** The fitted parts and the spares: the one truth about what is aboard,
     *  plain and serialisable (decision 11). Every bay is listed. */
    UPROPERTY()
    FShipLoadoutState Loadout;

    /** Every part asset this ship has fitted or been given, by id: what a
     *  PartId in Loadout resolves to. A part made in code resolves exactly
     *  as a catalogue part does, and nothing fitted is ever collected. */
    UPROPERTY()
    TMap<FName, TObjectPtr<UShipModuleDataAsset>> KnownParts;

    /** The catalogue, as DefaultGame.ini names it. Empty by default, on
     *  purpose: a section that fails to load finds nothing rather than the
     *  wrong catalogue. */
    UPROPERTY(Config)
    FSoftObjectPath CatalogueAsset;

    /** A part this ship already knows by id, else the catalogue's. */
    UShipModuleDataAsset* PartFor(FName PartId) const;

    /** Refuses null, a None bay, no id, and a different asset under a known
     *  id; otherwise remembers Part under its id. */
    bool Register(UShipModuleDataAsset* Part);

    /** Where Part goes: its core bay, or the aux slot already holding it,
     *  else the first free aux slot, else Aux1. */
    TOptional<EShipBay> SlotFor(const UShipModuleDataAsset& Part) const;

    /** Part into Bay; whatever was there becomes a spare. */
    void FitState(EShipBay Bay, const FShipPartState& Part);

    /** Part into Bay, its draw re-booked under the bay's key, the ratings
     *  pushed. The one place a bay changes. */
    void SetBayPart(EShipBay Bay, const FShipPartState& Part);

    /** The supply and the lights' want, from the ratings. Never the
     *  boosters' want: ApplyAllocation writes that. */
    void PushRatings();

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
        EGround Ground = EGround::None;
        FWorldReliefParams Relief;
        TArray<FFlightSurface> Others;
    };

    /** A solid body's ground for the flight, shared and kept while its relief
     *  is unchanged, so a frame does not allocate one per body. */
    FGroundFieldRef GroundFor(const FSkyBody& Body);

    struct FGroundCacheEntry
    {
        FWorldReliefParams Params;
        FGroundFieldRef Ground;
    };
    TMap<FName, FGroundCacheEntry> GroundCache;

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
     *  still coasts through the fold, and at 0.1 c that is a sizeable part of
     *  the world's standoff. Empty outside an in-system fold. */
    TOptional<FUniversePosition> FoldDeparture;

    /** The fold's draw off the top, while the jump winds (ds.Nav.FoldDraw). */
    void SetFoldDraw(float Watts);

    /** The id of the system the ship is in, asked of its position. */
    TOptional<FSystemId> SystemHere() const;

    const UUniverseSubsystem* Universe() const;

    /** What SetFoldDraw last put on the reactor; 0 is none. */
    float FoldDrawWatts = 0.0f;

    /** The hold's want, W: rewritten by ApplyAllocation only when it moves by
     *  more than a watt (landing decision 5). */
    float HoldWant = 0.0f;

    /** The boosters' split between the hold and the manoeuvre, this frame's. */
    ShipPower::FBoosterSplit LastSplit;

    bool bLightsOn = true;

    /**
     * How hard a completely starved set of boosters still pushes, as a
     * fraction. Not zero: a ship that cannot move is a failure state, and
     * the whole model is that systems degrade instead of failing. A quarter
     * thrust is unmistakably sluggish and still gets you home.
     */
    static constexpr float StarvedBoosterThrust = 0.25f;
};
