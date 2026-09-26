#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDriveLeverTest,
    "DeepSpace.Ship.DriveLever",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    constexpr double Km = 1.0e5;                                  // cm/s per km/s
    constexpr double C = ShipDriveLever::LightCmPerSecond;
    constexpr int32 Top = 18;

    double RelativeError(double Actual, double Expected)
    {
        return FMath::Abs(Actual - Expected) / FMath::Max(FMath::Abs(Expected), UE_DOUBLE_SMALL_NUMBER);
    }

    /** Ease from From toward To for Seconds in frames of Frame seconds. */
    double EaseFor(double From, double To, double Seconds, double Frame, double Rate)
    {
        double P = From;
        const int32 Frames = FMath::RoundToInt32(Seconds / Frame);
        for (int32 Index = 0; Index < Frames; ++Index)
        {
            P = ShipDriveLever::Ease(P, To, Frame, Rate);
        }
        return P;
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

    // The table is decision 3's, notch for notch: STOP, then 1-2-5 in km/s
    // to 2,000 and in fractions of light from 0.01 to 1 c.
    {
        const double Expected[] = {
            0.0,
            1 * Km, 2 * Km, 5 * Km, 10 * Km, 20 * Km, 50 * Km, 100 * Km, 200 * Km, 500 * Km, 1000 * Km, 2000 * Km,
            0.01 * C, 0.02 * C, 0.05 * C, 0.1 * C, 0.2 * C, 0.5 * C, 1.0 * C,
        };
        TestEqual(TEXT("eighteen notches"), TableNotches(), Top);
        for (int32 Notch = 0; Notch <= Top; ++Notch)
        {
            TestEqual(FString::Printf(TEXT("notch %d is its label"), Notch), NotchSpeed(Notch), Expected[Notch]);
        }
        TestEqual(TEXT("STOP is rest"), NotchSpeed(0), 0.0);
        TestEqual(TEXT("the top is light"), NotchSpeed(Top), C);
        TestEqual(TEXT("below STOP is STOP"), NotchSpeed(-3), 0.0);
        TestEqual(TEXT("above the top is the top"), NotchSpeed(40), C);

        double Smallest = 10.0;
        double Largest = 0.0;
        for (int32 Notch = 1; Notch < Top; ++Notch)
        {
            const double Ratio = NotchSpeed(Notch + 1) / NotchSpeed(Notch);
            Smallest = FMath::Min(Smallest, Ratio);
            Largest = FMath::Max(Largest, Ratio);
        }
        // 2,000 km/s to 0.01 c is 1.499, the one step that is not x2 or x2.5.
        TestTrue(FString::Printf(TEXT("strictly increasing, every step x1.5 to x2.5 (%.4f to %.4f)"), Smallest, Largest),
                 Smallest > 1.49 && Largest <= 2.5 + 1e-12);
        TestTrue(TEXT("1 km/s is five times cruise's 200 m/s"), NotchSpeed(1) == 5.0 * 20000.0);
    }

    // SpeedAt and PositionOf are inverses, continuous and monotonic, linear
    // from STOP to the first notch and geometric between notches.
    {
        double Worst = 0.0;
        for (double V = 1.0; V <= C; V *= 1.07)
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

        TestTrue(TEXT("linear from STOP: half way is 500 m/s"), RelativeError(SpeedAt(0.5), 0.5 * Km) < 1e-12);
        TestTrue(TEXT("and a quarter is 250 m/s"), RelativeError(SpeedAt(0.25), 0.25 * Km) < 1e-12);
        TestTrue(TEXT("geometric between notches: 1.5 is sqrt(1 x 2) km/s"),
                 RelativeError(SpeedAt(1.5), FMath::Sqrt(2.0) * Km) < 1e-12);
        TestTrue(TEXT("and 17.5 is sqrt(0.5 x 1) c"), RelativeError(SpeedAt(17.5), FMath::Sqrt(0.5) * C) < 1e-12);
        TestEqual(TEXT("rest is position 0"), PositionOf(0.0), 0.0);
        TestEqual(TEXT("below rest is position 0"), PositionOf(-5.0), 0.0);
        TestEqual(TEXT("and position 0 is rest"), SpeedAt(0.0), 0.0);
        TestEqual(TEXT("below STOP reads rest"), SpeedAt(-1.0), 0.0);
        TestEqual(TEXT("beyond the top reads light"), SpeedAt(25.0), C);
        TestEqual(TEXT("faster than light is the top position"), PositionOf(3.0 * C), static_cast<double>(Top));
    }

    // NotchCount: 19 positions at 1 c; a lower top drops notches above it;
    // no top ever adds one.
    {
        TestEqual(TEXT("1 c: STOP and eighteen notches"), NotchCount(C), 19);
        TestEqual(TEXT("0.5 c keeps 0.5 c"), NotchCount(0.5 * C), 18);
        TestEqual(TEXT("a shade under 1 c drops it"), NotchCount(0.999 * C), 18);
        TestEqual(TEXT("0.1 c: STOP, eleven km/s notches and four of light"), NotchCount(0.1 * C), 16);
        TestEqual(TEXT("3 km/s: STOP, 1 and 2"), NotchCount(3.0 * Km), 3);
        TestEqual(TEXT("10 c adds nothing"), NotchCount(10.0 * C), 19);
        TestEqual(TEXT("nor does any top"), NotchCount(1.0e30), 19);
        TestEqual(TEXT("a top below 1 km/s still leaves STOP and 1 km/s"), NotchCount(0.0), 2);
        TestEqual(TEXT("and a negative one"), NotchCount(-C), 2);
        TestEqual(TEXT("a top a rounding error under 0.2 c keeps 0.2 c"), NotchCount(0.2 * C * (1.0 - 1.0e-7)), 17);
    }

    // Taps count from the ship, not the lever (decision 3).
    {
        const double Capped = PositionOf(22.0 * Km);
        TestEqual(TEXT("under the cap at 22 km/s with the lever at 1 c, Ctrl gives 20 km/s at once"),
                  NotchSpeed(TapDown(Top, Capped)), 20.0 * Km);
        TestEqual(TEXT("ten notches above a ship at notch 5, Ctrl lands on notch 4"), TapDown(15, 5.0), 4);
        TestEqual(TEXT("and a ship between 5 and 6 lands on 5"), TapDown(15, 5.3), 5);
        TestEqual(TEXT("in steady flight Ctrl is one notch down"), TapDown(7, 7.0), 6);
        TestEqual(TEXT("from a hair under the notch too"), TapDown(7, 7.0 - 1.0e-12), 6);
        TestEqual(TEXT("and a hair over"), TapDown(7, 7.0 + 1.0e-12), 6);
        TestEqual(TEXT("held at exactly a notch's speed under the cap, Ctrl still slows"),
                  TapDown(Top, PositionOf(20.0 * Km)), 4);
        TestEqual(TEXT("spooling up, Ctrl stops the climb where it is"), TapDown(12, 8.6), 8);
        TestEqual(TEXT("at STOP, Ctrl stays at STOP: no reverse"), TapDown(0, 0.0), 0);
        TestEqual(TEXT("spooling down to STOP, Ctrl stays at STOP"), TapDown(0, 3.4), 0);
        TestEqual(TEXT("at the bottom notch, Ctrl is STOP"), TapDown(1, 1.0), 0);

        TestEqual(TEXT("spooling down after X, Shift stops the fall where it is"), TapUp(0, 10.5, Top), 11);
        TestEqual(TEXT("in steady flight Shift is one notch up"), TapUp(7, 7.0, Top), 8);
        TestEqual(TEXT("from a hair under the notch too"), TapUp(7, 7.0 - 1.0e-12, Top), 8);
        TestEqual(TEXT("and a hair over"), TapUp(7, 7.0 + 1.0e-12, Top), 8);
        TestEqual(TEXT("at STOP, Shift is the bottom notch"), TapUp(0, 0.0, Top), 1);
        TestEqual(TEXT("under the cap, Shift is one notch up the lever, not below it"), TapUp(Top, Capped, Top), Top);
        TestEqual(TEXT("at the top, Shift stays at the top"), TapUp(Top, static_cast<double>(Top), Top), Top);
        TestEqual(TEXT("a lower top stops it there"), TapUp(15, 15.0, 15), 15);
        TestEqual(TEXT("and clamps a spool-down from above it"), TapUp(0, 17.5, 15), 15);

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
            TestEqual(FString::Printf(TEXT("STOP to 1 c is six seconds held at %.0f Hz"), Hz),
                      1 + RepeatsFor(6.0, Hz, DefaultSweep), Top);
        }
        TestTrue(TEXT("and not five and a half"), 1 + RepeatsFor(5.5, 144.0, DefaultSweep) < Top);

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
            for (const TPair<double, double>& Move : { TPair<double, double>(0.0, 18.0), TPair<double, double>(18.0, 0.0),
                                                       TPair<double, double>(3.0, 4.0), TPair<double, double>(9.5, 9.25) })
            {
                double P = Move.Key;
                const int32 Steps = FMath::CeilToInt32(20.0 / Step);
                for (int32 Index = 0; Index < Steps; ++Index)
                {
                    const double Next = Ease(P, Move.Value, Step, DefaultResponse);
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
                 FMath::IsNearlyEqual(EaseFor(0.0, 18.0, 1.0, 1.0 / 30.0, DefaultResponse),
                                      EaseFor(0.0, 18.0, 1.0, 1.0 / 144.0, DefaultResponse), 1e-9)
                 && FMath::IsNearlyEqual(EaseFor(0.0, 18.0, 1.0, 1.0, DefaultResponse),
                                         EaseFor(0.0, 18.0, 1.0, 1.0 / 144.0, DefaultResponse), 1e-9)
                 && FMath::IsNearlyEqual(EaseFor(18.0, 0.0, 6.0, 1.0 / 30.0, DefaultResponse),
                                         EaseFor(18.0, 0.0, 6.0, 1.0 / 144.0, DefaultResponse), 1e-9));

        // All stop from 1 c: under cruise's top in 6.3 s, at rest in about nine.
        double P = static_cast<double>(Top);
        double UnderCruise = -1.0;
        double AtRest = -1.0;
        for (int32 Index = 1; Index <= 20 * 120 && AtRest < 0.0; ++Index)
        {
            P = Ease(P, 0.0, Frame, DefaultResponse);
            if (UnderCruise < 0.0 && SpeedAt(P) < 20000.0)
            {
                UnderCruise = Index * Frame;
            }
            if (P == 0.0)
            {
                AtRest = Index * Frame;
            }
        }
        TestTrue(FString::Printf(TEXT("all stop from 1 c is under cruise's top in 6.3 s (%.2f s)"), UnderCruise),
                 UnderCruise > 6.2 && UnderCruise < 6.4);
        TestTrue(FString::Printf(TEXT("and exactly at rest in about nine (%.2f s)"), AtRest), AtRest > 8.0 && AtRest < 9.5);
        TestEqual(TEXT("rest is rest: SpeedAt is exactly 0"), SpeedAt(P), 0.0);

        // Starved boosters: time scaled by thrust (the documented way to feed
        // it) takes exactly four times as long to reach any notch.
        const double Full = EaseFor(0.0, 18.0, 2.0, Frame, DefaultResponse);
        double Starved = 0.0;
        for (int32 Index = 0; Index < 8 * 120; ++Index)
        {
            Starved = Ease(Starved, 18.0, Frame * 0.25, DefaultResponse);
        }
        TestTrue(TEXT("a quarter thrust is where full thrust was in a quarter of the time"),
                 FMath::IsNearlyEqual(Starved, Full, 1e-9));
        TestEqual(TEXT("no response, no motion"), Ease(3.0, 9.0, 1.0, 0.0), 3.0);
        TestEqual(TEXT("no time, no motion"), Ease(3.0, 9.0, 0.0, DefaultResponse), 3.0);
        TestEqual(TEXT("at the target, stays"), Ease(9.0, 9.0, 1.0, DefaultResponse), 9.0);
    }

    // The cruise sweep, with its detent at zero (decision 2).
    {
        const double Frame = 1.0 / 60.0;
        const double Rate = DefaultCruiseSweep;
        auto Hold = [&](double Throttle, bool bUp, bool bDown, int32 FirstUp, int32 FirstDown, double Seconds)
        {
            double T = Throttle;
            const int32 Frames = FMath::RoundToInt32(Seconds / Frame);
            for (int32 Index = 0; Index < Frames; ++Index)
            {
                T = SweepCruise(T, bUp, bDown, Index == 0 ? FirstUp : 0, Index == 0 ? FirstDown : 0, Frame, Rate);
            }
            return T;
        };

        TestTrue(TEXT("Shift held from rest, pressed: 0.5 a second"), FMath::IsNearlyEqual(Hold(0.0, true, false, 1, 0, 1.0), 0.5, 1e-9));
        TestEqual(TEXT("and stops at full ahead"), Hold(0.0, true, false, 1, 0, 5.0), 1.0);
        TestEqual(TEXT("it stays where it is left"), SweepCruise(0.37, false, false, 0, 0, 1.0, Rate), 0.37);
        TestEqual(TEXT("both keys held cancel"), SweepCruise(0.37, true, true, 1, 1, 1.0, Rate), 0.37);

        TestEqual(TEXT("Ctrl held from ahead stops at zero"), Hold(0.3, false, true, 1, 1, 5.0), 0.0);
        TestEqual(TEXT("Shift held from astern stops at zero"), Hold(-0.3, true, false, 1, 0, 5.0), 0.0);
        TestEqual(TEXT("a press above zero cannot be spent crossing it, even in one long frame"),
                  SweepCruise(0.01, false, true, 0, 1, 1.0, Rate), 0.0);
        TestEqual(TEXT("nor below it"), SweepCruise(-0.01, true, false, 1, 0, 1.0, Rate), 0.0);
        TestEqual(TEXT("held at zero with no fresh press, it stays"), SweepCruise(0.0, false, true, 0, 0, Frame, Rate), 0.0);
        TestEqual(TEXT("either way"), SweepCruise(0.0, true, false, 0, 0, Frame, Rate), 0.0);
        TestTrue(TEXT("a fresh Ctrl at zero goes astern"), Hold(0.0, false, true, 0, 1, 1.0) < -0.49);
        TestTrue(TEXT("a fresh Shift at zero goes ahead"), Hold(0.0, true, false, 1, 0, 1.0) > 0.49);
        TestEqual(TEXT("and a fresh press of the other key does not"), SweepCruise(0.0, false, true, 1, 0, Frame, Rate), 0.0);
        TestEqual(TEXT("astern stops at full astern"), Hold(0.0, false, true, 0, 1, 5.0), -1.0);
        TestEqual(TEXT("an out-of-range lever is clamped"), SweepCruise(4.0, false, false, 0, 0, Frame, Rate), 1.0);
    }
    return true;
}

#endif
