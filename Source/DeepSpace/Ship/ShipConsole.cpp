#include "Ship/ShipConsole.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Ship/InteractableComponent.h"
#include "Ship/ShipSubsystem.h"

AShipConsole::AShipConsole()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(Root);

    Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
    Interactable->DisplayName = NSLOCTEXT("DeepSpace", "EngConsole", "Engineering Console");
    Interactable->InteractionVerb = NSLOCTEXT("DeepSpace", "PowerOn", "Power on");
}

void AShipConsole::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    CentreMesh();
}

void AShipConsole::CentreMesh()
{
    if (!Mesh)
    {
        return;
    }

    const UStaticMesh* Asset = Mesh->GetStaticMesh();
    if (!Asset)
    {
        return;
    }

    const FVector Centre = Asset->GetBoundingBox().GetCenter();
    Mesh->SetRelativeLocation(-Centre * Mesh->GetRelativeScale3D());
}

void AShipConsole::BeginPlay()
{
    Super::BeginPlay();
    CentreMesh();
    Interactable->OnInteracted.AddDynamic(this, &AShipConsole::HandleInteracted);
}

void AShipConsole::HandleInteracted(AActor* InteractInstigator)
{
    bIsPowered = !bIsPowered;

    Interactable->InteractionVerb = bIsPowered
        ? NSLOCTEXT("DeepSpace", "PowerOff", "Power off")
        : NSLOCTEXT("DeepSpace", "PowerOn", "Power on");

    OnReadoutChanged();
}

FText AShipConsole::GetReadout() const
{
    if (!bIsPowered)
    {
        return NSLOCTEXT("DeepSpace", "ConsoleOffline", "OFFLINE");
    }

    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return NSLOCTEXT("DeepSpace", "ConsoleNoShip", "NO SIGNAL");
    }

    return FText::Format(
        NSLOCTEXT("DeepSpace", "ConsoleReadout", "DRAW {0} W / {1} W\nHEADROOM {2} W"),
        FText::AsNumber(FMath::RoundToInt(Ship->GetPowerDraw())),
        FText::AsNumber(FMath::RoundToInt(Ship->GetReactorOutput())),
        FText::AsNumber(FMath::RoundToInt(Ship->GetPowerHeadroom())));
}
