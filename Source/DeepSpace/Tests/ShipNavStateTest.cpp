#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipNavState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipNavStateTest,
    "DeepSpace.Ship.NavState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    FSystemId MakeId(int64 X, int32 Slot)
    {
        FSystemId Id;
        Id.Sector = FInt64Vector(X, -3, 11);
        Id.Slot = Slot;
        return Id;
    }

    constexpr double Frame = 1.0 / 60.0;
    constexpr double Charged = 1.0;
    constexpr double Aligned = 0.0;

    /** Steps for Seconds with the inputs held, and counts what came back. */
    struct FRun
    {
        int32 Began = 0;
        int32 Arrived = 0;
    };

    FRun StepFor(FShipNavState& Nav, double Seconds, double Charge, double OffBoresight,
                 const FNavTuning& Tuning = FNavTuning())
    {
        FRun Run;
        const int32 Frames = FMath::RoundToInt32(Seconds / Frame);
        for (int32 Index = 0; Index < Frames; ++Index)
        {
            switch (Nav.Step(Frame, Charge, OffBoresight, Tuning))
            {
            case ENavEvent::TransitBegan: ++Run.Began; break;
            case ENavEvent::Arrived:      ++Run.Arrived; break;
            default: break;
            }
        }
        return Run;
    }
}

bool FShipNavStateTest::RunTest(const FString& Parameters)
{
    const FSystemId Kessa = MakeId(4, 2);
    const FNavTuning Tuning;

    // With no course there is nothing to engage, and nothing ever fires.
    {
        FShipNavState Nav;
        TestFalse(TEXT("engaging with no course is refused"), Nav.SetEngaged(true));
        TestFalse(TEXT("and leaves the jump idle"), Nav.IsEngaged());
        TestEqual(TEXT("an idle jump is Idle"), Nav.GetJumpState(Charged), EJumpState::Idle);
        TestEqual(TEXT("engaging with no course never transits"),
                  StepFor(Nav, 60.0, Charged, Aligned).Began, 0);
    }

    // Plotted and aligned, but not engaged: the course alone is not "go".
    {
        FShipNavState Nav;
        Nav.Plot(Kessa);
        TestEqual(TEXT("a plotted course without engage never transits"),
                  StepFor(Nav, 60.0, Charged, Aligned).Began, 0);
        TestEqual(TEXT("and is Idle, charged or not"), Nav.GetJumpState(Charged), EJumpState::Idle);
    }

    // An uncharged drive never transits, and says it is winding.
    {
        FShipNavState Nav;
        Nav.Plot(Kessa);
        TestTrue(TEXT("engaging with a course is accepted"), Nav.SetEngaged(true));
        TestEqual(TEXT("an engaged, uncharged jump is Winding"), Nav.GetJumpState(0.4), EJumpState::Winding);
        TestEqual(TEXT("an uncharged drive never transits"),
                  StepFor(Nav, 60.0, 0.999, Aligned).Began, 0);
    }

    // The word and the fire decision change at the same charge, full: a
    // screen that said JUMP READY while the fold refused to open would be
    // the word and the state machine disagreeing about the one thing the
    // player is waiting for.
    {
        FShipNavState Nav;
        Nav.Plot(Kessa);
        Nav.SetEngaged(true);
        const double NearlyFull = 1.0 - 1e-9;
        TestEqual(TEXT("a hair below full is still Winding"), Nav.GetJumpState(NearlyFull), EJumpState::Winding);
        TestEqual(TEXT("and does not fire, aligned"), Nav.Step(Frame, NearlyFull, Aligned, Tuning), ENavEvent::None);
        TestEqual(TEXT("full is Ready"), Nav.GetJumpState(Charged), EJumpState::Ready);
        TestEqual(TEXT("and fires on that step"), Nav.Step(Frame, Charged, Aligned, Tuning), ENavEvent::TransitBegan);
    }

    // Charged and misaligned, it holds at ready for as long as it takes.
    // Nothing escalates: ten minutes on, it is exactly where it was.
    {
        FShipNavState Nav;
        Nav.Plot(Kessa);
        Nav.SetEngaged(true);
        const double Off = Tuning.ConeRadians * 1.5;
        TestEqual(TEXT("misaligned at full charge holds indefinitely"),
                  StepFor(Nav, 600.0, Charged, Off).Began, 0);
        TestEqual(TEXT("holding at Ready"), Nav.GetJumpState(Charged), EJumpState::Ready);
        TestTrue(TEXT("still engaged"), Nav.IsEngaged());
        TestTrue(TEXT("still plotted"), Nav.GetPlotted().IsSet());
        TestEqual(TEXT("and no arrival has been invented"), Nav.GetJumpSerial(), 0);
    }

    // Aligned, charged and engaged: the fold opens on that very step, by
    // itself, and the transit runs its length and arrives.
    {
        FShipNavState Nav;
        Nav.Plot(Kessa);
        Nav.SetEngaged(true);
        TestEqual(TEXT("still holding at the cone's edge plus a hair"),
                  Nav.Step(Frame, Charged, Tuning.ConeRadians + 1e-6, Tuning), ENavEvent::None);
        TestEqual(TEXT("aligned, charged and engaged gives TransitBegan on that step"),
                  Nav.Step(Frame, Charged, Tuning.ConeRadians, Tuning), ENavEvent::TransitBegan);
        TestTrue(TEXT("in transit"), Nav.IsInTransit());
        TestEqual(TEXT("which is Transit whatever the charge"), Nav.GetJumpState(0.0), EJumpState::Transit);

        // Refused while between stars: the fold is already open.
        TestFalse(TEXT("plot is refused in transit"), Nav.Plot(MakeId(9, 0)));
        TestFalse(TEXT("stand-down is refused in transit"), Nav.SetEngaged(false));
        Nav.ClearPlot();
        TestTrue(TEXT("and the course cannot be cleared mid-fold"),
                 Nav.GetPlotted().IsSet() && Nav.GetPlotted().GetValue() == Kessa);

        FRun Half = StepFor(Nav, Tuning.TransitSeconds * 0.5, 0.0, UE_DOUBLE_PI);
        TestEqual(TEXT("no arrival halfway"), Half.Arrived, 0);
        TestTrue(TEXT("progress is about a half"), FMath::IsNearlyEqual(Nav.GetTransitProgress(), 0.5, 0.01));

        // Alignment and charge do not matter once the fold is open.
        FRun Rest = StepFor(Nav, Tuning.TransitSeconds * 0.5 + Frame, 0.0, UE_DOUBLE_PI);
        TestEqual(TEXT("Arrived comes once, after TransitSeconds"), Rest.Arrived, 1);
        TestEqual(TEXT("and no second fold opens"), Rest.Began, 0);
        TestFalse(TEXT("out of transit"), Nav.IsInTransit());
        TestTrue(TEXT("LastArrival is the course"),
                 Nav.GetLastArrival().IsSet() && Nav.GetLastArrival().GetValue() == Kessa);
        TestTrue(TEXT("the destination is visited"), Nav.HasVisited(Kessa));
        TestFalse(TEXT("the course is cleared: you are there"), Nav.GetPlotted().IsSet());
        TestFalse(TEXT("engage was a one-shot and returns to off"), Nav.IsEngaged());
        TestEqual(TEXT("the serial is +1"), Nav.GetJumpSerial(), 1);
        TestEqual(TEXT("progress reads 0 outside transit"), Nav.GetTransitProgress(), 0.0);
        TestEqual(TEXT("and the jump is Idle again"), Nav.GetJumpState(0.0), EJumpState::Idle);
    }

    // A changed tuning is honoured on the next step, never cached.
    {
        FShipNavState Nav;
        Nav.Plot(Kessa);
        Nav.SetEngaged(true);
        const double Off = FMath::DegreesToRadians(15.0);
        TestEqual(TEXT("15 degrees off is outside the default cone"),
                  Nav.Step(Frame, Charged, Off, Tuning), ENavEvent::None);

        FNavTuning Wide;
        Wide.ConeRadians = FMath::DegreesToRadians(20.0);
        Wide.TransitSeconds = 1.0;
        TestEqual(TEXT("a wider cone fires on the next step"), Nav.Step(Frame, Charged, Off, Wide),
                  ENavEvent::TransitBegan);
        TestEqual(TEXT("and a shorter transit arrives on its own schedule"),
                  StepFor(Nav, 1.0 + Frame, 0.0, 0.0, Wide).Arrived, 1);
    }

    // Standing down and clearing: levers, each where it is left.
    {
        FShipNavState Nav;
        Nav.Plot(Kessa);
        Nav.SetEngaged(true);
        TestTrue(TEXT("standing down is allowed"), Nav.SetEngaged(false));
        TestTrue(TEXT("and leaves the course plotted"), Nav.GetPlotted().IsSet());

        Nav.SetEngaged(true);
        Nav.ClearPlot();
        TestFalse(TEXT("clearing the course stands the jump down"), Nav.IsEngaged());
        TestFalse(TEXT("and clears it"), Nav.GetPlotted().IsSet());

        TestTrue(TEXT("replotting while engaged is allowed"), Nav.Plot(Kessa) && Nav.SetEngaged(true) && Nav.Plot(MakeId(5, 1)));
        TestTrue(TEXT("and changes the course"), Nav.GetPlotted().GetValue() == MakeId(5, 1));
    }

    // The visited set is a set, and the start system can be marked by hand.
    {
        FShipNavState Nav;
        TestFalse(TEXT("nothing is visited at first"), Nav.HasVisited(Kessa));
        Nav.MarkVisited(Kessa);
        Nav.MarkVisited(Kessa);
        TestTrue(TEXT("marked"), Nav.HasVisited(Kessa));
        TestFalse(TEXT("and only that one"), Nav.HasVisited(MakeId(4, 3)));
    }

    // ShipNav::OffBoresight is the one alignment test.
    {
        TestEqual(TEXT("the nose is 0 off"), ShipNav::OffBoresight(FVector::ForwardVector), 0.0);
        TestTrue(TEXT("abeam is a right angle"),
                 FMath::IsNearlyEqual(ShipNav::OffBoresight(FVector(0.0, -3.0, 0.0)), UE_DOUBLE_HALF_PI, 1e-12));
        TestTrue(TEXT("astern is pi"),
                 FMath::IsNearlyEqual(ShipNav::OffBoresight(-FVector::ForwardVector), UE_DOUBLE_PI, 1e-12));
        TestEqual(TEXT("no direction is never aligned"), ShipNav::OffBoresight(FVector::ZeroVector), UE_DOUBLE_PI);
    }

    // Decision 3's claim, flown: at cruise limits, a full-rate turn released
    // the moment the course enters the cone stops inside it. Otherwise
    // "let go when it says dead ahead" becomes hunting for alignment.
    {
        const FShipFlightLimits Cruise = FShipFlightLimits::Cruise();
        int32 NoseAxes = 0;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            FVector Rate = FVector::ZeroVector;
            Rate[Axis] = 1.0;
            const FVector Course = FQuat(Rate, FMath::DegreesToRadians(40.0)).GetForwardVector();
            if (ShipNav::OffBoresight(Course) < UE_KINDA_SMALL_NUMBER)
            {
                continue;   // the axis through the nose only rolls it
            }
            ++NoseAxes;

            const double Overshoot = FMath::Square(Cruise.MaxAngularRate[Axis])
                / (2.0 * Cruise.AngularAcceleration[Axis]);
            TestTrue(TEXT("the release overshoot is inside the cone's full width"),
                     Overshoot < 2.0 * Tuning.ConeRadians);

            FShipFlightState Flight;
            FShipFlightCommand Turn;
            Turn.AttitudeRate = Rate;
            Flight.SetCommand(Turn);

            bool bReleased = false;
            for (int32 Index = 0; Index < 60 * 60; ++Index)
            {
                const FVector Local = Flight.GetUniverseOrientation().UnrotateVector(Course);
                if (!bReleased && ShipNav::OffBoresight(Local) <= Tuning.ConeRadians)
                {
                    Flight.ReleaseAttitude();
                    bReleased = true;
                }
                Flight.Step(Frame);
            }
            const FVector Local = Flight.GetUniverseOrientation().UnrotateVector(Course);
            TestTrue(TEXT("the turn reached the cone"), bReleased);
            TestTrue(TEXT("the ship has stopped turning"), Flight.GetAngularVelocity().IsNearlyZero(1e-9));
            TestTrue(TEXT("and stopped inside the cone"), ShipNav::OffBoresight(Local) <= Tuning.ConeRadians);
        }
        TestEqual(TEXT("two axes swing the nose"), NoseAxes, 2);
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
