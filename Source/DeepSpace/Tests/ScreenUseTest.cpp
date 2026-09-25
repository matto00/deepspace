#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipLaptop.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScreenUseTest,
    "DeepSpace.Ship.ScreenUse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Sitting down at a screen. The laptop's sliders were too fine to aim at
 * from across the galley, so using it seats the player, frames the panel and
 * hands the pointer to a real cursor -- and standing up must put every one of
 * those back, or the player is left unable to move with a cursor on screen.
 */
bool FScreenUseTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ScreenUseTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UStaticMesh* Chamfer = LoadObject<UStaticMesh>(
        nullptr, TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube"));

    AShipLaptop* Laptop = World->SpawnActor<AShipLaptop>(FVector(0.0, 0.0, 75.0), FRotator::ZeroRotator);
    ADeepSpaceCharacter* Player = World->SpawnActor<ADeepSpaceCharacter>(
        FVector(300.0, 0.0, 90.0), FRotator::ZeroRotator);

    if (TestNotNull(TEXT("the laptop spawns"), Laptop) &&
        TestNotNull(TEXT("the character spawns"), Player) &&
        TestNotNull(TEXT("SM_ChamferCube loads"), Chamfer))
    {
        for (UActorComponent* Component : Laptop->GetComponents())
        {
            if (UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Component))
            {
                AsMesh->SetStaticMesh(Chamfer);
            }
        }
        Laptop->RerunConstructionScripts();

        TestTrue(TEXT("the laptop is usable"), Laptop->IsUsable());
        TestFalse(TEXT("nobody is using it to start with"), Player->IsUsingScreen());

        Player->UseScreen(Laptop);

        TestTrue(TEXT("using it seats the player"), Player->IsUsingScreen());
        TestEqual(TEXT("and the body sits"), Player->GetPosture(), EPosture::Seated);
        TestEqual(TEXT("movement is off"),
                  Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);

        // The seat is in front of the panel, facing it -- not wherever the
        // player happened to walk up from.
        const FVector Seat = Laptop->GetUseTransform().GetLocation();
        TestTrue(TEXT("the player is moved to the seat"),
                 FVector::Dist2D(Player->GetActorLocation(), Seat) < 1.0);

        // The camera leaves the head and frames the panel.
        Player->PlaceCamera(0.016f, Player->GetViewRotation());
        TestTrue(TEXT("the camera frames the screen"),
                 FVector::Dist(Player->GetEyeLocation(),
                               Laptop->GetViewTransform().GetLocation()) < 1.0);

        // The cursor drives the widget, not the head.
        if (const UWidgetInteractionComponent* Pointer = Player->GetPointer())
        {
            TestEqual(TEXT("the pointer follows the cursor"),
                      Pointer->InteractionSource, EWidgetInteractionSource::Mouse);
        }

        Player->StopUsingScreen();

        TestFalse(TEXT("standing up releases the screen"), Player->IsUsingScreen());
        TestEqual(TEXT("and movement comes back"),
                  Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
        if (const UWidgetInteractionComponent* Pointer = Player->GetPointer())
        {
            TestEqual(TEXT("and the pointer goes back to the view"),
                      Pointer->InteractionSource, EWidgetInteractionSource::World);
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
