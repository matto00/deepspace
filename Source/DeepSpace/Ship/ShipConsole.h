#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipConsole.generated.h"

class UInteractableComponent;
class UStaticMeshComponent;

/**
 * An engineering console. Reads power figures from UShipSubsystem on demand
 * and never stores them — the discipline that keeps ship state from
 * scattering across actors.
 */
UCLASS()
class DEEPSPACE_API AShipConsole : public AActor
{
    GENERATED_BODY()

public:
    AShipConsole();

    /** Text for the screen. Queries the subsystem fresh on every call. */
    UFUNCTION(BlueprintPure, Category = "Console")
    FText GetReadout() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    bool bIsPowered = false;

    /** Implemented in Blueprint to update the screen material. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Console")
    void OnReadoutChanged();

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    TObjectPtr<UInteractableComponent> Interactable;

private:
    UFUNCTION()
    void HandleInteracted(AActor* InteractInstigator);
};
