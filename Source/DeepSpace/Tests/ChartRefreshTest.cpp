#include "Components/WidgetComponent.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipNavScreen.h"
#include "Ship/ShipNavState.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FChartAsksOnChangeTest,
    "DeepSpace.UI.ChartAsksOnChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ChartAsksOnChangeLocal
{
    /** What row Index must read if the chart is showing the ship's answer
     *  now: GetRowText's own shape, built from the ship and NavText. */
    FString ExpectedRow(const UShipSubsystem& Ship, const FStarSystemStub& Stub)
    {
        const bool bPlotted = Ship.GetPlottedSystem() && *Ship.GetPlottedSystem() == Stub.Id;
        FString Row = bPlotted ? FString(UNavigationWidget::PlottedMark) + TEXT(" ") : FString();
        Row += Stub.Name + NavText::Separator
            + NavText::Distance(Ship.GetFlightState().GetUniversePosition().DistanceTo(Stub.Position))
            + NavText::Separator + NavText::StarClass(Stub.Class);
        if (Ship.HasVisited(Stub.Id))
        {
            Row += NavText::Separator + NavText::Visited(true);
        }
        return Row;
    }

    /** Every row is the ship's answer now, and there are as many as it has,
     *  up to six. */
    bool RowsAreTheShips(FAutomationTestBase& Test, const UNavigationWidget& Chart, const UShipSubsystem& Ship,
                         const TCHAR* When)
    {
        const TArray<FStarSystemStub> Chartered = Ship.GetChart();
        const int32 Want = FMath::Min(Chartered.Num(), UNavigationWidget::RowCount);
        bool bAll = Test.TestEqual(FString::Printf(TEXT("%s: as many rows as the ship charts"), When),
                                   Chart.GetShownRowCount(), Want);
        for (int32 Index = 0; Index < Want; ++Index)
        {
            bAll &= Test.TestEqual(FString::Printf(TEXT("%s: row %d"), When, Index),
                                   Chart.GetRowText(Index).ToString(), ExpectedRow(Ship, Chartered[Index]));
        }
        return bAll;
    }

    TArray<FString> RowTexts(const UNavigationWidget& Chart)
    {
        TArray<FString> Rows;
        for (int32 Index = 0; Index < UNavigationWidget::RowCount; ++Index)
        {
            Rows.Add(Chart.GetRowText(Index).ToString());
        }
        return Rows;
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
 * The chart is in view from the helm, and what it shows costs sector scans
 * and generated systems. It asks for them again only when something they
 * depend on has changed -- and when anything has, what it shows is the
 * ship's answer on the very next frame, exactly as though it had asked.
 *
 * Checked through the real chart: the screen actor spawned before play, and
 * the widget its widget component made, refreshed as its tick refreshes it.
 * Each thing the answer depends on is changed on its own, and each time the
 * rows must be the ship's answer; with nothing changed, frame after frame,
 * nothing is asked at all.
 */
bool FChartAsksOnChangeTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ChartAsksOnChangeLocal;

    FSkyWorld Test(TEXT("ChartAsksOnChangeWorld"));
    AShipNavScreen* Screen = Test.World ? Test.World->SpawnActor<AShipNavScreen>(FVector(300.0, 0.0, 105.0), FRotator::ZeroRotator) : nullptr;
    if (!TestNotNull(TEXT("the chart spawns"), Screen) || !TestNotNull(TEXT("the world has a ship"), Test.Ship))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;

    UWidgetComponent* Panel = Screen->GetScreen();
    UNavigationWidget* Chart = Cast<UNavigationWidget>(Panel->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the chart's widget component made the chart's widget"), Chart))
    {
        return false;
    }

    // The chart's part of a frame, as the game runs it: the panel's component
    // ticks in TG_DuringPhysics, which is where it takes its widget and the
    // widget builds its tree, and the chart refreshes as its Slate tick
    // would. Headless nothing paints, so the refresh is called directly.
    const auto Look = [Panel, Chart]()
    {
        Panel->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
        Chart->RefreshFromShip();
    };
    Look();
    if (!RowsAreTheShips(*this, *Chart, *Ship, TEXT("at the start")) || Ship->GetChart().Num() < 2)
    {
        AddError(TEXT("the start needs at least two systems in reach for this test to mean anything"));
        return false;
    }

    // -- nothing changes: nothing is asked -----------------------------------
    int32 Systems = Chart->GetSystemsAsked();
    int32 Course = Chart->GetCourseAsked();
    const FString Here = Chart->GetHereText().ToString();
    for (int32 Frame = 0; Frame < 60; ++Frame)
    {
        Look();
        Test.Step(1.0f / 60.0f);
    }
    TestEqual(TEXT("a second at the helm with nothing changing asks for no systems"), Chart->GetSystemsAsked(), Systems);
    TestEqual(TEXT("and no course"), Chart->GetCourseAsked(), Course);
    TestEqual(TEXT("and here still reads the same"), Chart->GetHereText().ToString(), Here);

    // -- a course plotted elsewhere ------------------------------------------
    const FStarSystemStub Destination = Ship->GetChart()[1];
    TestTrue(TEXT("a course plotted from outside the chart"), Ship->PlotCourse(Destination.Id));
    Look();
    TestTrue(TEXT("is asked for on the next frame"), Chart->GetSystemsAsked() > Systems);
    RowsAreTheShips(*this, *Chart, *Ship, TEXT("plotted"));
    const TOptional<FVector> Bearing = Ship->GetCourseDirectionShipLocal();
    TestEqual(TEXT("and its course reads as the ship's"), Chart->GetCourseText().ToString(),
              NavText::Course(Destination.Name, Bearing, Ship->GetJumpConeRadians()) + TEXT("."));

    // -- the ship turns: the bearing changes, the systems do not ------------
    Systems = Chart->GetSystemsAsked();
    Course = Chart->GetCourseAsked();
    const FString Before = Chart->GetCourseText().ToString();
    const FUniversePosition Parked = Ship->GetFlightState().GetUniversePosition();
    const FQuat Turned = FQuat(FVector::UpVector, FMath::DegreesToRadians(25.0)) * Ship->GetFlightState().GetUniverseOrientation();
    Ship->PlaceShip(Parked, Turned);
    Look();
    TestEqual(TEXT("turning asks for no systems"), Chart->GetSystemsAsked(), Systems);
    TestTrue(TEXT("but asks for the course"), Chart->GetCourseAsked() > Course);
    TestEqual(TEXT("which reads the new bearing"), Chart->GetCourseText().ToString(),
              NavText::Course(Destination.Name, Ship->GetCourseDirectionShipLocal(), Ship->GetJumpConeRadians()) + TEXT("."));
    TestNotEqual(TEXT("and not the old one"), Chart->GetCourseText().ToString(), Before);

    // -- ds.Nav.ConeDeg tuned in play: the same bearing, a different word ---
    // The course a dozen degrees off the nose reads in degrees under the
    // design's cone and as dead ahead under a cone wide enough to hold it,
    // with the ship not turned at all: only the cone can have changed it.
    {
        const FVector Toward = Ship->GetCourseDirection().Get(FVector::ForwardVector);
        const FVector Nose = FQuat(FVector::UpVector, FMath::DegreesToRadians(12.0)) * Toward;
        const FQuat Off = FRotationMatrix::MakeFromX(Nose).ToQuat();
        Ship->PlaceShip(Parked, Off);
        Look();
        const FString Narrow = Chart->GetCourseText().ToString();
        {
            FScopedCVar Wider(TEXT("ds.Nav.ConeDeg"), 20.0f);
            Course = Chart->GetCourseAsked();
            Look();
            TestTrue(TEXT("a wider cone asks for the course"), Chart->GetCourseAsked() > Course);
            TestEqual(TEXT("which reads the ship's answer under the wider cone"), Chart->GetCourseText().ToString(),
                      NavText::Course(Destination.Name, Ship->GetCourseDirectionShipLocal(), Ship->GetJumpConeRadians()) + TEXT("."));
            TestNotEqual(TEXT("and not what it read under the narrower one"), Chart->GetCourseText().ToString(), Narrow);
        }
        Look();
        TestEqual(TEXT("the cone put back, the course reads as it did"), Chart->GetCourseText().ToString(), Narrow);
        Ship->PlaceShip(Parked, Turned);
        Look();
    }

    // -- a small move: a cruise's worth, nothing the rows can show -----------
    Systems = Chart->GetSystemsAsked();
    Ship->PlaceShip(Parked + FVector(1.0e5 * UniverseUnits::CmPerKm, 0.0, 0.0), Turned);
    Look();
    TestEqual(TEXT("a hundred thousand km asks for no systems"), Chart->GetSystemsAsked(), Systems);
    RowsAreTheShips(*this, *Chart, *Ship, TEXT("moved a little"));

    // -- a walk in small steps: the rows never fall behind a displayed tenth
    // Four thousandths of a light year a step, a tenth of a light year and
    // more in all: some row's distance crosses a tenth on the way, and on
    // every step the rows must read what the ship answers -- so the chart
    // may let the ship move no more than a step's worth before asking.
    {
        const FVector Toward = (Destination.Position - Parked).GetSafeNormal();
        const TArray<FString> Setting = RowTexts(*Chart);
        bool bKept = true;
        for (int32 Step = 1; Step <= 30 && bKept; ++Step)
        {
            Ship->PlaceShip(Parked + Toward * (4.0e-3 * Step) * UniverseUnits::CmPerLightYear, Turned);
            Look();
            bKept = RowsAreTheShips(*this, *Chart, *Ship, *FString::Printf(TEXT("walked %d steps"), Step));
        }
        TestTrue(TEXT("and some row read differently by the end of the walk"), RowTexts(*Chart) != Setting);
        Ship->PlaceShip(Parked, Turned);
        Look();
        RowsAreTheShips(*this, *Chart, *Ship, TEXT("walked back"));
        Systems = Chart->GetSystemsAsked();
    }

    // -- a large move, still in the system: the drive's reach ---------------
    const TArray<FString> Near = RowTexts(*Chart);
    const FVector Away = (Destination.Position - Parked).GetSafeNormal();
    Ship->PlaceShip(Parked + Away * 0.2 * UniverseUnits::CmPerLightYear, Turned);
    Look();
    TestTrue(TEXT("a fifth of a light year is asked for"), Chart->GetSystemsAsked() > Systems);
    RowsAreTheShips(*this, *Chart, *Ship, TEXT("moved a fifth of a light year"));
    TestTrue(TEXT("and the rows read differently for it"), RowTexts(*Chart) != Near);
    Ship->PlaceShip(Parked, Turned);
    Look();
    RowsAreTheShips(*this, *Chart, *Ship, TEXT("back"));

    // -- ds.Nav.RangeLy tuned in play ----------------------------------------
    {
        const int32 Shown = Chart->GetShownRowCount();
        float Range = 1.0f;
        for (; Range < 12.0f; Range += 0.5f)
        {
            FScopedCVar Probe(TEXT("ds.Nav.RangeLy"), Range);
            if (FMath::Min(Ship->GetChart().Num(), UNavigationWidget::RowCount) != Shown)
            {
                break;
            }
        }
        FScopedCVar Shorter(TEXT("ds.Nav.RangeLy"), Range);
        Look();
        RowsAreTheShips(*this, *Chart, *Ship, *FString::Printf(TEXT("ds.Nav.RangeLy %.1f"), Range));
        TestNotEqual(TEXT("which shows a different number of rows"), Chart->GetShownRowCount(), Shown);
    }
    Look();
    RowsAreTheShips(*this, *Chart, *Ship, TEXT("ds.Nav.RangeLy put back"));

    // -- the priors reloaded in play -----------------------------------------
    {
        UProcGenPriorsConfig* Priors = GetMutableDefault<UProcGenPriorsConfig>();
        const TArray<FString> Rows = RowTexts(*Chart);
        const FString Place = Chart->GetHereText().ToString();
        // Red dwarfs as rare as blue-white stars: every M star in reach is
        // now another class, so every row that named one reads differently.
        FScopedPrior Rarer(Priors->ClassWeightM, Priors->ClassWeightB);
        Look();
        RowsAreTheShips(*this, *Chart, *Ship, TEXT("the priors reloaded"));
        TestTrue(TEXT("and the chart shows the new universe"),
                 RowTexts(*Chart) != Rows || Chart->GetHereText().ToString() != Place);
    }
    Look();
    RowsAreTheShips(*this, *Chart, *Ship, TEXT("the priors put back"));

    // -- the jump winds: its word every frame, no systems -------------------
    Systems = Chart->GetSystemsAsked();
    Ship->SetJumpEngaged(true);
    Test.Step(0.1f);
    Look();
    TestEqual(TEXT("the jump's word follows the ship"), Chart->GetJumpText().ToString(),
              NavText::JumpWord(Ship->GetJumpState()) + TEXT("."));
    TestEqual(TEXT("winding asks for no systems"), Chart->GetSystemsAsked(), Systems);
    return true;
}

#endif
