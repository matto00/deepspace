#include "Sky/ShipSky.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyStarfield.h"
#include "Universe/UniverseUnits.h"

DEFINE_LOG_CATEGORY_STATIC(LogShipSky, Log, All);

// Tunables, read at use and never cached, so a playtest moves them without a
// rebuild (Linux has no Live Coding). Settled values are written back as
// defaults once, at the end.
namespace
{
    /**
     * The galley's EV100, in the HDR visualisation's convention, which
     * manual exposure fixes for the whole game (sky decision 6). The spec's
     * default is "whatever auto exposure settles on in the galley"; nobody
     * has read that off a rendered galley yet, so this is the estimate: a
     * ceiling grid of ~2.5 cd lights 3 m apart puts about a lux on the
     * floor, ~0.3 cd/m^2 on average, and log2((0.3 / 0.18) / 1) is +0.7.
     * Read the real one with ds.Sky.ExposureMode 0 and VisualizeHDR, and put
     * it here.
     */
    TAutoConsoleVariable<float> CVarExposure(
        TEXT("ds.Sky.Exposure"), 0.7f,
        TEXT("The fixed exposure: the scene EV100 shown at middle grey, as the HDR visualisation reports it. ")
        TEXT("Higher is a darker picture."));

    TAutoConsoleVariable<int32> CVarExposureMode(
        TEXT("ds.Sky.ExposureMode"), 1,
        TEXT("0: the engine's own auto exposure, untouched (for reading the galley's EV). ")
        TEXT("1: manual at ds.Sky.Exposure (the design). ")
        TEXT("2: auto held within ds.Sky.ExposureRange of ds.Sky.Exposure (the fallback if manual feels dead); ")
        TEXT("a scene at exactly ds.Sky.Exposure looks the same in 1 and 2."));

    TAutoConsoleVariable<float> CVarExposureRange(
        TEXT("ds.Sky.ExposureRange"), 1.5f,
        TEXT("Mode 2 only: how many stops either side of ds.Sky.Exposure auto exposure may wander."));

    /**
     * The scene luminance of the sky's unit of emission: a white Lambert
     * surface 1 AU from a Sun. The projection's brightnesses are relative to
     * that and say nothing about how it compares with the ship's lamps, and
     * without this the only way to brighten the outside would be exposure,
     * which brightens the galley too. At the galley's +0.7, a picture scale
     * of 2^0.3, a sunlit Earth-albedo world shows about a pixel value of 1:
     * plainly brighter than the dim interior, not blown out.
     */
    TAutoConsoleVariable<float> CVarRadiance(
        TEXT("ds.Sky.Radiance"), 3.0f,
        TEXT("Scene luminance of a white surface 1 AU from a Sun. Scales every body, star and point outside."));

    /**
     * The sunlight on the deck at 1 AU, lux. pi times ds.Sky.Radiance, so a
     * white card in the sunbeam and a white world at the same distance from
     * the star are the same brightness -- as they are -- and the patch on
     * the galley table is about five times the room around it. Keep the two
     * in that ratio when tuning either.
     */
    TAutoConsoleVariable<float> CVarSunLux(
        TEXT("ds.Sky.SunLux"), 9.4f,
        TEXT("Sunlight through the glass at 1 AU from a Sun, lux; scaled by the star's compressed irradiance."));

    TAutoConsoleVariable<float> CVarFluxGamma(
        TEXT("ds.Sky.FluxGamma"), 0.5f,
        TEXT("Irradiance, point-boost and point-star compression: 1 is honest, 0.5 turns 900x into 30x."));

    TAutoConsoleVariable<float> CVarPointPixels(
        TEXT("ds.Sky.PointPixels"), 2.0f,
        TEXT("The smallest a body or star is drawn, px. Try 3 if two-pixel points shimmer under TSR."));

    /**
     * A resolved Sun's surface brightness in the sky's unit. Honestly it is
     * pi over the Sun's solid angle at 1 AU, about 46,000 -- and at the
     * galley's exposure that is a pixel value past 65,504, the ceiling of the
     * half-float scene colour, where it becomes infinity and the bloom goes
     * with it. 1,000 is blinding under bloom and puts a Sun more than a
     * decade under the ceiling, and the hottest star, held at
     * SkyProjection::MaxStarWarmth, under half of it. The projection's own
     * default of 1 would draw the Sun three times as bright as a planet,
     * which is not the brightest thing in the game. Stays 1,000 until the
     * developer's verdict on the glare (flight-feel decision 9).
     */
    TAutoConsoleVariable<float> CVarStarSurface(
        TEXT("ds.Sky.StarSurface"), 1000.0f,
        TEXT("A resolved Sun's surface brightness, in units of a white surface at 1 AU. Honest is ~46,000. ")
        TEXT("Candidates for the glare verdict: 1000, 700, 450."));

    /**
     * TEMPORARY, until the developer's verdict on the star's glare
     * (flight-feel decision 9): the exponent on a star's (T / T_sun)^4. 1 is
     * the ruled law, honest to SkyProjection::MaxStarWarmth; 0.5 is the
     * compressed T^2 the sky shipped with. Both live, so the two can be put
     * side by side in play. Once judged, the chosen law is written without
     * this and it is deleted.
     */
    TAutoConsoleVariable<float> CVarStarWarmthGamma(
        TEXT("ds.Sky.StarWarmthGamma"), 1.0f,
        TEXT("TEMPORARY. A star's surface goes as ((T / T_sun)^4)^this, capped at 8x a Sun's: 1 honest (the ruled law), ")
        TEXT("0.5 the old compressed T^2."));

    /** The faintest background star, so that it is just there against black
     *  at the galley's exposure and the brightest -- 400 times its flux, 20
     *  times its brightness at FluxGamma 0.5 -- glints. */
    TAutoConsoleVariable<float> CVarStarfieldFaint(
        TEXT("ds.Sky.StarfieldFaint"), 0.01f,
        TEXT("Emission of a flux-1 point at infinity, before ds.Sky.Radiance."));

    /**
     * The coarse face: continents and basins on rock, belts on a giant. It
     * is what a world is recognised by from across the system, so it is
     * strong enough to read at a few dozen pixels; the graph bounds it with
     * the detail at SkyMaterial::SurfaceMaxSwing however far this is pushed.
     */
    TAutoConsoleVariable<float> CVarMottle(
        TEXT("ds.Sky.Mottle"), 0.35f,
        TEXT("Amplitude of a resolved world's coarse face -- continents and basins, or a giant's belts."));

    /**
     * The fine bands, each of which fades in only once the screen can hold
     * it: this is what keeps a closing world showing new ground rather
     * than a bigger blur, and so what says how near it is. Every octave is
     * scaled alike (sky_material_contract.json's detail_weights, flat), so
     * this is the contrast of the ground arriving at the screen's scale at
     * any distance, the last hundred kilometres included. Each octave
     * weaker than the coarse face, so the continents still read under it.
     */
    TAutoConsoleVariable<float> CVarSurfaceDetail(
        TEXT("ds.Sky.SurfaceDetail"), 0.3f,
        TEXT("Amplitude of the finer bands of a world's face, which fade in as the world grows on screen."));

    /**
     * The relief: how steeply the ground the detail bands draw tilts, which
     * is what makes the terminator ragged and the ground close in read as
     * ground rather than as fog. Every band's slope is alike, so this is
     * the relief of whatever the screen holds at any distance; it shows
     * where the light is low and hardly at all under a high sun, as relief
     * does.
     */
    TAutoConsoleVariable<float> CVarRelief(
        TEXT("ds.Sky.Relief"), 0.2f,
        TEXT("Slope of a world's relief, per detail band. Shows at the terminator; 0 is a smooth sphere."));

    /** Craters, times each world's own Cratering: 1 is the look's word on
     *  how much a world has kept, 0 none anywhere. */
    TAutoConsoleVariable<float> CVarCraters(
        TEXT("ds.Sky.Craters"), 1.0f,
        TEXT("Scale on every world's craters, relief and albedo alike. 0 removes them."));

    TAutoConsoleVariable<float> CVarVeil(
        TEXT("ds.Sky.Veil"), 1.0f,
        TEXT("How strongly the glass reflects the lit room. At 1, a fully lit room hides stars fainter than flux 4, ")
        TEXT("about seven in eight; with the lights off every star shows."));

    TAutoConsoleVariable<float> CVarBloom(
        TEXT("ds.Sky.Bloom"), 0.675f,
        TEXT("Bloom intensity. Bloom is what makes a two-pixel star read as bright rather than merely white."));

    /** Above any volume the level template carries, whose priority is 0. */
    constexpr float ExposurePriority = 10.0f;

    FLinearColor AsParameter(const FVector& Vector)
    {
        return FLinearColor(static_cast<float>(Vector.X), static_cast<float>(Vector.Y), static_cast<float>(Vector.Z), 0.0f);
    }

    FSkyViewParams ViewParams(double PixelAngle)
    {
        FSkyViewParams Params;
        Params.PixelAngle = PixelAngle;
        Params.FluxGamma = CVarFluxGamma.GetValueOnGameThread();
        Params.MinPointPixels = AShipSky::PointPixels();
        Params.StarSurface = CVarStarSurface.GetValueOnGameThread();
        Params.StarWarmthGamma = FMath::Max(0.0f, CVarStarWarmthGamma.GetValueOnGameThread());
        return Params;
    }
}

AShipSky::AShipSky()
{
    PrimaryActorTick.bCanEverTick = true;

    // Beside the counter-frame, after the ship subsystem has stepped the
    // flight state for this frame, and before the scene goes to the render
    // thread. The order between the two actors does not matter: both ask
    // the subsystem, and neither reads the other.
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    SetRootComponent(Root);

    Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
    Sun->SetupAttachment(Root);
    Sun->SetMobility(EComponentMobility::Movable);
    Sun->SetCastShadows(true);
    // No atmosphere to scatter it: the sun is a light and nothing else.
    Sun->bAtmosphereSunLight = false;

    Exposure = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Exposure"));
    Exposure->SetupAttachment(Root);
    Exposure->SetMobility(EComponentMobility::Movable);
    Exposure->bUnbound = true;
    Exposure->Priority = ExposurePriority;
    Exposure->BlendWeight = 1.0f;

    NeighbourStars = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("NeighbourStars"));
    NeighbourStars->SetupAttachment(Root);
    NeighbourStars->SetMobility(EComponentMobility::Movable);
    NeighbourStars->NumCustomDataFloats = SkyMaterial::StarfieldCustomData;
    NeighbourStars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NeighbourStars->SetCastShadow(false);
    NeighbourStars->bNeverDistanceCull = true;
    NeighbourStars->bAffectDynamicIndirectLighting = false;
    NeighbourStars->bAffectDistanceFieldLighting = false;
    NeighbourStars->bVisibleInRayTracing = false;
    NeighbourStars->bVisibleInReflectionCaptures = false;
    NeighbourStars->bVisibleInRealTimeSkyCaptures = false;
}

void AShipSky::BeginPlay()
{
    Super::BeginPlay();

    // Everything outside the hull hangs off the counter-frame, and exactly
    // one thing applies the inverse rotation (flight spec decision 5). The
    // relative transform is forced to identity rather than kept, because the
    // projection's output is counter-frame local: a sky placed a metre off
    // would draw the universe a metre off.
    TActorIterator<AShipCounterFrame> Frame(GetWorld());
    if (Frame)
    {
        AttachToActor(*Frame, FAttachmentTransformRules::KeepRelativeTransform);
        SetActorRelativeTransform(FTransform::Identity);
    }
    else
    {
        UE_LOG(LogShipSky, Warning, TEXT("No AShipCounterFrame in the level; the sky will not turn with the ship."));
    }

    if (PointStarMaterial)
    {
        NeighbourStars->SetMaterial(0, PointStarMaterial);
    }
    if (BodyMesh)
    {
        NeighbourStars->SetStaticMesh(BodyMesh);
    }

    SyncToShip();
}

void AShipSky::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    SyncToShip();
}

void AShipSky::SyncToShip()
{
    // Asked every frame: a copy held here would be a second answer to
    // "which system am I in", and the day the two disagreed nobody would
    // know which was wrong.
    const UWorld* World = GetWorld();
    SyncTo(LocalSystem::Current(World), LocalSystem::Serial(World), LocalSystem::InTransit(World));
}

void AShipSky::SyncTo(const FSkySystem& System, int32 Serial, bool bInTransit)
{
    ApplyExposure();
    WriteParameters();

    if (Serial != BuiltForSerial)
    {
        RebuildFor(System);
        BuiltForSerial = Serial;
    }

    // Between stars there is nowhere to be near: no sun, no planets, no
    // neighbours, only the streaks and the dome the counter-frame keeps.
    if (bInTransit)
    {
        SetSkyVisible(false);
        return;
    }
    DrawFrom(System);
}

bool AShipSky::IsBuiltFor(const FSkySystem& System) const
{
    const FUniversePosition Star = System.Bodies.IsEmpty() ? FUniversePosition() : System.Bodies[0].Position;
    return Proxies.Num() == System.Bodies.Num() && BuiltForSystem == System.SystemId && BuiltForStar == Star;
}

void AShipSky::RebuildFor(const FSkySystem& System)
{
    for (UStaticMeshComponent* Proxy : Proxies)
    {
        if (Proxy)
        {
            Proxy->DestroyComponent();
        }
    }
    Proxies.Reset();
    BuiltForSystem = System.SystemId;
    BuiltForStar = System.Bodies.IsEmpty() ? FUniversePosition() : System.Bodies[0].Position;

    for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = System.Bodies[Index];

        // Named by index: a body's Id is a label with spaces in it, and an
        // object name may not have them.
        UStaticMeshComponent* Proxy = NewObject<UStaticMeshComponent>(
            this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), *FString::Printf(TEXT("Body%d"), Index)));
        Proxy->SetMobility(EComponentMobility::Movable);
        Proxy->SetupAttachment(Root);
        Proxy->SetStaticMesh(BodyMesh);

        // A 125,000 km sphere must touch nothing inside the ship: no
        // collision, no shadow, no bounce light, no distance-field lighting,
        // no ray tracing, and no reflection or sky capture -- a sky light
        // captures everything past 1.5 km, which is every proxy, and would
        // bake the opening planet into the deck's ambient light for good.
        // The materials are unlit, so the sun does not light them either
        // (sky decision 5).
        Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Proxy->SetGenerateOverlapEvents(false);
        Proxy->SetCastShadow(false);
        Proxy->bAffectDynamicIndirectLighting = false;
        Proxy->bAffectDistanceFieldLighting = false;
        Proxy->bVisibleInRayTracing = false;
        Proxy->bVisibleInReflectionCaptures = false;
        Proxy->bVisibleInRealTimeSkyCaptures = false;
        Proxy->bReceivesDecals = false;
        Proxy->bNeverDistanceCull = true;

        UMaterialInterface* Material = Body.Kind == ESkyBodyKind::Star ? StarMaterial.Get() : BodyMaterial.Get();
        if (Material)
        {
            UMaterialInstanceDynamic* Instance = Proxy->CreateDynamicMaterialInstance(0, Material);
            Instance->SetVectorParameterValue(SkyMaterial::Colour, Body.Colour);
            if (Body.Kind != ESkyBodyKind::Star)
            {
                Instance->SetVectorParameterValue(SkyMaterial::Rim, Body.Rim);
                // A world's face is its own for as long as the proxy lives:
                // set once, like its colour, and never per frame.
                Instance->SetVectorParameterValue(SkyMaterial::SurfaceSeed, ShipSky::SurfaceSeed(Body.SurfaceSeed, Body.BeltPairs));
                Instance->SetScalarParameterValue(SkyMaterial::Banding, ShipSky::Banding(Body.Surface));
            }
        }

        Proxy->RegisterComponent();
        Proxies.Add(Proxy);
    }
}

void AShipSky::DrawFrom(const FSkySystem& System)
{
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return;
    }
    if (!IsBuiltFor(System))
    {
        RebuildFor(System);
    }

    const FShipFlightState& Flight = Ship->GetFlightState();
    const double PixelAngle = GetPixelAngle();
    const FSkyViewParams Params = ViewParams(PixelAngle);
    LastFrame = SkyProjection::Project(System, Flight.GetUniversePosition(), Params);

    SetSkyVisible(true);
    DrawBodies(System, LastFrame, Flight.GetCounterFrameTransform().GetRotation());
    DrawSun(System, LastFrame);
    DrawNeighbours(System, Flight.GetUniversePosition(), PixelAngle, Params.MinPointPixels);
}

void AShipSky::DrawBodies(const FSkySystem& System, const FSkyFrame& Frame, const FQuat& CounterFrameRotation)
{
    // The mesh's size and centre are read from its bounds, never assumed:
    // meshes in this project do not agree on where their pivot is.
    double MeshRadius = 1.0;
    FVector MeshCentre = FVector::ZeroVector;
    if (BodyMesh)
    {
        const FBox Box = BodyMesh->GetBoundingBox();
        MeshRadius = FMath::Max(Box.GetExtent().GetMax(), UE_DOUBLE_SMALL_NUMBER);
        MeshCentre = Box.GetCenter();
    }

    const float Radiance = CVarRadiance.GetValueOnGameThread();
    const float Mottle = CVarMottle.GetValueOnGameThread();
    const float Detail = CVarSurfaceDetail.GetValueOnGameThread();
    const float Relief = CVarRelief.GetValueOnGameThread();
    const float Craters = CVarCraters.GetValueOnGameThread();

    for (int32 Index = 0; Index < Proxies.Num(); ++Index)
    {
        UStaticMeshComponent* Proxy = Proxies[Index];
        const FSkyBodyView& View = Frame.Bodies[Index];
        const double Scale = View.ProxyRadius / MeshRadius;
        Proxy->SetRelativeTransform(FTransform(FQuat::Identity, View.ProxyLocation - MeshCentre * Scale, FVector(Scale)));

        UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Proxy->GetMaterial(0));
        if (!Instance)
        {
            continue;
        }
        // Brightness is written as the projection gives it: the resolve's
        // flux correction is already in it, and applying anything here but
        // the one uniform scale would break the point-to-disc continuity.
        Instance->SetScalarParameterValue(SkyMaterial::Brightness, static_cast<float>(View.Brightness * Radiance));
        if (System.Bodies[Index].Kind != ESkyBodyKind::Star)
        {
            Instance->SetScalarParameterValue(SkyMaterial::PointBlend, static_cast<float>(View.PointBlend));
            Instance->SetScalarParameterValue(SkyMaterial::Mottle, Mottle);
            Instance->SetScalarParameterValue(SkyMaterial::Detail, Detail);
            Instance->SetScalarParameterValue(SkyMaterial::Relief, Relief);
            Instance->SetScalarParameterValue(SkyMaterial::Cratering,
                static_cast<float>(System.Bodies[Index].Cratering) * Craters);
            // The material shades with world-space normals, and the proxy's
            // world is the counter-frame's rotation of universe axes. Asked
            // of the flight state rather than of the actor, so the phase is
            // this frame's whichever actor ticks first.
            Instance->SetVectorParameterValue(SkyMaterial::LightDirection,
                AsParameter(CounterFrameRotation.RotateVector(View.LightDirection)));
        }
    }
}

void AShipSky::DrawSun(const FSkySystem& System, const FSkyFrame& Frame)
{
    const int32 StarIndex = System.Bodies.IndexOfByPredicate([](const FSkyBody& Body) { return Body.Kind == ESkyBodyKind::Star; });
    if (StarIndex == INDEX_NONE)
    {
        if (Sun->IsVisible())
        {
            Sun->SetVisibility(false);
        }
        return;
    }
    const FSkyBody& Star = System.Bodies[StarIndex];
    if (!Sun->IsVisible())
    {
        Sun->SetVisibility(true);
    }

    // Light travels from the star toward the ship. Relative to the
    // counter-frame, so the patch on the deck crawls as the ship turns.
    Sun->SetRelativeRotation(FRotationMatrix::MakeFromX(-Frame.SunDirection).ToQuat());

    // Compressed irradiance, the one term decision 2 allows compressed, and
    // the eclipse: park in a planet's shadow and the deck goes dark but for
    // the ship's own lamps.
    Sun->SetIntensity(CVarSunLux.GetValueOnGameThread() * Frame.SunIrradiance * Frame.SunVisibleFraction);

    // An orange star makes a warm ship (sky open question 4).
    Sun->SetLightColor(Star.Colour);

    // The penumbra is the star's true width: a far sun casts hard shadows.
    Sun->SetLightSourceAngle(static_cast<float>(FMath::RadiansToDegrees(2.0 * Frame.Bodies[StarIndex].AngularRadius)));
}

void AShipSky::DrawNeighbours(const FSkySystem& System, const FUniversePosition& ShipPosition, double PixelAngle,
                              double PointPixels)
{
    const int32 Count = System.Neighbours.Num();
    if (NeighbourStars->GetInstanceCount() != Count)
    {
        NeighbourStars->ClearInstances();
        TArray<FTransform> Placeholders;
        Placeholders.Init(FTransform::Identity, Count);
        NeighbourStars->AddInstances(Placeholders, false, false, false);
    }
    if (Count == 0)
    {
        return;
    }

    double MeshDiameter = 1.0;
    if (BodyMesh)
    {
        MeshDiameter = FMath::Max(2.0 * BodyMesh->GetBoundingBox().GetExtent().GetMax(), UE_DOUBLE_SMALL_NUMBER);
    }
    const double Scale = ShipSky::PointDiameter(DomeRadius, PixelAngle, PointPixels) / MeshDiameter;

    // Direction only, on the dome, and from the ship rather than the star:
    // near the system's edge the two differ by degrees. A system with
    // neighbours and no star has nothing to refer them from but the star's
    // own frame, which is all LocalSystem gave.
    const FSkyBody* Star = System.Bodies.FindByPredicate([](const FSkyBody& Body) { return Body.Kind == ESkyBodyKind::Star; });
    const FVector StarFromShip = Star ? Star->Position - ShipPosition : FVector::ZeroVector;

    TArray<FTransform> Transforms;
    Transforms.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FSkyNeighbour Neighbour = ShipSky::NeighbourFromShip(System.Neighbours[Index], StarFromShip);
        Transforms.Emplace(FQuat::Identity, Neighbour.Direction * DomeRadius, FVector(Scale));

        const FLinearColor Colour = SkyColour::Blackbody(Neighbour.TemperatureK);
        float Data[SkyMaterial::StarfieldCustomData];
        Data[SkyMaterial::CustomDataRed] = Colour.R;
        Data[SkyMaterial::CustomDataGreen] = Colour.G;
        Data[SkyMaterial::CustomDataBlue] = Colour.B;
        Data[SkyMaterial::CustomDataBrightness] = PointStarBrightness(ShipSky::NeighbourFlux(Neighbour));
        NeighbourStars->SetCustomData(Index, TArrayView<const float>(Data, SkyMaterial::StarfieldCustomData), false);
    }
    NeighbourStars->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}

void AShipSky::ApplyExposure()
{
    FPostProcessSettings& Settings = Exposure->Settings;
    const int32 Mode = CVarExposureMode.GetValueOnGameThread();
    const float EV = CVarExposure.GetValueOnGameThread();

    // Mode 0 hands exposure back to the engine entirely, so what the HDR
    // visualisation reports is the galley's own EV and not this one.
    Settings.bOverride_AutoExposureMethod = Mode != 0;
    Settings.bOverride_AutoExposureBias = Mode != 0;
    Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = Mode == 1;
    Settings.bOverride_AutoExposureMinBrightness = Mode == 2;
    Settings.bOverride_AutoExposureMaxBrightness = Mode == 2;

    if (Mode == 1)
    {
        // Exactly ds.Sky.Exposure and nothing else: no camera's aperture,
        // shutter or ISO takes part.
        Settings.AutoExposureMethod = AEM_Manual;
        Settings.AutoExposureApplyPhysicalCameraExposure = false;
        Settings.AutoExposureBias = static_cast<float>(ShipSky::ManualExposureBias(EV));
    }
    else if (Mode == 2)
    {
        // The engine's default bias, pinned rather than inherited: a level
        // volume setting its own would shift the fallback off the design by
        // exactly that many stops. The brightness limits are white-point
        // EV100, ds.Sky.Exposure's convention, so a scene at ds.Sky.Exposure
        // looks the same here as under manual.
        const float Range = FMath::Max(CVarExposureRange.GetValueOnGameThread(), 0.0f);
        Settings.AutoExposureBias = static_cast<float>(ShipSky::AutoExposureDefaultBias);
        Settings.AutoExposureMethod = AEM_Histogram;
        Settings.AutoExposureMinBrightness = EV - Range;
        Settings.AutoExposureMaxBrightness = EV + Range;
    }

    // Bloom scales with total energy, so a near star glares and a far one
    // glints without any code; lens flares are a camera's, not an eye's.
    Settings.bOverride_BloomIntensity = true;
    Settings.BloomIntensity = CVarBloom.GetValueOnGameThread();
    Settings.bOverride_LensFlareIntensity = true;
    Settings.LensFlareIntensity = 0.0f;

    // The project's 0.8 compresses exactly the contrast the sky shows.
    Settings.bOverride_LocalExposureHighlightContrastScale = true;
    Settings.LocalExposureHighlightContrastScale = 1.0f;
    Settings.bOverride_LocalExposureShadowContrastScale = true;
    Settings.LocalExposureShadowContrastScale = 1.0f;
}

void AShipSky::WriteParameters()
{
    UWorld* World = GetWorld();
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!SkyParameters || !World || !Ship)
    {
        return;
    }
    UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(SkyParameters);
    if (!Instance)
    {
        return;
    }
    // Asked every frame and stored nowhere: the room's light is the power
    // split's, and a dimmed galley reflects less in the glass.
    const float InteriorLight = Ship->AreLightsOn() ? Ship->GetConsumerSatisfaction(ShipPower::Lights) : 0.0f;
    Instance->SetScalarParameterValue(SkyMaterial::InteriorLight, InteriorLight);
    Instance->SetScalarParameterValue(SkyMaterial::Veil, CVarVeil.GetValueOnGameThread());
}

void AShipSky::SetSkyVisible(bool bVisible)
{
    for (UStaticMeshComponent* Proxy : Proxies)
    {
        if (Proxy && Proxy->IsVisible() != bVisible)
        {
            Proxy->SetVisibility(bVisible);
        }
    }
    // The sun is shown by DrawSun, which alone knows whether there is one.
    if (!bVisible && Sun->IsVisible())
    {
        Sun->SetVisibility(false);
    }
    if (NeighbourStars->IsVisible() != bVisible)
    {
        NeighbourStars->SetVisibility(bVisible);
    }
}

double AShipSky::GetPixelAngle() const
{
    return ShipSky::ViewPixelAngle(GetWorld());
}

double AShipSky::GetDomeRadius() const
{
    return DomeRadius;
}

const FSkyFrame& AShipSky::GetLastFrame() const { return LastFrame; }
int32 AShipSky::GetBuiltForSerial() const { return BuiltForSerial; }
int32 AShipSky::GetProxyCount() const { return Proxies.Num(); }
UStaticMeshComponent* AShipSky::GetProxy(int32 Index) const { return Proxies.IsValidIndex(Index) ? Proxies[Index].Get() : nullptr; }
UDirectionalLightComponent* AShipSky::GetSun() const { return Sun; }
UPostProcessComponent* AShipSky::GetExposure() const { return Exposure; }
UInstancedStaticMeshComponent* AShipSky::GetNeighbourStars() const { return NeighbourStars; }

float AShipSky::PointStarBrightness(double Flux)
{
    const double Compressed = SkyProjection::Compress(Flux, CVarFluxGamma.GetValueOnGameThread());
    return static_cast<float>(Compressed * CVarStarfieldFaint.GetValueOnGameThread() * CVarRadiance.GetValueOnGameThread());
}

double AShipSky::PointPixels()
{
    return CVarPointPixels.GetValueOnGameThread();
}

// ---------------------------------------------------------------------------
// The pure half.

FSkyNeighbour ShipSky::NeighbourFromShip(const FSkyNeighbour& Neighbour, const FVector& StarFromShip)
{
    const FVector FromShip = StarFromShip + Neighbour.Direction * Neighbour.Distance;
    FSkyNeighbour Seen = Neighbour;
    Seen.Distance = FromShip.Size();
    if (Seen.Distance > 0.0)
    {
        Seen.Direction = FromShip / Seen.Distance;
    }
    return Seen;
}

double ShipSky::NeighbourFlux(const FSkyNeighbour& Neighbour)
{
    const double Ly = Neighbour.Distance / UniverseUnits::CmPerLightYear;
    if (Ly <= 0.0)
    {
        return SkyStarfield::MaxFlux;
    }
    const double Reach = FaintestFluxSunDistanceLy / Ly;
    return FMath::Clamp(Neighbour.Luminosity * Reach * Reach, 1.0, SkyStarfield::MaxFlux);
}

double ShipSky::PointDiameter(double Distance, double PixelAngle, double Pixels)
{
    return 2.0 * Distance * FMath::Tan(0.5 * Pixels * PixelAngle);
}

double ShipSky::PixelAngle(double FovDegrees, double WidthPixels)
{
    // Only what the view is missing is assumed. Headless there is a camera
    // with a field of view and no viewport to be wide: the camera's zoom is
    // still real, so it still counts.
    const double Fov = FovDegrees > 0.0 ? FovDegrees : FallbackFovDegrees;
    const double Width = WidthPixels > 0.0 ? WidthPixels : FallbackWidthPixels;
    return 2.0 * FMath::Tan(0.5 * FMath::DegreesToRadians(Fov)) / Width;
}

double ShipSky::ViewPixelAngle(const UWorld* World)
{
    const APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
    const APlayerCameraManager* Camera = Player ? Player->PlayerCameraManager.Get() : nullptr;
    int32 Width = 0;
    int32 Height = 0;
    if (Player)
    {
        Player->GetViewportSize(Width, Height);
    }
    return PixelAngle(Camera ? static_cast<double>(Camera->GetFOVAngle()) : 0.0, static_cast<double>(Width));
}

double ShipSky::ManualExposureBias(double SceneEV100)
{
    return AutoExposureDefaultBias - SceneEV100;
}

FLinearColor ShipSky::SurfaceSeed(uint64 Seed, double BeltPairs)
{
    const auto Bits = [Seed](int32 Shift) { return static_cast<float>((Seed >> Shift) & 0xFFFFull) / 65536.0f; };
    const float Span = static_cast<float>(SkyMaterial::SurfaceOffsetSpan);
    return FLinearColor(Bits(0) * Span, Bits(16) * Span, Bits(32) * Span, static_cast<float>(FMath::Max(BeltPairs, 0.0)));
}

float ShipSky::Banding(ESkySurface Surface)
{
    return Surface == ESkySurface::Banded ? 1.0f : 0.0f;
}

int32 ShipSky::FindBody(const FSkySystem& System, const FString& Which)
{
    const FString Trimmed = Which.TrimStartAndEnd();
    if (Trimmed.IsNumeric())
    {
        const int32 Index = FCString::Atoi(*Trimmed);
        return System.Bodies.IsValidIndex(Index) ? Index : INDEX_NONE;
    }
    const FName Name(*Trimmed);
    return System.Bodies.IndexOfByPredicate([&Name](const FSkyBody& Body) { return Body.Id == Name; });
}

TOptional<FNavPlacement> ShipSky::GotoPlacement(const FSkySystem& System, int32 Body, double AltitudeCm,
                                                const FUniversePosition& From, EGotoSide Side)
{
    if (!System.Bodies.IsValidIndex(Body))
    {
        return {};
    }
    const FSkyBody& Target = System.Bodies[Body];
    const FSkyBody* Star = System.Bodies.FindByPredicate([](const FSkyBody& Candidate) { return Candidate.Kind == ESkyBodyKind::Star; });

    // Out from the body toward where the ship will hang: the star, for a
    // world's day side, so it is seen full and lit; away from it for the
    // night side, so the world is dark with its star behind it; for a star,
    // back toward the ship.
    FVector Out = FVector::ZeroVector;
    if (Target.Kind != ESkyBodyKind::Star && Star)
    {
        Out = (Star->Position - Target.Position).GetSafeNormal() * (Side == EGotoSide::Night ? -1.0 : 1.0);
    }
    if (Out.IsNearlyZero())
    {
        Out = (From - Target.Position).GetSafeNormal();
    }
    if (Out.IsNearlyZero())
    {
        Out = -FVector::ForwardVector;
    }

    FNavPlacement Placement;
    Placement.Position = Target.Position + Out * (Target.Radius + FMath::Max(AltitudeCm, 0.0));

    // Facing it, with the system's up kept up so the orbits lie level.
    const FVector Nose = -Out;
    Placement.Orientation = FMath::Abs(Nose.Z) < 0.999
        ? FRotationMatrix::MakeFromXZ(Nose, FVector::UpVector).ToQuat()
        : FRotationMatrix::MakeFromX(Nose).ToQuat();
    return Placement;
}

// ---------------------------------------------------------------------------
// ds.Sky.Goto: the sky's one write path, a one-shot PlaceShip for tuning.

void AShipSky::Goto(UShipSubsystem& Ship, const FSkySystem& System, bool bInTransit, TConstArrayView<FString> Args,
                    FOutputDevice& Out)
{
    // A trailing "night" asks for the far side from the star; it is taken
    // off before the rest is read, so a body named "night" still needs its
    // altitude after it.
    TConstArrayView<FString> Rest = Args;
    ShipSky::EGotoSide Side = ShipSky::EGotoSide::Day;
    if (Rest.Num() >= 3 && Rest.Last().Equals(TEXT("night"), ESearchCase::IgnoreCase))
    {
        Side = ShipSky::EGotoSide::Night;
        Rest = Rest.Slice(0, Rest.Num() - 1);
    }
    if (Rest.Num() < 2)
    {
        Out.Log(TEXT("ds.Sky.Goto <body> <altitude_km> [night]: onto the body's day side, or its night side, ")
                TEXT("facing it. Bodies:"));
        for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
        {
            Out.Logf(TEXT("  %d  %s"), Index, *System.Bodies[Index].Id.ToString());
        }
        return;
    }
    if (bInTransit)
    {
        Out.Log(TEXT("ds.Sky.Goto: between stars; there is nothing to go to."));
        return;
    }

    // A body's Id can have spaces in it, so every argument but the last is
    // the body.
    const FString Which = FString::Join(Rest.Slice(0, Rest.Num() - 1), TEXT(" "));
    const double AltitudeKm = FCString::Atod(*Rest.Last());
    const int32 Body = ShipSky::FindBody(System, Which);
    const TOptional<FNavPlacement> Placement = ShipSky::GotoPlacement(
        System, Body, AltitudeKm * UniverseUnits::CmPerKm, Ship.GetFlightState().GetUniversePosition(), Side);
    if (!Placement)
    {
        Out.Logf(TEXT("ds.Sky.Goto: no body '%s' here (%d bodies)."), *Which, System.Bodies.Num());
        return;
    }
    Ship.PlaceShip(Placement->Position, Placement->Orientation);
    Out.Logf(TEXT("ds.Sky.Goto: %.0f km above %s%s."), AltitudeKm, *System.Bodies[Body].Id.ToString(),
             Side == ShipSky::EGotoSide::Night && System.Bodies[Body].Kind != ESkyBodyKind::Star ? TEXT(", night side") : TEXT(""));
}

namespace
{
    void Goto(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        UShipSubsystem* Ship = World ? World->GetSubsystem<UShipSubsystem>() : nullptr;
        if (!Ship)
        {
            Out.Log(TEXT("ds.Sky.Goto: no ship."));
            return;
        }
        AShipSky::Goto(*Ship, LocalSystem::Current(World), LocalSystem::InTransit(World), Args, Out);
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice GotoCommand(
        TEXT("ds.Sky.Goto"),
        TEXT("'ds.Sky.Goto <body> <altitude_km> [night]': place the ship above a body of this system, on its day ")
        TEXT("side -- or with 'night' its far side from the star, the world dark in the glare -- facing it, once; ")
        TEXT("the orientation is never held. <body> is an index or a name. Then 'ds.Nav.Target <body>' brackets it."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Goto));
}
