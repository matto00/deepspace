#include "Ship/ShipScreen.h"

#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"

AShipScreen::AShipScreen()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Screen = CreateDefaultSubobject<UWidgetComponent>(TEXT("Screen"));
    Screen->SetupAttachment(Root);
    ConfigurePanel(Screen, PanelWidthCm, DrawSizePixels);
}

void AShipScreen::ConfigurePanel(UWidgetComponent* Panel, float WidthCm, const FVector2D& DrawSizePixels)
{
    if (!Panel || DrawSizePixels.X <= 0.0f)
    {
        return;
    }

    Panel->SetWidgetSpace(EWidgetSpace::World);
    Panel->SetDrawAtDesiredSize(false);
    Panel->SetDrawSize(DrawSizePixels);
    Panel->SetPivot(FVector2D(0.5f, 0.5f));
    Panel->SetTwoSided(false);
    Panel->SetBlendMode(EWidgetBlendMode::Opaque);
    Panel->SetBackgroundColor(FLinearColor(0.012f, 0.030f, 0.036f, 1.0f));

    // A widget component's quad has its normal along +X (FWidget3DSceneProxy
    // builds it with TangentZ = (1,0,0)), but every wall-mounted fixture in
    // this ship faces -X at yaw 0 -- that is the convention
    // placement.resolve_mount yaws things by. Turning the panel round here
    // means a screen is mounted with exactly the same yaw as the thing it
    // sits on, rather than everyone remembering to add 180.
    Panel->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));

    // The pointer finds screens by tracing Visibility, the same channel the
    // reach trace uses. Blocking only that channel keeps a screen out of the
    // way of movement and of anything else the ship traces for.
    Panel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Panel->SetCollisionResponseToAllChannels(ECR_Ignore);
    Panel->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Panel->SetGenerateOverlapEvents(false);

    // One draw-size pixel is one unreal unit before scaling, so the panel's
    // real size is entirely a matter of the component's scale. Uniform, so
    // the widget's aspect ratio survives.
    Panel->SetRelativeScale3D(FVector(WidthCm / static_cast<float>(DrawSizePixels.X)));
}

void AShipScreen::SetPanelWidthCm(float WidthCm)
{
    PanelWidthCm = WidthCm;
    ConfigurePanel(Screen, WidthCm, DrawSizePixels);
}

void AShipScreen::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    SetPanelWidthCm(PanelWidthCm);
}
