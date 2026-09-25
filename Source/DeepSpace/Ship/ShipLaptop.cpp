#include "Ship/ShipLaptop.h"

#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "UI/PowerAllocationWidget.h"

namespace
{
    // Centimetres. A 30 x 22 cm machine: small enough to be a thing left on a
    // table rather than a terminal installed in one.
    const FVector BaseSize(22.0f, 30.0f, 2.0f);
    const FVector LidSize(1.5f, 30.0f, 20.0f);
    const FVector LidCentre(9.0f, 0.0f, 12.0f);

    // Leaning back, so someone sitting at the bench looks squarely at it.
    // Negative pitch tips the lid's top toward +X, which is away from the
    // reader (the screen faces -X).
    constexpr float LidPitch = -15.0f;
}

AShipLaptop::AShipLaptop()
{
    PanelWidthCm = 26.0f;
    DrawSizePixels = FVector2D(480.0f, 320.0f);

    // Left movable: a static child of a movable root never has its world
    // transform updated when the actor moves, which silently leaves the
    // screen's collision body behind while the panel draws in the right place.
    Base = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Base"));
    Base->SetupAttachment(Root);

    Lid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
    Lid->SetupAttachment(Root);
    Lid->SetRelativeRotation(FRotator(LidPitch, 0.0f, 0.0f));

    // The screen rides the lid, just clear of its front face, so tilting the
    // lid tilts the screen with it.
    Screen->SetupAttachment(Lid);
    Screen->SetRelativeLocation(FVector(-LidSize.X * 0.5f - 0.2f, 0.0f, 0.0f));
    Screen->SetWidgetClass(UPowerAllocationWidget::StaticClass());

    SetPanelWidthCm(PanelWidthCm);
}

void AShipLaptop::FitBox(UStaticMeshComponent* Component, const FVector& SizeCm, const FVector& Centre)
{
    if (!Component)
    {
        return;
    }
    const UStaticMesh* Asset = Component->GetStaticMesh();
    if (!Asset)
    {
        return;
    }

    const FBox Bounds = Asset->GetBoundingBox();
    const FVector Extent = Bounds.GetSize();
    if (Extent.GetMin() <= UE_SMALL_NUMBER)
    {
        return;
    }

    const FVector Scale = SizeCm / Extent;
    Component->SetRelativeScale3D(Scale);
    Component->SetRelativeLocation(Centre - Bounds.GetCenter() * Scale);
}

void AShipLaptop::FitParts()
{
    FitBox(Base, BaseSize, FVector(0.0f, 0.0f, BaseSize.Z * 0.5f));

    // The lid carries its own rotation, so it is fitted about its own origin
    // and the rotation is left alone; SetRelativeLocation below would
    // otherwise be composed with the pitch.
    if (Lid && Lid->GetStaticMesh())
    {
        const FBox Bounds = Lid->GetStaticMesh()->GetBoundingBox();
        const FVector Extent = Bounds.GetSize();
        if (Extent.GetMin() > UE_SMALL_NUMBER)
        {
            const FVector Scale = LidSize / Extent;
            Lid->SetRelativeScale3D(Scale);
            const FVector Offset = Lid->GetRelativeRotation().RotateVector(-Bounds.GetCenter() * Scale);
            Lid->SetRelativeLocation(LidCentre + Offset);
        }
    }

    SetPanelWidthCm(PanelWidthCm);
}

void AShipLaptop::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    FitParts();
}

void AShipLaptop::BeginPlay()
{
    Super::BeginPlay();
    FitParts();
}
