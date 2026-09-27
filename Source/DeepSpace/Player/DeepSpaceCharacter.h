#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
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

    /**
     * Get up from a screen: back to where the player was standing when they
     * sat, or the nearest clear floor to it -- never on the chair.
     */
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

    /** The window shape a screen is framed for, and the axis it keeps. */
    struct FFramingView
    {
        float Aspect;
        EAspectRatioAxisConstraint Constraint;
    };

    /**
     * What FrameUsedScreen fits the screen to, given the viewport's size in
     * pixels (0x0 when there is none, as headless), the player's configured
     * axis constraint, and the camera -- which may letterbox to its own shape
     * or keep its own axis whatever the window is. Separate from reading the
     * viewport so the one input fitting depends on in a real window can be
     * tested without one.
     */
    static FFramingView ResolveFramingView(const FIntPoint& ViewportSize,
                                           EAspectRatioAxisConstraint PlayerConstraint,
                                           const UCameraComponent& Camera);

    /** The seam the attitude handlers go through, and what tests drive:
     *  held attitude, -1..1 per body axis. */
    void SetFlightInput(const FVector& Attitude);

    /**
     * A lever key pressed (Direction +1 is Shift, -1 is Ctrl), counted as a
     * press and not read from a level, so a press released inside one frame
     * is still one notch (flight-feel decision 3). What IA_LeverUp and
     * IA_LeverDown's Started do; public so a test can tap without an input
     * stack. Nothing is held by it.
     */
    void TapLever(int32 Direction);

    /** Which lever key is held: +1 Shift, -1 Ctrl, 0 neither. What their
     *  Triggered and Completed do; public for the tests. */
    void HoldLever(int32 Direction);

    /** What the drive key does, exposed so a test can press it without an
     *  input stack. */
    void PressDrive() { ToggleDrive(); }

    /** What the stop key does (X): all stop, both levers. */
    void PressStop();

    /**
     * What Tab does (IA_CycleTarget): the next world in the system as the
     * target, while the map is zoomed at the chart chair (the system map
     * spec's decision 13). Bound now, with the lever actions, so the
     * character's Blueprint is recompiled once for all the new input; it does
     * nothing until the zoomed map exists to give it meaning.
     */
    void CycleTarget();

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
     * The live lever's keys (Shift up, Ctrl down), Boolean: a press is
     * counted on Started, and the key's being held on Triggered and
     * Completed. Under the drive a press is one notch and a hold repeats; in
     * cruise a hold sweeps and a fresh press leaves the detent at zero. The
     * levers themselves are the ship's (FShipFlightCommand), not the pawn's:
     * this only says what the hands are doing. See Tools/setup_flight_input.py.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> LeverUpAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> LeverDownAction;

    /**
     * Pressed to toggle which lever is live (F): the drive's or cruise's.
     * Each keeps its own setting across the toggle, so the speed F goes to is
     * the one left there. See Tools/setup_flight_input.py.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> DriveAction;

    /** Pressed for all stop (X): both levers to STOP. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> StopAction;

    /** Pressed to cycle the target (Tab), on the zoomed map; see CycleTarget. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> CycleTargetAction;

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
    void PressLeverUp() { TapLever(1); }
    void PressLeverDown() { TapLever(-1); }
    void HoldLeverUp() { bLeverUpHeld = true; }
    void ReleaseLeverUp() { bLeverUpHeld = false; }
    void HoldLeverDown() { bLeverDownHeld = true; }
    void ReleaseLeverDown() { bLeverDownHeld = false; }

    /** Flips the drive. Refused by the subsystem unless we are the pilot. */
    void ToggleDrive();

    /** Hands the ship what the helm's hands did this frame, once, and zeroes
     *  the press counts. Refused by the subsystem unless we are the pilot,
     *  which is the only gate; a non-pilot's presses are dropped, never
     *  saved up for when they sit down. */
    void PushHelmInput();

    /** Sets MaxWalkSpeed from the sprint request and the movement rules. */
    void UpdateWalkSpeed();

    /** Restricts or restores how far the camera may turn. */
    void SetViewLimits(bool bSeated, float SeatYaw);

    /**
     * Sets the camera's field of view to frame the screen sat at, whole, in
     * the viewport as it is now. Asked of the screen every frame rather than
     * fixed at sitting down, so a window resized mid-read still fits.
     */
    void FrameUsedScreen();

    /**
     * Where to stand up from a screen: the capsule's centre, on the floor
     * under the seat, with a standing capsule clear of everything.
     * StandingFeet if that is floor and still fits; else the nearest spot on
     * a few rings round it that is floor, fits, and a standing capsule can be
     * swept to from it. Unset if none does.
     */
    TOptional<FVector> FindStandingSpot() const;

    /** Tells the camera manager this frame's view is a cut, not a move. */
    void MarkCameraCut() const;

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

    /**
     * Where the feet were when the player sat down at a screen, so standing
     * up returns them there rather than onto the chair. The character's own
     * memory of its own body -- nobody else's state -- set on sitting and
     * cleared on standing.
     */
    TOptional<FVector> StandingFeet;

    /** The seat we are sitting in, or null. */
    UPROPERTY()
    TObjectPtr<APilotSeat> Seat;

    /** IA_Move's latest value, kept for the sprint rule; zero when released. */
    FVector2D MoveInput = FVector2D::ZeroVector;

    bool bWantsToSprint = false;

    /** Held attitude input, -1..1 per body axis; zero when released. */
    FVector AttitudeInput = FVector::ZeroVector;

    /** Whether each lever key is down, and how many times each was pressed
     *  since the last hand-over. The hands, not the levers: the levers are
     *  the ship's. */
    bool bLeverUpHeld = false;
    bool bLeverDownHeld = false;
    int32 LeverUpPresses = 0;
    int32 LeverDownPresses = 0;
};
