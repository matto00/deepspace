#include "Ship/InteractableComponent.h"

UInteractableComponent::UInteractableComponent()
{
    // Purely reactive: ticking every interactable in the ship is wasted work.
    PrimaryComponentTick.bCanEverTick = false;
}

FText UInteractableComponent::GetPrompt() const
{
    return FText::Format(
        NSLOCTEXT("DeepSpace", "InteractPrompt", "{0}  {1}"),
        InteractionVerb,
        DisplayName);
}

bool UInteractableComponent::CanInteract() const
{
    return bInteractionEnabled;
}

void UInteractableComponent::Interact(AActor* Instigator)
{
    if (!CanInteract())
    {
        return;
    }
    OnInteracted.Broadcast(Instigator);
}
