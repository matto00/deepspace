#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipLaptop.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScreenPointerTest,
    "DeepSpace.Ship.ScreenPointer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The pointer is the whole interaction model for screens, so the properties
 * that make it feel like pointing at a thing in the room are what is guarded
 * here: it aims along the *view* rather than along some component's idea of
 * forward, it reaches exactly as far as the hands do, it finds the panel's
 * real surface, and it is switched off in the one place it would fight the
 * player -- the pilot's seat.
 *
 * What is deliberately *not* asserted is Slate's own hit path: whether the
 * ray lands on a particular button. A UWidgetComponent only builds its
 * widget through UGameInstance, and only paints -- and so only fills the hit
 * test grid -- with a real RHI. Neither exists under `-nullrhi`, which is how
 * the suite runs. Everything up to and including the surface the ray lands on
 * is checked here; which control that surface is showing is a playtest
 * question, and the reason the prototype is built first.
 */
bool FScreenPointerTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ScreenPointerTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    // Spawned before play begins, because a widget component builds its Slate
    // window and its collision body in BeginPlay. A screen spawned into an
    // already-running world is never traceable at all.
    ADeepSpaceCharacter* Character = World->SpawnActor<ADeepSpaceCharacter>(
        FVector::ZeroVector, FRotator::ZeroRotator);
    AShipLaptop* Laptop = World->SpawnActor<AShipLaptop>(
        FVector(200.0f, 0.0f, 0.0f), FRotator::ZeroRotator);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    if (TestNotNull(TEXT("the character spawns"), Character)
        && TestNotNull(TEXT("a laptop spawns"), Laptop))
    {
        UWidgetInteractionComponent* Pointer = Character->GetPointer();
        if (TestNotNull(TEXT("the character has a pointer"), Pointer))
        {
            Character->Tick(0.016f);

            TestTrue(TEXT("the pointer is live on foot"), Pointer->IsActive());
            TestEqual(TEXT("it reaches exactly as far as the hands do"),
                      Pointer->InteractionDistance, 250.0f);
            TestEqual(TEXT("it is a virtual Slate user, not the real one"),
                      Pointer->VirtualUserIndex, 8);

            // Aimed along the view, from the eyes. A pointer traced from the
            // capsule, or along a component's forward, drifts from what the
            // player can see they are pointing at.
            TestTrue(TEXT("the pointer starts at the eyes"),
                     Pointer->GetComponentLocation().Equals(Character->GetEyeLocation(), 0.1f));
            TestTrue(TEXT("the pointer aims along the view"),
                     Pointer->GetComponentRotation().Equals(Character->GetViewRotation(), 0.1f));

            // Put the panel squarely in front of the eyes, inside reach. This
            // is the geometry contract: the quad faces -X at yaw 0, the same
            // way every wall-mounted fixture in the ship does, and it blocks
            // Visibility. Get either wrong and nothing aboard can be clicked.
            const FVector Wanted = Character->GetEyeLocation() + FVector(150.0f, 0.0f, 0.0f);
            Laptop->AddActorWorldOffset(Wanted - Laptop->GetScreen()->GetComponentLocation());
            Character->Tick(0.016f);
            Pointer->TickComponent(0.016f, LEVELTICK_All, nullptr);
            TestEqual(TEXT("the pointer lands on the panel in front of the player"),
                      Pointer->GetLastHitResult().GetComponent(),
                      static_cast<UPrimitiveComponent*>(Laptop->GetScreen()));

            // Out of reach the same panel is not driveable, so the console
            // cannot be operated from across the room.
            Laptop->AddActorWorldOffset(FVector(400.0f, 0.0f, 0.0f));
            Character->Tick(0.016f);
            Pointer->TickComponent(0.016f, LEVELTICK_All, nullptr);
            TestNull(TEXT("a panel out of reach is not driveable"),
                     Pointer->GetLastHitResult().GetComponent());
            TestFalse(TEXT("and nothing reports as pointed at"),
                      Character->IsPointingAtScreen());

            // Seated, the controls belong to the ship.
            APilotSeat* Seat = World->SpawnActor<APilotSeat>(
                FVector::ZeroVector, FRotator::ZeroRotator);
            Character->SitIn(Seat);
            Character->Tick(0.016f);
            TestFalse(TEXT("the pointer is off at the helm"), Pointer->IsActive());
            Character->StandUp();
            Character->Tick(0.016f);
            TestTrue(TEXT("standing up brings the pointer back"), Pointer->IsActive());
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
