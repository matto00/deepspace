#include "Components/WidgetComponent.h"
#include "GenericPlatform/GenericApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"
#include "Widgets/SWidget.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/NavStart.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipLaptop.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipNavScreen.h"
#include "Sky/LocalSystem.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "UI/ShipHUDWidget.h"
#include "UI/SystemMapLayout.h"
#include "UI/SystemMapView.h"
#include "UI/SystemMapWidget.h"
#include "UI/TargetMarker.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystem.h"
#include "Universe/SystemNames.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

// Two tests and no children under either path: a test path with children
// becomes a group, and a group silently stops running its own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSystemMapScreenTest,
    "DeepSpace.UI.SystemMapScreen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipMapScreenTest,
    "DeepSpace.Ship.MapScreen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SystemMapScreenTestLocal
{
    /** Where the layout puts the map's glass: the middle desk screen, 1 cm
     *  proud of its aft face, on the centre line, facing aft (system map
     *  spec, decision 1). Tools/test_placement.py holds the layout to it. */
    const FVector MapGlass(1711.0, 0.0, 105.0);

    /** What row Orbit must read: the numeral the orrery labels its dot
     *  with, always; the kind, or the given name of an inhabited world in
     *  its place; and the surface distance in the altitude line's words.
     *  Built from procgen and NavText, not from the widget. */
    FString ExpectedRow(const FStarSystem& System, int32 Orbit, const FUniversePosition& Where)
    {
        const FPlanet& Planet = System.Planets[Orbit];
        const double Surface = FMath::Max(0.0, Where.DistanceTo(System.PlanetPosition(Orbit))
            - Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius);
        const FString Second = Planet.GivenName.IsEmpty() ? NavText::WorldKind(Planet.Kind) : Planet.GivenName;
        return SystemNames::RomanNumeral(Orbit + 1) + NavText::Separator + Second + NavText::Separator
            + UShipHUDWidget::AltitudeWords(Surface);
    }

    /** Every row as ExpectedRow says, and the target's -- Marked, if any --
     *  starting with the chart's mark. */
    bool RowsAre(FAutomationTestBase& Test, const USystemMapWidget& Map, const FStarSystem& System,
                 const FUniversePosition& Where, const TCHAR* When, int32 Marked = INDEX_NONE)
    {
        bool bAll = Test.TestEqual(FString::Printf(TEXT("%s: one row per world"), When),
                                   Map.GetShownRowCount(), System.Planets.Num());
        for (int32 Orbit = 0; Orbit < System.Planets.Num(); ++Orbit)
        {
            const FString Mark = Orbit == Marked ? FString(UNavigationWidget::PlottedMark) + TEXT(" ") : FString();
            bAll &= Test.TestEqual(FString::Printf(TEXT("%s: row %d"), When, Orbit),
                                   Map.GetRowText(Orbit).ToString(), Mark + ExpectedRow(System, Orbit, Where));
        }
        return bAll;
    }

    /** One of the priors moved for a scope and put back, as though
     *  ds.Universe.ReloadPriors had read a tuned ini and then the original. */
    struct FScopedPrior
    {
        double& Prior;
        double Previous;

        FScopedPrior(double& InPrior, double Value)
            : Prior(InPrior)
            , Previous(InPrior)
        {
            Prior = Value;
        }

        ~FScopedPrior()
        {
            Prior = Previous;
        }
    };
}

/**
 * The map as the ship's answer: a row per world in the altitude line's own
 * words, the orrery laid out from the system the ship is in, and a drawing
 * asked for only when the system, transit or the priors change -- not when
 * the ship flies 100 AU within the system. Between stars it says so and
 * draws nothing. Both pick paths end at SelectWorld, which marks the ship's
 * target or lets it go; the target shows on the map however it was set, with
 * the ship's own target line, and the band's button plots and engages the
 * in-system jump, stands it down, or says the world is near enough to fly.
 * An in-system fold keeps the system drawn and says so.
 *
 * Through the real screen: the actor spawned before play, its widget
 * component's own widget, refreshed as its tick refreshes it.
 */
bool FSystemMapScreenTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace SystemMapScreenTestLocal;

    FSkyWorld Test(TEXT("SystemMapScreenWorld"));
    AShipMapScreen* Screen = Test.World ? Test.World->SpawnActor<AShipMapScreen>(MapGlass, FRotator::ZeroRotator) : nullptr;
    if (!TestNotNull(TEXT("the map spawns"), Screen) || !TestNotNull(TEXT("the world has a ship"), Test.Ship)
        || !TestNotNull(TEXT("and a universe"), Test.Universe))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    const UUniverseSubsystem* Universe = Test.Universe;

    UWidgetComponent* Panel = Screen->GetScreen();
    USystemMapWidget* Map = Cast<USystemMapWidget>(Panel->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the placed map's widget is a USystemMapWidget"), Map))
    {
        return false;
    }
    // A frame as the map sees one: the component's tick, then Slate's tick
    // of the widget, which is what calls NativeTick in play. Headless the
    // component never paints, and painting is what ticks a widget in play,
    // so the Slate tick is made by hand -- on the widget's own Slate side,
    // never by calling RefreshFromShip, so a map that stopped refreshing
    // itself would fail here. GetCanTick is the one hop this skips: whether
    // the paint pass would tick it at all.
    const TSharedRef<SWidget> Slate = Map->TakeWidget();
    TestTrue(TEXT("the map's Slate widget asks to be ticked"), Slate->GetCanTick());
    double Clock = 0.0;
    const auto Look = [Panel, Slate, &Clock]()
    {
        constexpr float Frame = 1.0f / 60.0f;
        Panel->TickComponent(Frame, LEVELTICK_All, nullptr);
        Clock += Frame;
        Slate->Tick(FGeometry::MakeRoot(FVector2D(600.0, 424.0), FSlateLayoutTransform()), Clock, Frame);
    };
    const auto Where = [Ship]() { return Ship->GetFlightState().GetUniversePosition(); };
    const auto Facing = [Ship]() { return Ship->GetFlightState().GetUniverseOrientation(); };
    Look();

    const TOptional<FStarSystem> Home = Universe->GetSystemAt(Where());
    if (!TestTrue(TEXT("the ship opens in a system with worlds"), Home.IsSet() && Home->Planets.Num() >= 2))
    {
        return false;
    }

    // -- the rows, the title, the orrery ------------------------------------
    RowsAre(*this, *Map, *Home, Where(), TEXT("at the opening"));
    TestEqual(TEXT("the title names the system as the chart does"), Map->GetTitleText().ToString(),
              NavText::Place(Home->Stub.Name, Home->Star.Class));
    TestTrue(TEXT("the target line is empty with no target"), Map->GetTargetText().IsEmpty());
    TestTrue(TEXT("in the plane, on the map, the footer says nothing"), Map->GetFooterText().IsEmpty());
    {
        const SystemMap::FMapLayout Expected = SystemMap::Layout(*Home, SystemMap::Fit(*Home, UShipSubsystem::GetStandoffAU()));
        const SystemMap::FMapLayout* Drawn = Map->GetLayout();
        bool bSame = Drawn && Drawn->Dots.Num() == Expected.Dots.Num() && Drawn->RingPx == Expected.RingPx;
        for (int32 Orbit = 0; bSame && Orbit < Expected.Dots.Num(); ++Orbit)
        {
            bSame &= Drawn->Dots[Orbit].Centre.Equals(Expected.Dots[Orbit].Centre, 1.0e-9);
        }
        TestTrue(TEXT("the orrery is the system's layout, fitted to the arrival standoff as tuned"), bSame);
        TestTrue(TEXT("and the view is drawing it"), Map->GetView()->HasDrawing());
    }

    // The window's order, which the map's colours and radii are read in:
    // the star first, then the worlds innermost first.
    {
        const FSkySystem Sky = LocalSystem::Here(Home);
        bool bOrder = Sky.Bodies.Num() == Home->Planets.Num() + 1;
        for (int32 Orbit = 0; bOrder && Orbit < Home->Planets.Num(); ++Orbit)
        {
            bOrder &= Sky.Bodies[Orbit + 1].Position == Home->PlanetPosition(Orbit)
                && FMath::IsNearlyEqual(Sky.Bodies[Orbit + 1].Radius, Home->Planets[Orbit].RadiusEarth * UniverseUnits::CmPerEarthRadius, 1.0);
        }
        TestTrue(TEXT("the sky's body i + 1 is world i"), bOrder);
    }

    // The nearest world's row agrees with the altitude line to the digit.
    {
        const FSkyNearestSurface Nearest = LocalSystem::NearestSurface(LocalSystem::Here(Home), Where());
        if (TestTrue(TEXT("at the opening a world is the nearest surface"), Nearest.Body >= 1))
        {
            TestTrue(TEXT("its row ends in the altitude line's words"),
                     Map->GetRowText(Nearest.Body - 1).ToString().EndsWith(UShipHUDWidget::AltitudeWords(Nearest.Distance)));
        }
    }

    // -- nothing changes: the drawing is not asked for again ------------------
    int32 Asked = Map->GetLayoutAsked();
    for (int32 Frame = 0; Frame < 60; ++Frame)
    {
        Look();
        Test.Step(1.0f / 60.0f);
    }
    TestEqual(TEXT("a second with nothing changing asks for no drawing"), Map->GetLayoutAsked(), Asked);

    // -- 100 AU within the system: the ship moves, the drawing does not ------
    {
        const TArray<FString> Before = {Map->GetRowText(0).ToString(), Map->GetRowText(1).ToString()};
        const FUniversePosition Far = Home->Stub.Position + FVector(0.0, 100.0 * UniverseUnits::CmPerAU, 0.0);
        TestTrue(TEXT("100 AU out is still in the system"), Universe->GetSystemIdAt(Far) == TOptional<FSystemId>(Home->Stub.Id));
        Ship->PlaceShip(Far, Facing());
        Look();
        TestEqual(TEXT("flying 100 AU within the system asks for no drawing"), Map->GetLayoutAsked(), Asked);
        RowsAre(*this, *Map, *Home, Where(), TEXT("100 AU out"));
        TestNotEqual(TEXT("and the rows read the new distances"), Map->GetRowText(0).ToString(), Before[0]);
        TestTrue(TEXT("out past the rim, the footer says so"), Map->GetFooterText().ToString().Contains(TEXT("Beyond the map.")));
    }

    // -- the footer: held at the edges, and off the plane --------------------
    {
        const SystemMap::FMapScale Scale = SystemMap::Fit(*Home, UShipSubsystem::GetStandoffAU());
        Ship->PlaceShip(Home->Stub.Position + FVector(0.5 * Scale.InnerAU * UniverseUnits::CmPerAU, 0.0, 0.0), Facing());
        Look();
        TestEqual(TEXT("inside the inner knot, the footer names the innermost orbit"), Map->GetFooterText().ToString(),
                  FString::Printf(TEXT("Inside %s's orbit."), *Home->Planets[0].Designation));

        const double Out = Home->Planets.Last().SemiMajorAxisAU * UniverseUnits::CmPerAU;
        Ship->PlaceShip(Home->Stub.Position + FVector(0.0, 0.0, Out), Facing());
        Look();
        TestEqual(TEXT("over the pole, the footer says how far above the plane"), Map->GetFooterText().ToString(),
                  FString(TEXT("90° above the plane.")));
        Ship->PlaceShip(Home->Stub.Position + FVector(Out, 0.0, -Out * FMath::Tan(FMath::DegreesToRadians(18.0))), Facing());
        Look();
        TestEqual(TEXT("and below it"), Map->GetFooterText().ToString(), FString(TEXT("18° below the plane.")));

        // The longest footer there is, both held and off the plane, shares
        // its row with the band's button: it wraps short of the room the
        // button keeps, in the two lines the row has, and never runs under it.
        const double Inner = 0.5 * Scale.InnerAU * UniverseUnits::CmPerAU;
        Ship->PlaceShip(Home->Stub.Position + Inner * FVector(FMath::Cos(FMath::DegreesToRadians(34.0)), 0.0,
                                                              FMath::Sin(FMath::DegreesToRadians(34.0))), Facing());
        Look();
        TestEqual(TEXT("held inside and off the plane, the footer says both"), Map->GetFooterText().ToString(),
                  FString::Printf(TEXT("Inside %s's orbit. 34° above the plane."), *Home->Planets[0].Designation));
        const FVector2D Longest = Map->MeasureFooter();
        const float Room = 600.0f - 2.0f * 6.0f - USystemMapWidget::JumpReserve;
        AddInfo(FString::Printf(TEXT("the longest footer, \"%s\", is %.0f x %.0f px in a row of %.0f"),
                                *Map->GetFooterText().ToString(), Longest.X, Longest.Y, Room));
        TestTrue(FString::Printf(TEXT("it is measured (%.0f x %.0f px)"), Longest.X, Longest.Y), Longest.X > 100.0 && Longest.Y > 10.0);
        TestTrue(FString::Printf(TEXT("and stops short of the button's room (%.0f of %.0f px)"), Longest.X, Room), Longest.X <= Room);
        TestTrue(FString::Printf(TEXT("in no more than the row's 44 px (%.0f px)"), Longest.Y), Longest.Y <= 44.0);
        Ship->PlaceShip(Home->Stub.Position + FVector(Out, 0.0, Out * FMath::Tan(FMath::DegreesToRadians(4.0))), Facing());
        Look();
        TestTrue(TEXT("but not within 5 degrees of it"), Map->GetFooterText().IsEmpty());
        TestEqual(TEXT("none of it asked for a drawing"), Map->GetLayoutAsked(), Asked);
    }

    // -- both pick paths end at SelectWorld, which marks the ship's target ---
    const auto Held = [Ship]() { return Ship->GetTarget(); };
    const auto Is = [&Home](const TOptional<FBodyId>& Target, int32 Orbit)
    {
        return Target.IsSet() && *Target == FBodyId{Home->Stub.Id, Orbit, -1};
    };
    {
        Map->PressRow(1);
        TestTrue(TEXT("a row targets its world"), Is(Held(), 1));
        TestTrue(TEXT("and the row carries the mark at once"), Map->GetRowText(1).ToString().StartsWith(UNavigationWidget::PlottedMark));
        const SystemMap::FMapLayout* Drawn = Map->GetLayout();
        if (Drawn)
        {
            Map->GetView()->ClickAt(Drawn->Dots[0].Centre + FVector2D(1.0, 1.0));
            TestTrue(TEXT("a dot targets its world, replacing the last"), Is(Held(), 0));
            TestFalse(TEXT("whose row is no longer marked"), Map->GetRowText(1).ToString().StartsWith(UNavigationWidget::PlottedMark));
            Map->GetView()->ClickAt(Drawn->Pixels.Centre);
            TestTrue(TEXT("the star is not a pick"), Is(Held(), 0));
        }
        Map->SelectWorld(Home->Planets.Num());
        TestTrue(TEXT("nor is an orbit the system lacks"), Is(Held(), 0));
        Map->SelectWorld(0);
        TestFalse(TEXT("selecting the target again lets it go"), Held().IsSet());
        TestFalse(TEXT("and its row is unmarked"), Map->GetRowText(0).ToString().StartsWith(UNavigationWidget::PlottedMark));
    }

    // -- the orrery picks on a press released on it, as a row's button does ---
    // A second pick of the same world clears it, so "picks nothing" is a
    // target left exactly as it was.
    if (const SystemMap::FMapLayout* Drawn = Map->GetLayout())
    {
        // The view's own Slate widget, given the root geometry: absolute is
        // local, so a pointer at a dot's centre is on the dot.
        const TSharedRef<SWidget> Orrery = Map->GetView()->TakeWidget();
        const FGeometry Square = FGeometry::MakeRoot(FVector2D(256.0, 256.0), FSlateLayoutTransform());
        const auto Pointer = [](const FVector2D& At)
        {
            return FPointerEvent(0, At, At, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
        };
        const FVector2D OnDot = Drawn->Dots[0].Centre;
        Orrery->OnMouseButtonDown(Square, Pointer(OnDot));
        TestFalse(TEXT("a press on a dot alone picks nothing yet"), Held().IsSet());
        Orrery->OnMouseButtonUp(Square, Pointer(OnDot));
        TestTrue(TEXT("released on it, it picks that world"), Is(Held(), 0));
        Orrery->OnMouseButtonUp(Square, Pointer(OnDot));
        TestTrue(TEXT("a release with no press picks nothing"), Is(Held(), 0));
        Orrery->OnMouseButtonDown(Square, Pointer(OnDot));
        Orrery->OnMouseLeave(Pointer(FVector2D(300.0, 300.0)));
        Orrery->OnMouseButtonUp(Square, Pointer(OnDot));
        TestTrue(TEXT("a press that left the orrery before it was released picks nothing"), Is(Held(), 0));
    }

    // -- a target set elsewhere shows here untold, with the ship's own line --
    {
        FOutputDeviceNull Quiet;
        IConsoleObject* Target = IConsoleManager::Get().FindConsoleObject(TEXT("ds.Nav.Target"));
        if (TestNotNull(TEXT("ds.Nav.Target exists"), Target) && Target->AsCommand())
        {
            Target->AsCommand()->Execute({TEXT("2")}, Test.World, Quiet);
        }
        TestTrue(TEXT("ds.Nav.Target 2 marks the second world"), Is(Held(), 1));
        Look();
        TestTrue(TEXT("and the map marks its row without being told"),
                 Map->GetRowText(1).ToString().StartsWith(UNavigationWidget::PlottedMark));
        const TOptional<FTargetView> Seen = Ship->GetTargetView(*Home);
        if (TestTrue(TEXT("the ship sees its target"), Seen.IsSet()))
        {
            TestEqual(TEXT("the target line is the ship's view of it, printed"), Map->GetTargetText().ToString(),
                      TargetMarker::Line(*Seen));
            TestTrue(TEXT("naming the world"), Map->GetTargetText().ToString().StartsWith(
                         FString(UNavigationWidget::PlottedMark) + TEXT(" ") + NavText::WorldName(Home->Planets[1])));
        }
    }

    // -- the band's button: the in-system jump, from the map -----------------
    {
        const FUniversePosition Parked = Where();
        const FQuat Facing0 = Facing();
        TestTrue(TEXT("with a target the button is there"), Map->IsJumpButtonShown());
        const FString Label = Map->GetJumpButtonText().ToString();
        if (Ship->IsNearEnoughToFly(*Home, *Held()))
        {
            TestEqual(TEXT("near enough to fly: it says so"), Label, FString(TEXT("Near enough to fly")));
        }
        // From the far side of the system, well outside any world's reach.
        Ship->PlaceShip(Home->Stub.Position + FVector(0.0, 0.0, 3.0 * UniverseUnits::CmPerAU), Facing0);
        Look();
        TestEqual(TEXT("it reads Jump here"), Map->GetJumpButtonText().ToString(), FString(TEXT("Jump here")));
        TestTrue(TEXT("and can be pressed"), Map->IsJumpButtonEnabled());
        Map->PressJumpButton();
        TestTrue(TEXT("Jump here plots the target as the course"), Ship->GetPlottedWorld() == Held() && Held().IsSet());
        TestTrue(TEXT("and engages, in one press"), Ship->IsJumpEngaged());
        TestFalse(TEXT("it is no star course"), Ship->GetPlottedSystem().IsSet());
        TestEqual(TEXT("then it reads Stand down"), Map->GetJumpButtonText().ToString(), FString(TEXT("Stand down")));
        Map->PressJumpButton();
        TestFalse(TEXT("Stand down clears the course"), Ship->HasCourse());
        TestFalse(TEXT("which stands the jump down"), Ship->IsJumpEngaged());
        TestTrue(TEXT("and leaves the target"), Is(Held(), 1));
        TestEqual(TEXT("which can be jumped to again"), Map->GetJumpButtonText().ToString(), FString(TEXT("Jump here")));

        // Inside twice the standoff: near enough to fly.
        const FVector Out = FVector(0.0, 0.0, 1.0);
        const double Radius = Home->Planets[1].RadiusEarth * UniverseUnits::CmPerEarthRadius;
        const double Standoff = NavStart::WorldStandoffCm(Radius, UShipSubsystem::FloorFor(LocalSystem::Here(Home).Bodies[2]));
        Ship->PlaceShip(Home->PlanetPosition(1) + Out * (1.5 * Standoff), Facing0);
        Look();
        TestEqual(TEXT("inside the reach it reads Near enough to fly"), Map->GetJumpButtonText().ToString(), FString(TEXT("Near enough to fly")));
        TestFalse(TEXT("and cannot be pressed"), Map->IsJumpButtonEnabled());
        const FVector2D Widest = Map->MeasureJumpButton();
        TestTrue(FString::Printf(TEXT("its widest label fits the room the footer leaves it (%.0f px of %.0f)"), Widest.X,
                                 USystemMapWidget::JumpReserve - USystemMapWidget::JumpGap),
                 Widest.X > 60.0 && Widest.X <= USystemMapWidget::JumpReserve - USystemMapWidget::JumpGap);
        Map->PressJump();
        TestFalse(TEXT("pressed anyway, nothing is plotted"), Ship->HasCourse());

        Ship->ClearTarget();
        Look();
        TestFalse(TEXT("with no target the button is absent"), Map->IsJumpButtonShown());
        TestTrue(TEXT("and the line is empty"), Map->GetTargetText().IsEmpty());
        Ship->PlaceShip(Parked, Facing0);
        Look();
    }

    // -- the standoff the rim is fitted to: drawn again when it is retuned ---
    {
        Asked = Map->GetLayoutAsked();
        {
            FScopedCVar Wider(TEXT("ds.Nav.StandoffAU"), 4.0f);
            Look();
            TestEqual(TEXT("a retuned ds.Nav.StandoffAU is drawn on the next frame"), Map->GetLayoutAsked(), Asked + 1);
        }
        Look();
        TestEqual(TEXT("and put back, drawn again"), Map->GetLayoutAsked(), Asked + 2);
    }

    // -- another system, placed into without a jump: drawn again at once ------
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    TOptional<FStarSystem> Other;
    for (const FStarSystemStub& Stub : Chart)
    {
        Other = Universe->GetSystem(Stub.Id);
        if (Other && Other->Planets.Num() != Home->Planets.Num() && !Other->Planets.IsEmpty())
        {
            break;
        }
        Other.Reset();
    }
    if (TestTrue(TEXT("a neighbour with a different number of worlds (or this proves little)"), Other.IsSet()))
    {
        TestTrue(TEXT("home's first world is marked before leaving"), Ship->SetTarget(FBodyId{Home->Stub.Id, 0, -1}));
        Asked = Map->GetLayoutAsked();
        Ship->PlaceShip(Other->Stub.Position + FVector(2.0 * UniverseUnits::CmPerAU, 0.0, 0.0), Facing());

        Look();
        TestEqual(TEXT("a PlaceShip into another system is drawn on the next frame, once"), Map->GetLayoutAsked(), Asked + 1);
        RowsAre(*this, *Map, *Other, Where(), TEXT("in the other system"));
        TestEqual(TEXT("and titled for it"), Map->GetTitleText().ToString(), NavText::Place(Other->Stub.Name, Other->Star.Class));
        TestTrue(TEXT("its selections are its own worlds"), SystemMap::Select(*Other, 0, {})->Body.System == Other->Stub.Id);
        TestTrue(TEXT("home's target is still held (or the next two prove nothing)"), Is(Held(), 0));
        TestFalse(TEXT("but it names nothing here, so no row is marked"), Map->GetRowText(0).ToString().StartsWith(UNavigationWidget::PlottedMark));
        TestTrue(TEXT("and there is no target line"), Map->GetTargetText().IsEmpty());
        TestFalse(TEXT("nor a button"), Map->IsJumpButtonShown());
        TestFalse(TEXT("and it cannot be set to another system's world"), Ship->SetTarget(FBodyId{Home->Stub.Id, 1, -1}));

        // A press in the frame the system changes, on a row of the system
        // the glass still shows: refused, not turned into the same orbit of
        // a system the player never saw.
        Ship->PlaceShip(Home->Stub.Position + FVector(0.0, 3.0 * UniverseUnits::CmPerAU, 0.0), Facing());
        Ship->ClearTarget();
        Asked = Map->GetLayoutAsked();
        TestTrue(TEXT("the other system's first row is still on the glass (or this proves nothing)"), Map->IsRowEnabled(0));
        Map->PressRow(0);
        TestEqual(TEXT("the press is what brought the map home"), Map->GetLayoutAsked(), Asked + 1);
        TestFalse(TEXT("and a press made on a drawing that did not survive it picks nothing"), Held().IsSet());
        Map->PressRow(0);
        TestTrue(TEXT("pressed again, on the drawing now shown, it picks home's world"), Is(Held(), 0));
        Ship->ClearTarget();
    }

    // -- an inhabited world, and a system with none: the corpus near home ----
    {
        TOptional<FStarSystem> Inhabited;
        TOptional<FStarSystem> Empty;
        TArray<FStarSystemStub> Near = Universe->GetSystemsNear(Home->Stub.Position, 60.0 * UniverseUnits::CmPerLightYear);
        Near.SetNum(FMath::Min(Near.Num(), 2000));
        for (const FStarSystemStub& Stub : Near)
        {
            if (Inhabited && Empty)
            {
                break;
            }
            TOptional<FStarSystem> System = Universe->GetSystem(Stub.Id);
            if (!System)
            {
                continue;
            }
            if (!Empty && System->Planets.IsEmpty())
            {
                Empty = System;
            }
            if (!Inhabited && System->Planets.ContainsByPredicate([](const FPlanet& Planet) { return !Planet.GivenName.IsEmpty(); }))
            {
                Inhabited = System;
            }
        }
        if (TestTrue(TEXT("an inhabited world near home (or this proves nothing)"), Inhabited.IsSet()))
        {
            Ship->PlaceShip(Inhabited->Stub.Position + FVector(0.0, 2.0 * UniverseUnits::CmPerAU, 0.0), Facing());
            Look();
            RowsAre(*this, *Map, *Inhabited, Where(), TEXT("with an inhabited world"));
            for (int32 Orbit = 0; Orbit < Inhabited->Planets.Num(); ++Orbit)
            {
                if (!Inhabited->Planets[Orbit].GivenName.IsEmpty())
                {
                    const FString Row = Map->GetRowText(Orbit).ToString();
                    TestTrue(FString::Printf(TEXT("the inhabited world's row starts with its dot's numeral: %s"), *Row),
                             Row.StartsWith(SystemNames::RomanNumeral(Orbit + 1) + NavText::Separator));
                    TestTrue(TEXT("and names it"), Row.Contains(Inhabited->Planets[Orbit].GivenName));
                }
            }
        }
        if (TestTrue(TEXT("a system with no worlds near home (or this proves nothing)"), Empty.IsSet()))
        {
            Ship->PlaceShip(Empty->Stub.Position + FVector(0.0, 0.5 * UniverseUnits::CmPerAU, 0.0), Facing());
            Look();
            TestTrue(TEXT("0.5 AU from an empty system's star is in it"), Universe->GetSystemIdAt(Where()) == TOptional<FSystemId>(Empty->Stub.Id));
            TestEqual(TEXT("a system with no worlds says so"), Map->GetFooterText().ToString(),
                      FString::Printf(TEXT("Nothing orbits %s."), *Empty->Stub.Name));
            TestEqual(TEXT("with no rows"), Map->GetShownRowCount(), 0);
            TestTrue(TEXT("but draws its star"), Map->GetView()->HasDrawing());
            TestEqual(TEXT("and is titled for it"), Map->GetTitleText().ToString(), NavText::Place(Empty->Stub.Name, Empty->Star.Class));
        }
    }

    Ship->PlaceShip(Home->Stub.Position + FVector(0.0, 3.0 * UniverseUnits::CmPerAU, 0.0), Facing());
    Look();
    RowsAre(*this, *Map, *Home, Where(), TEXT("back home"));

    // -- the priors reloaded in play ------------------------------------------
    {
        Asked = Map->GetLayoutAsked();
        UProcGenPriorsConfig* Priors = GetMutableDefault<UProcGenPriorsConfig>();
        {
            // Red dwarfs as rare as blue-white stars: home is re-rolled.
            FScopedPrior Rarer(Priors->ClassWeightM, Priors->ClassWeightB);
            Look();
            TestEqual(TEXT("reloaded priors are drawn on the next frame"), Map->GetLayoutAsked(), Asked + 1);
            if (const TOptional<FStarSystem> Rerolled = Universe->GetSystemAt(Where()))
            {
                RowsAre(*this, *Map, *Rerolled, Where(), TEXT("the priors reloaded"));
            }
            else
            {
                TestEqual(TEXT("the priors reloaded out of any system: nothing is drawn"), Map->GetShownRowCount(), 0);
            }
        }
        Look();
        TestEqual(TEXT("and put back, drawn again"), Map->GetLayoutAsked(), Asked + 2);
        RowsAre(*this, *Map, *Home, Where(), TEXT("the priors put back"));
    }

    // -- between stars: placed there ----------------------------------------
    if (Chart.Num() > 0)
    {
        const FUniversePosition Midway = Home->Stub.Position + (Chart[0].Position - Home->Stub.Position) * 0.5;
        if (!Universe->GetSystemIdAt(Midway))
        {
            Ship->PlaceShip(Midway, Facing());
            Look();
            TestEqual(TEXT("between stars the map says so"), Map->GetFooterText().ToString(), FString(TEXT("Between stars.")));
            TestEqual(TEXT("with no rows"), Map->GetShownRowCount(), 0);
            TestFalse(TEXT("and draws nothing"), Map->GetView()->HasDrawing());
            TestTrue(TEXT("and has no title"), Map->GetTitleText().IsEmpty());
            Ship->ClearTarget();
            Map->SelectWorld(0);
            TestFalse(TEXT("and a press there marks nothing"), Held().IsSet());
            Ship->PlaceShip(Home->Stub.Position + FVector(0.0, 3.0 * UniverseUnits::CmPerAU, 0.0), Facing());
            Look();
        }
    }

    // -- an in-system fold: the system stays, and the map says so ------------
    {
        FScopedCVar QuickCharge(TEXT("ds.Nav.ChargeSeconds"), 0.5f);
        const int32 Outermost = Home->Planets.Num() - 1;
        TestTrue(TEXT("the outermost world is targeted"), Ship->SetTarget(FBodyId{Home->Stub.Id, Outermost, -1}));
        Look();
        Map->PressJumpButton();
        TestTrue(TEXT("Jump here, pressed"), Ship->IsJumpEngaged() && Ship->GetPlottedWorld().IsSet());
        const FVector Toward = (Home->PlanetPosition(Outermost) - Where()).GetSafeNormal();
        Ship->PlaceShip(Where(), FRotationMatrix::MakeFromX(Toward).ToQuat());
        for (int32 Tick = 0; Tick < 100 && !Ship->IsInTransit(); ++Tick)
        {
            Ship->Tick(0.1f);
        }
        if (TestTrue(TEXT("the in-system fold opens"), Ship->IsInTransit()))
        {
            Asked = Map->GetLayoutAsked();
            Look();
            TestEqual(TEXT("an in-system fold asks for no drawing"), Map->GetLayoutAsked(), Asked);
            TestEqual(TEXT("it says it is in the fold"), Map->GetFooterText().ToString(), FString(TEXT("In the fold.")));
            RowsAre(*this, *Map, *Home, Where(), TEXT("in the fold"), Outermost);
            TestTrue(TEXT("and still draws the system"), Map->GetView()->HasDrawing());
            TestTrue(TEXT("the target line is empty in the fold"), Map->GetTargetText().IsEmpty());
            TestTrue(TEXT("the target's row is still marked"), Map->GetRowText(Outermost).ToString().StartsWith(UNavigationWidget::PlottedMark));
            TestEqual(TEXT("the button still reads Stand down"), Map->GetJumpButtonText().ToString(), FString(TEXT("Stand down")));
            TestFalse(TEXT("and cannot be pressed in the fold"), Map->IsJumpButtonEnabled());
        }
        for (int32 Tick = 0; Tick < 200 && Ship->IsInTransit(); ++Tick)
        {
            Ship->Tick(0.1f);
        }
        Look();
        TestFalse(TEXT("the in-system jump arrives"), Ship->IsInTransit());
        TestTrue(TEXT("with the world still marked"), Map->GetRowText(Outermost).ToString().StartsWith(UNavigationWidget::PlottedMark));
        TestTrue(TEXT("and the line naming it"), Map->GetTargetText().ToString().Contains(NavText::WorldName(Home->Planets[Outermost])));
        TestEqual(TEXT("which can be jumped to again only once flown away from"), Map->GetJumpButtonText().ToString(), FString(TEXT("Near enough to fly")));
        Ship->ClearTarget();
    }

    // -- between stars: in the fold -------------------------------------------
    {
        FScopedCVar QuickCharge(TEXT("ds.Nav.ChargeSeconds"), 0.5f);
        const FStarSystemStub Destination = Chart[0];
        TestTrue(TEXT("a course"), Ship->PlotCourse(Destination.Id));
        TestTrue(TEXT("engaged"), Ship->SetJumpEngaged(true));
        const FVector Toward = (Destination.Position - Where()).GetSafeNormal();
        Ship->PlaceShip(Where(), FRotationMatrix::MakeFromX(Toward).ToQuat());
        for (int32 Tick = 0; Tick < 100 && !Ship->IsInTransit(); ++Tick)
        {
            Ship->Tick(0.1f);
        }
        if (TestTrue(TEXT("the fold opens"), Ship->IsInTransit()))
        {
            Asked = Map->GetLayoutAsked();
            Look();
            TestEqual(TEXT("the fold is drawn once"), Map->GetLayoutAsked(), Asked + 1);
            TestEqual(TEXT("in a star jump's fold the map says between stars"), Map->GetFooterText().ToString(), FString(TEXT("Between stars.")));
            TestEqual(TEXT("with no rows"), Map->GetShownRowCount(), 0);
            TestFalse(TEXT("and draws nothing"), Map->GetView()->HasDrawing());
            TestFalse(TEXT("and has no button"), Map->IsJumpButtonShown());
        }
        for (int32 Tick = 0; Tick < 200 && Ship->IsInTransit(); ++Tick)
        {
            Ship->Tick(0.1f);
        }
        Look();
        const TOptional<FStarSystem> There = Universe->GetSystemAt(Where());
        if (TestTrue(TEXT("the jump arrives"), !Ship->IsInTransit() && There.IsSet()))
        {
            RowsAre(*this, *Map, *There, Where(), TEXT("on arrival"));
            TestTrue(TEXT("and the arrival is on the map"), !Map->GetFooterText().ToString().Contains(TEXT("Beyond")));
        }
    }
    return true;
}

/**
 * The map's actor: a screen you read and never sit at, drawn at the size the
 * helm sees it, with the map as its widget -- spawned before play, because a
 * widget component builds its widget and its collision body in BeginPlay.
 * (Its seat virtuals are stage 4's, once AShipScreen has them.)
 */
bool FShipMapScreenTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace SystemMapScreenTestLocal;

    FSkyWorld Test(TEXT("ShipMapScreenWorld"));
    AShipMapScreen* Screen = Test.World ? Test.World->SpawnActor<AShipMapScreen>(MapGlass, FRotator::ZeroRotator) : nullptr;
    AShipNavScreen* Chart = Test.World ? Test.World->SpawnActor<AShipNavScreen>(FVector(1711.0, 85.0, 105.0), FRotator::ZeroRotator) : nullptr;
    if (!TestNotNull(TEXT("the map spawns before play"), Screen) || !TestNotNull(TEXT("and the chart beside it"), Chart))
    {
        return false;
    }
    Test.BeginPlay();

    TestFalse(TEXT("the map is not usable: E sits nobody down at it"), Screen->IsUsable());
    TestTrue(TEXT("while the chart is"), Chart->IsUsable());

    UWidgetComponent* Panel = Screen->GetScreen();
    TestTrue(TEXT("its draw size is 600 x 424, what the helm sees of it"), Panel->GetDrawSize() == FIntPoint(600, 424));
    TestTrue(TEXT("its widget class is the map"), Panel->GetWidgetClass() == USystemMapWidget::StaticClass());
    TestTrue(TEXT("and the widget it made is one"), Cast<USystemMapWidget>(Panel->GetUserWidgetObject()) != nullptr);

    // 68 cm across, so a centimetre of bezel inside the desk screen's 70 cm.
    const double WidthCm = Panel->GetComponentScale().X * 600.0;
    TestTrue(TEXT("its glass is 68 cm across"), FMath::IsNearlyEqual(WidthCm, 68.0, 1.0e-3));
    const FVector2D Framed = Screen->GetFramedSizeCm();
    TestTrue(TEXT("framed with its bezel, the desk screen's 70 x 50 cm face"),
             FMath::IsNearlyEqual(Framed.X, 70.0, 1.0e-3) && FMath::IsNearlyEqual(Framed.Y, 68.0 * 424.0 / 600.0 + 2.0, 1.0e-3));
    TestEqual(TEXT("seen at the chart's distance when framed"), Screen->GetViewDistanceCm(), 60.0f);

    // Spawned before play, its glass takes a trace from the helm's eye: the
    // body the pointer needs exists, in the right place, facing aft.
    FHitResult Hit;
    FCollisionQueryParams Params(TEXT("MapFromHelm"), false);
    const bool bHit = Test.World->LineTraceSingleByChannel(Hit, PilotEye, MapGlass + (MapGlass - PilotEye).GetSafeNormal() * 5.0,
                                                           ECC_Visibility, Params);
    TestTrue(TEXT("a trace from the helm's eye lands on the map's glass"), bHit && Hit.GetComponent() == Panel);
    TestTrue(TEXT("its face looks aft, toward the chairs"), Panel->GetComponentTransform().GetUnitAxis(EAxis::X).X < -0.99);

    // What a seat may do with each screen (decisions 2 and 13), a class
    // decision each, asked of the classes the level places.
    TestTrue(TEXT("the map is drivable seated: the helm looks and clicks"), Screen->IsDrivableSeated());
    TestTrue(TEXT("and zoomable from the chart chair"), Screen->IsZoomableFromChartChair());
    TestEqual(TEXT("where E calls it the map"), Screen->GetZoomPrompt().ToString(), FString(TEXT("Map")));
    TestTrue(TEXT("the chart is drivable seated too: the helm looks and clicks it (ruling, 2026-09-27)"),
             Chart->IsDrivableSeated());
    TestTrue(TEXT("it is zoomable from its chair"), Chart->IsZoomableFromChartChair());
    TestFalse(TEXT("and sitting at it no longer zooms it"), Chart->ZoomsOnSit());
    TestEqual(TEXT("where E calls it the chart"), Chart->GetZoomPrompt().ToString(), FString(TEXT("Chart")));
    const AShipScreen* Laptop = GetDefault<AShipLaptop>();
    TestTrue(TEXT("the laptop still zooms as you sit"), Laptop->ZoomsOnSit());
    TestFalse(TEXT("and is neither drivable seated"), Laptop->IsDrivableSeated());
    TestFalse(TEXT("nor zoomable from the chart chair"), Laptop->IsZoomableFromChartChair());
    // That the engineering console, a world screen but no ship screen, is
    // neither is behaviour, not a class fact: DeepSpace.Ship.ChartChair looks
    // at one from the chair.
    return true;
}

#endif
