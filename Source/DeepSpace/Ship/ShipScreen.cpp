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
    Screen->SetWidgetSpace(EWidgetSpace::World);
    Screen->SetDrawAtDesiredSize(false);
    Screen->SetDrawSize(DrawSizePixels);
    Screen->SetPivot(FVector2D(0.5f, 0.5f));
    Screen->SetTwoSided(false);
    Screen->SetBlendMode(EWidgetBlendMode::Opaque);
    Screen->SetBackgroundColor(FLinearColor(0.012f, 0.030f, 0.036f, 1.0f));

    // A widget component's quad has its normal along +X (FWidget3DSceneProxy
    // builds it with TangentZ = (1,0,0)), but every wall-mounted fixture in
    // this ship faces -X at yaw 0 -- that is the convention
    // placement.resolve_mount yaws things by. Turning the panel round here
    // means a screen is mounted with exactly the same yaw as the console it
    // sits on, rather than everyone remembering to add 180.
    Screen->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));

    // The pointer finds screens by tracing Visibility, the same channel the
    // reach trace uses. Blocking only that channel keeps a screen out of the
    // way of movement and of anything else the ship traces for.
    Screen->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Screen->SetCollisionResponseToAllChannels(ECR_Ignore);
    Screen->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Screen->SetGenerateOverlapEvents(false);

    SetPanelWidthCm(PanelWidthCm);
}

void AShipScreen::SetPanelWidthCm(float WidthCm)
{
    PanelWidthCm = WidthCm;
    if (!Screen || DrawSizePixels.X <= 0.0f)
    {
        return;
    }

    // One draw-size pixel is one unreal unit before scaling, so the panel's
    // real size is entirely a matter of the component's scale. Uniform, so
    // the aspect ratio of the widget survives.
    Screen->SetDrawSize(DrawSizePixels);
    const float Scale = WidthCm / static_cast<float>(DrawSizePixels.X);
    Screen->SetRelativeScale3D(FVector(Scale));
}

void AShipScreen::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    SetPanelWidthCm(PanelWidthCm);
}
