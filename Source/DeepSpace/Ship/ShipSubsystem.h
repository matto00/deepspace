#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Ship/ShipPowerState.h"
#include "ShipSubsystem.generated.h"

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
class DEEPSPACE_API UShipSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Convenience accessor. Returns nullptr if there is no world. */
    static UShipSubsystem* Get(const UObject* WorldContext);

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

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

private:
    FShipPowerState PowerState;

    UPROPERTY()
    TArray<TObjectPtr<UShipModuleDataAsset>> InstalledModules;

    /** Placeholder reactor rating for milestone 1. Becomes a module later. */
    static constexpr float DefaultReactorOutput = 1000.0f;
};
