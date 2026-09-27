#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipSubsystem.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"
#include "UI/ShipScreenWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipHUDSpeedTest,
    "DeepSpace.UI.HUDSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    constexpr double MetresPerSecond = 100.0;     // cm/s
    constexpr double KmPerSecond = 1.0e5;         // cm/s
    constexpr double Light = ShipDriveLever::LightCmPerSecond;

    /** The lever's labels, as the flight-feel spec prints them (decision 3):
     *  what the readout must say at each notch, settled. */
    const TCHAR* const NotchLabels[] = {
        TEXT("1 KM/S"), TEXT("2 KM/S"), TEXT("5 KM/S"), TEXT("10 KM/S"), TEXT("20 KM/S"), TEXT("50 KM/S"),
        TEXT("100 KM/S"), TEXT("200 KM/S"), TEXT("500 KM/S"), TEXT("1,000 KM/S"), TEXT("2,000 KM/S"),
        TEXT("0.01 C"), TEXT("0.02 C"), TEXT("0.05 C"), TEXT("0.1 C"), TEXT("0.2 C"), TEXT("0.5 C"), TEXT("1 C"),
    };

    /**
     * Whether a line holds a time: a word NavText::Duration would print a
     * time in, or a clock. The corner describes the ship; a time needs a
     * destination, and the live ETA is the target's (ruling 3).
     */
    bool HoldsTime(const FString& Line)
    {
        static const TSet<FString> TimeWords = {
            TEXT("S"), TEXT("MIN"), TEXT("H"), TEXT("D"), TEXT("ETA"),
            TEXT("SEC"), TEXT("SECS"), TEXT("SECONDS"), TEXT("MINS"), TEXT("MINUTES"), TEXT("HOURS"),
        };
        TArray<FString> Words;
        Line.ToUpper().ParseIntoArrayWS(Words);
        for (const FString& Word : Words)
        {
            if (TimeWords.Contains(Word) || Word.Contains(TEXT(":")))
            {
                return true;
            }
        }
        return false;
    }

    /** Frames, not one call: the flight state integrates at most
     *  MaxSubStepsPerCall a call and drops the rest, as a hitch would. */
    void Fly(FShipFlightState& State, double Seconds)
    {
        constexpr double Frame = 1.0 / 30.0;
        for (double Flown = 0.0; Flown < Seconds - 1.0e-9; Flown += Frame)
        {
            State.Step(FMath::Min(Frame, Seconds - Flown));
        }
    }

    FShipFlightState Flying(double Throttle, bool bDrive, int32 Notch, double Seconds)
    {
        FShipFlightState State;
        FShipFlightCommand Command;
        Command.Throttle = Throttle;
        Command.bDrive = bDrive;
        Command.DriveNotch = Notch;
        State.SetCommand(Command);
        Fly(State, Seconds);
        return State;
    }
}

/**
 * The bottom-left corner's speed (flight-feel decision 7): the ship's speed
 * in the unit a person would say it in, both levers by the speeds they ask
 * for -- the live one in ink, the other dim, which is what F would go to --
 * and SPOOLING DOWN while the ship eases out of the drive. "Out of control
 * and over controlled" was a pilot who could not read the lever they were
 * moving; this is the line that shows it, so every unit is checked either
 * side of where it hands over, and every notch reads as its label.
 */
bool FShipHUDSpeedTest::RunTest(const FString& Parameters)
{
    const auto Words = [](double CmPerSecond) { return UShipHUDWidget::SpeedWords(CmPerSecond); };
    const FString Sep = NavText::Separator;

    // -- The units, at each boundary ----------------------------------------
    TestEqual(TEXT("at rest"), Words(0.0), FString(TEXT("0 M/S")));
    TestEqual(TEXT("never negative"), Words(-500.0), FString(TEXT("0 M/S")));
    TestEqual(TEXT("cruise's top"), Words(200.0 * MetresPerSecond), FString(TEXT("200 M/S")));
    TestEqual(TEXT("metres under a kilometre a second"), Words(999.4 * MetresPerSecond), FString(TEXT("999 M/S")));
    TestEqual(TEXT("a kilometre a second, never 1000 M/S"), Words(999.6 * MetresPerSecond), FString(TEXT("1 KM/S")));
    TestEqual(TEXT("tenths close in"), Words(12.43 * KmPerSecond), FString(TEXT("12.4 KM/S")));
    TestEqual(TEXT("a tenth that rounds whole drops its decimal"), Words(12.04 * KmPerSecond), FString(TEXT("12 KM/S")));
    TestEqual(TEXT("just under a hundred"), Words(99.94 * KmPerSecond), FString(TEXT("99.9 KM/S")));
    TestEqual(TEXT("a hundred, never 100.0 KM/S"), Words(99.96 * KmPerSecond), FString(TEXT("100 KM/S")));
    TestEqual(TEXT("hundreds whole"), Words(437.4 * KmPerSecond), FString(TEXT("437 KM/S")));
    TestEqual(TEXT("thousands grouped"), Words(2000.0 * KmPerSecond), FString(TEXT("2,000 KM/S")));
    TestEqual(TEXT("just under a hundredth of light, still kilometres"),
              Words(0.00999 * Light), FString(TEXT("2,995 KM/S")));
    TestEqual(TEXT("a hundredth of light, in light"), Words(0.01 * Light), FString(TEXT("0.01 C")));
    TestEqual(TEXT("a rounding error short of a hundredth of light is still the notch, not 2,998 KM/S"),
              Words(0.01 * Light * (1.0 - 1.0e-12)), FString(TEXT("0.01 C")));
    TestEqual(TEXT("hundredths under one"), Words(0.37 * Light), FString(TEXT("0.37 C")));
    TestEqual(TEXT("just under one"), Words(0.994 * Light), FString(TEXT("0.99 C")));
    TestEqual(TEXT("one, never 1.00 C"), Words(0.996 * Light), FString(TEXT("1 C")));
    TestEqual(TEXT("the top"), Words(Light), FString(TEXT("1 C")));

    // -- The reading keeps its decimals while it moves -----------------------
    // Dropped, "12.9", "13", "13.1" would change length every round number
    // it passed, and slide the lever words drawn after it under the eye.
    // Only a reading that says what its lever asks for drops them.
    const auto Reading = [](double CmPerSecond, double Lever) { return UShipHUDWidget::SpeedReading(CmPerSecond, Lever); };
    const double To50 = 50.0 * KmPerSecond;
    TestEqual(TEXT("passing a whole number, the tenth is kept"), Reading(13.0 * KmPerSecond, To50), FString(TEXT("13.0 KM/S")));
    TestEqual(TEXT("so the reading is as long as it was below it"), Reading(13.0 * KmPerSecond, To50).Len(), Reading(12.9 * KmPerSecond, To50).Len());
    TestEqual(TEXT("and above it"), Reading(13.0 * KmPerSecond, To50).Len(), Reading(13.1 * KmPerSecond, To50).Len());
    TestEqual(TEXT("in light, the hundredths are kept"), Reading(0.1 * Light, Light), FString(TEXT("0.10 C")));
    TestEqual(TEXT("and between"), Reading(0.37 * Light, Light), FString(TEXT("0.37 C")));
    TestEqual(TEXT("on its lever, a reading is the lever's label"), Reading(To50, To50), FString(TEXT("50 KM/S")));
    TestEqual(TEXT("in light too"), Reading(0.1 * Light, 0.1 * Light), FString(TEXT("0.1 C")));
    TestEqual(TEXT("and near enough to round to it"), Reading(49.97 * KmPerSecond, To50), FString(TEXT("50 KM/S")));
    TestEqual(TEXT("a lever astern is the same label"), Reading(100.0 * MetresPerSecond, -100.0 * MetresPerSecond), FString(TEXT("100 M/S")));
    TestEqual(TEXT("whole units have no decimals to keep"), Reading(437.4 * KmPerSecond, Light), FString(TEXT("437 KM/S")));

    // -- Every notch reads as its label --------------------------------------
    TestEqual(TEXT("a label for every notch"), static_cast<int32>(UE_ARRAY_COUNT(NotchLabels)), ShipDriveLever::TableNotches());
    for (int32 Notch = 1; Notch <= FMath::Min<int32>(UE_ARRAY_COUNT(NotchLabels), ShipDriveLever::TableNotches()); ++Notch)
    {
        const FString Label = NotchLabels[Notch - 1];
        TestEqual(FString::Printf(TEXT("notch %d's speed reads as its label"), Notch),
                  Words(ShipDriveLever::NotchSpeed(Notch)), Label);

        // And flown: settled on the notch, the ship and the lever say the
        // same words, so a speed the player sets is one they can come back to.
        const FShipFlightState Settled = Flying(0.0, true, Notch, 12.0);
        TestEqual(FString::Printf(TEXT("settled on notch %d, the ship reads what its lever says"), Notch),
                  UShipHUDWidget::MotionLine(Settled).Ink, Label + Sep + TEXT("DRIVE ") + Label);
    }

    // -- The motion line's six shapes ----------------------------------------
    TArray<TPair<EFlightMode, UShipHUDWidget::FMotionWords>> Seen;
    const auto Line = [&](const FShipFlightState& State)
    {
        const UShipHUDWidget::FMotionWords Motion = UShipHUDWidget::MotionLine(State);
        Seen.Emplace(State.GetMode(), Motion);
        return Motion;
    };

    // Cruising up to its lever, the drive left at 1 c: F's speed is on screen.
    {
        const FShipFlightState State = Flying(1.0, false, ShipDriveLever::TableNotches(), 3.55);
        const UShipHUDWidget::FMotionWords Motion = Line(State);
        TestEqual(TEXT("cruising, speed and the live lever in ink"), Motion.Ink, FString(TEXT("142 M/S")) + Sep + TEXT("CRUISE 200 M/S"));
        TestEqual(TEXT("and the drive's lever dim"), Motion.Dim, Sep + TEXT("DRIVE 1 C"));
    }
    // Astern.
    {
        const FShipFlightState State = Flying(-0.5, false, 0, 2.0);
        const UShipHUDWidget::FMotionWords Motion = Line(State);
        TestEqual(TEXT("astern is said"), Motion.Ink, FString(TEXT("80 M/S")) + Sep + TEXT("CRUISE ASTERN 100 M/S"));
        TestEqual(TEXT("and the drive at STOP, dim"), Motion.Dim, Sep + TEXT("DRIVE STOP"));
    }
    // Under the drive, climbing to its notch, cruise's lever left at its top.
    {
        const FShipFlightState State = Flying(1.0, true, 6, 1.0);
        const UShipHUDWidget::FMotionWords Motion = Line(State);
        TestTrue(FString::Printf(TEXT("climbing, the ship is short of its lever ('%s')"), *Motion.Ink),
                 State.GetSpeed() < 50.0 * KmPerSecond && State.GetSpeed() > KmPerSecond);
        TestEqual(TEXT("under the drive, its lever is live"), Motion.Ink, Reading(State.GetSpeed(), To50) + Sep + TEXT("DRIVE 50 KM/S"));
        TestTrue(FString::Printf(TEXT("and the reading short of it keeps its tenth ('%s')"), *Motion.Ink),
                 Motion.Ink.Contains(TEXT(".")) && Motion.Ink.StartsWith(Reading(State.GetSpeed(), To50)));
        TestEqual(TEXT("and cruise's lever is what F would go to"), Motion.Dim, Sep + TEXT("CRUISE 200 M/S"));
    }
    // At the top, cruise at STOP.
    FShipFlightState Top = Flying(0.0, true, ShipDriveLever::TableNotches(), 12.0);
    {
        const UShipHUDWidget::FMotionWords Motion = Line(Top);
        TestEqual(TEXT("at the top"), Motion.Ink, FString(TEXT("1 C")) + Sep + TEXT("DRIVE 1 C"));
        TestEqual(TEXT("cruise at STOP, dim"), Motion.Dim, Sep + TEXT("CRUISE STOP"));
    }
    // Spooling down: F from the top, cruise's lever at half.
    {
        FShipFlightCommand Command = Top.GetCommand();
        Command.bDrive = false;
        Command.Throttle = 0.5;
        Top.SetCommand(Command);
        Fly(Top, 0.5);
        TestEqual(TEXT("F above cruise's top spools down"), static_cast<int32>(Top.GetMode()), static_cast<int32>(EFlightMode::SpoolingDown));
        const UShipHUDWidget::FMotionWords Motion = Line(Top);
        TestEqual(TEXT("spooling down, cruise's lever is live and the corner says why the speed is not yet it"),
                  Motion.Ink, Reading(Top.GetSpeed(), 100.0 * MetresPerSecond) + Sep + TEXT("CRUISE 100 M/S") + Sep + TEXT("SPOOLING DOWN"));
        TestTrue(FString::Printf(TEXT("still far above cruise ('%s')"), *Motion.Ink), Motion.Ink.StartsWith(TEXT("0.")));
        TestEqual(TEXT("and the drive's 1 c is kept, dim"), Motion.Dim, Sep + TEXT("DRIVE 1 C"));

        Fly(Top, 20.0);
        const UShipHUDWidget::FMotionWords After = Line(Top);
        TestEqual(TEXT("the spool over, it is cruise"), static_cast<int32>(Top.GetMode()), static_cast<int32>(EFlightMode::Cruise));
        TestFalse(TEXT("and SPOOLING DOWN is gone"), After.Ink.Contains(TEXT("SPOOLING")));
        TestEqual(TEXT("cruising at its lever"), After.Ink, FString(TEXT("100 M/S")) + Sep + TEXT("CRUISE 100 M/S"));
    }
    // At rest, both levers at STOP.
    {
        const FShipFlightState State = Flying(0.0, true, 0, 1.0);
        const UShipHUDWidget::FMotionWords Motion = Line(State);
        TestEqual(TEXT("at rest is STATIONARY, not a measurement of nothing"), Motion.Ink, FString(TEXT("STATIONARY")) + Sep + TEXT("DRIVE STOP"));
        TestEqual(TEXT("and cruise at STOP, dim"), Motion.Dim, Sep + TEXT("CRUISE STOP"));
    }

    // A lever a hair off its detent is at STOP: a very short frame of Shift
    // can leave cruise's lever a fraction of a metre a second up, and
    // "CRUISE 0 M/S" would name as a setting what is none. Either way.
    for (const double Hair : { 0.001, -0.001 })
    {
        const FShipFlightState State = Flying(Hair, false, 0, 1.0);
        TestTrue(TEXT("the lever is off its detent"), State.GetLeverSpeed() != 0.0);
        TestEqual(FString::Printf(TEXT("a lever at %g of its travel reads STOP"), Hair),
                  Line(State).Ink, FString(TEXT("STATIONARY")) + Sep + TEXT("CRUISE STOP"));
        FShipFlightState Other = Flying(Hair, true, 1, 1.0);
        TestEqual(FString::Printf(TEXT("and at %g, dim, too"), Hair), Line(Other).Dim, Sep + TEXT("CRUISE STOP"));
    }

    // In every mode the dim part names the other lever, and nothing in the
    // corner is a time.
    for (const TPair<EFlightMode, UShipHUDWidget::FMotionWords>& Each : Seen)
    {
        const FString Live = Each.Key == EFlightMode::Drive ? TEXT("DRIVE") : TEXT("CRUISE");
        const FString Other = Each.Key == EFlightMode::Drive ? TEXT("CRUISE") : TEXT("DRIVE");
        TestTrue(FString::Printf(TEXT("'%s' dims the other lever"), *Each.Value.Dim),
                 Each.Value.Dim.StartsWith(Sep + Other + TEXT(" ")));
        TestTrue(FString::Printf(TEXT("'%s' names the live lever in ink"), *Each.Value.Ink),
                 Each.Value.Ink.Contains(Sep + Live + TEXT(" ")) && !Each.Value.Dim.Contains(Live + TEXT(" ")));
        TestFalse(FString::Printf(TEXT("'%s%s' holds no time"), *Each.Value.Ink, *Each.Value.Dim),
                  HoldsTime(Each.Value.Ink + Each.Value.Dim));
    }
    TestTrue(TEXT("the time check can fail"), HoldsTime(TEXT("0.37 C · ETA 52 S")) && HoldsTime(TEXT("12 MIN")));
    TestFalse(TEXT("and speeds are not times"), HoldsTime(TEXT("999 M/S · 12.4 KM/S · 1 C · 10.2 KM")));

    // -- The corner itself draws it ------------------------------------------
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("HUDSpeedTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    if (TestNotNull(TEXT("the world has a ship"), Ship))
    {
        APawn* Pilot = World->SpawnActor<APawn>();
        Ship->SetPilot(Pilot);
        Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
        Ship->SetDriveLever(Pilot, 3);
        for (int32 Frame = 0; Frame < 30; ++Frame)
        {
            Ship->Tick(1.0f / 30.0f);
        }

        UShipHUDWidget* HUD = NewObject<UShipHUDWidget>(World);
        HUD->Initialize();
        HUD->TakeWidget();
        HUD->NativeTick(FGeometry(), 0.016f);

        const UShipHUDWidget::FMotionWords Expected = UShipHUDWidget::MotionLine(Ship->GetFlightState());
        const UTextBlock* Ink = nullptr;
        const UTextBlock* Dim = nullptr;
        HUD->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (const UTextBlock* Block = Cast<UTextBlock>(Widget))
            {
                const FString Text = Block->GetText().ToString();
                Ink = Text == Expected.Ink ? Block : Ink;
                Dim = Text == Expected.Dim ? Block : Dim;
            }
        });
        TestTrue(FString::Printf(TEXT("the HUD's corner reads '%s' in one block"), *Expected.Ink),
                 Ink != nullptr && Expected.Ink.EndsWith(TEXT("CRUISE 200 M/S")));
        TestTrue(FString::Printf(TEXT("and '%s' in another"), *Expected.Dim),
                 Dim != nullptr && Expected.Dim == Sep + TEXT("DRIVE 5 KM/S"));
        if (Ink && Dim)
        {
            TestTrue(TEXT("the other lever is drawn dim, the live one in ink"),
                     Ink->GetColorAndOpacity().GetSpecifiedColor().Equals(UShipScreenWidget::Ink)
                     && Dim->GetColorAndOpacity().GetSpecifiedColor().Equals(UShipScreenWidget::Dim));
        }

        // Nothing the ticked HUD draws in its corners holds a time.
        HUD->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (const UTextBlock* Block = Cast<UTextBlock>(Widget))
            {
                const FString Text = Block->GetText().ToString();
                TestFalse(FString::Printf(TEXT("the HUD's '%s' holds no time"), *Text), HoldsTime(Text));
            }
        });

        // Between stars the ship is folded, not flown. The flight state goes
        // on stepping with both levers at STOP and would ease the corner to
        // STATIONARY mid-jump; the corner says nothing, as the altitude does.
        const TArray<FStarSystemStub> Chart = Ship->GetChart();
        if (TestTrue(TEXT("the chart has somewhere to go"), Chart.Num() > 0 && Ship->PlotCourse(Chart[0].Id)))
        {
            SkyTestWorld::FScopedCVar Instant(TEXT("ds.Nav.ChargeSeconds"), 0.0f);
            Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(),
                            FRotationMatrix::MakeFromX(Ship->GetCourseDirection().Get(FVector::ForwardVector)).ToQuat());
            Ship->SetJumpEngaged(true);
            for (int32 Tick = 0; Tick < 4 && !Ship->IsInTransit(); ++Tick)
            {
                Ship->Tick(0.05f);
            }
            if (TestTrue(TEXT("aligned, engaged and charged, the jump fires"), Ship->IsInTransit()))
            {
                for (int32 Frame = 0; Frame < 15 && Ship->IsInTransit(); ++Frame)
                {
                    Ship->Tick(1.0f / 30.0f);
                }
                TestTrue(TEXT("still between stars"), Ship->IsInTransit());
                HUD->NativeTick(FGeometry(), 0.016f);
                const UShipHUDWidget::FMotionWords Folded = UShipHUDWidget::MotionLineOf(*Ship);
                TestEqual(TEXT("between stars the corner's speed is a dash"), Folded.Ink, FString(TEXT("-----")));
                TestTrue(TEXT("and no lever"), Folded.Dim.IsEmpty());
                bool bLevers = false;
                bool bDash = false;
                HUD->WidgetTree->ForEachWidget([&](UWidget* Widget)
                {
                    if (const UTextBlock* Block = Cast<UTextBlock>(Widget))
                    {
                        const FString Text = Block->GetText().ToString();
                        bLevers |= Text.Contains(TEXT("STOP")) || Text.Contains(TEXT("STATIONARY"));
                        bDash |= Block->GetColorAndOpacity().GetSpecifiedColor().Equals(UShipScreenWidget::Ink)
                                 && Text == Folded.Ink;
                    }
                });
                TestFalse(TEXT("the drawn HUD names no lever and no STATIONARY mid-jump"), bLevers);
                TestTrue(TEXT("and draws the dash in ink"), bDash);
            }
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
