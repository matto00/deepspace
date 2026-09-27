#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Universe/UniversePosition.h"
#include "ShipCounterFrame.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class UShipSubsystem;
class UStaticMeshComponent;

/**
 * The dust's law (flight-feel decision 8), pure: how fast the near field is
 * streamed, and how long each mote is drawn, for the ship's true speed. All
 * speeds cm/s.
 *
 * Optic flow says *that* the ship moves, which way, and roughly how fast
 * within a decade or so; it cannot tell 0.1 c from 1 c at any honest scale,
 * and past about 3.6 km/s a 400 m field's motes (some 120 m apart) step
 * further than half their spacing in a 60 Hz frame and strobe. So the dust is
 * honest up to a knee -- cruise exactly, and the drive's first notches at
 * their true 1 and 2 km/s, five and ten times cruise's top -- and above it a
 * representation that says "faster still", by decades, and no more: the seen
 * speed climbs slowly from the knee to DustTop at the drive's top, on a log
 * scale, and each mote stretches along the velocity by the same fraction of
 * that scale. Which notch the ship is at is read from numbers, never from
 * the dust.
 */
namespace ShipDust
{
    /** ds.Sky.DustKnee: the fastest the dust is honest, cm/s (2 km/s). */
    inline constexpr double DefaultKnee = 2.0e5;

    /** ds.Sky.DustTop: the seen speed at the drive's top, cm/s (3 km/s): 50 m
     *  a frame at 60 Hz, under the ~60 m half-spacing that strobes. Only at
     *  60 Hz: the step is per frame and nothing here scales with frame time,
     *  so at 45 Hz it is 67 m and at 30 Hz 100 m -- past the limit, as the
     *  knee itself is at 30 Hz. A playtest question at the real frame rate. */
    inline constexpr double DefaultDustTop = 3.0e5;

    /** ds.Sky.DustStretch: how many times its width a mote is drawn long at
     *  the drive's top. Eight is well short of the fold's forty-one. */
    inline constexpr double DefaultStretch = 8.0;

    /**
     * Where Speed sits between the knee and the drive's top, on a log scale:
     * 0 at or below the knee, 1 at or above the top, ln(v / Knee) / ln(Top /
     * Knee) between. 0 everywhere when the top is at or below the knee, where
     * the dust is honest over the whole lever and there is nothing to
     * represent.
     */
    DEEPSPACE_API double LogFraction(double Speed, double Knee, double Top);

    /**
     * The speed the field is streamed at: Speed itself up to the knee, then
     * Knee x (DustTop / Knee) ^ LogFraction, so DustTop at the drive's top
     * and never above it. Continuous at the knee and never falling, so the
     * drive never looks slower than cruise. A DustTop under the knee is read
     * as the knee.
     */
    DEEPSPACE_API double SeenSpeed(double Speed, double Knee, double DustTop, double Top);

    /** How many times its width a mote is drawn along the velocity: 1 at or
     *  below the knee, MaxStretch ^ LogFraction above it, so MaxStretch at
     *  the top and never more. A MaxStretch under 1 is read as 1. */
    DEEPSPACE_API double Stretch(double Speed, double Knee, double Top, double MaxStretch);
}

/**
 * Everything outside the hull hangs off this actor.
 *
 * The ship never moves (ADR 0005): its transform is identity permanently, and
 * the universe is drawn through the inverse of where the ship is and which way
 * it points. This actor carries the rotation half of that inverse.
 *
 * **Its translation is always zero, and that is load-bearing.** The naive
 * reading of "transform by the inverse" is to put the root at -Position too,
 * but a universe position does not fit in an FTransform at all (ADR 0007), and
 * an actor at -10^9 cm would put every child's render transform through float
 * precision that cannot resolve a metre. Translation is applied per object by
 * UniverseToWorld, which subtracts in doubles through the chunk index and
 * yields a small number.
 *
 * The starfield is two layers, because they answer different questions:
 *
 * - **Distant stars** are direction only, at a fixed radius standing in for
 *   infinity. They never translate -- subtracting a finite ship position from
 *   an infinite distance changes nothing -- so they are placed once and simply
 *   rotate. They are what makes the universe feel vast, and they are a galaxy
 *   seen from inside it (SkyStarfield), not an even spread. They do not
 *   change across a jump either: the unmoved background is what makes the
 *   new sun and the shifted neighbours read as somewhere else.
 * - **Near stars** -- the dust -- are a field of offsets a few hundred
 *   metres round the ship, held in *field space*: a cube in universe axes,
 *   advanced every frame by the ship's own displacement and wrapped, and
 *   placed through the ship's rotation only. Without them, flying forward
 *   produces no visual change whatsoever and the flight model cannot be
 *   tuned by eye. Up to ds.Sky.DustKnee this is exactly the honest parallax
 *   of dust at universe positions, for translation and rotation alike;
 *   above it the field streams at ShipDust::SeenSpeed, and nothing is at a
 *   universe position, because nothing needs to be (flight-feel decision
 *   8). They are shown at every speed. Between stars they are the streaks.
 *
 * It also carries the course marker: a teal point on the dome at the plotted
 * star's true direction, created at runtime the first time a course is
 * plotted, so the placed actor's saved hierarchy never changes for it.
 *
 * It asks the ship for everything every frame -- its speed, its velocity, the
 * drive's top -- and keeps only the dust's own offsets and cache keys: the
 * jump serial it last scattered the near field for, the position it last
 * advanced the field from, and the pixel angle it last sized the dome for.
 */
UCLASS()
class DEEPSPACE_API AShipCounterFrame : public AActor
{
    GENERATED_BODY()

public:
    AShipCounterFrame();

    /** Place both star layers from scratch. Called at BeginPlay; public so a
     *  headless test can drive it without a running world. */
    UFUNCTION(BlueprintCallable, Category = "Counter-Frame")
    void RebuildStarfield();

    /** Take the ship's attitude and re-place the near field. Called every
     *  frame; public for the same reason. */
    UFUNCTION(BlueprintCallable, Category = "Counter-Frame")
    void SyncToShip();

    UInstancedStaticMeshComponent* GetDistantStars() const;
    UInstancedStaticMeshComponent* GetNearStars() const;

    /** Null until a course is first plotted; a runtime component, never a
     *  constructor one. */
    UStaticMeshComponent* GetCourseMarker() const;

    /** The jump serial the near field was last scattered for; INDEX_NONE
     *  before the first build. A cache key, never an answer. */
    int32 GetBuiltForSerial() const;

    /** The dust, one offset per near-star instance: cm from the ship, in
     *  universe axes, each inside [-NearFieldRadius, NearFieldRadius) on
     *  every axis. What the motes are drawn from; for the tests. */
    TConstArrayView<FVector> GetDustField() const;

    /**
     * Radians per pixel at the centre of the view: ShipSky::ViewPixelAngle,
     * the sky's own answer, so a star stays two pixels at the helm and at a
     * screen alike and the dome's stars are the size of the sky's.
     */
    double GetPixelAngle() const;

    /**
     * What is wrong with the materials the level gave this frame, one line
     * each; empty when the dome is M_SkyStarfield and the motes M_SkyStar.
     *
     * The frame never loads them itself -- Tools/build_hauler.py assigns them
     * (ADR 0002, sky spec) -- so a level built with the wrong ones still
     * draws, and wrongly: any other dome material ignores the per-instance
     * colour and brightness, so every star draws alike, and any other mote
     * material has no Colour, so the course marker cannot be tinted. None of that is visible under -nullrhi, so it is
     * said, as a warning, at BeginPlay.
     */
    TArray<FString> FindMaterialProblems() const;

    /** Where the dome is drawn, cm: DistantStarRadius. Behind every body the
     *  sky draws, so nothing on it crosses a planet. */
    double GetDomeRadius() const;

    /** How many stars stand in for infinity, and how far out they are drawn.
     *  250,000 km: behind every body proxy the sky draws, whose band ends at
     *  125,000 km, so no star is ever drawn across a planet. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    int32 DistantStarCount = 3000;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    double DistantStarRadius = 2.5e10;

    /** The near field is a cube of this half-extent around the ship, in
     *  universe axes, wrapped: a mote that falls out of the back comes round
     *  the front. They are dust that is everywhere rather than landmarks, so
     *  wrapping is honest as well as cheap. Drawn at 400 m at every speed,
     *  far inside the 50 km near edge of the sky's proxies, so the dust is
     *  in front of every world. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    int32 NearStarCount = 300;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    double NearFieldRadius = 40000.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    double NearStarScale = 0.5;

    /** The root the starfield and the motes derive from when the world has
     *  no universe to ask for one (plan conflict 4); with a universe they
     *  derive from its root seed, and this is never read. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    int32 StarSeed = 20260922;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Counter-Frame")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Starfield")
    TObjectPtr<UInstancedStaticMeshComponent> DistantStars;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Starfield")
    TObjectPtr<UInstancedStaticMeshComponent> NearStars;

private:
    /** Re-scatter the near field around wherever the ship now is. */
    void ScatterNearField();

    /** Place the dome's stars, AShipSky::PointPixels across at PixelAngle,
     *  with their colour and brightness in per-instance custom data. */
    void BuildDistantStars(double PixelAngle);

    /** Put the motes on a runtime copy of their material, once: the course
     *  marker is tinted from its parent. */
    void AdoptMoteMaterial();

    /** Move the dust by the ship's displacement since the last frame, at
     *  the seen speed; frozen between stars. */
    void AdvanceDust(const UShipSubsystem& Ship);

    /** Place every mote from the dust: in flight along the velocity, at the
     *  dust's stretch; between stars as the fold's streaks. */
    void DrawDust(const UShipSubsystem& Ship);

    /** Place, size and show or hide the course marker. */
    void SyncCourseMarker(const UShipSubsystem& Ship, double PixelAngle);

    /** The dust: offsets from the ship, universe axes, cm, one per near-star
     *  instance (GetDustField). */
    TArray<FVector> DustField;

    /** Where the ship was when the dust was last advanced: the field moves
     *  by the difference, so it depends on how far the ship went and not on
     *  how the time was chopped into frames. Reset with every scatter. */
    FUniversePosition DustAnchor;

    int32 BuiltForSerial = INDEX_NONE;

    /** The pixel angle the distant stars were last sized for; 0 for never. */
    double SizedForPixelAngle = 0.0;

    /** What AShipSky::PointStarBrightness answered for the faintest and the
     *  brightest star when the dome was last written: a cache key, so that
     *  moving ds.Sky.FluxGamma, StarfieldFaint or Radiance in play re-lights
     *  the dome as it re-lights the neighbours, and nothing else does. */
    float BrightenedFaintest = -1.0f;
    float BrightenedBrightest = -1.0f;

    /** What AShipSky::PointPixels answered when the dome was last sized: the
     *  same cache key for size, so ds.Sky.PointPixels resizes the galaxy the
     *  frame it resizes the neighbours. */
    double SizedForPointPixels = -1.0;

    /** A runtime copy of the motes' material, at its authored brightness at
     *  every speed; null until the first sync finds a material to copy. */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> MoteMaterial;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> CourseMarker;
};
