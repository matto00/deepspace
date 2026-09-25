#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShipLightingSubsystem.generated.h"

class UPointLightComponent;

/**
 * Makes the ship's lights answer to its power allocation.
 *
 * Lights are found by the actor tag `Power.Lights`, which is the same string
 * as the consumer they belong to (ShipPower::Lights) and is applied by
 * Tools/build_hauler.py. Never by name, never by index: names are for humans
 * and indices change whenever the layout does, but a tag is a contract the
 * generator can keep -- and it is how all generated geometry will be
 * addressed once proc-gen moves into C++ (ADR 0006).
 *
 * The lights degrade and never fail. Under-fed they dim; well under-fed they
 * warm and flicker, like a fixture browning out, rather than switching off.
 * There is no alarm and no threshold that breaks anything: a dark ship is a
 * thing the player reads instantly and fixes if they care to.
 */
UCLASS()
class DEEPSPACE_API UShipLightingSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    /**
     * Re-finds the tagged lights and re-reads their rated intensity.
     *
     * Called once when play begins. Public because a test spawns its lights
     * itself, and because the runtime generator will one day build the ship
     * after this subsystem already exists.
     */
    UFUNCTION(BlueprintCallable, Category = "Lighting")
    void Refresh();

    UFUNCTION(BlueprintPure, Category = "Lighting")
    int32 GetLightCount() const;

    /** Below this satisfaction the lights warm and start to flicker. */
    static constexpr float BrownOutBelow = 1.0f / 3.0f;

    /**
     * How dim a completely starved light gets, as a fraction of its rating.
     * Not zero: the fiction is a fixture browning out, and a light that
     * switches itself off is a failure state wearing a dimmer's clothes.
     */
    static constexpr float StarvedGlow = 0.06f;

private:
    struct FShipLight
    {
        TWeakObjectPtr<UPointLightComponent> Light;

        /** The fixture's rating, from the level. Not ship state: this is
         *  what the lamp *is*, and the allocation scales it. */
        float RatedIntensity = 0.0f;
        FLinearColor RatedColour = FLinearColor::White;
    };

    TArray<FShipLight> Lights;

    /** Advances with time; the flicker is a function of it, not of random
     *  draws, so the same allocation always looks the same way. */
    float Phase = 0.0f;
};
