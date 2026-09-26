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
     */
    UFUNCTION(BlueprintCallable, Category = "Flight")
    bool SetFlightCommand(APawn* Commander, float Throttle, FVector AttitudeRate);

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
     *  SetFlightCommand, SetDriveEngaged, ClearPilot and this subsystem's own
     *  tick -- which is where the jump's JumpTo happens -- and that is what
     *  makes the state trustworthy. */
    const FShipFlightState& GetFlightState() const;

    // -- the drive: the in-system lever at the helm (sky decision 8) --------
    //
    // Two levers, and two words that never swap (plan conflict 7): "the
    // drive" closes on what is near, "the jump" folds between stars. All C++
    // only, so nothing a Blueprint inherits changes.

    /**
     * Gated on the pilot exactly as SetFlightCommand is; false if refused.
     * A lever, like the throttle: it persists when the pilot stands up, so an
     * approach can be set and watched from the galley.
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
     *  ship holds, the way it holds a throttle setting. */
    bool PlotCourse(const FSystemId& Id);

    /** Also stands the jump down. Ignored in transit. */
    void ClearCourse();

    TOptional<FSystemId> GetPlottedSystem() const;

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

    /** Unit, universe axes, from the ship toward the plotted star, through
     *  FUniversePosition::operator-. Empty with no course. */
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

    bool HasVisited(const FSystemId& Id) const;

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

    /** The drive's input: the nearest surface, from LocalSystem, read once. */
    void UpdateDriveRoom();

    /** NavState.Step, then act on what it asks for. */
    void StepNavigation(float DeltaSeconds);

    /** The fold's draw off the top, while the jump winds (ds.Nav.FoldDraw). */
    void SetFoldDraw(float Watts);

    /** The id of the system the ship is in, asked of its position. */
    TOptional<FSystemId> SystemHere() const;

    const UUniverseSubsystem* Universe() const;

    /** What SetFoldDraw last put on the reactor; 0 is none. */
    float FoldDrawWatts = 0.0f;

    bool bLightsOn = true;

    /** Placeholder reactor rating for milestone 1. Becomes a module later. */
    static constexpr float DefaultReactorOutput = 1000.0f;

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
