#include "Misc/AutomationTest.h"
#include "Tests/GenTestVectors.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/GenSeed.h"

#if WITH_DEV_AUTOMATION_TESTS

// A Label is a compile-time constant, or a purpose could be computed from
// something that changes. These are the known-value table's hash rows, held
// at compile time as well as at run time.
static_assert(GenSeed::Label("star") == 0xAEFD58191D95E091ull, "Label is FNV-1a 64");
static_assert(GenSeed::Mix(0) == 0xE220A8397B1DCDAFull, "Mix is SplitMix64's output");
static_assert(GenSeed::Derive(1, GenSeed::Label("system"), 0) == 0x07389B5FDEDF9306ull, "Derive folds index, purpose, parent");
static_assert(GenSeed::HashCoord(-1, 0, 0) == 0xFC042709560421DAull, "HashCoord is two's complement");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGenSeedTest,
    "DeepSpace.Universe.Seed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGenSeedTest::RunTest(const FString& Parameters)
{
    // Procgen's known-value table, typed in from the spec. If one of these
    // changes, every universe has changed; this test makes that a decision
    // somebody takes rather than an accident somebody commits.
    {
        TestEqual(TEXT("Mix(0)"), GenSeed::Mix(0), 0xE220A8397B1DCDAFull);
        TestEqual(TEXT("Mix(1)"), GenSeed::Mix(1), 0x910A2DEC89025CC1ull);
        TestEqual(TEXT("Label(\"star\")"), GenSeed::Label("star"), 0xAEFD58191D95E091ull);
        TestEqual(TEXT("Derive(1, Label(\"system\"), 0)"), GenSeed::Derive(1, GenSeed::Label("system"), 0), 0x07389B5FDEDF9306ull);
        TestEqual(TEXT("HashCoord((1,0,0))"), GenSeed::HashCoord(FInt64Vector(1, 0, 0)), 0xB18A02F46D8D86C3ull);
        TestEqual(TEXT("HashCoord((0,1,0))"), GenSeed::HashCoord(FInt64Vector(0, 1, 0)), 0x44E5B98100C67FB0ull);
        TestEqual(TEXT("HashCoord((-1,0,0))"), GenSeed::HashCoord(FInt64Vector(-1, 0, 0)), 0xFC042709560421DAull);
        TestEqual(TEXT("HashCoord((0,0,0))"), GenSeed::HashCoord(FInt64Vector(0, 0, 0)), 0x238275BC38FCBE91ull);
    }

    // LabelText is Label for text built at run time: the same hash.
    TestEqual(TEXT("LabelText agrees with Label"), GenSeed::LabelText(TEXT("sky.starfield")), GenSeed::Label("sky.starfield"));

    // The chain from a root to a system, by hand and through the generator
    // that uses it -- so the generator's wiring is pinned, not only the hash.
    {
        constexpr uint64 Root = 20260925;
        const uint64 Galaxy = GenSeed::Derive(Root, GenSeed::Label("galaxy"));
        const uint64 Sector = GenSeed::Derive(Galaxy, GenSeed::Label("sector"), GenSeed::HashCoord(FInt64Vector(0, 0, 0)));
        const uint64 Slot0 = GenSeed::Derive(Sector, GenSeed::Label("system"), 0);
        TestEqual(TEXT("root 20260925 -> galaxy"), Galaxy, 0x499B04106B52A25Full);
        TestEqual(TEXT("-> sector (0,0,0)"), Sector, 0xABDDDC1BF5D4C7FBull);
        TestEqual(TEXT("-> slot 0"), Slot0, 0x4F6049C72F9DF12Dull);

        const FGalaxyGenerator Generator(Root, FGenPriors{});
        TestEqual(TEXT("the generator's galaxy seed is the chain's"), Generator.GetGalaxySeed(), Galaxy);
        TestEqual(TEXT("the generator's sector seed is the chain's"), Generator.SectorSeed(FInt64Vector(0, 0, 0)), Sector);
        TestEqual(TEXT("the generator's system seed is the chain's"), Generator.SystemSeed(FSystemId{FInt64Vector(0, 0, 0), 0}), Slot0);
    }

    // Siblings are unrelated, and the sign of a coordinate matters.
    TestNotEqual(TEXT("(1,0,0) is not (-1,0,0)"), GenSeed::HashCoord(FInt64Vector(1, 0, 0)), GenSeed::HashCoord(FInt64Vector(-1, 0, 0)));
    TestNotEqual(TEXT("(1,0,0) is not (0,1,0)"), GenSeed::HashCoord(FInt64Vector(1, 0, 0)), GenSeed::HashCoord(FInt64Vector(0, 1, 0)));

    // Every seed row and the chain of the shared vectors file, which
    // Tools/test_rng.py holds the Python mirror to.
    FString Error;
    const TSharedPtr<FJsonObject> Vectors = GenTestVectors::Load(Error);
    if (!TestTrue(TEXT("rng_vectors.json loads: ") + Error, Vectors.IsValid()))
    {
        return false;
    }

    int32 Rows = 0;
    for (const TSharedPtr<FJsonValue>& Value : Vectors->GetArrayField(TEXT("seed")))
    {
        const TSharedPtr<FJsonObject>& Row = Value->AsObject();
        const FString Fn = Row->GetStringField(TEXT("fn"));
        const FString Expr = Row->GetStringField(TEXT("expr"));
        const TArray<TSharedPtr<FJsonValue>>& Args = Row->GetArrayField(TEXT("args"));
        const uint64 Expected = GenTestVectors::U64(Row->GetStringField(TEXT("value")));

        uint64 Actual = 0;
        if (Fn == TEXT("Mix"))
        {
            Actual = GenSeed::Mix(GenTestVectors::U64(Args[0]->AsString()));
        }
        else if (Fn == TEXT("Label"))
        {
            Actual = GenSeed::LabelText(Args[0]->AsString());
        }
        else if (Fn == TEXT("Derive"))
        {
            Actual = GenSeed::Derive(GenTestVectors::U64(Args[0]->AsString()), GenTestVectors::U64(Args[1]->AsString()),
                GenTestVectors::U64(Args[2]->AsString()));
        }
        else if (Fn == TEXT("HashCoord"))
        {
            Actual = GenSeed::HashCoord(FInt64Vector(
                static_cast<int64>(Args[0]->AsNumber()), static_cast<int64>(Args[1]->AsNumber()), static_cast<int64>(Args[2]->AsNumber())));
        }
        else
        {
            AddError(TEXT("unknown seed function in the vectors: ") + Fn);
            continue;
        }
        TestEqual(TEXT("vectors: ") + Expr, Actual, Expected);
        ++Rows;
    }
    TestTrue(TEXT("the vectors hold every seed row"), Rows >= 8);

    const TSharedPtr<FJsonObject> Chain = Vectors->GetObjectField(TEXT("chain"));
    const uint64 Root = GenTestVectors::U64(Chain->GetStringField(TEXT("root")));
    const FGalaxyGenerator Generator(Root, FGenPriors{});
    TestEqual(TEXT("vectors: the chain's root is 20260925"), Root, uint64(20260925));
    TestEqual(TEXT("vectors: galaxy"), Generator.GetGalaxySeed(), GenTestVectors::U64(Chain->GetStringField(TEXT("galaxy"))));
    for (const TSharedPtr<FJsonValue>& Value : Chain->GetArrayField(TEXT("sectors")))
    {
        const TSharedPtr<FJsonObject>& Row = Value->AsObject();
        const TArray<TSharedPtr<FJsonValue>>& Coord = Row->GetArrayField(TEXT("coord"));
        const FInt64Vector Sector(
            static_cast<int64>(Coord[0]->AsNumber()), static_cast<int64>(Coord[1]->AsNumber()), static_cast<int64>(Coord[2]->AsNumber()));
        const FString Name = FString::Printf(TEXT("vectors: sector (%lld,%lld,%lld)"),
            static_cast<long long>(Sector.X), static_cast<long long>(Sector.Y), static_cast<long long>(Sector.Z));

        const uint64 SectorSeed = Generator.SectorSeed(Sector);
        TestEqual(Name + TEXT(" seed"), SectorSeed, GenTestVectors::U64(Row->GetStringField(TEXT("seed"))));
        TestEqual(Name + TEXT(" count seed"), GenSeed::Derive(SectorSeed, GenSeed::Label("count")),
            GenTestVectors::U64(Row->GetStringField(TEXT("count_seed"))));
        TestEqual(Name + TEXT(" slot 0"), Generator.SystemSeed(FSystemId{Sector, 0}),
            GenTestVectors::U64(Row->GetStringField(TEXT("slot0"))));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
