#include "Components/BoxComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Player/Posture.h"
#include "Ship/InteractableComponent.h"
#include "Ship/ShipNavScreen.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipSubsystem.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// Two tests and no children under either path: a test path with children
// becomes a group, and a group silently stops running its own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNavigationScreenTest,
    "DeepSpace.UI.NavigationScreen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNavScreenChairTest,
    "DeepSpace.Ship.NavScreen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** A console variable moved for the length of a scope, then put back, so
     *  a test that tunes one cannot leak it into the next. */
    struct FScopedCVar
    {
        IConsoleVariable* Variable = nullptr;
        FString Previous;

        FScopedCVar(const TCHAR* Name, float Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name))
        {
            if (Variable)
            {
                Previous = Variable->GetString();
                Variable->Set(*FString::SanitizeFloat(Value), ECVF_SetByCode);
            }
        }

        ~FScopedCVar()
        {
            if (Variable)
            {
                Variable->Set(*Previous, ECVF_SetByCode);
            }
        }
    };

    UWorld* MakeWorld(const TCHAR* Name)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, Name);
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        return World;
    }

    /** Play ends before the world goes; a world whose actors never began
     *  play ignores it. */
    void DestroyWorld(UWorld* World)
    {
        World->EndPlay(EEndPlayReason::RemovedFromWorld);
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    void BeginPlay(UWorld* World)
    {
        World->InitializeActorsForPlay(FURL());
        World->BeginPlay();
    }

    /** The same, and then every actor's BeginPlay, which a world with no
     *  game mode never dispatches by itself: the call its game state would
     *  make. A chart's widget component makes its widget there. */
    void BeginPlayForActors(UWorld* World)
    {
        BeginPlay(World);
        World->GetWorldSettings()->NotifyBeginPlay();
    }

    bool HasNumber(const FString& Text)
    {
        for (const TCHAR Character : Text)
        {
            if (FChar::IsDigit(Character) || Character == TEXT('%'))
            {
                return true;
            }
        }
        return false;
    }

    double ChartRangeCm()
    {
        const IConsoleVariable* Range = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Nav.RangeLy"));
        return (Range ? Range->GetFloat() : 12.0f) * UniverseUnits::CmPerLightYear;
    }

    /**
     * What the chart should list, asked of the universe directly rather than
     * through UShipSubsystem::GetChart: the systems within ds.Nav.RangeLy,
     * nearest first, without the one the ship is in, six at most.
     */
    TArray<FStarSystemStub> Expected(const UUniverseSubsystem& Universe, const UShipSubsystem& Ship)
    {
        const FUniversePosition Where = Ship.GetFlightState().GetUniversePosition();
        TArray<FStarSystemStub> Near = Universe.GetSystemsNear(Where, ChartRangeCm());
        if (const TOptional<FStarSystem> Here = Universe.GetSystemAt(Where))
        {
            Near.RemoveAll([&Here](const FStarSystemStub& Stub) { return Stub.Id == Here->Stub.Id; });
        }
        if (Near.Num() > UNavigationWidget::RowCount)
        {
            Near.SetNum(UNavigationWidget::RowCount);
        }
        return Near;
    }

    /**
     * Whether a ship arrived at From would find Home among its rows. Asked
     * from the star's own position rather than the arrival point, which is
     * a standoff of a few AU from it: against light-years of spacing that
     * reorders nothing but an exact tie.
     */
    bool ChartFromListsHome(const UUniverseSubsystem& Universe, const FStarSystemStub& From, const FSystemId& Home)
    {
        TArray<FStarSystemStub> Near = Universe.GetSystemsNear(From.Position, ChartRangeCm());
        Near.RemoveAll([&From](const FStarSystemStub& Stub) { return Stub.Id == From.Id; });
        if (Near.Num() > UNavigationWidget::RowCount)
        {
            Near.SetNum(UNavigationWidget::RowCount);
        }
        return Near.ContainsByPredicate([&Home](const FStarSystemStub& Stub) { return Stub.Id == Home; });
    }

    int32 EnabledRows(const UNavigationWidget& Chart)
    {
        int32 Count = 0;
        for (int32 Index = 0; Index < UNavigationWidget::RowCount; ++Index)
        {
            Count += Chart.IsRowEnabled(Index) ? 1 : 0;
        }
        return Count;
    }

    /** Every row the chart shows is the expected system, in order, worded
     *  as ds.Nav.Near words it, marked visited exactly when the ship has
     *  been there, and there to be pressed. */
    void CheckRows(FAutomationTestBase& Test, const UNavigationWidget& Chart, const UUniverseSubsystem& Universe,
                   const UShipSubsystem& Ship, const TCHAR* When)
    {
        const TArray<FStarSystemStub> Want = Expected(Universe, Ship);
        Test.TestEqual(FString::Printf(TEXT("%s: the chart shows the six nearest, or all there are"), When),
                       Chart.GetShownRowCount(), Want.Num());

        Test.TestEqual(FString::Printf(TEXT("%s: every row shown can be pressed"), When),
                       EnabledRows(Chart), Want.Num());

        const FUniversePosition Where = Ship.GetFlightState().GetUniversePosition();
        const FString VisitedColumn = FString(NavText::Separator) + NavText::Visited(true);
        for (int32 Index = 0; Index < Want.Num(); ++Index)
        {
            FString Row = Chart.GetRowText(Index).ToString();
            Row.RemoveFromStart(UNavigationWidget::PlottedMark);
            Row.TrimStartInline();
            const FString Distance = FString::Printf(
                TEXT("%.1f ly"), Where.DistanceTo(Want[Index].Position) / UniverseUnits::CmPerLightYear);
            Test.TestTrue(FString::Printf(TEXT("%s: row %d is %s, %s, a %s (it reads '%s')"), When, Index,
                                          *Want[Index].Name, *Distance, *NavText::StarClass(Want[Index].Class), *Row),
                          Row.StartsWith(Want[Index].Name + NavText::Separator + Distance + NavText::Separator
                                         + NavText::StarClass(Want[Index].Class)));

            // Visited is the ship's record, not the chart's guess: a row
            // that says it of somewhere never reached is as wrong as one
            // that forgets where the ship has been.
            const bool bBeen = Ship.HasVisited(Want[Index].Id);
            Test.TestEqual(FString::Printf(TEXT("%s: row %d (%s) reads visited only if the ship has been there "
                                                "(it reads '%s')"), When, Index, *Want[Index].Name, *Row),
                           Row.EndsWith(VisitedColumn), bBeen);
        }
        Test.TestTrue(FString::Printf(TEXT("%s: nothing past the last row"), When),
                      Chart.GetRowText(Want.Num()).IsEmpty());

        // The system the ship is in is not somewhere to go.
        if (const TOptional<FStarSystem> Here = Universe.GetSystemAt(Where))
        {
            bool bListsHere = false;
            for (int32 Index = 0; Index < UNavigationWidget::RowCount; ++Index)
            {
                FString Row = Chart.GetRowText(Index).ToString();
                Row.RemoveFromStart(UNavigationWidget::PlottedMark);
                bListsHere |= Row.TrimStart().StartsWith(Here->Stub.Name + NavText::Separator);
            }
            Test.TestFalse(FString::Printf(TEXT("%s: the chart leaves out %s, where the ship is"), When,
                                           *Here->Stub.Name), bListsHere);
        }
    }

    bool RowIsPlotted(const UNavigationWidget& Chart, int32 Index)
    {
        return Chart.GetRowText(Index).ToString().StartsWith(UNavigationWidget::PlottedMark);
    }

    int32 PlottedRows(const UNavigationWidget& Chart)
    {
        int32 Count = 0;
        for (int32 Index = 0; Index < UNavigationWidget::RowCount; ++Index)
        {
            Count += RowIsPlotted(Chart, Index) ? 1 : 0;
        }
        return Count;
    }

    template <typename FDone>
    void TickUntil(UShipSubsystem* Ship, double Limit, FDone Done)
    {
        for (double Elapsed = 0.0; Elapsed < Limit && !Done(); Elapsed += 0.1)
        {
            Ship->Tick(0.1f);
        }
    }
}

/**
 * The chart as the player uses it, from choosing to arriving, driven through
 * the handlers its buttons end at. Slate's own hit path cannot run under
 * -nullrhi (CLAUDE.md), so which button a click lands on is a playtest
 * question; everything from the handler inward is checked here: that a row
 * is the universe's answer, that choosing it plots it in the ship and not in
 * the screen, that a second screen agrees without being told, and that the
 * jump is a word with no number in it in every state it has.
 */
bool FNavigationScreenTest::RunTest(const FString& Parameters)
{
    FScopedCVar QuickCharge(TEXT("ds.Nav.ChargeSeconds"), 2.0f);
    FScopedCVar QuickTransit(TEXT("ds.Nav.TransitSeconds"), 1.0f);

    UWorld* World = MakeWorld(TEXT("NavigationScreenTestWorld"));

    // The chart itself, spawned before play begins: a widget component
    // builds its widget and its collision body in BeginPlay, and one spawned
    // into a running world never does.
    AShipNavScreen* Screen = World->SpawnActor<AShipNavScreen>(FVector(300.0, 0.0, 105.0), FRotator::ZeroRotator);
    // A second chart, as though there were another in the ship. Neither is
    // special, and neither is told what the other did.
    AShipNavScreen* OtherScreen = World->SpawnActor<AShipNavScreen>(FVector(300.0, 200.0, 105.0), FRotator::ZeroRotator);
    BeginPlayForActors(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>();
    if (!TestNotNull(TEXT("the chart spawns"), Screen) || !TestNotNull(TEXT("and the other"), OtherScreen)
        || !TestNotNull(TEXT("the world has a ship"), Ship) || !TestNotNull(TEXT("and a universe"), Universe))
    {
        DestroyWorld(World);
        return false;
    }
    TestTrue(TEXT("the chart's panel shows the navigation widget"),
             Screen->GetScreen()->GetWidgetClass() == UNavigationWidget::StaticClass());

    // The widgets the panels made, not ones made beside them: what is under
    // test is the chart the level places.
    UNavigationWidget* Chart = Cast<UNavigationWidget>(Screen->GetScreen()->GetUserWidgetObject());
    UNavigationWidget* Other = Cast<UNavigationWidget>(OtherScreen->GetScreen()->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the chart's panel made its widget"), Chart)
        || !TestNotNull(TEXT("and so did the other's"), Other))
    {
        DestroyWorld(World);
        return false;
    }
    // A panel takes its widget on its component's first tick, which is when
    // the widget builds its tree.
    Screen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);
    OtherScreen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);

    Ship->Tick(0.1f);
    Chart->RefreshFromShip();
    Other->RefreshFromShip();

    // -- where you are, and where you could go --------------------------
    const FSystemId Start = Universe->GetStartSystem();
    const TOptional<FStarSystem> Home = Universe->GetSystem(Start);
    if (!TestTrue(TEXT("the universe has a start system"), Home.IsSet()))
    {
        DestroyWorld(World);
        return false;
    }
    TestEqual(TEXT("here is the start system, its colour, and that it is visited"),
              Chart->GetHereText().ToString(),
              NavText::Place(Home->Stub.Name, Home->Star.Class, true));

    const TArray<FStarSystemStub> Near = Expected(*Universe, *Ship);
    if (!TestTrue(TEXT("there are at least five places to go from the start"), Near.Num() >= 5))
    {
        DestroyWorld(World);
        return false;
    }
    CheckRows(*this, *Chart, *Universe, *Ship, TEXT("at the start"));
    // Home is the only place the ship has been, and home is not a row: so
    // no row reads visited, whatever the ship's record says.
    for (int32 Index = 0; Index < UNavigationWidget::RowCount; ++Index)
    {
        const FString Row = Chart->GetRowText(Index).ToString();
        TestFalse(FString::Printf(TEXT("at the start row %d is not visited (it reads '%s')"), Index, *Row),
                  Row.EndsWith(FString(NavText::Separator) + NavText::Visited(true)));
    }

    // Somewhere to go from which home is still in sight, so that arriving
    // shows a row that must read visited, not only rows that must not.
    int32 DestinationRow = INDEX_NONE;
    for (int32 Index = 0; Index < Near.Num() && DestinationRow == INDEX_NONE; ++Index)
    {
        DestinationRow = ChartFromListsHome(*Universe, Near[Index], Start) ? Index : INDEX_NONE;
    }
    if (!TestTrue(TEXT("some system in reach charts home among its six nearest"), DestinationRow != INDEX_NONE))
    {
        DestroyWorld(World);
        return false;
    }
    TestEqual(TEXT("nothing is plotted to start with"), PlottedRows(*Chart), 0);
    TestEqual(TEXT("no course"), Chart->GetCourseText().ToString(), NavText::NoCourse() + TEXT("."));

    // -- engaging needs a course ----------------------------------------
    TestFalse(TEXT("with no course the toggle cannot be pressed"), Chart->IsEngageEnabled());
    Chart->PressEngage();
    TestFalse(TEXT("and pressing it anyway engages nothing"), Ship->IsJumpEngaged());
    TestEqual(TEXT("the jump is idle"), Chart->GetJumpText().ToString(), FString(TEXT("Idle.")));

    // -- choosing -------------------------------------------------------
    Chart->SelectRow(2);
    TestTrue(TEXT("choosing row 2 plots its system in the ship"),
             Ship->GetPlottedSystem().IsSet() && *Ship->GetPlottedSystem() == Near[2].Id);
    TestTrue(TEXT("the chosen row is marked"), RowIsPlotted(*Chart, 2));
    TestEqual(TEXT("and no other"), PlottedRows(*Chart), 1);
    TestTrue(TEXT("with a course the toggle can be pressed"), Chart->IsEngageEnabled());
    TestEqual(TEXT("and would engage"), Chart->GetEngageLabel().ToString(), FString(TEXT("ENGAGE")));

    // The bearing is the helm's, word for word.
    const TOptional<FVector> Bearing = Ship->GetCourseDirectionShipLocal();
    if (TestTrue(TEXT("a plotted course has a bearing"), Bearing.IsSet()))
    {
        // Joined as the HUD joins its line, with NavText's separator: the
        // chart once put its own dash here.
        TestEqual(TEXT("the course reads as the helm reads it"), Chart->GetCourseText().ToString(),
                  NavText::Course(Near[2].Name, Bearing, Ship->GetJumpConeRadians()) + TEXT("."));
        TestTrue(TEXT("and joins it as the helm does"),
                 NavText::Jump(EJumpState::Idle, Near[2].Name, *Bearing, Ship->GetJumpConeRadians())
                     .EndsWith(Chart->GetCourseText().ToString().LeftChop(1)));
    }

    // The other chart was not told; it asks, and agrees.
    TestEqual(TEXT("the other chart has not looked yet"), PlottedRows(*Other), 0);
    Other->RefreshFromShip();
    TestTrue(TEXT("once it looks, the other chart marks the same row"), RowIsPlotted(*Other, 2));
    TestEqual(TEXT("and reads the same course"), Other->GetCourseText().ToString(), Chart->GetCourseText().ToString());

    // Choosing elsewhere, from the other chart, moves the course; clicking
    // the plotted row clears it.
    Other->SelectRow(4);
    TestTrue(TEXT("choosing from the other chart replots in the ship"),
             Ship->GetPlottedSystem().IsSet() && *Ship->GetPlottedSystem() == Near[4].Id);
    Chart->RefreshFromShip();
    TestTrue(TEXT("and this chart follows"), RowIsPlotted(*Chart, 4) && PlottedRows(*Chart) == 1);
    Chart->SelectRow(4);
    TestFalse(TEXT("choosing the plotted row clears the course"), Ship->GetPlottedSystem().IsSet());
    TestEqual(TEXT("and the chart says so"), Chart->GetCourseText().ToString(), NavText::NoCourse() + TEXT("."));

    Chart->SelectRow(UNavigationWidget::RowCount);
    Chart->SelectRow(-1);
    TestFalse(TEXT("a row that is not there plots nothing"), Ship->GetPlottedSystem().IsSet());

    // -- engaging, standing down, and every state the jump has ------------
    Chart->SelectRow(DestinationRow);
    const FSystemId Destination = Near[DestinationRow].Id;
    const FUniversePosition Parked = Ship->GetFlightState().GetUniversePosition();
    const FVector Toward = Ship->GetCourseDirection().Get(FVector::ForwardVector);

    // Turned away, so a charged drive holds at ready rather than firing.
    Ship->PlaceShip(Parked, FRotationMatrix::MakeFromX(-Toward).ToQuat());

    TArray<FString> JumpWords;
    const auto Look = [&]()
    {
        Chart->RefreshFromShip();
        JumpWords.Add(Chart->GetJumpText().ToString());
    };

    Chart->PressEngage();
    TestTrue(TEXT("the toggle engages the jump"), Ship->IsJumpEngaged());
    Ship->Tick(0.1f);
    Look();
    TestEqual(TEXT("which winds"), Chart->GetJumpText().ToString(), FString(TEXT("Winding.")));
    TestEqual(TEXT("and the toggle would stand it down"), Chart->GetEngageLabel().ToString(),
              FString(TEXT("STAND DOWN")));

    Chart->PressEngage();
    TestFalse(TEXT("pressed again it stands down"), Ship->IsJumpEngaged());
    Look();
    TestEqual(TEXT("idle"), Chart->GetJumpText().ToString(), FString(TEXT("Idle.")));
    TestTrue(TEXT("standing down keeps the course"),
             Ship->GetPlottedSystem().IsSet() && *Ship->GetPlottedSystem() == Destination);

    Chart->PressEngage();
    TickUntil(Ship, 30.0, [Ship] { return Ship->GetJumpState() == EJumpState::Ready; });
    Look();
    TestEqual(TEXT("charged and turned away, it is ready"), Chart->GetJumpText().ToString(), FString(TEXT("Ready.")));

    Ship->PlaceShip(Parked, FRotationMatrix::MakeFromX(Toward).ToQuat());
    Ship->Tick(0.1f);
    Look();
    if (TestTrue(TEXT("aligned, it fires by itself"), Ship->IsInTransit()))
    {
        TestEqual(TEXT("between stars, the jump says so"), Chart->GetJumpText().ToString(),
                  FString(TEXT("Between stars.")));
        TestEqual(TEXT("and so does here"), Chart->GetHereText().ToString(), FString(TEXT("Between stars")));
        TestEqual(TEXT("the course is named with no bearing"), Chart->GetCourseText().ToString(),
                  NavText::Course(Near[DestinationRow].Name, {}, Ship->GetJumpConeRadians()) + TEXT("."));
        TestFalse(TEXT("nothing can be engaged between stars"), Chart->IsEngageEnabled());
        TestTrue(TEXT("the rows stay readable between stars"), Chart->GetShownRowCount() > 0);
        TestEqual(TEXT("and none of them can be pressed"), EnabledRows(*Chart), 0);
        Chart->SelectRow(0);
        TestTrue(TEXT("nor replotted"), *Ship->GetPlottedSystem() == Destination);
    }

    TickUntil(Ship, 10.0, [Ship] { return !Ship->IsInTransit(); });
    Look();

    // -- arrived --------------------------------------------------------
    const TOptional<FStarSystem> There = Universe->GetSystem(Destination);
    if (TestTrue(TEXT("the jump arrives"), !Ship->IsInTransit() && There.IsSet()))
    {
        TestEqual(TEXT("here is where the jump went, visited"), Chart->GetHereText().ToString(),
                  NavText::Place(There->Stub.Name, There->Star.Class, true));
        TestEqual(TEXT("the course is spent"), Chart->GetCourseText().ToString(), NavText::NoCourse() + TEXT("."));
        TestEqual(TEXT("and the jump is idle"), Chart->GetJumpText().ToString(), FString(TEXT("Idle.")));
        CheckRows(*this, *Chart, *Universe, *Ship, TEXT("on arrival"));

        // Home, chosen above to be among the six nearest from here, is now a
        // row, and somewhere you have been.
        bool bHomeShown = false;
        for (int32 Index = 0; Index < UNavigationWidget::RowCount; ++Index)
        {
            const FString Row = Chart->GetRowText(Index).ToString().TrimStart();
            if (Row.StartsWith(Home->Stub.Name + NavText::Separator))
            {
                bHomeShown = true;
                TestTrue(FString::Printf(TEXT("home is listed as visited (it reads '%s')"), *Row),
                         Row.EndsWith(FString(NavText::Separator) + NavText::Visited(true)));
            }
        }
        TestTrue(TEXT("home is among the rows on arrival"), bHomeShown);
    }

    // A word, never a number, in every state the jump has been in here.
    TestEqual(TEXT("the jump was seen in all four of its states"), TSet<FString>(JumpWords).Num(), 4);
    for (const FString& Word : JumpWords)
    {
        TestFalse(FString::Printf(TEXT("'%s' has no digit and no %%"), *Word), HasNumber(Word));
    }

    DestroyWorld(World);
    return true;
}

/**
 * The chair: that the chart can be reached, sat at, and clicked, the way the
 * laptop can. Every property here is one a screen has shipped without: a
 * reach volume in front of the glass, a static part left behind when the
 * actor moved, and a seat facing the wrong way.
 */
bool FNavScreenChairTest::RunTest(const FString& Parameters)
{
    UWorld* World = MakeWorld(TEXT("NavScreenChairTestWorld"));

    // Both before play begins, for the widget component's sake.
    ADeepSpaceCharacter* Player = World->SpawnActor<ADeepSpaceCharacter>(FVector(0.0, 0.0, 90.0), FRotator::ZeroRotator);
    AShipNavScreen* Chart = World->SpawnActor<AShipNavScreen>(FVector(200.0, 0.0, 105.0), FRotator::ZeroRotator);
    BeginPlay(World);

    if (!TestNotNull(TEXT("the character spawns"), Player) || !TestNotNull(TEXT("the chart spawns"), Chart))
    {
        DestroyWorld(World);
        return false;
    }
    UWidgetComponent* Panel = Chart->GetScreen();
    UBoxComponent* Reach = Chart->GetReach();
    if (!TestNotNull(TEXT("it has a panel"), Panel) || !TestNotNull(TEXT("and a reach volume"), Reach))
    {
        DestroyWorld(World);
        return false;
    }

    TestTrue(TEXT("the chart is somewhere you sit"), Chart->IsUsable());
    TestTrue(TEXT("its prompt is to sit at the chart"),
             Chart->GetInteractable()->GetPrompt().ToString().Contains(TEXT("Sit at")) &&
             Chart->GetInteractable()->GetPrompt().ToString().Contains(TEXT("chart")));

    // 68 cm across at 816 px: the laptop's 12 px/cm, so its text is the
    // laptop's size.
    const FVector Scale = Panel->GetComponentScale();
    TestTrue(TEXT("the panel's scale is uniform"),
             FMath::IsNearlyEqual(Scale.X, Scale.Y, 1e-4f) && FMath::IsNearlyEqual(Scale.Y, Scale.Z, 1e-4f));
    TestTrue(TEXT("the panel is 68 cm across"),
             FMath::IsNearlyEqual(Panel->GetDrawSize().X * Scale.X, 68.0, 0.01));
    TestEqual(TEXT("the reach volume moves with the actor"),
              static_cast<int32>(Reach->Mobility), static_cast<int32>(EComponentMobility::Movable));

    // A hand-made world has no game mode, and it is the game mode that
    // dispatches actors' BeginPlay: World->BeginPlay() alone begins play for
    // the subsystems and for no actor at all. The chart binds E to sitting
    // down in its BeginPlay, so that is dispatched here, or E's path would be
    // the one thing this test could not see.
    Chart->DispatchBeginPlay();
    TestTrue(TEXT("the chart has begun play"), Chart->HasActorBegunPlay());

    // The eyes settle on the first tick; aim from where they settle.
    Player->Tick(0.016f);

    // Put the glass squarely in front of the eyes, inside reach, as a player
    // walking up to it would have it -- moving the actor, so a part left
    // behind by the move is caught.
    const FVector Wanted = Player->GetEyeLocation() + FVector(150.0, 0.0, 0.0);
    Chart->AddActorWorldOffset(Wanted - Panel->GetComponentLocation());

    const FTransform Face = Panel->GetComponentTransform();
    const FVector Normal = Face.GetUnitAxis(EAxis::X);   // a widget quad's normal, toward the reader
    const FVector Right = Face.GetUnitAxis(EAxis::Y);
    const FVector Up = Face.GetUnitAxis(EAxis::Z);
    const FVector Centre = Panel->GetComponentLocation();
    TestTrue(TEXT("the glass faces the player, as a fixture at yaw 0 faces -X"),
             Normal.Equals(-FVector::ForwardVector, 1e-3));

    // Every part of the glass is the first thing a trace meets, from where a
    // standing player's eyes are: in front and above. The reach volume is
    // behind the glass and never in front of it.
    {
        const FVector Eye = Centre + Normal * 60.0 + FVector(0.0, 0.0, 40.0);
        const FVector2D Half = Panel->GetDrawSize() * Scale.X * 0.5 * 0.9;
        const TArray<FVector> Targets = {
            Centre,
            Centre + Right * Half.X + Up * Half.Y,
            Centre - Right * Half.X + Up * Half.Y,
            Centre + Right * Half.X - Up * Half.Y,
            Centre - Right * Half.X - Up * Half.Y,
        };
        int32 Blocked = 0;
        for (const FVector& Target : Targets)
        {
            FHitResult Hit;
            World->LineTraceSingleByChannel(Hit, Eye, Target, ECC_Visibility,
                                            FCollisionQueryParams(SCENE_QUERY_STAT(NavScreenGlass), false));
            Blocked += Hit.GetComponent() == Panel ? 0 : 1;
        }
        TestEqual(TEXT("every part of the glass can be pointed at"), Blocked, 0);

        // The rim, on every side: an eye on the bezel still finds the chart,
        // and one well off it does not, since the next screen along the desk
        // is 15 cm away and must answer for itself.
        const double HalfWidth = Panel->GetDrawSize().X * Scale.X * 0.5;
        const double HalfHeight = Panel->GetDrawSize().Y * Scale.X * 0.5;
        struct FSide { const TCHAR* Name; FVector Out; double Half; };
        const FSide Sides[] = {
            {TEXT("one side"), Right, HalfWidth},
            {TEXT("the other side"), -Right, HalfWidth},
            {TEXT("the top"), Up, HalfHeight},
            {TEXT("the bottom"), -Up, HalfHeight},
        };
        for (const FSide& Side : Sides)
        {
            const auto Trace = [&](double PastEdgeCm)
            {
                const FVector At = Centre + Side.Out * (Side.Half + PastEdgeCm);
                FHitResult Hit;
                World->LineTraceSingleByChannel(Hit, At + Normal * 60.0, At - Normal * 2.0, ECC_Visibility,
                                                FCollisionQueryParams(SCENE_QUERY_STAT(NavScreenRim), false));
                return Hit;
            };
            const FHitResult OnBezel = Trace(1.5);
            TestTrue(FString::Printf(TEXT("the bezel at %s is the reach volume's"), Side.Name),
                     OnBezel.GetComponent() == Reach && OnBezel.GetActor() == Chart);
            TestTrue(FString::Printf(TEXT("6 cm off %s is not the chart"), Side.Name),
                     Trace(6.0).GetActor() != Chart);
        }
    }

    // The player's own traces: E's prompt, and the pointer on the glass.
    Player->Tick(0.016f);
    TestTrue(TEXT("looking at it, E would sit at the chart"),
             Player->GetFocusedInteractable() == Chart->GetInteractable());
    if (UWidgetInteractionComponent* Pointer = Player->GetPointer())
    {
        Pointer->TickComponent(0.016f, LEVELTICK_All, nullptr);
        TestTrue(TEXT("and the pointer lands on the glass"),
                 Pointer->GetLastHitResult().GetComponent() == Panel);
    }

    // Sitting down: through the interactable, as E does it. The chair is a
    // seat, not a lock (system map spec, decision 13): the body sits and the
    // view stays the player's. Zooming the chart from it, E looking at it,
    // is DeepSpace.Ship.ChartChair's.
    Chart->GetInteractable()->Interact(Player);
    if (TestTrue(TEXT("interacting sits the player in the chart's chair"), Player->IsInScreenChair()))
    {
        TestEqual(TEXT("seated"), Player->GetPosture(), EPosture::Seated);
        TestFalse(TEXT("and the chart is not framed by sitting"), Player->IsUsingScreen());

        const FTransform Seat = Chart->GetUseTransform();
        TestTrue(TEXT("on the chair"), FVector::Dist2D(Player->GetActorLocation(), Seat.GetLocation()) < 1.0);
        // 126 cm: from the mount at cockpit x 301 back to the starboard
        // pilot_seat's centre at x 175, so the body is on the chair and not
        // perched on its front edge.
        TestTrue(TEXT("which is the chair's distance back from the glass"),
                 FMath::IsNearlyEqual(FVector::Dist2D(Seat.GetLocation(), Centre), 126.0, 0.5));
        TestTrue(TEXT("at the chair's height"), FMath::IsNearlyEqual(Seat.GetLocation().Z, 55.0, 0.01));
        TestTrue(TEXT("facing the chart"),
                 FVector::DotProduct(Player->GetActorForwardVector(),
                                     (Centre - Player->GetActorLocation()).GetSafeNormal2D()) > 0.999);

        // Zoomed, the eyes lean in to the chart's own 60 cm, square on.
        TestTrue(TEXT("its framing is 60 cm off the glass, square on"),
                 Chart->GetViewTransform().GetLocation().Equals(Centre + Normal * 60.0, 1.0));

        Player->StopUsingScreen();
        TestFalse(TEXT("and standing up leaves it"), Player->IsInScreenChair());
    }

    DestroyWorld(World);
    return true;
}

#endif
