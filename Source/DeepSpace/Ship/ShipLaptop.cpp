#include "Ship/ShipLaptop.h"

#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/InteractableComponent.h"
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

    // How far the screen stands off the lid's front face, cm. Small, but it
    // must be larger than nothing: the lid blocks the Visibility channel the
    // pointer traces on, so a screen flush with it is unreachable.
    constexpr float ScreenClearance = 0.5f;
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

    // The screen stays on the unscaled root and is *placed* to match the lid
    // (FitParts), rather than parented to it. FitBox gives the lid a heavily
    // non-uniform scale -- roughly 0.015 x 0.30 x 0.20 -- and a child inherits
    // it: the quad and its collision box come out squashed, and the stand-off
    // below collapses to a fraction of a millimetre, burying the screen inside
    // the lid where the lid blocks the trace first. The console has always
    // hung its screen off Root for the same reason; this one did not, which
    // is why the console could be clicked and the laptop could not.
    Screen->SetWidgetClass(UPowerAllocationWidget::StaticClass());

    // Sitting down is how this one is used: its sliders are too fine to aim
    // at from across the galley (the spec's second addendum).
    bUsable = true;

    Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
    Interactable->DisplayName = NSLOCTEXT("DeepSpace", "LaptopName", "Laptop");
    Interactable->InteractionVerb = NSLOCTEXT("DeepSpace", "LaptopVerb", "Sit at");

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

    // Size the panel first: ConfigurePanel sets the panel's relative rotation
    // itself, to turn the quad's +X normal round to the -X every fixture in
    // this ship faces. Placing the screen before this call means that flip
    // overwrites the lid's tilt -- which leaves a vertical panel inside a
    // lid leaning 15 degrees, most of it behind a surface that blocks the
    // channel the pointer traces on. It looked fine and could not be clicked.
    SetPanelWidthCm(PanelWidthCm);

    // Now place it: tilted with the lid, and standing off its front face.
    if (Screen)
    {
        const FRotator Tilt(LidPitch, 0.0f, 0.0f);

        // Compose rather than replace: the flip must survive, or the screen
        // shows its back. Quaternion order applies the flip first.
        const FQuat Flip(FRotator(0.0f, 180.0f, 0.0f));
        Screen->SetRelativeRotation(FQuat(Tilt) * Flip);
        Screen->SetRelativeLocation(
            LidCentre + Tilt.RotateVector(FVector(-(LidSize.X * 0.5f + ScreenClearance), 0.0f, 0.0f)));
    }
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

    if (Interactable)
    {
        Interactable->OnInteracted.AddDynamic(this, &AShipLaptop::HandleInteracted);
    }
}

void AShipLaptop::HandleInteracted(AActor* InteractInstigator)
{
    if (ADeepSpaceCharacter* Character = Cast<ADeepSpaceCharacter>(InteractInstigator))
    {
        Character->UseScreen(this);
    }
}
