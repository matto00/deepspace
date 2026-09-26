#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipDressing.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShipDressingSubsystem.generated.h"

class UMaterialInterface;
class UMeshComponent;

/** A furniture material slot the wear swapped, and what it wore before. */
USTRUCT()
struct FDressWornSlot
{
    GENERATED_BODY()

    UPROPERTY()
    TWeakObjectPtr<UMeshComponent> Mesh;

    UPROPERTY()
    int32 Slot = 0;

    /** Held, so the level's material cannot be collected while swapped out. */
    UPROPERTY()
    TObjectPtr<UMaterialInterface> Original;
};

/**
 * Dresses the ship when the world begins play: somebody's mugs on the
 * counter, their books on the desk, their wear on the furniture (lived-in
 * decisions 1a, 12).
 *
 * It finds the layout's AShipDressingSurface and AShipDressingKeepOut
 * markers by tag, asks ShipDressing::Dress for a plan, and turns the plan
 * into instances on one transient actor tagged Dress.Clutter: one
 * UInstancedStaticMeshComponent per (mesh, material role), every one
 * Movable and NoCollision. Surface clutter is scenery the capsule cannot
 * reach, so nothing can be tripped over, picked up, knocked or tidied.
 *
 * It does not tick, on purpose. Nothing in the dressing changes at runtime,
 * and a subsystem with no tick has nowhere to put a mechanism that could
 * make the ship untidier while you fly: no mess accumulates, nothing is
 * ever a task (the anti-chore principle, kept by structure rather than
 * care). It stores no ship state (ADR 0003): only weak pointers to what it
 * spawned and the materials the wear swapped out, so Redress can undo both.
 *
 * Game and PIE worlds only. The editor's world never begins play, so the
 * viewport shows bare surfaces; seeing the dressing means pressing Play.
 */
UCLASS()
class DEEPSPACE_API UShipDressingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    static UShipDressingSubsystem* Get(const UObject* WorldContext);

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    /**
     * Takes everything down -- the clutter actor, the wear -- and dresses
     * again from the markers as they are now. Idempotent: the same seed and
     * rules give the same instances. Called once when play begins; public
     * because a test spawns its own markers, because ds.Dress.* redresses a
     * running session in place, and because the runtime ship generator will
     * one day build the ship after this subsystem already exists.
     */
    void Redress();

    /** The seed dressing is drawn from: ds.Dress.Seed if set, taken as a
     *  root seed, else the universe's root, so two players aboard the same
     *  universe see the same mugs in the same places. */
    uint64 GetDressSeed() const;

    /** The rules as they stand, with ds.Dress.LivedIn read into them now. */
    FShipDressingRules GetRules() const;

    /** The markers this world's level exports, read fresh, by tag. */
    void GatherSurfaces(TArray<FDressSurface>& OutSurfaces, TArray<FBox>& OutKeepOut) const;

    /** The actor the clutter lives on; null when there is none. */
    AActor* GetClutter() const;

    /** Instances across every component of the clutter actor. */
    int32 GetInstanceCount() const;

    /** One line per surface and its items: what ds.Dress.Describe prints. */
    FString Describe() const;

    /** /Game/Materials/MI_Ship_<Role>, the instance build_hauler.py authors
     *  for that role. */
    static FString MaterialPath(FName Role);

    /** The level's own prototype meshes: the furniture uses the same three,
     *  so they are loaded with the level and their bounds are live. */
    static const TCHAR* MeshPath(EDressMesh Mesh);

    /** The material a worn piece is swapped to; empty for Standard. */
    static FName WearRole(EDressWear Wear);

    /** Without a universe, the root falls back to this: the sky's
     *  starfield keeps the same fallback. */
    static constexpr uint64 FallbackRoot = 20260922;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void Undress();
    void ApplyWear(uint64 Seed, const FShipDressingRules& Rules);

    TWeakObjectPtr<AActor> Clutter;

    UPROPERTY()
    TArray<FDressWornSlot> Worn;
};
