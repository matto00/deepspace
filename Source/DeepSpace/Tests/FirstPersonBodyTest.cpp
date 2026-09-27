#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipDressingSubsystem.h"
#include "Tests/HaulerDressingMarkers.h"
#include "Tests/SkyTestWorld.h"

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
    FCameraStaysOutOfClutterTest,
    "DeepSpace.Player.CameraStaysOutOfClutter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCameraStaysOutOfClutterTest::RunTest(const FString& Parameters)
{
    // The capsule never reaches a surface, but the eye does: crouched, it
    // leans some 30 cm past the capsule at about a metre up, level with
    // what stands on a workbench or a counter, and standing at the rack it
    // is in a crate's band. Clutter the camera's sweep passed through put
    // the view inside a toolbox. So the hauler's own clutter, dressed from
    // its real markers, and the character put exactly where its eye would
    // land in the middle of the largest thing on them.
    HaulerDressingMarkers::FMarkers Markers;
    FString Error;
    const bool bLoaded = HaulerDressingMarkers::Load(Markers, Error);
    if (!TestTrue(FString::Printf(TEXT("the hauler's markers load %s"), *Error), bLoaded))
    {
        return false;
    }

    // The world's own dressing, whatever a session last set.
    IConsoleVariable* LivedIn = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Dress.LivedIn"));
    IConsoleVariable* Seed = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Dress.Seed"));
    const FString WasLivedIn = LivedIn ? LivedIn->GetString() : FString();
    const FString WasSeed = Seed ? Seed->GetString() : FString();
    if (LivedIn) { LivedIn->Set(TEXT("1"), ECVF_SetByConsole); }
    if (Seed) { Seed->Set(TEXT("-1"), ECVF_SetByConsole); }

    UWorld* World = MakeWorld(TEXT("CameraClutterTestWorld"));
    ON_SCOPE_EXIT
    {
        DropWorld(World);
        if (LivedIn) { LivedIn->Set(*WasLivedIn, ECVF_SetByConsole); }
        if (Seed) { Seed->Set(*WasSeed, ECVF_SetByConsole); }
    };

    HaulerDressingMarkers::Spawn(World, Markers);
    UShipDressingSubsystem* Dressing = World->GetSubsystem<UShipDressingSubsystem>();
    if (!TestNotNull(TEXT("a game world has a dressing subsystem"), Dressing))
    {
        return false;
    }
    Dressing->Redress();

    TArray<FBox> Things;
    if (const AActor* Clutter = Dressing->GetClutter())
    {
        TInlineComponentArray<UInstancedStaticMeshComponent*> Layers(Clutter);
        for (const UInstancedStaticMeshComponent* Layer : Layers)
        {
            const FBox MeshBox = Layer->GetStaticMesh()->GetBoundingBox();
            for (int32 I = 0; I < Layer->GetInstanceCount(); ++I)
            {
                FTransform Instance;
                Layer->GetInstanceTransform(I, Instance, /*bWorldSpace*/ true);
                Things.Add(MeshBox.TransformBy(Instance));
            }
        }
    }
    if (!TestTrue(FString::Printf(TEXT("the hauler is dressed (%d things)"), Things.Num()), Things.Num() > 0))
    {
        return false;
    }

    ADeepSpaceCharacter* Character =
        PoseCharacter(*this, World, TEXT("/Game/Characters/DeepSpace/Anims/RTG_crouching_idle.RTG_crouching_idle"));
    if (!Character)
    {
        return false;
    }
    constexpr float NearClip = 10.0f;
    constexpr float ProbeClear = 13.0f;    // the eye sweep's sphere, and a margin
    Character->PlaceCamera(0.0f, FRotator::ZeroRotator);
    const FVector Lean = Character->GetEyeLocation() - Character->GetActorLocation();

    // The largest thing whose middle the eye can be put in with the sweep
    // starting clear of everything: a start inside a box proves nothing.
    Things.Sort([](const FBox& A, const FBox& B) { return A.GetVolume() > B.GetVolume(); });
    const FBox* Target = nullptr;
    FVector Stand = FVector::ZeroVector;
    for (const FBox& Thing : Things)
    {
        const FVector At = Thing.GetCenter() - Lean;
        const FVector Axis(At.X, At.Y, Thing.GetCenter().Z);
        const bool bClear = !Things.ContainsByPredicate([&Axis, ProbeClear](const FBox& Other)
        {
            return Other.ComputeSquaredDistanceToPoint(Axis) < FMath::Square(ProbeClear);
        });
        if (bClear)
        {
            Target = &Thing;
            Stand = At;
            break;
        }
    }
    if (!TestNotNull(TEXT("something on the hauler's surfaces the eye can lean into"), Target))
    {
        return false;
    }

    Character->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
    const FVector Wants = Stand + Lean;
    const FVector Size = Target->GetSize();
    AddInfo(FString::Printf(TEXT("the eye leans into a %.0f x %.0f x %.0f cm thing centred at (%.0f, %.0f, %.0f)"),
                            Size.X, Size.Y, Size.Z, Wants.X, Wants.Y, Wants.Z));
    TestTrue(TEXT("unobstructed, the eye would be inside it: the premise"), Target->IsInsideOrOn(Wants));

    Character->PlaceCamera(0.0f, FRotator::ZeroRotator);
    const FVector Eye = Character->GetEyeLocation();
    const float Gap = FMath::Sqrt(Target->ComputeSquaredDistanceToPoint(Eye));
    AddInfo(FString::Printf(TEXT("the eye stops %.1f cm short of it"), Gap));
    TestFalse(TEXT("the eye is inside nothing on the ship's surfaces"),
              Things.ContainsByPredicate([&Eye](const FBox& Thing) { return Thing.IsInsideOrOn(Eye); }));
    TestTrue(TEXT("and stops short of it by more than the near plane"), Gap > NearClip);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSeatedEyeIsPilotEyeTest,
    "DeepSpace.Player.SeatedEyeIsPilotEye",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSeatedEyeIsPilotEyeTest::RunTest(const FString& Parameters)
{
    // SkyTestWorld::PilotEye is the eye every helm test looks from, and the
    // layout's checks from the helm -- the map in view, the glass along the
    // nose -- look from the same point. It was a standing height, 170 cm,
    // and a seated pilot's eye is some 45 cm lower: from there the nose
    // line met the port desk screen before the glass, and the layout's
    // check passed only because the eye was in the wrong place. So sit the
    // real character in a helm seat, play the idle it sits in, and measure
    // where PlaceCamera puts its eyes through the whole clip.
    UWorld* World = MakeWorld(TEXT("SeatedEyeTestWorld"));
    ON_SCOPE_EXIT { DropWorld(World); };
    const TCHAR* SittingIdle = TEXT("/Game/Characters/DeepSpace/Anims/RTG_sitting_idle.RTG_sitting_idle");
    ADeepSpaceCharacter* Character = PoseCharacter(*this, World, SittingIdle);
    APilotSeat* Seat = World->SpawnActor<APilotSeat>(SkyTestWorld::HelmSeat, FRotator::ZeroRotator);
    if (!Character || !TestNotNull(TEXT("the helm seat spawns"), Seat))
    {
        return false;
    }
    Character->SitIn(Seat);
    if (!TestTrue(TEXT("the character is seated"), Character->IsSeated()))
    {
        return false;
    }

    // Looking level along the nose, the pose SitIn sets. A long step lets
    // the eye's height damping settle onto each frame's head.
    const FRotator Level(0.0f, Seat->GetSeatTransform().Rotator().Yaw, 0.0f);
    USkeletalMeshComponent* Mesh = Character->GetMesh();
    const UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, SittingIdle);
    const float Length = Idle ? Idle->GetPlayLength() : 0.0f;
    constexpr int32 Samples = 40;
    FVector Sum = FVector::ZeroVector;
    FBox Range(ForceInit);
    for (int32 I = 0; I <= Samples; ++I)
    {
        Mesh->SetPosition(Length * I / Samples);
        EvaluatePose(Mesh);
        Character->PlaceCamera(10.0f, Level);
        const FVector Eye = Character->GetEyeLocation();
        Sum += Eye;
        Range += Eye;
    }
    const FVector Mean = Sum / (Samples + 1);
    const FVector& Want = SkyTestWorld::PilotEye;
    AddInfo(FString::Printf(TEXT("seated eye through the idle: mean (%.1f, %.1f, %.1f), z %.1f to %.1f; PilotEye (%.1f, %.1f, %.1f)"),
                            Mean.X, Mean.Y, Mean.Z, Range.Min.Z, Range.Max.Z, Want.X, Want.Y, Want.Z));

    TestTrue(TEXT("the clip has a length to sample"), Length > 0.0f);
    TestTrue(FString::Printf(TEXT("PilotEye is the seated eye, to a centimetre (off by %.1f cm)"), FVector::Dist(Mean, Want)),
             Mean.Equals(Want, 1.0));
    // The offset every seat aims its first view from (UseScreen, the chart
    // chair) is this same eye, from the seat's anchor.
    const FVector Offset = Mean - Seat->GetSeatTransform().GetLocation();
    TestTrue(FString::Printf(TEXT("SeatedEyeOffset is the seated eye, to a centimetre (measured %.1f, %.1f, %.1f)"),
                             Offset.X, Offset.Y, Offset.Z),
             Offset.Equals(ADeepSpaceCharacter::SeatedEyeOffset, 1.0));
    TestTrue(TEXT("the idle keeps the eye within PilotEyeBob of it, up and down"),
             Range.Min.Z >= Want.Z - SkyTestWorld::PilotEyeBob && Range.Max.Z <= Want.Z + SkyTestWorld::PilotEyeBob);
    TestTrue(TEXT("and within PilotEyeBob of it sideways"),
             (Range.Max - Range.Min).X <= 2.0 * SkyTestWorld::PilotEyeBob && (Range.Max - Range.Min).Y <= 2.0 * SkyTestWorld::PilotEyeBob);
    return true;
}

#endif
