#include "Ship/ShipCounterFrame.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Ship/ShipSubsystem.h"

namespace
{
    /** Fold Value back into [-Radius, Radius). Returns true if it moved. */
    bool WrapIntoField(double& Value, double Radius)
    {
        if (FMath::Abs(Value) < Radius)
        {
            return false;
        }
        const double Span = 2.0 * Radius;
        double Folded = FMath::Fmod(Value + Radius, Span);
        if (Folded < 0.0)
        {
            Folded += Span;
        }
        Value = Folded - Radius;
        return true;
    }
}

AShipCounterFrame::AShipCounterFrame()
{
    PrimaryActorTick.bCanEverTick = true;

    // The last tick group before the scene is handed to the render thread, so
    // the view cannot lag the ship by a frame. At cruise rates that lag would
    // be invisible and would therefore go unnoticed for months.
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);

    DistantStars = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DistantStars"));
    DistantStars->SetupAttachment(Root);

    NearStars = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("NearStars"));
    NearStars->SetupAttachment(Root);

    for (UInstancedStaticMeshComponent* Layer : { DistantStars.Get(), NearStars.Get() })
    {
        Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Layer->SetCastShadow(false);
    }
}

UInstancedStaticMeshComponent* AShipCounterFrame::GetDistantStars() const { return DistantStars; }
UInstancedStaticMeshComponent* AShipCounterFrame::GetNearStars() const { return NearStars; }

void AShipCounterFrame::BeginPlay()
{
    Super::BeginPlay();
    RebuildStarfield();
    SyncToShip();
}

void AShipCounterFrame::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    SyncToShip();
}

void AShipCounterFrame::RebuildStarfield()
{
    // Golden-angle spiral: even coverage where uniform random clumps. These
    // are placed in the actor's own space and never touched again -- the
    // actor's rotation is the whole of their motion.
    DistantStars->ClearInstances();
    const double GoldenAngle = UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
    for (int32 Index = 0; Index < DistantStarCount; ++Index)
    {
        const double Height = DistantStarCount > 1
            ? 1.0 - (static_cast<double>(Index) / (DistantStarCount - 1)) * 2.0
            : 0.0;
        const double Ring = FMath::Sqrt(FMath::Max(0.0, 1.0 - Height * Height));
        const double Theta = GoldenAngle * Index;
        const FVector Direction(FMath::Cos(Theta) * Ring, Height, FMath::Sin(Theta) * Ring);
        DistantStars->AddInstance(
            FTransform(FQuat::Identity, Direction * DistantStarRadius, FVector(DistantStarScale)));
    }

    NearStars->ClearInstances();
    NearStarPositions.Reset();

    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return;
    }
    const FShipFlightState& Flight = Ship->GetFlightState();

    // Scattered around wherever the ship happens to be, and then remembered as
    // real universe positions: it is the conversion, not the actor, that moves
    // them, which is what makes the parallax honest rather than a scrolling
    // texture.
    FRandomStream Stream(StarSeed);
    for (int32 Index = 0; Index < NearStarCount; ++Index)
    {
        const FVector Local(
            Stream.FRandRange(-NearFieldRadius, NearFieldRadius),
            Stream.FRandRange(-NearFieldRadius, NearFieldRadius),
            Stream.FRandRange(-NearFieldRadius, NearFieldRadius));
        NearStarPositions.Add(Flight.WorldToUniverse(Local));
        NearStars->AddInstance(FTransform(FQuat::Identity, Local, FVector(NearStarScale)), true);
    }
}

void AShipCounterFrame::SyncToShip()
{
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return;
    }
    const FShipFlightState& Flight = Ship->GetFlightState();

    // Rotation only. See the class comment: the translation belongs to each
    // object, through the conversion, in double precision.
    SetActorRotation(Flight.GetCounterFrameTransform().GetRotation());

    for (int32 Index = 0; Index < NearStarPositions.Num(); ++Index)
    {
        // The ship is permanently at the world origin, so a universe position
        // converted to world space is also its offset from the ship.
        FVector Local = Flight.UniverseToWorld(NearStarPositions[Index]);

        bool bWrapped = WrapIntoField(Local.X, NearFieldRadius);
        bWrapped |= WrapIntoField(Local.Y, NearFieldRadius);
        bWrapped |= WrapIntoField(Local.Z, NearFieldRadius);
        if (bWrapped)
        {
            // Rewritten only on a wrap: re-deriving the universe position every
            // frame would walk it a little further each time.
            NearStarPositions[Index] = Flight.WorldToUniverse(Local);
        }

        // World space, because the actor itself is rotated and the conversion
        // has already applied that rotation. Marking the render state dirty
        // once, on the last instance, rather than per instance.
        NearStars->UpdateInstanceTransform(
            Index, FTransform(FQuat::Identity, Local, FVector(NearStarScale)),
            /*bWorldSpace*/ true,
            /*bMarkRenderStateDirty*/ Index == NearStarPositions.Num() - 1,
            /*bTeleport*/ true);
    }
}
