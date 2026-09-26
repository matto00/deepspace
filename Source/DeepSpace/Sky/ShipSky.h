#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ship/NavStart.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Universe/UniversePosition.h"
#include "ShipSky.generated.h"

class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialParameterCollection;
class UPostProcessComponent;
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
 * keyed by the serial it built them for -- never the answer.
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

    /** Throw away the proxies and make one per body of System. SyncToShip
     *  calls it when the jump serial moves; public for tests. */
    void RebuildFor(const FSkySystem& System);

    /** SyncToShip's every-frame half, drawing System as seen from where the
     *  ship is now. Rebuilds first if the proxies were built for a system of
     *  a different size: a cache is trusted only while it matches. Public so
     *  a test can draw a fixture into a world whose LocalSystem has nothing
     *  to say. */
    void DrawFrom(const FSkySystem& System);

    /** Radians per pixel at the centre of the player's view: the live FOV
     *  over the viewport width, so a zoom or a small window resolves planets
     *  at the right moment. 90 degrees over 1920 px with no player or no
     *  viewport, which is what -nullrhi gets. For anything else that draws
     *  on the dome -- navigation's course marker. */
    double GetPixelAngle() const;

    /** Where points at infinity are drawn, cm: behind every body proxy. */
    double GetDomeRadius() const;

    /** The last projection DrawFrom made. For tests. */
    const FSkyFrame& GetLastFrame() const;

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
     * What a point at infinity of this flux (1 = the faintest drawn) writes
     * as its per-instance brightness: ds.Sky.StarfieldFaint per unit of flux,
     * times ds.Sky.Radiance. One function so the background starfield and
     * the neighbours cannot drift apart -- a neighbour is drawn exactly like
     * a background star. Reads the CVars at call time.
     */
    static float PointStarBrightness(double Flux);

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

    /** MPC_Sky, which M_SkyGlass reads for the veil. Null until the veil
     *  lands (slice 2); nothing is written while it is. */
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
    void DrawNeighbours(const FSkySystem& System, double PixelAngle, double PointPixels);
    void ApplyExposure();
    void WriteParameters();
    void SetSkyVisible(bool bVisible);

    /** Made at runtime, one per body: their number depends on the system, and
     *  runtime components cannot go stale in a saved level or a Blueprint. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> Proxies;

    /** The jump serial the proxies were built for. A cache key, never the
     *  answer: INDEX_NONE until the first SyncToShip builds. */
    int32 BuiltForSerial = INDEX_NONE;

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
     * A neighbour's flux on the starfield's scale. Honestly it is
     * L (70.4 ly / D)^2 -- a Sun at 4 ly is 310, most red dwarfs within
     * reach are below 1, and naked-eye invisible, as Proxima is. That
     * honest flux is compressed about the faint end by FluxGamma, the same
     * exponent irradiance gets (sky decision 2), and held in the starfield's
     * [1, MaxFlux]: a Sun at 4 ly becomes 18, one of the brightest forty
     * stars in the sky and not the brightest, and no destination is ever
     * missing from the sky altogether.
     */
    DEEPSPACE_API double NeighbourFlux(const FSkyNeighbour& Neighbour, double FluxGamma);

    /** The diameter a point needs to be Pixels across at Distance, cm. */
    DEEPSPACE_API double PointDiameter(double Distance, double PixelAngle, double Pixels);

    /**
     * The exposure bias that makes manual exposure show a scene of this EV100
     * (Unreal's convention, as the HDR visualisation reports it) exactly as
     * auto exposure would: auto maps the average to middle grey 0.18 at its
     * default bias of +1, while manual maps its base luminance to white. So
     * log2(0.18) + 1 - EV. One stop brighter a scene, one stop less bias.
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
