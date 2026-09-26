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
 * - **Near stars** hold real universe positions a few hundred metres out and
 *   are re-placed through the conversion every frame. Without them, flying
 *   forward produces no visual change whatsoever and the flight model cannot
 *   be tuned by eye. At cruise they are the only thing that makes speed
 *   visible; under the drive they fade out, because motes wrapping every
 *   frame would strobe, and the planets' own parallax, real at those speeds,
 *   takes over. Between stars they are the streaks.
 *
 * It also carries the course marker: a teal point on the dome at the plotted
 * star's true direction, created at runtime the first time a course is
 * plotted, so the placed actor's saved hierarchy never changes for it.
 *
 * It asks the ship for everything every frame and keeps only cache keys --
 * the jump serial it last scattered the near field for, and the pixel angle
 * it last sized the dome for.
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

    /**
     * Radians per pixel at the centre of the view, from the player camera's
     * live field of view and the viewport's width: what anything drawn on
     * the dome is sized by, so a star stays two pixels at the helm and at a
     * screen alike. 90 degrees over 1920 pixels with no player or viewport,
     * which is what -nullrhi gives.
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
     * material has no Brightness, so the fade pops and the course marker
     * cannot be tinted. None of that is visible under -nullrhi, so it is
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

    /** Each distant star's diameter, in pixels: the smallest size a point can
     *  honestly have (FSkyViewParams::MinPointPixels). Sized in pixels, never
     *  in centimetres, so the dome's radius can move without changing them. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    double DistantStarPixels = 2.0;

    /** The near field is a cube of this half-extent around the ship, wrapped:
     *  a mote that falls out of the back comes round the front. They are dust
     *  that is everywhere rather than landmarks, so wrapping is honest as well
     *  as cheap. */
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

    /** Place the dome's stars, DistantStarPixels across at PixelAngle, with
     *  their colour and brightness in per-instance custom data. */
    void BuildDistantStars(double PixelAngle);

    /** The motes' brightness from the ship's speed: full at cruise, gone by
     *  ds.Sky.MoteFadeSpeed. */
    void FadeMotes(double Speed, bool bInTransit);

    /** Place, size and show or hide the course marker. */
    void SyncCourseMarker(const UShipSubsystem& Ship, double PixelAngle);

    /** Real universe positions, one per near-star instance. */
    TArray<FUniversePosition> NearStarPositions;

    int32 BuiltForSerial = INDEX_NONE;

    /** The pixel angle the distant stars were last sized for; 0 for never. */
    double SizedForPixelAngle = 0.0;

    /** What AShipSky::PointStarBrightness answered for the faintest and the
     *  brightest star when the dome was last written: a cache key, so that
     *  moving ds.Sky.FluxGamma, StarfieldFaint or Radiance in play re-lights
     *  the dome as it re-lights the neighbours, and nothing else does. */
    float BrightenedFaintest = -1.0f;
    float BrightenedBrightest = -1.0f;

    /** A runtime copy of the motes' material, whose Brightness the fade
     *  drives; null until the first sync finds a material to copy. */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> MoteMaterial;

    /** The motes' own Brightness before any fade: the material's value,
     *  so the look is still tuned where the material is authored. */
    float MoteBrightness = 1.0f;

    UPROPERTY(Transient)
    TObjectPtr<UStaticMeshComponent> CourseMarker;
};
