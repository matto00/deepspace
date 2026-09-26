#include "Ship/ShipCounterFrame.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkyStarfield.h"
#include "UI/ShipScreenWidget.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"

namespace
{
    TAutoConsoleVariable<float> CVarMarkerPixels(
        TEXT("ds.Nav.MarkerPixels"), 6.0f,
        TEXT("The course marker's diameter on the dome, in pixels."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarStreakLength(
        TEXT("ds.Nav.StreakLength"), 40.0f,
        TEXT("How many times its own length a mote is stretched at the middle of a transit."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarStreakSweep(
        TEXT("ds.Nav.StreakSweep"), 5.0f,
        TEXT("How many widths of the near field sweep past the window over one transit."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarMoteFadeSpeed(
        TEXT("ds.Sky.MoteFadeSpeed"), 2000.0f,
        TEXT("Speed, m/s, by which the near-field motes have faded out entirely."),
        ECVF_Default);

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

    /** How big a mesh is and where its middle sits relative to its pivot,
     *  read from its bounds: meshes do not agree on pivot placement, so a
     *  point is centred on its bounds and never on an assumed origin. */
    struct FMeshFit
    {
        double Diameter = 100.0;
        FVector Centre = FVector::ZeroVector;
    };

    FMeshFit FitOf(const UStaticMeshComponent* Component)
    {
        const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
        if (!Mesh)
        {
            return FMeshFit();
        }
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        FMeshFit Fit;
        Fit.Diameter = Bounds.BoxExtent.GetMax() > 0.0 ? 2.0 * Bounds.BoxExtent.GetMax() : Fit.Diameter;
        Fit.Centre = Bounds.Origin;
        return Fit;
    }

    /** A point Diameter across with its middle at Where. */
    FTransform PointAt(const FVector& Where, double Diameter, const FMeshFit& Fit)
    {
        const double Scale = Diameter / Fit.Diameter;
        return FTransform(FQuat::Identity, Where - Fit.Centre * Scale, FVector(Scale));
    }

    /** The sky's flux exponent, read at use if the sky has registered it, so
     *  the background and the bodies are compressed alike; the projection's
     *  own default otherwise. */
    double FluxGamma()
    {
        if (const IConsoleVariable* Gamma = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.FluxGamma")))
        {
            return Gamma->GetFloat();
        }
        return FSkyViewParams().FluxGamma;
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
    DistantStars->NumCustomDataFloats = SkyMaterial::StarfieldCustomData;

    NearStars = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("NearStars"));
    NearStars->SetupAttachment(Root);

    for (UInstancedStaticMeshComponent* Layer : { DistantStars.Get(), NearStars.Get() })
    {
        Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Layer->SetCastShadow(false);
        Layer->bNeverDistanceCull = true;
    }
}

UInstancedStaticMeshComponent* AShipCounterFrame::GetDistantStars() const { return DistantStars; }
UInstancedStaticMeshComponent* AShipCounterFrame::GetNearStars() const { return NearStars; }
UStaticMeshComponent* AShipCounterFrame::GetCourseMarker() const { return CourseMarker; }
int32 AShipCounterFrame::GetBuiltForSerial() const { return BuiltForSerial; }
double AShipCounterFrame::GetDomeRadius() const { return DistantStarRadius; }

double AShipCounterFrame::GetPixelAngle() const
{
    const double Fallback = FSkyViewParams().PixelAngle;
    const UWorld* World = GetWorld();
    const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
    if (!Player || !Player->PlayerCameraManager)
    {
        return Fallback;
    }
    int32 Width = 0;
    int32 Height = 0;
    Player->GetViewportSize(Width, Height);
    const double FieldOfView = Player->PlayerCameraManager->GetFOVAngle();
    if (Width <= 0 || FieldOfView <= 0.0)
    {
        return Fallback;
    }
    return 2.0 * FMath::Tan(FMath::DegreesToRadians(FieldOfView) * 0.5) / Width;
}

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
    BuildDistantStars(GetPixelAngle());
    ScatterNearField();
}

void AShipCounterFrame::BuildDistantStars(double PixelAngle)
{
    // The galaxy, from procgen's stream on the universe's own seed: the same
    // sky for everyone who shares a seed, and the same sky after every jump.
    // Placed in the actor's own space, which is universe axes, and never
    // moved again: the actor's rotation is the whole of their motion.
    const TArray<FSkyStar> Stars = SkyStarfield::Generate(
        LocalSystem::StarfieldSeed(GetWorld(), static_cast<uint64>(StarSeed)), FMath::Max(0, DistantStarCount));

    const FMeshFit Fit = FitOf(DistantStars);
    const double Diameter = DistantStarPixels * PixelAngle * DistantStarRadius;
    TArray<FTransform> Transforms;
    Transforms.Reserve(Stars.Num());
    for (const FSkyStar& Star : Stars)
    {
        Transforms.Add(PointAt(Star.Direction * DistantStarRadius, Diameter, Fit));
    }

    DistantStars->ClearInstances();
    DistantStars->SetNumCustomDataFloats(SkyMaterial::StarfieldCustomData);
    DistantStars->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false,
                               /*bUpdateNavigation*/ false);

    // Only ever points, with no surface to keep honest: their brightness is
    // their flux compressed as the sky compresses everything's, and their
    // colour the one blackbody. One material and one draw call for all of
    // them, reading these four floats.
    const double Gamma = FluxGamma();
    float Data[SkyMaterial::StarfieldCustomData];
    for (int32 Index = 0; Index < Stars.Num(); ++Index)
    {
        const FLinearColor Colour = SkyColour::Blackbody(Stars[Index].TemperatureK);
        Data[SkyMaterial::CustomDataRed] = Colour.R;
        Data[SkyMaterial::CustomDataGreen] = Colour.G;
        Data[SkyMaterial::CustomDataBlue] = Colour.B;
        Data[SkyMaterial::CustomDataBrightness] = static_cast<float>(SkyProjection::Compress(Stars[Index].Flux, Gamma));
        DistantStars->SetCustomData(Index, Data, /*bMarkRenderStateDirty*/ false);
    }
    DistantStars->MarkRenderStateDirty();
    SizedForPixelAngle = PixelAngle;
}

void AShipCounterFrame::ScatterNearField()
{
    NearStars->ClearInstances();
    NearStarPositions.Reset();

    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return;
    }
    const FShipFlightState& Flight = Ship->GetFlightState();
    BuiltForSerial = Ship->GetJumpSerial();

    // Scattered around wherever the ship happens to be, and then remembered as
    // real universe positions: it is the conversion, not the actor, that moves
    // them, which is what makes the parallax honest rather than a scrolling
    // texture. Uniform in the cube, because dust has no preferred place --
    // the one thing out here that genuinely is uniform -- and a fresh scatter
    // after each jump, so the dust here is not the dust there.
    FGenStream Stream(GenSeed::Derive(LocalSystem::StarfieldSeed(GetWorld(), static_cast<uint64>(StarSeed)),
                                      GenSeed::Label("sky.motes"), static_cast<uint64>(BuiltForSerial)));
    for (int32 Index = 0; Index < NearStarCount; ++Index)
    {
        const FVector Local(
            (2.0 * Stream.Unit() - 1.0) * NearFieldRadius,
            (2.0 * Stream.Unit() - 1.0) * NearFieldRadius,
            (2.0 * Stream.Unit() - 1.0) * NearFieldRadius);
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

    // After a jump the near field would lie light years behind: scatter it
    // again round where the ship landed. The dome is not touched.
    if (Ship->GetJumpSerial() != BuiltForSerial)
    {
        ScatterNearField();
    }

    // Between stars the fold is all there is: the dome and the marker go,
    // and the motes become the streaks.
    const bool bInTransit = Ship->IsInTransit();
    DistantStars->SetVisibility(!bInTransit);

    // A star stays two pixels across whatever the view does -- a screen's
    // narrow framing, a smaller window -- so the dome is resized when the
    // pixel angle moves by more than a few percent, and not otherwise.
    const double PixelAngle = GetPixelAngle();
    if (SizedForPixelAngle <= 0.0 || FMath::Abs(PixelAngle / SizedForPixelAngle - 1.0) > 0.05)
    {
        BuildDistantStars(PixelAngle);
    }

    // The streaks: each mote stretched along the ship's forward, most at the
    // middle of the transit, and swept aft by a displacement that is purely
    // for show -- the ship itself is not moving between stars any faster
    // than it cruises. Scaling instances is the whole effect.
    const double Progress = bInTransit ? Ship->GetTransitProgress() : 0.0;
    const double Stretch = 1.0 + FMath::Max(0.0f, CVarStreakLength.GetValueOnGameThread()) * FMath::Sin(UE_DOUBLE_PI * Progress);
    const double Sweep = FMath::Max(0.0f, CVarStreakSweep.GetValueOnGameThread()) * NearFieldRadius
        * (1.0 - FMath::Cos(UE_DOUBLE_PI * Progress));
    const FVector MoteScale(NearStarScale * Stretch, NearStarScale, NearStarScale);

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

        // The sweep is drawn, never stored: it is not where the mote is.
        FVector Shown = Local;
        if (Sweep > 0.0)
        {
            Shown.X -= Sweep;
            WrapIntoField(Shown.X, NearFieldRadius);
        }

        // World space, because the actor itself is rotated and the conversion
        // has already applied that rotation -- which is also what makes the
        // stretch lie along the ship's forward rather than the universe's.
        // Marking the render state dirty once, on the last instance.
        NearStars->UpdateInstanceTransform(
            Index, FTransform(FQuat::Identity, Shown, MoteScale),
            /*bWorldSpace*/ true,
            /*bMarkRenderStateDirty*/ Index == NearStarPositions.Num() - 1,
            /*bTeleport*/ true);
    }

    FadeMotes(Flight.GetSpeed(), bInTransit);
    SyncCourseMarker(*Ship, PixelAngle);
}

void AShipCounterFrame::FadeMotes(double Speed, bool bInTransit)
{
    if (!MoteMaterial)
    {
        if (UMaterialInterface* Authored = NearStars->GetMaterial(0))
        {
            float AuthoredBrightness = 1.0f;
            if (Authored->GetScalarParameterValue(FHashedMaterialParameterInfo(SkyMaterial::Brightness), AuthoredBrightness))
            {
                MoteBrightness = AuthoredBrightness;
            }
            MoteMaterial = UMaterialInstanceDynamic::Create(Authored, this);
            NearStars->SetMaterial(0, MoteMaterial);
        }
    }

    // Speed is shown by the nearest thing that can honestly show it. At
    // cruise that is the motes; well before the drive has them wrapping every
    // frame and strobing, they step aside for the planets' own parallax,
    // which at those speeds is real. Between stars they are the streaks, and
    // stay.
    const double FadeSpeed = FMath::Max(1.0, CVarMoteFadeSpeed.GetValueOnGameThread() * 100.0);
    const double Fade = bInTransit ? 1.0 : FMath::Clamp(1.0 - Speed / FadeSpeed, 0.0, 1.0);
    if (MoteMaterial)
    {
        MoteMaterial->SetScalarParameterValue(SkyMaterial::Brightness, static_cast<float>(MoteBrightness * Fade));
    }
    NearStars->SetVisibility(Fade > 0.0);
}

void AShipCounterFrame::SyncCourseMarker(const UShipSubsystem& Ship, double PixelAngle)
{
    const TOptional<FVector> Course = Ship.GetCourseDirection();
    if (!Course)
    {
        if (CourseMarker)
        {
            CourseMarker->SetVisibility(false);
        }
        return;
    }

    if (!CourseMarker)
    {
        // Runtime, not a constructor component: it adds nothing to the placed
        // actor's saved hierarchy, so the level needs no rebuild for it and
        // there is no asset slot for the level script to fill. The mesh is
        // the dome's, the material the motes' M_SkyStar, tinted the ship's
        // accent -- the one teal thing through the glass.
        CourseMarker = NewObject<UStaticMeshComponent>(this, TEXT("CourseMarker"));
        CourseMarker->SetStaticMesh(DistantStars->GetStaticMesh());
        CourseMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CourseMarker->SetCastShadow(false);
        CourseMarker->bNeverDistanceCull = true;
        CourseMarker->SetupAttachment(Root);
        CourseMarker->RegisterComponent();

        UMaterialInterface* Star = MoteMaterial ? MoteMaterial->Parent.Get() : NearStars->GetMaterial(0);
        if (Star)
        {
            UMaterialInstanceDynamic* Tinted = UMaterialInstanceDynamic::Create(Star, this);
            Tinted->SetVectorParameterValue(SkyMaterial::Colour, UShipScreenWidget::Accent);
            CourseMarker->SetMaterial(0, Tinted);
        }
    }

    // On the dome at the star's true direction, in universe axes like the
    // actor's own space, and sized in pixels so it reads the same at the helm
    // and at a screen. Hidden between stars with the dome it sits on.
    const double Diameter = FMath::Max(0.0f, CVarMarkerPixels.GetValueOnGameThread()) * PixelAngle * GetDomeRadius();
    CourseMarker->SetRelativeTransform(PointAt(*Course * GetDomeRadius(), Diameter, FitOf(CourseMarker)));
    CourseMarker->SetVisibility(!Ship.IsInTransit());
}
