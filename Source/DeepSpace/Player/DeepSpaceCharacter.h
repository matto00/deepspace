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
     * Sit down at a screen: the body takes the seat in front of it. A screen
     * that ZoomsOnSit -- the laptop -- is framed at once, the mouse becoming a
     * cursor that drives it. One that does not -- the chart -- leaves the
     * view the player's, within the seated limits, and E then zooms whichever
     * zoomable screen the view is on (system map spec, decision 13).
     *
     * Zooming is the one place a screen stops being something you aim at.
     * The spec's decision 1 kept every screen a surface in the room driven by
     * the view, and in play the laptop's sliders were too fiddly to aim at;
     * the answer was to sit the player down rather than to make it a menu.
     * The galley stays visible around it and nothing pauses -- see the
     * spec's second addendum.
     */
    void UseScreen(AShipScreen* Screen);

    /**
     * Get up from a screen: back to where the player was standing when they
     * sat, or the nearest clear floor to it -- never on the chair.
     */
    void StopUsingScreen();

    /**
     * True while a screen is framed and the mouse is a cursor over it: sat
     * at the laptop, or zoomed at the chart chair. What the HUD hides its dot
     * for. Sat in the chart chair unzoomed is not using a screen -- the view
     * is the player's and the dot is what they aim with; IsInScreenChair
     * says where the body is.
     */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    bool IsUsingScreen() const { return ZoomedScreen.IsValid(); }

    /** True while the body is in a screen's seat -- the laptop's bench or the
     *  chart chair -- zoomed or not. */
    bool IsInScreenChair() const { return UsedScreen != nullptr; }

    /** The screen framed now, or null. */
    AShipScreen* GetZoomedScreen() const { return ZoomedScreen.Get(); }

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

    /**
     * Where a seated body's eyes settle, cm, from its seat's anchor on the
     * floor, in the seat's own frame: (forward, starboard, up). Measured,
     * not chosen -- the sitting idle's head, through PlaceCamera -- and held
     * to that by DeepSpace.Player.SeatedEyeIsPilotEye, and to the layout's
     * SEATED_EYE by test_placement.py. Every seat places the body the same
     * way, on the floor with the idle lifting the hips, so this is every
     * seat's eye. UseScreen aims the chart chair's first view from here,
     * because when it runs the head is still where the standing pose left it.
     */
    static const FVector SeatedEyeOffset;

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

    /** What E does, exposed so a test can press it without an input stack:
     *  at the chart chair it zooms, goes back and stands up by what the view
     *  is on (decision 13). */
    void PressInteract() { TryInteract(); }

    /**
     * What Tab does (IA_CycleTarget): the next world in the system as the
     * target (UShipSubsystem::CycleTarget), and only while the map is zoomed
     * at the chart chair, where it is the map in front of the player that
     * picks (the system map spec's decision 13). At the helm, unzoomed, or
     * zoomed on the chart, it does nothing: "pilot just has look and click
     * control of map". Not pilot-gated, like every pick.
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
     * Held while piloting to turn the ship: X roll, Y pitch, Z yaw, each
     * -1..1, as FShipFlightCommand::AttitudeRate turns about them. Keyboard flies and the mouse keeps looking -- the pilot's head
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
     * Sets the camera's field of view to frame the zoomed screen, whole, in
     * the viewport as it is now. Asked of the screen every frame rather than
     * fixed at zooming, so a window resized mid-read still fits.
     */
    void FrameZoomedScreen();

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
     *  used: seated, unless the view is on a screen that IsDrivableSeated. */
    void UpdatePointer();

    /**
     * The screen the view is on, if it is the first thing a trace from the
     * eyes along the view meets -- on the pointer's channel, out to its
     * reach, ignoring this pawn and the chair it sits in -- with the hit
     * itself in OutHit. Seated, this trace *is* the pointer's (UpdatePointer
     * hands it over), and E at the chart chair zooms what it finds, so what
     * E zooms is what the pointer is on.
     */
    AShipScreen* FindScreenInView(FHitResult* OutHit = nullptr) const;

    /** Frames Screen from the seat the body is in: the camera to its view
     *  transform, the mouse a cursor over it, the body hidden, a cut. */
    void ZoomScreen(AShipScreen* Screen);

    /** Back to the seat from a zoom: the view the player's again, where it
     *  was before the zoom, and a cut. The body stays in the chair. */
    void Unzoom();

    /** Undoes everything ZoomScreen did to the controller, the pointer, the
     *  camera and the body, without placing the view: shared by Unzoom and
     *  by standing up, which places it itself. */
    void ReleaseZoom();

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

    /** The screen whose seat the body is in, or null. */
    UPROPERTY()
    TObjectPtr<AShipScreen> UsedScreen;

    /**
     * The screen framed, or null: UsedScreen when it ZoomsOnSit, or whichever
     * of the chart and the map E zoomed from the chart chair. Not reflected,
     * so BP_DeepSpaceCharacter's saved layout does not change for it; weak,
     * so a screen destroyed while framed leaves nothing dangling. Every
     * framing function reads this, never UsedScreen.
     */
    TWeakObjectPtr<AShipScreen> ZoomedScreen;

    /** Where the view was when the chart chair zoomed, so going back looks
     *  where the player was looking. The pawn's own memory, like
     *  StandingFeet. */
    FRotator UnzoomedView = FRotator::ZeroRotator;

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
