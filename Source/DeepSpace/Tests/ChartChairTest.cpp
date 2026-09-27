#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/InteractableComponent.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipConsole.h"
#include "Ship/ShipLaptop.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipNavScreen.h"
#include "Ship/ShipNavState.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/StarSystem.h"

#if WITH_DEV_AUTOMATION_TESTS

// One test and no children: a test path with children becomes a group, and a
// group silently stops running its own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FChartChairTest,
    "DeepSpace.Ship.ChartChair",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ChartChairTestLocal
{
    /** Where the layout puts the two desk screens' glass, world space:
     *  hauler_layout's MAP_SCREEN and NAV_SCREEN, resolved (the cockpit's
     *  (x, y) is the world's (x + 1410, y - 200)). The chart chair is the
     *  chart's use transform, 126 cm aft of its glass. */
    const FVector MapGlass(1711.0, 0.0, 105.0);
    const FVector ChartGlass(1711.0, 85.0, 105.0);

    /** A blocking box, as solid to a body as the ship's deck is. */
    AActor* SpawnBlock(UWorld* World, const FVector& Centre, const FVector& Extent)
    {
        AActor* Block = World->SpawnActor<AActor>();
        UBoxComponent* Box = NewObject<UBoxComponent>(Block);
        Box->InitBoxExtent(Extent);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Block->SetRootComponent(Box);
        Box->RegisterComponent();
        Block->SetActorLocation(Centre);
        return Block;
    }

    /** Holds Mesh in the sitting idle's first frame. */
    void PoseSitting(USkeletalMeshComponent* Mesh, UAnimSequence* Idle)
    {
        Mesh->PlayAnimation(Idle, true);
        Mesh->SetPosition(0.0f);
        Mesh->TickAnimation(0.0f, false);
        Mesh->RefreshBoneTransforms();
    }

    /**
     * The character as the game builds it, holding the sitting idle, possessed
     * by a controller with a camera manager: the seat's view limits and the
     * zoom's look lock are the controller's, and the eyes are where the idle
     * puts the head. Spawned into a world already playing, so its BeginPlay
     * runs, with no controller yet to make a HUD for. With bSitting false it
     * is left in its reference pose, standing, as a player who walks up to
     * the chair is.
     */
    ADeepSpaceCharacter* SpawnSitter(FAutomationTestBase& Test, UWorld* World, const FVector& Feet,
                                     APlayerController*& OutController, bool bSitting = true)
    {
        OutController = nullptr;
        UClass* CharacterClass = LoadClass<ADeepSpaceCharacter>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
        UAnimSequence* Idle = LoadObject<UAnimSequence>(
            nullptr, TEXT("/Game/Characters/DeepSpace/Anims/RTG_sitting_idle.RTG_sitting_idle"));
        if (!Test.TestNotNull(TEXT("BP_DeepSpaceCharacter loads"), CharacterClass) ||
            !Test.TestNotNull(TEXT("the sitting idle loads"), Idle))
        {
            return nullptr;
        }
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const float HalfHeight = GetDefault<ADeepSpaceCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        ADeepSpaceCharacter* Character = World->SpawnActor<ADeepSpaceCharacter>(
            CharacterClass, Feet + FVector(0.0, 0.0, HalfHeight + 2.0), FRotator::ZeroRotator, Spawn);
        if (!Test.TestNotNull(TEXT("the character spawns"), Character))
        {
            return nullptr;
        }
        if (bSitting)
        {
            PoseSitting(Character->GetMesh(), Idle);
        }

        OutController = World->SpawnActor<APlayerController>();
        if (!Test.TestNotNull(TEXT("a controller spawns"), OutController))
        {
            return Character;
        }
        if (!OutController->PlayerCameraManager)
        {
            OutController->SpawnPlayerCameraManager();
        }
        OutController->Possess(Character);
        return Character;
    }

    /** Turns the head to look from the eyes at Point, and lets a frame run. */
    void LookAt(ADeepSpaceCharacter* Character, APlayerController* Controller, const FVector& Point)
    {
        Controller->SetControlRotation((Point - Character->GetEyeLocation()).Rotation());
        Character->Tick(0.016f);
        Controller->SetControlRotation((Point - Character->GetEyeLocation()).Rotation());
        Character->Tick(0.016f);
    }

    /** The field of view the character fits Screen to, headless: no
     *  viewport, so 16:9, on the axis the player's settings keep. */
    float FittedFov(const AShipScreen* Screen, const UCameraComponent& Camera)
    {
        const ADeepSpaceCharacter::FFramingView View = ADeepSpaceCharacter::ResolveFramingView(
            FIntPoint(0, 0), GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint, Camera);
        return Screen->GetUseFieldOfView(View.Aspect, View.Constraint, Camera.AspectRatio);
    }
}

/**
 * The chart chair is a seat, not a lock (system map spec, decision 13, from
 * the developer's ruling: "copilot doesn't auto lock to jump menu, they can
 * choose either jump menu or map and that will zoom the screen ... When map
 * is zoomed, (Tab) switches between planets in the system"). E at the chart
 * sits the player with the view their own; E then zooms whichever of the
 * chart and the map the view is on, with that screen's fitted framing, and
 * again goes back; on neither it stands up. Tab cycles the target only on the
 * zoomed map. The helm never zooms, and the laptop still frames as it sits.
 *
 * Every screen is spawned before play begins, because a widget component
 * builds its collision body in BeginPlay and E finds what to zoom by tracing
 * for it.
 */
bool FChartChairTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ChartChairTestLocal;

    FSkyWorld Test(TEXT("ChartChairWorld"));
    UWorld* World = Test.World;
    AShipMapScreen* Map = World->SpawnActor<AShipMapScreen>(MapGlass, FRotator::ZeroRotator);
    AShipNavScreen* Chart = World->SpawnActor<AShipNavScreen>(ChartGlass, FRotator::ZeroRotator);
    APilotSeat* Helm = World->SpawnActor<APilotSeat>(HelmSeat, FRotator::ZeroRotator);
    // Far from the cockpit, so nothing there is in its view or its reach.
    AShipLaptop* Laptop = World->SpawnActor<AShipLaptop>(FVector(0.0, 3000.0, 75.0), FRotator::ZeroRotator);
    // Another world screen, in reach of the chart chair and turned to face
    // it from starboard (yaw 90 turns the panel's -X face to -Y): the
    // engineering console, which no seat may drive or zoom.
    AShipConsole* Console = World->SpawnActor<AShipConsole>(FVector(1604.0, 230.0, 125.0), FRotator(0.0, 90.0, 0.0));
    // The deck, its top at Z = 0 as the ship's is, for standing up onto.
    SpawnBlock(World, FVector(1500.0, 0.0, -10.0), FVector(600.0, 600.0, 10.0));
    SpawnBlock(World, FVector(0.0, 3000.0, -10.0), FVector(300.0, 300.0, 10.0));
    if (!TestNotNull(TEXT("the map spawns"), Map) || !TestNotNull(TEXT("the chart spawns"), Chart) ||
        !TestNotNull(TEXT("the helm spawns"), Helm) || !TestNotNull(TEXT("the laptop spawns"), Laptop) ||
        !TestNotNull(TEXT("the console spawns"), Console))
    {
        return false;
    }
    Test.BeginPlay();

    UShipSubsystem* Ship = Test.Ship;
    const TOptional<FStarSystem> Home = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
    if (!TestTrue(TEXT("the ship opens in a system with worlds to cycle"), Home.IsSet() && Home->Planets.Num() >= 2))
    {
        return false;
    }
    const FSystemId HomeId = Home->Stub.Id;
    const auto Target = [&Ship]() { return Ship->GetTarget(); };
    const auto Orbit = [&HomeId](int32 Index) { return TOptional<FBodyId>(FBodyId{ HomeId, Index, -1 }); };

    // Stood a metre and a half behind the chair, where a player walks up from.
    const FVector StoodFeet(1450.0, 85.0, 0.0);
    APlayerController* Controller = nullptr;
    ADeepSpaceCharacter* Player = SpawnSitter(*this, World, StoodFeet, Controller);
    UCameraComponent* Camera = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
    UWidgetInteractionComponent* Pointer = Player ? Player->GetPointer() : nullptr;
    if (!Player || !Controller || !TestNotNull(TEXT("with a camera"), Camera) || !TestNotNull(TEXT("and a pointer"), Pointer))
    {
        return false;
    }
    APlayerCameraManager* CameraManager = Controller->PlayerCameraManager;
    if (!TestNotNull(TEXT("the controller has a camera manager"), CameraManager))
    {
        return false;
    }
    const float Walking = Camera->FieldOfView;
    const auto Prompt = [Player]() { return Player->GetCurrentPrompt().ToString(); };

    // -- Sitting: the chair, not the chart --------------------------------------
    Chart->GetInteractable()->Interact(Player);
    Player->Tick(0.016f);
    TestTrue(TEXT("E at the chart sits the player in its chair"), Player->IsInScreenChair());
    TestFalse(TEXT("and frames nothing: the chart no longer zooms on sit"), Player->IsUsingScreen());
    TestEqual(TEXT("seated"), Player->GetPosture(), EPosture::Seated);
    TestFalse(TEXT("the head is the player's: look input is not held"), Controller->IsLookInputIgnored());
    TestTrue(TEXT("within the seated limits, as at the helm: 100 degrees of yaw either way"),
             FMath::IsNearlyEqual(CameraManager->ViewYawMin, -100.0f) && FMath::IsNearlyEqual(CameraManager->ViewYawMax, 100.0f));
    TestTrue(TEXT("and 70 of pitch"),
             FMath::IsNearlyEqual(CameraManager->ViewPitchMin, -70.0f) && FMath::IsNearlyEqual(CameraManager->ViewPitchMax, 70.0f));
    TestTrue(TEXT("at the walking field of view"), FMath::IsNearlyEqual(Camera->FieldOfView, Walking, 0.01f));
    TestFalse(TEXT("no cursor"), Controller->bShowMouseCursor);
    TestTrue(TEXT("the pointer is no cursor"), Pointer->InteractionSource != EWidgetInteractionSource::Mouse);
    TestTrue(TEXT("the body is seen, as at the helm"), Player->GetMesh()->IsVisible());

    // The body sits on the chair as it sits at the helm, the same chair prop
    // either side of the desk: the eyes at the helm's seated height, not a
    // cushion's height above it.
    const FVector ChairEye = Player->GetEyeLocation();
    AddInfo(FString::Printf(TEXT("chart chair eye (%.1f, %.1f, %.1f); the helm's is at %.1f"),
                            ChairEye.X, ChairEye.Y, ChairEye.Z, PilotEye.Z));
    TestTrue(FString::Printf(TEXT("the seated eye is at the helm's height (%.1f, want %.1f)"), ChairEye.Z, PilotEye.Z),
             FMath::Abs(ChairEye.Z - PilotEye.Z) <= PilotEyeBob);

    TestEqual(TEXT("the view starts on the chart, so E names it"), Prompt(), FString(TEXT("Chart")));
    TestFalse(TEXT("the pointer is off on the chart: it is not drivable seated"), Pointer->IsActive());
    TestFalse(TEXT("so nothing reports as pointed at"), Player->IsPointingAtScreen());

    // Tab, unzoomed: nothing.
    Ship->SetTarget(*Orbit(1));
    Player->CycleTarget();
    TestTrue(TEXT("Tab in the chair, unzoomed, changes nothing"), Target() == Orbit(1));

    // -- E on the chart: its zoom, and back -------------------------------------
    const FRotator OnChart = Player->GetViewRotation();
    Player->PressInteract();
    Player->Tick(0.016f);
    TestTrue(TEXT("E looking at the chart zooms it"), Player->GetZoomedScreen() == Chart);
    TestTrue(TEXT("which is using a screen"), Player->IsUsingScreen());
    TestTrue(TEXT("the eyes at the chart's framing"),
             FVector::Dist(Player->GetEyeLocation(), Chart->GetViewTransform().GetLocation()) < 1.0);
    const float ChartFov = FittedFov(Chart, *Camera);
    TestTrue(FString::Printf(TEXT("with the chart's fitted field of view (%.2f, want %.2f)"), Camera->FieldOfView, ChartFov),
             FMath::IsNearlyEqual(Camera->FieldOfView, ChartFov, 0.01f));
    TestTrue(TEXT("the look held while the mouse is a cursor"), Controller->IsLookInputIgnored() && Controller->bShowMouseCursor);
    TestTrue(TEXT("the pointer follows the cursor, and is on"),
             Pointer->InteractionSource == EWidgetInteractionSource::Mouse && Pointer->IsActive());
    TestFalse(TEXT("the body is hidden from the framed view"), Player->GetMesh()->IsVisible());
    TestEqual(TEXT("zoomed, E says it goes back"), Prompt(), FString(TEXT("Back")));
    Player->CycleTarget();
    TestTrue(TEXT("Tab zoomed on the chart changes nothing"), Target() == Orbit(1));

    Player->PressInteract();
    Player->Tick(0.016f);
    TestTrue(TEXT("E again goes back to the seat"), Player->IsInScreenChair() && !Player->IsUsingScreen());
    TestTrue(TEXT("looking where the player looked before"), Player->GetViewRotation().Equals(OnChart, 0.01f));
    TestTrue(TEXT("at the walking field of view"), FMath::IsNearlyEqual(Camera->FieldOfView, Walking, 0.01f));
    TestTrue(TEXT("the head the player's again"), !Controller->IsLookInputIgnored() && !Controller->bShowMouseCursor);
    TestTrue(TEXT("the pointer no cursor again"), Pointer->InteractionSource != EWidgetInteractionSource::Mouse);
    TestTrue(TEXT("the body seen again"), Player->GetMesh()->IsVisible());
    TestTrue(TEXT("and the eyes back at the head"), FVector::Dist(Player->GetEyeLocation(), ChairEye) < 2.0);

    // -- A glance to the map: drivable from here, and E zooms it ----------------
    LookAt(Player, Controller, MapGlass);
    TestEqual(TEXT("looking at the map, E names it"), Prompt(), FString(TEXT("Map")));
    TestTrue(TEXT("the pointer is live on the map from the chair, as at the helm"), Pointer->IsActive());
    Pointer->TickComponent(0.016f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("and lands on its glass"),
             Pointer->GetLastHitResult().GetComponent() == static_cast<UPrimitiveComponent*>(Map->GetScreen()));
    Player->CycleTarget();
    TestTrue(TEXT("Tab looking at the map unzoomed changes nothing"), Target() == Orbit(1));

    const FVector Seated = Player->GetActorLocation();
    Player->PressInteract();
    Player->Tick(0.016f);
    TestTrue(TEXT("E looking at the map zooms the map"), Player->GetZoomedScreen() == Map);
    TestTrue(TEXT("the eyes at the map's own framing"),
             FVector::Dist(Player->GetEyeLocation(), Map->GetViewTransform().GetLocation()) < 1.0);
    const float MapFov = FittedFov(Map, *Camera);
    TestTrue(FString::Printf(TEXT("with the map's fitted field of view (%.2f, want %.2f)"), Camera->FieldOfView, MapFov),
             FMath::IsNearlyEqual(Camera->FieldOfView, MapFov, 0.01f));
    TestTrue(TEXT("the body unmoved in the chair"), Player->GetActorLocation().Equals(Seated, 0.01));
    TestEqual(TEXT("zoomed on the map, E says it goes back"), Prompt(), FString(TEXT("Back")));

    // -- Tab on the zoomed map: I, II, ... and round --------------------------
    Ship->ClearTarget();
    const int32 Worlds = Home->Planets.Num();
    bool bInOrder = true;
    for (int32 Press = 0; Press <= Worlds; ++Press)
    {
        Player->CycleTarget();
        bInOrder &= TestTrue(FString::Printf(TEXT("Tab %d on the zoomed map targets world %d"), Press + 1, Press % Worlds + 1),
                             Target() == Orbit(Press % Worlds));
    }
    TestTrue(TEXT("from none to the innermost, outward, and round from the outermost"), bInOrder);

    Player->PressInteract();
    Player->Tick(0.016f);
    TestFalse(TEXT("back from the map"), Player->IsUsingScreen());
    const TOptional<FBodyId> Before = Target();
    Player->CycleTarget();
    TestTrue(TEXT("Tab back in the seat changes nothing"), Target() == Before);

    // -- The console, from the chair: no seat's screen --------------------------
    // The seat's gate asks for a ship screen that says what a seat may do
    // with it. The console's glass is a world screen in reach and under the
    // view, and is neither: the pointer stays off it, and E is a stand-up.
    {
        LookAt(Player, Controller, Console->GetScreen()->GetComponentLocation());
        FHitResult Seen;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(ChartChairConsole), false, Player);
        const FVector Eye = Player->GetEyeLocation();
        World->LineTraceSingleByChannel(Seen, Eye, Eye + Player->GetViewRotation().Vector() * 240.0, ECC_Visibility, Params);
        TestTrue(TEXT("from the chair, the view lands on the console's glass, in reach"),
                 Seen.GetComponent() == static_cast<UPrimitiveComponent*>(Console->GetScreen()));
        TestFalse(TEXT("but the pointer is off on it"), Pointer->IsActive());
        TestEqual(TEXT("and E, looking at it, stands up"), Prompt(), FString(TEXT("Stand up")));
        Player->PressInteract();
        Player->Tick(0.016f);
        TestFalse(TEXT("E looking at the console zooms nothing"), Player->IsUsingScreen());
        TestFalse(TEXT("and stands the player up"), Player->IsInScreenChair());
        Chart->GetInteractable()->Interact(Player);
        Player->Tick(0.016f);
        TestTrue(TEXT("sat back in the chart chair"), Player->IsInScreenChair());
    }

    // -- On neither, E stands up ----------------------------------------------
    LookAt(Player, Controller, ChairEye + FVector(-100.0, -300.0, 0.0));
    TestEqual(TEXT("on neither screen, E stands up"), Prompt(), FString(TEXT("Stand up")));
    Player->PressInteract();
    TestFalse(TEXT("E on neither stands up"), Player->IsInScreenChair());
    {
        const float Bottom = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        TestTrue(FString::Printf(TEXT("onto the floor (feet at %.1f cm)"), Bottom), Bottom >= -0.1f && Bottom <= 5.0f);
        TestTrue(TEXT("where they stood before sitting"),
                 FVector::Dist2D(Player->GetActorLocation(), StoodFeet) < 1.0);
        TestEqual(TEXT("walking again"), Player->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
        const APlayerCameraManager* Defaults = GetDefault<APlayerCameraManager>();
        TestTrue(TEXT("with the seat's limits gone"),
                 FMath::IsNearlyEqual(CameraManager->ViewYawMin, Defaults->ViewYawMin) &&
                     FMath::IsNearlyEqual(CameraManager->ViewPitchMin, Defaults->ViewPitchMin));
    }

    // Standing up from a zoom goes the whole way: E zooms, and standing up
    // from there gives everything back.
    Chart->GetInteractable()->Interact(Player);
    Player->Tick(0.016f);
    Player->PressInteract();
    TestTrue(TEXT("zoomed on the chart again"), Player->GetZoomedScreen() == Chart);
    Player->StopUsingScreen();
    TestTrue(TEXT("standing up from a zoom leaves nothing framed and nothing held"),
             !Player->IsInScreenChair() && !Player->IsUsingScreen() && !Controller->IsLookInputIgnored() &&
                 !Controller->bShowMouseCursor && FMath::IsNearlyEqual(Camera->FieldOfView, Walking, 0.01f));

    // -- The helm: look and click, never a zoom, and no Tab --------------------
    Player->SitIn(Helm);
    LookAt(Player, Controller, MapGlass);
    TestTrue(TEXT("at the helm, the map is live under the view"), Pointer->IsActive());
    TestEqual(TEXT("and E still stands up"), Prompt(), FString(TEXT("Stand up")));
    Ship->SetTarget(*Orbit(0));
    Player->CycleTarget();
    TestTrue(TEXT("Tab at the helm changes nothing"), Target() == Orbit(0));
    Player->PressInteract();
    TestFalse(TEXT("E at the helm, looking at the map, stands up"), Player->IsSeated());
    TestFalse(TEXT("and zooms nothing"), Player->IsUsingScreen());

    // -- The laptop: still framed as it sits ----------------------------------
    Player->UseScreen(Laptop);
    TestTrue(TEXT("sitting at the laptop frames it at once"), Player->GetZoomedScreen() == Laptop);
    TestEqual(TEXT("and E there stands up"), Prompt(), FString(TEXT("Stand up")));
    Player->CycleTarget();
    TestTrue(TEXT("Tab at the laptop changes nothing"), Target() == Orbit(0));
    Player->PressInteract();
    TestFalse(TEXT("E at the laptop stands up"), Player->IsInScreenChair() || Player->IsUsingScreen());

    Controller->UnPossess();

    // -- Sat down from standing: the view is still on the chart once sat ------
    // In the game E runs from input, before the frame's animation, so when
    // UseScreen aims the first view the body is still in the standing pose,
    // its eyes some 50 cm above where they settle. The sitting idle takes
    // over in the frames after and the eyes sink onto the seated head; the
    // view must be on the chart when they get there, or the second press --
    // to read it -- stands the player straight back up. The sitter above
    // was posed sitting before it sat, and cannot see this.
    {
        APlayerController* WalkerController = nullptr;
        ADeepSpaceCharacter* Walker = SpawnSitter(*this, World, StoodFeet, WalkerController, false);
        UAnimSequence* Idle = LoadObject<UAnimSequence>(
            nullptr, TEXT("/Game/Characters/DeepSpace/Anims/RTG_sitting_idle.RTG_sitting_idle"));
        if (Walker && WalkerController && Idle)
        {
            Walker->Tick(0.016f);
            const float StandingEye = Walker->GetEyeLocation().Z;
            TestTrue(FString::Printf(TEXT("the walker's eyes are a standing body's (%.1f cm)"), StandingEye),
                     StandingEye > PilotEye.Z + 30.0f);

            Chart->GetInteractable()->Interact(Walker);
            PoseSitting(Walker->GetMesh(), Idle);
            for (int32 Frame = 0; Frame < 120; ++Frame)
            {
                Walker->Tick(0.016f);
            }
            const FVector Settled = Walker->GetEyeLocation();
            AddInfo(FString::Printf(TEXT("sat from standing: eye settled at z %.1f, pitch %.1f"),
                                    Settled.Z, Walker->GetViewRotation().Pitch));
            TestTrue(FString::Printf(TEXT("sat from standing, the eyes settle at the seated height (%.1f, want %.1f)"),
                                     Settled.Z, PilotEye.Z),
                     FMath::Abs(Settled.Z - PilotEye.Z) <= PilotEyeBob);
            TestEqual(TEXT("and the view is on the chart, so E still names it"),
                      Walker->GetCurrentPrompt().ToString(), FString(TEXT("Chart")));
            Walker->PressInteract();
            Walker->Tick(0.016f);
            TestTrue(TEXT("one press after sitting zooms the chart"), Walker->GetZoomedScreen() == Chart);
            Walker->StopUsingScreen();
        }
        if (WalkerController)
        {
            WalkerController->UnPossess();
        }
    }
    return true;
}

#endif
