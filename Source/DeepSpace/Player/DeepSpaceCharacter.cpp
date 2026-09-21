#include "Player/DeepSpaceCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Ship/InteractableComponent.h"

ADeepSpaceCharacter::ADeepSpaceCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    // Roughly eye height for a standing adult, in centimetres.
    FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
    FirstPersonCamera->bUsePawnControlRotation = true;

    // Corridors are tight; a slower walk reads better than the engine default.
    GetCharacterMovement()->MaxWalkSpeed = 300.0f;
}

void ADeepSpaceCharacter::BeginPlay()
{
    Super::BeginPlay();

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

void ADeepSpaceCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
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
}

void ADeepSpaceCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    if (!Controller)
    {
        return;
    }
    AddMovementInput(GetActorForwardVector(), Axis.Y);
    AddMovementInput(GetActorRightVector(), Axis.X);
}

void ADeepSpaceCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    AddControllerYawInput(Axis.X);
    AddControllerPitchInput(-Axis.Y);
}

void ADeepSpaceCharacter::UpdateFocusedInteractable()
{
    FocusedInteractable = nullptr;

    if (!FirstPersonCamera)
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
    return FocusedInteractable ? FocusedInteractable->GetPrompt() : FText::GetEmpty();
}
