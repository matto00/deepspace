#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/StringOutputDevice.h"
#include "Universe/GenPriors.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FProcGenPriorsTest,
    "DeepSpace.Universe.Priors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ProcGenPriorsTestLocal
{
    /** A game world that never begins play: the universe answers without it. */
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

    /** Whatever this test does to the config cache or the class default, the
     *  next test sees the ini as it is on disk. */
    struct FRestoreFromIni
    {
        ~FRestoreFromIni() { UProcGenPriorsConfig::ReloadFromIni(); }
    };

    /** The section the class itself reads, derived from the class rather
     *  than typed: a section header in DefaultGame.ini that does not match it
     *  is a section nothing reads. */
    FString Section()
    {
        return UProcGenPriorsConfig::StaticClass()->GetPathName();
    }

    /** The first field-for-field difference, or empty. */
    FString Difference(const FGenPriors& A, const FGenPriors& B)
    {
#define DS_DIFF_PRIOR(Name) \
        if (A.Name != B.Name) { return FString::Printf(TEXT("%s: %.17g vs %.17g"), TEXT(#Name), A.Name, B.Name); }
        DS_GEN_PRIORS(DS_DIFF_PRIOR)
#undef DS_DIFF_PRIOR
        return FString();
    }

    /** How many of the stubs have a sun of this class. */
    int32 CountClass(const TArray<FStarSystemStub>& Stubs, EStarClass Class)
    {
        int32 Count = 0;
        for (const FStarSystemStub& Stub : Stubs)
        {
            Count += Stub.Class == Class ? 1 : 0;
        }
        return Count;
    }
}

bool FProcGenPriorsTest::RunTest(const FString& Parameters)
{
    using namespace ProcGenPriorsTestLocal;
    FRestoreFromIni Restore;
    UClass* Class = UProcGenPriorsConfig::StaticClass();
    const FGenPriors CodeDefaults;

    // -- the mirror is whole: every prior a Config property, and nothing else --
    {
        int32 Mirrored = 0;
#define DS_CHECK_PROPERTY(Name)                                                                          \
        {                                                                                                \
            const FDoubleProperty* Property = FindFProperty<FDoubleProperty>(Class, TEXT(#Name));        \
            if (TestNotNull(TEXT(#Name " is a double property of the config"), Property))               \
            {                                                                                            \
                TestTrue(TEXT(#Name " is read from the ini"), Property->HasAnyPropertyFlags(CPF_Config)); \
                ++Mirrored;                                                                              \
            }                                                                                            \
        }
        DS_GEN_PRIORS(DS_CHECK_PROPERTY)
#undef DS_CHECK_PROPERTY
        TestEqual(TEXT("every prior is mirrored"), Mirrored, NumGenPriors);

        int32 ConfigProperties = 0;
        for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            ConfigProperties += It->HasAnyPropertyFlags(CPF_Config) ? 1 : 0;
        }
        TestEqual(TEXT("and the config reads nothing that is not a prior"), ConfigProperties, NumGenPriors);
    }

    // -- ToPriors carries each property to the field of its own name ----------
    // Every property is given a different value, so a crossed pair shows.
    {
        UProcGenPriorsConfig* Scratch = NewObject<UProcGenPriorsConfig>(GetTransientPackage());
        double Next = 1000.0;
#define DS_SET_PRIOR(Name) \
        FindFProperty<FDoubleProperty>(Class, TEXT(#Name))->SetPropertyValue_InContainer(Scratch, Next); Next += 1.0;
        DS_GEN_PRIORS(DS_SET_PRIOR)
#undef DS_SET_PRIOR

        const FGenPriors Carried = Scratch->ToPriors();
        double Expected = 1000.0;
#define DS_CHECK_CARRIED(Name) \
        TestEqual(TEXT("ToPriors carries " #Name), Carried.Name, Expected); Expected += 1.0;
        DS_GEN_PRIORS(DS_CHECK_CARRIED)
#undef DS_CHECK_CARRIED
    }

    // -- DefaultGame.ini: every prior a line, in the class's own section, ------
    // and every line the honest default (the developer's ruling, 2026-09-25).
    // Read from the config cache directly, not through the class: a section
    // header nothing matches leaves the class at FGenPriors{} too, which would
    // pass for the ini's values while no line of the ini was being read.
    {
        const FString SectionName = Section();
        TestEqual(TEXT("the class reads the section DefaultGame.ini names"), SectionName,
            FString(TEXT("/Script/DeepSpace.ProcGenPriorsConfig")));
        const UProcGenPriorsConfig* Defaults = GetDefault<UProcGenPriorsConfig>();

#define DS_CHECK_INI(Name)                                                                                 \
        {                                                                                                  \
            double FromIni = 0.0;                                                                          \
            if (TestTrue(TEXT(#Name " has a line in the priors section of DefaultGame.ini"),              \
                    GConfig->GetDouble(*SectionName, TEXT(#Name), FromIni, GGameIni)))                     \
            {                                                                                              \
                TestEqual(TEXT(#Name ": the ini ships the honest default"), FromIni, CodeDefaults.Name);  \
                TestEqual(TEXT(#Name ": the class default read the ini"), Defaults->Name, FromIni);       \
            }                                                                                              \
        }
        DS_GEN_PRIORS(DS_CHECK_INI)
#undef DS_CHECK_INI
    }

    FTestWorld Test(TEXT("ProcGenPriorsTestWorld"));
    UUniverseSubsystem* Universe = Test.World->GetSubsystem<UUniverseSubsystem>();
    if (!TestNotNull(TEXT("the world has a universe"), Universe))
    {
        return false;
    }
    const FString Asked = Difference(Universe->GetPriors(), GetDefault<UProcGenPriorsConfig>()->ToPriors());
    TestTrue(TEXT("the universe's priors are the config's: ") + Asked, Asked.IsEmpty());

    const FUniversePosition Origin;
    const double Radius = 20.0 * UniverseUnits::CmPerLightYear;

    // -- the ini reaches the generator ---------------------------------------
    // An ini whose every sun is blue, put into the config cache as a reload
    // would find it. The universe already alive in this world must answer
    // with blue suns and nothing else, having been told nothing.
    {
        const FString SectionName = Section();
        GConfig->SetDouble(*SectionName, TEXT("ClassWeightM"), 0.0, GGameIni);
        GConfig->SetDouble(*SectionName, TEXT("ClassWeightK"), 0.0, GGameIni);
        GConfig->SetDouble(*SectionName, TEXT("ClassWeightG"), 0.0, GGameIni);
        GConfig->SetDouble(*SectionName, TEXT("ClassWeightF"), 0.0, GGameIni);
        GConfig->SetDouble(*SectionName, TEXT("ClassWeightA"), 0.0, GGameIni);
        GConfig->SetDouble(*SectionName, TEXT("ClassWeightB"), 1.0, GGameIni);
        GetMutableDefault<UProcGenPriorsConfig>()->ReloadConfig();

        const TArray<FStarSystemStub> Blue = Universe->GetSystemsNear(Origin, Radius);
        TestTrue(TEXT("there are suns to look at"), Blue.Num() > 10);
        TestEqual(TEXT("an ini of blue suns gives a sky of blue suns"), CountClass(Blue, EStarClass::B), Blue.Num());
    }

    // -- ds.Universe.ReloadPriors re-reads the file, not the cache ------------
    // The blue edit above lives only in memory. A reload that re-read the
    // cache would keep it; one that re-reads DefaultGame.ini from disk loses
    // it, which is what a developer who has just saved the file needs.
    {
        FStringOutputDevice Out;
        Out.SetAutoEmitLineTerminator(true);
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Universe.ReloadPriors"), Out, Test.World);
        AddInfo(TEXT("ds.Universe.ReloadPriors:\n") + Out);

        const FString Back = Difference(Universe->GetPriors(), CodeDefaults);
        TestTrue(TEXT("after the reload the priors are the file's again: ") + Back, Back.IsEmpty());
        TestTrue(TEXT("and it says which moved"), Out.Contains(TEXT("ClassWeightB")) && Out.Contains(TEXT("ClassWeightM")));
        TestFalse(TEXT("and only those"), Out.Contains(TEXT("InhabitedChance")));

        const TArray<FStarSystemStub> Honest = Universe->GetSystemsNear(Origin, Radius);
        TestTrue(TEXT("and red dwarfs are most of the sky again"), CountClass(Honest, EStarClass::M) * 2 > Honest.Num());

        FStringOutputDevice Again;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Universe.ReloadPriors"), Again, Test.World);
        TestTrue(TEXT("a second reload has nothing to report"), Again.Contains(TEXT("already in use")));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
