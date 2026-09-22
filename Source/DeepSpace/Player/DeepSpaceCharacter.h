#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Player/Posture.h"
#include "DeepSpaceCharacter.generated.h"

class APilotSeat;
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UInteractableComponent;
class UUserWidget;
struct FInputActionValue;

/**
 * First-person pawn with a body. Owns movement (walk, sprint, crouch), the
 * camera -- which rides the body's head -- the trace that finds the
 * interactable the player is looking at, and sitting in the pilot seat.
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

    /** Prompt for the focused interactable, "Stand up" while seated, or empty. */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    FText GetCurrentPrompt() const;

    /** What the body is doing. Decided here; the animation only reads it. */
    UFUNCTION(BlueprintPure, Category = "Movement")
    EPosture GetPosture() const;

    /** Sit at the helm: movement off, camera limited, the ship piloted. */
    void SitIn(APilotSeat* NewSeat);

    /** Leave the seat, returning to the seat's exit point. */
    void StandUp();

    bool IsSeated() const { return Seat != nullptr; }

    /**
     * Sets the body up to be seen from inside: hides the head the camera sits
     * in. Called from BeginPlay; public so a test can exercise exactly what
     * the game does.
     */
    void ConfigureFirstPersonBody();

protected:
    virtual void BeginPlay() override;

    /**
     * Holds the camera at the head bone. A zero-length spring arm rather than
     * attaching the camera directly, for its lag: the camera follows the
     * head's position through running bob, crouching and sitting, smoothed so
     * the bob is not nauseating. Rotation stays under the mouse. Tune
     * CameraLagSpeed and SocketOffset in the Blueprint.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USpringArmComponent> CameraArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    /** Bone the camera rides, hidden from the player's own view. */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    FName HeadBone = TEXT("head");

    /** How far the player can reach, in centimetres. */
    UPROPERTY(EditDefaultsOnly, Category = "Interaction")
    float InteractionRange = 250.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    /**
     * Mouse look ships as a second context in the First Person template:
     * IMC_Default binds IA_Look to Gamepad_Right2D only, and Mouse2D lives in
     * IMC_MouseLook driving its own action. Both must be added or the mouse
     * does nothing while a gamepad works fine.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> MouseLookMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> LookAction;

    /** Mouse equivalent of LookAction; both drive the same handler. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> MouseLookAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> InteractAction;

    /** Held to sprint. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> SprintAction;

    /** Pressed to toggle crouch. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> CrouchAction;

    /** How far the view may turn from the seat's facing while seated, degrees. */
    UPROPERTY(EditDefaultsOnly, Category = "Seat")
    float SeatedYawLimit = 100.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Seat")
    float SeatedPitchLimit = 70.0f;

    /**
     * Widget shown for the whole session; it reads GetCurrentPrompt() itself
     * rather than being pushed text, so there is one source of truth.
     */
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UUserWidget> HUDWidgetClass;

private:
    void Move(const FInputActionValue& Value);
    void StopMoving(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void TryInteract();
    void StartSprinting();
    void StopSprinting();
    void ToggleCrouch();

    /** Sets MaxWalkSpeed from the sprint request and the movement rules. */
    void UpdateWalkSpeed();

    /** Restricts or restores how far the camera may turn. */
    void SetViewLimits(bool bSeated, float SeatYaw);

    /** Re-runs the reach trace and updates FocusedInteractable. */
    void UpdateFocusedInteractable();

    UPROPERTY()
    TObjectPtr<UInteractableComponent> FocusedInteractable;

    UPROPERTY()
    TObjectPtr<UUserWidget> HUDWidget;

    /** The seat we are sitting in, or null. */
    UPROPERTY()
    TObjectPtr<APilotSeat> Seat;

    /** IA_Move's latest value, kept for the sprint rule; zero when released. */
    FVector2D MoveInput = FVector2D::ZeroVector;

    bool bWantsToSprint = false;
};
