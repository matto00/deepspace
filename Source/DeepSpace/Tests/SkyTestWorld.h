#pragma once

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Universe/UniverseSubsystem.h"

/**
 * A game world as the level build leaves it -- the universe's and the ship's
 * subsystems, a counter-frame and a sky with the assets place_sky gives them
 * -- for the tests that need the two actors and the simulation to meet.
 * Test scaffolding: nothing outside Tests/ may include this.
 */
namespace SkyTestWorld
{
    /** Where the pilot's eyes are, ship space: the port seat at the helm
     *  (sky spec, DeepSpace.Sky.ShipSky). The ship is the world origin, so
     *  this is also world space. */
    inline const FVector PilotEye(1585.0, -70.0, 170.0);

    /** A game world with its subsystems -- the universe's and the ship's --
     *  a counter-frame and a sky, both spawned before play begins, as the
     *  level build places them. */
    struct FSkyWorld
    {
        UWorld* World = nullptr;
        UShipSubsystem* Ship = nullptr;
        UUniverseSubsystem* Universe = nullptr;
        AShipCounterFrame* Frame = nullptr;
        AShipSky* Sky = nullptr;
        UStaticMesh* Sphere = nullptr;

        /** DistantStars is kept small unless a test needs the real dome: the
         *  count is what the level has, not what a test must pay for. */
        explicit FSkyWorld(const TCHAR* Name, int32 DistantStarCount = 8)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, Name);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            Ship = World->GetSubsystem<UShipSubsystem>();
            Universe = World->GetSubsystem<UUniverseSubsystem>();

            Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
            Frame = World->SpawnActor<AShipCounterFrame>();
            // Off the origin, as a hand in the editor might leave it: the
            // sky must land in the counter-frame's space regardless.
            Sky = World->SpawnActor<AShipSky>(FVector(500.0, -200.0, 50.0), FRotator(0.0, 30.0, 0.0));
            if (Frame)
            {
                // What place_counter_frame assigns, and all it assigns.
                for (UInstancedStaticMeshComponent* Layer : { Frame->GetDistantStars(), Frame->GetNearStars() })
                {
                    Layer->SetStaticMesh(Sphere);
                }
                Frame->GetDistantStars()->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarfieldPath));
                Frame->GetNearStars()->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarPath));
                Frame->DistantStarCount = DistantStarCount;
                Frame->NearStarCount = 8;
            }
            if (Sky)
            {
                // What place_sky assigns, and all it assigns.
                Sky->BodyMesh = Sphere;
                Sky->BodyMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::BodyPath);
                Sky->StarMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarPath);
                Sky->PointStarMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarfieldPath);
                Sky->SkyParameters = LoadObject<UMaterialParameterCollection>(nullptr, SkyMaterial::ParametersPath);
            }
        }

        /** The subsystems' OnWorldBeginPlay -- where the opening placement
         *  happens -- and then every actor's BeginPlay. A test world has no
         *  game mode, and UWorld::BeginPlay reaches actors only through one,
         *  so the second half is the call its game state would make. */
        void BeginPlay()
        {
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
            World->GetWorldSettings()->NotifyBeginPlay();
        }

        /** One frame in the order the game runs it: the ship subsystem steps
         *  the flight state, then both actors, in TG_PostUpdateWork, draw
         *  what it now answers. */
        void Step(float DeltaSeconds)
        {
            Ship->Tick(DeltaSeconds);
            Frame->SyncToShip();
            Sky->SyncToShip();
        }

        ~FSkyWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };

    /** A console variable set for one scope and put back after, so one test
     *  cannot tune another. */
    struct FScopedCVar
    {
        IConsoleVariable* Variable;
        FString Previous;

        FScopedCVar(const TCHAR* Name, float Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name))
        {
            check(Variable);
            Previous = Variable->GetString();
            Variable->Set(Value, ECVF_SetByCode);
        }

        ~FScopedCVar()
        {
            Variable->Set(*Previous, ECVF_SetByCode);
        }
    };

    inline float CVarFloat(const TCHAR* Name)
    {
        return IConsoleManager::Get().FindConsoleVariable(Name)->GetFloat();
    }

    /** A proxy's true sphere in the world: centre and radius, measured from
     *  the mesh's bounds, as the actor must place it. */
    struct FDrawnSphere
    {
        FVector Centre = FVector::ZeroVector;
        double Radius = 0.0;
    };

    inline FDrawnSphere Drawn(const UStaticMeshComponent* Proxy, const UStaticMesh* Mesh)
    {
        const FBox Box = Mesh->GetBoundingBox();
        const FTransform& Transform = Proxy->GetComponentTransform();
        return { Transform.TransformPosition(Box.GetCenter()), Box.GetExtent().GetMax() * Transform.GetScale3D().X };
    }

    inline double Subtense(const FDrawnSphere& Sphere, const FVector& Eye)
    {
        return 2.0 * FMath::Asin(Sphere.Radius / (Sphere.Centre - Eye).Size());
    }

    /** Whether every proxy's visibility is bVisible, and there is at least
     *  one: an empty sky proves nothing either way. */
    inline bool AllProxies(const AShipSky* Sky, bool bVisible)
    {
        if (Sky->GetProxyCount() == 0)
        {
            return false;
        }
        for (int32 Index = 0; Index < Sky->GetProxyCount(); ++Index)
        {
            if (!Sky->GetProxy(Index) || Sky->GetProxy(Index)->IsVisible() != bVisible)
            {
                return false;
            }
        }
        return true;
    }
}
