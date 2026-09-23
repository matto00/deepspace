#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Universe/UniversePosition.h"
#include "ShipCounterFrame.generated.h"

class UInstancedStaticMeshComponent;
class USceneComponent;

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
 *   rotate. They are what makes the universe feel vast.
 * - **Near stars** hold real universe positions a few hundred metres out and
 *   are re-placed through the conversion every frame. Without them, flying
 *   forward produces no visual change whatsoever and the flight model cannot
 *   be tuned by eye. They are the only thing in the game that makes speed
 *   visible.
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

    /** How many stars stand in for infinity, and how far out they are drawn.
     *  The radius is arbitrary: nothing is ever placed relative to it. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    int32 DistantStarCount = 160;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    double DistantStarRadius = 12000.0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starfield")
    double DistantStarScale = 0.3;

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

    /** Until the world seed of docs/vision.md exists, the near field derives
     *  from its own integer seed so that a session is reproducible. */
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
    /** Real universe positions, one per near-star instance. */
    TArray<FUniversePosition> NearStarPositions;
};
