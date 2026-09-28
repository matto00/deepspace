#include "Atmosphere/PlanetAir.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Not a test of correctness but a run whose output is read, like
 * DeepSpace.Universe.Corpus, and beside it: the noon zenith from the ground
 * of every temperate world in the corpus's 10,000 systems (atmospheres
 * decision 2, the sky the mix weights are judged by), under the universe the
 * game would play, to Saved/procgen_corpus_skies.tsv for
 * Tools/procgen_corpus.py. Named outside DeepSpace. because it takes minutes
 * (each sky builds two columns of its world's table), so the default suite,
 * which every worktree runs behind one lock, never runs it: run it by name.
 *
 * It fails only if its header no longer fits the contract's sky_columns,
 * something is not a number, no sky was written, or the file cannot be
 * written.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCorpusSkiesTest,
    "Atmosphere.Full.CorpusSkies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace CorpusSkiesTestLocal
{
    /** DeepSpace.Universe.Corpus's CorpusSize: the same 10,000 systems. */
    constexpr int32 CorpusSize = 10000;

    /** The columns in the order the rows write them; the contract checks them. */
    const TCHAR* const WrittenColumns[] = {
        TEXT("sector_x"), TEXT("sector_y"), TEXT("sector_z"), TEXT("slot"), TEXT("planet"), TEXT("designation"),
        TEXT("sky_zenith_rgb"), TEXT("sky_zenith_saturation")};

    struct FTestWorld
    {
        UWorld* World = nullptr;

        explicit FTestWorld(const TCHAR* Name)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, Name);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
        }

        ~FTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };

    TArray<FString> SkyColumnsOfContract(FString& OutError)
    {
        TArray<FString> Result;
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/procgen_corpus_contract.json"));
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            OutError = TEXT("cannot read or parse ") + Path;
            return Result;
        }
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("sky_columns")))
        {
            Result.Add(Value->AsString());
        }
        return Result;
    }

    /** Six significant figures, as the corpus writes them. */
    FString Num(double Value, int32& NonFinite)
    {
        if (!std::isfinite(Value))
        {
            ++NonFinite;
        }
        return FString::Printf(TEXT("%.6g"), Value);
    }
}

bool FCorpusSkiesTest::RunTest(const FString& Parameters)
{
    using namespace CorpusSkiesTestLocal;

    // The contract first, so a header that no longer fits fails at once
    // rather than after minutes of skies.
    FString Error;
    const TArray<FString> Contract = SkyColumnsOfContract(Error);
    TArray<FString> Columns;
    for (const TCHAR* Column : WrittenColumns)
    {
        Columns.Add(Column);
    }
    if (!TestEqual(TEXT("the skies file writes the contract's sky_columns, in its order ") + Error,
            FString::Join(Columns, TEXT(",")), FString::Join(Contract, TEXT(","))))
    {
        return false;
    }

    // The universe the game would play, as DeepSpace.Universe.Corpus takes it.
    FTestWorld Test(TEXT("CorpusSkiesWorld"));
    const UUniverseSubsystem* Universe = Test.World->GetSubsystem<UUniverseSubsystem>();
    if (!TestNotNull(TEXT("the world has a universe"), Universe))
    {
        return false;
    }
    const FGalaxyGenerator Galaxy(Universe->GetRootSeed(), Universe->GetPriors());
    const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem());
    if (!TestTrue(TEXT("the universe has a home"), Home.IsSet()))
    {
        return false;
    }
    TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(Home->Stub.Position, FGalaxyGenerator::MaxSearchRadiusLy * UniverseUnits::CmPerLightYear);
    Near.SetNum(FMath::Min(Near.Num(), CorpusSize));

    int32 NonFinite = 0;
    int32 Skies = 0;
    FString Tsv = FString::Join(Columns, TEXT("\t")) + TEXT("\n");
    FString FirstRow;
    FVector3d FirstZenith = FVector3d::ZeroVector;
    for (const FStarSystemStub& Stub : Near)
    {
        const FStarSystem System = Galaxy.GenerateSystem(Stub);
        for (const FPlanet& Planet : System.Planets)
        {
            if (Planet.Kind != EPlanetKind::Terrestrial && Planet.Kind != EPlanetKind::Ocean)
            {
                continue;
            }
            const FVector3d Zenith = PlanetAir::NoonZenith(Planet, System.Star.TemperatureK);
            const FString Row = FString::Join(TArray<FString>{
                FString::Printf(TEXT("%lld"), static_cast<long long>(Stub.Id.Sector.X)),
                FString::Printf(TEXT("%lld"), static_cast<long long>(Stub.Id.Sector.Y)),
                FString::Printf(TEXT("%lld"), static_cast<long long>(Stub.Id.Sector.Z)),
                FString::FromInt(Stub.Id.Slot),
                FString::FromInt(Planet.Id.Planet),
                Planet.Designation,
                Num(Zenith.X, NonFinite) + TEXT(",") + Num(Zenith.Y, NonFinite) + TEXT(",") + Num(Zenith.Z, NonFinite),
                Num(PlanetAir::Saturation(Zenith), NonFinite)}, TEXT("\t"));
            if (Skies == 0)
            {
                FirstRow = Row;
                FirstZenith = Zenith;
            }
            Tsv += Row + TEXT("\n");
            ++Skies;
        }
    }
    TestTrue(FString::Printf(TEXT("%d temperate worlds have a sky"), Skies), Skies > 0);
    TestEqual(TEXT("nothing in the skies is NaN or infinite"), NonFinite, 0);
    TestTrue(FString::Printf(TEXT("the first sky is a colour, not black (%.4f, %.4f, %.4f)"), FirstZenith.X, FirstZenith.Y, FirstZenith.Z),
        FirstZenith.GetMax() > 0.0);

    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), TEXT("procgen_corpus_skies.tsv"));
    TestTrue(TEXT("the skies are written to ") + Path, FFileHelper::SaveStringToFile(Tsv, *Path));

    // Read back as the Python will: the header, and the first row whole.
    TArray<FString> Lines;
    FFileHelper::LoadFileToStringArray(Lines, *Path);
    TestEqual(TEXT("one line per sky, plus the header"), Lines.Num(), Skies + 1);
    TestTrue(TEXT("the first row reads back as written"), Lines.Num() > 1 && Lines[1] == FirstRow);

    AddInfo(FString::Printf(TEXT("%d skies from %d systems -> %s. Then: python3 Tools/procgen_corpus.py"), Skies, Near.Num(), *Path));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
