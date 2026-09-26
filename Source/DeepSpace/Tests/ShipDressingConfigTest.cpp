#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ship/ShipDressing.h"
#include "Ship/ShipDressingConfig.h"
#include "Ship/ShipDressingRules.h"
#include "Ship/ShipDressingSubsystem.h"
#include "Tests/DressingTestFixtures.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDressingConfigTest,
    "DeepSpace.Ship.Dressing.Config",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipDressingConfigTestLocal
{
    /** Whatever this test does to the config cache or the class default, the
     *  next test sees the ini as it is on disk. */
    struct FRestoreFromIni
    {
        ~FRestoreFromIni() { UShipDressingConfig::ReloadFromIni(); }
    };

    /** Derived from the class, not typed: a section header that does not
     *  match it is a section nothing reads. */
    FString Section()
    {
        return UShipDressingConfig::StaticClass()->GetPathName();
    }

    /** Everything the rules decide with, as text: equal rules, equal text. */
    FString Fingerprint(const FShipDressingRules& R)
    {
        FString Out;
#define DS_PRINT_SCALAR(Name) Out += FString::Printf(TEXT(#Name "=%.17g "), R.Name);
        DS_DRESS_SCALARS(DS_PRINT_SCALAR)
#undef DS_PRINT_SCALAR
        Out += FString::Printf(TEXT("StackCap=%d Turns=%g,%g,%g,%g LivedIn=%g\n"), R.StackCap, R.TurnWeights[0], R.TurnWeights[1],
                               R.TurnWeights[2], R.TurnWeights[3], R.LivedIn);
        for (const FDressKind& K : R.Kinds)
        {
            Out += FString::Printf(TEXT("kind %s %.17g:"), *K.Kind.ToString(), K.Lambda);
            for (const FDressWeight& W : K.Mix)
            {
                Out += FString::Printf(TEXT(" %s=%.17g"), *W.Name.ToString(), W.Weight);
            }
            Out += TEXT("\n");
        }
        for (const FDressTemplate& T : R.Templates)
        {
            Out += FString::Printf(TEXT("template %s %d%d:"), *T.Name.ToString(), T.bStacks, T.bAlternates);
            for (const FDressPart& P : T.Parts)
            {
                Out += FString::Printf(TEXT(" [%d %s %s %s]"), static_cast<int32>(P.Mesh), *P.At.ToString(), *P.Size.ToString(), *P.Role.ToString());
            }
            Out += TEXT("\n");
        }
        for (const FDressWeight& C : R.Colours)
        {
            Out += FString::Printf(TEXT("colour %s=%.17g\n"), *C.Name.ToString(), C.Weight);
        }
        return Out;
    }

    /** Lines put into the config cache, as an ini edit would put them, and
     *  taken out again however the scope ends. */
    struct FIniLines
    {
        TArray<TPair<FString, TArray<FString>>> Was;

        void Set(const TCHAR* Key, const TArray<FString>& Values)
        {
            if (!Was.ContainsByPredicate([Key](const TPair<FString, TArray<FString>>& P) { return P.Key == Key; }))
            {
                TArray<FString> Old;
                GConfig->GetArray(*Section(), Key, Old, GGameIni);
                Was.Emplace(Key, Old);
            }
            GConfig->SetArray(*Section(), Key, Values, GGameIni);
        }

        void Set(const TCHAR* Key, const TCHAR* Value) { Set(Key, TArray<FString>{ Value }); }

        void PutBack()
        {
            for (const TPair<FString, TArray<FString>>& Line : Was)
            {
                if (Line.Value.IsEmpty())
                {
                    GConfig->RemoveKey(*Section(), *Line.Key, GGameIni);
                }
                else
                {
                    GConfig->SetArray(*Section(), *Line.Key, Line.Value, GGameIni);
                }
            }
            Was.Reset();
        }

        ~FIniLines() { PutBack(); }
    };

    const FDressKind* KindOf(const FShipDressingRules& Rules, const TCHAR* Kind)
    {
        return Rules.Kinds.FindByPredicate([Kind](const FDressKind& K) { return K.Kind == FName(Kind); });
    }
}

/**
 * The first tier of the dressing's tuning loop (lived-in decision 1c): every
 * scalar is an ini line of its own name; the tables are overlays by name, so
 * the ini lists only what is tuned; a deleted line falls back to its code
 * default; and a read Dress could not take is refused whole, with the line
 * named and the rules in use left as they were.
 */
bool FShipDressingConfigTest::RunTest(const FString& Parameters)
{
    using namespace ShipDressingConfigTestLocal;
    FRestoreFromIni Restore;
    UClass* Class = UShipDressingConfig::StaticClass();
    const FShipDressingRules CodeDefaults;

    // -- the mirror is whole ----------------------------------------------------
    {
        int32 Scalars = 0;
#define DS_CHECK_PROPERTY(Name)                                                                          \
        {                                                                                                \
            const FDoubleProperty* Property = FindFProperty<FDoubleProperty>(Class, TEXT(#Name));        \
            if (TestNotNull(TEXT(#Name " is a double property of the config"), Property))               \
            {                                                                                            \
                TestTrue(TEXT(#Name " is read from the ini"), Property->HasAnyPropertyFlags(CPF_Config)); \
            }                                                                                            \
            ++Scalars;                                                                                   \
        }
        DS_DRESS_SCALARS(DS_CHECK_PROPERTY)
#undef DS_CHECK_PROPERTY
        for (const TCHAR* Name : { TEXT("StackCap"), TEXT("TurnWeights"), TEXT("Kinds"), TEXT("Templates"), TEXT("Colours") })
        {
            const FProperty* Property = FindFProperty<FProperty>(Class, Name);
            TestTrue(FString::Printf(TEXT("%s is read from the ini"), Name), Property && Property->HasAnyPropertyFlags(CPF_Config));
        }
        int32 ConfigProperties = 0;
        for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            ConfigProperties += It->HasAnyPropertyFlags(CPF_Config) ? 1 : 0;
        }
        TestEqual(TEXT("and the config reads nothing else"), ConfigProperties, Scalars + 5);
    }

    // -- ToRules carries each property to the field of its own name ------------
    {
        UShipDressingConfig* Scratch = NewObject<UShipDressingConfig>(GetTransientPackage());
        Scratch->ResetToCodeDefaults();
        TestEqual(TEXT("reset, the config is exactly the code's rules"), Fingerprint(Scratch->ToRules()), Fingerprint(CodeDefaults));

        double Next = 1000.0;
#define DS_SET_SCALAR(Name) Scratch->Name = Next; Next += 1.0;
        DS_DRESS_SCALARS(DS_SET_SCALAR)
#undef DS_SET_SCALAR
        Scratch->StackCap = 7;
        Scratch->TurnWeights = { 1.0, 2.0, 3.0, 4.0 };
        const FShipDressingRules Carried = Scratch->ToRules();
        double Expected = 1000.0;
#define DS_CHECK_CARRIED(Name) TestEqual(TEXT("ToRules carries " #Name), Carried.Name, Expected); Expected += 1.0;
        DS_DRESS_SCALARS(DS_CHECK_CARRIED)
#undef DS_CHECK_CARRIED
        TestEqual(TEXT("ToRules carries StackCap"), Carried.StackCap, 7);
        TestTrue(TEXT("ToRules carries the turn weights, in order"),
                 Carried.TurnWeights[0] == 1.0 && Carried.TurnWeights[1] == 2.0 && Carried.TurnWeights[2] == 3.0 && Carried.TurnWeights[3] == 4.0);
    }

    // -- the code defaults, and the shipped ini ----------------------------------
    {
        const TArray<FString> CodeRefusals = DressRuleDomain::Refusals(CodeDefaults);
        TestEqual(TEXT("the code defaults are inside the domain: ") + FString::Join(CodeRefusals, TEXT("; ")), CodeRefusals.Num(), 0);

        TestEqual(TEXT("the class reads the section DefaultGame.ini names"), Section(), FString(TEXT("/Script/DeepSpace.ShipDressingConfig")));
        FString Ini;
        FFileHelper::LoadFileToString(Ini, *FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultGame.ini")));
        TestTrue(TEXT("DefaultGame.ini has the dressing's section, so the tune has somewhere to go"),
                 Ini.Contains(TEXT("[/Script/DeepSpace.ShipDressingConfig]")));

        const UShipDressingConfig* Defaults = GetDefault<UShipDressingConfig>();
        TestEqual(TEXT("the shipped ini is taken: ") + FString::Join(Defaults->GetRefusals(), TEXT("; ")), Defaults->GetRefusals().Num(), 0);
        TestEqual(TEXT("and what it says is what dresses the ship"), Fingerprint(Defaults->GetRules()), Fingerprint(Defaults->ToRules()));
    }

    const FString FromFile = Fingerprint(GetDefault<UShipDressingConfig>()->GetRules());
    const FShipDressingRules FileRules = GetDefault<UShipDressingConfig>()->GetRules();

    // -- rows overlay by name, scalars by line ----------------------------------
    {
        FIniLines Lines;
        Lines.Set(TEXT("BackA"), TEXT("5.5"));
        Lines.Set(TEXT("TurnWeights"), { TEXT("4"), TEXT("3"), TEXT("2"), TEXT("1") });
        Lines.Set(TEXT("Kinds"), {
            TEXT("(Kind=\"desk.top\",Lambda=0.5)"),
            TEXT("(Kind=\"counter.top\",Lambda=4,Mix=((Name=\"kettle\",Weight=1)))"),
            TEXT("(Kind=\"bunk_shelf.top\",Lambda=1,Mix=((Name=\"mug\",Weight=1)))"),
        });
        Lines.Set(TEXT("Templates"), {
            TEXT("(Name=\"kettle\",Parts=((Mesh=Cylinder,At=(X=0,Y=0,Z=10),Size=(X=16,Y=16,Z=20),Role=\"metal\")))"),
            TEXT("(Name=\"plate\",Parts=((Mesh=Chamfer,At=(X=0,Y=0,Z=1.5),Size=(X=24,Y=24,Z=3),Role=\"ceramic\")))"),
        });
        Lines.Set(TEXT("Colours"), TEXT("(Name=\"navy\",Weight=9)"));

        const TArray<FString> Refused = UShipDressingConfig::ApplyConfigCache();
        TestEqual(TEXT("a tune inside the domain is taken: ") + FString::Join(Refused, TEXT("; ")), Refused.Num(), 0);
        const FShipDressingRules Tuned = GetDefault<UShipDressingConfig>()->GetRules();

        TestEqual(TEXT("a scalar line is read"), Tuned.BackA, 5.5);
        TestEqual(TEXT("and the scalars it does not name are the code's"), Tuned.BackB, CodeDefaults.BackB);
        TestTrue(TEXT("four turn lines are the four quarter turns, in order"),
                 Tuned.TurnWeights[0] == 4.0 && Tuned.TurnWeights[1] == 3.0 && Tuned.TurnWeights[2] == 2.0 && Tuned.TurnWeights[3] == 1.0);

        const FDressKind* Desk = KindOf(Tuned, TEXT("desk.top"));
        TestTrue(TEXT("a kind row moves its kind's mean"), Desk && Desk->Lambda == 0.5);
        TestTrue(TEXT("and a row with no mix keeps the code's mix"), Desk && Desk->Mix.Num() == KindOf(CodeDefaults, TEXT("desk.top"))->Mix.Num()
                                                                        && Desk->Mix[0].Name == KindOf(CodeDefaults, TEXT("desk.top"))->Mix[0].Name);
        const FDressKind* Counter = KindOf(Tuned, TEXT("counter.top"));
        TestTrue(TEXT("a row with a mix replaces it whole: the counter holds kettles"),
                 Counter && Counter->Mix.Num() == 1 && Counter->Mix[0].Name == FName(TEXT("kettle")));
        TestTrue(TEXT("a kind the code has not got is added"), KindOf(Tuned, TEXT("bunk_shelf.top")) != nullptr);
        TestEqual(TEXT("and the kinds replaced are replaced, not doubled"), Tuned.Kinds.Num(), CodeDefaults.Kinds.Num() + 1);
        TestEqual(TEXT("the kinds the ini does not name are the code's"), KindOf(Tuned, TEXT("galley_table.top"))->Lambda,
                  KindOf(CodeDefaults, TEXT("galley_table.top"))->Lambda);

        const FDressTemplate* Kettle = Tuned.FindTemplate(TEXT("kettle"));
        TestTrue(TEXT("a new template is a whole new thing to leave about, from the ini alone"),
                 Kettle && Kettle->Height() == 20.0 && Kettle->Parts.Num() == 1 && Kettle->Parts[0].Mesh == EDressMesh::Cylinder);
        const FDressTemplate* Plate = Tuned.FindTemplate(TEXT("plate"));
        TestTrue(TEXT("a template row replaces the code's of that name"),
                 Plate && Plate->Height() == 3.0 && Plate->Parts[0].Mesh == EDressMesh::Chamfer);
        TestEqual(TEXT("and the catalogue is one longer, not two"), Tuned.Templates.Num(), CodeDefaults.Templates.Num() + 1);
        TestEqual(TEXT("the templates it does not name are the code's"), Tuned.FindTemplate(TEXT("mug"))->Height(),
                  CodeDefaults.FindTemplate(TEXT("mug"))->Height());

        const FDressWeight* Navy = Tuned.Colours.FindByPredicate([](const FDressWeight& C) { return C.Name == FName(TEXT("navy")); });
        TestTrue(TEXT("a colour row replaces its weight"), Navy && Navy->Weight == 9.0);
        TestEqual(TEXT("and leaves the palette its size"), Tuned.Colours.Num(), CodeDefaults.Colours.Num());

        // What dresses: the subsystem asks the config at every dress.
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShipDressingConfigTestWorld"));
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        if (const UShipDressingSubsystem* Dressing = World->GetSubsystem<UShipDressingSubsystem>())
        {
            const TArray<FDressSurface> Hauler = DressingFixtures::Hauler();
            const int32 CounterAt = Hauler.IndexOfByPredicate([](const FDressSurface& S) { return S.Kind == FName(TEXT("counter.top")); });
            int32 Kettles = 0, Other = 0;
            for (int32 Seed = 0; Seed < 20; ++Seed)
            {
                for (const FDressItem& Item : ShipDressing::Dress(Hauler, DressingFixtures::HaulerKeepOut(), ShipDressing::DressSeed(Seed), Dressing->GetRules()))
                {
                    if (Item.Surface == CounterAt)
                    {
                        (Item.Template == FName(TEXT("kettle")) ? Kettles : Other) += 1;
                    }
                }
            }
            TestTrue(FString::Printf(TEXT("the subsystem dresses with the tune: the counter has kettles and nothing else (%d, %d)"), Kettles, Other),
                     Kettles > 0 && Other == 0);
        }
        else
        {
            AddError(TEXT("a game world has a dressing subsystem"));
        }
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    }

    // -- a deleted line falls back to its code default ---------------------------
    // As it would at start-up, not to whatever the last read left: the lines
    // above are gone from the cache now.
    {
        const TArray<FString> Refused = UShipDressingConfig::ApplyConfigCache();
        TestEqual(TEXT("with the tune's lines gone, the ini is taken"), Refused.Num(), 0);
        TestEqual(TEXT("and the rules are the file's again, not the tune's"), Fingerprint(GetDefault<UShipDressingConfig>()->GetRules()), FromFile);
    }

    // -- outside the domain: refused whole, and the ship goes on ---------------
    {
        AddExpectedMessagePlain(TEXT("ShipDressingConfig] refused: "), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);

        struct FOutOfDomain
        {
            const TCHAR* Named;
            const TCHAR* Key;
            TArray<FString> Values;
        };
        const FOutOfDomain Cases[] = {
            { TEXT("BackA"), TEXT("BackA"), { TEXT("0") } },
            { TEXT("AlongUseB"), TEXT("AlongUseB"), { TEXT("nan") } },
            { TEXT("StackChance"), TEXT("StackChance"), { TEXT("1.5") } },
            { TEXT("StackCap"), TEXT("StackCap"), { TEXT("0") } },
            { TEXT("StackCap"), TEXT("StackCap"), { TEXT("40") } },
            { TEXT("ReplacedBelow"), TEXT("ReplacedBelow"), { TEXT("0.95") } },
            { TEXT("TurnWeights"), TEXT("TurnWeights"), { TEXT("0"), TEXT("0"), TEXT("0"), TEXT("0") } },
            { TEXT("TurnWeights"), TEXT("TurnWeights"), { TEXT("1"), TEXT("1"), TEXT("1") } },
            { TEXT("TurnWeights[2]"), TEXT("TurnWeights"), { TEXT("1"), TEXT("1"), TEXT("-1"), TEXT("1") } },
            { TEXT("desk.top Lambda"), TEXT("Kinds"), { TEXT("(Kind=\"desk.top\",Lambda=40)") } },
            { TEXT("desk.top Lambda"), TEXT("Kinds"), { TEXT("(Kind=\"desk.top\",Lambda=-1)") } },
            { TEXT("mugg"), TEXT("Kinds"), { TEXT("(Kind=\"desk.top\",Lambda=3,Mix=((Name=\"mugg\",Weight=1)))") } },
            { TEXT("book's weight"), TEXT("Kinds"), { TEXT("(Kind=\"desk.top\",Lambda=3,Mix=((Name=\"book\",Weight=-2)))") } },
            { TEXT("Sphere"), TEXT("Templates"), { TEXT("(Name=\"mug\",Parts=((Mesh=Sphere,At=(X=0,Y=0,Z=5),Size=(X=8,Y=8,Z=10),Role=\"ceramic\")))") } },
            { TEXT("mug: its lowest part"), TEXT("Templates"), { TEXT("(Name=\"mug\",Parts=((Mesh=Cylinder,At=(X=0,Y=0,Z=0),Size=(X=8,Y=8,Z=10),Role=\"ceramic\")))") } },
            { TEXT("mug: its lowest part"), TEXT("Templates"), { TEXT("(Name=\"mug\",Parts=((Mesh=Cylinder,At=(X=0,Y=0,Z=9),Size=(X=8,Y=8,Z=10),Role=\"ceramic\")))") } },
            { TEXT("mug part 0"), TEXT("Templates"), { TEXT("(Name=\"mug\",Parts=((Mesh=Cylinder,At=(X=0,Y=0,Z=0),Size=(X=8,Y=0,Z=0),Role=\"ceramic\")))") } },
            { TEXT("mug part 0: has no Role"), TEXT("Templates"), { TEXT("(Name=\"mug\",Parts=((Mesh=Cylinder,At=(X=0,Y=0,Z=5),Size=(X=8,Y=8,Z=10))))") } },
            { TEXT("mug: has no Parts"), TEXT("Templates"), { TEXT("(Name=\"mug\")") } },
            { TEXT("Colours"), TEXT("Colours"), { TEXT("(Name=\"olive\",Weight=0)"), TEXT("(Name=\"navy\",Weight=0)"), TEXT("(Name=\"rust\",Weight=0)"),
                                                  TEXT("(Name=\"ochre\",Weight=0)") } },
        };
        for (const FOutOfDomain& Case : Cases)
        {
            const FString Label = FString::Printf(TEXT("%s=%s"), Case.Key, *FString::Join(Case.Values, TEXT(" / ")));
            FIniLines Lines;
            Lines.Set(Case.Key, Case.Values);
            const TArray<FString> Refused = UShipDressingConfig::ApplyConfigCache();
            const FString Why = FString::Join(Refused, TEXT("; "));
            TestTrue(FString::Printf(TEXT("%s is refused, naming %s (said: %s)"), *Label, Case.Named, *Why), !Refused.IsEmpty() && Why.Contains(Case.Named));
            TestEqual(Label + TEXT(": the rules in use are the file's still"), Fingerprint(GetDefault<UShipDressingConfig>()->GetRules()), FromFile);

            // And Dress can still dress with them: nothing half-applied reached it.
            TestTrue(Label + TEXT(": the ship is still dressed"),
                     ShipDressing::Dress(DressingFixtures::Hauler(), DressingFixtures::HaulerKeepOut(), ShipDressing::DressSeed(1),
                                         GetDefault<UShipDressingConfig>()->GetRules()).Num() > 0);

            Lines.PutBack();
            const TArray<FString> Put = UShipDressingConfig::ApplyConfigCache();
            TestEqual(Label + TEXT(" put back: taken again"), Put.Num(), 0);
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
