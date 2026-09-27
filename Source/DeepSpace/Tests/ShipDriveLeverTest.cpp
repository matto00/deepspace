#include "Misc/AutomationTest.h"
#include <limits>
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDriveLeverTest,
    "DeepSpace.Ship.DriveLever",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    constexpr double Km = 1.0e5;                                  // cm/s per km/s
    constexpr double C = ShipDriveLever::LightCmPerSecond;
    constexpr int32 Top = 11;
    constexpr double Tenth = 0.1 * C;                            // the drive's top

    double RelativeError(double Actual, double Expected)
    {
        return FMath::Abs(Actual - Expected) / FMath::Max(FMath::Abs(Expected), UE_DOUBLE_SMALL_NUMBER);
    }

    /** Ease from From toward To for Seconds in frames of Frame seconds. */
    double EaseFor(double From, double To, double Seconds, double Frame, double Rate, double Thrust = 1.0)
    {
        double P = From;
        const int32 Frames = FMath::RoundToInt32(Seconds / Frame);
        for (int32 Index = 0; Index < Frames; ++Index)
        {
            P = ShipDriveLever::Ease(P, To, Frame, Rate, Thrust);
        }
        return P;
    }

    /** Seconds, in frames of Frame, for the ease from From to cover Share of
     *  the way to To; negative if it never does in a minute. */
    double SecondsToShare(double From, double To, double Share, double Frame, double Rate, double Thrust)
    {
        double P = From;
        for (int32 Index = 1; Index <= FMath::RoundToInt32(60.0 / Frame); ++Index)
        {
            P = ShipDriveLever::Ease(P, To, Frame, Rate, Thrust);
            if ((P - From) / (To - From) >= Share)
            {
                return Index * Frame;
            }
        }
        return -1.0;
    }

    /** Repeats a hold of Seconds produces at a frame rate. */
    int32 RepeatsFor(double Seconds, double Hz, double Rate)
    {
        ShipDriveLever::FNotchRepeat Repeat;
        int32 Total = 0;
        const int32 Frames = FMath::RoundToInt32(Seconds * Hz);
        for (int32 Index = 0; Index < Frames; ++Index)
        {
            Total += Repeat.Update(true, 1.0 / Hz, Rate);
        }
        return Total;
    }
}

bool FShipDriveLeverTest::RunTest(const FString& Parameters)
{
    using namespace ShipDriveLever;

    // The table is the 2026-09-27 ruling's, notch for notch: STOP, then
    // 1-2-5 in km/s from 20 to 20,000, then 0.1 c.
    {
        const double Expected[] = {
            0.0,
            20 * Km, 50 * Km, 100 * Km, 200 * Km, 500 * Km, 1000 * Km, 2000 * Km, 5000 * Km, 10000 * Km, 20000 * Km,
            0.1 * C,
        };
        static_assert(UE_ARRAY_COUNT(Expected) == Top + 1, "STOP and eleven notches");
        TestEqual(TEXT("eleven notches"), TableNotches(), Top);
        for (int32 Notch = 0; Notch <= Top; ++Notch)
        {
            TestEqual(FString::Printf(TEXT("notch %d is its label"), Notch), NotchSpeed(Notch), Expected[Notch]);
        }
        TestEqual(TEXT("STOP is rest"), NotchSpeed(0), 0.0);
        TestEqual(TEXT("the top is a tenth of light"), NotchSpeed(Top), Tenth);
        TestEqual(TEXT("the top is ds.Drive.Top's default"), NotchSpeed(Top), DefaultTopLight * C);
        TestEqual(TEXT("below STOP is STOP"), NotchSpeed(-3), 0.0);
        TestEqual(TEXT("above the top is the top"), NotchSpeed(40), Tenth);

        double Smallest = 10.0;
        double Largest = 0.0;
        for (int32 Notch = 1; Notch < Top; ++Notch)
        {
            const double Ratio = NotchSpeed(Notch + 1) / NotchSpeed(Notch);
            Smallest = FMath::Min(Smallest, Ratio);
            Largest = FMath::Max(Largest, Ratio);
        }
        // 20,000 km/s to 0.1 c is 1.499, the one step that is not x2 or x2.5.
        TestTrue(FString::Printf(TEXT("strictly increasing, every step x1.5 to x2.5 (%.4f to %.4f)"), Smallest, Largest),
                 Smallest > 1.49 && Largest <= 2.5 + 1e-12);
        // The handover: the drive begins exactly where cruise ends, so leaving
        // it spools down to the bottom notch and cruise holds it from there.
        TestEqual(TEXT("the bottom notch is cruise's top, 20 km/s"), NotchSpeed(1), FShipFlightLimits::Cruise().MaxSpeed);
        TestEqual(TEXT("which is 20 km/s"), FShipFlightLimits::Cruise().MaxSpeed, 20.0 * Km);
    }

    // SpeedAt and PositionOf are inverses, continuous and monotonic, linear
    // from STOP to the first notch and geometric between notches.
    {
        double Worst = 0.0;
        for (double V = 1.0; V <= Tenth; V *= 1.07)
        {
            Worst = FMath::Max(Worst, RelativeError(SpeedAt(PositionOf(V)), V));
        }
        TestTrue(FString::Printf(TEXT("SpeedAt(PositionOf(v)) == v to 1e-9 (worst %.2e)"), Worst), Worst < 1.0e-9);

        bool bOnNotches = true;
        for (int32 Notch = 0; Notch <= Top; ++Notch)
        {
            bOnNotches &= FMath::Abs(PositionOf(NotchSpeed(Notch)) - Notch) < 1.0e-9;
            bOnNotches &= RelativeError(SpeedAt(Notch), NotchSpeed(Notch)) < 1.0e-12 || Notch == 0;
        }
        TestTrue(TEXT("each notch's speed is at its whole position"), bOnNotches);

        bool bIncreasing = true;
        double Jump = 0.0;
        double Previous = SpeedAt(0.0);
        for (double P = 1.0e-4; P <= Top; P += 1.0e-4)
        {
            const double V = SpeedAt(P);
            bIncreasing &= V > Previous;
            if (P > 1.0 + 2.0e-4)
            {
                // Geometric: 1e-4 of a notch changes speed by at most x2.5^1e-4.
                Jump = FMath::Max(Jump, V / Previous);
            }
            Previous = V;
        }
        TestTrue(TEXT("SpeedAt is strictly increasing"), bIncreasing);
        TestTrue(FString::Printf(TEXT("and continuous: no step of 1e-4 notch jumps (worst x%.8f)"), Jump),
                 Jump <= FMath::Pow(2.5, 1.0e-4) * (1.0 + 1e-12));
        for (int32 Notch = 1; Notch <= Top; ++Notch)
        {
            TestTrue(FString::Printf(TEXT("continuous across notch %d"), Notch),
                     RelativeError(SpeedAt(Notch - 1.0e-9), NotchSpeed(Notch)) < 1.0e-8
                     && (Notch == Top || RelativeError(SpeedAt(Notch + 1.0e-9), NotchSpeed(Notch)) < 1.0e-8));
        }

        TestTrue(TEXT("linear from STOP: half way is 10 km/s"), RelativeError(SpeedAt(0.5), 10.0 * Km) < 1e-12);
        TestTrue(TEXT("and a quarter is 5 km/s"), RelativeError(SpeedAt(0.25), 5.0 * Km) < 1e-12);
        TestTrue(TEXT("geometric between notches: 1.5 is sqrt(20 x 50) km/s"),
                 RelativeError(SpeedAt(1.5), FMath::Sqrt(1000.0) * Km) < 1e-12);
        TestTrue(TEXT("and 10.5 is sqrt(20,000 km/s x 0.1 c)"),
                 RelativeError(SpeedAt(10.5), FMath::Sqrt(20000.0 * Km * Tenth)) < 1e-12);
        TestEqual(TEXT("rest is position 0"), PositionOf(0.0), 0.0);
        TestEqual(TEXT("below rest is position 0"), PositionOf(-5.0), 0.0);
        TestEqual(TEXT("and position 0 is rest"), SpeedAt(0.0), 0.0);
        TestEqual(TEXT("below STOP reads rest"), SpeedAt(-1.0), 0.0);
        TestEqual(TEXT("beyond the top reads the top"), SpeedAt(25.0), Tenth);
        TestEqual(TEXT("faster than the top is the top position"), PositionOf(3.0 * C), static_cast<double>(Top));
    }

    // NotchCount: 12 positions at 0.1 c; a lower top drops notches above
    // it; no top ever adds one.
    {
        TestEqual(TEXT("0.1 c: STOP and eleven notches"), NotchCount(Tenth), 12);
        TestEqual(TEXT("a shade under 0.1 c drops it"), NotchCount(0.0999 * C), 11);
        TestEqual(TEXT("0.07 c keeps 20,000 km/s"), NotchCount(0.07 * C), 11);
        TestEqual(TEXT("0.05 c keeps 10,000 km/s"), NotchCount(0.05 * C), 10);
        TestEqual(TEXT("1,000 km/s: STOP and six notches"), NotchCount(1000.0 * Km), 7);
        TestEqual(TEXT("60 km/s: STOP, 20 and 50"), NotchCount(60.0 * Km), 3);
        TestEqual(TEXT("1 c adds nothing"), NotchCount(C), 12);
        TestEqual(TEXT("nor does any top"), NotchCount(1.0e30), 12);
        TestEqual(TEXT("a top below 20 km/s still leaves STOP and 20 km/s"), NotchCount(0.0), 2);
        TestEqual(TEXT("and a negative one"), NotchCount(-C), 2);
        TestEqual(TEXT("a top a rounding error under 20,000 km/s keeps it"),
                  NotchCount(20000.0 * Km * (1.0 - 1.0e-7)), 11);
        TestEqual(TEXT("and one a rounding error under 0.1 c keeps 0.1 c"), NotchCount(Tenth * (1.0 - 1.0e-7)), 12);
    }

    // Taps count from the ship, not the lever (decision 3).
    {
        const double Capped = PositionOf(22.0 * Km);
        TestEqual(TEXT("under the cap at 22 km/s with the lever at 0.1 c, Ctrl gives 20 km/s at once"),
                  NotchSpeed(TapDown(Top, Capped)), 20.0 * Km);
        TestEqual(TEXT("five notches above a ship at notch 5, Ctrl lands on notch 4"), TapDown(10, 5.0), 4);
        TestEqual(TEXT("and a ship between 5 and 6 lands on 5"), TapDown(10, 5.3), 5);
        TestEqual(TEXT("in steady flight Ctrl is one notch down"), TapDown(7, 7.0), 6);
        TestEqual(TEXT("from a hair under the notch too, the lever on it"), TapDown(7, 7.0 - 1.0e-12), 6);
        TestEqual(TEXT("and a hair over"), TapDown(7, 7.0 + 1.0e-12), 6);
        // With the lever away from the ship, only the tolerance decides: a ship
        // held a rounding error over notch 5 is at notch 5, and Ctrl is notch 4.
        TestEqual(TEXT("under the cap a rounding error over notch 5, Ctrl is notch 4, not 5"), TapDown(Top, 5.0 + 1.0e-12), 4);
        TestEqual(TEXT("held at exactly a notch's speed under the cap, Ctrl still slows"),
                  TapDown(Top, PositionOf(200.0 * Km)), 3);
        TestEqual(TEXT("spooling up, Ctrl stops the climb where it is"), TapDown(10, 8.6), 8);
        TestEqual(TEXT("at STOP, Ctrl stays at STOP: no reverse"), TapDown(0, 0.0), 0);
        TestEqual(TEXT("spooling down to STOP, Ctrl stays at STOP"), TapDown(0, 3.4), 0);
        TestEqual(TEXT("at the bottom notch, Ctrl is STOP"), TapDown(1, 1.0), 0);

        TestEqual(TEXT("spooling down after X, Shift stops the fall where it is"), TapUp(0, 10.5, Top), 11);
        TestEqual(TEXT("in steady flight Shift is one notch up"), TapUp(7, 7.0, Top), 8);
        TestEqual(TEXT("from a hair under the notch too, the lever on it"), TapUp(7, 7.0 - 1.0e-12, Top), 8);
        TestEqual(TEXT("and a hair over"), TapUp(7, 7.0 + 1.0e-12, Top), 8);
        // And the mirror: spooling down after X, a rounding error under notch
        // 10, Shift is notch 11 -- not 10, which would speed the ship by nothing.
        TestEqual(TEXT("spooling down a rounding error under notch 10, Shift is notch 11, not a swallowed tap"),
                  TapUp(0, 10.0 - 1.0e-12, Top), 11);
        TestEqual(TEXT("at STOP, Shift is the bottom notch"), TapUp(0, 0.0, Top), 1);
        TestEqual(TEXT("under the cap, Shift is one notch up the lever, not below it"), TapUp(Top, Capped, Top), Top);
        TestEqual(TEXT("at the top, Shift stays at the top"), TapUp(Top, static_cast<double>(Top), Top), Top);
        TestEqual(TEXT("a lower top stops it there"), TapUp(8, 8.0, 8), 8);
        TestEqual(TEXT("and clamps a spool-down from above it"), TapUp(0, 9.5, 8), 8);

        // Ctrl always slows the ship and Shift always speeds it, from the first
        // tap, wherever the lever and the ship are.
        bool bDownSlows = true;
        bool bUpSpeeds = true;
        bool bDownNeverRaises = true;
        bool bUpNeverLowers = true;
        for (int32 Notch = 0; Notch <= Top; ++Notch)
        {
            for (double P = 0.0; P <= Top; P += 0.05)
            {
                const int32 Down = TapDown(Notch, P);
                const int32 Up = TapUp(Notch, P, Top);
                bDownNeverRaises &= Down <= FMath::Max(Notch - 1, 0);
                bUpNeverLowers &= Up >= FMath::Min(Notch + 1, Top);
                if (P > 0.01)
                {
                    bDownSlows &= NotchSpeed(Down) < SpeedAt(P);
                }
                if (P < Top - 0.99)
                {
                    bUpSpeeds &= NotchSpeed(Up) > SpeedAt(P);
                }
            }
        }
        TestTrue(TEXT("Ctrl always asks for less than the ship is doing"), bDownSlows);
        TestTrue(TEXT("Shift always asks for more"), bUpSpeeds);
        TestTrue(TEXT("Ctrl never raises the lever"), bDownNeverRaises);
        TestTrue(TEXT("Shift never lowers it"), bUpNeverLowers);
    }

    // A hold repeats after 0.3 s at the sweep rate, counted the same at any
    // frame rate: a 1 s hold is the press and 3 x 0.7 = 2 repeats.
    {
        for (const double Hz : { 30.0, 60.0, 144.0 })
        {
            TestEqual(FString::Printf(TEXT("a 1 s hold at %.0f Hz repeats twice"), Hz), RepeatsFor(1.0, Hz, DefaultSweep), 2);
            TestEqual(FString::Printf(TEXT("a 0.28 s hold at %.0f Hz is a tap"), Hz), RepeatsFor(0.28, Hz, DefaultSweep), 0);
            TestEqual(FString::Printf(TEXT("STOP to 0.1 c is 3.7 seconds held at %.0f Hz"), Hz),
                      1 + RepeatsFor(3.7, Hz, DefaultSweep), Top);
        }
        TestTrue(TEXT("and not 3.6"), 1 + RepeatsFor(3.6, 144.0, DefaultSweep) < Top);

        ShipDriveLever::FNotchRepeat Repeat;
        int32 Total = 0;
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            Total += Repeat.Update(true, 1.0 / 60.0, DefaultSweep);
        }
        Total += Repeat.Update(false, 1.0 / 60.0, DefaultSweep);
        const int32 BeforeRelease = Total;
        for (int32 Frame = 0; Frame < 12; ++Frame)
        {
            Total += Repeat.Update(true, 1.0 / 60.0, DefaultSweep);
        }
        TestEqual(TEXT("letting go resets the hold: 0.2 s after a fresh press repeats nothing"), Total, BeforeRelease);
        TestEqual(TEXT("a zero rate never repeats"), RepeatsFor(3.0, 60.0, 0.0), 0);
        TestEqual(TEXT("and one long frame is the same as many short ones"),
                  [] { ShipDriveLever::FNotchRepeat R; return R.Update(true, 1.0, DefaultSweep); }(), 2);
    }

    // The ease: never overshoots, settles a one-notch step to 95% in 1.2 s,
    // and moves at most the response rate.
    {
        const double Frame = 1.0 / 120.0;
        const double At95 = EaseFor(4.0, 5.0, 1.2, Frame, DefaultResponse);
        const double At11 = EaseFor(4.0, 5.0, 1.1, Frame, DefaultResponse);
        TestTrue(FString::Printf(TEXT("one notch up is 95%% there in 1.2 s (%.4f)"), At95 - 4.0), At95 - 4.0 >= 0.95);
        TestTrue(FString::Printf(TEXT("and not yet at 1.1 s (%.4f): the 0.4 s law, not faster"), At11 - 4.0), At11 - 4.0 < 0.95);
        TestTrue(TEXT("one notch down mirrors it"),
                 FMath::IsNearlyEqual(5.0 - EaseFor(5.0, 4.0, 1.2, Frame, DefaultResponse), At95 - 4.0, 1e-12));

        bool bNeverPast = true;
        bool bWithinRate = true;
        for (const double Step : { 1.0 / 144.0, 1.0 / 30.0, 0.25, 2.0 })
        {
            for (const TPair<double, double>& Move : { TPair<double, double>(0.0, Top), TPair<double, double>(Top, 0.0),
                                                       TPair<double, double>(3.0, 4.0), TPair<double, double>(9.5, 9.25) })
            {
                double P = Move.Key;
                const int32 Steps = FMath::CeilToInt32(20.0 / Step);
                for (int32 Index = 0; Index < Steps; ++Index)
                {
                    const double Next = Ease(P, Move.Value, Step, DefaultResponse, 1.0);
                    bNeverPast &= Move.Value >= Move.Key ? Next <= Move.Value : Next >= Move.Value;
                    bNeverPast &= Move.Value >= Move.Key ? Next >= P : Next <= P;
                    bWithinRate &= FMath::Abs(Next - P) <= DefaultResponse * Step * (1.0 + 1e-12);
                    P = Next;
                }
                bNeverPast &= P == Move.Value;
            }
        }
        TestTrue(TEXT("the ease never overshoots, never turns back, and arrives, at any frame length"), bNeverPast);
        TestTrue(TEXT("and never moves faster than the response rate"), bWithinRate);

        TestTrue(TEXT("the same second at 30 Hz, 144 Hz and in one call lands in the same place"),
                 FMath::IsNearlyEqual(EaseFor(0.0, Top, 1.0, 1.0 / 30.0, DefaultResponse),
                                      EaseFor(0.0, Top, 1.0, 1.0 / 144.0, DefaultResponse), 1e-9)
                 && FMath::IsNearlyEqual(EaseFor(0.0, Top, 1.0, 1.0, DefaultResponse),
                                         EaseFor(0.0, Top, 1.0, 1.0 / 144.0, DefaultResponse), 1e-9)
                 && FMath::IsNearlyEqual(EaseFor(Top, 0.0, 6.0, 1.0 / 30.0, DefaultResponse),
                                         EaseFor(Top, 0.0, 6.0, 1.0 / 144.0, DefaultResponse), 1e-9));

        // All stop from 0.1 c: at cruise's top, the bottom notch, in 3.3 s, at
        // rest in 5.9 -- rate-limited down to 1.2 notches, then the 0.4 s
        // exponential down to ArriveNotches, 100 m/s, then that pace held,
        // 250 m/s^2, onto rest -- and the last step onto rest is one step of
        // that steady finish, never a snap.
        double P = static_cast<double>(Top);
        double AtCruiseTop = -1.0;
        double AtRest = -1.0;
        double LastMoving = 0.0;
        for (int32 Index = 1; Index <= 20 * 120 && AtRest < 0.0; ++Index)
        {
            const double Before = SpeedAt(P);
            P = Ease(P, 0.0, Frame, DefaultResponse, 1.0);
            if (AtCruiseTop < 0.0 && SpeedAt(P) <= FShipFlightLimits::Cruise().MaxSpeed)
            {
                AtCruiseTop = Index * Frame;
            }
            if (P == 0.0)
            {
                AtRest = Index * Frame;
                LastMoving = Before;
            }
        }
        TestTrue(FString::Printf(TEXT("all stop from 0.1 c is at cruise's top in 3.3 s (%.2f s)"), AtCruiseTop),
                 AtCruiseTop > 3.2 && AtCruiseTop < 3.45);
        TestTrue(FString::Printf(TEXT("and exactly at rest in about 5.9 (%.2f s)"), AtRest), AtRest > 5.8 && AtRest < 5.95);
        TestEqual(TEXT("rest is rest: SpeedAt is exactly 0"), SpeedAt(P), 0.0);
        const double FinishStep = NotchSpeed(1) * ArriveNotches / EaseSeconds * Frame;
        TestTrue(FString::Printf(TEXT("and it arrives from one step of the steady finish, not a snap (%.3f m/s)"), LastMoving / 100.0),
                 LastMoving > 0.0 && LastMoving <= FinishStep * (1.0 + 1e-9));

        // A tap arrives, exactly, in a known time: 0.4 x ln(1 / ArriveNotches)
        // down the exponential and 0.4 s of the steady finish, 2.52 s. The
        // exponential alone approaches forever, and the snap that once ended
        // it at 5e-5 of a notch came four seconds after the tap, the reading
        // a kilometre a second short of its lever's label for the last one.
        for (const TPair<double, double>& Tap : {TPair<double, double>(4.0, 5.0), TPair<double, double>(1.0, 0.0),
                                                   TPair<double, double>(10.0, 11.0)})
        {
            double Q = Tap.Key;
            double Arrived = -1.0;
            for (int32 Index = 1; Index <= 10 * 120 && Arrived < 0.0; ++Index)
            {
                Q = Ease(Q, Tap.Value, Frame, DefaultResponse, 1.0);
                if (Q == Tap.Value)
                {
                    Arrived = Index * Frame;
                }
            }
            const double Expected = EaseSeconds * (FMath::Loge(1.0 / ArriveNotches) + 1.0);
            TestTrue(FString::Printf(TEXT("a tap from %.0f to %.0f is on its notch in %.2f s (%.3f s)"), Tap.Key, Tap.Value, Expected, Arrived),
                     Arrived > 0.0 && FMath::Abs(Arrived - Expected) <= Frame);
        }

        // Starved boosters (decision 4): thrust scales the whole law, so a
        // quarter thrust makes every change in exactly four times the time.
        // Not sixteen, which a rate pre-scaled by thrust would give, and not
        // the 1.4 of scaling the rate alone, under which a one-notch tap --
        // inside the rate limit throughout -- would hardly slow at all.
        const double TapFull = SecondsToShare(4.0, 5.0, 0.95, Frame, DefaultResponse, 1.0);
        const double TapQuarter = SecondsToShare(4.0, 5.0, 0.95, Frame, DefaultResponse, 0.25);
        TestTrue(FString::Printf(TEXT("a one-notch tap at a quarter thrust takes four times as long (%.3f s against %.3f s)"),
                                 TapQuarter, TapFull),
                 TapFull > 0.0 && FMath::Abs(TapQuarter - 4.0 * TapFull) <= 4.0 * Frame);
        const double SweepFull = SecondsToShare(0.0, Top, 1.0, Frame, DefaultResponse, 1.0);
        const double SweepQuarter = SecondsToShare(0.0, Top, 1.0, Frame, DefaultResponse, 0.25);
        TestTrue(FString::Printf(TEXT("and STOP to 0.1 c, four times as long and arriving (%.3f s against %.3f s)"),
                                 SweepQuarter, SweepFull),
                 SweepFull > 0.0 && FMath::Abs(SweepQuarter - 4.0 * SweepFull) <= 4.0 * Frame);
        TestTrue(TEXT("at the same frame rate, a quarter thrust for 8 s is where full thrust was at 2 s"),
                 FMath::IsNearlyEqual(EaseFor(0.0, Top, 8.0, Frame, DefaultResponse, 0.25),
                                      EaseFor(0.0, Top, 2.0, Frame, DefaultResponse, 1.0), 1e-9));
        TestTrue(TEXT("and never moves faster than a quarter of the response"),
                 FMath::Abs(Ease(0.0, Top, Frame, DefaultResponse, 0.25)) <= 0.25 * DefaultResponse * Frame * (1.0 + 1e-12)
                 && Ease(0.0, Top, Frame, DefaultResponse, 0.25) > 0.0);
        TestEqual(TEXT("no thrust, no motion"), Ease(3.0, 9.0, 1.0, DefaultResponse, 0.0), 3.0);
        TestTrue(TEXT("and more than full thrust is full thrust"),
                 Ease(3.0, 9.0, Frame, DefaultResponse, 4.0) == Ease(3.0, 9.0, Frame, DefaultResponse, 1.0));
        TestEqual(TEXT("no response, no motion"), Ease(3.0, 9.0, 1.0, 0.0, 1.0), 3.0);
        TestEqual(TEXT("no time, no motion"), Ease(3.0, 9.0, 0.0, DefaultResponse, 1.0), 3.0);
        TestEqual(TEXT("at the target, stays"), Ease(9.0, 9.0, 1.0, DefaultResponse, 1.0), 9.0);
    }

    // The cruise sweep, with its detent at zero (decision 2) and its astern
    // end-stop (the 2026-09-27 ruling).
    {
        const double Frame = 1.0 / 60.0;
        const double Rate = DefaultCruiseSweep;
        const FShipFlightLimits Limits = FShipFlightLimits::Cruise();
        const double Astern = CruiseAsternLimit(Limits.MaxSpeed, Limits.AsternSpeed);
        auto Hold = [&](double Throttle, bool bUp, bool bDown, int32 FirstUp, int32 FirstDown, double Seconds)
        {
            double T = Throttle;
            const int32 Frames = FMath::RoundToInt32(Seconds / Frame);
            for (int32 Index = 0; Index < Frames; ++Index)
            {
                T = SweepCruise(T, bUp, bDown, Index == 0 ? FirstUp : 0, Index == 0 ? FirstDown : 0, Frame, Rate, Astern);
            }
            return T;
        };

        TestEqual(TEXT("ds.Cruise.Sweep defaults to 0.2 a second"), Rate, 0.2);
        TestTrue(TEXT("Shift held from rest, pressed: 0.2 a second"), FMath::IsNearlyEqual(Hold(0.0, true, false, 1, 0, 1.0), 0.2, 1e-9));
        TestTrue(TEXT("rest to full ahead is five seconds held, and not 4.9"),
                 Hold(0.0, true, false, 1, 0, 5.0) > 1.0 - 1e-9 && Hold(0.0, true, false, 1, 0, 4.9) < 0.99);
        TestEqual(TEXT("and stops at full ahead"), Hold(0.0, true, false, 1, 0, 8.0), 1.0);
        TestEqual(TEXT("it stays where it is left"), SweepCruise(0.37, false, false, 0, 0, 1.0, Rate, Astern), 0.37);
        TestEqual(TEXT("both keys held cancel"), SweepCruise(0.37, true, true, 1, 1, 1.0, Rate, Astern), 0.37);

        TestEqual(TEXT("Ctrl held from ahead stops at zero"), Hold(0.3, false, true, 1, 1, 5.0), 0.0);
        TestEqual(TEXT("Shift held from astern stops at zero"), Hold(-0.3, true, false, 1, 0, 5.0), 0.0);
        TestEqual(TEXT("a press above zero cannot be spent crossing it, even in one long frame"),
                  SweepCruise(0.01, false, true, 0, 1, 1.0, Rate, Astern), 0.0);
        TestEqual(TEXT("nor below it"), SweepCruise(-0.01, true, false, 1, 0, 1.0, Rate, Astern), 0.0);
        TestEqual(TEXT("held at zero with no fresh press, it stays"), SweepCruise(0.0, false, true, 0, 0, Frame, Rate, Astern), 0.0);
        TestEqual(TEXT("either way"), SweepCruise(0.0, true, false, 0, 0, Frame, Rate, Astern), 0.0);
        TestTrue(TEXT("a fresh Ctrl at zero goes astern"), Hold(0.0, false, true, 0, 1, 1.0) < -0.19);
        TestTrue(TEXT("a fresh Shift at zero goes ahead"), Hold(0.0, true, false, 1, 0, 1.0) > 0.19);
        TestEqual(TEXT("and a fresh press of the other key does not"), SweepCruise(0.0, false, true, 1, 0, Frame, Rate, Astern), 0.0);
        TestEqual(TEXT("astern stops at the astern end-stop"), Hold(0.0, false, true, 0, 1, 5.0), -Astern);
        TestEqual(TEXT("an out-of-range lever is clamped ahead"), SweepCruise(4.0, false, false, 0, 0, Frame, Rate, Astern), 1.0);
        TestEqual(TEXT("and astern, to the end-stop"), SweepCruise(-1.0, false, false, 0, 0, Frame, Rate, Astern), -Astern);
        TestEqual(TEXT("a lever with no astern cannot leave zero astern"),
                  CruiseSpeed(SweepCruise(0.0, false, true, 0, 1, 1.0, Rate, 0.0), Limits.MaxSpeed, Limits.AsternSpeed), 0.0);
    }

    // What the cruise lever asks for (the 2026-09-27 ruling): a log scale,
    // 1 m/s just off zero to 20 km/s at full, rest exactly at zero, and
    // astern the same law mirrored, ending at 200 m/s.
    {
        const FShipFlightLimits Limits = FShipFlightLimits::Cruise();
        const double V = Limits.MaxSpeed;
        const double Back = Limits.AsternSpeed;
        const double Metre = 100.0;                                // cm/s
        TestEqual(TEXT("cruise's top is 20 km/s"), V, 20.0 * Km);
        TestEqual(TEXT("its astern top is 200 m/s"), Back, 200.0 * Metre);
        TestEqual(TEXT("the log scale's floor is 1 m/s"), CruiseFloorCmPerSecond, Metre);

        TestEqual(TEXT("zero is rest, exactly"), CruiseSpeed(0.0, V, Back), 0.0);
        TestEqual(TEXT("and so is negative zero"), CruiseSpeed(-0.0, V, Back), 0.0);
        TestEqual(TEXT("and NaN"), CruiseSpeed(std::numeric_limits<double>::quiet_NaN(), V, Back), 0.0);
        TestTrue(TEXT("full ahead is the top"), RelativeError(CruiseSpeed(1.0, V, Back), V) < 1e-12);
        TestTrue(TEXT("past full ahead is still the top"), RelativeError(CruiseSpeed(3.0, V, Back), V) < 1e-12);
        TestTrue(TEXT("a hair off zero is 1 m/s: the first frame of a press moves the ship"),
                 RelativeError(CruiseSpeed(1.0e-9, V, Back), Metre) < 1e-6);
        TestTrue(TEXT("half way is the geometric mean, sqrt(1 m/s x 20 km/s) = 141 m/s"),
                 RelativeError(CruiseSpeed(0.5, V, Back), FMath::Sqrt(Metre * V)) < 1e-12);

        // Fine at the bottom: every tenth of the lever is the same factor of
        // speed, 20,000^0.1 = 2.69, so 1 to 3 m/s takes as much lever as 7
        // to 20 km/s does.
        const double Factor = FMath::Pow(V / Metre, 0.1);
        bool bSameFactor = true;
        bool bIncreasing = true;
        double Previous = 0.0;
        for (int32 Tenth10 = 1; Tenth10 <= 1000; ++Tenth10)
        {
            const double P = Tenth10 / 1000.0;
            const double Speed = CruiseSpeed(P, V, Back);
            bIncreasing &= Speed > Previous;
            Previous = Speed;
            if (P <= 0.9 + 1e-12)
            {
                bSameFactor &= RelativeError(CruiseSpeed(P + 0.1, V, Back) / Speed, Factor) < 1e-9;
            }
        }
        TestTrue(TEXT("strictly increasing ahead"), bIncreasing);
        TestTrue(FString::Printf(TEXT("and every tenth of the lever is x%.3f"), Factor), bSameFactor);
        TestTrue(TEXT("10 m/s is under a quarter of the lever"), CruiseSpeed(0.23, V, Back) < 10.0 * Metre);

        // Astern: the mirror, position for position, to the end-stop.
        const double Astern = CruiseAsternLimit(V, Back);
        TestTrue(FString::Printf(TEXT("the astern end-stop is ln 200 / ln 20,000 = 0.535 (%.6f)"), Astern),
                 FMath::IsNearlyEqual(Astern, FMath::Loge(200.0) / FMath::Loge(20000.0), 1e-12));
        TestTrue(TEXT("and at it the lever asks for 200 m/s astern"), RelativeError(CruiseSpeed(-Astern, V, Back), -Back) < 1e-9);
        bool bMirror = true;
        bool bNeverPast = true;
        for (double P = 0.001; P <= 1.0; P += 0.001)
        {
            if (P <= Astern)
            {
                bMirror &= CruiseSpeed(-P, V, Back) == -CruiseSpeed(P, V, Back);
            }
            bNeverPast &= CruiseSpeed(-P, V, Back) >= -Back && CruiseSpeed(-P, V, Back) < 0.0;
        }
        TestTrue(TEXT("astern is the ahead law mirrored, up to the end-stop"), bMirror);
        TestTrue(TEXT("and never faster than 200 m/s, whatever the lever says"), bNeverPast);
        TestEqual(TEXT("full astern past the end-stop is 200 m/s"), CruiseSpeed(-1.0, V, Back), -Back);

        TestEqual(TEXT("astern as fast as ahead: the whole travel"), CruiseAsternLimit(V, V), 1.0);
        TestEqual(TEXT("and faster: still the whole travel"), CruiseAsternLimit(V, 2.0 * V), 1.0);
        TestEqual(TEXT("no astern: no travel"), CruiseAsternLimit(V, 0.0), 0.0);
        TestEqual(TEXT("an astern top under the floor: no travel"), CruiseAsternLimit(V, 50.0), 0.0);
        TestEqual(TEXT("the floor itself: none either, since a hair off zero is already the floor"), CruiseAsternLimit(V, Metre), 0.0);
        TestEqual(TEXT("a top under the floor is linear"), CruiseSpeed(0.5, 60.0, 60.0), 30.0);
        TestEqual(TEXT("and so is its astern"), CruiseAsternLimit(60.0, 30.0), 0.5);
        TestEqual(TEXT("no top, no speed"), CruiseSpeed(0.5, 0.0, Back), 0.0);
    }
    return true;
}

#endif
