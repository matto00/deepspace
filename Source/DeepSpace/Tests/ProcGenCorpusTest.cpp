#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Universe/AirFacts.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/StarSystem.h"
#include "Universe/SystemDescription.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Not a test of correctness -- DeepSpace.Universe.SystemGeneration is that --
 * but a run whose output is read. It writes, under the universe the game
 * would play (the ini's seed, or -UniverseSeed=, and the ini's priors):
 *
 *   Saved/procgen_corpus.tsv    the 10,000 systems nearest home, one row per
 *                               planet, for Tools/procgen_corpus.py
 *   Saved/procgen_describe.txt  home, its twelve nearest neighbours, and the
 *                               homes of seeds 1-12, as text
 *
 * The question both answer is whether the systems read as *places* or as
 * *rolls* (procgen spec, "What needs eyes"). Statistics can say a prior is
 * plausible; only reading says whether a system is worth flying to.
 *
 * It fails only if something is not a number, the files cannot be written,
 * or the file no longer fits Tools/procgen_corpus_contract.json -- the
 * columns the Python reads by name.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProcGenCorpusTest,
    "DeepSpace.Universe.Corpus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ProcGenCorpusTestLocal
{
    /** The nearest this many to home: the neighbourhood a player can reach,
     *  not a cube of the galaxy nobody will visit. About 85 ly at the
     *  default density, inside FGalaxyGenerator's 100 ly search cap. */
    constexpr int32 CorpusSize = 10000;
    constexpr int32 DescribedNeighbours = 12;
    constexpr uint64 DescribedSeeds = 12;

    /** The columns in the order SystemColumns and PlanetColumns write them.
     *  Written here, not read from the contract, so that the contract checks
     *  this file rather than dictating a header the cells might not follow. */
    const TCHAR* const WrittenColumns[] = {
        TEXT("system"), TEXT("sector_x"), TEXT("sector_y"), TEXT("sector_z"), TEXT("slot"), TEXT("distance_ly"),
        TEXT("star_class"), TEXT("star_mass_solar"), TEXT("star_luminosity_solar"), TEXT("star_temperature_k"),
        TEXT("habitable_inner_au"), TEXT("habitable_outer_au"), TEXT("frost_line_au"), TEXT("planet_count"),
        TEXT("planet"), TEXT("designation"), TEXT("given_name"), TEXT("kind"),
        TEXT("semi_major_axis_au"), TEXT("mass_earth"), TEXT("radius_earth"), TEXT("equilibrium_k"), TEXT("population"), TEXT("surface_gravity_g"), TEXT("relief_km"),
        TEXT("air_mix"), TEXT("surface_pressure_bar"), TEXT("scale_height_km"), TEXT("nadir_tau_450")};

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

    TSharedPtr<FJsonObject> LoadContract(FString& OutError)
    {
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/procgen_corpus_contract.json"));
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Path))
        {
            OutError = TEXT("cannot read ") + Path;
            return nullptr;
        }
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            OutError = TEXT("cannot parse ") + Path;
            return nullptr;
        }
        return Root;
    }

    TArray<FString> Strings(const TSharedPtr<FJsonObject>& Contract, const TCHAR* Field)
    {
        TArray<FString> Result;
        for (const TSharedPtr<FJsonValue>& Value : Contract->GetArrayField(Field))
        {
            Result.Add(Value->AsString());
        }
        return Result;
    }

    /** Six significant figures: enough to histogram, few enough to read. */
    FString Num(double Value, int32& NonFinite)
    {
        if (!std::isfinite(Value))
        {
            ++NonFinite;
        }
        return FString::Printf(TEXT("%.6g"), Value);
    }

    /** The row's system columns, everything up to planet_count inclusive. */
    FString SystemColumns(const FStarSystem& System, double DistanceLy, int32& NonFinite)
    {
        const FStar& Star = System.Star;
        const FSystemId& Id = System.Stub.Id;
        return FString::Join(TArray<FString>{
            System.Stub.Name,
            FString::Printf(TEXT("%lld"), static_cast<long long>(Id.Sector.X)),
            FString::Printf(TEXT("%lld"), static_cast<long long>(Id.Sector.Y)),
            FString::Printf(TEXT("%lld"), static_cast<long long>(Id.Sector.Z)),
            FString::FromInt(Id.Slot),
            Num(DistanceLy, NonFinite),
            SystemDescription::ClassName(Star.Class),
            Num(Star.MassSolar, NonFinite),
            Num(Star.LuminositySolar, NonFinite),
            Num(Star.TemperatureK, NonFinite),
            Num(Star.HabitableInnerAU, NonFinite),
            Num(Star.HabitableOuterAU, NonFinite),
            Num(Star.FrostLineAU, NonFinite),
            FString::FromInt(System.Planets.Num())}, TEXT("\t"));
    }

    FString PlanetColumns(const FPlanet& Planet, int32& NonFinite)
    {
        return FString::Join(TArray<FString>{
            FString::FromInt(Planet.Id.Planet),
            Planet.Designation,
            Planet.GivenName,
            SystemDescription::KindName(Planet.Kind),
            Num(Planet.SemiMajorAxisAU, NonFinite),
            Num(Planet.MassEarth, NonFinite),
            Num(Planet.RadiusEarth, NonFinite),
            Num(Planet.EquilibriumK, NonFinite),
            Num(Planet.Population, NonFinite),
            Num(Planet.SurfaceGravityEarth(), NonFinite),
            Num(Planet.ReliefKm, NonFinite),
            AirFacts::Name(Planet.AirMix),
            Num(Planet.SurfacePressureBar, NonFinite),
            Num(AirFacts::ScaleHeightKm(Planet.AirMix, Planet.EquilibriumK, Planet.SurfaceGravityEarth()), NonFinite),
            Num(AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, Planet.SurfaceGravityEarth()), NonFinite)}, TEXT("\t"));
    }

    /** What every column of one row must hold, by column name, worked out
     *  here from the system rather than read from the writer: the Python reads
     *  by name, so a cell under the wrong header, or a quantity in the wrong
     *  unit, would be read as something else without a complaint. Planet -1
     *  is an empty system's only row. */
    TMap<FString, FString> ExpectedRow(const FStarSystem& System, double DistanceLy, int32 PlanetIndex)
    {
        const FStar& Star = System.Star;
        const bool bAny = System.Planets.IsValidIndex(PlanetIndex);
        const FPlanet Planet = bAny ? System.Planets[PlanetIndex] : FPlanet();
        const auto G = [](double Value) { return FString::Printf(TEXT("%.6g"), Value); };
        return {
            {TEXT("system"), System.Stub.Name},
            {TEXT("sector_x"), FString::Printf(TEXT("%lld"), static_cast<long long>(System.Stub.Id.Sector.X))},
            {TEXT("sector_y"), FString::Printf(TEXT("%lld"), static_cast<long long>(System.Stub.Id.Sector.Y))},
            {TEXT("sector_z"), FString::Printf(TEXT("%lld"), static_cast<long long>(System.Stub.Id.Sector.Z))},
            {TEXT("slot"), FString::FromInt(System.Stub.Id.Slot)},
            {TEXT("distance_ly"), G(DistanceLy)},
            {TEXT("star_class"), SystemDescription::ClassName(Star.Class)},
            {TEXT("star_mass_solar"), G(Star.MassSolar)},
            {TEXT("star_luminosity_solar"), G(Star.LuminositySolar)},
            {TEXT("star_temperature_k"), G(Star.TemperatureK)},
            {TEXT("habitable_inner_au"), G(Star.HabitableInnerAU)},
            {TEXT("habitable_outer_au"), G(Star.HabitableOuterAU)},
            {TEXT("frost_line_au"), G(Star.FrostLineAU)},
            {TEXT("planet_count"), FString::FromInt(System.Planets.Num())},
            {TEXT("planet"), FString::FromInt(bAny ? PlanetIndex : -1)},
            {TEXT("designation"), bAny ? Planet.Designation : FString()},
            {TEXT("given_name"), bAny ? Planet.GivenName : FString()},
            {TEXT("kind"), bAny ? SystemDescription::KindName(Planet.Kind) : TEXT("")},
            {TEXT("semi_major_axis_au"), bAny ? G(Planet.SemiMajorAxisAU) : FString()},
            {TEXT("mass_earth"), bAny ? G(Planet.MassEarth) : FString()},
            {TEXT("radius_earth"), bAny ? G(Planet.RadiusEarth) : FString()},
            {TEXT("equilibrium_k"), bAny ? G(Planet.EquilibriumK) : FString()},
            {TEXT("population"), bAny ? G(Planet.Population) : FString()},
            {TEXT("surface_gravity_g"), bAny ? G(Planet.SurfaceGravityEarth()) : FString()},
            {TEXT("relief_km"), bAny ? G(Planet.ReliefKm) : FString()},
            {TEXT("air_mix"), bAny ? FString(AirFacts::Name(Planet.AirMix)) : FString()},
            {TEXT("surface_pressure_bar"), bAny ? G(Planet.SurfacePressureBar) : FString()},
            {TEXT("scale_height_km"), bAny ? G(AirFacts::ScaleHeightKm(Planet.AirMix, Planet.EquilibriumK, Planet.SurfaceGravityEarth())) : FString()},
            {TEXT("nadir_tau_450"), bAny ? G(AirFacts::NadirTau450(Planet.AirMix, Planet.SurfacePressureBar, Planet.SurfaceGravityEarth())) : FString()}};
    }

    /** One row a TSV line must be, to be read. */
    struct FRowToCheck
    {
        FString What;
        int32 Line = 0;
        int32 System = 0;
        int32 Planet = -1;
    };

    /** The planet columns of a system with no planets: planet -1, the rest
     *  empty, so the system is still one row and a count of rows per system
     *  still sees it. */
    FString NoPlanetColumns()
    {
        return TEXT("-1\t\t\t\t\t\t\t\t\t\t\t\t\t\t");
    }
}

bool FProcGenCorpusTest::RunTest(const FString& Parameters)
{
    using namespace ProcGenCorpusTestLocal;

    FString Error;
    const TSharedPtr<FJsonObject> Contract = LoadContract(Error);
    if (!TestTrue(TEXT("procgen_corpus_contract.json loads: ") + Error, Contract.IsValid()))
    {
        return false;
    }
    TArray<FString> Columns;
    for (const TCHAR* Column : WrittenColumns)
    {
        Columns.Add(Column);
    }
    TestEqual(TEXT("the corpus writes the contract's columns, in its order"),
        FString::Join(Columns, TEXT(",")), FString::Join(Strings(Contract, TEXT("columns")), TEXT(",")));

    // The words the Python counts by are the words the game prints.
    {
        const TArray<FString> Classes = Strings(Contract, TEXT("classes"));
        TestEqual(TEXT("the contract names every star class"), Classes.Num(), NumStarClasses);
        for (int32 I = 0; I < FMath::Min(Classes.Num(), NumStarClasses); ++I)
        {
            TestEqual(TEXT("star class word"), FString(SystemDescription::ClassName(static_cast<EStarClass>(I))), Classes[I]);
        }
        const TArray<FString> Kinds = Strings(Contract, TEXT("kinds"));
        const EPlanetKind AllKinds[] = {EPlanetKind::Barren, EPlanetKind::Terrestrial, EPlanetKind::Ocean, EPlanetKind::Ice, EPlanetKind::GasGiant};
        TestEqual(TEXT("the contract names every planet kind"), Kinds.Num(), int32(UE_ARRAY_COUNT(AllKinds)));
        for (int32 I = 0; I < FMath::Min(Kinds.Num(), int32(UE_ARRAY_COUNT(AllKinds))); ++I)
        {
            TestEqual(TEXT("planet kind word"), FString(SystemDescription::KindName(AllKinds[I])), Kinds[I]);
        }
    }

    // The universe the game would play: its seed resolved as Initialize
    // resolves it, and the ini's priors.
    FTestWorld Test(TEXT("ProcGenCorpusWorld"));
    const UUniverseSubsystem* Universe = Test.World->GetSubsystem<UUniverseSubsystem>();
    if (!TestNotNull(TEXT("the world has a universe"), Universe))
    {
        return false;
    }
    const uint64 Seed = Universe->GetRootSeed();
    const FGenPriors Priors = Universe->GetPriors();
    const FGalaxyGenerator Galaxy(Seed, Priors);
    const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem());
    if (!TestTrue(TEXT("the universe has a home"), Home.IsSet()))
    {
        return false;
    }

    TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(Home->Stub.Position, FGalaxyGenerator::MaxSearchRadiusLy * UniverseUnits::CmPerLightYear);
    TestTrue(FString::Printf(TEXT("%d systems within %.0f ly of home, enough for the corpus"), Near.Num(), FGalaxyGenerator::MaxSearchRadiusLy),
        Near.Num() >= CorpusSize);
    if (Near.Num() > CorpusSize)
    {
        Near.SetNum(CorpusSize);
    }

    // -- the TSV ---------------------------------------------------------------
    int32 NonFinite = 0;
    int32 Rows = 0;
    // Rows read back by name: home's first, the nearest neighbour's first --
    // the first with a distance that is not zero whatever its unit -- the
    // furthest system's last, and the first inhabited world's, which is the
    // first row where given_name and population are not empty or zero.
    TArray<FRowToCheck> ToCheck;
    FString Tsv = FString::Join(Columns, TEXT("\t")) + TEXT("\n");
    Tsv.Reserve(Near.Num() * 4 * 160);
    for (int32 S = 0; S < Near.Num(); ++S)
    {
        const FStarSystemStub& Stub = Near[S];
        const FStarSystem System = Galaxy.GenerateSystem(Stub);
        const double DistanceLy = Home->Stub.Position.DistanceTo(Stub.Position) / UniverseUnits::CmPerLightYear;
        const FString Prefix = SystemColumns(System, DistanceLy, NonFinite) + TEXT("\t");
        if (S == 0 || S == 1)
        {
            ToCheck.Add({S == 0 ? TEXT("home") : TEXT("the nearest neighbour"), Rows + 1, S, System.Planets.IsEmpty() ? -1 : 0});
        }
        if (System.Planets.IsEmpty())
        {
            Tsv += Prefix + NoPlanetColumns() + TEXT("\n");
            ++Rows;
        }
        for (const FPlanet& Planet : System.Planets)
        {
            if (Planet.Population > 0.0 && !ToCheck.ContainsByPredicate([](const FRowToCheck& Row) { return Row.What.StartsWith(TEXT("the first inhabited")); }))
            {
                ToCheck.Add({TEXT("the first inhabited world"), Rows + 1, S, Planet.Id.Planet});
            }
            Tsv += Prefix + PlanetColumns(Planet, NonFinite) + TEXT("\n");
            ++Rows;
        }
        if (S == Near.Num() - 1)
        {
            ToCheck.Add({TEXT("the furthest system"), Rows, S, System.Planets.Num() - 1});
        }
    }
    TestTrue(TEXT("the corpus has somebody living in it, so the population column is read somewhere it is not zero"),
        ToCheck.ContainsByPredicate([](const FRowToCheck& Row) { return Row.What.StartsWith(TEXT("the first inhabited")); }));
    TestEqual(TEXT("nothing in the corpus is NaN or infinite"), NonFinite, 0);

    const FString TsvPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), TEXT("procgen_corpus.tsv"));
    TestTrue(TEXT("the corpus is written to ") + TsvPath, FFileHelper::SaveStringToFile(Tsv, *TsvPath));

    // Read back as the Python will: every line as wide as the header, and one
    // line per planet or per empty system.
    {
        TArray<FString> Lines;
        FFileHelper::LoadFileToStringArray(Lines, *TsvPath);
        TestEqual(TEXT("one line per planet or empty system, plus the header"), Lines.Num(), Rows + 1);
        const int32 PlanetColumn = Columns.IndexOfByKey(FString(TEXT("planet")));
        int32 Ragged = 0;
        int32 SystemsSeen = 0;
        for (int32 I = 1; I < Lines.Num(); ++I)
        {
            TArray<FString> Cells;
            Lines[I].ParseIntoArray(Cells, TEXT("\t"), /*InCullEmpty*/ false);
            Ragged += Cells.Num() == Columns.Num() ? 0 : 1;
            // Each system's first row is its planet 0, or its only row, -1.
            SystemsSeen += Cells.IsValidIndex(PlanetColumn) && (Cells[PlanetColumn] == TEXT("0") || Cells[PlanetColumn] == TEXT("-1")) ? 1 : 0;
        }
        TestEqual(TEXT("every line has the contract's columns"), Ragged, 0);
        TestEqual(TEXT("every system is in the file, the empty ones too"), SystemsSeen, Near.Num());

        // Every column of each row to check holds what its name says. The
        // expected distance is worked out again here, so a writer that used
        // another unit, or put the distance under another header, fails on
        // every row but home's, where it is zero either way.
        for (const FRowToCheck& Row : ToCheck)
        {
            if (!TestTrue(Row.What + TEXT("'s row is in the file"), Lines.IsValidIndex(Row.Line)))
            {
                continue;
            }
            TArray<FString> Cells;
            Lines[Row.Line].ParseIntoArray(Cells, TEXT("\t"), /*InCullEmpty*/ false);
            const FStarSystem System = Galaxy.GenerateSystem(Near[Row.System]);
            const double DistanceLy = Home->Stub.Position.DistanceTo(Near[Row.System].Position) / UniverseUnits::CmPerLightYear;
            if (Row.System > 0)
            {
                TestTrue(Row.What + TEXT(" is some way from home"), DistanceLy > 0.0);
            }
            const TMap<FString, FString> Expected = ExpectedRow(System, DistanceLy, Row.Planet);
            TestEqual(TEXT("every column has an expected value"), Expected.Num(), Columns.Num());
            for (int32 C = 0; C < Columns.Num() && C < Cells.Num(); ++C)
            {
                const FString* Want = Expected.Find(Columns[C]);
                TestTrue(FString::Printf(TEXT("%s's %s is '%s' (wrote '%s')"), *Row.What, *Columns[C], Want ? **Want : TEXT("?"), *Cells[C]),
                    Want && *Want == Cells[C]);
            }
        }
    }

    // -- the descriptions ------------------------------------------------------
    FString Describe = FString::Printf(
        TEXT("Universe 0x%016llX, the ini's priors. Read these as a player arriving would, and ask:\n")
        TEXT("do they read as places, or as rolls?\n\n")
        TEXT("== Home, and its %d nearest neighbours: what the chart offers first ==\n\n"),
        static_cast<unsigned long long>(Seed), DescribedNeighbours);
    Describe += SystemDescription::Describe(*Home) + TEXT("\n");
    for (int32 I = 1; I <= DescribedNeighbours && I < Near.Num(); ++I)
    {
        Describe += FString::Printf(TEXT("%.2f ly from home\n"), Home->Stub.Position.DistanceTo(Near[I].Position) / UniverseUnits::CmPerLightYear);
        Describe += SystemDescription::Describe(Galaxy.GenerateSystem(Near[I])) + TEXT("\n");
    }

    Describe += FString::Printf(TEXT("== The homes of seeds 1-%llu: how different a first system can be ==\n\n"),
        static_cast<unsigned long long>(DescribedSeeds));
    TArray<FString> OtherHomes;
    for (uint64 Other = 1; Other <= DescribedSeeds; ++Other)
    {
        const FGalaxyGenerator Elsewhere(Other, Priors);
        if (const TOptional<FStarSystemStub> Stub = Elsewhere.GenerateStub(Elsewhere.StartSystem()))
        {
            Describe += FString::Printf(TEXT("seed %llu\n"), static_cast<unsigned long long>(Other));
            Describe += SystemDescription::Describe(Elsewhere.GenerateSystem(*Stub)) + TEXT("\n");
            OtherHomes.Add(Stub->Name);
        }
    }
    TestEqual(TEXT("every seed has a home"), OtherHomes.Num(), int32(DescribedSeeds));

    const FString DescribePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), TEXT("procgen_describe.txt"));
    TestTrue(TEXT("the descriptions are written to ") + DescribePath, FFileHelper::SaveStringToFile(Describe, *DescribePath));
    TestTrue(TEXT("and name home"), Describe.Contains(Home->Stub.Name));

    AddInfo(FString::Printf(TEXT("%d systems, %d rows -> %s; descriptions -> %s. Then: python3 Tools/procgen_corpus.py"),
        Near.Num(), Rows, *TsvPath, *DescribePath));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
