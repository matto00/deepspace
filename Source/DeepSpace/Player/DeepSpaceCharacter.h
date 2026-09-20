#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DeepSpaceCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UInteractableComponent;
struct FInputActionValue;

/**
 * First-person pawn. Owns movement, the camera, and the trace that finds the
 * interactable the player is looking at.
 */
UCLASS()
class DEEPSPACE_API ADeepSpaceCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ADeepSpaceCharacter();

    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    /** The interactable currently under the crosshair, or nullptr. */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    UInteractableComponent* GetFocusedInteractable() const;

    /** Prompt for the focused interactable, or empty text if there is none. */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    FText GetCurrentPrompt() const;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    /** How far the player can reach, in centimetres. */
    UPROPERTY(EditDefaultsOnly, Category = "Interaction")
    float InteractionRange = 250.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> InteractAction;

private:
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void TryInteract();

    /** Re-runs the reach trace and updates FocusedInteractable. */
    void UpdateFocusedInteractable();

    UPROPERTY()
    TObjectPtr<UInteractableComponent> FocusedInteractable;
};
