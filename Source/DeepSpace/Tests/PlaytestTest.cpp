#include "Animation/AnimSequence.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipNavScreen.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkySystem.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "UI/ShipHUDWidget.h"
#include "UI/SystemMapView.h"
#include "UI/SystemMapWidget.h"
#include "UI/TargetMarker.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The second playtest's fixes, end to end: what the developer is asked to do
 * in the next playtest, each step done here first, through the same seams
 * the keys and the pointer reach -- the pawn's hands, the map's orrery, the
 * chart's buttons -- and read back in the words the HUD and the map print.
 * Each half has its own tests; these check what the playtest will try.
 *
 * Siblings, and no test path with children: a path with children becomes a
 * group, and a group silently stops running its own body.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestClickTargetFromHelmTest, "DeepSpace.Playtest.ClickTargetFromHelm",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestLeverToLightAndBackTest, "DeepSpace.Playtest.LeverToLightAndBack",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestCapBindsOnCollisionTest, "DeepSpace.Playtest.CapBindsOnCollision",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestCapIgnoresAMissTest, "DeepSpace.Playtest.CapIgnoresAMiss",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestInSystemJumpToTargetTest, "DeepSpace.Playtest.InSystemJumpToTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestInterstellarJumpAtRestTest, "DeepSpace.Playtest.InterstellarJumpAtRest",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestEtaCountsDownTest, "DeepSpace.Playtest.EtaCountsDown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestKeysTurnTheShipTest, "DeepSpace.Playtest.KeysTurnTheShip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PlaytestTestLocal
{
    using namespace SkyTestWorld;

    /** A frame at 30 Hz: the slowest the game is expected to run, and so
     *  the coarsest the helm's taps and the cap's approach ever see. */
    constexpr float Dt = 1.0f / 30.0f;

    /** The map's and the chart's glass, world space (hauler_layout's
     *  MAP_SCREEN and NAV_SCREEN). */
    const FVector MapGlass(1711.0, 0.0, 105.0);
    const FVector ChartGlass(1711.0, 85.0, 105.0);

    void Console(UWorld* World, const TCHAR* Name, const TArray<FString>& Args = {})
    {
        FOutputDeviceNull Quiet;
        if (IConsoleObject* Command = IConsoleManager::Get().FindConsoleObject(Name))
        {
            if (Command->AsCommand())
            {
                Command->AsCommand()->Execute(Args, World, Quiet);
            }
        }
    }

    TOptional<FStarSystem> SystemHere(const FSkyWorld& Test)
    {
        return Test.Universe->GetSystemAt(Test.Ship->GetFlightState().GetUniversePosition());
    }

    FBodyId WorldId(const FStarSystem& System, int32 Orbit)
    {
        return FBodyId{ System.Stub.Id, Orbit, -1 };
    }

    /** A world as the sky and the drive have it: body Orbit + 1, the star
     *  first, with the floor the cap stops at. */
    struct FWorldFloor
    {
        FUniversePosition Centre;
        double Radius = 0.0;
        double Floor = 0.0;
    };

    FWorldFloor WorldFloor(const FStarSystem& System, int32 Orbit)
    {
        const FSkySystem Sky = LocalSystem::Here(TOptional<FStarSystem>(System));
        const FSkyBody& Body = Sky.Bodies[Orbit + 1];
        return { Body.Position, Body.Radius, UShipSubsystem::FloorFor(Body) };
    }

    /** How far above the world's floor the ship is, cm; negative under it. */
    double Room(const UShipSubsystem& Ship, const FWorldFloor& World)
    {
        return Ship.GetFlightState().GetUniversePosition().DistanceTo(World.Centre) - World.Radius - World.Floor;
    }

    int32 LargestPlanet(const FStarSystem& System)
    {
        int32 Largest = INDEX_NONE;
        for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
        {
            if (Largest == INDEX_NONE || System.Planets[Index].RadiusEarth > System.Planets[Largest].RadiusEarth)
            {
                Largest = Index;
            }
        }
        return Largest;
    }

    /** The outermost world the ship is not already near enough to fly to. */
    int32 FarWorld(const UShipSubsystem& Ship, const FStarSystem& Here)
    {
        for (int32 Orbit = Here.Planets.Num() - 1; Orbit >= 0; --Orbit)
        {
            if (!Ship.IsNearEnoughToFly(Here, WorldId(Here, Orbit)))
            {
                return Orbit;
            }
        }
        return INDEX_NONE;
    }

    FQuat Facing(const FVector& Direction)
    {
        return FRotationMatrix::MakeFromX(Direction.GetSafeNormal()).ToQuat();
    }

    /**
     * Puts the ship LightSeconds of light out from World's floor, on the line
     * from its centre through where the ship is, with the nose on the
     * centre: far enough out that the drive reaches 1 c before the cap has
     * anything to say. The opening shot is only 40,000 km per Earth radius
     * out, which the spool-up never leaves.
     */
    void BackOff(UShipSubsystem* Ship, const FWorldFloor& World, double LightSeconds)
    {
        const FVector Out = (Ship->GetFlightState().GetUniversePosition() - World.Centre).GetSafeNormal();
        const FUniversePosition At = World.Centre + Out * (World.Radius + World.Floor + LightSeconds * ShipDriveLever::LightCmPerSecond);
        Ship->PlaceShip(At, Facing(-Out));
    }

    /** The nearest body floor the ray from the ship along Direction meets,
     *  cm, and whether it is World's: that the nose is on World and on
     *  nothing nearer. */
    bool FirstOnPathIs(const FShipFlightState& Flight, const FVector& Direction, const FWorldFloor& World)
    {
        TOptional<double> Nearest;
        TOptional<double> Its;
        for (const FFlightSurface& Surface : Flight.GetSurfaces())
        {
            const TOptional<double> D = Surface.bInsideOut ? TOptional<double>() : ShipFlight::RayToFloor(Surface, Flight.GetUniversePosition(), Direction);
            if (D && (!Nearest || *D < *Nearest))
            {
                Nearest = D;
            }
            if (D && Surface.Centre.DistanceTo(World.Centre) < 1.0)
            {
                Its = D;
            }
        }
        return Its && Nearest && *Its == *Nearest;
    }

    /** Whether a ray from the ship along Direction meets no body's floor
     *  sphere: the edge is left out, since every ray from inside meets it. */
    bool MissesEveryBody(const FShipFlightState& Flight, const FVector& Direction)
    {
        for (const FFlightSurface& Surface : Flight.GetSurfaces())
        {
            if (!Surface.bInsideOut && ShipFlight::RayToFloor(Surface, Flight.GetUniversePosition(), Direction))
            {
                return false;
            }
        }
        return true;
    }

    /**
     * What a pilot's hands do to put the nose on a direction given in ship
     * axes: the body rates the flight integrates, about +Z to swing the nose
     * to starboard and about +Y to put it down, harder the further off, and
     * full rate from 20 degrees.
     */
    FVector SteerToward(const FVector& Local)
    {
        const auto Rate = [](double Radians) { return FMath::Clamp(Radians / FMath::DegreesToRadians(20.0), -1.0, 1.0); };
        const double Starboard = FMath::Atan2(Local.Y, Local.X);
        const double Up = FMath::Atan2(Local.Z, FVector2D(Local.X, Local.Y).Size());
        return FVector(0.0, -Rate(Up), Rate(Starboard));
    }

    /** The real pawn -- the Blueprint the level spawns -- holding the
     *  sitting idle, with a controller, as MapFromHelm has it. */
    struct FSeatedPilot
    {
        ADeepSpaceCharacter* Pilot = nullptr;
        APlayerController* Controller = nullptr;
    };

    FSeatedPilot SpawnBlueprintPilot(UWorld* World, const FVector& Near)
    {
        FSeatedPilot Out;
        UClass* CharacterClass = LoadClass<ADeepSpaceCharacter>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
        UAnimSequence* Idle = LoadObject<UAnimSequence>(
            nullptr, TEXT("/Game/Characters/DeepSpace/Anims/RTG_sitting_idle.RTG_sitting_idle"));
        if (!CharacterClass || !Idle)
        {
            return Out;
        }
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Out.Pilot = World->SpawnActor<ADeepSpaceCharacter>(CharacterClass, Near, FRotator::ZeroRotator, Spawn);
        Out.Controller = World->SpawnActor<APlayerController>();
        if (!Out.Pilot || !Out.Controller)
        {
            return Out;
        }
        Out.Pilot->GetMesh()->PlayAnimation(Idle, true);
        Out.Pilot->GetMesh()->SetPosition(0.0f);
        Out.Pilot->GetMesh()->TickAnimation(0.0f, false);
        Out.Pilot->GetMesh()->RefreshBoneTransforms();
        Out.Controller->Possess(Out.Pilot);
        return Out;
    }

    /** Turns the head to look from the eyes at Point, twice, since the eyes
     *  move a little as the head turns. */
    void LookAt(ADeepSpaceCharacter* Character, APlayerController* Controller, const FVector& Point)
    {
        for (int32 Pass = 0; Pass < 2; ++Pass)
        {
            Controller->SetControlRotation((Point - Character->GetEyeLocation()).Rotation());
            Character->Tick(0.016f);
        }
    }

    /** Where a point of a widget component's drawing is in the world: the
     *  inverse of UWidgetComponent::GetLocalHitLocation, for a flat panel. */
    FVector PanelPointToWorld(const UWidgetComponent& Panel, const FVector2D& WidgetPoint)
    {
        const FVector2D Draw = Panel.GetCurrentDrawSize();
        const FVector2D Pivot = Panel.GetPivot();
        const FVector Local(0.0, -(WidgetPoint.X - Draw.X * Pivot.X), -(WidgetPoint.Y - Draw.Y * Pivot.Y));
        return Panel.GetComponentTransform().TransformPosition(Local);
    }

    /** One frame as the game runs it: the pawn hands its keys over in the
     *  actor tick, and the ship, ticking after, applies them. */
    void Frame(ADeepSpaceCharacter* Player, UShipSubsystem* Ship, float Seconds = Dt)
    {
        Player->Tick(Seconds);
        Ship->Tick(Seconds);
    }

    /** The rest of a tap: the key comes up in the frame it went down. */
    void Tap(ADeepSpaceCharacter* Player, UShipSubsystem* Ship, int32 Direction)
    {
        Player->TapLever(Direction);
        Player->HoldLever(0);
        Frame(Player, Ship);
    }
}

// ---------------------------------------------------------------------------

/**
 * Seated at the helm, the pilot looks at a world's dot on the middle screen
 * and clicks it, and it is the target (map spec decision 2, ruling 4: "pilot
 * just has look and click control of map"). The dot is found on the map's
 * own drawing and laid out on the panel in the world; the head turns to it;
 * the pointer, live because the map says it is drivable seated, lands on
 * the glass where the dot is; and that landing point, clicked, picks that
 * world. The HUD and the map then say the same line about it.
 *
 * Under -nullrhi Slate's hit-test grid is never filled, so the press does
 * not route itself to the orrery; the landing point is handed to the
 * orrery's click, the whole of its input once a press is released.
 */
bool FPlaytestClickTargetFromHelmTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTestLocal;

    FSkyWorld Test(TEXT("PlaytestClickWorld"));
    // Before play: the widget component builds its collision body in BeginPlay.
    AShipMapScreen* Map = Test.World->SpawnActor<AShipMapScreen>(MapGlass, FRotator::ZeroRotator);
    APilotSeat* Helm = Test.World->SpawnActor<APilotSeat>(HelmSeat, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the map spawns"), Map) || !TestNotNull(TEXT("the helm spawns"), Helm))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;

    const FSeatedPilot Seated = SpawnBlueprintPilot(Test.World, HelmSeat + FVector(-150.0, 0.0, 100.0));
    if (!TestNotNull(TEXT("the pilot spawns"), Seated.Pilot) || !TestNotNull(TEXT("with a controller"), Seated.Controller))
    {
        return false;
    }
    ADeepSpaceCharacter* Pilot = Seated.Pilot;
    Pilot->SitIn(Helm);
    UWidgetInteractionComponent* Pointer = Pilot->GetPointer();
    if (!TestTrue(TEXT("the pilot is at the helm"), Pilot->IsSeated()) || !TestNotNull(TEXT("with a pointer"), Pointer))
    {
        return false;
    }

    UWidgetComponent* Panel = Map->GetScreen();
    Panel->TickComponent(0.016f, LEVELTICK_All, nullptr);
    USystemMapWidget* Widget = Cast<USystemMapWidget>(Panel->GetUserWidgetObject());
    const TOptional<FStarSystem> Home = SystemHere(Test);
    if (!TestNotNull(TEXT("the map's widget"), Widget) ||
        !TestTrue(TEXT("the opening shot is in a system with worlds"), Home.IsSet() && Home->Planets.Num() > 0))
    {
        return false;
    }
    Widget->RefreshFromShip();
    const SystemMap::FMapLayout* Layout = Widget->GetLayout();
    const UCanvasPanelSlot* OrreryCell = Cast<UCanvasPanelSlot>(Widget->GetView()->Slot);
    if (!TestNotNull(TEXT("the map has its drawing"), Layout) || !TestNotNull(TEXT("and the orrery a place on it"), OrreryCell))
    {
        return false;
    }
    TestFalse(TEXT("nothing is targeted at the opening"), Ship->GetTarget().IsSet());

    // The outermost world: the one farthest from the star on the orrery,
    // and never the opening shot's frame by accident.
    const int32 Orbit = Home->Planets.Num() - 1;
    const FVector2D OnPanel = OrreryCell->GetPosition() + Layout->Dots[Orbit].Centre;
    const FVector DotInWorld = PanelPointToWorld(*Panel, OnPanel);

    LookAt(Pilot, Seated.Controller, DotInWorld);
    TestTrue(TEXT("looking at the world's dot from the helm, the pointer is live"), Pointer->IsActive());
    Pointer->TickComponent(0.016f, LEVELTICK_All, nullptr);
    if (!TestTrue(TEXT("and lands on the map's glass"),
                  Pointer->GetLastHitResult().GetComponent() == static_cast<UPrimitiveComponent*>(Panel)))
    {
        return false;
    }
    const FVector2D Landed = Pointer->Get2DHitLocation();
    // Not to the pixel: the eye the pointer traces from moves a few
    // millimetres as the head turns onto the dot. Well inside the reach a
    // click has of its dot, which is what decides what it picks.
    TestTrue(FString::Printf(TEXT("where the dot is drawn, well inside its pick radius: (%.1f, %.1f) against (%.1f, %.1f)"),
                             Landed.X, Landed.Y, OnPanel.X, OnPanel.Y),
             FVector2D::Distance(Landed, OnPanel) <= 0.5 * SystemMap::PickRadius);

    Pilot->PressPointer();
    const TOptional<int32> Picked = Widget->GetView()->ClickAt(Landed - OrreryCell->GetPosition());
    Pilot->ReleasePointer();
    TestTrue(TEXT("the click picks the world under it"), Picked == TOptional<int32>(Orbit));
    TestTrue(TEXT("and it is the ship's target"), Ship->GetTarget() == TOptional<FBodyId>(WorldId(*Home, Orbit)));

    Widget->RefreshFromShip();
    const FString Name = NavText::WorldName(Home->Planets[Orbit]);
    const FString HudLine = UShipHUDWidget::TargetLineText(*Ship, Home).ToString();
    TestTrue(FString::Printf(TEXT("the HUD names it (\"%s\")"), *HudLine), HudLine.Contains(Name));
    TestEqual(TEXT("and the map says the same line"), Widget->GetTargetText().ToString(), HudLine);
    TestTrue(TEXT("its row carries the mark"), Widget->GetRowText(Orbit).ToString().StartsWith(UNavigationWidget::PlottedMark));
    TestTrue(TEXT("the ship is still being flown from the helm: nothing zoomed"), Pilot->IsSeated() && !Pilot->IsUsingScreen());

    Seated.Controller->UnPossess();
    return true;
}

// ---------------------------------------------------------------------------

/**
 * The drive lever from STOP to 1 c and back, a tap at a time, then X (ruling
 * 1, flight-feel decisions 1-4). Every tap is one notch, the ship settles on
 * it without overshooting, and the corner's motion line then reads the
 * notch's own label for the ship and for the lever; at the top it says
 * "1 C", and one more Shift does nothing. Down again the same way, and X
 * brings the ship to rest with both levers at STOP, where it stays through F.
 * Through the pawn's hands, on a path that meets nothing, so the cap never
 * has a say.
 */
bool FPlaytestLeverToLightAndBackTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTestLocal;

    FSkyWorld Test(TEXT("PlaytestLeverWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    if (!TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    Ship->SetPilot(Player);
    Test.Step(0.0f);

    const FShipFlightState& Flight = Ship->GetFlightState();
    const auto Notch = [&]() { return Flight.GetCommand().DriveNotch; };

    // Straight out of the plane, or whichever way meets nothing.
    FVector Away = FVector::ZeroVector;
    for (const FVector& Candidate : { FVector::UpVector, FVector::DownVector, FVector(0.3, 0.2, 1.0), FVector(-0.2, 0.3, -1.0) })
    {
        if (MissesEveryBody(Flight, Candidate.GetSafeNormal()))
        {
            Away = Candidate.GetSafeNormal();
            break;
        }
    }
    if (!TestFalse(TEXT("a way out that meets no body"), Away.IsZero()))
    {
        return false;
    }
    Ship->PlaceShip(Flight.GetUniversePosition(), Facing(Away));
    Frame(Player, Ship, 0.0f);

    Player->PressDrive();
    Frame(Player, Ship);
    TestTrue(TEXT("F puts the drive's lever live"), Ship->IsDriveEngaged());
    TestEqual(TEXT("at STOP"), Notch(), 0);

    const int32 Top = Flight.GetDriveNotchCount() - 1;
    TestEqual(TEXT("the lever is STOP and eighteen notches"), Top, 18);

    bool bFree = true;
    bool bNeverFalls = true;
    bool bNeverOvershoots = true;
    double Previous = Flight.GetSpeed();
    for (int32 Want = 1; Want <= Top; ++Want)
    {
        Tap(Player, Ship, 1);
        const double Speed = ShipDriveLever::NotchSpeed(Want);
        for (int32 Tick = 0; Tick < 90; ++Tick)
        {
            Frame(Player, Ship);
            bFree &= Flight.GetHold() == EFlightHold::Free;
            bNeverFalls &= Flight.GetSpeed() >= Previous * (1.0 - 1e-12);
            bNeverOvershoots &= Flight.GetSpeed() <= Speed * (1.0 + 1e-9);
            Previous = Flight.GetSpeed();
        }
        const FString Words = UShipHUDWidget::SpeedWords(Speed);
        const FString Ink = UShipHUDWidget::MotionLineOf(*Ship).Ink;
        TestEqual(FString::Printf(TEXT("Shift %d is notch %d"), Want, Want), Notch(), Want);
        TestTrue(FString::Printf(TEXT("settled on %s in 3 s (%.6g cm/s)"), *Words, Flight.GetSpeed()),
                 FMath::IsNearlyEqual(Flight.GetSpeed(), Speed, Speed * 1e-3));
        TestTrue(FString::Printf(TEXT("the corner reads the label for the ship and the lever: \"%s\""), *Ink),
                 Ink.StartsWith(Words + NavText::Separator) && Ink.Contains(FString(TEXT("DRIVE ")) + Words));
    }
    TestTrue(TEXT("the cap never has a say on a path that meets nothing"), bFree);
    TestTrue(TEXT("going up, the speed never falls"), bNeverFalls);
    TestTrue(TEXT("and never overshoots a notch"), bNeverOvershoots);
    TestEqual(TEXT("the top notch reads 1 C"), UShipHUDWidget::SpeedWords(ShipDriveLever::NotchSpeed(Top)), FString(TEXT("1 C")));
    TestTrue(FString::Printf(TEXT("and is light, exactly (%.9g cm/s)"), Flight.GetSpeed()),
             FMath::IsNearlyEqual(Flight.GetSpeed(), ShipDriveLever::LightCmPerSecond, ShipDriveLever::LightCmPerSecond * 1e-9));

    Tap(Player, Ship, 1);
    for (int32 Tick = 0; Tick < 30; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestEqual(TEXT("one more Shift at 1 c does nothing: anything faster is a jump"), Notch(), Top);
    TestTrue(TEXT("and the ship stays at light"), Flight.GetSpeed() <= ShipDriveLever::LightCmPerSecond * (1.0 + 1e-9));

    bool bNeverRises = true;
    bool bNeverUndershoots = true;
    Previous = Flight.GetSpeed();
    for (int32 Want = Top - 1; Want >= 1; --Want)
    {
        Tap(Player, Ship, -1);
        const double Speed = ShipDriveLever::NotchSpeed(Want);
        for (int32 Tick = 0; Tick < 90; ++Tick)
        {
            Frame(Player, Ship);
            bNeverRises &= Flight.GetSpeed() <= Previous * (1.0 + 1e-12);
            bNeverUndershoots &= Flight.GetSpeed() >= Speed * (1.0 - 1e-9);
            Previous = Flight.GetSpeed();
        }
        TestEqual(FString::Printf(TEXT("Ctrl down to notch %d"), Want), Notch(), Want);
        TestTrue(FString::Printf(TEXT("settled on %s"), *UShipHUDWidget::SpeedWords(Speed)),
                 FMath::IsNearlyEqual(Flight.GetSpeed(), Speed, Speed * 1e-3));
    }
    TestTrue(TEXT("coming down, the speed never rises"), bNeverRises);
    TestTrue(TEXT("and never drops past a notch"), bNeverUndershoots);

    // X from the bottom notch, and then from light itself.
    Player->PressStop();
    double Seconds = 0.0;
    while (Seconds < 5.0 && Flight.GetSpeed() > 0.0)
    {
        Frame(Player, Ship);
        Seconds += Dt;
    }
    TestTrue(FString::Printf(TEXT("X from 1 km/s: at rest, exactly, in %.2f s"), Seconds), Flight.GetSpeed() == 0.0);
    TestTrue(TEXT("with both levers at STOP"), Notch() == 0 && Flight.GetCommand().Throttle == 0.0);

    Ship->SetDriveLever(Player, Top);
    for (int32 Tick = 0; Tick < 300; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestTrue(TEXT("back at light"), FMath::IsNearlyEqual(Flight.GetSpeed(), ShipDriveLever::LightCmPerSecond, ShipDriveLever::LightCmPerSecond * 1e-3));
    Player->PressStop();
    Seconds = 0.0;
    while (Seconds < 12.0 && Flight.GetSpeed() > 0.0)
    {
        Frame(Player, Ship);
        Seconds += Dt;
    }
    AddInfo(FString::Printf(TEXT("X from 1 c comes to rest in %.2f s"), Seconds));
    TestTrue(FString::Printf(TEXT("X from 1 c: at rest in a known few seconds (%.2f s)"), Seconds),
             Flight.GetSpeed() == 0.0 && Seconds < 10.0);

    Player->PressDrive();
    for (int32 Tick = 0; Tick < 60; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestTrue(TEXT("F after X: cruise, and still at rest"), !Ship->IsDriveEngaged() && Flight.GetSpeed() < 1.0);
    Player->PressDrive();
    for (int32 Tick = 0; Tick < 60; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestTrue(TEXT("and F again: the drive, at STOP, still at rest"), Ship->IsDriveEngaged() && Notch() == 0 && Flight.GetSpeed() == 0.0);
    Tap(Player, Ship, 1);
    TestEqual(TEXT("after X the lever starts again from STOP: one Shift is 1 km/s"), Notch(), 1);
    return true;
}

// ---------------------------------------------------------------------------

/**
 * The soft cap on a collision course (ruling 2, flight-feel decision 5):
 * thirty light seconds out from the largest world, nose on it, the drive
 * set to 1 c: the lever is left alone, all the way to light, until the floor is four seconds off at the present speed; then the
 * cap takes the whole speed, never gives any back, sheds it no faster than
 * the hold's e-every-four-seconds or the braking curve allow, and brings the
 * ship to rest on the floor -- the sky's rendered floor -- without passing
 * it. The corner says HOLDING OFF while it does, and AT THE FLOOR after.
 */
bool FPlaytestCapBindsOnCollisionTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTestLocal;

    FSkyWorld Test(TEXT("PlaytestCapWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    Test.Step(0.0f);
    const TOptional<FStarSystem> Home = SystemHere(Test);
    const int32 Largest = Home ? LargestPlanet(*Home) : INDEX_NONE;
    if (!TestTrue(TEXT("home has a world to close on"), Largest != INDEX_NONE))
    {
        return false;
    }
    const FWorldFloor World = WorldFloor(*Home, Largest);
    const FShipFlightState& Flight = Ship->GetFlightState();
    BackOff(Ship, World, 30.0);
    Test.Step(0.0f);
    if (!TestTrue(TEXT("thirty light seconds out, the nose is on the world and on nothing nearer"),
                  FirstOnPathIs(Flight, Flight.GetUniverseOrientation().GetForwardVector(), World)))
    {
        return false;
    }

    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetDriveEngaged(Pilot, true);
    const int32 Top = Flight.GetDriveNotchCount() - 1;
    Ship->SetDriveLever(Pilot, Top);
    bool bReachedLight = false;

    const double Hold = CVarFloat(TEXT("ds.Drive.HoldSeconds"));
    const double Braking = ShipFlight::BrakingMargin * Flight.GetLimits().LinearAcceleration;
    double Seconds = 0.0;
    double BoundAt = -1.0;
    double LeadAtBind = 0.0;
    double Previous = 0.0;
    double WorstDrop = 0.0;
    bool bUntouchedBefore = true;
    bool bNeverRisesAfter = true;
    bool bNeverUnder = true;
    bool bSaidHoldingOff = false;
    double RestFor = 0.0;
    while (Seconds < 400.0 && RestFor < 1.0)
    {
        Ship->Tick(Dt);
        Seconds += Dt;
        const double Speed = Flight.GetSpeed();
        bNeverUnder &= Room(*Ship, World) >= -1.0;
        if (BoundAt < 0.0 && Flight.GetHold() != EFlightHold::Free)
        {
            BoundAt = Seconds;
            LeadAtBind = Room(*Ship, World) / FMath::Max(Previous, 1.0);
        }
        if (BoundAt < 0.0)
        {
            bUntouchedBefore &= Speed >= Previous * (1.0 - 1e-12);
            bReachedLight |= FMath::IsNearlyEqual(Speed, ShipDriveLever::LightCmPerSecond, ShipDriveLever::LightCmPerSecond * 1e-9);
        }
        else
        {
            bNeverRisesAfter &= Speed <= Previous * (1.0 + 1e-9) + 1e-6;
            // What a frame may shed: the hold's v / N, or the braking curve's
            // deceleration, whichever the cap is on, with a quarter to spare.
            const double Allowed = 1.25 * Dt * FMath::Max(Previous / Hold, Braking) + 1.0;
            WorstDrop = FMath::Max(WorstDrop, (Previous - Speed) / Allowed);
            bSaidHoldingOff |= UShipHUDWidget::AltitudeLineText(*Ship).ToString().Contains(TEXT("HOLDING OFF"));
        }
        RestFor = Speed < 1.0 ? RestFor + Dt : 0.0;
        Previous = Speed;
    }
    AddInfo(FString::Printf(TEXT("bound at %.1f s with %.2f s to the floor; at rest %.1f s in; worst frame %.2f of its allowance"),
                            BoundAt, LeadAtBind, Seconds, WorstDrop));
    TestTrue(TEXT("the cap binds"), BoundAt > 0.0);
    TestTrue(TEXT("until then the lever alone sets the speed, climbing"), bUntouchedBefore);
    TestTrue(TEXT("all the way to light"), bReachedLight);
    TestTrue(FString::Printf(TEXT("it binds with the floor four seconds off at the present speed (%.2f s)"), LeadAtBind),
             FMath::Abs(LeadAtBind - Hold) <= 0.1 * Hold);
    TestTrue(TEXT("once bound it never gives speed back"), bNeverRisesAfter);
    TestTrue(FString::Printf(TEXT("and sheds it smoothly: no frame faster than the hold or the braking curve (%.2f)"), WorstDrop),
             WorstDrop <= 1.0);
    TestTrue(TEXT("the corner says HOLDING OFF while it does"), bSaidHoldingOff);
    TestTrue(TEXT("the ship never passes the floor"), bNeverUnder);
    TestTrue(FString::Printf(TEXT("and comes to rest on it (%.2f m of room)"), Room(*Ship, World) / 100.0),
             Flight.GetSpeed() < 1.0 && Room(*Ship, World) <= FShipFlightState::AtFloorCm);
    TestTrue(FString::Printf(TEXT("the floor is the sky's rendered floor, about 10 km or more (%.1f km)"), World.Floor / 1.0e5),
             World.Floor >= ShipFlight::DefaultFloorCm);
    const FString Corner = UShipHUDWidget::AltitudeLineText(*Ship).ToString();
    TestTrue(FString::Printf(TEXT("and the corner says AT THE FLOOR (\"%s\")"), *Corner), Corner.Contains(TEXT("AT THE FLOOR")));
    return true;
}

// ---------------------------------------------------------------------------

/**
 * A path that misses is untouched (ruling 2: only when the nose's ray meets
 * a floor sphere). Twenty light seconds out, the nose is laid three floor
 * radii off the largest world's centre at the closest approach, and the
 * drive flown at 1 c past
 * it: the cap never binds, the ship holds light exactly all the way past,
 * goes where the nose points, and never dips under the floor. The target
 * line says PASSING, not an ETA, while it closes.
 */
bool FPlaytestCapIgnoresAMissTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTestLocal;

    FSkyWorld Test(TEXT("PlaytestMissWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    Test.Step(0.0f);
    const TOptional<FStarSystem> Home = SystemHere(Test);
    const int32 Largest = Home ? LargestPlanet(*Home) : INDEX_NONE;
    if (!TestTrue(TEXT("home has a world to pass"), Largest != INDEX_NONE))
    {
        return false;
    }
    const FWorldFloor World = WorldFloor(*Home, Largest);
    const FShipFlightState& Flight = Ship->GetFlightState();
    BackOff(Ship, World, 20.0);

    // Three floor radii off the centre at closest approach: turned off the
    // line to the centre by the angle that leaves that miss distance.
    const FVector ToCentre = World.Centre - Flight.GetUniversePosition();
    const double Miss = 3.0 * (World.Radius + World.Floor);
    const double Off = FMath::Asin(Miss / ToCentre.Size());
    const FVector Side = FVector::CrossProduct(ToCentre, FVector::UpVector).GetSafeNormal();
    const FVector Aim = ToCentre.GetSafeNormal().RotateAngleAxisRad(Off, FVector::CrossProduct(Side, ToCentre).GetSafeNormal());
    const double Expected = ToCentre.Size() * FMath::Sin(FMath::Acos(FVector::DotProduct(Aim, ToCentre.GetSafeNormal())));
    Ship->PlaceShip(Flight.GetUniversePosition(), Facing(Aim));
    Test.Step(0.0f);
    if (!TestTrue(TEXT("the aimed path meets no body's floor"), MissesEveryBody(Flight, Aim)))
    {
        return false;
    }
    Ship->SetTarget(WorldId(*Home, Largest));

    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetDriveEngaged(Pilot, true);
    const int32 Top = Flight.GetDriveNotchCount() - 1;
    Ship->SetDriveLever(Pilot, Top);

    double Seconds = 0.0;
    double Closest = TNumericLimits<double>::Max();
    double PastFor = 0.0;
    bool bFree = true;
    bool bAtLight = true;
    bool bNeverUnder = true;
    bool bSaidPassing = false;
    bool bSaidEta = false;
    bool bLightBeforeClosest = false;
    while (Seconds < 400.0 && PastFor < 5.0)
    {
        Ship->Tick(Dt);
        Seconds += Dt;
        bFree &= Flight.GetHold() == EFlightHold::Free;
        bNeverUnder &= Room(*Ship, World) >= 0.0;
        if (Flight.GetDrivePosition() >= Top - ShipDriveLever::SettleNotches)
        {
            bAtLight &= FMath::IsNearlyEqual(Flight.GetSpeed(), ShipDriveLever::LightCmPerSecond, ShipDriveLever::LightCmPerSecond * 1e-9);
        }
        const double Now = Flight.GetUniversePosition().DistanceTo(World.Centre);
        if (Now < Closest)
        {
            Closest = Now;
            bLightBeforeClosest = Flight.GetDrivePosition() >= Top - ShipDriveLever::SettleNotches;
            const FString Line = UShipHUDWidget::TargetLineText(*Ship, Home).ToString();
            bSaidPassing |= Line.Contains(TEXT("PASSING"));
            bSaidEta |= Line.Contains(TEXT("ETA"));
        }
        else
        {
            PastFor += Dt;
        }
    }
    AddInfo(FString::Printf(TEXT("passed %.0f km from the centre after %.1f s, aimed at %.0f km"),
                            Closest / 1.0e5, Seconds, Expected / 1.0e5));
    TestTrue(TEXT("the ship flies past"), PastFor >= 5.0);
    TestTrue(TEXT("the cap never binds on a path that misses"), bFree);
    TestTrue(TEXT("at light before it comes abreast of the world"), bLightBeforeClosest);
    TestTrue(TEXT("and the ship holds light, exactly, all the way past"), bAtLight);
    TestTrue(FString::Printf(TEXT("it goes where the nose points: past at %.0f km against %.0f aimed"), Closest / 1.0e5, Expected / 1.0e5),
             FMath::IsNearlyEqual(Closest, Expected, Expected * 0.01));
    TestTrue(TEXT("and never under the floor"), bNeverUnder);
    TestTrue(TEXT("the target line says PASSING while it closes"), bSaidPassing);
    TestFalse(TEXT("and never an ETA, since it will not arrive"), bSaidEta);
    return true;
}

// ---------------------------------------------------------------------------

/**
 * The in-system jump to the target (ruling 1, map decision 12), flown as the
 * playtest will: a far world picked on the map, the drive under way, "Jump
 * here", the charge, the pilot's hands turning the nose onto it, and the
 * fold opening by itself. The fold is an all stop; the arrival is at rest,
 * both levers at STOP, at the standoff that shows the world two degrees
 * across, above its floor and still the target -- and it stays at rest
 * until the pilot asks for a speed.
 */
bool FPlaytestInSystemJumpToTargetTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTestLocal;

    FSkyWorld Test(TEXT("PlaytestInSystemJumpWorld"));
    AShipMapScreen* Screen = Test.World->SpawnActor<AShipMapScreen>(MapGlass, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the map spawns before play"), Screen))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    Screen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);
    USystemMapWidget* Map = Cast<USystemMapWidget>(Screen->GetScreen()->GetUserWidgetObject());
    const TOptional<FStarSystem> Home = SystemHere(Test);
    if (!TestNotNull(TEXT("the pilot spawns"), Player) || !TestNotNull(TEXT("the map's widget"), Map) ||
        !TestTrue(TEXT("a system with worlds"), Home.IsSet() && Home->Planets.Num() > 0))
    {
        return false;
    }
    Ship->SetPilot(Player);
    Frame(Player, Ship, 0.0f);
    const int32 Orbit = FarWorld(*Ship, *Home);
    if (!TestTrue(TEXT("a world the opening shot is not near"), Orbit != INDEX_NONE))
    {
        return false;
    }
    const FWorldFloor World = WorldFloor(*Home, Orbit);
    const FShipFlightState& Flight = Ship->GetFlightState();
    const auto Levers = [&]() { return Flight.GetCommand().DriveNotch == 0 && Flight.GetCommand().Throttle == 0.0; };

    // Under way: the drive four notches up, 10 km/s.
    Player->PressDrive();
    for (int32 Tap = 0; Tap < 4; ++Tap)
    {
        PlaytestTestLocal::Tap(Player, Ship, 1);
    }
    for (int32 Tick = 0; Tick < 60; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestTrue(FString::Printf(TEXT("under way under the drive (%.1f km/s)"), Flight.GetSpeed() / 1.0e5), Flight.GetSpeed() > 5.0e5);

    Map->RefreshFromShip();
    Map->SelectWorld(Orbit);
    TestTrue(TEXT("the map marks the far world"), Ship->GetTarget() == TOptional<FBodyId>(WorldId(*Home, Orbit)));
    Map->RefreshFromShip();
    TestEqual(TEXT("and offers to jump there"), Map->GetJumpButtonText().ToString(), FString(TEXT("Jump here")));
    Map->PressJumpButton();
    TestTrue(TEXT("Jump here: plotted and engaged"), Ship->GetPlottedWorld().IsSet() && Ship->IsJumpEngaged());
    Console(Test.World, TEXT("ds.Nav.Charge"));
    Frame(Player, Ship);

    // The pilot's hands turn the nose onto the world; the fold opens by itself.
    double Steering = 0.0;
    while (Steering < 90.0 && !Ship->IsInTransit())
    {
        const TOptional<FVector> Course = Ship->GetCourseDirectionShipLocal();
        Player->SetFlightInput(Course ? SteerToward(*Course) : FVector::ZeroVector);
        Frame(Player, Ship);
        Steering += Dt;
    }
    Player->SetFlightInput(FVector::ZeroVector);
    if (!TestTrue(FString::Printf(TEXT("aimed by hand, the fold opens by itself (%.1f s)"), Steering), Ship->IsInTransit()))
    {
        return false;
    }
    TestTrue(TEXT("and the fold is an all stop: both levers at STOP"), Levers());

    double InFold = 0.0;
    while (InFold < 60.0 && Ship->IsInTransit())
    {
        Frame(Player, Ship, 0.1f);
        InFold += 0.1;
    }
    if (!TestFalse(TEXT("out of the fold"), Ship->IsInTransit()))
    {
        return false;
    }
    TestTrue(TEXT("it arrives at rest, exactly"), Flight.GetVelocity().IsZero());
    TestTrue(TEXT("with both levers at STOP"), Levers());
    TestTrue(TEXT("in the same system"), SystemHere(Test).IsSet() && SystemHere(Test)->Stub.Id == Home->Stub.Id);
    const double Distance = Flight.GetUniversePosition().DistanceTo(World.Centre);
    const double Across = FMath::RadiansToDegrees(2.0 * FMath::Asin(World.Radius / Distance));
    TestTrue(FString::Printf(TEXT("at a standoff above the world, not at a star: %.3f degrees across, %.0f km off"),
                             Across, Distance / 1.0e5),
             FMath::IsNearlyEqual(Across, UShipSubsystem::GetWorldStandoffDeg(), 1e-6));
    TestTrue(TEXT("above its floor"), Room(*Ship, World) > 0.0);
    TestTrue(TEXT("still the target"), Ship->GetTarget() == TOptional<FBodyId>(WorldId(*Home, Orbit)));
    const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
    TestTrue(TEXT("and still ahead of the nose"), View.IsSet() && View->ShipLocalDir.X > FMath::Cos(FMath::DegreesToRadians(10.0)));

    for (int32 Tick = 0; Tick < 90; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestTrue(TEXT("and it stays at rest until the pilot asks for a speed"), Flight.GetSpeed() == 0.0 && Levers());
    Map->RefreshFromShip();
    TestEqual(TEXT("the map says the world is near enough to fly"), Map->GetJumpButtonText().ToString(), FString(TEXT("Near enough to fly")));
    TestFalse(TEXT("and will not jump again"), Map->IsJumpButtonEnabled());
    const FString Line = UShipHUDWidget::TargetLineText(*Ship, Home).ToString();
    TestTrue(FString::Printf(TEXT("the HUD names it, with no ETA at rest (\"%s\")"), *Line),
             Line.Contains(NavText::WorldName(Home->Planets[Orbit])) && !Line.Contains(TEXT("ETA")));

    // The fold let go within the jump's cone, which at this standoff can
    // miss the world. The pilot turns onto it at rest, then asks for speed.
    double Turning = 0.0;
    for (TOptional<FTargetView> On = Ship->GetTargetView(*Home);
         Turning < 30.0 && On && On->ShipLocalDir.X < FMath::Cos(FMath::DegreesToRadians(0.2));
         On = Ship->GetTargetView(*Home))
    {
        Player->SetFlightInput(SteerToward(On->ShipLocalDir));
        Frame(Player, Ship);
        Turning += Dt;
    }
    Player->SetFlightInput(FVector::ZeroVector);
    TestTrue(FString::Printf(TEXT("turned onto the world at rest (%.1f s)"), Turning), Turning < 30.0 && Flight.GetSpeed() == 0.0);
    PlaytestTestLocal::Tap(Player, Ship, 1);
    for (int32 Tick = 0; Tick < 60; ++Tick)
    {
        Frame(Player, Ship);
    }
    const FString Moving = UShipHUDWidget::TargetLineText(*Ship, Home).ToString();
    TestTrue(FString::Printf(TEXT("one Shift, and the ship closes with an ETA (\"%s\")"), *Moving), Moving.Contains(TEXT("ETA")));
    return true;
}

// ---------------------------------------------------------------------------

/**
 * The interstellar jump still works, and arrives at rest (flight-feel
 * decision 4). Both levers set -- the drive under way, cruise at full --
 * a star plotted and engaged from the chart, the charge, the nose turned by
 * hand, and the fold opens by itself: an all stop, which lets go of the
 * world targeted at home. The arrival is in the plotted system, at rest,
 * both levers at STOP, and F there is still rest either way.
 */
bool FPlaytestInterstellarJumpAtRestTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTestLocal;

    FSkyWorld Test(TEXT("PlaytestInterstellarWorld"));
    AShipNavScreen* Screen = Test.World->SpawnActor<AShipNavScreen>(ChartGlass, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the chart spawns before play"), Screen))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    Screen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);
    UNavigationWidget* Chart = Cast<UNavigationWidget>(Screen->GetScreen()->GetUserWidgetObject());
    const TOptional<FStarSystem> Home = SystemHere(Test);
    if (!TestNotNull(TEXT("the pilot spawns"), Player) || !TestNotNull(TEXT("the chart's widget"), Chart) ||
        !TestTrue(TEXT("a home with worlds"), Home.IsSet() && Home->Planets.Num() > 0))
    {
        return false;
    }
    Ship->SetPilot(Player);
    Frame(Player, Ship, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();

    TestTrue(TEXT("a world at home is the target"), Ship->SetTarget(WorldId(*Home, 0)));

    // Both levers set: the drive three notches up, cruise at full ahead.
    Player->PressDrive();
    for (int32 Tap = 0; Tap < 3; ++Tap)
    {
        PlaytestTestLocal::Tap(Player, Ship, 1);
    }
    Player->PressDrive();
    Player->TapLever(1);
    Player->HoldLever(1);
    for (int32 Tick = 0; Tick < 90; ++Tick)
    {
        Frame(Player, Ship);
    }
    Player->HoldLever(0);
    Player->PressDrive();
    Frame(Player, Ship);
    TestTrue(FString::Printf(TEXT("both levers set: drive notch %d, cruise %.2f"), Flight.GetCommand().DriveNotch, Flight.GetCommand().Throttle),
             Flight.GetCommand().DriveNotch == 3 && Flight.GetCommand().Throttle == 1.0 && Ship->IsDriveEngaged());

    Chart->RefreshFromShip();
    Chart->SelectRow(0);
    const TOptional<FSystemId> Destination = Ship->GetPlottedSystem();
    if (!TestTrue(TEXT("the chart plots the nearest star"), Destination.IsSet()))
    {
        return false;
    }
    Chart->RefreshFromShip();
    Chart->PressEngage();
    TestTrue(TEXT("and engages"), Ship->IsJumpEngaged());
    Console(Test.World, TEXT("ds.Nav.Charge"));
    Frame(Player, Ship);

    double Steering = 0.0;
    while (Steering < 90.0 && !Ship->IsInTransit())
    {
        const TOptional<FVector> Course = Ship->GetCourseDirectionShipLocal();
        Player->SetFlightInput(Course ? SteerToward(*Course) : FVector::ZeroVector);
        Frame(Player, Ship);
        Steering += Dt;
    }
    Player->SetFlightInput(FVector::ZeroVector);
    if (!TestTrue(FString::Printf(TEXT("aimed by hand, the fold opens by itself (%.1f s)"), Steering), Ship->IsInTransit()))
    {
        return false;
    }
    TestTrue(TEXT("the fold is an all stop: both levers at STOP"),
             Flight.GetCommand().DriveNotch == 0 && Flight.GetCommand().Throttle == 0.0);
    TestFalse(TEXT("and lets go of the world at home"), Ship->GetTarget().IsSet());

    double InFold = 0.0;
    while (InFold < 60.0 && Ship->IsInTransit())
    {
        Frame(Player, Ship, 0.1f);
        InFold += 0.1;
    }
    if (!TestFalse(TEXT("out of the fold"), Ship->IsInTransit()))
    {
        return false;
    }
    const TOptional<FStarSystem> There = SystemHere(Test);
    TestTrue(TEXT("in the plotted system"), There.IsSet() && There->Stub.Id == *Destination);
    TestTrue(TEXT("at rest, exactly"), Flight.GetVelocity().IsZero());
    TestTrue(TEXT("with both levers at STOP"), Flight.GetCommand().DriveNotch == 0 && Flight.GetCommand().Throttle == 0.0);
    for (int32 Tick = 0; Tick < 60; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestTrue(TEXT("and stays at rest under the drive"), Flight.GetSpeed() == 0.0);
    Player->PressDrive();
    for (int32 Tick = 0; Tick < 90; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestTrue(FString::Printf(TEXT("and in cruise after F (%.3f cm/s)"), Flight.GetSpeed()), !Ship->IsDriveEngaged() && Flight.GetSpeed() < 1.0);
    return true;
}

// ---------------------------------------------------------------------------

/**
 * The live ETA counts down as the ship closes (ruling 3). Thirty light
 * seconds out, the largest world targeted and the nose on it, the drive at
 * 1 c:
 * once the ship is at its lever's speed, every second's reading is a second
 * less, give or take a tenth, and the time it names is the time the ship
 * actually reaches the floor -- the ETA and the flight are the same law.
 * The HUD prints it; at rest on the floor it goes, since there is nothing
 * left to arrive at.
 */
bool FPlaytestEtaCountsDownTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTestLocal;

    FSkyWorld Test(TEXT("PlaytestEtaWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    Test.Step(0.0f);
    const TOptional<FStarSystem> Home = SystemHere(Test);
    const int32 Largest = Home ? LargestPlanet(*Home) : INDEX_NONE;
    if (!TestTrue(TEXT("home has a world to close on"), Largest != INDEX_NONE))
    {
        return false;
    }
    const FWorldFloor World = WorldFloor(*Home, Largest);
    const FShipFlightState& Flight = Ship->GetFlightState();
    BackOff(Ship, World, 30.0);
    Test.Step(0.0f);
    TestTrue(TEXT("the world is targeted"), Ship->SetTarget(WorldId(*Home, Largest)));
    TestFalse(TEXT("at rest there is no ETA"), UShipHUDWidget::TargetLineText(*Ship, Home).ToString().Contains(TEXT("ETA")));

    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetDriveEngaged(Pilot, true);
    const int32 Top = Flight.GetDriveNotchCount() - 1;
    Ship->SetDriveLever(Pilot, Top);

    struct FReading
    {
        double At = 0.0;
        double Eta = 0.0;
        FString Line;
        bool bCapped = false;
    };
    TArray<FReading> Readings;
    double Seconds = 0.0;
    double Arrived = -1.0;
    int32 Tick = 0;
    while (Seconds < 400.0 && Arrived < 0.0)
    {
        Ship->Tick(Dt);
        Seconds += Dt;
        ++Tick;
        const bool bSettled = Flight.GetHold() != EFlightHold::Free || Flight.GetDrivePosition() >= Top - ShipDriveLever::SettleNotches;
        if (Tick % 30 == 0 && bSettled)
        {
            const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
            if (View && View->EtaSeconds)
            {
                Readings.Add({ Seconds, *View->EtaSeconds, UShipHUDWidget::TargetLineText(*Ship, Home).ToString(),
                               Flight.GetHold() != EFlightHold::Free });
            }
        }
        if (Room(*Ship, World) <= FShipFlightState::AtFloorCm && Flight.GetSpeed() < TargetMarker::MinSpeed)
        {
            Arrived = Seconds;
        }
    }
    if (!TestTrue(FString::Printf(TEXT("the ship reaches the floor (%.1f s) with readings on the way (%d)"), Arrived, Readings.Num()),
                  Arrived > 0.0 && Readings.Num() >= 10))
    {
        return false;
    }
    int32 Free = 0;
    for (const FReading& Reading : Readings)
    {
        Free += Reading.bCapped ? 0 : 1;
    }
    TestTrue(FString::Printf(TEXT("read at light before the cap binds (%d) and under it (%d)"), Free, Readings.Num() - Free),
             Free >= 5 && Readings.Num() - Free >= 5);
    bool bCountsDown = true;
    double WorstStep = 0.0;
    double WorstArrival = 0.0;
    for (int32 Index = 0; Index < Readings.Num(); ++Index)
    {
        const double Predicted = Readings[Index].At + Readings[Index].Eta;
        WorstArrival = FMath::Max(WorstArrival, FMath::Abs(Predicted - Arrived));
        if (Index > 0)
        {
            const double Fell = Readings[Index - 1].Eta - Readings[Index].Eta;
            const double Elapsed = Readings[Index].At - Readings[Index - 1].At;
            bCountsDown &= Fell > 0.0;
            WorstStep = FMath::Max(WorstStep, FMath::Abs(Fell - Elapsed));
        }
    }
    AddInfo(FString::Printf(TEXT("first reading \"%s\" at %.0f s; arrived at %.1f s"), *Readings[0].Line, Readings[0].At, Arrived));
    TestTrue(TEXT("every reading is less than the one before"), bCountsDown);
    TestTrue(FString::Printf(TEXT("a second less every second, to a tenth (worst %.3f s)"), WorstStep), WorstStep <= 0.1);
    TestTrue(FString::Printf(TEXT("and the time it names is when the ship arrives, to a second (worst %.2f s)"), WorstArrival),
             WorstArrival <= 1.0);
    bool bPrinted = true;
    for (const FReading& Reading : Readings)
    {
        bPrinted &= Reading.Line.Contains(TEXT("ETA ")) && Reading.Line.Contains(NavText::WorldName(Home->Planets[Largest]));
    }
    TestTrue(TEXT("the HUD prints it on the target's line every time"), bPrinted);
    TestNotEqual(TEXT("and the words change as it counts"), Readings[0].Line, Readings.Last().Line);
    for (int32 Rest = 0; Rest < 30; ++Rest)
    {
        Ship->Tick(Dt);
    }
    TestFalse(TEXT("at rest on the floor the ETA is gone"), UShipHUDWidget::TargetLineText(*Ship, Home).ToString().Contains(TEXT("ETA")));
    return true;
}

// ---------------------------------------------------------------------------

/**
 * The keys turn the ship the way CLAUDE.md's *Flying* says: W puts the nose
 * down, S up, D swings it to starboard, A to port, Z rolls right and Q left.
 * Read from IMC_Default as setup_flight_input.py leaves it -- each key's
 * modifiers applied to a press -- handed to the pawn as the attitude action
 * would, and flown for half a second in cruise at rest. Measured on the
 * ship's own axes as it started, so nothing about the view is assumed.
 */
bool FPlaytestKeysTurnTheShipTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestTestLocal;

    UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
    const UInputAction* Attitude = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Attitude.IA_Attitude"));
    if (!TestNotNull(TEXT("IMC_Default loads"), Context) || !TestNotNull(TEXT("IA_Attitude loads"), Attitude))
    {
        return false;
    }
    /** What a press of Key sends the attitude action: the key's own value
     *  through its mapping's modifiers, then the action's. */
    const auto Sent = [&](const FKey& Key) -> TOptional<FVector>
    {
        for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
        {
            if (Mapping.Action != Attitude || Mapping.Key != Key)
            {
                continue;
            }
            FInputActionValue Value(EInputActionValueType::Axis3D, FVector(1.0, 0.0, 0.0));
            for (const UInputModifier* Modifier : Mapping.Modifiers)
            {
                Value = Modifier ? Modifier->ModifyRaw(nullptr, Value, 0.0f) : Value;
            }
            for (const UInputModifier* Modifier : Attitude->Modifiers)
            {
                Value = Modifier ? Modifier->ModifyRaw(nullptr, Value, 0.0f) : Value;
            }
            return Value.Get<FVector>();
        }
        return {};
    };

    FSkyWorld Test(TEXT("PlaytestKeysWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    if (!TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    Ship->SetPilot(Player);
    Frame(Player, Ship, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();

    struct FExpect
    {
        FKey Key;
        const TCHAR* Does;
        // Which of the starting axes the nose (or, for a roll, the up axis)
        // should lean toward: +1 or -1 on (forward, right, up) components.
        bool bRoll;
        FVector Toward;
    };
    const FExpect Keys[] = {
        { EKeys::W, TEXT("W puts the nose down"), false, FVector(0.0, 0.0, -1.0) },
        { EKeys::S, TEXT("S puts the nose up"), false, FVector(0.0, 0.0, 1.0) },
        { EKeys::D, TEXT("D swings the nose to starboard"), false, FVector(0.0, 1.0, 0.0) },
        { EKeys::A, TEXT("A swings the nose to port"), false, FVector(0.0, -1.0, 0.0) },
        { EKeys::Z, TEXT("Z rolls right: the top leans to starboard"), true, FVector(0.0, 1.0, 0.0) },
        { EKeys::Q, TEXT("Q rolls left: the top leans to port"), true, FVector(0.0, -1.0, 0.0) },
    };
    for (const FExpect& Expect : Keys)
    {
        const TOptional<FVector> Value = Sent(Expect.Key);
        if (!TestTrue(FString::Printf(TEXT("%s is mapped to the attitude"), *Expect.Key.ToString()), Value.IsSet()))
        {
            continue;
        }
        Ship->PlaceShip(Flight.GetUniversePosition(), FQuat::Identity);
        Frame(Player, Ship, 0.0f);
        const FQuat Before = Flight.GetUniverseOrientation();
        Player->SetFlightInput(*Value);
        for (int32 Tick = 0; Tick < 30; ++Tick)
        {
            Frame(Player, Ship);
        }
        Player->SetFlightInput(FVector::ZeroVector);
        const FQuat After = Flight.GetUniverseOrientation();
        const FVector Nose = Before.UnrotateVector(After.GetForwardVector());
        const FVector Top = Before.UnrotateVector(After.GetUpVector());
        AddInfo(FString::Printf(TEXT("%s: nose up %.3f, nose starboard %.3f, top to starboard %.3f"), *Expect.Key.ToString(), Nose.Z, Nose.Y, Top.Y));
        const FVector Moved = Expect.bRoll ? Top : Nose;
        const double Lean = FVector::DotProduct(Moved, Expect.Toward);
        const double Leak = Expect.bRoll ? FMath::Abs(Moved.X) : FMath::Abs(Moved.Y * (1.0 - FMath::Abs(Expect.Toward.Y)) + Moved.Z * (1.0 - FMath::Abs(Expect.Toward.Z)));
        TestTrue(FString::Printf(TEXT("%s (sent (%.0f, %.0f, %.0f); leaned %.3f, off-axis %.3f)"), Expect.Does,
                                 Value->X, Value->Y, Value->Z, Lean, Leak),
                 Lean > 0.05 && Leak < 0.01);
        // Let the turn die away before the next key.
        for (int32 Tick = 0; Tick < 60; ++Tick)
        {
            Frame(Player, Ship);
        }
    }
    return true;
}

#endif
