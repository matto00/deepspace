#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ship/NavStart.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Universe/UniversePosition.h"
#include "ShipSky.generated.h"

class FOutputDevice;
class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialParameterCollection;
class UPostProcessComponent;
class UShipSubsystem;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Everything outside the glass that is *somewhere*: the local star, its
 * planets, the neighbouring stars, the one sun that lights the deck, and the
 * fixed exposure that makes brightness mean something (sky decisions 1-7).
 *
 * **It stores nothing it could ask for.** Which system the ship is in, the
 * jump serial and whether the ship is between stars are LocalSystem's to
 * answer, and they are asked every frame; where the ship is and which way it
 * points are UShipSubsystem's. There is no SetSystem and no SetInTransit
 * (plan conflict 2): nothing calls into the sky and nothing needs to find
 * it. What it keeps is a cache of its own drawing -- the proxies it built,
 * keyed by the serial and the system it built them for -- never the answer.
 *
 * **It never writes the flight state.** The one write path it owns is the
 * ds.Sky.Goto console command, a one-shot PlaceShip for tuning.
 *
 * Attached to the counter-frame at BeginPlay with an identity relative
 * transform, so its local space *is* the counter-frame's -- universe axes --
 * and the ship's attitude reaches every body, the neighbours and the sun
 * without a line of code here. Everything is Movable: it follows the ship,
 * and a Static child of a movable root never has its world transform updated.
 *
 * Asset slots are set by the level build and by nothing else (ADR 0002);
 * this class never loads an asset.
 */
UCLASS()
class DEEPSPACE_API AShipSky : public AActor
{
    GENERATED_BODY()

public:
    AShipSky();

    /** Poll LocalSystem and the ship, and draw what they answer. Every frame
     *  from Tick; public for tests. */
    void SyncToShip();

    /**
     * Everything SyncToShip does once it has asked: LocalSystem's three
     * answers passed in rather than asked for. Rebuilds when the serial
     * moves, hides the whole sky between stars, and otherwise draws.
     *
     * Public so a test can give it answers the null-world branch never
     * gives -- a serial that moves, a ship in transit. Nothing else calls it:
     * a caller that did would be pushing a system at the sky, which is what
     * plan conflict 2 removed.
     */
    void SyncTo(const FSkySystem& System, int32 Serial, bool bInTransit);

    /** Throw away the proxies and make one per body of System. SyncTo calls
     *  it when the jump serial moves; public for tests. */
    void RebuildFor(const FSkySystem& System);

    /**
     * SyncTo's every-frame half, drawing System as seen from where the ship
     * is now. Rebuilds first unless the proxies were built for this system
     * -- its name, where its star is, and how many bodies it has -- because
     * a cache is trusted only while it matches, and a PlaceShip into another
     * system (ds.Nav.PlaceAtStart, ds.Sky.Goto's successors, a teleport)
     * need not move the serial. Public so a test can draw a fixture into a
     * world whose LocalSystem has nothing to say.
     */
    void DrawFrom(const FSkySystem& System);

    /**
     * ds.Sky.Goto with LocalSystem's answers passed in: the console command
     * asks for them and calls this. Refuses in transit and for a body the
     * system does not have, and otherwise makes one PlaceShip. Public for
     * the same reason as SyncTo.
     */
    static void Goto(UShipSubsystem& Ship, const FSkySystem& System, bool bInTransit,
                     TConstArrayView<FString> Args, FOutputDevice& Out);

    /** Radians per pixel at the centre of the player's view,
     *  ShipSky::ViewPixelAngle: the live FOV over the viewport width, so a
     *  zoom or a small window resolves planets at the right moment. */
    double GetPixelAngle() const;

    /** Where points at infinity are drawn, cm: behind every body proxy. */
    double GetDomeRadius() const;

    /** The last projection DrawFrom made. For tests. */
    const FSkyFrame& GetLastFrame() const;

    /** The jump serial the proxies were last built for; INDEX_NONE before
     *  the first SyncToShip. A cache key, never an answer: for tests, which
     *  must see the sky rebuild on arrival rather than infer it. */
    int32 GetBuiltForSerial() const;

    int32 GetProxyCount() const;
    UStaticMeshComponent* GetProxy(int32 Index) const;
    UDirectionalLightComponent* GetSun() const;
    UPostProcessComponent* GetExposure() const;
    UInstancedStaticMeshComponent* GetNeighbourStars() const;

    /**
     * The dome, 250,000 km: twice FSkyViewParams::FarProxy, so every body's
     * proxy draws in front of every point at infinity. At this distance a
     * float resolves a vertex to about 20 m relative to the camera, 8e-8 rad,
     * which a star cannot show. The counter-frame's distant stars belong on
     * the same shell.
     */
    static constexpr double DomeRadius = 2.5e10;

    /**
     * What a point at infinity writes as its per-instance brightness, from
     * its flux on the starfield's honest scale (FSkyStar::Flux: 1 = the
     * faintest drawn, SkyStarfield::MaxFlux the brightest). Compressed by
     * ds.Sky.FluxGamma -- a point's brightness is its compressed flux (sky
     * decision 2) -- then ds.Sky.StarfieldFaint per unit, times
     * ds.Sky.Radiance. Reads the CVars at call time.
     *
     * **The counter-frame must write DistantStars' brightness through this
     * too**, as PointStarBrightness(Star.Flux). A neighbour is drawn exactly
     * like a background star of its flux only while both go through one
     * function; the counter-frame writing its own number is how the
     * destinations disappear into the backdrop or outshine it.
     */
    static float PointStarBrightness(double Flux);

    /**
     * How many pixels across a point at infinity is drawn: ds.Sky.PointPixels,
     * read at call time. A size, where PointStarBrightness is a brightness,
     * and bound by the same rule: **the counter-frame sizes DistantStars
     * through this too**, with ShipSky::PointDiameter. A neighbour is the
     * same point as a background star of its flux only while both agree on
     * how big a point is as well as how bright -- moved for the neighbours
     * alone, the destinations stand out from the galaxy by size, and the
     * shimmer the CVar exists to cure stays on every other star.
     */
    static double PointPixels();

    /** Assets, assigned by Tools/build_hauler.py. The sphere every body and
     *  every neighbour is drawn with. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<UStaticMesh> BodyMesh;

    /** M_SkyBody: planets and moons. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<UMaterialInterface> BodyMaterial;

    /** M_SkyStar: the local star. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<UMaterialInterface> StarMaterial;

    /** M_SkyStarfield: the neighbours, colour and brightness per instance. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<UMaterialInterface> PointStarMaterial;

    /** MPC_Sky, which M_SkyGlass reads for the veil: the room's light and
     *  the reflection's strength, written every frame. Nothing is written
     *  while it is unassigned, and the glass then shows the collection's
     *  defaults, the lit ship. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<UMaterialParameterCollection> SkyParameters;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<USceneComponent> Root;

    /** The only sun, and the only light from outside (sky decision 5). */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<UDirectionalLightComponent> Sun;

    /** Unbound: fixed exposure everywhere, with no volume to place. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<UPostProcessComponent> Exposure;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sky")
    TObjectPtr<UInstancedStaticMeshComponent> NeighbourStars;

private:
    void DrawBodies(const FSkySystem& System, const FSkyFrame& Frame, const FQuat& CounterFrameRotation);
    void DrawSun(const FSkySystem& System, const FSkyFrame& Frame);
    void DrawNeighbours(const FSkySystem& System, const FUniversePosition& ShipPosition, double PixelAngle, double PointPixels);
    void ApplyExposure();
    void WriteParameters();
    void SetSkyVisible(bool bVisible);

    /** Made at runtime, one per body: their number depends on the system, and
     *  runtime components cannot go stale in a saved level or a Blueprint. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> Proxies;

    /** Whether the proxies were drawn for this system: its name and its
     *  star's position (a name can repeat across the galaxy, a star's
     *  position cannot), and its size. */
    bool IsBuiltFor(const FSkySystem& System) const;

    /** The jump serial the proxies were built for. A cache key, never the
     *  answer: INDEX_NONE until the first SyncToShip builds. */
    int32 BuiltForSerial = INDEX_NONE;

    /** The system the proxies were built for, by name and by where its star
     *  is. Cache keys too; RebuildFor alone writes them. */
    FName BuiltForSystem;
    FUniversePosition BuiltForStar;

    FSkyFrame LastFrame;
};

/** The pure arithmetic AShipSky does beyond SkyProjection's, testable with no
 *  world. */
namespace ShipSky
{
    /** The flux of the faintest background star, in solar luminosities at a
     *  light year squared: a Sun at 70.4 ly. The Sun is absolute magnitude
     *  4.83, and at 21.6 pc it is magnitude 6.5 -- the faint end of the
     *  naked-eye range SkyStarfield draws. */
    inline constexpr double FaintestFluxSunDistanceLy = 70.4;

    /**
     * The neighbour as the ship sees it. LocalSystem gives its direction
     * and distance from this system's star, and the ship can be anywhere out
     * to the system's edge (plan conflict 10): at 0.25 ly from the star a
     * neighbour 4 ly off is displaced by 3.6 degrees, and navigation's
     * course marker, which uses the true ship-relative direction, would sit
     * that far off the star it marks. Worked in doubles through
     * StarFromShip, the universe-position difference, so nothing here loses
     * the ship's metres against the neighbour's light years.
     */
    DEEPSPACE_API FSkyNeighbour NeighbourFromShip(const FSkyNeighbour& Neighbour, const FVector& StarFromShip);

    /**
     * A neighbour's flux on the starfield's honest scale, from its distance:
     * L (70.4 ly / D)^2. A Sun at 4 ly is 310 -- among the brightest few in
     * a sky whose brightest is 400, as Alpha Centauri is -- and most red
     * dwarfs within reach are below 1, naked-eye invisible, as Proxima is.
     * Held in the starfield's [1, MaxFlux] so no destination is ever missing
     * from the sky altogether, and none outshines the brightest background
     * star. Compression is PointStarBrightness's, as it is a background
     * star's.
     */
    DEEPSPACE_API double NeighbourFlux(const FSkyNeighbour& Neighbour);

    /** The diameter a point needs to be Pixels across at Distance, cm. */
    DEEPSPACE_API double PointDiameter(double Distance, double PixelAngle, double Pixels);

    /** The view FSkyViewParams' default pixel angle is taken from, used for
     *  whichever half of a view is missing. */
    inline constexpr double FallbackFovDegrees = 90.0;
    inline constexpr double FallbackWidthPixels = 1920.0;

    /**
     * Radians per pixel at the centre of a view FovDegrees across and
     * WidthPixels wide: 2 tan(FOV / 2) / width. A view with no width --
     * which is what -nullrhi gives -- is taken as FallbackWidthPixels wide,
     * and one with no field of view as FallbackFovDegrees across; neither
     * missing, it is FSkyViewParams' default.
     */
    DEEPSPACE_API double PixelAngle(double FovDegrees, double WidthPixels);

    /**
     * PixelAngle of the first player's live view: its camera's field of view
     * over its viewport's width. The one answer everything drawn on the dome
     * is sized by -- the sky's neighbours, the counter-frame's stars and its
     * course marker -- so a point drawn by one is the size of a point drawn
     * by the other at the helm and at a screen alike.
     */
    DEEPSPACE_API double ViewPixelAngle(const UWorld* World);

    /**
     * The engine's default AutoExposureBias, which auto exposure runs at in
     * ds.Sky.ExposureMode 0 (where the galley's EV is read) and which mode 2
     * is pinned to, so that the fallback and the design agree.
     */
    inline constexpr double AutoExposureDefaultBias = 1.0;

    /**
     * The exposure bias that makes manual exposure show a scene of this EV100
     * exactly as auto exposure would. SceneEV100 is in the HDR
     * visualisation's convention, log2((average / 0.18) / LuminanceMax),
     * with LuminanceMax 1 at the default lens attenuation -- the white-point
     * EV that AutoExposureMin/MaxBrightness are also in. Auto scales the
     * picture by 2^bias / (average / 0.18) = 2^(bias - EV); manual with no
     * physical camera has a white point of 1 and scales by 2^bias. So
     * AutoExposureDefaultBias - EV. One stop brighter a scene, one stop less
     * bias.
     */
    DEEPSPACE_API double ManualExposureBias(double SceneEV100);

    /** Index of the body named or numbered by Which in System, INDEX_NONE if
     *  none: a number is an index, anything else is a body's Id. */
    DEEPSPACE_API int32 FindBody(const FSkySystem& System, const FString& Which);

    /**
     * ds.Sky.Goto's placement: AltitudeCm above Body's surface on its day
     * side, facing it, the system's up kept up. A star has no day side, so
     * the ship stays on the side of it it is already on. Empty for an index
     * the system does not have.
     */
    DEEPSPACE_API TOptional<FNavPlacement> GotoPlacement(const FSkySystem& System, int32 Body,
                                                         double AltitudeCm, const FUniversePosition& From);
}
