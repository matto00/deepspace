#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipNavScreen.h"
#include "Sky/LocalSystem.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"
#include "UI/SystemMapLayout.h"
#include "UI/SystemMapView.h"
#include "UI/SystemMapWidget.h"
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

    /** What row Orbit must read: the numeral (or a given name), the kind,
     *  and the surface distance in the altitude line's words. Built from
     *  procgen and NavText, not from the widget. */
    FString ExpectedRow(const FStarSystem& System, int32 Orbit, const FUniversePosition& Where)
    {
        const FPlanet& Planet = System.Planets[Orbit];
        const double Surface = FMath::Max(0.0, Where.DistanceTo(System.PlanetPosition(Orbit))
            - Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius);
        const FString Name = Planet.GivenName.IsEmpty() ? SystemNames::RomanNumeral(Orbit + 1) : Planet.GivenName;
        return Name + NavText::Separator + NavText::WorldKind(Planet.Kind) + NavText::Separator
            + UShipHUDWidget::AltitudeWords(Surface);
    }

    bool RowsAre(FAutomationTestBase& Test, const USystemMapWidget& Map, const FStarSystem& System,
                 const FUniversePosition& Where, const TCHAR* When)
    {
        bool bAll = Test.TestEqual(FString::Printf(TEXT("%s: one row per world"), When),
                                   Map.GetShownRowCount(), System.Planets.Num());
        for (int32 Orbit = 0; Orbit < System.Planets.Num(); ++Orbit)
        {
            bAll &= Test.TestEqual(FString::Printf(TEXT("%s: row %d"), When, Orbit),
                                   Map.GetRowText(Orbit).ToString(), ExpectedRow(System, Orbit, Where));
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
 * draws nothing. Both pick paths end at SelectWorld, which works out the
 * chart's rule for worlds. (The target itself, and what SelectWorld does
 * with it, are stage 3's.)
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
    const auto Look = [Panel, Map]()
    {
        Panel->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
        Map->RefreshFromShip();
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
        const SystemMap::FMapLayout Expected = SystemMap::Layout(*Home, SystemMap::Fit(*Home, NavStart::DefaultStandoffAU));
        const SystemMap::FMapLayout* Drawn = Map->GetLayout();
        bool bSame = Drawn && Drawn->Dots.Num() == Expected.Dots.Num() && Drawn->RingPx == Expected.RingPx;
        for (int32 Orbit = 0; bSame && Orbit < Expected.Dots.Num(); ++Orbit)
        {
            bSame &= Drawn->Dots[Orbit].Centre.Equals(Expected.Dots[Orbit].Centre, 1.0e-9);
        }
        TestTrue(TEXT("the orrery is the system's layout, fitted to the arrival standoff"), bSame);
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
        const SystemMap::FMapScale Scale = SystemMap::Fit(*Home, NavStart::DefaultStandoffAU);
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
        Ship->PlaceShip(Home->Stub.Position + FVector(Out, 0.0, Out * FMath::Tan(FMath::DegreesToRadians(4.0))), Facing());
        Look();
        TestTrue(TEXT("but not within 5 degrees of it"), Map->GetFooterText().IsEmpty());
        TestEqual(TEXT("none of it asked for a drawing"), Map->GetLayoutAsked(), Asked);
    }

    // -- both pick paths end at SelectWorld, which asks the chart's rule ------
    {
        TArray<SystemMap::FMapSelection> Selections;
        const FDelegateHandle Handle = Map->OnSelected.AddLambda(
            [&Selections](const SystemMap::FMapSelection& Selection) { Selections.Add(Selection); });

        Map->PressRow(1);
        const SystemMap::FMapLayout* Drawn = Map->GetLayout();
        if (Drawn)
        {
            Map->GetView()->ClickAt(Drawn->Dots[0].Centre + FVector2D(1.0, 1.0));
            Map->GetView()->ClickAt(Drawn->Pixels.Centre);
        }
        Map->SelectWorld(Home->Planets.Num());
        if (TestEqual(TEXT("a row and a dot each made one selection; the star and a missing orbit none"), Selections.Num(), 2))
        {
            TestTrue(TEXT("the row targets its world"), Selections[0].Action == SystemMap::EMapSelect::Target
                && Selections[0].Body == FBodyId{Home->Stub.Id, 1, -1});
            TestTrue(TEXT("the dot targets its world"), Selections[1].Action == SystemMap::EMapSelect::Target
                && Selections[1].Body == FBodyId{Home->Stub.Id, 0, -1});
        }
        Map->OnSelected.Remove(Handle);
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
        Asked = Map->GetLayoutAsked();
        Ship->PlaceShip(Other->Stub.Position + FVector(2.0 * UniverseUnits::CmPerAU, 0.0, 0.0), Facing());
        Look();
        TestEqual(TEXT("a PlaceShip into another system is drawn on the next frame, once"), Map->GetLayoutAsked(), Asked + 1);
        RowsAre(*this, *Map, *Other, Where(), TEXT("in the other system"));
        TestEqual(TEXT("and titled for it"), Map->GetTitleText().ToString(), NavText::Place(Other->Stub.Name, Other->Star.Class));
        TestTrue(TEXT("its selections are its own worlds"), SystemMap::Select(*Other, 0, {})->Body.System == Other->Stub.Id);
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
            Map->SelectWorld(0);
            Ship->PlaceShip(Home->Stub.Position + FVector(0.0, 3.0 * UniverseUnits::CmPerAU, 0.0), Facing());
            Look();
        }
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
    return true;
}

#endif
