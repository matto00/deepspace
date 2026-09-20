#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteracted, AActor*, Instigator);

/**
 * Makes any actor interactable. Deliberately a component rather than a base
 * class: doors, consoles and levers compose this instead of inheriting from
 * a hierarchy that would calcify as the ship grows.
 */
UCLASS(ClassGroup = (DeepSpace), meta = (BlueprintSpawnableComponent))
class DEEPSPACE_API UInteractableComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractableComponent();

    /** What the thing is called, e.g. "Engineering Console". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
    FText DisplayName;

    /** What interacting does, e.g. "Power on". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
    FText InteractionVerb;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
    bool bInteractionEnabled = true;

    /** Broadcast when this component is successfully interacted with. */
    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteracted OnInteracted;

    /** Player-facing prompt combining verb and name. */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    FText GetPrompt() const;

    UFUNCTION(BlueprintPure, Category = "Interaction")
    bool CanInteract() const;

    /** Broadcasts OnInteracted if interaction is currently allowed. */
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void Interact(AActor* Instigator);
};
