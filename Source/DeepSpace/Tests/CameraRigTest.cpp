#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCameraRigTest,
    "DeepSpace.Player.CameraRig",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The Blueprint stores a template for every native component it inherits, so a
 * C++ change can leave it carrying stale values -- and the symptom is the view
 * shaking rather than anything obviously broken: with bUsePawnControlRotation
 * off the camera does not turn with the controller, while PlaceCamera still
 * swings its position by the view's yaw.
 *
 * This spawns the character as the game builds it -- the Blueprint, not the
 * C++ class -- so a stale template fails here instead of in play.
 */
bool FCameraRigTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CameraRigTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UClass* CharacterClass = LoadClass<ADeepSpaceCharacter>(
        nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));

    if (TestNotNull(TEXT("BP_DeepSpaceCharacter loads"), CharacterClass))
    {
        ADeepSpaceCharacter* Character =
            World->SpawnActor<ADeepSpaceCharacter>(CharacterClass, FVector::ZeroVector, FRotator::ZeroRotator);

        if (TestNotNull(TEXT("the character spawns"), Character))
        {
            UCameraComponent* Camera = Character->FindComponentByClass<UCameraComponent>();
            if (TestNotNull(TEXT("it has a camera"), Camera))
            {
                TestTrue(TEXT("the camera turns with the controller"),
                         Camera->bUsePawnControlRotation);
                TestEqual(TEXT("the camera hangs off the capsule, not a spring arm"),
                          Camera->GetAttachParent(),
                          static_cast<USceneComponent*>(Character->GetCapsuleComponent()));
            }

            // A spring arm is what caused the original three camera bugs; its
            // return would silently reintroduce all of them.
            TestEqual(TEXT("there is no spring arm"),
                      Character->GetComponentsByTag(USceneComponent::StaticClass(), TEXT("CameraArm")).Num(), 0);
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
