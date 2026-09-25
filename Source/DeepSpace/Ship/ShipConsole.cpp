#include "Ship/ShipConsole.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/WidgetComponent.h"
#include "Ship/InteractableComponent.h"
#include "Ship/ShipScreen.h"
#include "Ship/ShipSubsystem.h"
#include "UI/EngineeringConsoleWidget.h"

AShipConsole::AShipConsole()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(Root);

    Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
    Interactable->DisplayName = NSLOCTEXT("DeepSpace", "EngConsole", "Engineering Console");
    Interactable->InteractionVerb = NSLOCTEXT("DeepSpace", "LightsVerb", "Lights");

    // The panel's face points -X at yaw 0, so the screen stands off along -X.
    Screen = CreateDefaultSubobject<UWidgetComponent>(TEXT("Screen"));
    Screen->SetupAttachment(Root);
    Screen->SetWidgetClass(UEngineeringConsoleWidget::StaticClass());
    AShipScreen::ConfigurePanel(Screen, PanelWidthCm, DrawSizePixels);
    Screen->SetRelativeLocation(FVector(-ScreenStandoff, 0.0f, 0.0f));
}

void AShipConsole::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    CentreMesh();
    AShipScreen::ConfigurePanel(Screen, PanelWidthCm, DrawSizePixels);
    Screen->SetRelativeLocation(FVector(-ScreenStandoff, 0.0f, 0.0f));
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
    SyncPrompt();
}

void AShipConsole::SyncPrompt()
{
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    Interactable->InteractionVerb = (Ship && Ship->AreLightsOn())
        ? NSLOCTEXT("DeepSpace", "LightsOffVerb", "Lights off")
        : NSLOCTEXT("DeepSpace", "LightsOnVerb", "Lights on");
}

void AShipConsole::HandleInteracted(AActor* InteractInstigator)
{
    // E at the console is the same switch the screen's button is. The state
    // lives in the subsystem, so pressing one and looking at the other can
    // never show two different answers.
    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->SetLightsOn(!Ship->AreLightsOn());
    }
    SyncPrompt();
    OnReadoutChanged();
}

FText AShipConsole::GetReadout() const
{
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return NSLOCTEXT("DeepSpace", "ConsoleNoShip", "NO SIGNAL");
    }

    return FText::Format(
        NSLOCTEXT("DeepSpace", "ConsoleReadout", "DRAW {0} W / {1} W\nSPARE {2} W"),
        FText::AsNumber(FMath::RoundToInt(Ship->GetPowerDraw())),
        FText::AsNumber(FMath::RoundToInt(Ship->GetReactorOutput())),
        FText::AsNumber(FMath::RoundToInt(Ship->GetPowerHeadroom())));
}
