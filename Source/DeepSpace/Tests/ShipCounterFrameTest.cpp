#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCounterFrameTest,
    "DeepSpace.Ship.CounterFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipCounterFrameTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CounterFrameTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    AShipCounterFrame* Frame = World->SpawnActor<AShipCounterFrame>();

    if (TestNotNull(TEXT("the counter-frame spawns"), Frame) && TestNotNull(TEXT("with a ship"), Ship))
    {
        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        Frame->GetDistantStars()->SetStaticMesh(Sphere);
        Frame->GetNearStars()->SetStaticMesh(Sphere);

        Frame->DistantStarCount = 16;
        Frame->NearStarCount = 32;
        Frame->NearFieldRadius = 10000.0;
        Frame->RebuildStarfield();

        // Both layers exist, and the distant one is a shell at its radius.
        TestEqual(TEXT("the distant layer is placed"),
                  Frame->GetDistantStars()->GetInstanceCount(), 16);
        TestEqual(TEXT("the near layer is placed"),
                  Frame->GetNearStars()->GetInstanceCount(), 32);

        bool bAllOnShell = true;
        for (int32 Index = 0; Index < Frame->GetDistantStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetDistantStars()->GetInstanceTransform(Index, Instance, false);
            bAllOnShell &= FMath::IsNearlyEqual(Instance.GetLocation().Size(), Frame->DistantStarRadius, 1.0);
        }
        TestTrue(TEXT("every distant star sits on the shell"), bAllOnShell);

        // The counter-frame carries the ship's attitude, inverted -- and
        // nothing else. Its translation staying at zero is the load-bearing
        // part: children must never inherit a large coordinate.
        const FQuat Attitude(FVector::UpVector, 0.7);
        Ship->PlaceShip(FUniversePosition::FromVector(FVector(4.0e9, -2.0e9, 1.0e9)), Attitude);
        Frame->SyncToShip();
        TestTrue(TEXT("the counter-frame is the ship's attitude inverted"),
                 Frame->GetActorQuat().Equals(Attitude.Inverse(), 1e-5));
        TestEqual(TEXT("and never translates"), Frame->GetActorLocation(), FVector::ZeroVector);

        // Distant stars are direction only: flying a million kilometres does
        // not move them, which is exactly why they cannot show speed.
        FTransform BeforeFlight;
        Frame->GetDistantStars()->GetInstanceTransform(0, BeforeFlight, false);
        Ship->PlaceShip(FUniversePosition::FromVector(FVector(4.0e9 + 1.0e11, -2.0e9, 1.0e9)), Attitude);
        Frame->SyncToShip();
        FTransform AfterFlight;
        Frame->GetDistantStars()->GetInstanceTransform(0, AfterFlight, false);
        TestEqual(TEXT("a distant star does not translate with the ship"),
                  AfterFlight.GetLocation(), BeforeFlight.GetLocation());

        // Near stars are the layer that makes translation visible. Advance the
        // ship 1000 cm along world +X and every mote that did not wrap must
        // have slid exactly 1000 cm the other way.
        Ship->PlaceShip(FUniversePosition::FromVector(FVector::ZeroVector), FQuat::Identity);
        Frame->RebuildStarfield();
        Frame->SyncToShip();

        TArray<FVector> Before;
        for (int32 Index = 0; Index < Frame->GetNearStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetNearStars()->GetInstanceTransform(Index, Instance, true);
            Before.Add(Instance.GetLocation());
        }

        Ship->PlaceShip(FUniversePosition::FromVector(FVector(1000.0, 0.0, 0.0)), FQuat::Identity);
        Frame->SyncToShip();

        int32 Parallaxed = 0;
        for (int32 Index = 0; Index < Frame->GetNearStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetNearStars()->GetInstanceTransform(Index, Instance, true);
            if ((Instance.GetLocation() - Before[Index]).Equals(FVector(-1000.0, 0.0, 0.0), 1e-3))
            {
                ++Parallaxed;
            }
        }
        TestTrue(TEXT("the near field slides past as the ship advances"), Parallaxed > 24);

        // And it keeps doing so: fly far past the field and the motes have
        // wrapped round rather than being left behind.
        Ship->PlaceShip(FUniversePosition::FromVector(FVector(3.7e6, -1.1e6, 8.0e5)), FQuat::Identity);
        Frame->SyncToShip();

        bool bAllInField = true;
        for (int32 Index = 0; Index < Frame->GetNearStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetNearStars()->GetInstanceTransform(Index, Instance, true);
            const FVector Local = Instance.GetLocation();
            bAllInField &= Local.GetAbsMax() <= Frame->NearFieldRadius + 1.0;
        }
        TestTrue(TEXT("the near field wraps and stays around the ship"), bAllInField);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
