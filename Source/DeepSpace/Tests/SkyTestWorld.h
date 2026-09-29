#pragma once

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Surface/WorldGround.h"
#include "Universe/UniverseSubsystem.h"

/**
 * A game world as the level build leaves it -- the universe's and the ship's
 * subsystems, a counter-frame and a sky with the assets place_sky gives them
 * -- for the tests that need the two actors and the simulation to meet.
 * Test scaffolding: nothing outside Tests/ may include this.
 */
namespace SkyTestWorld
{
    /** Where the pilot's eyes are, ship space, seated at the helm (the port
     *  seat; sky spec, DeepSpace.Sky.ShipSky). The ship is the world origin,
     *  so this is also world space. Measured, not chosen:
     *  DeepSpace.Player.SeatedEyeIsPilotEye holds it to where PlaceCamera
     *  puts a seated character's eyes, 19 cm forward of the seat and 125 cm
     *  up. It was once 170, a standing eye, which saw over a desk screen the
     *  real pilot could not. */
    inline const FVector PilotEye(1604.0, -72.0, 125.0);

    /** The helm's seat anchor, on the floor: hauler_layout's PILOT_SEAT,
     *  resolved. DeepSpace.Player.SeatedEyeIsPilotEye sits the character here
     *  and measures where its eyes go, which is what PilotEye must be. */
    inline const FVector HelmSeat(1585.0, -70.0, 0.0);

    /** How far, cm, a seated pilot's eye may stray from PilotEye, either
     *  way. The sitting idle moves it under a centimetre; the rest is margin,
     *  and the layout's nose-line check looks from both ends of it
     *  (validate_hauler.py's SEATED_EYE_BOB, which test_placement holds equal
     *  to this), so an idle that strayed further must fail here first. */
    inline constexpr double PilotEyeBob = 5.0;

    /** Whether a test world casts shadows: Tiles marches every tile's
     *  vertices (ds.Terrain.Shadows), Maps bakes every solid world's map
     *  (ds.Sky.ShadowMaps), On both (the cast-shadow plan). They are the
     *  costliest things a test world does and most tests never look at one,
     *  so they are off unless a test asks, and put back after. Off is not
     *  what the game ships (both switches default to 1): a test of the
     *  ground in flight -- GroundKeepsUp, the handover, the landing
     *  playtests, the loop -- asks for On, so its timing is play's. */
    enum class EShadows : uint8
    {
        Off = 0,
        Tiles = 1,
        Maps = 2,
        On = 3
    };

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
        AWorldGround* Ground = nullptr;
        UStaticMesh* Sphere = nullptr;

        /** The shadow switches as they were before this world set them. */
        TMap<IConsoleVariable*, FString> ShadowSwitchesWere;

        /** DistantStars is kept small unless a test needs the real dome: the
         *  count is what the level has, not what a test must pay for. */
        explicit FSkyWorld(const TCHAR* Name, int32 DistantStarCount = 8, EShadows Shadows = EShadows::Off)
        {
            // Before any actor exists: the ground and the sky read these when
            // they first build. The tiles and the maps are asked for apart,
            // so a test of one never pays for the other in the background.
            const TPair<const TCHAR*, EShadows> Switches[] = { { TEXT("ds.Terrain.Shadows"), EShadows::Tiles }, { TEXT("ds.Sky.ShadowMaps"), EShadows::Maps } };
            for (const TPair<const TCHAR*, EShadows>& Switch : Switches)
            {
                if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Switch.Key))
                {
                    ShadowSwitchesWere.Add(Variable, Variable->GetString());
                    const bool bOn = (static_cast<uint8>(Shadows) & static_cast<uint8>(Switch.Value)) != 0;
                    Variable->Set(bOn ? 1 : 0, ECVF_SetByCode);
                }
            }
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
            // What place_ground assigns, and all it assigns: the ground's
            // material. Before M_SkyGround exists (Task T7), the engine's.
            Ground = World->SpawnActor<AWorldGround>();
            if (Ground)
            {
                UMaterialInterface* GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/Sky/M_SkyGround.M_SkyGround"));
                Ground->GroundMaterial = GroundMaterial ? GroundMaterial : UMaterial::GetDefaultMaterial(MD_Surface);
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
            if (Ground)
            {
                Ground->SyncToShip();
            }
            Sky->SyncToShip();
        }

        /** Play ends before the world goes, as it does in the game, so every
         *  actor and subsystem is told and the world is not torn down still
         *  playing. A world that never began play, or whose test ended it
         *  already, ignores the call. */
        ~FSkyWorld()
        {
            World->EndPlay(EEndPlayReason::RemovedFromWorld);
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
            for (const TPair<IConsoleVariable*, FString>& Was : ShadowSwitchesWere)
            {
                Was.Key->Set(*Was.Value, ECVF_SetByCode);
            }
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
