#include "Ship/PilotSeat.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/InteractableComponent.h"

APilotSeat::APilotSeat()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    // Sized to envelop the pilot_seat prop (60 x 60, back to 125 cm), with a
    // little margin so the trace meets this box before the prop's surfaces.
    ReachVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ReachVolume"));
    ReachVolume->SetupAttachment(Root);
    ReachVolume->SetBoxExtent(FVector(40.0f, 35.0f, 65.0f));
    ReachVolume->SetRelativeLocation(FVector(0.0f, 0.0f, 65.0f));
    ReachVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ReachVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    ReachVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

    SeatAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("SeatAnchor"));
    SeatAnchor->SetupAttachment(Root);

    // Behind the seat: the prop's back reaches 30 cm aft of its centre.
    ExitPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ExitPoint"));
    ExitPoint->SetupAttachment(Root);
    ExitPoint->SetRelativeLocation(FVector(-80.0f, 0.0f, 0.0f));

    Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
    Interactable->DisplayName = NSLOCTEXT("DeepSpace", "PilotSeat", "Pilot Seat");
    Interactable->InteractionVerb = NSLOCTEXT("DeepSpace", "SitIn", "Sit in");
}

void APilotSeat::BeginPlay()
{
    Super::BeginPlay();
    Interactable->OnInteracted.AddDynamic(this, &APilotSeat::HandleInteracted);
}

void APilotSeat::HandleInteracted(AActor* InteractInstigator)
{
    if (ADeepSpaceCharacter* Character = Cast<ADeepSpaceCharacter>(InteractInstigator))
    {
        Character->SitIn(this);
    }
}

FTransform APilotSeat::GetSeatTransform() const
{
    return SeatAnchor->GetComponentTransform();
}

FVector APilotSeat::GetExitLocation() const
{
    return ExitPoint->GetComponentLocation();
}
