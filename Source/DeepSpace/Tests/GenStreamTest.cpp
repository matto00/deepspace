#include "Misc/AutomationTest.h"
#include "Tests/GenTestVectors.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGenStreamTest,
    "DeepSpace.Universe.Stream",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Named, never anonymous: the unity build pastes every test file into one
// translation unit, where each file's anonymous namespace is the same one
// and a second SameStub is a redefinition.
namespace GenStreamTestLocal
{
    constexpr int32 Draws = 100000;

    struct FMoments
    {
        double Mean = 0.0;
        double Variance = 0.0;
    };

    template <typename FDraw>
    FMoments MomentsOf(FDraw&& Draw)
    {
        double Sum = 0.0;
        double SumSq = 0.0;
        for (int32 I = 0; I < Draws; ++I)
        {
            const double X = Draw();
            Sum += X;
            SumSq += X * X;
        }
        FMoments M;
        M.Mean = Sum / Draws;
        M.Variance = SumSq / Draws - M.Mean * M.Mean;
        return M;
    }

    double Phi(double X)
    {
        return 0.5 * std::erfc(-X / std::sqrt(2.0));
    }

    /** One sampler section of the vectors file: a fresh stream on its seed,
     *  32 draws, each compared exactly -- doubles by their bits. */
    void CheckSection(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Section, int32& OutDraws)
    {
        using namespace GenTestVectors;

        const FString Name = Section->GetStringField(TEXT("name"));
        const FString Sampler = Section->GetStringField(TEXT("sampler"));
        const FString Returns = Section->GetStringField(TEXT("returns"));
        const TArray<TSharedPtr<FJsonValue>>& Args = Section->GetArrayField(TEXT("args"));
        const TArray<TSharedPtr<FJsonValue>>& Values = Section->GetArrayField(TEXT("values"));

        auto Double = [&Args](int32 I) { return F64(Args[I]->AsString()); };

        TArray<double> Weights;
        if (Sampler == TEXT("Categorical"))
        {
            for (const TSharedPtr<FJsonValue>& W : Args[0]->AsArray())
            {
                Weights.Add(F64(W->AsString()));
            }
        }

        FGenStream Stream(U64(Section->GetStringField(TEXT("seed"))));
        int32 Mismatches = 0;
        FString FirstMismatch;

        for (int32 Index = 0; Index < Values.Num(); ++Index)
        {
            const TSharedPtr<FJsonValue>& Expected = Values[Index];
            bool bMatch = false;
            FString Got;
            FString Want;

            if (Returns == TEXT("double"))
            {
                double Actual = 0.0;
                if (Sampler == TEXT("Unit")) { Actual = Stream.Unit(); }
                else if (Sampler == TEXT("UnitOpen")) { Actual = Stream.UnitOpen(); }
                else if (Sampler == TEXT("Normal")) { Actual = Stream.Normal(Double(0), Double(1)); }
                else if (Sampler == TEXT("LogNormal")) { Actual = Stream.LogNormal(Double(0), Double(1)); }
                else if (Sampler == TEXT("LogNormalBounded")) { Actual = Stream.LogNormalBounded(Double(0), Double(1), Double(2), Double(3)); }
                else if (Sampler == TEXT("Exponential")) { Actual = Stream.Exponential(Double(0)); }
                else if (Sampler == TEXT("ParetoBounded")) { Actual = Stream.ParetoBounded(Double(0), Double(1), Double(2)); }
                else if (Sampler == TEXT("Gamma")) { Actual = Stream.Gamma(Double(0)); }
                else if (Sampler == TEXT("Beta")) { Actual = Stream.Beta(Double(0), Double(1)); }
                else { Test.AddError(TEXT("unknown double sampler in the vectors: ") + Sampler); return; }

                const double Wanted = F64(Expected->AsString());
                bMatch = Bits(Actual) == Bits(Wanted);
                Got = FString::Printf(TEXT("%.17g"), Actual);
                Want = FString::Printf(TEXT("%.17g (%s)"), Wanted, *Expected->AsString());
            }
            else if (Returns == TEXT("int64"))
            {
                const int64 Actual = Stream.UniformInt(I64(Args[0]->AsString()), I64(Args[1]->AsString()));
                const int64 Wanted = I64(Expected->AsString());
                bMatch = Actual == Wanted;
                Got = LexToString(Actual);
                Want = LexToString(Wanted);
            }
            else if (Returns == TEXT("int"))
            {
                int32 Actual = 0;
                if (Sampler == TEXT("Poisson")) { Actual = Stream.Poisson(Double(0), static_cast<int32>(Args[1]->AsNumber())); }
                else if (Sampler == TEXT("Categorical")) { Actual = Stream.Categorical(Weights); }
                else { Test.AddError(TEXT("unknown int sampler in the vectors: ") + Sampler); return; }

                const int32 Wanted = static_cast<int32>(Expected->AsNumber());
                bMatch = Actual == Wanted;
                Got = LexToString(Actual);
                Want = LexToString(Wanted);
            }
            else if (Returns == TEXT("bool"))
            {
                const bool Actual = Stream.Chance(Double(0));
                const bool Wanted = Expected->AsBool();
                bMatch = Actual == Wanted;
                Got = LexToString(Actual);
                Want = LexToString(Wanted);
            }
            else
            {
                Test.AddError(TEXT("unknown return type in the vectors: ") + Returns);
                return;
            }

            ++OutDraws;
            if (!bMatch)
            {
                if (Mismatches++ == 0)
                {
                    FirstMismatch = FString::Printf(TEXT("draw %d: got %s, want %s"), Index, *Got, *Want);
                }
            }
        }

        if (Mismatches > 0)
        {
            Test.AddError(FString::Printf(TEXT("vectors: %s differs from Tools/rng.py in %d of %d draws; first, %s"),
                *Name, Mismatches, Values.Num(), *FirstMismatch));
        }
    }
}

bool FGenStreamTest::RunTest(const FString& Parameters)
{
    using namespace GenStreamTestLocal;

    // -- the known-value table's stream rows, typed in from the spec ----------
    {
        // SplitMix64's published reference sequence from seed 0: checked
        // against the outside world, not only against ourselves.
        FGenStream Stream(0);
        TestEqual(TEXT("seed 0, first NextU64"), Stream.NextU64(), 0xE220A8397B1DCDAFull);
        TestEqual(TEXT("seed 0, second NextU64"), Stream.NextU64(), 0x6E789E6AA1B965F4ull);
        TestEqual(TEXT("seed 0, third NextU64"), Stream.NextU64(), 0x06C45D188009454Full);

        FGenStream Fresh(0);
        const double Unit = Fresh.Unit();
        TestTrue(TEXT("seed 0, first Unit is exactly (Mix(0) >> 11) * 2^-53"),
            GenTestVectors::Bits(Unit) == GenTestVectors::Bits(0.88331080821364261));

        for (uint64 Seed : {uint64(0), uint64(1), uint64(0xDEADBEEF)})
        {
            FGenStream S(Seed);
            TestEqual(TEXT("NextU64 from state S is Mix(S)"), S.NextU64(), GenSeed::Mix(Seed));
        }
    }

    // The two sector counts pin Poisson and the count stream as well as the
    // hash. The mean is written here, not taken from the priors: a prior tune
    // must never turn a known-value test red.
    {
        FGenPriors Pinned;
        Pinned.SystemsPerSector = 0.5;
        const FGalaxyGenerator Galaxy(20260925, Pinned);
        TestEqual(TEXT("sector (0,0,0) holds 0 systems at mean 0.5"), Galaxy.SystemCount(FInt64Vector(0, 0, 0)), 0);
        TestEqual(TEXT("sector (1,0,0) holds 2 systems at mean 0.5"), Galaxy.SystemCount(FInt64Vector(1, 0, 0)), 2);
    }

    // -- Tools/rng_vectors.json, the referee between this and Tools/rng.py ---
    {
        FString Error;
        const TSharedPtr<FJsonObject> Vectors = GenTestVectors::Load(Error);
        if (!TestTrue(TEXT("rng_vectors.json loads: ") + Error, Vectors.IsValid()))
        {
            return false;
        }
        using namespace GenTestVectors;

        for (const TSharedPtr<FJsonValue>& Value : Vectors->GetArrayField(TEXT("stream")))
        {
            const TSharedPtr<FJsonObject>& Row = Value->AsObject();
            const FString SeedText = Row->GetStringField(TEXT("seed"));
            FGenStream Stream(U64(SeedText));
            int32 Index = 0;
            for (const TSharedPtr<FJsonValue>& Expected : Row->GetArrayField(TEXT("next_u64")))
            {
                TestEqual(FString::Printf(TEXT("vectors: seed %s, NextU64 #%d"), *SeedText, Index++),
                    Stream.NextU64(), U64(Expected->AsString()));
            }
        }

        const TSharedPtr<FJsonObject> UnitRow = Vectors->GetObjectField(TEXT("unit"));
        FGenStream UnitStream(U64(UnitRow->GetStringField(TEXT("seed"))));
        TestTrue(TEXT("vectors: first Unit"), Bits(UnitStream.Unit()) == Bits(F64(UnitRow->GetStringField(TEXT("value")))));

        const TSharedPtr<FJsonObject> Chain = Vectors->GetObjectField(TEXT("chain"));
        for (const TSharedPtr<FJsonValue>& Value : Chain->GetArrayField(TEXT("sectors")))
        {
            const TSharedPtr<FJsonObject>& Row = Value->AsObject();
            FGenStream Count(U64(Row->GetStringField(TEXT("count_seed"))));
            TestEqual(TEXT("vectors: a sector's count"),
                Count.Poisson(F64(Row->GetStringField(TEXT("count_mean"))), static_cast<int32>(Row->GetNumberField(TEXT("count_max")))),
                static_cast<int32>(Row->GetNumberField(TEXT("count"))));
        }

        int32 Sections = 0;
        int32 SamplerDraws = 0;
        for (const TSharedPtr<FJsonValue>& Value : Vectors->GetArrayField(TEXT("samplers")))
        {
            CheckSection(*this, Value->AsObject(), SamplerDraws);
            ++Sections;
        }
        AddInfo(FString::Printf(TEXT("vectors: %d sampler sections, %d draws, compared bit for bit"), Sections, SamplerDraws));
        TestTrue(TEXT("the vectors hold every sampler section"), Sections >= 33 && SamplerDraws >= 33 * 32);
    }

    // -- the shapes: 100,000 draws of each against its analytic moments ------
    // A fixed seed, so these are deterministic: a pass is a pass for good,
    // and the tolerances are about four standard errors.
    {
        FGenStream S(GenSeed::Label("stream.moments"));
        const FMoments U = MomentsOf([&S] { return S.Unit(); });
        TestTrue(TEXT("Unit mean is 1/2"), FMath::Abs(U.Mean - 0.5) < 0.004);
        TestTrue(TEXT("Unit variance is 1/12"), FMath::Abs(U.Variance - 1.0 / 12.0) < 0.002);
    }
    {
        FGenStream S(GenSeed::Label("stream.unitopen"));
        bool bOpen = true;
        for (int32 I = 0; I < Draws; ++I)
        {
            const double X = S.UnitOpen();
            bOpen &= X > 0.0 && X < 1.0;
        }
        TestTrue(TEXT("UnitOpen never reaches either end"), bOpen);
    }
    {
        FGenStream S(GenSeed::Label("stream.uniformint"));
        bool bInRange = true;
        int32 Hits[13] = {};
        for (int32 I = 0; I < Draws; ++I)
        {
            const int64 X = S.UniformInt(-3, 9);
            bInRange &= X >= -3 && X <= 9;
            if (X >= -3 && X <= 9) { ++Hits[X + 3]; }
        }
        TestTrue(TEXT("UniformInt stays in its range"), bInRange);
        bool bEven = true;
        for (const int32 H : Hits)
        {
            bEven &= FMath::Abs(H - Draws / 13.0) < 4.0 * FMath::Sqrt(Draws / 13.0);
        }
        TestTrue(TEXT("UniformInt favours no value"), bEven);
    }
    {
        FGenStream S(GenSeed::Label("stream.chance"));
        const FMoments C = MomentsOf([&S] { return S.Chance(0.3) ? 1.0 : 0.0; });
        TestTrue(TEXT("Chance(0.3) comes up three times in ten"), FMath::Abs(C.Mean - 0.3) < 0.006);
    }
    for (const double Mean : {0.5, 4.0, 9.0})
    {
        FGenStream S(GenSeed::Derive(GenSeed::Label("stream.poisson"), 0, static_cast<uint64>(Mean * 10.0)));
        const FMoments P = MomentsOf([&S, Mean] { return static_cast<double>(S.Poisson(Mean, 100)); });
        TestTrue(FString::Printf(TEXT("Poisson(%.1f) mean"), Mean), FMath::Abs(P.Mean - Mean) < 4.0 * FMath::Sqrt(Mean / Draws));
        TestTrue(FString::Printf(TEXT("Poisson(%.1f) variance equals its mean"), Mean), FMath::Abs(P.Variance - Mean) < 0.03 * Mean + 0.01);
    }
    {
        FGenStream S(GenSeed::Label("stream.poissoncap"));
        bool bCapped = true;
        for (int32 I = 0; I < Draws; ++I)
        {
            bCapped &= S.Poisson(6.0, 3) <= 3;
        }
        TestTrue(TEXT("Poisson never exceeds its Max"), bCapped);
    }
    {
        FGenStream S(GenSeed::Label("stream.normal"));
        const FMoments N = MomentsOf([&S] { return S.Normal(10.0, 3.0); });
        TestTrue(TEXT("Normal mean"), FMath::Abs(N.Mean - 10.0) < 0.04);
        TestTrue(TEXT("Normal variance"), FMath::Abs(N.Variance - 9.0) < 0.2);
    }
    {
        FGenStream S(GenSeed::Label("stream.lognormal"));
        int32 Below = 0;
        for (int32 I = 0; I < Draws; ++I)
        {
            Below += S.LogNormal(20.0, 0.4) < 20.0 ? 1 : 0;
        }
        TestTrue(TEXT("LogNormal's median is its Median"), FMath::Abs(Below / double(Draws) - 0.5) < 0.007);
    }
    {
        FGenStream S(GenSeed::Label("stream.exponential"));
        const FMoments E = MomentsOf([&S] { return S.Exponential(0.18); });
        TestTrue(TEXT("Exponential mean"), FMath::Abs(E.Mean - 0.18) < 0.003);
    }
    {
        FGenStream S(GenSeed::Label("stream.pareto"));
        bool bBounded = true;
        for (int32 I = 0; I < Draws; ++I)
        {
            const double X = S.ParetoBounded(1.5, 1.0, 400.0);
            bBounded &= X >= 1.0 && X <= 400.0;
        }
        TestTrue(TEXT("ParetoBounded respects its bounds"), bBounded);
    }
    for (const double Shape : {0.5, 1.2, 5.0})
    {
        FGenStream S(GenSeed::Derive(GenSeed::Label("stream.gamma"), 0, static_cast<uint64>(Shape * 10.0)));
        const FMoments G = MomentsOf([&S, Shape] { return S.Gamma(Shape); });
        TestTrue(FString::Printf(TEXT("Gamma(%.1f) mean is its shape"), Shape), FMath::Abs(G.Mean - Shape) < 4.0 * FMath::Sqrt(Shape / Draws));
        TestTrue(FString::Printf(TEXT("Gamma(%.1f) variance is its shape"), Shape), FMath::Abs(G.Variance - Shape) < 0.05 * Shape);
    }
    for (const TPair<double, double>& AB : {TPair<double, double>(1.2, 2.0), TPair<double, double>(2.0, 4.0), TPair<double, double>(0.867, 3.03)})
    {
        const double A = AB.Key;
        const double B = AB.Value;
        FGenStream S(GenSeed::Derive(GenSeed::Label("stream.beta"), static_cast<uint64>(A * 1000.0), static_cast<uint64>(B * 1000.0)));
        bool bBounded = true;
        const FMoments M = MomentsOf([&] {
            const double X = S.Beta(A, B);
            bBounded &= X >= 0.0 && X <= 1.0;
            return X;
        });
        const double Mean = A / (A + B);
        const double Variance = A * B / ((A + B) * (A + B) * (A + B + 1.0));
        TestTrue(FString::Printf(TEXT("Beta(%.3g, %.3g) stays in [0, 1]"), A, B), bBounded);
        TestTrue(FString::Printf(TEXT("Beta(%.3g, %.3g) mean is a/(a+b)"), A, B), FMath::Abs(M.Mean - Mean) < 4.0 * FMath::Sqrt(Variance / Draws));
        TestTrue(FString::Printf(TEXT("Beta(%.3g, %.3g) variance"), A, B), FMath::Abs(M.Variance - Variance) < 0.03 * Variance);
    }
    {
        FGenStream S(GenSeed::Label("stream.categorical"));
        const double Weights[] = {0.76, 0.12, 0.076, 0.030, 0.006, 0.0013};
        double Total = 0.0;
        for (const double W : Weights) { Total += W; }
        int32 Hits[6] = {};
        for (int32 I = 0; I < Draws; ++I)
        {
            ++Hits[S.Categorical(Weights)];
        }
        bool bInProportion = true;
        for (int32 I = 0; I < 6; ++I)
        {
            const double P = Weights[I] / Total;
            bInProportion &= FMath::Abs(Hits[I] / double(Draws) - P) < 4.0 * FMath::Sqrt(P * (1.0 - P) / Draws) + 1.0e-4;
        }
        TestTrue(TEXT("Categorical chooses in proportion to its weights"), bInProportion);

        const double WithZeros[] = {0.0, 1.0, 0.0, 2.0};
        bool bNeverZero = true;
        for (int32 I = 0; I < Draws; ++I)
        {
            const int32 Index = S.Categorical(WithZeros);
            bNeverZero &= Index == 1 || Index == 3;
        }
        TestTrue(TEXT("Categorical never chooses a zero weight"), bNeverZero);
    }

    // -- truncation, not clamping ---------------------------------------------
    // The test that fails if someone swaps truncation for a clamp: the mean
    // matches the *truncated* distribution's, and almost nothing sits on a
    // bound. A clamp would put a third of these draws exactly on one.
    {
        constexpr double Median = 1.0;
        constexpr double Sigma = 0.9;
        constexpr double Min = 0.5;
        constexpr double Max = 3.0;

        FGenStream S(GenSeed::Label("stream.truncated"));
        int32 OnBound = 0;
        bool bBounded = true;
        const FMoments M = MomentsOf([&] {
            const double X = S.LogNormalBounded(Median, Sigma, Min, Max);
            bBounded &= X >= Min && X <= Max;
            OnBound += (X == Min || X == Max) ? 1 : 0;
            return X;
        });

        const double Alpha = std::log(Min / Median) / Sigma;
        const double Beta = std::log(Max / Median) / Sigma;
        const double Expected = Median * std::exp(0.5 * Sigma * Sigma) * (Phi(Beta - Sigma) - Phi(Alpha - Sigma)) / (Phi(Beta) - Phi(Alpha));

        AddInfo(FString::Printf(TEXT("truncated log-normal: mean %.5f, analytic %.5f, %d draws on a bound"), M.Mean, Expected, OnBound));
        TestTrue(TEXT("LogNormalBounded respects its bounds"), bBounded);
        TestTrue(TEXT("LogNormalBounded's mean is the truncated distribution's"), FMath::Abs(M.Mean - Expected) < 0.005);
        TestTrue(TEXT("fewer than 1 in 10^4 draws sit exactly on a bound"), OnBound < Draws / 10000);
    }

    // The generator's own bounded draws respect their bounds.
    {
        FGenStream S(GenSeed::Label("stream.generatorbounds"));
        bool bSpacing = true;
        bool bMass = true;
        bool bPopulation = true;
        for (int32 I = 0; I < Draws; ++I)
        {
            const double K = S.LogNormalBounded(20.0, 0.4, 10.0, 60.0);
            bSpacing &= K >= 10.0 && K <= 60.0;
            const double M = S.LogNormalBounded(100.0, 1.0, 15.0, 3000.0);
            bMass &= M >= 15.0 && M <= 3000.0;
            const double P = S.LogNormalBounded(5.0e4, 2.0, 200.0, 5.0e8);
            bPopulation &= P >= 200.0 && P <= 5.0e8;
        }
        TestTrue(TEXT("Hill spacing stays in [10, 60]"), bSpacing);
        TestTrue(TEXT("giant mass stays in [15, 3000]"), bMass);
        TestTrue(TEXT("population stays in [200, 5e8]"), bPopulation);
    }

    // A copied stream is the same stream: all its state is one number.
    {
        FGenStream A(42);
        A.Normal(0.0, 1.0);
        FGenStream B = A;
        TestTrue(TEXT("a copied stream continues identically"), A.Normal(0.0, 1.0) == B.Normal(0.0, 1.0));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
