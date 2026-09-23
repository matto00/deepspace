#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipPowerState.h"
#include "ShipSubsystem.generated.h"

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

    /** Placeholder reactor rating for milestone 1. Becomes a module later. */
    static constexpr float DefaultReactorOutput = 1000.0f;
};
