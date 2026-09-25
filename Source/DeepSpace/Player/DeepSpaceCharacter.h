#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Player/Posture.h"
#include "DeepSpaceCharacter.generated.h"

class APilotSeat;
class AShipScreen;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UInteractableComponent;
class UUserWidget;
class UWidgetInteractionComponent;
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

    /**
     * Sit down at a screen: the body takes a seat in front of it, the camera
     * frames it, and the mouse becomes a cursor that drives it.
     *
     * This is the one place a screen stops being something you aim at. The
     * spec's decision 1 kept every screen a surface in the room driven by the
     * view, and in play the laptop's sliders were too fiddly to aim at; the
     * answer was to sit the player down rather than to make it a menu. The
     * galley stays visible around it and nothing pauses -- see the spec's
     * second addendum.
     */
    void UseScreen(AShipScreen* Screen);

    /** Get up from a screen. */
    void StopUsingScreen();

    UFUNCTION(BlueprintPure, Category = "Interaction")
    bool IsUsingScreen() const { return UsedScreen != nullptr; }

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

    /** The seam the input handlers go through, and what tests drive: held
     *  attitude -1..1 per body axis, and throttle as a rate, not a position. */
    void SetFlightInput(const FVector& Attitude, float ThrottleRate);

    /** The lever's position, -1..1. */
    float GetThrottle() const { return Throttle; }

    /**
     * True when the pointer is live and over something on a ship screen.
     *
     * The pointer is the whole interaction model for screens (spec decision
     * 1): no cursor, no fullscreen, no pause -- a virtual Slate user driven
     * along the view, live only within reach of a screen.
     */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    bool IsPointingAtScreen() const;

    /** Press and release, exposed so a test can click without an input stack. */
    void PressPointer();
    void ReleasePointer();

    UWidgetInteractionComponent* GetPointer() const { return Pointer; }

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

    /**
     * Horizontal field of view, degrees. The engine's 90 is narrow for a
     * first-person body inside a cramped hull -- you cannot see your own
     * hands, or the edges of a doorway you are standing in. Wider trades some
     * edge distortion for knowing where you are.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float FieldOfView = 103.0f;

    /**
     * Field of view while sat at a screen, degrees. Narrow: the panel should
     * fill the view the way a thing you are reading does. Framing by angle
     * rather than by moving closer keeps the camera out of the body.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float UseFieldOfView = 52.0f;

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

    /** Click, for driving a screen. Left mouse button; see
     *  Tools/setup_pointer_input.py. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> PointAction;

    /** Held to sprint. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> SprintAction;

    /** Pressed to toggle crouch. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> CrouchAction;

    /**
     * Held while piloting to turn the ship: X pitch, Y yaw, Z roll, each
     * -1..1. Keyboard flies and the mouse keeps looking -- the pilot's head
     * turns independently of the ship, which is what makes a turn read as the
     * ship turning rather than the camera swinging.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> AttitudeAction;

    /**
     * Held to move the throttle, not to set it: the flight command's throttle
     * is persistent (set and leave), so this is a rate. +1 opens, -1 closes.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> ThrottleAction;

    /** How fast held throttle input sweeps the throttle, fraction per second.
     *  Four seconds lever-stop to lever-stop: slow enough to settle on a
     *  cruise by feel rather than by tapping. */
    UPROPERTY(EditDefaultsOnly, Category = "Flight")
    float ThrottleSweepRate = 0.5f;

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
    void SetAttitudeInput(const FInputActionValue& Value);
    void ClearAttitudeInput(const FInputActionValue& Value);
    void SetThrottleInput(const FInputActionValue& Value);
    void ClearThrottleInput(const FInputActionValue& Value);

    /** Sweeps the throttle and hands the ship this frame's intent. Refused by
     *  the subsystem unless we are the pilot, which is the only gate. */
    void PushFlightCommand(float DeltaSeconds);

    /** Sets MaxWalkSpeed from the sprint request and the movement rules. */
    void UpdateWalkSpeed();

    /** Restricts or restores how far the camera may turn. */
    void SetViewLimits(bool bSeated, float SeatYaw);

    /** Re-runs the reach trace and updates FocusedInteractable. */
    void UpdateFocusedInteractable();

    /** Aims the pointer along the view and switches it off when it cannot be
     *  used -- seated, or with no screen in reach. */
    void UpdatePointer();

    /**
     * Drives world screens. Traces along the view out to InteractionRange, so
     * a screen can only be operated from where the player could touch it, and
     * runs as a virtual Slate user so no OS cursor is ever involved.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interaction", meta = (AllowPrivateAccess = "true"))
    TObjectPtr<UWidgetInteractionComponent> Pointer;

    /** Eye height above the actor's origin, damped; see EyeHeightLagSpeed. */
    float DampedEyeHeight = 0.0f;

    /** False until DampedEyeHeight has a real value to damp towards. */
    bool bEyeHeightSettled = false;

    UPROPERTY()
    TObjectPtr<UInteractableComponent> FocusedInteractable;

    UPROPERTY()
    TObjectPtr<UUserWidget> HUDWidget;

    /** The screen we are sat at, or null. */
    UPROPERTY()
    TObjectPtr<AShipScreen> UsedScreen;

    /** The seat we are sitting in, or null. */
    UPROPERTY()
    TObjectPtr<APilotSeat> Seat;

    /** IA_Move's latest value, kept for the sprint rule; zero when released. */
    FVector2D MoveInput = FVector2D::ZeroVector;

    bool bWantsToSprint = false;

    /** Held attitude input, -1..1 per body axis; zero when released. */
    FVector AttitudeInput = FVector::ZeroVector;

    /** Held throttle input, -1..1; a rate, see ThrottleAction. */
    float ThrottleInput = 0.0f;

    /** The lever's position, -1..1. Survives standing up: a cruise you set and
     *  walked away from is the point (FShipFlightState::ReleaseAttitude). */
    float Throttle = 0.0f;
};
