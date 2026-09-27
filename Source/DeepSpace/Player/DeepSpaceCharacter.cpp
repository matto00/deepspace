#include "Player/DeepSpaceCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Player/MovementRules.h"
#include "Ship/InteractableComponent.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipScreen.h"
#include "Ship/ShipSubsystem.h"
#include "UI/ShipHUDWidget.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

namespace
{
    // Standing up from a screen. The rings are the search for somewhere to
    // stand when the spot the player left is taken: 30 cm apart, a little
    // under a capsule's width, so no gap a body fits is stepped over, and out
    // to 1.2 m -- still beside the screen, which is the point of getting up
    // there rather than somewhere else in the room.
    constexpr float StandRingStepCm = 30.0f;
    constexpr int32 StandRings = 4;
    constexpr int32 StandDirections = 12;

    // A spot counts as floor only if something is under it within this of
    // the floor under the seat: higher is standing on something, lower is a
    // hole or the far side of a hull.
    constexpr float StandFloorAboveCm = 5.0f;
    constexpr float StandFloorBelowCm = 15.0f;

    // How far above the floor the capsule is put, cm: inside the movement
    // component's own 1.9--2.4 cm float, so the first walking frame finds the
    // floor where it expects it rather than stepping or falling onto it.
    constexpr float StandFloorClearanceCm = 2.0f;
}

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

// 19 cm forward of the anchor, 2 to port, 125 up: what the sitting idle does
// with the head, measured (DeepSpace.Player.SeatedEyeIsPilotEye).
const FVector ADeepSpaceCharacter::SeatedEyeOffset(19.0, -2.0, 125.0);

void ADeepSpaceCharacter::PlaceCamera(float DeltaSeconds, const FRotator& ViewRotation)
{
    const USkeletalMeshComponent* Body = GetMesh();
    if (!Body || !FirstPersonCamera)
    {
        return;
    }

    // Zoomed on a screen the camera leaves the head entirely and frames the
    // panel. Rotation still comes from the controller, which ZoomScreen
    // pointed at the screen and IgnoreLookInput now holds there. In the chart
    // chair unzoomed it rides the head, as at the helm.
    if (const AShipScreen* Zoomed = ZoomedScreen.Get())
    {
        FirstPersonCamera->SetWorldLocation(Zoomed->GetViewTransform().GetLocation());
        FrameZoomedScreen();
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
    PushHelmInput();
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

    if (IsUsingScreen())
    {
        // Mouse source ignores the component's transform, so there is
        // nothing to aim; ZoomScreen switched it on, and it stays on.
        return;
    }

    // Seated -- at the helm, or in the chart chair with nothing zoomed --
    // the keys are the ship's and E means the seat's business, and a pointer
    // live on any screen in reach would find the chart from the helm, two
    // metres off and laid out for its own chair. So seated it is live only
    // on a screen that says that seat may drive it: from the helm, the map
    // (system map spec, decision 2); from the chart chair, the map and the
    // chart. The left button is bound to nothing else at a seat, so a live
    // pointer fights nothing.
    //
    // Seated, the pointer is handed the gate's own hit (the Custom source)
    // rather than tracing for itself. Its own trace ignores only this pawn,
    // and the helm's eye is inside the helm seat's reach box -- the box that
    // lets a standing player's E find the chair -- so from the helm its own
    // trace met the seat and never reached the map. The gate's trace ignores
    // the chair too; handing it over means the gate and the pointer cannot
    // disagree about what is under the view.
    const bool bSeated = IsSeated() || IsInScreenChair();
    bool bAllowed = true;
    FHitResult Hit;
    if (bSeated)
    {
        const AShipScreen* InView = FindScreenInView(&Hit);
        bAllowed = InView && Hit.GetComponent() == InView->GetScreen()
            && (IsInScreenChair() ? InView->IsDrivableFromChartChair() : InView->IsDrivableSeated());
    }
    if (Pointer->IsActive() != bAllowed)
    {
        if (bAllowed)
        {
            Pointer->Activate();
        }
        else
        {
            // Never leave a button held down because the view left the map
            // or the player sat down -- and release it *before* switching
            // off: the release goes through the pointer's virtual Slate
            // user, which deactivating unregisters, so a release after it is
            // dropped and the key stays down inside the component. Its next
            // press is then swallowed as a repeat, and a click on the map is
            // lost each time the view slid off it mid-press.
            Pointer->ReleasePointerKey(EKeys::LeftMouseButton);
            Pointer->Deactivate();
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
    Pointer->InteractionSource = bSeated ? EWidgetInteractionSource::Custom : EWidgetInteractionSource::World;
    if (bSeated)
    {
        Pointer->SetCustomHitResult(Hit);
    }
}

AShipScreen* ADeepSpaceCharacter::FindScreenInView(FHitResult* OutHit) const
{
    FHitResult Local;
    FHitResult& Hit = OutHit ? *OutHit : Local;
    Hit = FHitResult();
    const UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    // The pointer's trace: from the eyes along the view, on its channel
    // (Visibility), out to InteractionRange, ignoring this pawn -- and the
    // chair it is sat in, whose reach box encloses a seated pilot's eyes and
    // would otherwise be the first thing every look from the helm met.
    const FVector Start = GetEyeLocation();
    const FVector End = Start + GetViewRotation().Vector() * InteractionRange;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ScreenInView), false, this);
    if (Seat)
    {
        Params.AddIgnoredActor(Seat);
    }
    if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
    {
        // What a pointer handed this hit must see as a miss, still with the
        // line it traced along.
        Hit.TraceStart = Start;
        Hit.TraceEnd = End;
        return nullptr;
    }
    return Cast<AShipScreen>(Hit.GetActor());
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
    // Started counts the press; Triggered and Completed say whether the key
    // is down. Counting presses rather than reading a level is what keeps a
    // tap released inside one frame from being lost (flight-feel decision 3).
    if (LeverUpAction)
    {
        Input->BindAction(LeverUpAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::PressLeverUp);
        Input->BindAction(LeverUpAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::HoldLeverUp);
        Input->BindAction(LeverUpAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::ReleaseLeverUp);
        Input->BindAction(LeverUpAction, ETriggerEvent::Canceled, this, &ADeepSpaceCharacter::ReleaseLeverUp);
    }
    if (LeverDownAction)
    {
        Input->BindAction(LeverDownAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::PressLeverDown);
        Input->BindAction(LeverDownAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::HoldLeverDown);
        Input->BindAction(LeverDownAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::ReleaseLeverDown);
        Input->BindAction(LeverDownAction, ETriggerEvent::Canceled, this, &ADeepSpaceCharacter::ReleaseLeverDown);
    }
    if (DriveAction)
    {
        Input->BindAction(DriveAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::ToggleDrive);
    }
    if (StopAction)
    {
        Input->BindAction(StopAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::PressStop);
    }
    if (CycleTargetAction)
    {
        Input->BindAction(CycleTargetAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::CycleTarget);
    }
}

void ADeepSpaceCharacter::ToggleDrive()
{
    // The subsystem is the gate: only the pilot moves the lever, and the
    // lever's position is the ship's, never a copy kept here.
    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->SetDriveEngaged(this, !Ship->IsDriveEngaged());
    }
}

void ADeepSpaceCharacter::PressStop()
{
    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->AllStop(this);
    }
}

void ADeepSpaceCharacter::CycleTarget()
{
    // Only on the zoomed map, which is where picking happens: the key then
    // walks the rows the player is looking at. At the helm the map is look
    // and click (decision 2), and Tab takes no key from a helm with little
    // room left; zoomed on the chart it would change a thing not on screen.
    if (!Cast<AShipMapScreen>(ZoomedScreen.Get()))
    {
        return;
    }
    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->CycleTarget();
    }
}

void ADeepSpaceCharacter::SetFlightInput(const FVector& Attitude)
{
    AttitudeInput = Attitude.BoundToBox(FVector(-1.0), FVector(1.0));
}

void ADeepSpaceCharacter::TapLever(int32 Direction)
{
    if (Direction > 0)
    {
        ++LeverUpPresses;
    }
    else if (Direction < 0)
    {
        ++LeverDownPresses;
    }
}

void ADeepSpaceCharacter::HoldLever(int32 Direction)
{
    bLeverUpHeld = Direction > 0;
    bLeverDownHeld = Direction < 0;
}

void ADeepSpaceCharacter::SetAttitudeInput(const FInputActionValue& Value)
{
    SetFlightInput(Value.Get<FVector>());
}

void ADeepSpaceCharacter::ClearAttitudeInput(const FInputActionValue& Value)
{
    SetFlightInput(FVector::ZeroVector);
}

void ADeepSpaceCharacter::PushHelmInput()
{
    FHelmInput Input;
    Input.Attitude = AttitudeInput;
    Input.bUpHeld = bLeverUpHeld;
    Input.bDownHeld = bLeverDownHeld;
    Input.UpPresses = LeverUpPresses;
    Input.DownPresses = LeverDownPresses;
    LeverUpPresses = 0;
    LeverDownPresses = 0;

    // Once a frame, whoever we are: the subsystem is the gate, and moves the
    // levers in its own tick. A pawn that is not flying hands over nothing,
    // and its presses are gone rather than saved for when it sits down.
    UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (Ship && Ship->GetPilot() == this)
    {
        Ship->SetHelmInput(this, Input);
    }
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
    if (IsSeated() || IsInScreenChair())
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
    if (!NewSeat || IsSeated() || IsInScreenChair())
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

void ADeepSpaceCharacter::UseScreen(AShipScreen* Screen)
{
    if (!Screen || !Screen->IsUsable() || IsSeated() || IsInScreenChair())
    {
        return;
    }

    // Remembered before anything moves: the feet, not the capsule's centre,
    // because the capsule may be crouched now and standing when it returns.
    StandingFeet = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

    if (bIsCrouched)
    {
        UnCrouch();
    }

    UsedScreen = Screen;
    FocusedInteractable = nullptr;

    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    SetActorEnableCollision(false);

    // The capsule stands on the floor under the seat, not on its cushion:
    // the sitting idle is posed against the helm, whose anchor is on the
    // floor, and it lifts the hips onto the chair itself. On the cushion the
    // body sat a cushion's height above the chair with its eyes at 1.8 m --
    // unseen while sitting always framed the screen and hid the body, and
    // plain once the chart chair left the view the player's own.
    const FTransform UsePose = Screen->GetUseTransform();
    const float SeatYaw = UsePose.Rotator().Yaw;
    const FVector Anchor(UsePose.GetLocation().X, UsePose.GetLocation().Y, Screen->GetUseFloorZ());
    SetActorLocationAndRotation(
        Anchor + FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
        FRotator(0.0f, SeatYaw, 0.0f));
    bUseControllerRotationYaw = false;

    // A seat's limits, as at the helm: the head turns, the body does not.
    SetViewLimits(true, SeatYaw);

    if (Screen->ZoomsOnSit())
    {
        ZoomScreen(Screen);
        return;
    }

    // The chart chair: sat, and the view still the player's (decision 13).
    // It starts on the screen sat at, so choosing that one is one press, as
    // it was when sitting framed it, and choosing the map beside it is a
    // glance and the same press.
    //
    // Aimed from where the eyes will settle, not from where they are. This
    // runs from input, before the frame's animation, so the head is still
    // where the standing pose put it: aimed from there, the view kept a
    // standing eye's pitch while the body sat and the eye dropped 40 cm,
    // ended under the chart, and E -- pressed again to read it -- stood the
    // player straight back up. The eye then settles onto the head as it
    // always does, damped, and the view -- held -- is on the glass once it
    // has.
    const FVector SeatedEye = Anchor + FRotator(0.0f, SeatYaw, 0.0f).RotateVector(SeatedEyeOffset);
    const FVector Glass = Screen->GetScreen() ? Screen->GetScreen()->GetComponentLocation() : Screen->GetActorLocation();
    const FRotator OnScreen = (Glass - SeatedEye).Rotation();
    if (Controller)
    {
        Controller->SetControlRotation(OnScreen);
    }
    bEyeHeightSettled = false;
    PlaceCamera(0.0f, OnScreen);
    MarkCameraCut();
}

void ADeepSpaceCharacter::ZoomScreen(AShipScreen* Screen)
{
    if (!Screen || !IsInScreenChair() || IsUsingScreen())
    {
        return;
    }

    UnzoomedView = GetViewRotation();
    ZoomedScreen = Screen;
    const FRotator Framed = Screen->GetViewTransform().Rotator();

    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        // The view is framed and stays framed: the mouse is a cursor now, so
        // letting it also turn the head would fight every attempt to click.
        PC->SetControlRotation(Framed);
        PC->SetIgnoreLookInput(true);

        PC->bShowMouseCursor = true;
        FInputModeGameAndUI Mode;
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
        Mode.SetHideCursorDuringCapture(false);
        PC->SetInputMode(Mode);
    }

    if (Pointer)
    {
        // Mouse source deprojects the cursor instead of tracing along the
        // view, which is the whole point: the pointer goes where the cursor
        // is rather than where the head is. On, whatever the seated gate
        // last said: the chart is zoomable and never drivable seated.
        Pointer->InteractionSource = EWidgetInteractionSource::Mouse;
        if (!Pointer->IsActive())
        {
            Pointer->Activate();
        }
    }

    // The camera goes to the screen now, in the same frame the cut is
    // marked, rather than on the next tick: a cut marked a frame early is
    // spent on a frame that did not jump, and the jump then smears.
    PlaceCamera(0.0f, Framed);
    MarkCameraCut();

    // The framed camera sits in front of the face, which is behind the body's
    // own arms: left visible they fill both sides of the screen. Nobody looks
    // at their own shoulders while reading a laptop.
    if (USkeletalMeshComponent* Body = GetMesh())
    {
        Body->SetVisibility(false, true);
    }
}

void ADeepSpaceCharacter::ReleaseZoom()
{
    // Explicitly null, not merely invalid: a framed screen destroyed while
    // framed must still give the controller its look input back.
    if (ZoomedScreen.IsExplicitlyNull())
    {
        return;
    }
    ZoomedScreen.Reset();

    if (APlayerController* PC = Cast<APlayerController>(Controller))
    {
        PC->SetIgnoreLookInput(false);
        PC->bShowMouseCursor = false;
        PC->SetInputMode(FInputModeGameOnly());
    }
    if (Pointer)
    {
        Pointer->InteractionSource = EWidgetInteractionSource::World;
    }
    if (FirstPersonCamera)
    {
        FirstPersonCamera->SetFieldOfView(FieldOfView);
    }
    if (USkeletalMeshComponent* Body = GetMesh())
    {
        Body->SetVisibility(true, true);
    }
}

void ADeepSpaceCharacter::Unzoom()
{
    if (!IsUsingScreen())
    {
        return;
    }
    ReleaseZoom();

    // Looking where the player looked before the zoom, which was the screen
    // they zoomed: back in the chair, one glance from either screen. The
    // eyes go back to the head now, with a cut, as standing up does.
    if (Controller)
    {
        Controller->SetControlRotation(UnzoomedView);
    }
    bEyeHeightSettled = false;
    PlaceCamera(0.0f, UnzoomedView);
    MarkCameraCut();
}

void ADeepSpaceCharacter::StopUsingScreen()
{
    if (!IsInScreenChair())
    {
        return;
    }

    const FRotator Facing = GetActorRotation();
    ReleaseZoom();

    // Found before collision comes back, though it would not matter: every
    // query here ignores this actor. What matters is that it is found at all
    // -- the old answer was to stand up where the seat was, which is on the
    // chair, and a player had to crouch to get off it.
    const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (const TOptional<FVector> Spot = FindStandingSpot())
    {
        // FindStandingSpot answers for a standing capsule; if this one is
        // still crouched it is shorter, and goes on the same floor.
        SetActorLocation(*Spot - FVector(0.0f, 0.0f, GetDefaultHalfHeight() - HalfHeight),
                         false, nullptr, ETeleportType::TeleportPhysics);
    }
    else if (StandingFeet.IsSet())
    {
        // Nowhere near fits a body. Where they came from is still the least
        // wrong place: a standing body fitted there a moment ago, and the
        // movement component pushes a capsule out of whatever has arrived
        // since. It may be up on something they climbed; the seat itself is
        // never the answer -- that is the one known to be wrong.
        UE_LOG(LogTemp, Warning, TEXT("StopUsingScreen: no clear floor near %s; standing where the player sat down from"),
               *StandingFeet->ToCompactString());
        SetActorLocation(*StandingFeet + FVector(0.0f, 0.0f, HalfHeight + StandFloorClearanceCm),
                         false, nullptr, ETeleportType::TeleportPhysics);
    }
    StandingFeet.Reset();
    UsedScreen = nullptr;

    SetActorEnableCollision(true);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    bUseControllerRotationYaw = true;
    SetViewLimits(false, 0.0f);

    // Stand up looking where the body faces, not where the view was
    // pointing, which was tilted down at a screen. ReleaseZoom has already
    // given back the look input, the pointer, the angle and the body, if a
    // screen was framed.
    if (Controller)
    {
        Controller->SetControlRotation(FRotator(0.0f, Facing.Yaw, 0.0f));
    }

    // The camera is attached to the capsule and was framing the screen, so
    // the teleport carried it along at that offset -- somewhere in front of
    // the new spot, perhaps in a wall. Put it back at the eyes now rather
    // than on the next tick, and cut rather than ease: an eased camera would
    // travel from the screen to the body through whatever lies between.
    bEyeHeightSettled = false;
    PlaceCamera(0.0f, FRotator(0.0f, Facing.Yaw, 0.0f));
    MarkCameraCut();
}

void ADeepSpaceCharacter::MarkCameraCut() const
{
    // Sitting and standing move the view up to 60 cm in one frame. Temporal
    // AA and motion blur read the previous frame to build this one, and
    // across a jump that history is of somewhere else: the frame smears. The
    // camera manager's cut flag is how the engine is told the history is
    // void; it clears itself once the frame has been drawn.
    if (const APlayerController* PC = Cast<APlayerController>(Controller))
    {
        if (PC->PlayerCameraManager)
        {
            PC->PlayerCameraManager->SetGameCameraCutThisFrame();
        }
    }
}

ADeepSpaceCharacter::FFramingView ADeepSpaceCharacter::ResolveFramingView(
    const FIntPoint& ViewportSize, EAspectRatioAxisConstraint PlayerConstraint, const UCameraComponent& Camera)
{
    // Headless there is no viewport; 16:9 is the window this is played in.
    FFramingView View{16.0f / 9.0f, PlayerConstraint};
    if (ViewportSize.X > 0 && ViewportSize.Y > 0)
    {
        View.Aspect = static_cast<float>(ViewportSize.X) / static_cast<float>(ViewportSize.Y);
    }
    if (Camera.bConstrainAspectRatio)
    {
        // Letterboxed to the camera's own aspect, whatever the window is.
        View.Aspect = Camera.AspectRatio;
    }
    else if (Camera.bOverrideAspectRatioAxisConstraint)
    {
        View.Constraint = Camera.AspectRatioAxisConstraint;
    }
    return View;
}

void ADeepSpaceCharacter::FrameZoomedScreen()
{
    const AShipScreen* Zoomed = ZoomedScreen.Get();
    if (!Zoomed || !FirstPersonCamera)
    {
        return;
    }

    FIntPoint ViewportSize(0, 0);
    EAspectRatioAxisConstraint Constraint = GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint;
    if (const APlayerController* PC = Cast<APlayerController>(Controller))
    {
        PC->GetViewportSize(ViewportSize.X, ViewportSize.Y);
        if (const ULocalPlayer* Local = PC->GetLocalPlayer())
        {
            Constraint = Local->AspectRatioAxisConstraint;
        }
    }

    const FFramingView View = ResolveFramingView(ViewportSize, Constraint, *FirstPersonCamera);
    FirstPersonCamera->SetFieldOfView(
        Zoomed->GetUseFieldOfView(View.Aspect, View.Constraint, FirstPersonCamera->AspectRatio));
}

TOptional<FVector> ADeepSpaceCharacter::FindStandingSpot() const
{
    const UWorld* World = GetWorld();
    if (!World || !StandingFeet.IsSet() || !UsedScreen)
    {
        return {};
    }

    // A standing capsule, whatever this one is now: a spot a crouched body
    // fits and a standing one does not is the crawlway, and nobody gets up
    // from a screen into the crawlway.
    const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight = GetDefaultHalfHeight();
    const FCollisionShape Standing = FCollisionShape::MakeCapsule(Radius, HalfHeight);
    const FCollisionQueryParams Params(SCENE_QUERY_STAT(StandUpFromScreen), false, this);
    const FVector Feet = *StandingFeet;

    // Floor is measured from the floor under the seat, never from the height
    // the feet were at. Jump is bound, and a player who climbed onto the
    // chair and sat down from there left their feet on its cushion: measured
    // from the feet, the cushion is floor, and they stood up on top of it.
    const double FloorZ = UsedScreen->GetUseFloorZ();

    // Where a standing capsule's centre goes over At, if At is floor. Not
    // "the nearest thing below": a box top is something below, and standing
    // on the chair is the bug.
    auto CentreOver = [&](const FVector2D& At) -> TOptional<FVector>
    {
        FHitResult Floor;
        const FVector Top(At.X, At.Y, FloorZ + StandFloorAboveCm);
        const FVector Bottom(At.X, At.Y, FloorZ - StandFloorBelowCm);
        if (!World->LineTraceSingleByChannel(Floor, Top, Bottom, ECC_Pawn, Params) || Floor.bStartPenetrating)
        {
            return {};
        }
        return FVector(At.X, At.Y, Floor.ImpactPoint.Z + HalfHeight + StandFloorClearanceCm);
    };
    auto Fits = [&](const FVector& Centre)
    {
        return !World->OverlapBlockingTestByChannel(Centre, FQuat::Identity, ECC_Pawn, Standing, Params);
    };

    const FVector2D Home2D(Feet.X, Feet.Y);
    const TOptional<FVector> Home = CentreOver(Home2D);
    if (Home && Fits(*Home))
    {
        return Home;
    }

    // A ring spot must be reachable from the one the player left, or the
    // search would happily stand them on the far side of a bulkhead. Reached
    // by a body, not a line: a standing capsule swept along the floor, so a
    // table, a bench or a gap narrower than a body is in the way, as it is
    // to walking. Swept from the floor at the remembered spot, not from the
    // feet, which may have been up on the furniture. Whatever occupies that
    // start is left out -- a sweep that starts inside a thing says nothing
    // about walls -- and that includes the chair a player climbed onto.
    //
    // It cannot step up, where walking steps 45 cm: a spot past a raised
    // threshold is refused though a body could walk to it. That errs toward
    // a nearer ring or the remembered spot, never toward the far side of
    // something, and the ship's decks are flat.
    const FVector From(Feet.X, Feet.Y, FloorZ + HalfHeight + StandFloorClearanceCm);
    FCollisionQueryParams PathParams = Params;
    {
        TArray<FOverlapResult> Occupants;
        World->OverlapMultiByChannel(Occupants, From, FQuat::Identity, ECC_Pawn, Standing, Params);
        for (const FOverlapResult& Occupant : Occupants)
        {
            if (const UPrimitiveComponent* Component = Occupant.GetComponent())
            {
                PathParams.AddIgnoredComponent(Component);
            }
        }
    }

    // Search outward, and round each ring starting from the side away from
    // the seat: that is where the player walked up from, and it is open
    // floor, where toward the seat is the chair and the screen.
    FVector2D Away = Home2D - FVector2D(GetActorLocation().X, GetActorLocation().Y);
    if (!Away.Normalize())
    {
        Away = -FVector2D(GetActorForwardVector().X, GetActorForwardVector().Y).GetSafeNormal();
    }
    const float AwayYaw = FMath::Atan2(Away.Y, Away.X);
    const float Step = 2.0f * UE_PI / StandDirections;

    for (int32 Ring = 1; Ring <= StandRings; ++Ring)
    {
        const float Distance = StandRingStepCm * Ring;
        for (int32 I = 0; I < StandDirections; ++I)
        {
            // 0, +1, -1, +2, -2, ...: nearest to "away" first.
            const int32 Turn = (I % 2 == 1) ? (I + 1) / 2 : -(I / 2);
            const float Yaw = AwayYaw + Turn * Step;
            const FVector2D At = Home2D + FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw)) * Distance;
            const TOptional<FVector> Centre = CentreOver(At);
            if (Centre && Fits(*Centre) &&
                !World->SweepTestByChannel(From, *Centre, FQuat::Identity, ECC_Pawn, Standing, PathParams))
            {
                return Centre;
            }
        }
    }
    return {};
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

    // Seated, E means the seat's business whatever the player is looking
    // at: stand up at the helm, and zoom, go back or stand up in a screen's
    // chair (TryInteract).
    if (!FirstPersonCamera || IsSeated() || IsInScreenChair())
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
    // In a screen's chair (decision 13). Zoomed, E goes back to the seat --
    // except at a screen that zooms on sit, the laptop, where the zoom is the
    // sitting and E stands up as it always has. Unzoomed, E zooms the
    // zoomable screen the view is on, and on neither it stands up. Choosing
    // is one press, switching screens two and a glance, leaving two.
    if (IsInScreenChair())
    {
        if (IsUsingScreen())
        {
            if (UsedScreen->ZoomsOnSit())
            {
                StopUsingScreen();
            }
            else
            {
                Unzoom();
            }
            return;
        }
        AShipScreen* InView = FindScreenInView();
        if (InView && InView->IsZoomableFromChartChair())
        {
            ZoomScreen(InView);
        }
        else
        {
            StopUsingScreen();
        }
        return;
    }

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
    // Says what E will do, which in a screen's chair TryInteract decides.
    if (IsInScreenChair())
    {
        if (IsUsingScreen() && !UsedScreen->ZoomsOnSit())
        {
            return NSLOCTEXT("DeepSpace", "Unzoom", "Back");
        }
        if (!IsUsingScreen())
        {
            const AShipScreen* InView = FindScreenInView();
            if (InView && InView->IsZoomableFromChartChair())
            {
                return InView->GetZoomPrompt();
            }
        }
        return NSLOCTEXT("DeepSpace", "StandUp", "Stand up");
    }
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
