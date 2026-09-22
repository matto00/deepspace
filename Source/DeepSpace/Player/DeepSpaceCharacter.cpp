#include "Player/DeepSpaceCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Blueprint/UserWidget.h"
#include "Player/MovementRules.h"
#include "Ship/InteractableComponent.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipSubsystem.h"

ADeepSpaceCharacter::ADeepSpaceCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    // The mannequin's root is at its feet and it faces +Y; the capsule's
    // origin is at its centre and the pawn faces +X.
    GetMesh()->SetRelativeLocationAndRotation(
        FVector(0.0f, 0.0f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()),
        FRotator(0.0f, -90.0f, 0.0f));

    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(GetMesh(), HeadBone);
    CameraArm->TargetArmLength = 0.0f;
    CameraArm->bDoCollisionTest = false;
    CameraArm->bUsePawnControlRotation = true;
    CameraArm->bEnableCameraLag = true;
    CameraArm->CameraLagSpeed = 15.0f;
    // Just forward of the head bone, so the view is not from inside the skull.
    CameraArm->SocketOffset = FVector(8.0f, 0.0f, 0.0f);
    // The head bone is at the base of the skull; eyes are a little higher.
    // World-space, so the raise stays straight up however the head is turned.
    // Tools/check_anim_heights.py adds it to every clip's head height, so a
    // bigger raise must still fit inside the crouched capsule.
    CameraArm->TargetOffset = FVector(0.0f, 0.0f, 6.0f);

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
    FirstPersonCamera->bUsePawnControlRotation = false;

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->MaxWalkSpeed = FMovementRules::WalkSpeed;
    Movement->MaxWalkSpeedCrouched = FMovementRules::CrouchSpeed;
    Movement->NavAgentProps.bCanCrouch = true;
    Movement->SetCrouchedHalfHeight(FMovementRules::CrouchedHalfHeight);
}

void ADeepSpaceCharacter::BeginPlay()
{
    Super::BeginPlay();
    ConfigureFirstPersonBody();

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
}

void ADeepSpaceCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateWalkSpeed();
    UpdateFocusedInteractable();
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
    if (SprintAction)
    {
        Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::StartSprinting);
        Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::StopSprinting);
    }
    if (CrouchAction)
    {
        Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::ToggleCrouch);
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

void ADeepSpaceCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
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

    const FVector Start = FirstPersonCamera->GetComponentLocation();
    const FVector End = Start + FirstPersonCamera->GetForwardVector() * InteractionRange;

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
    return FocusedInteractable ? FocusedInteractable->GetPrompt() : FText::GetEmpty();
}
