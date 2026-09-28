#include "Core/DeepSpaceGameMode.h"
#include "Tests/StockShip.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Materials/MaterialInterface.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipHumComponent.h"
#include "Ship/ShipHumSource.h"
#include "Ship/ShipLaptop.h"
#include "Ship/ShipLightingSubsystem.h"
#include "Ship/ShipNavScreen.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipPowerState.h"
#include "Sky/SkyProjection.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "UI/PowerAllocationWidget.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Slice 2 with every half in the world at once: the chart, the hum, the
 * laptop, the lighting, the sky. Each half has its own tests; these are the
 * ones that could not exist until the halves met, and they check what the
 * slice promises the player -- choose at the chart and hear it wind; park in
 * a shadow and feel the deck go dark; starve the lights and see every lamp
 * go with them -- rather than what any one class does.
 */

namespace SliceChooseTestLocal
{
    using namespace SkyTestWorld;

    /**
     * The button in Screen whose click runs Handler: found by what its
     * OnClicked is bound to, not by where it sits in the tree, so pressing it
     * is exactly what a pointer's click on it does -- the button broadcasts,
     * and whatever the screen wired to it runs.
     */
    UButton* ButtonFor(UUserWidget* Screen, FName Handler)
    {
        UButton* Found = nullptr;
        if (Screen && Screen->WidgetTree)
        {
            Screen->WidgetTree->ForEachWidget([&](UWidget* Widget)
            {
                UButton* Button = Cast<UButton>(Widget);
                if (Button && Button->OnClicked.Contains(Screen, Handler))
                {
                    Found = Button;
                }
            });
        }
        return Found;
    }

    /** The reactor's voice, spawned before play begins as the level build
     *  places it: at the reactor, its Kind set before its component starts.
     *  The world has no audio device, so nothing is heard; what is checked is
     *  what the hum posts to the audio thread, which is all the voice hears. */
    AShipHumSource* SpawnReactor(UWorld* World)
    {
        const FTransform AtTheReactor(FVector(600.0, 280.0, 120.0));
        AShipHumSource* Source = World->SpawnActorDeferred<AShipHumSource>(AShipHumSource::StaticClass(), AtTheReactor);
        if (Source)
        {
            Source->Kind = EShipHumKind::Reactor;
            Source->FinishSpawning(AtTheReactor);
        }
        return Source;
    }

    /** What the drone should be fed: the watts reaching the jump drive over
     *  what winding asks for (plan conflict 8). */
    float ExpectedFeed(const UShipSubsystem& Ship)
    {
        const float Want = Ship.GetWindingWant();
        return Want > 0.0f ? FMath::Clamp(Ship.GetConsumerShare(ShipPower::Engine) / Want, 0.0f, 1.0f) : 0.0f;
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

    /** What a lamp box glows with right now: its material's Colour. */
    FLinearColor Glow(const AStaticMeshActor* Lamp)
    {
        FLinearColor Colour = FLinearColor::Black;
        if (const UMaterialInterface* Material = Lamp->GetStaticMeshComponent()->GetMaterial(0))
        {
            Material->GetVectorParameterValue(FHashedMaterialParameterInfo(ShipLighting::LampColourParameter), Colour);
        }
        return Colour;
    }
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSliceChooseChartTest,
    "DeepSpace.Loop.Chart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Choose, hear it wind, arrive: the loop with the console taken out of it.
 * A row on the chart is pressed -- its button, broadcasting to whatever the
 * screen wired it to -- and the ship has that course; Engage is pressed and
 * the jump winds; the reactor's drone is fed, tick by tick, the watts
 * reaching the jump drive, and rises when the laptop leans the split on the
 * engine; charged, it settles back; pointed down the course the jump fires
 * with no confirm, and the ship arrives where the chart said.
 *
 * The chart and the laptop are the screen actors the level places, spawned
 * before play, and their widgets are the ones their widget components made.
 * Every frame runs in the game's order: the hum and the panels in
 * TG_DuringPhysics, then the ship's subsystem, then the sky -- so the drone
 * hears the watts the ship answered as the frame began, a frame behind, as
 * it does in play.
 */
bool FSliceChooseChartTest::RunTest(const FString& Parameters)
{
    using namespace SliceChooseTestLocal;

    FSkyWorld Test(TEXT("SliceChooseChartWorld"));
    AShipHumSource* Reactor = Test.World ? SpawnReactor(Test.World) : nullptr;
    // Before play begins, as the level places them: a widget component makes
    // its widget in BeginPlay, and one spawned into a running world never does.
    AShipNavScreen* ChartScreen = Test.World
        ? Test.World->SpawnActor<AShipNavScreen>(FVector(300.0, 0.0, 105.0), FRotator::ZeroRotator) : nullptr;
    AShipLaptop* LaptopScreen = Test.World
        ? Test.World->SpawnActor<AShipLaptop>(FVector(0.0, 300.0, 75.0), FRotator::ZeroRotator) : nullptr;
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("a universe"), Test.Universe)
        || !TestNotNull(TEXT("a sky"), Test.Sky) || !TestNotNull(TEXT("a reactor to hear"), Reactor)
        || !TestNotNull(TEXT("the chart"), ChartScreen) || !TestNotNull(TEXT("and the laptop"), LaptopScreen))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    TestTrue(TEXT("the stock loadout installs"), StockShip::Install(Ship) > 0);
    UShipHumComponent* Hum = Reactor->GetHum();
    UWidgetComponent* ChartPanel = ChartScreen->GetScreen();
    UWidgetComponent* LaptopPanel = LaptopScreen->GetScreen();
    UNavigationWidget* Chart = Cast<UNavigationWidget>(ChartPanel->GetUserWidgetObject());
    UPowerAllocationWidget* Laptop = Cast<UPowerAllocationWidget>(LaptopPanel->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the chart's panel made the chart"), Chart)
        || !TestNotNull(TEXT("and the laptop's the power split"), Laptop))
    {
        return false;
    }

    // The screens' part of a frame: each panel's component ticks, which is
    // where it takes its widget and the widget builds its tree, and each
    // widget refreshes as its Slate tick would. Headless nothing paints, so
    // the refresh is called directly.
    const auto Look = [&](float Seconds)
    {
        ChartPanel->TickComponent(Seconds, LEVELTICK_All, nullptr);
        LaptopPanel->TickComponent(Seconds, LEVELTICK_All, nullptr);
        Chart->RefreshFromShip();
        Laptop->RefreshFromShip();
    };

    // One frame in the game's order. The hum and the panels are components
    // in TG_DuringPhysics; the ship's subsystem ticks with the tickable
    // objects after the physics groups; the counter-frame and the sky draw
    // in TG_PostUpdateWork (FSkyWorld::Step).
    const auto Step = [&](float Seconds)
    {
        Hum->TickComponent(Seconds, LEVELTICK_All, nullptr);
        Look(Seconds);
        Test.Step(Seconds);
    };
    Step(0.0f);
    Look(0.0f);

    TestEqual(TEXT("idle, the drone is fed nothing"), Hum->GetPostedInputs().EngineFeed, 0.0f);

    // Row 1, not row 0: ds.Nav.Plot 0's system, and the one a screen that
    // ignored which button was pressed would most likely land on.
    const TArray<FStarSystemStub> Rows = Ship->GetChart();
    if (!TestTrue(TEXT("the chart offers at least two systems"), Rows.Num() >= 2 && Chart->GetShownRowCount() >= 2))
    {
        return false;
    }
    const FStarSystemStub Destination = Rows[1];
    UButton* RowButton = ButtonFor(Chart, TEXT("HandleRow1"));
    UButton* EngageButton = ButtonFor(Chart, TEXT("HandleEngage"));
    if (!TestNotNull(TEXT("row 1 has a button"), RowButton) || !TestNotNull(TEXT("and so does Engage"), EngageButton))
    {
        return false;
    }
    TestTrue(TEXT("row 1 reads as the chart's second system"), Chart->GetRowText(1).ToString().Contains(Destination.Name));

    RowButton->OnClicked.Broadcast();
    TestTrue(FString::Printf(TEXT("pressing row 1 plots %s in the ship"), *Destination.Name),
             Ship->GetPlottedSystem().IsSet() && *Ship->GetPlottedSystem() == Destination.Id);
    Look(0.0f);
    TestTrue(TEXT("and the chart marks that row, asked of the ship"),
             Chart->GetRowText(1).ToString().StartsWith(UNavigationWidget::PlottedMark));
    const TOptional<FVector> Course = Ship->GetCourseDirection();
    if (!TestTrue(TEXT("the course has a direction"), Course.IsSet()))
    {
        return false;
    }

    // Pointed away, so winding and aiming are separate acts, and the drone
    // can be heard through the whole wind before anything fires.
    Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(-*Course).ToQuat());
    Step(0.0f);
    EngageButton->OnClicked.Broadcast();
    Step(0.1f);
    Look(0.0f);
    TestTrue(TEXT("pressing Engage engages the jump"), Ship->IsJumpEngaged());
    TestEqual(TEXT("and it winds"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Winding));
    TestEqual(TEXT("which the chart says in words"), Chart->GetJumpText().ToString(), NavText::JumpWord(EJumpState::Winding) + TEXT("."));

    // Winding: every tick, the drone is fed exactly the watts reaching the
    // jump drive. Part way, the laptop leans the split on the engine, and the
    // watts and the drone rise together.
    float Worst = 0.0f;
    float Lowest = 1.0f;
    float BeforeLean = -1.0f;
    float AfterLean = -1.0f;
    float WattsBefore = -1.0f;
    float WattsAfter = -1.0f;
    int32 Ticks = 0;
    while (Ticks < 2000 && Ship->GetJumpState() == EJumpState::Winding)
    {
        if (Ticks == 10)
        {
            BeforeLean = Hum->GetPostedInputs().EngineFeed;
            WattsBefore = Ship->GetConsumerShare(ShipPower::Engine);
            Laptop->SetRowWeight(ShipPower::Engine, UPowerAllocationWidget::MaxWeight);
        }
        // What the ship answers as the frame begins, which is what the hum
        // is ticked with before the ship steps.
        const float Heard = ExpectedFeed(*Ship);
        Step(0.5f);
        if (Ship->GetJumpState() != EJumpState::Winding)
        {
            break;
        }
        const float Posted = Hum->GetPostedInputs().EngineFeed;
        Worst = FMath::Max(Worst, FMath::Abs(Posted - Heard));
        Lowest = FMath::Min(Lowest, Posted);
        if (Ticks == 10)
        {
            AfterLean = Posted;
            WattsAfter = Ship->GetConsumerShare(ShipPower::Engine);
        }
        ++Ticks;
    }
    TestTrue(FString::Printf(TEXT("winding, the drone is fed the watts over ds.Nav.WindingWant, tick by tick (worst %.5f out over %d ticks)"),
                             Worst, Ticks),
             Ticks > 10 && Worst < 1e-5f);
    TestTrue(FString::Printf(TEXT("and it is fed something throughout (lowest %.3f)"), Lowest), Lowest > 0.0f);
    TestTrue(FString::Printf(TEXT("the laptop leaning on the engine sends it more watts (%.0f W to %.0f W)"), WattsBefore, WattsAfter),
             WattsAfter > WattsBefore && WattsBefore > 0.0f);
    TestTrue(FString::Printf(TEXT("and the drone rises with them (feed %.3f to %.3f)"), BeforeLean, AfterLean),
             AfterLean > BeforeLean && BeforeLean > 0.0f);
    Laptop->SetRowWeight(ShipPower::Engine, 1.0f);

    TestEqual(TEXT("it charges to ready"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Ready));
    // The engine's want follows the jump state at the start of the ship's
    // next tick, and the hum hears the ship as each frame begins, before
    // the ship steps: so the drone settles two frames after Ready, the
    // first frame it could.
    Step(0.1f);
    TestTrue(TEXT("a frame after Ready the drone still hears the last winding watts"),
             Hum->GetPostedInputs().EngineFeed > 0.0f);
    Step(0.1f);
    TestFalse(TEXT("pointed away, it holds"), Ship->IsInTransit());
    TestEqual(TEXT("charged, the drone settles back: a charged jump draws nothing"), Hum->GetPostedInputs().EngineFeed, 0.0f);

    // Turned down the course, it fires by itself: nothing is pressed.
    Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(),
                    FRotationMatrix::MakeFromX(Ship->GetCourseDirection().Get(*Course)).ToQuat());
    Step(0.1f);
    TestTrue(TEXT("aligned, the jump fires with no confirm"), Ship->IsInTransit());

    double Transit = 0.0;
    while (Transit < 120.0 && Ship->GetJumpSerial() == 0)
    {
        Step(0.25f);
        Transit += 0.25;
    }
    TestEqual(TEXT("it arrives"), Ship->GetJumpSerial(), 1);
    const TOptional<FStarSystem> There = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
    TestTrue(FString::Printf(TEXT("in %s, the system the chart's row named"), *Destination.Name),
             There.IsSet() && There->Stub.Id == Destination.Id);
    Look(0.0f);
    TestTrue(FString::Printf(TEXT("the chart says so (\"%s\")"), *Chart->GetHereText().ToString()),
             Chart->GetHereText().ToString().Contains(Destination.Name));
    TestFalse(TEXT("the course is spent"), Ship->GetPlottedSystem().IsSet());
    TestEqual(TEXT("the sky has redrawn for the arrival"), Test.Sky->GetBuiltForSerial(), 1);
    TestEqual(TEXT("and the drone is idle again"), Hum->GetPostedInputs().EngineFeed, 0.0f);

    Test.World->EndPlay(EEndPlayReason::RemovedFromWorld);
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSliceChooseEclipseTest,
    "DeepSpace.Loop.Eclipse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Parked behind a real planet of the start system, the sun goes and the deck
 * goes dark. Swept sideways out of the shadow, three planet radii behind it,
 * the sun comes back through a penumbra: never snapping, never flickering
 * back and forth, and the deck's sunlight exactly the sun's visible share of
 * what it would be at every step.
 */
bool FSliceChooseEclipseTest::RunTest(const FString& Parameters)
{
    using namespace SliceChooseTestLocal;

    FSkyWorld Test(TEXT("SliceChooseEclipseWorld"));
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("and a sky"), Test.Sky))
    {
        return false;
    }
    FScopedCVar Lux(TEXT("ds.Sky.SunLux"), 50.0f);
    Test.BeginPlay();
    Test.Step(0.0f);

    const TOptional<FStarSystem> Home = Test.Universe->GetSystem(Test.Universe->GetStartSystem());
    const int32 Largest = Home ? LargestPlanet(*Home) : INDEX_NONE;
    if (!TestTrue(TEXT("home has a planet to hide behind"), Largest != INDEX_NONE))
    {
        return false;
    }
    const double Radius = Home->Planets[Largest].RadiusEarth * UniverseUnits::CmPerEarthRadius;
    const FUniversePosition Planet = Home->PlanetPosition(Largest);
    const FVector Axis = (Planet - Home->Stub.Position).GetSafeNormal();
    const FVector Side = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();

    // Three radii behind the planet the shadow's edge, where the limb crosses
    // the star, is about one radius off the axis: swept from 0.8 to 1.2
    // radii, the ship starts in full shadow and ends in full sun.
    const auto ParkAt = [&](double Across)
    {
        Test.Ship->PlaceShip(Planet + Axis * (3.0 * Radius) + Side * (Across * Radius), FQuat::Identity);
        Test.Step(0.0f);
        return Test.Sky->GetLastFrame();
    };

    const FSkyFrame Umbra = ParkAt(0.8);
    TestEqual(TEXT("in the planet's shadow none of the sun shows"), Umbra.SunVisibleFraction, 0.0);
    TestTrue(FString::Printf(TEXT("though the star is as bright as ever (irradiance %.3f)"), Umbra.SunIrradiance),
             Umbra.SunIrradiance > 0.0);
    TestEqual(TEXT("and the deck is dark"), Test.Sky->GetSun()->Intensity, 0.0f);

    int32 Penumbra = 0;
    int32 Backwards = 0;
    double Worst = 0.0;
    double Previous = 0.0;
    FSkyFrame Last;
    for (int32 Index = 0; Index <= 4000; ++Index)
    {
        Last = ParkAt(0.8 + 0.4 * Index / 4000.0);
        const double Fraction = Last.SunVisibleFraction;
        Penumbra += Fraction > 0.01 && Fraction < 0.99 ? 1 : 0;
        Backwards += Fraction < Previous - 1e-9 ? 1 : 0;
        Previous = Fraction;
        const double Want = 50.0 * Last.SunIrradiance * Fraction;
        Worst = FMath::Max(Worst, FMath::Abs(Test.Sky->GetSun()->Intensity - Want) / FMath::Max(Want, 1e-3));
    }
    TestEqual(TEXT("out of the shadow the whole sun shows"), Last.SunVisibleFraction, 1.0);
    TestTrue(TEXT("and lights the deck"), Test.Sky->GetSun()->Intensity > 0.0f);
    TestTrue(FString::Printf(TEXT("the light comes back through a penumbra, not a snap (%d of 4001 steps part-lit)"), Penumbra),
             Penumbra >= 5);
    TestEqual(TEXT("and never goes back the way it came"), Backwards, 0);
    TestTrue(FString::Printf(TEXT("at every step the deck's sunlight is ds.Sky.SunLux x irradiance x the visible share (worst %.2e out)"),
                             Worst),
             Worst < 1e-4);
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSliceChooseLampsTest,
    "DeepSpace.Loop.LampsFollowTheSplit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Everything the player sees of the lights answers the one lever. With a
 * jump winding and the laptop leaning the split away from the lights, every
 * room's lamp -- a ceiling panel and a prop's glowing part, on the materials
 * the level build makes, tagged as it tags them -- dims with its room's
 * light, tick by tick, flicker and all; the glass reflects the room exactly
 * as brightly as the lights are fed; and moved back, all of it is back.
 */
bool FSliceChooseLampsTest::RunTest(const FString& Parameters)
{
    using namespace SliceChooseTestLocal;

    // Every room aboard, as hauler_layout.ROOMS names them. A room whose
    // lamp material the level build did not make, or made without Colour,
    // is a room whose panels can never dim.
    static const TCHAR* const Rooms[] = { TEXT("corridor"), TEXT("cockpit"), TEXT("cargo_bay"), TEXT("engineering"),
                                          TEXT("galley"), TEXT("crawlway"), TEXT("airlock"), TEXT("bunk") };
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("the cube"), Cube))
    {
        return false;
    }

    FSkyWorld Test(TEXT("SliceChooseLampsWorld"));
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("and a sky"), Test.Sky))
    {
        return false;
    }
    // The stock ship's modules, as play installs them. On a bare reactor the
    // engine, capped at its winding want, cannot starve the lights into
    // their brown-out at all -- and a test of how the split looks would be
    // describing a ship nobody flies.
    {
        const UClass* ModeClass = LoadClass<ADeepSpaceGameMode>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceGameMode.BP_DeepSpaceGameMode_C"));
        const ADeepSpaceGameMode* Mode = ModeClass ? ModeClass->GetDefaultObject<ADeepSpaceGameMode>() : GetDefault<ADeepSpaceGameMode>();
        for (const TSoftObjectPtr<UShipModuleDataAsset>& Soft : Mode->GetStartingModules())
        {
            if (UShipModuleDataAsset* Module = Soft.LoadSynchronous())
            {
                Test.Ship->FitPart(Module);
            }
        }
    }

    struct FRoomLamps
    {
        FString Room;
        FLinearColor Rated;
        UPointLightComponent* Light = nullptr;
        float RatedIntensity = 0.0f;
        FLinearColor RatedLight;
        TArray<AStaticMeshActor*> Lamps;
    };
    TArray<FRoomLamps> Built;
    for (const TCHAR* Room : Rooms)
    {
        const FString Path = FString::Printf(TEXT("/Game/Materials/MI_Ship_lamp_%s.MI_Ship_lamp_%s"), Room, Room);
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path);
        FRoomLamps Entry;
        Entry.Room = Room;
        if (!TestNotNull(FString::Printf(TEXT("the %s's lamp material"), Room), Material)
            || !TestTrue(FString::Printf(TEXT("the %s's lamp material glows with a Colour"), Room),
                         Material->GetVectorParameterValue(FHashedMaterialParameterInfo(ShipLighting::LampColourParameter), Entry.Rated)))
        {
            return false;
        }
        // Its ceiling panel and a prop's glowing part, as build_hauler's
        // spawn_box leaves both: Static, the room's lamp material, tagged.
        for (int32 Part = 0; Part < 2; ++Part)
        {
            AStaticMeshActor* Lamp = Test.World->SpawnActor<AStaticMeshActor>();
            Lamp->GetStaticMeshComponent()->SetStaticMesh(Cube);
            Lamp->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
            Lamp->GetStaticMeshComponent()->SetMaterial(0, Material);
            Lamp->Tags.Add(ShipLighting::LampsTag);
            Entry.Lamps.Add(Lamp);
        }
        APointLight* Light = Test.World->SpawnActor<APointLight>();
        Light->Tags.Add(ShipPower::Lights);
        Entry.Light = Light->FindComponentByClass<UPointLightComponent>();
        Entry.Light->SetMobility(EComponentMobility::Movable);
        Entry.RatedIntensity = Entry.Light->Intensity;
        Entry.RatedLight = Entry.Light->GetLightColor();
        Built.Add(Entry);
    }
    // The laptop the split is set at, placed before play as the level places
    // it, so its widget is the one its panel makes.
    AShipLaptop* LaptopScreen = Test.World->SpawnActor<AShipLaptop>(FVector(0.0, 300.0, 75.0), FRotator::ZeroRotator);

    Test.BeginPlay();
    UShipLightingSubsystem* Lighting = Test.World->GetSubsystem<UShipLightingSubsystem>();
    UMaterialParameterCollectionInstance* Glass = Test.Sky->SkyParameters
        ? Test.World->GetParameterCollectionInstance(Test.Sky->SkyParameters) : nullptr;
    if (!TestNotNull(TEXT("the lighting"), Lighting) || !TestNotNull(TEXT("and the glass's collection"), Glass))
    {
        return false;
    }
    TestEqual(TEXT("play finds every lamp by its tag"), Lighting->GetLampCount(), 2 * static_cast<int32>(UE_ARRAY_COUNT(Rooms)));
    const auto Step = [&](float Seconds)
    {
        Test.Step(Seconds);
        Lighting->Tick(Seconds);
    };
    const auto Reflected = [Glass]()
    {
        float Value = -1.0f;
        Glass->GetScalarParameterValue(SkyMaterial::InteriorLight, Value);
        return Value;
    };

    // A jump winding makes the split a real one: the engine wants its
    // winding watts, and the reactor cannot meet every want at once.
    const TArray<FStarSystemStub> Rows = Test.Ship->GetChart();
    if (!TestTrue(TEXT("somewhere to go"), Rows.Num() > 0 && Test.Ship->PlotCourse(Rows[0].Id)))
    {
        return false;
    }
    Test.Ship->PlaceShip(Test.Ship->GetFlightState().GetUniversePosition(),
                         FRotationMatrix::MakeFromX(-Test.Ship->GetCourseDirection().Get(FVector::XAxisVector)).ToQuat());
    Test.Ship->SetJumpEngaged(true);

    UPowerAllocationWidget* Laptop = LaptopScreen
        ? Cast<UPowerAllocationWidget>(LaptopScreen->GetScreen()->GetUserWidgetObject()) : nullptr;
    if (!TestNotNull(TEXT("the laptop's panel made the power split"), Laptop))
    {
        return false;
    }
    // Its component's first tick is where it takes the widget, and the
    // widget builds its rows.
    LaptopScreen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);
    Laptop->SetRowWeight(ShipPower::Engine, UPowerAllocationWidget::MaxWeight);
    Laptop->SetRowWeight(ShipPower::Boosters, UPowerAllocationWidget::MaxWeight);
    Laptop->SetRowWeight(ShipPower::Lights, 0.1f);
    Step(0.016f);
    const float Fed = Test.Ship->GetConsumerSatisfaction(ShipPower::Lights);
    TestEqual(TEXT("the jump is winding"), static_cast<int32>(Test.Ship->GetJumpState()), static_cast<int32>(EJumpState::Winding));
    TestTrue(FString::Printf(TEXT("and the laptop's split starves the lights into their brown-out, not off (%.3f fed)"), Fed),
             Fed > 0.0f && Fed < UShipLightingSubsystem::BrownOutBelow);

    // How far a colour has warmed from its rating in one channel: that
    // channel's share against red, over the same share when rated. Red is
    // the channel a brown-out keeps, so brightness alone never shows here,
    // and a panel lit its room's kelvin compares with a light that is white.
    const auto Warmth = [](const FLinearColor& Now, const FLinearColor& Rated, int32 Channel)
    {
        return (Now.Component(Channel) / Now.R) / (Rated.Component(Channel) / Rated.R);
    };

    float Worst = 0.0f;
    float WorstHue = 0.0f;
    float LightWarmest = 1.0f;
    float WorstGlass = 0.0f;
    float Dimmest = TNumericLimits<float>::Max();
    float Brightest = 0.0f;
    for (int32 Tick = 0; Tick < 120; ++Tick)
    {
        Step(0.016f);
        WorstGlass = FMath::Max(WorstGlass, FMath::Abs(Reflected() - Test.Ship->GetConsumerSatisfaction(ShipPower::Lights)));
        for (const FRoomLamps& Room : Built)
        {
            const float Fraction = Room.Light->Intensity / Room.RatedIntensity;
            Dimmest = FMath::Min(Dimmest, Fraction);
            Brightest = FMath::Max(Brightest, Fraction);
            const FLinearColor LightNow = Room.Light->GetLightColor();
            for (const AStaticMeshActor* Lamp : Room.Lamps)
            {
                const FLinearColor Panel = Glow(Lamp);
                Worst = FMath::Max(Worst, FMath::Abs((Panel.R / Room.Rated.R) / Fraction - 1.0f));
                for (int32 Channel = 1; Channel <= 2; ++Channel)
                {
                    const float LightWarmth = Warmth(LightNow, Room.RatedLight, Channel);
                    LightWarmest = FMath::Min(LightWarmest, LightWarmth);
                    WorstHue = FMath::Max(WorstHue, FMath::Abs(Warmth(Panel, Room.Rated, Channel) - LightWarmth));
                }
            }
        }
    }
    TestTrue(FString::Printf(TEXT("starved, the lights dim and flicker (%.3f to %.3f of their rating)"), Dimmest, Brightest),
             Brightest < 0.5f && Dimmest > 0.0f && Brightest / Dimmest > 1.05f);
    TestTrue(FString::Printf(TEXT("every room's lamps glow at their light's fraction, tick by tick (worst %.3f%% out)"), 100.0f * Worst),
             Worst < 1e-3f);
    // The light's colour is held as 8-bit sRGB, so its warmth is only good
    // to a percent or so; a panel that stopped browning out would be off by
    // the whole brown-out, tens of percent.
    TestTrue(FString::Printf(TEXT("starved, the lights run warm (green and blue down to %.2f of their share)"), LightWarmest),
             LightWarmest < 0.9f);
    TestTrue(FString::Printf(TEXT("and every lamp browns out with its light, on its own rated colour (worst %.3f out)"), WorstHue),
             WorstHue < 0.02f);
    TestTrue(FString::Printf(TEXT("and the glass reflects the room as brightly as the lights are fed (worst %.5f out)"), WorstGlass),
             WorstGlass < 1e-6f);

    // The lever to the lights, and everything with it: a split that feeds
    // them, whatever the reactor's margin at the default happens to be, is
    // the promise the lamps have to keep.
    Laptop->SetRowWeight(ShipPower::Lights, 1.0f);
    Laptop->SetRowWeight(ShipPower::Engine, 0.0f);
    Laptop->SetRowWeight(ShipPower::Boosters, 0.0f);
    Test.Ship->SetJumpEngaged(false);
    Step(0.016f);
    Step(0.016f);
    const float Back = Test.Ship->GetConsumerSatisfaction(ShipPower::Lights);
    TestEqual(TEXT("fed again, the lights are whole"), Back, 1.0f);
    bool bAllBack = true;
    for (const FRoomLamps& Room : Built)
    {
        bAllBack &= FMath::IsNearlyEqual(Room.Light->Intensity, Room.RatedIntensity, 1e-3f * Room.RatedIntensity);
        for (const AStaticMeshActor* Lamp : Room.Lamps)
        {
            const FLinearColor Panel = Glow(Lamp);
            for (int32 Channel = 0; Channel <= 2; ++Channel)
            {
                bAllBack &= FMath::IsNearlyEqual(Panel.Component(Channel), Room.Rated.Component(Channel),
                                                 1e-3f * Room.Rated.Component(Channel));
            }
        }
    }
    TestTrue(TEXT("and every light and every lamp is back at its rating"), bAllBack);
    TestEqual(TEXT("and so is the glass"), Reflected(), 1.0f);

    Test.World->EndPlay(EEndPlayReason::RemovedFromWorld);
    return true;
}

#endif
