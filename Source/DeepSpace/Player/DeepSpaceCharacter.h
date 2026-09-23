#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Player/Posture.h"
#include "DeepSpaceCharacter.generated.h"

class APilotSeat;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UInteractableComponent;
class UUserWidget;
struct FInputActionValue;

/**
 * First-person pawn with a body. Owns movement (walk, sprint, crouch), the
 * camera -- which rides the body's head, kept inside the capsule -- the trace
 * that finds the interactable the player is looking at, and sitting in the
 * pilot seat.
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

    /**
     * Puts the camera where the eyes are, for the view rotation given. Called
     * every frame from Tick with GetViewRotation(); the rotation is a
     * parameter so a test can look straight down without a controller.
     */
    void PlaceCamera(float DeltaSeconds, const FRotator& ViewRotation);

    /** Where the eyes are in the world. */
    FVector GetEyeLocation() const;

protected:
    virtual void BeginPlay() override;

    /**
     * The view. Hangs off the capsule, not the head bone: PlaceCamera puts it
     * at the head every frame, but only after checking the head has not
     * carried it out of the ship. Rotation comes from the controller.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    /** Bone the camera rides, hidden from the player's own view. */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    FName HeadBone = TEXT("head");

    /**
     * How far above the head bone the eyes sit, cm. The bone is at the base
     * of the skull. Tools/check_anim_heights.py adds this to every clip's
     * head height, so a bigger raise must still fit the crouched capsule.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float EyeHeightAboveHead = 6.0f;

    /**
     * How far forward of the head bone the eyes sit, cm, so the view is not
     * from inside the skull. Applied in the view's *yaw* only: swung with
     * pitch it would dive into the neck as the player looked down, and the
     * body would be seen from the inside.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float EyeForwardOffset = 8.0f;

    /**
     * Radius of the sphere swept from the capsule's axis out to the eyes, cm.
     * Bigger than the 10 cm near clip plane, so whatever the camera stops
     * against is still drawn rather than clipped away.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float EyeProbeRadius = 12.0f;

    /**
     * How fast the eye height follows the head's, per second; 0 disables the
     * smoothing. Running bob is vertical and raw it is nauseating, so the
     * height is damped. The lean is horizontal and stays rigid: damping that
     * is what let the body run ahead of the camera and come into frame.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float EyeHeightLagSpeed = 12.0f;

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

    /** Eye height above the actor's origin, damped; see EyeHeightLagSpeed. */
    float DampedEyeHeight = 0.0f;

    /** False until DampedEyeHeight has a real value to damp towards. */
    bool bEyeHeightSettled = false;

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
