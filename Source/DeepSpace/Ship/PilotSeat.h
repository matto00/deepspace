#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PilotSeat.generated.h"

class UBoxComponent;
class UInteractableComponent;
class USceneComponent;

/**
 * The helm. Sitting here puts the ship in pilot mode: the seat tells
 * UShipSubsystem who is piloting, and everything else asks the subsystem.
 *
 * Carries no mesh of its own: the level builder places it over the
 * generated pilot-seat prop. Its box exists to catch the player's reach trace
 * -- it envelops the prop, so the trace hits the seat rather than the
 * decorative chair beneath it, which has no interactable.
 */
UCLASS()
class DEEPSPACE_API APilotSeat : public AActor
{
    GENERATED_BODY()

public:
    APilotSeat();

    /** Where the seated character stands its feet, facing the way it sits. */
    FTransform GetSeatTransform() const;

    /** Where the character is put when it stands up. */
    FVector GetExitLocation() const;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<USceneComponent> Root;

    /** Blocks only the Visibility channel: the reach trace, not movement. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<UBoxComponent> ReachVolume;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<USceneComponent> SeatAnchor;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<USceneComponent> ExitPoint;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<UInteractableComponent> Interactable;

private:
    UFUNCTION()
    void HandleInteracted(AActor* InteractInstigator);
};
