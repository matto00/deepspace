#include "Misc/AutomationTest.h"
#include "Ship/ShipHumVoice.h"

#if WITH_DEV_AUTOMATION_TESTS

// One test and no children: a test path with children becomes a group, and a
// group silently stops running its own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FHumVoiceTest,
    "DeepSpace.Ship.HumVoice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    FShipHumInputs Inputs(float EngineFeed, float Push)
    {
        FShipHumInputs Out;
        Out.EngineFeed = EngineFeed;
        Out.Push = Push;
        return Out;
    }

    TArray<float> Render(FShipHumVoice& Voice, double Seconds)
    {
        TArray<float> Out;
        Out.SetNumZeroed(FMath::RoundToInt32(Seconds * Voice.GetSampleRate()));
        Voice.Render(Out.GetData(), Out.Num());
        return Out;
    }

    double Rms(const TArray<float>& Samples)
    {
        double Sum = 0.0;
        for (const float Sample : Samples)
        {
            Sum += static_cast<double>(Sample) * Sample;
        }
        return Samples.Num() > 0 ? FMath::Sqrt(Sum / Samples.Num()) : 0.0;
    }

    /** The RMS of one second, after ten time constants at these inputs:
     *  what the voice settles to, not how it got there. */
    double SettledRms(const FShipHumVoice::FSettings& Settings, float EngineFeed, float Push)
    {
        FShipHumVoice Voice(48000.0f, 7u, Settings);
        Voice.SetTargets(Inputs(EngineFeed, Push));
        Render(Voice, 10.0 * FShipHumVoice::SmoothingSeconds);
        return Rms(Render(Voice, 1.0));
    }
}

/**
 * The hum, with no world and no audio device: everything about the sound
 * that can be stated rather than heard. Each assertion is about a term the
 * voice actually has (lived-in step 3), and the no-click bound is analytic
 * rather than a tuned epsilon, so it cannot be loosened into passing.
 */
bool FHumVoiceTest::RunTest(const FString& Parameters)
{
    // -- the inputs: watts and effort into 0..1 -----------------------------
    TestEqual(TEXT("feed is watts over the winding want"), ShipHum::EngineFeed(400.0f, 800.0f), 0.5f);
    TestEqual(TEXT("and never more than 1"), ShipHum::EngineFeed(900.0f, 800.0f), 1.0f);
    TestEqual(TEXT("an idle engine, drawing nothing, feeds nothing"), ShipHum::EngineFeed(0.0f, 800.0f), 0.0f);
    TestEqual(TEXT("and a want of zero is no feed, not a division"), ShipHum::EngineFeed(300.0f, 0.0f), 0.0f);

    TestEqual(TEXT("pushing at the rating is full push"), ShipHum::Push(4000.0f, 4000.0f, 0.0f, 1.0f, 0.35f), 1.0f);
    TestEqual(TEXT("starved boosters accelerate at a quarter, and push a quarter"),
              ShipHum::Push(1000.0f, 4000.0f, 1.0f, 0.25f, 0.35f), 0.25f);
    TestEqual(TEXT("holding a full cruise keeps CruiseHiss"), ShipHum::Push(0.0f, 4000.0f, 1.0f, 1.0f, 0.35f), 0.35f);
    TestEqual(TEXT("astern the same as ahead"), ShipHum::Push(0.0f, 4000.0f, -1.0f, 1.0f, 0.35f), 0.35f);
    TestEqual(TEXT("thinner with the boosters starved"),
              ShipHum::Push(0.0f, 4000.0f, 1.0f, 0.25f, 0.35f), 0.35f * 0.25f);
    TestEqual(TEXT("CruiseHiss 0: silent once the speed is reached"), ShipHum::Push(0.0f, 4000.0f, 1.0f, 1.0f, 0.0f), 0.0f);
    TestEqual(TEXT("the drive's jolt is clamped, not a scream"), ShipHum::Push(1.0e9f, 4000.0f, 0.0f, 1.0f, 0.35f), 1.0f);

    // -- determinism, and bounds -----------------------------------------------
    for (const float Rate : { 44100.0f, 48000.0f })
    {
        const FString At = FString::Printf(TEXT("at %.0f Hz"), Rate);
        FShipHumVoice A(Rate, 12345u);
        FShipHumVoice B(Rate, 12345u);
        FShipHumVoice Other(Rate, 54321u);
        TArray<float> SamplesA, SamplesB, SamplesOther;
        // Inputs changing mid-stream, so the glides are exercised too.
        for (const FShipHumInputs& Step : { Inputs(0.0f, 0.0f), Inputs(1.0f, 0.0f), Inputs(0.3f, 1.0f), Inputs(1.0f, 1.0f) })
        {
            A.SetTargets(Step);
            B.SetTargets(Step);
            Other.SetTargets(Step);
            SamplesA.Append(Render(A, 0.5));
            SamplesB.Append(Render(B, 0.5));
            SamplesOther.Append(Render(Other, 0.5));
        }
        TestTrue(At + TEXT(": the same seed and inputs give the same samples"), SamplesA == SamplesB);
        TestFalse(At + TEXT(": another seed hisses differently, so two sources never comb"), SamplesA == SamplesOther);

        bool bFinite = true;
        float Peak = 0.0f;
        for (const float Sample : SamplesA)
        {
            bFinite &= FMath::IsFinite(Sample);
            Peak = FMath::Max(Peak, FMath::Abs(Sample));
        }
        TestTrue(At + TEXT(": no NaN, no infinity"), bFinite);
        TestTrue(At + FString::Printf(TEXT(": |x| <= 1 (peak %.4f)"), Peak), Peak <= 1.0f);
        TestTrue(At + FString::Printf(TEXT(": and quiet -- under -12 dBFS (peak %.4f)"), Peak), Peak < 0.25f);
    }

    // -- smoothing, on the parameter trajectory ---------------------------------
    {
        const float Rate = 48000.0f;
        FShipHumVoice Voice(Rate, 1u);
        Voice.SetTargets(Inputs(1.0f, 1.0f));
        const int32 AtTau = FMath::RoundToInt32(FShipHumVoice::SmoothingSeconds * Rate);
        bool bMonotone = true;
        bool bNoOvershoot = true;
        float Last = 0.0f;
        float Scratch = 0.0f;
        for (int32 Index = 0; Index < AtTau; ++Index)
        {
            Voice.Render(&Scratch, 1);
            const float Now = Voice.GetSmoothed().EngineFeed;
            bMonotone &= Now >= Last;
            bNoOvershoot &= Now <= 1.0f;
            Last = Now;
        }
        TestTrue(TEXT("a stepped target is approached monotonically"), bMonotone);
        TestTrue(TEXT("and never overshot"), bNoOvershoot);
        TestTrue(FString::Printf(TEXT("at one time constant it is 0.632 +- 0.01 of the way (%.4f)"), Last),
                 FMath::Abs(Last - 0.632f) <= 0.01f);
        TestTrue(TEXT("push glides the same way"), FMath::IsNearlyEqual(Voice.GetSmoothed().Push, Last, 1e-5f));
        TestTrue(TEXT("and so does the voice's onset"), FMath::IsNearlyEqual(Voice.GetPresence(), Last, 1e-5f));
    }

    // -- no clicks, on the tonal voice -----------------------------------------
    //
    // Every sample-to-sample step of a sum of sines is bounded by each term's
    // slope, 2 pi f A / SR, plus how far its amplitude may move in a sample,
    // which a glide limits to (A + dA) / (tau SR): A for the onset, dA for the
    // input. Anything continuous is under the bound; a phase reset or an
    // unsmoothed step is far over it. Noise is excluded, being discontinuous
    // by nature.
    {
        FShipHumVoice::FSettings Tonal = FShipHumVoice::Reactor();
        Tonal.bNoise = false;
        for (const float Rate : { 44100.0f, 48000.0f })
        {
            FShipHumVoice Voice(Rate, 3u, Tonal);
            Voice.SetTargets(Inputs(0.0f, 0.0f));
            TArray<float> Samples = Render(Voice, 1.0);
            Voice.SetTargets(Inputs(1.0f, 0.0f));
            Samples.Append(Render(Voice, 1.0));

            const double Tau = FShipHumVoice::SmoothingSeconds;
            const double TwoPi = 2.0 * UE_DOUBLE_PI;
            const double FundamentalMax = FShipHumVoice::FundamentalHz * (1.0 + FShipHumVoice::PitchRise);
            struct FTerm { double Hz; double Level; double Rise; };
            const FTerm Terms[] = {
                { FundamentalMax, FShipHumVoice::FundamentalLevel, 0.0 },
                { 2.0 * FundamentalMax - FShipHumVoice::BeatHz, 0.5 * FShipHumVoice::SecondLevel, 0.0 },
                { 2.0 * FundamentalMax + FShipHumVoice::BeatHz, 0.5 * FShipHumVoice::SecondLevel, 0.0 },
                { 3.0 * FundamentalMax, FShipHumVoice::ThirdLevel + FShipHumVoice::ThirdRise, FShipHumVoice::ThirdRise },
            };
            double Bound = 0.0;
            for (const FTerm& Term : Terms)
            {
                Bound += TwoPi * Term.Hz * Term.Level + (Term.Level + Term.Rise) / Tau;
            }
            Bound *= FShipHumVoice::MasterGain / Rate * 1.01;

            double Worst = FMath::Abs(Samples[0]);
            for (int32 Index = 1; Index < Samples.Num(); ++Index)
            {
                Worst = FMath::Max(Worst, static_cast<double>(FMath::Abs(Samples[Index] - Samples[Index - 1])));
            }
            TestTrue(FString::Printf(TEXT("at %.0f Hz no step, onset or feed change included, exceeds the bound (%.6f <= %.6f)"),
                                     Rate, Worst, Bound),
                     Worst <= Bound);
        }
    }

    // -- terms that follow their inputs -----------------------------------------
    {
        FShipHumVoice::FSettings Tonal = FShipHumVoice::Reactor();
        Tonal.bNoise = false;
        FShipHumVoice::FSettings Hiss = FShipHumVoice::Reactor();
        Hiss.bTone = false;

        const double Starved = SettledRms(Tonal, 0.0f, 0.0f);
        const double Fed = SettledRms(Tonal, 1.0f, 0.0f);
        TestTrue(FString::Printf(TEXT("the drone is fuller fed (%.4f) than starved (%.4f)"), Fed, Starved), Fed > Starved);
        // A register, not a fault: starved, the drone is still nearly all
        // there. The fundamental and the beating pair do not follow the feed.
        TestTrue(FString::Printf(TEXT("and starved is a different ship, not a silent one (%.4f of fed)"), Starved / Fed),
                 Starved >= 0.75 * Fed);
        TestTrue(TEXT("the drone does not follow push"),
                 FMath::IsNearlyEqual(SettledRms(Tonal, 0.5f, 1.0f), SettledRms(Tonal, 0.5f, 0.0f), 1e-6));

        const double Quiet = SettledRms(Hiss, 0.0f, 0.0f);
        const double Pushing = SettledRms(Hiss, 0.0f, 1.0f);
        TestTrue(FString::Printf(TEXT("the hiss opens up with push (%.4f over %.4f)"), Pushing, Quiet), Pushing > 2.0 * Quiet);
        TestTrue(TEXT("and does not follow the feed"),
                 FMath::IsNearlyEqual(SettledRms(Hiss, 1.0f, 0.5f), SettledRms(Hiss, 0.0f, 0.5f), 1e-6));

        // The air follows nothing at all: sample for sample the same.
        FShipHumVoice Still(48000.0f, 9u, FShipHumVoice::Air());
        FShipHumVoice Pushed(48000.0f, 9u, FShipHumVoice::Air());
        Pushed.SetTargets(Inputs(1.0f, 1.0f));
        TestTrue(TEXT("the air is the same whatever the ship does"), Render(Still, 2.0) == Render(Pushed, 2.0));
        const double Air = SettledRms(FShipHumVoice::Air(), 0.0f, 0.0f);
        TestTrue(FString::Printf(TEXT("and quieter than the reactor's drone at its lowest (%.4f against %.4f)"), Air, Starved),
                 Air > 0.0 && Air < Starved);
    }

    return true;
}

#endif
