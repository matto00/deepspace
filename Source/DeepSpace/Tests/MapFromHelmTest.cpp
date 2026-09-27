#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipNavScreen.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMapFromHelmTest,
    "DeepSpace.Ship.MapFromHelm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace MapFromHelmTestLocal
{
    /** The map's and the chart's glass, world space, as the layout places
     *  them (hauler_layout's MAP_SCREEN and NAV_SCREEN). */
    const FVector MapGlass(1711.0, 0.0, 105.0);
    const FVector ChartGlass(1711.0, 85.0, 105.0);

    /**
     * Whether the pointer holds Key down. The component keeps its own set of
     * pressed keys and says nothing about it; a key it believes held makes
     * its next press of that key a repeat, which it drops -- so this set is
     * exactly what decides whether the next click reaches the map. Read
     * through a derived name, the one way C++ lets a protected member be
     * named from outside, and never written.
     */
    struct FPointerKeys : public UWidgetInteractionComponent
    {
        static bool IsDown(const UWidgetInteractionComponent& Pointer, const FKey& Key)
        {
            return (Pointer.*(&FPointerKeys::PressedKeys)).Contains(Key);
        }
    };

    /** Turns the head to look from the eyes at Point, and lets a frame run:
     *  twice, since the eyes move a little as the head turns. */
    void LookAt(ADeepSpaceCharacter* Character, APlayerController* Controller, const FVector& Point)
    {
        for (int32 Pass = 0; Pass < 2; ++Pass)
        {
            Controller->SetControlRotation((Point - Character->GetEyeLocation()).Rotation());
            Character->Tick(0.016f);
        }
    }
}

/**
 * The map from the helm (system map spec, decision 2, as ruled: "pilot just
 * has look and click control of map"). Seated at the helm, the pointer is
 * live only while the view is on a screen that says it is drivable seated --
 * the map -- and it then lands on the map's glass; on the chart, two metres
 * off and inside the hands' reach, it is off, because the chart says no.
 * Looking away mid-press lets the button go, so the next click on the map is
 * a click and not a dropped repeat.
 *
 * The real character in the helm seat, holding the sitting idle, so the
 * pointer traces from the pilot's own eye. As with ScreenPointer, it checks
 * up to the surface the ray lands on: under -nullrhi the Slate hit-test grid
 * is never filled, so which control is under the ray is a playtest question.
 */
bool FMapFromHelmTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace MapFromHelmTestLocal;

    FSkyWorld Test(TEXT("MapFromHelmWorld"));
    UWorld* World = Test.World;
    // Before play, because the widget component builds its collision body
    // in BeginPlay: a screen spawned later is never traceable.
    AShipMapScreen* Map = World->SpawnActor<AShipMapScreen>(MapGlass, FRotator::ZeroRotator);
    AShipNavScreen* Chart = World->SpawnActor<AShipNavScreen>(ChartGlass, FRotator::ZeroRotator);
    APilotSeat* Helm = World->SpawnActor<APilotSeat>(HelmSeat, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the map spawns"), Map) || !TestNotNull(TEXT("the chart spawns"), Chart) ||
        !TestNotNull(TEXT("the helm spawns"), Helm))
    {
        return false;
    }
    Test.BeginPlay();

    UClass* CharacterClass = LoadClass<ADeepSpaceCharacter>(
        nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
    UAnimSequence* Idle = LoadObject<UAnimSequence>(
        nullptr, TEXT("/Game/Characters/DeepSpace/Anims/RTG_sitting_idle.RTG_sitting_idle"));
    if (!TestNotNull(TEXT("BP_DeepSpaceCharacter loads"), CharacterClass) || !TestNotNull(TEXT("the sitting idle loads"), Idle))
    {
        return false;
    }
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADeepSpaceCharacter* Pilot = World->SpawnActor<ADeepSpaceCharacter>(
        CharacterClass, HelmSeat + FVector(-150.0, 0.0, 100.0), FRotator::ZeroRotator, Spawn);
    APlayerController* Controller = World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("the pilot spawns"), Pilot) || !TestNotNull(TEXT("with a controller"), Controller))
    {
        return false;
    }
    Pilot->GetMesh()->PlayAnimation(Idle, true);
    Pilot->GetMesh()->SetPosition(0.0f);
    Pilot->GetMesh()->TickAnimation(0.0f, false);
    Pilot->GetMesh()->RefreshBoneTransforms();
    Controller->Possess(Pilot);

    Pilot->SitIn(Helm);
    UWidgetInteractionComponent* Pointer = Pilot->GetPointer();
    if (!TestTrue(TEXT("the pilot is at the helm"), Pilot->IsSeated()) || !TestNotNull(TEXT("with a pointer"), Pointer))
    {
        return false;
    }

    // Looking out along the nose, as SitIn leaves the view: nothing to drive.
    Pilot->Tick(0.016f);
    TestFalse(TEXT("looking ahead from the helm the pointer is off"), Pointer->IsActive());

    // The map, from the pilot's own eye.
    LookAt(Pilot, Controller, MapGlass);
    const FVector Eye = Pilot->GetEyeLocation();
    AddInfo(FString::Printf(TEXT("the helm's eye, looking at the map: (%.1f, %.1f, %.1f); PilotEye (%.1f, %.1f, %.1f)"),
                            Eye.X, Eye.Y, Eye.Z, PilotEye.X, PilotEye.Y, PilotEye.Z));
    // The forward offset swings with the head's yaw, some 5 cm for a look
    // 34 degrees to starboard; the rest is the idle.
    TestTrue(TEXT("the eye is the helm's"), FVector::Dist(Eye, PilotEye) < PilotEyeBob + 8.0);
    TestTrue(TEXT("looking at the map from the helm, the pointer is live"), Pointer->IsActive());
    // Handed the gate's own trace, which ignores the chair the pilot sits in:
    // the helm's eye is inside the seat's reach box, and a trace of the
    // pointer's own met that before the map.
    TestEqual(TEXT("handed the view's own trace"), Pointer->InteractionSource, EWidgetInteractionSource::Custom);
    TestTrue(TEXT("from the eyes"), Pointer->GetComponentLocation().Equals(Eye, 0.1));
    Pointer->TickComponent(0.016f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("and its own trace lands on the map's glass"),
             Pointer->GetLastHitResult().GetComponent() == static_cast<UPrimitiveComponent*>(Map->GetScreen()));
    TestEqual(TEXT("E is still stand up: nothing sits you at the map, and the helm never zooms"),
              Pilot->GetCurrentPrompt().ToString(), FString(TEXT("Stand up")));

    // The chart is within the hands' reach of the helm; only its class keeps
    // the pointer off it.
    TestTrue(TEXT("the chart is within reach of the helm's eye"), FVector::Dist(Eye, ChartGlass) < Pointer->InteractionDistance);

    // A press on the map, then the head turns to the chart before the
    // button comes up.
    Pilot->PressPointer();
    TestTrue(TEXT("a press on the map holds the button"), FPointerKeys::IsDown(*Pointer, EKeys::LeftMouseButton));
    LookAt(Pilot, Controller, ChartGlass);
    TestFalse(TEXT("looking at the chart from the helm, the pointer is off"), Pointer->IsActive());
    TestFalse(TEXT("so nothing reports as pointed at"), Pilot->IsPointingAtScreen());
    TestFalse(TEXT("and the button was let go on looking away"), FPointerKeys::IsDown(*Pointer, EKeys::LeftMouseButton));

    // Back on the map, a new press is a press, not a dropped repeat.
    LookAt(Pilot, Controller, MapGlass);
    TestTrue(TEXT("looking back at the map, the pointer is live again"), Pointer->IsActive());
    Pilot->PressPointer();
    TestTrue(TEXT("and the next press registers"), FPointerKeys::IsDown(*Pointer, EKeys::LeftMouseButton));
    Pilot->ReleasePointer();
    TestFalse(TEXT("and lets go"), FPointerKeys::IsDown(*Pointer, EKeys::LeftMouseButton));

    // Standing up gives the pointer back for every screen, as on foot.
    Pilot->StandUp();
    Pilot->Tick(0.016f);
    TestTrue(TEXT("standing, the pointer is live again whatever it is on"), !Pilot->IsSeated() && Pointer->IsActive());

    Controller->UnPossess();
    return true;
}

#endif
