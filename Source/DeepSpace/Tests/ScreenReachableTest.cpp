#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipConsole.h"
#include "Ship/ShipLaptop.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScreenReachableTest,
    "DeepSpace.Ship.ScreenReachable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /**
     * Trace at the panel the way the player's pointer does -- along the view,
     * on Visibility -- and report what is actually hit first. A screen that
     * draws perfectly but sits a hair inside its own casing fails here,
     * which is the whole point: both screens have shipped that way once.
     */
    void CheckReachable(FAutomationTestBase& Test, AActor* Owner, UWidgetComponent* Panel, const TCHAR* What)
    {
        if (!Test.TestNotNull(FString::Printf(TEXT("%s has a panel"), What), Panel))
        {
            return;
        }

        // The quad faces its own -X. Stand off a metre and look back at it.
        const FVector Face = Panel->GetComponentRotation().RotateVector(FVector(-1.0, 0.0, 0.0));
        const FVector Eye = Panel->GetComponentLocation() + Face * 100.0;
        const FVector Target = Panel->GetComponentLocation() - Face * 5.0;

        FCollisionQueryParams Params(SCENE_QUERY_STAT(ScreenReachable), false);
        FHitResult Hit;
        const bool bHit = Owner->GetWorld()->LineTraceSingleByChannel(
            Hit, Eye, Target, ECC_Visibility, Params);

        Test.AddInfo(FString::Printf(
            TEXT("%s: panelLoc=%s panelScale=%s eye=%s hit=%s(%s) blocking=%d"),
            What, *Panel->GetComponentLocation().ToCompactString(),
            *Panel->GetComponentScale().ToCompactString(),
            *Eye.ToCompactString(),
            *GetNameSafe(Hit.GetComponent()),
            *GetNameSafe(Hit.GetActor()), bHit ? 1 : 0));

        Test.TestTrue(FString::Printf(TEXT("%s: the pointer's trace hits something"), What), bHit);
        Test.TestEqual(
            FString::Printf(TEXT("%s: what it hits first is the screen, not the casing"), What),
            Hit.GetComponent(), static_cast<UPrimitiveComponent*>(Panel));

        // A non-uniform scale reaching the panel is the mechanism behind both
        // failures: it squashes the quad and its collision box together.
        const FVector Scale = Panel->GetComponentScale();
        Test.TestTrue(
            FString::Printf(TEXT("%s: the panel's scale is uniform"), What),
            FMath::IsNearlyEqual(Scale.X, Scale.Y, 1e-4f) && FMath::IsNearlyEqual(Scale.Y, Scale.Z, 1e-4f));
    }
}

bool FScreenReachableTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ScreenReachableTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    // The level builder hands these actors their meshes after spawning them
    // and only then calls FitParts (Tools/build_hauler.py). Without a mesh
    // FitBox returns early, nothing is scaled, and the very bug this test
    // exists for cannot occur -- so the test must set the scene up the way
    // the builder does, or it passes no matter what.
    UStaticMesh* Chamfer = LoadObject<UStaticMesh>(
        nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));
    if (!TestNotNull(TEXT("SM_ChamferCube loads"), Chamfer))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    // Far apart, so neither can be what the other's trace hits.
    AShipLaptop* Laptop = World->SpawnActor<AShipLaptop>(FVector(0.0, 0.0, 0.0), FRotator::ZeroRotator);
    AShipConsole* Console = World->SpawnActor<AShipConsole>(FVector(0.0, 5000.0, 0.0), FRotator::ZeroRotator);

    if (TestNotNull(TEXT("the laptop spawns"), Laptop))
    {
        UStaticMeshComponent* Lid = nullptr;
        for (UActorComponent* Component : Laptop->GetComponents())
        {
            if (UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Component))
            {
                AsMesh->SetStaticMesh(Chamfer);
                if (AsMesh->GetFName() == TEXT("Lid"))
                {
                    Lid = AsMesh;
                }
            }
        }
        // OnConstruction is what fits the parts; rerunning it is what the
        // editor does after the builder assigns the meshes.
        Laptop->RerunConstructionScripts();

        // Guard the guard: if the lid is not actually scaled, this test is
        // exercising nothing and would pass against the original bug.
        if (TestNotNull(TEXT("laptop: the lid component is found"), Lid))
        {
            TestFalse(TEXT("laptop: the lid really is non-uniformly scaled"),
                      Lid->GetComponentScale().AllComponentsEqual(1e-3f));
        }
        CheckReachable(*this, Laptop, Laptop->GetScreen(), TEXT("laptop"));
    }
    if (TestNotNull(TEXT("the console spawns"), Console))
    {
        if (UStaticMeshComponent* ConsoleMesh = Console->FindComponentByClass<UStaticMeshComponent>())
        {
            ConsoleMesh->SetStaticMesh(Chamfer);
        }
        Console->RerunConstructionScripts();
        CheckReachable(*this, Console, Console->GetScreen(), TEXT("console"));
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
