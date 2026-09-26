#include "Ship/ShipNavScreen.h"

#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/InteractableComponent.h"
#include "UI/NavigationWidget.h"

namespace
{
    /** How far the rim reaches past the glass on every side, cm: enough that
     *  an eye on the bezel still finds the chart, not so much that the next
     *  screen along the desk (15 cm away at the nearest) answers for it. */
    constexpr float RimCm = 3.0f;

    /** The volume's depth behind the face, and its gap from it, cm. The gap
     *  is what keeps the panel, not the volume, first along any trace that
     *  lands on the glass. */
    constexpr float ReachDepthCm = 4.0f;
    constexpr float ReachGapCm = 0.5f;
}

AShipNavScreen::AShipNavScreen()
{
    // 68 cm at 12 px/cm, the laptop's density: the same text reads the same
    // size on both screens, and 816 x 576 fills the desk screen's 70 x 50 cm
    // face with a centimetre to spare all round.
    PanelWidthCm = 68.0f;
    DrawSizePixels = FVector2D(816.0f, 576.0f);

    // The starting seat, to be moved by eye in the chart chair playtest.
    // They are per instance, set by build_hauler.py, so a nudge is a level
    // rebuild and never a C++ one: back far enough to be on the starboard
    // chair, at its cushion's height, and leaning in to read.
    bUsable = true;
    UseDistanceCm = 100.0f;
    SeatHeightCm = 55.0f;
    ViewDistanceCm = 60.0f;

    Screen->SetWidgetClass(UNavigationWidget::StaticClass());

    Reach = CreateDefaultSubobject<UBoxComponent>(TEXT("Reach"));
    Reach->SetupAttachment(Root);
    // Movable, like the root: a Static child of a movable root never has its
    // world transform updated, and its collision stays where it was built.
    Reach->SetMobility(EComponentMobility::Movable);
    Reach->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Reach->SetCollisionResponseToAllChannels(ECR_Ignore);
    Reach->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Reach->SetGenerateOverlapEvents(false);
    Reach->SetCanEverAffectNavigation(false);

    Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
    Interactable->DisplayName = NSLOCTEXT("DeepSpace", "ChartName", "the chart");
    Interactable->InteractionVerb = NSLOCTEXT("DeepSpace", "ChartVerb", "Sit at");

    SetPanelWidthCm(PanelWidthCm);
    FitReach();
}

void AShipNavScreen::FitReach()
{
    if (!Reach || DrawSizePixels.X <= 0.0f)
    {
        return;
    }
    const float HalfWidth = PanelWidthCm * 0.5f;
    const float HalfHeight = HalfWidth * DrawSizePixels.Y / DrawSizePixels.X;

    // The panel's face is at the actor's origin and looks along -X, so
    // behind it is +X.
    Reach->SetBoxExtent(FVector(ReachDepthCm * 0.5f, HalfWidth + RimCm, HalfHeight + RimCm));
    Reach->SetRelativeLocation(FVector(ReachGapCm + ReachDepthCm * 0.5f, 0.0f, 0.0f));
}

void AShipNavScreen::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    FitReach();
}

void AShipNavScreen::BeginPlay()
{
    Super::BeginPlay();
    if (Interactable)
    {
        Interactable->OnInteracted.AddDynamic(this, &AShipNavScreen::HandleInteracted);
    }
}

void AShipNavScreen::HandleInteracted(AActor* InteractInstigator)
{
    if (ADeepSpaceCharacter* Character = Cast<ADeepSpaceCharacter>(InteractInstigator))
    {
        Character->UseScreen(this);
    }
}
