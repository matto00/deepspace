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

        const FTransform Face = Panel->GetComponentTransform();
        const FVector Normal = Face.GetUnitAxis(EAxis::X);   // a widget quad's normal
        const FVector Right  = Face.GetUnitAxis(EAxis::Y);
        const FVector Up     = Face.GetUnitAxis(EAxis::Z);

        // Where a standing player's eyes are relative to a screen: in front
        // of it and above it. A trace straight at the centre is not enough --
        // a lid leaning away only occludes the upper part of its own panel,
        // so the bug that shipped survives a single centre shot.
        const FVector Centre = Panel->GetComponentLocation();
        const FVector Eye = Centre + Normal * 60.0 + FVector(0.0, 0.0, 40.0);

        // Sample the corners as well as the middle, inset so the very edge
        // of the quad is not what decides the result.
        const FVector2D Size = Panel->GetDrawSize() * Panel->GetComponentScale().X * 0.5 * 0.7;
        const TArray<FVector> Targets = {
            Centre,
            Centre + Right * Size.X + Up * Size.Y,
            Centre - Right * Size.X + Up * Size.Y,
            Centre + Right * Size.X - Up * Size.Y,
            Centre - Right * Size.X - Up * Size.Y,
        };

        int32 Blocked = 0;
        FString FirstBlocker;
        for (const FVector& Target : Targets)
        {
            FCollisionQueryParams Params(SCENE_QUERY_STAT(ScreenReachable), false);
            FHitResult Hit;
            Owner->GetWorld()->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility, Params);
            if (Hit.GetComponent() != Panel)
            {
                ++Blocked;
                if (FirstBlocker.IsEmpty())
                {
                    FirstBlocker = GetNameSafe(Hit.GetComponent());
                }
            }
        }

        Test.AddInfo(FString::Printf(
            TEXT("%s: panelLoc=%s scale=%s eye=%s blocked=%d/%d firstBlocker=%s"),
            What, *Centre.ToCompactString(), *Panel->GetComponentScale().ToCompactString(),
            *Eye.ToCompactString(), Blocked, Targets.Num(),
            FirstBlocker.IsEmpty() ? TEXT("-") : *FirstBlocker));

        Test.TestEqual(
            FString::Printf(TEXT("%s: every part of the screen is reachable from where a player stands"), What),
            Blocked, 0);

        // A non-uniform scale reaching the panel squashes the quad and its
        // collision box together; that was the laptop's first failure.
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
