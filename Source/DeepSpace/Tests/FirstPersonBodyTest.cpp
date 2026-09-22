#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCameraFollowsHeadTest,
    "DeepSpace.Player.CameraFollowsHead",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** Evaluate the mesh's current animation now, on this thread. */
    void EvaluatePose(USkeletalMeshComponent* Mesh)
    {
        Mesh->TickAnimation(0.0f, false);
        Mesh->RefreshBoneTransforms();
    }
}

bool FCameraFollowsHeadTest::RunTest(const FString& Parameters)
{
    // The camera rides the head bone, and the head bone is hidden so the view
    // is not from inside it. Hiding a bone can also stop the engine animating
    // it (USkeletalMeshComponent::ExcludeHiddenBones): the head then freezes
    // in its reference pose and the camera with it -- crouching, the view
    // stays at standing height. Play a crouch on the real character, set up
    // exactly as the game does, and check the head goes down with the body.
    UClass* CharacterClass = LoadClass<ADeepSpaceCharacter>(
        nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
    UAnimSequence* Crouch = LoadObject<UAnimSequence>(
        nullptr, TEXT("/Game/Characters/DeepSpace/Anims/RTG_crouching_idle.RTG_crouching_idle"));
    if (!TestNotNull(TEXT("BP_DeepSpaceCharacter loads"), CharacterClass) ||
        !TestNotNull(TEXT("RTG_crouching_idle loads"), Crouch))
    {
        return false;
    }

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CameraFollowsHeadTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    ADeepSpaceCharacter* Character = World->SpawnActor<ADeepSpaceCharacter>(CharacterClass);
    if (TestNotNull(TEXT("the character spawns"), Character))
    {
        USkeletalMeshComponent* Mesh = Character->GetMesh();
        const FName Head = TEXT("head");

        Mesh->PlayAnimation(Crouch, false);
        Mesh->SetPosition(0.0f);
        EvaluatePose(Mesh);
        const float Visible = Mesh->GetSocketTransform(Head, RTS_Component).GetLocation().Z;

        Character->ConfigureFirstPersonBody();
        EvaluatePose(Mesh);
        const float Hidden = Mesh->GetSocketTransform(Head, RTS_Component).GetLocation().Z;

        AddInfo(FString::Printf(TEXT("crouched head: %.1f cm before hiding, %.1f cm after"), Visible, Hidden));

        // The retargeted crouch idle carries the head bone to about 82 cm.
        TestTrue(TEXT("before hiding, the head follows the crouch"), Visible < 100.0f);
        TestTrue(TEXT("after hiding, the head still follows the crouch"), Hidden < 100.0f);
        TestTrue(TEXT("hiding the head does not move it"), FMath::Abs(Hidden - Visible) < 1.0f);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
