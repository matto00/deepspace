#include "Ship/ShipCounterFrame.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkyStarfield.h"
#include "UI/ShipScreenWidget.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"

DEFINE_LOG_CATEGORY_STATIC(LogCounterFrame, Log, All);

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

    // The dust's law is a playtest gate, not a decided default (flight-feel
    // decision 8): three questions in play -- does the drive's first notch
    // read as faster than cruise's top, does anything above the knee read as
    // faster than the knee, does anything read as the fold -- with these
    // live. Candidates: knee {1, 2}, top {2.5, 3, 3.5}, stretch {4, 8, 16}.
    TAutoConsoleVariable<float> CVarDustKnee(
        TEXT("ds.Sky.DustKnee"), static_cast<float>(ShipDust::DefaultKnee / 1.0e5),
        TEXT("km/s. The dust is honest up to this speed, and a representation above it."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarDustTop(
        TEXT("ds.Sky.DustTop"), static_cast<float>(ShipDust::DefaultDustTop / 1.0e5),
        TEXT("km/s. The speed the dust is seen to stream at when the ship is at the drive's top."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarDustStretch(
        TEXT("ds.Sky.DustStretch"), static_cast<float>(ShipDust::DefaultStretch),
        TEXT("How many times its width a mote is drawn along the velocity at the drive's top; 1 at the knee."),
        ECVF_Default);

    /** cm/s from a km/s console variable. */
    double CmPerSecond(const TAutoConsoleVariable<float>& KmPerSecond)
    {
        return FMath::Max(0.0f, KmPerSecond.GetValueOnGameThread()) * 1.0e5;
    }

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

    /** Whether a layer draws with the given material, or a runtime copy of
     *  it: compared by the base material, which a dynamic instance keeps. */
    bool DrawsWith(const UInstancedStaticMeshComponent* Layer, const TCHAR* Path)
    {
        const UMaterialInterface* Material = Layer ? Layer->GetMaterial(0) : nullptr;
        const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
        return Base && Base->GetPathName() == Path;
    }

    /**
     * Nothing outside the hull may light the inside or be seen in it, except
     * the one sun (sky decision 5). Reflection and sky-light captures would
     * otherwise bake the dome, the motes and the marker into the cockpit's
     * surfaces as a faint glow nobody placed there -- the sky's proxies and
     * neighbours are already kept out the same way.
     */
    void KeepOutOfTheInterior(UPrimitiveComponent* Component)
    {
        Component->bAffectDynamicIndirectLighting = false;
        Component->bAffectDistanceFieldLighting = false;
        Component->bVisibleInRayTracing = false;
        Component->bVisibleInReflectionCaptures = false;
        Component->bVisibleInRealTimeSkyCaptures = false;
    }
}

double ShipDust::LogFraction(double Speed, double Knee, double Top)
{
    const double K = FMath::Max(Knee, UE_DOUBLE_SMALL_NUMBER);
    if (Speed <= K || Top <= K)
    {
        return 0.0;
    }
    return FMath::Clamp(FMath::Loge(Speed / K) / FMath::Loge(Top / K), 0.0, 1.0);
}

double ShipDust::SeenSpeed(double Speed, double Knee, double DustTop, double Top)
{
    const double V = FMath::Max(Speed, 0.0);
    const double K = FMath::Max(Knee, UE_DOUBLE_SMALL_NUMBER);
    if (V <= K)
    {
        return V;
    }
    return K * FMath::Pow(FMath::Max(DustTop, K) / K, LogFraction(V, K, Top));
}

double ShipDust::Stretch(double Speed, double Knee, double Top, double MaxStretch)
{
    return FMath::Pow(FMath::Max(MaxStretch, 1.0), LogFraction(Speed, Knee, Top));
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
        KeepOutOfTheInterior(Layer);
    }
}

UInstancedStaticMeshComponent* AShipCounterFrame::GetDistantStars() const { return DistantStars; }
UInstancedStaticMeshComponent* AShipCounterFrame::GetNearStars() const { return NearStars; }
UStaticMeshComponent* AShipCounterFrame::GetCourseMarker() const { return CourseMarker; }
int32 AShipCounterFrame::GetBuiltForSerial() const { return BuiltForSerial; }
TConstArrayView<FVector> AShipCounterFrame::GetDustField() const { return DustField; }
double AShipCounterFrame::GetDomeRadius() const { return DistantStarRadius; }

double AShipCounterFrame::GetPixelAngle() const
{
    return ShipSky::ViewPixelAngle(GetWorld());
}

TArray<FString> AShipCounterFrame::FindMaterialProblems() const
{
    TArray<FString> Problems;
    const auto Named = [](const UInstancedStaticMeshComponent* Layer)
    {
        const UMaterialInterface* Material = Layer ? Layer->GetMaterial(0) : nullptr;
        return Material ? Material->GetPathName() : FString(TEXT("no material"));
    };
    if (!DrawsWith(DistantStars, SkyMaterial::StarfieldPath))
    {
        Problems.Add(FString::Printf(
            TEXT("DistantStars draws with %s, not %s: it ignores each star's colour and brightness, so every star draws alike"),
            *Named(DistantStars), SkyMaterial::StarfieldPath));
    }
    if (!DrawsWith(NearStars, SkyMaterial::StarPath))
    {
        Problems.Add(FString::Printf(
            TEXT("NearStars draws with %s, not %s: the course marker cannot be tinted"),
            *Named(NearStars), SkyMaterial::StarPath));
    }
    return Problems;
}

void AShipCounterFrame::BeginPlay()
{
    Super::BeginPlay();
    for (const FString& Problem : FindMaterialProblems())
    {
        UE_LOG(LogCounterFrame, Warning, TEXT("%s: %s. Rebuild the level (Tools/build_hauler.py)."), *GetName(), *Problem);
    }
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
    // As big as the sky draws a neighbour, through the sky's own size and
    // its own formula: the smallest a point can honestly be, sized in pixels
    // so the dome's radius can move without changing it.
    const double PointPixels = AShipSky::PointPixels();
    const double Diameter = ShipSky::PointDiameter(DistantStarRadius, PixelAngle, PointPixels);
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
    // the sky's own point brightness of their flux, and their colour the one
    // blackbody. Through AShipSky::PointStarBrightness and nothing else, so a
    // neighbour and a background star of the same flux are the same point:
    // a destination is found in the backdrop, never lost in it or shouting
    // over it. One material and one draw call for all of them, reading these
    // four floats.
    float Data[SkyMaterial::StarfieldCustomData];
    for (int32 Index = 0; Index < Stars.Num(); ++Index)
    {
        const FLinearColor Colour = SkyColour::Blackbody(Stars[Index].TemperatureK);
        Data[SkyMaterial::CustomDataRed] = Colour.R;
        Data[SkyMaterial::CustomDataGreen] = Colour.G;
        Data[SkyMaterial::CustomDataBlue] = Colour.B;
        Data[SkyMaterial::CustomDataBrightness] = AShipSky::PointStarBrightness(Stars[Index].Flux);
        DistantStars->SetCustomData(Index, Data, /*bMarkRenderStateDirty*/ false);
    }
    DistantStars->MarkRenderStateDirty();
    SizedForPixelAngle = PixelAngle;
    SizedForPointPixels = PointPixels;
    BrightenedFaintest = AShipSky::PointStarBrightness(1.0);
    BrightenedBrightest = AShipSky::PointStarBrightness(SkyStarfield::MaxFlux);
}

void AShipCounterFrame::ScatterNearField()
{
    NearStars->ClearInstances();
    DustField.Reset();

    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return;
    }
    const FShipFlightState& Flight = Ship->GetFlightState();
    BuiltForSerial = Ship->GetJumpSerial();
    DustAnchor = Flight.GetUniversePosition();

    // Scattered round wherever the ship happens to be, in field space: a
    // cube in universe axes, so turning the ship turns the view of the dust
    // and never reshuffles it. Uniform in the cube, because dust has no
    // preferred place -- the one thing out here that genuinely is uniform --
    // and a fresh scatter after each jump, so the dust here is not the dust
    // there.
    FGenStream Stream(GenSeed::Derive(LocalSystem::StarfieldSeed(GetWorld(), static_cast<uint64>(StarSeed)),
                                      GenSeed::Label("sky.motes"), static_cast<uint64>(BuiltForSerial)));
    for (int32 Index = 0; Index < NearStarCount; ++Index)
    {
        const FVector Offset(
            (2.0 * Stream.Unit() - 1.0) * NearFieldRadius,
            (2.0 * Stream.Unit() - 1.0) * NearFieldRadius,
            (2.0 * Stream.Unit() - 1.0) * NearFieldRadius);
        DustField.Add(Offset);
        NearStars->AddInstance(FTransform(FQuat::Identity, Flight.UniverseDirectionToWorld(Offset), FVector(NearStarScale)),
                               /*bWorldSpace*/ true);
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

    // A star stays ds.Sky.PointPixels across whatever the view does -- a screen's
    // narrow framing, a smaller window -- so the dome is resized when the
    // pixel angle moves by more than a few percent, and not otherwise.
    const double PixelAngle = GetPixelAngle();
    if (SizedForPixelAngle <= 0.0 || FMath::Abs(PixelAngle / SizedForPixelAngle - 1.0) > 0.05)
    {
        BuildDistantStars(PixelAngle);
    }
    else if (AShipSky::PointStarBrightness(1.0) != BrightenedFaintest
             || AShipSky::PointStarBrightness(SkyStarfield::MaxFlux) != BrightenedBrightest
             || AShipSky::PointPixels() != SizedForPointPixels)
    {
        // A tuning CVar moved: the dome must follow it the frame the
        // neighbours do, or a playtest compares one against the other stale.
        BuildDistantStars(SizedForPixelAngle);
    }

    AdoptMoteMaterial();
    AdvanceDust(*Ship);
    DrawDust(*Ship);
    SyncCourseMarker(*Ship, PixelAngle);
}

void AShipCounterFrame::AdvanceDust(const UShipSubsystem& Ship)
{
    const FShipFlightState& Flight = Ship.GetFlightState();
    const FUniversePosition Here = Flight.GetUniversePosition();
    // Through the chunk index, never the offsets (ADR 0007).
    const FVector Moved = Here - DustAnchor;
    DustAnchor = Here;

    // Between stars the seen speed is not used and the field does not
    // stream: the fold's sweep is the whole of the motion, as it always
    // was, and whatever the ship is still shedding from the drive as the
    // fold opens would otherwise race the dust past the streaks.
    if (Ship.IsInTransit() || Moved.IsZero())
    {
        return;
    }

    // The field moves against the ship's true path -- along the nose under
    // the drive, along cruise's slide after a turn -- by the true distance up
    // to the knee, which is exactly the parallax of dust at universe
    // positions, and by the seen distance above it. The ship's speed, not
    // this frame's distance over its length, picks the scale: a placed ship
    // has no speed, and moves the dust as far as it was moved.
    const double Speed = Flight.GetSpeed();
    const double Seen = ShipDust::SeenSpeed(Speed, CmPerSecond(CVarDustKnee), CmPerSecond(CVarDustTop),
                                            Flight.GetLimits().DriveTop);
    const FVector Advance = Speed > 0.0 ? Moved * (Seen / Speed) : Moved;
    for (FVector& Offset : DustField)
    {
        Offset -= Advance;
        WrapIntoField(Offset.X, NearFieldRadius);
        WrapIntoField(Offset.Y, NearFieldRadius);
        WrapIntoField(Offset.Z, NearFieldRadius);
    }
}

void AShipCounterFrame::DrawDust(const UShipSubsystem& Ship)
{
    const FShipFlightState& Flight = Ship.GetFlightState();
    const int32 Count = FMath::Min(DustField.Num(), NearStars->GetInstanceCount());
    NearStars->SetVisibility(Count > 0);

    if (Ship.IsInTransit())
    {
        // The streaks: each mote stretched along the ship's forward, most at
        // the middle of the transit, and swept aft by a displacement that is
        // purely for show -- the ship itself is not moving between stars any
        // faster than it cruises. Exactly the formula the streaks had when
        // the dust held universe positions, applied to where the dust is
        // now: the mote's offset turned into ship axes and wrapped in the
        // ship's own cube, swept, and stretched (DeepSpace.Ship.CounterFrameJump
        // pins the shape).
        const double Progress = Ship.GetTransitProgress();
        const double Stretch = 1.0 + FMath::Max(0.0f, CVarStreakLength.GetValueOnGameThread()) * FMath::Sin(UE_DOUBLE_PI * Progress);
        const double Sweep = FMath::Max(0.0f, CVarStreakSweep.GetValueOnGameThread()) * NearFieldRadius
            * (1.0 - FMath::Cos(UE_DOUBLE_PI * Progress));
        const FVector MoteScale(NearStarScale * Stretch, NearStarScale, NearStarScale);

        for (int32 Index = 0; Index < Count; ++Index)
        {
            // Drawn, never stored: neither the ship-axis wrap nor the sweep
            // is where the dust is.
            FVector Shown = Flight.UniverseDirectionToWorld(DustField[Index]);
            WrapIntoField(Shown.X, NearFieldRadius);
            WrapIntoField(Shown.Y, NearFieldRadius);
            WrapIntoField(Shown.Z, NearFieldRadius);
            if (Sweep > 0.0)
            {
                Shown.X -= Sweep;
                WrapIntoField(Shown.X, NearFieldRadius);
            }

            // World space, which is ship space -- the ship is the world
            // origin -- so the stretch lies along the ship's forward rather
            // than the universe's. The render state is marked dirty once, on
            // the last instance.
            NearStars->UpdateInstanceTransform(
                Index, FTransform(FQuat::Identity, Shown, MoteScale),
                /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ Index == Count - 1, /*bTeleport*/ true);
        }
        return;
    }

    // In flight: each mote where the dust is, through the ship's rotation,
    // and drawn long along the true velocity above the knee. A mote's own
    // X is stretched, so the velocity is turned onto X in universe axes and
    // the whole thing then through the ship's rotation with the offset.
    const FVector Velocity = Flight.GetVelocity();
    const double Speed = Velocity.Size();
    const double Stretch = ShipDust::Stretch(Speed, CmPerSecond(CVarDustKnee), Flight.GetLimits().DriveTop,
                                             FMath::Max(1.0f, CVarDustStretch.GetValueOnGameThread()));
    const FQuat Along = Stretch > 1.0 && Speed > 0.0
        ? FQuat::FindBetweenNormals(FVector::ForwardVector, Velocity / Speed)
        : FQuat::Identity;
    const FQuat Drawn = Flight.GetCounterFrameTransform().GetRotation() * Along;
    const FVector MoteScale(NearStarScale * Stretch, NearStarScale, NearStarScale);

    for (int32 Index = 0; Index < Count; ++Index)
    {
        NearStars->UpdateInstanceTransform(
            Index, FTransform(Drawn, Flight.UniverseDirectionToWorld(DustField[Index]), MoteScale),
            /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ Index == Count - 1, /*bTeleport*/ true);
    }
}

void AShipCounterFrame::AdoptMoteMaterial()
{
    if (MoteMaterial)
    {
        return;
    }
    // A runtime copy, left at the material's authored brightness at every
    // speed: the dust is shown whatever the ship does (flight-feel decision
    // 8), so the look is tuned where the material is authored. The copy
    // stays because the course marker is tinted from its parent.
    if (UMaterialInterface* Authored = NearStars->GetMaterial(0))
    {
        MoteMaterial = UMaterialInstanceDynamic::Create(Authored, this);
        NearStars->SetMaterial(0, MoteMaterial);
    }
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
        KeepOutOfTheInterior(CourseMarker);
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
