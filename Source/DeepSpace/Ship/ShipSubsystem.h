#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipPowerState.h"
#include "ShipSubsystem.generated.h"

// ---------------------------------------------------------------------------
// WAVE 2 CONTRACT -- not yet code. The additions Wave 2 makes to this class,
// frozen in Wave 0 so that the sky, navigation and the hum can be written
// against them concurrently (docs/superpowers/plans/2026-09-25-poc-build-plan.md,
// conflicts 1, 3, 7, 9, 10). Two levers, and two words that never swap:
// **the drive** is the in-system one (sky decision 8), **the jump** is the
// fold between stars (nav decision 5). All of it is C++ only -- no UFUNCTION,
// so nothing a Blueprint inherits changes.
//
// Startup
//   Initialize               also Collection.InitializeDependency<UUniverseSubsystem>().
//   virtual void OnWorldBeginPlay(UWorld& InWorld) override;
//                            The one owner of where the ship starts (conflict 3):
//                            PlaceShip(NavStart::OpeningPlacement(start system)),
//                            the largest planet 40,000 km dead ahead, its star to
//                            starboard. Game worlds only; ds.Nav.PlaceAtStart 0
//                            turns it off.
//
// The jump -- course, heading, engage; it fires by itself (nav decision 2)
//   TArray<FStarSystemStub> GetChart() const;
//                            Asks UUniverseSubsystem every call: GetSystemsNear(ship,
//                            ds.Nav.RangeLy) nearest first, without the system the
//                            ship is in. Stored nowhere.
//   bool PlotCourse(const FSystemId& Id);
//                            False in transit, for the system the ship is in, or for
//                            an id with no system. The course is the one piece of
//                            universe data the ship holds.
//   void ClearCourse();
//   TOptional<FSystemId> GetPlottedSystem() const;
//   void SetJumpEngaged(bool bOn);        Ignored in transit. Not pilot-gated: the
//                                         chart chair engages.
//   bool IsJumpEngaged() const;
//   EJumpState GetJumpState() const;      EJumpState {Idle, Winding, Ready, Transit},
//                            declared in Ship/ShipNavState.h. Idle: not engaged.
//                            Winding: engaged, charge below 1. Ready: engaged and
//                            charged, holding for alignment as long as it takes.
//                            Transit: between stars. A word, never a number.
//   int32 GetJumpSerial() const;          Bumps on every arrival, in the same tick
//                                         as JumpTo. LocalSystem::Serial.
//   bool IsInTransit() const;             LocalSystem::InTransit.
//   double GetTransitProgress() const;    0..1 through the transit; 0 outside it.
//   TOptional<FVector> GetCourseDirectionShipLocal() const;
//                            Unit, ship axes (+X the nose), from the ship toward the
//                            plotted star, through FUniversePosition::operator-.
//                            Empty with no course. The HUD's bearing words and the
//                            nose caret read this.
//
// The drive -- a lever at the helm; closes a tenth of the room every 1.5 s
//   bool SetDriveEngaged(APawn* Commander, bool bOn);
//                            Gated on the pilot exactly as SetFlightCommand is;
//                            false if refused. Persists when the pilot stands up.
//   bool IsDriveEngaged() const;
//
// Private
//   FShipNavState NavState;               Decides; holds no current system.
//   void StepNavigation(float DeltaSeconds);
//                            NavState.Step, then act on its event. TransitBegan:
//                            FlightState.SpendJumpCharge(). Arrived:
//                            FlightState.JumpTo(star - Dir * Standoff) and nothing
//                            else -- a translation, never a turn; the answer to
//                            GetSystemAt changes by itself (conflict 1). Standoff =
//                            max(ds.Nav.StandoffAU * sqrt(L), 1.5 * outermost orbit)
//                            (conflict 9).
//   EngineWant goes: the engine wants ds.Nav.WindingWant while engaged and not
//   yet charged, and 0 otherwise; the charge winds only while engaged. The hum's
//   EngineFeed is GetConsumerShare(ShipPower::Engine) / ds.Nav.WindingWant.
//
// Tick becomes
//   ApplyAllocation(DeltaTime);   // engine want follows the jump; boosters stretch the drive's tau
//   FlightState.SetDriveRoom(InTransit ? 0 : LocalSystem::NearestSurfaceDistance(
//       LocalSystem::Current(World), ship position));
//   FlightState.Step(DeltaTime);
//   StepNavigation(DeltaTime);
//
// The flight-state side, for reference (Ship/ShipFlightState.h, same wave):
//   FShipFlightLimits::DriveTau = 15 s, DriveFloor = 1e7 cm; FShipFlightCommand::bDrive;
//   SetDriveRoom(double); ChargeJumpDrive(DeltaSeconds, Satisfaction,
//   SecondsFromCold = JumpChargeSeconds); SpendJumpCharge(); JumpTo(const FUniversePosition&),
//   the fourth write path, private to this subsystem and called from one place.
// ---------------------------------------------------------------------------

class APawn;
class UShipModuleDataAsset;

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

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

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
     *  SetFlightCommand, ClearPilot and this subsystem's own tick, which is
     *  what makes the state trustworthy. */
    const FShipFlightState& GetFlightState() const;

private:
    FShipPowerState PowerState;
    FShipFlightState FlightState;

    /** Weak: the subsystem must not keep a pawn alive. */
    TWeakObjectPtr<APawn> Pilot;

    UPROPERTY()
    TArray<TObjectPtr<UShipModuleDataAsset>> InstalledModules;

    /** Applies this frame's allocation to the things it drives. Lights are
     *  the lighting subsystem's job; these are the ones that live here. */
    void ApplyAllocation(float DeltaSeconds);

    bool bLightsOn = true;

    /** Placeholder reactor rating for milestone 1. Becomes a module later. */
    static constexpr float DefaultReactorOutput = 1000.0f;

    /**
     * What each consumer would use given everything it asked for. They sum
     * to more than the reactor makes, deliberately: if everything could be
     * fed at once the split would never be a choice, and a choice with no
     * cost is not one.
     */
    static constexpr float LightsWant = 300.0f;
    static constexpr float BoostersWant = 450.0f;
    static constexpr float EngineWant = 500.0f;

    /**
     * How hard a completely starved set of boosters still pushes, as a
     * fraction. Not zero: a ship that cannot move is a failure state, and
     * the whole model is that systems degrade instead of failing. A quarter
     * thrust is unmistakably sluggish and still gets you home.
     */
    static constexpr float StarvedBoosterThrust = 0.25f;
};
