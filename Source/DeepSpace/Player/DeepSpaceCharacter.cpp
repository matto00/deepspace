#include "Player/DeepSpaceCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Player/MovementRules.h"
#include "Ship/InteractableComponent.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipSubsystem.h"
#include "UI/ShipHUDWidget.h"
#include "Engine/GameViewportClient.h"
#include "Components/WidgetInteractionComponent.h"

ADeepSpaceCharacter::ADeepSpaceCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    // The mannequin's root is at its feet and it faces +Y; the capsule's
    // origin is at its centre and the pawn faces +X.
    GetMesh()->SetRelativeLocationAndRotation(
        FVector(0.0f, 0.0f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()),
        FRotator(0.0f, -90.0f, 0.0f));

    // The camera hangs off the capsule and is moved to the head by
    // PlaceCamera each frame, rather than being attached to the head bone. A
    // bone attachment puts the camera wherever the animation says, and the
    // animation does not know about walls: the retargeted crouch idle carries
    // the head 57 cm from the capsule's axis, against a 34 cm capsule, and
    // crouching against a wall the view was outside the ship.
    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->bUsePawnControlRotation = true;

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->MaxWalkSpeed = FMovementRules::WalkSpeed;
    Movement->MaxWalkSpeedCrouched = FMovementRules::CrouchSpeed;
    Movement->NavAgentProps.bCanCrouch = true;
    Movement->SetCrouchedHalfHeight(FMovementRules::CrouchedHalfHeight);

    // The pointer. It rides the capsule and is aimed by UpdatePointer rather
    // than attached to the camera, for the same reason the camera is not
    // attached to the head: the thing it must agree with is the *view*
    // rotation, which is the controller's, not any component's.
    // The HUD is C++ Slate, not a widget blueprint: it is logic that reads
    // ship state every frame, and ADR 0002 keeps that out of Content.
    HUDWidgetClass = UShipHUDWidget::StaticClass();

    Pointer = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("Pointer"));
    Pointer->SetupAttachment(GetCapsuleComponent());
    Pointer->InteractionSource = EWidgetInteractionSource::World;
    Pointer->TraceChannel = ECC_Visibility;
    Pointer->InteractionDistance = InteractionRange;

    // A virtual Slate user, so no OS cursor is ever created and the game's
    // own input is untouched. Index 8 keeps clear of the real local players.
    Pointer->VirtualUserIndex = 8;
    Pointer->bEnableHitTesting = true;
    Pointer->bShowDebug = false;
}

void ADeepSpaceCharacter::BeginPlay()
{
    Super::BeginPlay();
    ConfigureFirstPersonBody();

    // PlaceCamera reads the head bone, so the pose must already be this
    // frame's. Nothing orders an actor's tick against its mesh's by default.
    // Set here, not in the constructor: a prerequisite added there is held on
    // the class default object, and would name the CDO's mesh, not ours.
    AddTickPrerequisiteComponent(GetMesh());

    // The pointer traces from wherever UpdatePointer last put it, so it must
    // tick after us or it aims at the previous frame's view.
    if (Pointer)
    {
        Pointer->AddTickPrerequisiteActor(this);
    }

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC)
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            for (UInputMappingContext* Context : {DefaultMappingContext.Get(),
                                                  MouseLookMappingContext.Get()})
            {
                if (Context)
                {
                    Subsystem->AddMappingContext(Context, 0);
                }
            }
        }

        if (HUDWidgetClass)
        {
            HUDWidget = CreateWidget<UUserWidget>(PC, HUDWidgetClass);
            if (HUDWidget)
            {
                HUDWidget->AddToViewport();
            }
        }
    }
}

void ADeepSpaceCharacter::ConfigureFirstPersonBody()
{
    USkeletalMeshComponent* Body = GetMesh();

    // Hiding a bone normally also stops the engine *animating* it: hidden
    // bones are dropped from the set it evaluates
    // (USkeletalMeshComponent::ExcludeHiddenBones). The head would freeze in
    // its reference pose, and the camera riding it with it -- crouching, the
    // view stayed at standing height. AlwaysTickPoseAndRefreshBones is the
    // engine's own exemption: hidden bones keep animating, and still are not
    // drawn. DeepSpace.Player.CameraFollowsHead checks this.
    Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

    // The camera sits at the head; hide it so the view is not from inside it.
    // Known stand-in: this also removes the head from the player's shadow.
    Body->HideBoneByName(HeadBone, EPhysBodyOp::PBO_None);

    // Set here rather than left to the Blueprint's camera template: the
    // template holds whatever was saved last, so a value changed in C++ would
    // silently not apply. Runtime always wins.
    if (FirstPersonCamera)
    {
        FirstPersonCamera->SetFieldOfView(FieldOfView);
    }
}

void ADeepSpaceCharacter::PlaceCamera(float DeltaSeconds, const FRotator& ViewRotation)
{
    const USkeletalMeshComponent* Body = GetMesh();
    if (!Body || !FirstPersonCamera)
    {
        return;
    }

    const FVector Origin = GetActorLocation();
    const FVector Head = Body->GetSocketLocation(HeadBone);

    // Height: follow the head, damped. Measured from the actor rather than in
    // world space, so walking up a ramp -- or being teleported out of the
    // seat -- does not smear the view.
    const float Wanted = Head.Z + EyeHeightAboveHead - Origin.Z;
    DampedEyeHeight = (bEyeHeightSettled && EyeHeightLagSpeed > 0.0f)
        ? FMath::FInterpTo(DampedEyeHeight, Wanted, DeltaSeconds, EyeHeightLagSpeed)
        : Wanted;
    bEyeHeightSettled = true;

    const float EyeZ = Origin.Z + DampedEyeHeight;

    // Sideways: the head exactly, plus the forward offset in the view's yaw.
    // Yaw only -- see EyeForwardOffset.
    const FVector Forward = FRotator(0.0f, ViewRotation.Yaw, 0.0f).Vector();
    const FVector Wants = FVector(Head.X, Head.Y, EyeZ) + Forward * EyeForwardOffset;

    // The animation can carry the head clean out of the capsule, so sweep
    // from the capsule's axis -- which the engine guarantees is clear -- out
    // to where the eyes want to be, and stop at whatever is in the way. This
    // is the spring arm's collision test, which a zero-length arm skipped.
    const FVector Axis(Origin.X, Origin.Y, EyeZ);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FirstPersonEye), false, this);
    FHitResult Hit;
    const bool bBlocked = GetWorld()->SweepSingleByChannel(
        Hit, Axis, Wants, FQuat::Identity, ECC_Camera,
        FCollisionShape::MakeSphere(EyeProbeRadius), Params);

    FirstPersonCamera->SetWorldLocation(bBlocked ? Hit.Location : Wants);
}

FVector ADeepSpaceCharacter::GetEyeLocation() const
{
    return FirstPersonCamera ? FirstPersonCamera->GetComponentLocation() : GetActorLocation();
}

void ADeepSpaceCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateWalkSpeed();
    PushFlightCommand(DeltaSeconds);
    PlaceCamera(DeltaSeconds, GetViewRotation());
    UpdateFocusedInteractable();
    UpdatePointer();
}

void ADeepSpaceCharacter::UpdatePointer()
{
    if (!Pointer)
    {
        return;
    }

    // Seated, the controls are the ship's and E means "stand up"; a pointer
    // live at the helm would fight both.
    const bool bAllowed = !IsSeated();
    if (Pointer->IsActive() != bAllowed)
    {
        bAllowed ? Pointer->Activate() : Pointer->Deactivate();
        if (!bAllowed)
        {
            // Never leave a button held down because the player sat down.
            Pointer->ReleasePointerKey(EKeys::LeftMouseButton);
        }
    }
    if (!bAllowed)
    {
        return;
    }

    // Reach is the same rule the hands obey: a screen can only be driven from
    // where the player could touch it, so the console cannot be operated from
    // across the room.
    Pointer->InteractionDistance = InteractionRange;
    Pointer->SetWorldLocationAndRotation(GetEyeLocation(), GetViewRotation());
}

bool ADeepSpaceCharacter::IsPointingAtScreen() const
{
    return Pointer && Pointer->IsActive() && Pointer->IsOverInteractableWidget();
}

void ADeepSpaceCharacter::PressPointer()
{
    if (Pointer && Pointer->IsActive())
    {
        Pointer->PressPointerKey(EKeys::LeftMouseButton);
    }
}

void ADeepSpaceCharacter::ReleasePointer()
{
    if (Pointer)
    {
        Pointer->ReleasePointerKey(EKeys::LeftMouseButton);
    }
}

void ADeepSpaceCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!Input)
    {
        return;
    }

    if (MoveAction)
    {
        Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::Move);
        Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::StopMoving);
    }
    if (LookAction)
    {
        Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::Look);
    }
    if (MouseLookAction)
    {
        Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::Look);
    }
    if (JumpAction)
    {
        Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
        Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
    }
    if (InteractAction)
    {
        Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::TryInteract);
    }
    if (PointAction)
    {
        Input->BindAction(PointAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::PressPointer);
        Input->BindAction(PointAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::ReleasePointer);
        Input->BindAction(PointAction, ETriggerEvent::Canceled, this, &ADeepSpaceCharacter::ReleasePointer);
    }
    if (SprintAction)
    {
        Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::StartSprinting);
        Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::StopSprinting);
    }
    if (CrouchAction)
    {
        Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::ToggleCrouch);
    }
    if (AttitudeAction)
    {
        Input->BindAction(AttitudeAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::SetAttitudeInput);
        Input->BindAction(AttitudeAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::ClearAttitudeInput);
    }
    if (ThrottleAction)
    {
        Input->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::SetThrottleInput);
        Input->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::ClearThrottleInput);
    }
}

void ADeepSpaceCharacter::SetFlightInput(const FVector& Attitude, float ThrottleRate)
{
    AttitudeInput = Attitude.BoundToBox(FVector(-1.0), FVector(1.0));
    ThrottleInput = FMath::Clamp(ThrottleRate, -1.0f, 1.0f);
}

void ADeepSpaceCharacter::SetAttitudeInput(const FInputActionValue& Value)
{
    SetFlightInput(Value.Get<FVector>(), ThrottleInput);
}

void ADeepSpaceCharacter::ClearAttitudeInput(const FInputActionValue& Value)
{
    SetFlightInput(FVector::ZeroVector, ThrottleInput);
}

void ADeepSpaceCharacter::SetThrottleInput(const FInputActionValue& Value)
{
    SetFlightInput(AttitudeInput, Value.Get<float>());
}

void ADeepSpaceCharacter::ClearThrottleInput(const FInputActionValue& Value)
{
    SetFlightInput(AttitudeInput, 0.0f);
}

void ADeepSpaceCharacter::PushFlightCommand(float DeltaSeconds)
{
    UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship || Ship->GetPilot() != this)
    {
        return;
    }

    // The throttle is a lever, not a button: input sweeps it and it stays put.
    Throttle = FMath::Clamp(Throttle + ThrottleInput * ThrottleSweepRate * DeltaSeconds, -1.0f, 1.0f);

    Ship->SetFlightCommand(this, Throttle, AttitudeInput);
}

void ADeepSpaceCharacter::Move(const FInputActionValue& Value)
{
    MoveInput = Value.Get<FVector2D>();
    if (!Controller || IsSeated())
    {
        return;
    }
    AddMovementInput(GetActorForwardVector(), MoveInput.Y);
    AddMovementInput(GetActorRightVector(), MoveInput.X);
}

void ADeepSpaceCharacter::StopMoving(const FInputActionValue& Value)
{
    MoveInput = FVector2D::ZeroVector;
}

void ADeepSpaceCharacter::StartSprinting()
{
    bWantsToSprint = true;
}

void ADeepSpaceCharacter::StopSprinting()
{
    bWantsToSprint = false;
}

void ADeepSpaceCharacter::ToggleCrouch()
{
    if (IsSeated())
    {
        return;
    }
    // UnCrouch is refused by the movement component while something is
    // overhead -- inside the crawlway, pressing C again does nothing.
    if (bIsCrouched)
    {
        UnCrouch();
    }
    else
    {
        Crouch();
    }
}

void ADeepSpaceCharacter::UpdateWalkSpeed()
{
    const bool bSprinting = bWantsToSprint && FMovementRules::CanSprint(bIsCrouched, MoveInput);
    GetCharacterMovement()->MaxWalkSpeed =
        bSprinting ? FMovementRules::SprintSpeed : FMovementRules::WalkSpeed;
}

EPosture ADeepSpaceCharacter::GetPosture() const
{
    if (IsSeated())
    {
        return EPosture::Seated;
    }
    if (GetCharacterMovement()->IsFalling())
    {
        return EPosture::Falling;
    }
    return bIsCrouched ? EPosture::Crouched : EPosture::Standing;
}

void ADeepSpaceCharacter::SitIn(APilotSeat* NewSeat)
{
    if (!NewSeat || IsSeated())
    {
        return;
    }
    if (bIsCrouched)
    {
        UnCrouch();
    }

    Seat = NewSeat;
    FocusedInteractable = nullptr;

    // Off: movement, and collision, so the capsule may overlap the chair.
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    SetActorEnableCollision(false);

    // The seat anchor is on the floor; the actor's origin is the capsule's
    // centre. The body faces the seat's way, and stays that way while the
    // head turns: it no longer follows the controller's yaw.
    const FTransform SeatTransform = Seat->GetSeatTransform();
    const float SeatYaw = SeatTransform.Rotator().Yaw;
    SetActorLocationAndRotation(
        SeatTransform.GetLocation() + FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
        FRotator(0.0f, SeatYaw, 0.0f));
    bUseControllerRotationYaw = false;
    if (Controller)
    {
        Controller->SetControlRotation(FRotator(0.0f, SeatYaw, 0.0f));
    }
    SetViewLimits(true, SeatYaw);

    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->SetPilot(this);
    }
}

void ADeepSpaceCharacter::StandUp()
{
    if (!IsSeated())
    {
        return;
    }
    const FVector Exit = Seat->GetExitLocation();
    Seat = nullptr;

    SetActorLocation(Exit + FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    SetActorEnableCollision(true);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    bUseControllerRotationYaw = true;
    SetViewLimits(false, 0.0f);

    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->ClearPilot();
    }
}

void ADeepSpaceCharacter::SetViewLimits(bool bSeated, float SeatYaw)
{
    const APlayerController* PC = Cast<APlayerController>(Controller);
    APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
    if (!Camera)
    {
        return;
    }
    if (bSeated)
    {
        Camera->ViewYawMin = SeatYaw - SeatedYawLimit;
        Camera->ViewYawMax = SeatYaw + SeatedYawLimit;
        Camera->ViewPitchMin = -SeatedPitchLimit;
        Camera->ViewPitchMax = SeatedPitchLimit;
    }
    else
    {
        // The engine's defaults: free yaw, pitch just short of straight up/down.
        const APlayerCameraManager* Defaults = GetDefault<APlayerCameraManager>();
        Camera->ViewYawMin = Defaults->ViewYawMin;
        Camera->ViewYawMax = Defaults->ViewYawMax;
        Camera->ViewPitchMin = Defaults->ViewPitchMin;
        Camera->ViewPitchMax = Defaults->ViewPitchMax;
    }
}

/**
 * Look diagnostics, off by default: `ds.LookDiag 1` in the console, then
 * move the mouse. Kept because the view shake this was written for is
 * intermittent across editor sessions and cannot be reproduced on demand.
 *
 * What it distinguishes: a healthy session shows same-sign deltas whose sum
 * tracks the yaw. The broken one shows the same 0.07 quantum alternating
 * sign every frame, so the sum stays near zero and the view never turns --
 * which puts the fault upstream of the game, in the pointer grab, rather
 * than anywhere in this class.
 */
static TAutoConsoleVariable<int32> CVarLookDiag(
    TEXT("ds.LookDiag"), 0,
    TEXT("Log mouse-look deltas, capture state and resulting rotation (0 off, N = frames to log)."),
    ECVF_Default);

void ADeepSpaceCharacter::Look(const FInputActionValue& Value)
{
    static FVector2D GLookSum = FVector2D::ZeroVector;
    static int32 GLookCalls = 0;

    const FVector2D Axis = Value.Get<FVector2D>();

    if (const int32 Budget = CVarLookDiag.GetValueOnGameThread(); Budget > 0 && GLookCalls < Budget)
    {
        ++GLookCalls;
        GLookSum += Axis;
        const APlayerController* PC = Cast<APlayerController>(Controller);
        const UGameViewportClient* VP = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
        UE_LOG(LogTemp, Warning,
            TEXT("LOOKDIAG #%d raw=(%.4f,%.4f) sum=(%.1f,%.1f) yaw=%.3f pitch=%.3f "
                 "capture=%d lockmode=%d cursor=%d pointerOver=%d ignoreLook=%d"),
            GLookCalls, Axis.X, Axis.Y, GLookSum.X, GLookSum.Y,
            PC ? PC->GetControlRotation().Yaw : -999.0f,
            PC ? PC->GetControlRotation().Pitch : -999.0f,
            VP ? (int32)VP->GetMouseCaptureMode() : -1,
            VP ? (int32)VP->GetMouseLockMode() : -1,
            PC ? (int32)PC->bShowMouseCursor : -1,
            (Pointer && Pointer->IsOverInteractableWidget()) ? 1 : 0,
            PC ? (int32)PC->IsLookInputIgnored() : -1);
    }

    AddControllerYawInput(Axis.X);
    AddControllerPitchInput(Axis.Y);
}

void ADeepSpaceCharacter::UpdateFocusedInteractable()
{
    FocusedInteractable = nullptr;

    // Seated, E means "stand up" whatever the player is looking at.
    if (!FirstPersonCamera || IsSeated())
    {
        return;
    }

    // The camera's own rotation is only refreshed when the view is built, so
    // reach along the controller's rotation rather than the component's.
    const FVector Start = FirstPersonCamera->GetComponentLocation();
    const FVector End = Start + GetViewRotation().Vector() * InteractionRange;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
    {
        return;
    }

    if (AActor* HitActor = Hit.GetActor())
    {
        UInteractableComponent* Interactable = HitActor->FindComponentByClass<UInteractableComponent>();
        if (Interactable && Interactable->CanInteract())
        {
            FocusedInteractable = Interactable;
        }
    }
}

void ADeepSpaceCharacter::TryInteract()
{
    if (IsSeated())
    {
        StandUp();
        return;
    }
    if (FocusedInteractable)
    {
        FocusedInteractable->Interact(this);
    }
}

UInteractableComponent* ADeepSpaceCharacter::GetFocusedInteractable() const
{
    return FocusedInteractable;
}

FText ADeepSpaceCharacter::GetCurrentPrompt() const
{
    if (IsSeated())
    {
        return NSLOCTEXT("DeepSpace", "StandUp", "Stand up");
    }
    if (FocusedInteractable)
    {
        return FocusedInteractable->GetPrompt();
    }
    if (IsPointingAtScreen())
    {
        return NSLOCTEXT("DeepSpace", "UseScreen", "Click  Screen");
    }
    return FText::GetEmpty();
}
