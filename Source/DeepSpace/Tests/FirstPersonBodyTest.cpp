#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
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

    /** A throwaway world, current, so actors spawned into it can be traced. */
    UWorld* MakeWorld(const TCHAR* Name)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        return World;
    }

    void DropWorld(UWorld* World)
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    /**
     * The character as the game builds it, standing at the origin, holding
     * the first frame of Clip. Null if the assets are missing.
     */
    ADeepSpaceCharacter* PoseCharacter(FAutomationTestBase& Test, UWorld* World, const TCHAR* Clip)
    {
        UClass* CharacterClass = LoadClass<ADeepSpaceCharacter>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
        UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, Clip);
        if (!Test.TestNotNull(TEXT("BP_DeepSpaceCharacter loads"), CharacterClass) ||
            !Test.TestNotNull(TEXT("the animation loads"), Anim))
        {
            return nullptr;
        }

        ADeepSpaceCharacter* Character = World->SpawnActor<ADeepSpaceCharacter>(
            CharacterClass, FVector::ZeroVector, FRotator::ZeroRotator);
        if (!Test.TestNotNull(TEXT("the character spawns"), Character))
        {
            return nullptr;
        }
        Character->ConfigureFirstPersonBody();
        Character->GetMesh()->PlayAnimation(Anim, true);
        Character->GetMesh()->SetPosition(0.0f);
        EvaluatePose(Character->GetMesh());
        return Character;
    }

    /** A blocking slab whose near face is Distance away, along Direction. */
    void SpawnWall(UWorld* World, const FVector& Direction, float Distance)
    {
        constexpr float Thickness = 20.0f;
        AActor* Wall = World->SpawnActor<AActor>();
        UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
        Box->InitBoxExtent(FVector(Thickness, 300.0f, 300.0f));
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Wall->SetRootComponent(Box);
        Box->RegisterComponent();
        Wall->SetActorLocationAndRotation(Direction * (Distance + Thickness), Direction.Rotation());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCameraStaysInsideWallsTest,
    "DeepSpace.Player.CameraStaysInsideWalls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCameraStaysInsideWallsTest::RunTest(const FString& Parameters)
{
    // The animation does not know about walls. The retargeted crouch idle
    // carries the head bone 57 cm from the capsule's axis -- 23 cm outside a
    // 34 cm capsule -- so a camera attached to the head sat outside the hull
    // whenever the player crouched against a wall, and the ship was seen
    // through. PlaceCamera sweeps out from the axis instead.
    UWorld* World = MakeWorld(TEXT("CameraWallsTestWorld"));
    ADeepSpaceCharacter* Character =
        PoseCharacter(*this, World, TEXT("/Game/Characters/DeepSpace/Anims/RTG_crouching_idle.RTG_crouching_idle"));
    if (Character)
    {
        const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
        constexpr float NearClip = 10.0f;

        // Unobstructed, the head is well outside the capsule: the premise.
        Character->PlaceCamera(0.0f, FRotator::ZeroRotator);
        const FVector Free = Character->GetEyeLocation();
        const FVector Lean = FVector(Free.X, Free.Y, 0.0f);
        AddInfo(FString::Printf(TEXT("crouched, nothing in the way: the eyes are %.1f cm from the axis"),
                                Lean.Size()));
        TestTrue(TEXT("the crouch really does carry the head out of the capsule"),
                 Lean.Size() > Radius);

        // A wall where the capsule stops against it: the closest the player
        // can ever get to one, in the direction the crouch leans.
        const FVector Direction = Lean.GetSafeNormal();
        SpawnWall(World, Direction, Radius);
        Character->PlaceCamera(0.0f, FRotator::ZeroRotator);
        const FVector Blocked = Character->GetEyeLocation();
        const float Reach = FVector::DotProduct(FVector(Blocked.X, Blocked.Y, 0.0f), Direction);
        AddInfo(FString::Printf(TEXT("with a wall at %.0f cm: the eyes reach %.1f cm"), Radius, Reach));
        TestTrue(TEXT("the eyes stop short of the wall by more than the near plane"),
                 Reach <= Radius - NearClip);
    }

    DropWorld(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCameraDoesNotDiveWhenLookingDownTest,
    "DeepSpace.Player.CameraDoesNotDiveWhenLookingDown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCameraDoesNotDiveWhenLookingDownTest::RunTest(const FString& Parameters)
{
    // The eye offset is forward of the head bone. A spring arm applies its
    // socket offset in the *view's* rotation, so looking straight down swung
    // those 8 cm downwards and dropped the camera into the neck: the body was
    // seen from the inside, and past it to whatever was behind. The offset is
    // the view's yaw only now, so pitch cannot move the camera at all.
    UWorld* World = MakeWorld(TEXT("CameraPitchTestWorld"));
    ADeepSpaceCharacter* Character =
        PoseCharacter(*this, World, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
    if (Character)
    {
        Character->PlaceCamera(0.0f, FRotator::ZeroRotator);
        const FVector Level = Character->GetEyeLocation();

        for (const float Pitch : {-90.0f, -45.0f, 89.0f})
        {
            Character->PlaceCamera(0.0f, FRotator(Pitch, 0.0f, 0.0f));
            const FVector Looked = Character->GetEyeLocation();
            TestTrue(FString::Printf(TEXT("looking %.0f deg does not move the camera"), Pitch),
                     Looked.Equals(Level, 0.01f));
        }

        // And it is in front of the head, not inside it.
        const FVector Head = Character->GetMesh()->GetSocketLocation(TEXT("head"));
        AddInfo(FString::Printf(TEXT("head at (%.1f, %.1f, %.1f); eyes at (%.1f, %.1f, %.1f)"),
                                Head.X, Head.Y, Head.Z, Level.X, Level.Y, Level.Z));
        TestTrue(TEXT("the eyes are forward of the head bone"), Level.X > Head.X);
        TestTrue(TEXT("the eyes are above the head bone"), Level.Z > Head.Z);
    }

    DropWorld(World);
    return true;
}

#endif
