#include "Sky/ShipSky.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "DynamicRHI.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
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
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/WorldGround.h"
#include "Surface/WorldRelief.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "Universe/UniverseUnits.h"

DEFINE_LOG_CATEGORY_STATIC(LogShipSky, Log, All);

// Tunables, read at use and never cached, so a playtest moves them without a
// rebuild (Linux has no Live Coding). Settled values are written back as
// defaults once, at the end.
namespace
{
    TAutoConsoleVariable<float> CVarShadows(
        TEXT("ds.Sky.Shadows"), 1.0f,
        TEXT("The cast shadow's strength in M_SkyBody and M_SkyGround, 0..1: 0 draws the unshadowed look. Baked, so it costs nothing either way."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowMaps(
        TEXT("ds.Sky.ShadowMaps"), 1,
        TEXT("1 bakes each solid world's cast-shadow map, off the game thread, re-baking one only when its relief, its light or the width changes; 0 bakes none and drops those held. The ground's tiles keep their own (ds.Terrain.Shadows) either way."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowMapWidth(
        TEXT("ds.Sky.ShadowMapWidth"), SunShadowMap::DefaultWidth,
        TEXT("Columns of each world's cast-shadow map, a power of two from 256 to 8192 (4096: 8.8 km texels on Baemsekai IV). Changing it re-bakes every map."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowBakeTasks(
        TEXT("ds.Sky.ShadowBakeTasks"), 2,
        TEXT("Cast-shadow maps baking at once, a world a task, on worker threads at low priority: beside the terrain's 3 tile builds, and only while a system's maps bake; never the core count."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowUploadKB(
        TEXT("ds.Sky.ShadowUploadKB"), 512,
        TEXT("KB of landed cast-shadow maps handed to the render thread a frame, all maps together, in pieces of whole rows: a 4096-column map goes up over about two dozen frames, so no frame hitches (ruled 2026-09-28). A world reads its map once the last piece has gone."),
        ECVF_Default);

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

    /** The body mesh's radius and centre, read from its bounds, never
     *  assumed: meshes in this project do not agree on where their pivot is. */
    void MeshShape(const UStaticMesh* Mesh, double& OutRadius, FVector& OutCentre)
    {
        OutRadius = 1.0;
        OutCentre = FVector::ZeroVector;
        if (Mesh)
        {
            const FBox Box = Mesh->GetBoundingBox();
            OutRadius = FMath::Max(Box.GetExtent().GetMax(), UE_DOUBLE_SMALL_NUMBER);
            OutCentre = Box.GetCenter();
        }
    }

    FSkyViewParams ViewParams(double PixelAngle, double MeshRadius)
    {
        FSkyViewParams Params;
        Params.PixelAngle = PixelAngle;
        Params.ProxyMeshRadius = MeshRadius;
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
    // The cast shadow's maps: each world's key against the map held for it,
    // outside transit (between stars the system reads empty, and a map baked
    // for the system being left is still that system's), then land and
    // launch.
    if (!bInTransit)
    {
        SyncShadowMaps(System);
    }
    PumpShadowBakes(false);
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
        // Placed through the counter-frame, but never turned or scaled by it:
        // its rotation is the world's identity and its scale exactly the
        // projection's, the two a GPU instance transform keeps without
        // rounding (SkyProjection::RenderedScaleBits). The face turns with
        // the ship in the material instead, through BodyAxisX and BodyAxisY.
        Proxy->SetUsingAbsoluteRotation(true);
        Proxy->SetUsingAbsoluteScale(true);

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
    double MeshRadius = 1.0;
    FVector MeshCentre = FVector::ZeroVector;
    MeshShape(BodyMesh, MeshRadius, MeshCentre);
    const FSkyViewParams Params = ViewParams(PixelAngle, MeshRadius);
    LastFrame = SkyProjection::Project(System, Flight.GetUniversePosition(), Params);

    SetSkyVisible(true);
    DrawBodies(System, LastFrame, Flight.GetCounterFrameTransform().GetRotation());
    DrawSun(System, LastFrame);
    DrawNeighbours(System, Flight.GetUniversePosition(), PixelAngle, Params.MinPointPixels);
}

void AShipSky::DrawBodies(const FSkySystem& System, const FSkyFrame& Frame, const FQuat& CounterFrameRotation)
{
    double MeshRadius = 1.0;
    FVector MeshCentre = FVector::ZeroVector;
    MeshShape(BodyMesh, MeshRadius, MeshCentre);

    // Universe axes as the world will see them when this frame is drawn: the
    // counter-frame's rotation this frame (whichever actor ticks first), then
    // the sky's own placement under it. The proxies are laid out through the
    // same two rotations, so the face the material turns by these axes is
    // the face of the sphere where it is drawn.
    const FQuat Universe = CounterFrameRotation * Root->GetRelativeRotation().Quaternion();

    const float Radiance = CVarRadiance.GetValueOnGameThread();
    const float Mottle = CVarMottle.GetValueOnGameThread();
    const float Detail = CVarSurfaceDetail.GetValueOnGameThread();

    // The ground below 50 km over a solid world (landing decision 7): it
    // says which body it draws; that proxy is hidden, and its look copied
    // into the ground's material. The projection still computes the hidden
    // proxy, with its rendered floor, so the depth stack is unchanged. The
    // ground ticks first (its tick is this actor's prerequisite), so this is
    // the frame's own answer.
    AWorldGround* Ground = nullptr;
    for (TActorIterator<AWorldGround> It(GetWorld()); It && !Ground; ++It)
    {
        Ground = *It;
    }
    const FName GroundBody = Ground ? Ground->GetDrawnBody() : NAME_None;

    for (int32 Index = 0; Index < Proxies.Num(); ++Index)
    {
        UStaticMeshComponent* Proxy = Proxies[Index];
        const FSkyBodyView& View = Frame.Bodies[Index];
        // Absolute rotation and scale: the identity and the projection's
        // renderable scale, exactly. The location is still the root's, in
        // universe axes, so the mesh's own centre -- world axes now -- is
        // taken back into them.
        const double Scale = View.ProxyScale;
        Proxy->SetRelativeTransform(FTransform(FQuat::Identity,
            View.ProxyLocation - Universe.UnrotateVector(MeshCentre * Scale), FVector(Scale)));
        // Every proxy's visibility is set here, never by SetSkyVisible(true),
        // so the one the ground has is not shown and hidden again each frame.
        const bool bGroundHasIt = GroundBody != NAME_None && System.Bodies[Index].Id == GroundBody;
        if (Proxy->IsVisible() == bGroundHasIt)
        {
            Proxy->SetVisibility(!bGroundHasIt);
        }

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
            Instance->SetScalarParameterValue(SkyMaterial::ReliefScale, static_cast<float>(ShipSky::ReliefScaleOf(System.Bodies[Index])));
            Instance->SetScalarParameterValue(SkyMaterial::Cratering, static_cast<float>(System.Bodies[Index].Cratering));
            // The material shades with world-space normals, and the proxy's
            // world is the counter-frame's rotation of universe axes. Asked
            // of the flight state rather than of the actor, so the phase is
            // this frame's whichever actor ticks first.
            Instance->SetVectorParameterValue(SkyMaterial::LightDirection,
                AsParameter(CounterFrameRotation.RotateVector(View.LightDirection)));
            // The face is fixed to the body, in universe axes; the mesh is
            // drawn unturned, so the material turns world directions into
            // universe axes with these rows. Material parameters are full
            // floats: 6e-8, where the instance transform's rotation kept 3e-5.
            Instance->SetVectorParameterValue(SkyMaterial::BodyAxisX, AsParameter(Universe.GetAxisX()));
            Instance->SetVectorParameterValue(SkyMaterial::BodyAxisY, AsParameter(Universe.GetAxisY()));
            // The cast shadow: the strength every frame; the map faded in
            // from the frame it landed, and it and its frame once it has.
            // The fade is the map's alone: the ground's vertices carry their
            // own shadow, which draws whether or not a map has landed.
            const FName Id = System.Bodies[Index].Id;
            const FShadowEntry* Shadow = ShadowEntries.Find(Id);
            const TObjectPtr<UTexture2DDynamic>* Map = ShadowTextures.Find(Id);
            const float Fade = Shadow && Map ? ShipSky::ShadowFade(GetWorld()->GetTimeSeconds() - Shadow->LandedAt) : 0.0f;
            Instance->SetScalarParameterValue(SkyMaterial::Shadows, ShipSky::ShadowStrength());
            Instance->SetScalarParameterValue(SkyMaterial::ShadowMapFade, Fade);
            if (Shadow && Map)
            {
                Instance->SetTextureParameterValue(SkyMaterial::ShadowMap, Map->Get());
                Instance->SetVectorParameterValue(SkyMaterial::ShadowFrameX, Shadow->Frame.X);
                Instance->SetVectorParameterValue(SkyMaterial::ShadowFrameZ, Shadow->Frame.Z);
            }
        }
        if (bGroundHasIt && Ground->GetGroundMaterialInstance())
        {
            ShipSky::CopyBodyLook(*Instance, *Ground->GetGroundMaterialInstance());
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
    // Shown, each proxy's visibility is DrawBodies', which runs next and
    // knows which body the ground draws (landing decision 7).
    for (UStaticMeshComponent* Proxy : Proxies)
    {
        if (!bVisible && Proxy && Proxy->IsVisible())
        {
            Proxy->SetVisibility(false);
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

void ShipSky::CopyBodyLook(UMaterialInstanceDynamic& From, UMaterialInstanceDynamic& To)
{
    for (const FName Name : { SkyMaterial::Colour, SkyMaterial::LightDirection, SkyMaterial::SurfaceSeed, SkyMaterial::ShadowFrameX, SkyMaterial::ShadowFrameZ })
    {
        To.SetVectorParameterValue(Name, From.K2_GetVectorParameterValue(Name));
    }
    for (const FName Name : { SkyMaterial::Brightness, SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::ReliefScale, SkyMaterial::Cratering, SkyMaterial::Shadows, SkyMaterial::ShadowMapFade })
    {
        To.SetScalarParameterValue(Name, From.K2_GetScalarParameterValue(Name));
    }
    To.SetTextureParameterValue(SkyMaterial::ShadowMap, From.K2_GetTextureParameterValue(SkyMaterial::ShadowMap));
}

double ShipSky::ReliefScaleOf(const FSkyBody& Body)
{
    if (Body.Ground == EGround::Solid)
    {
        return FWorldRelief(Body.Relief).SlopeScale();
    }
    return Body.Surface == ESkySurface::Banded ? GiantReliefScale : 0.0;
}

TOptional<FNavPlacement> ShipSky::GotoPlacement(const FSkySystem& System, int32 Body, double AltitudeCm,
                                                const FUniversePosition& From, EGotoSide Side, double DuskElevation)
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
    //
    // The night side is not the anti-sun line itself: that would hang the
    // world dead centre on the star's disc, a transit silhouette, which is
    // the easiest case to see and also the one most drowned in glare. It is
    // the .03 AU question's geometry, NightSideIsDrawn's and sky_probe
    // --night's: off the line by NightSideSlope, in the system's plane, to
    // +Y of a star at -X -- 3.8 degrees at the world, a 176-degree phase,
    // and the world about 3.7 degrees off the star's centre from the ship.
    FVector Out = FVector::ZeroVector;
    if (Target.Kind != ESkyBodyKind::Star && Star)
    {
        const FVector Sunward = (Star->Position - Target.Position).GetSafeNormal();
        Out = Sunward;
        if (Side == EGotoSide::Night)
        {
            FVector Aside = FVector::CrossProduct(FVector::UpVector, -Sunward).GetSafeNormal();
            if (Aside.IsNearlyZero())
            {
                Aside = FVector::CrossProduct(FVector::ForwardVector, -Sunward).GetSafeNormal();
            }
            Out = (-Sunward + Aside * NightSideSlope).GetSafeNormal();
        }
        else if (Side == EGotoSide::Dusk)
        {
            FVector Aside = FVector::CrossProduct(FVector::UpVector, Sunward).GetSafeNormal();
            if (Aside.IsNearlyZero())
            {
                Aside = FVector::CrossProduct(FVector::ForwardVector, Sunward).GetSafeNormal();
            }
            // The zenith DuskElevation short of square to the star.
            Out = (Aside * FMath::Cos(DuskElevation) + Sunward * FMath::Sin(DuskElevation)).GetSafeNormal();
        }
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

TOptional<ShipSky::FGotoRequest> ShipSky::ParseGoto(TConstArrayView<FString> Args)
{
    // A trailing "night" asks for the far side from the star, "dusk" for
    // ground under a low sun (EGotoSide::Dusk); either is always
    // taken off before the rest is read: "1 night", its altitude forgotten,
    // is then the usage, never night read as 0 km, onto the surface.
    FGotoRequest Request;
    TConstArrayView<FString> Rest = Args;
    if (!Rest.IsEmpty() && Rest.Last().Equals(TEXT("night"), ESearchCase::IgnoreCase))
    {
        Request.Side = EGotoSide::Night;
        Rest = Rest.Slice(0, Rest.Num() - 1);
    }
    else if (!Rest.IsEmpty() && Rest.Last().Equals(TEXT("dusk"), ESearchCase::IgnoreCase))
    {
        Request.Side = EGotoSide::Dusk;
        Rest = Rest.Slice(0, Rest.Num() - 1);
    }
    // The altitude must be a number: a body's name has spaces in it, so a
    // forgotten altitude would otherwise read the name's last word as 0 km.
    // Any number, exponent form included: the .03 AU case is 4500000 km,
    // and 4.5e6 is how a person writes it.
    if (Rest.Num() < 2 || !LexTryParseString(Request.AltitudeKm, *Rest.Last()) || !FMath::IsFinite(Request.AltitudeKm))
    {
        return {};
    }
    // A body's Id can have spaces in it, so every argument but the last is
    // the body.
    Request.Which = FString::Join(Rest.Slice(0, Rest.Num() - 1), TEXT(" "));
    return Request;
}

// ---------------------------------------------------------------------------
// ds.Sky.Goto: the sky's one write path, a one-shot PlaceShip for tuning.

void AShipSky::Goto(UShipSubsystem& Ship, const FSkySystem& System, bool bInTransit, TConstArrayView<FString> Args,
                    FOutputDevice& Out)
{
    const TOptional<ShipSky::FGotoRequest> Request = ShipSky::ParseGoto(Args);
    if (!Request)
    {
        Out.Log(TEXT("ds.Sky.Goto <body> <altitude_km> [night|dusk]: onto the body's day side, its night side, ")
                TEXT("or under a low sun, facing it. Bodies:"));
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

    const FString& Which = Request->Which;
    const double AltitudeKm = Request->AltitudeKm;
    const ShipSky::EGotoSide Side = Request->Side;
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
             System.Bodies[Body].Kind == ESkyBodyKind::Star ? TEXT("")
             : Side == ShipSky::EGotoSide::Night ? TEXT(", night side")
             : Side == ShipSky::EGotoSide::Dusk ? TEXT(", under a low sun") : TEXT(""));
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
        TEXT("'ds.Sky.Goto <body> <altitude_km> [night|dusk]': place the ship above a body of this system, on its day ")
        TEXT("side -- or with 'night' its far side from the star, 3.8 degrees off the star's line as the .03 AU ")
        TEXT("case is, the world dark in the glare and not in transit; or with 'dusk' over ground where the star ")
        TEXT("stands 10 degrees high -- facing it, once; ")
        TEXT("the orientation is never held. <body> is an index or a name. Then 'ds.Nav.Target <body>' brackets it."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Goto));
}

void AShipSky::EndPlay(const EEndPlayReason::Type Reason)
{
    // Cancelled and let go: the bakes hold no this, and stop within a column.
    TArray<FName> Held;
    ShadowEntries.GetKeys(Held);
    for (const FName Body : Held)
    {
        DropShadowMap(Body);
    }
    ShadowDraining.Reset();
    ShadowUploads.Reset();
    Super::EndPlay(Reason);
}

void AShipSky::SyncShadowMaps(const FSkySystem& System)
{
    TSet<FName> Wanted;
    if (CVarShadowMaps.GetValueOnGameThread() != 0)
    {
        const UShipSubsystem* Ship = UShipSubsystem::Get(this);
        const FUniversePosition From = Ship ? Ship->GetFlightState().GetUniversePosition() : FUniversePosition();
        const TArray<int32> Order = ShipSky::ShadowBakeOrder(System, From);
        // The per-system cap: each world's width from what its key is made
        // of, in the system's own order, never the ship's.
        TArray<int32> BySystem = Order;
        BySystem.Sort();
        TArray<ShipSky::FShadowWorld> Worlds;
        for (const int32 Index : BySystem)
        {
            const FSkyBody& Body = System.Bodies[Index];
            const FSunShadowMap Shape = SunShadowMap::Shape(FReliefGround(Body.Relief), SkyProjection::SunLightOf(System, Index), 2);
            Worlds.Add({ Body.Relief.RadiusCm, Shape.Width > 0 ? Shape.PsiLo : 0.5 * UE_DOUBLE_PI });
        }
        const TArray<int32> Widths = ShipSky::CappedShadowWidths(Worlds, ShipSky::ShadowMapWidth(), ShipSky::ShadowSystemCapBytes,
            [](int32 W, int32 R) { return ShipSky::ShadowTextureBytes(W, R); });
        for (const int32 Index : Order)
        {
            const FSkyBody& Body = System.Bodies[Index];
            Wanted.Add(Body.Id);
            ShipSky::FShadowKey Key;
            Key.Relief = Body.Relief;
            Key.Sun = SkyProjection::SunLightOf(System, Index);
            Key.SteepestSlope = SunShadow::SteepestSlope(Body.Relief);
            Key.Width = Widths[BySystem.IndexOfByKey(Index)];
            const FShadowEntry* Held = ShadowEntries.Find(Body.Id);
            if (Held && ShipSky::SameShadowKey(Held->Key, Key))
            {
                continue;   // made from the same things: nothing to bake
            }
            DropShadowMap(Body.Id);
            ShadowEntries.Add(Body.Id).Key = Key;
            ShadowQueue.Add(Body.Id);
        }
    }
    TArray<FName> Gone;
    for (const TPair<FName, FShadowEntry>& Pair : ShadowEntries)
    {
        if (!Wanted.Contains(Pair.Key))
        {
            Gone.Add(Pair.Key);
        }
    }
    for (const FName Body : Gone)
    {
        DropShadowMap(Body);
    }
}

void AShipSky::DropShadowMap(FName Body)
{
    if (FShadowEntry* Entry = ShadowEntries.Find(Body))
    {
        if (Entry->Cancel.IsValid())
        {
            Entry->Cancel->store(true, std::memory_order_relaxed);
        }
        if (Entry->Task.IsValid())
        {
            ShadowDraining.Add(MoveTemp(Entry->Task));
        }
        ShadowEntries.Remove(Body);
    }
    ShadowQueue.Remove(Body);
    ShadowTextures.Remove(Body);
    ShadowTexturesMade.Remove(Body);
    KeptShadowMaps.Remove(Body);
    // Pieces already handed over hold the map themselves, and run before
    // the texture's release, which the collector queues behind them.
    ShadowUploads.RemoveAll([Body](const FShadowUpload& Upload) { return Upload.Body == Body; });
}

int32 AShipSky::GetShadowBakesPending() const
{
    int32 Pending = ShadowQueue.Num();
    for (const TPair<FName, FShadowEntry>& Pair : ShadowEntries)
    {
        Pending += Pair.Value.Task.IsValid() ? 1 : 0;
    }
    for (const FShadowUpload& Upload : ShadowUploads)
    {
        Pending += Upload.Next < Upload.Pieces.Num() ? 1 : 0;
    }
    return Pending;
}

int64 AShipSky::GetShadowUploadCpuBytes() const
{
    int64 Bytes = 0;
    for (const FShadowUpload& Upload : ShadowUploads)
    {
        Bytes += Upload.Map.IsValid() ? Upload.Map->Bytes() : 0;
    }
    return Bytes;
}

double AShipSky::GetSlowestShadowUploadRenderSeconds() const
{
    return 1.0e-9 * static_cast<double>(SlowestShadowUploadNs->load(std::memory_order_relaxed));
}

int32 AShipSky::GetShadowPiecesLeft(FName Body) const
{
    const FShadowUpload* Upload = ShadowUploads.FindByPredicate([Body](const FShadowUpload& Candidate) { return Candidate.Body == Body; });
    return Upload ? Upload->Pieces.Num() - Upload->Next : 0;
}

int32 AShipSky::GetShadowWidth(FName Body) const
{
    const FShadowEntry* Entry = ShadowEntries.Find(Body);
    return Entry ? Entry->Key.Width : 0;
}

void AShipSky::PumpShadowBakes(bool bWait)
{
    using FBakeTask = UE::Tasks::TTask<TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>>;
    if (bWait)
    {
        for (FBakeTask& Task : ShadowDraining)
        {
            Task.Wait();
        }
    }
    ShadowDraining.RemoveAll([](const FBakeTask& Task) { return Task.IsCompleted(); });

    // Launch: what was let go still holds a worker until it stops.
    const int32 Cap = FMath::Max(1, CVarShadowBakeTasks.GetValueOnGameThread());
    int32 Busy = ShadowDraining.Num() + (GetShadowBakesPending() - ShadowQueue.Num());
    while (Busy < Cap && ShadowQueue.Num() > 0)
    {
        const FName Body = ShadowQueue[0];
        ShadowQueue.RemoveAt(0);
        FShadowEntry& Entry = ShadowEntries.FindChecked(Body);
        Entry.Cancel = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);
        const ShipSky::FShadowKey Key = Entry.Key;
        const TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> Cancel = Entry.Cancel;
        Entry.Task = UE::Tasks::Launch(UE_SOURCE_LOCATION,
            [Key, Cancel]() -> TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>
            {
                const FReliefGround Ground(Key.Relief);
                return MakeShared<FSunShadowMap, ESPMode::ThreadSafe>(SunShadowMap::Bake(Ground, Key.Sun, Key.SteepestSlope, Key.Width, Cancel.Get()));
            }, UE::Tasks::ETaskPriority::BackgroundLow);
        ++Busy;
        ++ShadowBakesStarted;
    }

    // Land: an empty texture for each map that finished, and its pieces
    // queued. Nothing reads it yet.
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    if (bWait)
    {
        // A test's flush waits for the bakes before the landing is timed:
        // play never waits, and the wait is the bake's, not the landing's.
        for (TPair<FName, FShadowEntry>& Pair : ShadowEntries)
        {
            if (Pair.Value.Task.IsValid())
            {
                Pair.Value.Task.Wait();
            }
        }
    }
    const double Began = FPlatformTime::Seconds();
    for (TPair<FName, FShadowEntry>& Pair : ShadowEntries)
    {
        FShadowEntry& Entry = Pair.Value;
        if (!Entry.Task.IsValid())
        {
            continue;
        }
        if (!Entry.Task.IsCompleted())
        {
            continue;
        }
        const TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe> Map = Entry.Task.GetResult();
        Entry.Task = FBakeTask();
        if (!Map.IsValid() || Map->LevelCount() == 0)
        {
            continue;
        }
        UTexture2DDynamic* Texture = ShipSky::MakeShadowTexture(*Map, Pair.Key);
        if (!Texture)
        {
            continue;
        }
        ShadowTexturesMade.Add(Pair.Key, Texture);
        Entry.Frame = { ShipSky::ShadowFrameX(*Map), ShipSky::ShadowFrameZ(*Map) };
        FShadowUpload& Upload = ShadowUploads.AddDefaulted_GetRef();
        Upload.Body = Pair.Key;
        Upload.Map = Map;
        Upload.Pieces = ShipSky::ShadowUploadPieces(*Map, ShipSky::ShadowUploadBytesPerFrame());
        if (bKeepShadowMapsForTest)
        {
            KeptShadowMaps.Add(Pair.Key, Map);
        }
    }

    // This frame's pieces, oldest map first: at most the frame's bytes, and
    // at least one piece, so every map goes up. A map whose last piece has
    // gone is the world's to read from now, and fades in from now.
    int64 Left = ShipSky::ShadowUploadBytesPerFrame();
    bool bHanded = false;
    for (FShadowUpload& Upload : ShadowUploads)
    {
        if (Upload.Next >= Upload.Pieces.Num())
        {
            continue;
        }
        const TObjectPtr<UTexture2DDynamic>* Texture = ShadowTexturesMade.Find(Upload.Body);
        TArray<ShipSky::FShadowPiece> Handed;
        while (Upload.Next < Upload.Pieces.Num() && (!bHanded || Upload.Pieces[Upload.Next].Bytes <= Left))
        {
            Left -= Upload.Pieces[Upload.Next].Bytes;
            Handed.Add(Upload.Pieces[Upload.Next++]);
            bHanded = true;
        }
        if (Texture && Handed.Num() > 0)
        {
            ShipSky::UploadShadowPieces(*Texture->Get(), Upload.Map, MoveTemp(Handed), SlowestShadowUploadNs);
        }
        if (Upload.Next >= Upload.Pieces.Num() && Texture)
        {
            ShadowTextures.Add(Upload.Body, *Texture);
            if (FShadowEntry* Entry = ShadowEntries.Find(Upload.Body))
            {
                // In play the fade starts now; a test's flush lands it whole.
                Entry->LandedAt = bWait ? Now - ShipSky::ShadowFadeSeconds : Now;
            }
            Upload.Fence = MakeUnique<FRenderCommandFence>();
            Upload.Fence->BeginFence();
        }
        if (Left <= 0)
        {
            break;
        }
    }
    SlowestShadowLandSeconds = FMath::Max(SlowestShadowLandSeconds, FPlatformTime::Seconds() - Began);

    // Once the render thread has run every piece, the CPU copy is dead weight.
    for (int32 Index = ShadowUploads.Num() - 1; Index >= 0; --Index)
    {
        FShadowUpload& Upload = ShadowUploads[Index];
        if (!Upload.Fence.IsValid())
        {
            continue;
        }
        if (bWait)
        {
            Upload.Fence->Wait();
        }
        if (Upload.Fence->IsFenceComplete())
        {
            ShadowUploads.RemoveAt(Index);
        }
    }
}

void AShipSky::FlushShadowBakesForTest()
{
    do
    {
        PumpShadowBakes(true);
    }
    while (GetShadowBakesPending() > 0 || ShadowUploads.Num() > 0);
}

UTexture2DDynamic* AShipSky::GetShadowTexture(FName Body) const
{
    const TObjectPtr<UTexture2DDynamic>* Found = ShadowTextures.Find(Body);
    return Found ? Found->Get() : nullptr;
}

const FSunShadowMap* AShipSky::GetShadowMapForTest(FName Body) const
{
    const TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>* Found = KeptShadowMaps.Find(Body);
    return Found && Found->IsValid() ? Found->Get() : nullptr;
}

float ShipSky::ShadowStrength()
{
    return FMath::Clamp(CVarShadows.GetValueOnGameThread(), 0.0f, 1.0f);
}

float ShipSky::ShadowFade(double SecondsSinceLanded)
{
    return static_cast<float>(FMath::Clamp(SecondsSinceLanded / ShadowFadeSeconds, 0.0, 1.0));
}

int32 ShipSky::ShadowMapWidth()
{
    return static_cast<int32>(FMath::RoundUpToPowerOfTwo(static_cast<uint32>(FMath::Clamp(CVarShadowMapWidth.GetValueOnGameThread(), 256, 8192))));
}

bool ShipSky::SameShadowKey(const FShadowKey& A, const FShadowKey& B)
{
    return SameRelief(A.Relief, B.Relief) && A.Sun.Direction == B.Sun.Direction && A.Sun.AngularRadius == B.Sun.AngularRadius
        && A.SteepestSlope == B.SteepestSlope && A.Width == B.Width;
}

TArray<int32> ShipSky::ShadowBakeOrder(const FSkySystem& System, const FUniversePosition& Ship)
{
    TArray<int32> Order;
    for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
    {
        if (System.Bodies[Index].Ground == EGround::Solid && System.Bodies[Index].Kind != ESkyBodyKind::Star)
        {
            Order.Add(Index);
        }
    }
    Order.StableSort([&](int32 A, int32 B)
    {
        return Ship.DistanceTo(System.Bodies[A].Position) - System.Bodies[A].Radius < Ship.DistanceTo(System.Bodies[B].Position) - System.Bodies[B].Radius;
    });
    return Order;
}

FLinearColor ShipSky::ShadowFrameX(const FSunShadowMap& Map)
{
    return FLinearColor(static_cast<float>(Map.FrameX.X), static_cast<float>(Map.FrameX.Y), static_cast<float>(Map.FrameX.Z), static_cast<float>(Map.PsiLo));
}

FLinearColor ShipSky::ShadowFrameZ(const FSunShadowMap& Map)
{
    return FLinearColor(static_cast<float>(Map.FrameZ.X), static_cast<float>(Map.FrameZ.Y), static_cast<float>(Map.FrameZ.Z), static_cast<float>(Map.Step));
}

int64 ShipSky::ShadowLevelsBytes(int32 Width, int32 Rows)
{
    if (Width <= 0 || Rows <= 0)
    {
        return 0;
    }
    FSunShadowMap Shape;
    Shape.Width = Width;
    Shape.Rows = Rows;
    int64 Bytes = 0;
    for (int32 Level = 0;; ++Level)
    {
        Bytes += static_cast<int64>(Shape.WidthAt(Level)) * Shape.RowsAt(Level) * static_cast<int64>(sizeof(uint16));
        if (Shape.WidthAt(Level) <= 1 && Shape.RowsAt(Level) <= 1)
        {
            return Bytes;
        }
    }
}

int64 ShipSky::ShadowTextureBytes(int32 Width, int32 Rows)
{
    if (Width <= 0 || Rows <= 0)
    {
        return 0;
    }
    static TMap<FIntPoint, int64> Asked;
    if (const int64* Known = Asked.Find(FIntPoint(Width, Rows)))
    {
        return *Known;
    }
    int64 Bytes = 0;
    if (GDynamicRHI && FCString::Stricmp(GDynamicRHI->GetName(), TEXT("Null")) != 0)
    {
        FSunShadowMap Shape;
        Shape.Width = Width;
        Shape.Rows = Rows;
        int32 Levels = 1;
        while (Shape.WidthAt(Levels - 1) > 1 || Shape.RowsAt(Levels - 1) > 1)
        {
            ++Levels;
        }
        const FRHITextureDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("ShadowMapSize"), Width, Rows, PF_G16).SetNumMips(Levels);
        Bytes = static_cast<int64>(RHICalcTexturePlatformSize(Desc).Size);
    }
    if (Bytes <= 0)
    {
        Bytes = ShadowLevelsBytes(Width, Rows);
    }
    return Asked.Add(FIntPoint(Width, Rows), Bytes);
}

TArray<int32> ShipSky::CappedShadowWidths(TConstArrayView<FShadowWorld> Worlds, int32 Width, int64 CapBytes)
{
    return CappedShadowWidths(Worlds, Width, CapBytes, [](int32 W, int32 R) { return ShadowLevelsBytes(W, R); });
}

TArray<int32> ShipSky::CappedShadowWidths(TConstArrayView<FShadowWorld> Worlds, int32 Width, int64 CapBytes,
                                          TFunctionRef<int64(int32, int32)> BytesOf)
{
    TArray<int32> Widths;
    Widths.Init(Width, Worlds.Num());
    const auto Bytes = [&](int32 Index)
    {
        return BytesOf(Widths[Index], SunShadowMap::RowsFor(Widths[Index], Worlds[Index].PsiLo));
    };
    int64 Total = 0;
    for (int32 Index = 0; Index < Worlds.Num(); ++Index)
    {
        Total += Bytes(Index);
    }
    while (Total > CapBytes)
    {
        int32 Finest = INDEX_NONE;
        double FinestTexel = TNumericLimits<double>::Max();
        for (int32 Index = 0; Index < Worlds.Num(); ++Index)
        {
            const double Texel = 2.0 * UE_DOUBLE_PI * Worlds[Index].RadiusCm / Widths[Index];
            if (Widths[Index] / 2 >= ShadowMinWidth && Texel < FinestTexel)
            {
                FinestTexel = Texel;
                Finest = Index;
            }
        }
        if (Finest == INDEX_NONE)
        {
            break;   // every map at the floor: the cap cannot be met
        }
        Total -= Bytes(Finest);
        Widths[Finest] /= 2;
        Total += Bytes(Finest);
    }
    return Widths;
}

UTexture2DDynamic* ShipSky::MakeShadowTexture(const FSunShadowMap& Map, FName Name)
{
    if (Map.LevelCount() == 0)
    {
        return nullptr;
    }
    const FName Unique = MakeUniqueObjectName(GetTransientPackage(), UTexture2DDynamic::StaticClass(),
        FName(*(TEXT("ShadowMap_") + Name.ToString().Replace(TEXT(" "), TEXT("_")))));
    UTexture2DDynamic* Texture = NewObject<UTexture2DDynamic>(GetTransientPackage(), Unique, RF_Transient);
    Texture->SizeX = Map.Width;
    Texture->SizeY = Map.Rows;
    Texture->Format = PF_G16;
    Texture->NumMips = Map.LevelCount();
    Texture->bIsResolveTarget = false;
    Texture->SRGB = false;
    Texture->bNoTiling = false;
    Texture->Filter = TF_Nearest;
    Texture->SamplerAddressMode = AM_Clamp;
    // Grayscale and linear, as the parameters' default is; in a group no
    // device profile biases, and a dynamic texture is never streamed: every
    // mip is on the GPU from its creation.
    Texture->CompressionSettings = TC_Grayscale;
    Texture->LODGroup = TEXTUREGROUP_Pixels2D;
    Texture->NeverStream = true;
    Texture->UpdateResource();
    return Texture;
}

TArray<ShipSky::FShadowPiece> ShipSky::ShadowUploadPieces(const FSunShadowMap& Map, int64 PieceBytes)
{
    TArray<FShadowPiece> Pieces;
    for (int32 Level = 0; Level < Map.LevelCount(); ++Level)
    {
        const int64 RowBytes = static_cast<int64>(Map.WidthAt(Level)) * static_cast<int64>(sizeof(uint16));
        const int32 Rows = Map.RowsAt(Level);
        const int32 Each = static_cast<int32>(FMath::Clamp<int64>(PieceBytes / RowBytes, 1, Rows));
        for (int32 First = 0; First < Rows; First += Each)
        {
            FShadowPiece& Piece = Pieces.AddDefaulted_GetRef();
            Piece.Level = Level;
            Piece.FirstRow = First;
            Piece.Rows = FMath::Min(Each, Rows - First);
            Piece.Bytes = Piece.Rows * RowBytes;
        }
    }
    return Pieces;
}

void ShipSky::UploadShadowPieces(UTexture2DDynamic& Texture, const TSharedPtr<const FSunShadowMap, ESPMode::ThreadSafe>& Map,
                                 TArray<FShadowPiece> Pieces, TSharedPtr<std::atomic<int64>, ESPMode::ThreadSafe> SlowestNs)
{
    FTextureResource* Resource = Texture.GetResource();
    if (!Resource || !Map.IsValid() || Pieces.Num() == 0)
    {
        return;
    }
    // The resource outlives this command: a texture's release is queued
    // behind it, since the collector's BeginDestroy enqueues later.
    ENQUEUE_RENDER_COMMAND(ShadowMapPieces)(
        [Resource, Map, Pieces = MoveTemp(Pieces), SlowestNs](FRHICommandListImmediate& RHICmdList)
        {
            const double Start = FPlatformTime::Seconds();
            FRHITexture* Target = Resource->TextureRHI;
            if (!Target)
            {
                return;
            }
            for (const FShadowPiece& Piece : Pieces)
            {
                const int32 Width = Map->WidthAt(Piece.Level);
                const FUpdateTextureRegion2D Region(0, Piece.FirstRow, 0, 0, Width, Piece.Rows);
                const uint16* Rows = Map->Levels[Piece.Level].GetData() + static_cast<int64>(Piece.FirstRow) * Width;
                RHICmdList.UpdateTexture2D(Target, Piece.Level, Region, Width * sizeof(uint16), reinterpret_cast<const uint8*>(Rows));
            }
            if (SlowestNs.IsValid())
            {
                const int64 Took = static_cast<int64>(1.0e9 * (FPlatformTime::Seconds() - Start));
                int64 Was = SlowestNs->load(std::memory_order_relaxed);
                while (Took > Was && !SlowestNs->compare_exchange_weak(Was, Took, std::memory_order_relaxed))
                {
                }
            }
        });
}

UTexture2DDynamic* ShipSky::MakeShadowTextureNow(const TSharedPtr<const FSunShadowMap, ESPMode::ThreadSafe>& Map, FName Name)
{
    if (!Map.IsValid())
    {
        return nullptr;
    }
    UTexture2DDynamic* Texture = MakeShadowTexture(*Map, Name);
    if (Texture)
    {
        UploadShadowPieces(*Texture, Map, ShadowUploadPieces(*Map, TNumericLimits<int64>::Max()));
    }
    return Texture;
}

int64 ShipSky::ShadowUploadBytesPerFrame()
{
    return static_cast<int64>(FMath::Max(1, CVarShadowUploadKB.GetValueOnGameThread())) * 1024;
}
