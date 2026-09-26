#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipDressing.h"
#include "Ship/ShipDressingSubsystem.h"
#include "Tests/HaulerDressingMarkers.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The dressing end to end, on the hauler's real surfaces: every marker the
 * layout exports, spawned before play as the level build places them, and
 * the subsystem left to do what it does at world start. The pure core's
 * tests (DeepSpace.Ship.Dressing.*) hold the plan to its rules on fixtures;
 * these hold the ship the player walks into to them.
 */

namespace DressingHaulerTestLocal
{
    using namespace HaulerDressingMarkers;

    /** A console variable set for one scope and put back after. */
    struct FScopedCVar
    {
        IConsoleVariable* Var;
        FString Was;
        explicit FScopedCVar(const TCHAR* Name)
            : Var(IConsoleManager::Get().FindConsoleVariable(Name)), Was(Var ? Var->GetString() : FString()) {}
        void Set(const FString& Value) const { if (Var) { Var->Set(*Value, ECVF_SetByConsole); } }
        void Set(double Value) const { Set(FString::SanitizeFloat(Value)); }
        ~FScopedCVar() { Set(Was); }
    };

    /** A game world with the hauler's markers in it, spawned before play
     *  begins, and play begun: what pressing Play on L_Hauler gives the
     *  dressing, less the furniture it never reads. */
    struct FDressedHauler
    {
        UWorld* World = nullptr;
        UShipDressingSubsystem* Dressing = nullptr;
        int32 ClutterBeforePlay = INDEX_NONE;

        FDressedHauler(const TCHAR* Name, const FMarkers& Markers)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, Name);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            Dressing = World->GetSubsystem<UShipDressingSubsystem>();
            Spawn(World, Markers);
            ClutterBeforePlay = Clutter().Num();
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
            World->GetWorldSettings()->NotifyBeginPlay();
        }

        ~FDressedHauler()
        {
            if (World->HasBegunPlay())
            {
                World->EndPlay(EEndPlayReason::RemovedFromWorld);
            }
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }

        TArray<AActor*> Clutter() const
        {
            TArray<AActor*> Out;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->ActorHasTag(ShipDressingTags::ClutterTag))
                {
                    Out.Add(*It);
                }
            }
            return Out;
        }

        /** What the subsystem draws from right now: its own markers, seed
         *  and rules through the core. */
        TArray<FDressItem> Plan(TArray<FDressSurface>* OutSurfaces = nullptr) const
        {
            TArray<FDressSurface> Surfaces;
            TArray<FBox> KeepOut;
            Dressing->GatherSurfaces(Surfaces, KeepOut);
            TArray<FDressItem> Items = ShipDressing::Dress(Surfaces, KeepOut, Dressing->GetDressSeed(), Dressing->GetRules());
            if (OutSurfaces)
            {
                *OutSurfaces = MoveTemp(Surfaces);
            }
            return Items;
        }
    };

    /** Every instance as it is drawn: its mesh's measured box through its
     *  instance transform, world. Measured, never assumed, because the three
     *  meshes disagree about their pivots. */
    TArray<FBox> InstanceBoxes(const AActor* Clutter)
    {
        TArray<FBox> Out;
        if (!Clutter)
        {
            return Out;
        }
        TInlineComponentArray<UInstancedStaticMeshComponent*> Layers(Clutter);
        for (const UInstancedStaticMeshComponent* Layer : Layers)
        {
            const FBox MeshBox = Layer->GetStaticMesh()->GetBoundingBox();
            for (int32 I = 0; I < Layer->GetInstanceCount(); ++I)
            {
                FTransform Instance;
                Layer->GetInstanceTransform(I, Instance, /*bWorldSpace*/ true);
                Out.Add(MeshBox.TransformBy(Instance));
            }
        }
        return Out;
    }

    int32 PartCount(const TArray<FDressItem>& Plan, const FShipDressingRules& Rules)
    {
        int32 Parts = 0;
        for (const FDressItem& Item : Plan)
        {
            const FDressTemplate* Template = Rules.FindTemplate(Item.Template);
            Parts += Template ? Template->Parts.Num() : 0;
        }
        return Parts;
    }

    /** How many places things were left, piles counting once: what Poisson
     *  counts, and so what LivedIn scales. */
    int32 Piles(const TArray<FDressItem>& Plan)
    {
        int32 Count = 0;
        for (const FDressItem& Item : Plan)
        {
            Count += Item.StackIndex == 0;
        }
        return Count;
    }

    /** The mean number of piles the rules ask of these surfaces at LivedIn:
     *  the sum of their Poisson means. */
    double ExpectedPiles(const TArray<FDressSurface>& Surfaces, const FShipDressingRules& Rules, double LivedIn)
    {
        double Mean = 0.0;
        for (const FDressSurface& Surface : Surfaces)
        {
            if (const FDressKind* Kind = Rules.FindKind(Surface.Kind))
            {
                Mean += Kind->Lambda * LivedIn;
            }
        }
        return Mean;
    }

    bool Intersects2D(const FBox& Box, const FBox2D& Rect, double Tolerance)
    {
        return Box.Min.X < Rect.Max.X - Tolerance && Rect.Min.X < Box.Max.X - Tolerance
            && Box.Min.Y < Rect.Max.Y - Tolerance && Rect.Min.Y < Box.Max.Y - Tolerance;
    }

    bool InsideColumn(const FBox& Box, const FDressSurface& Surface, double Tolerance)
    {
        const FBox Local = Box.InverseTransformBy(Surface.ToWorld);
        return Local.Min.X >= -0.5 * Surface.Size.X - Tolerance && Local.Max.X <= 0.5 * Surface.Size.X + Tolerance
            && Local.Min.Y >= -0.5 * Surface.Size.Y - Tolerance && Local.Max.Y <= 0.5 * Surface.Size.Y + Tolerance
            && Local.Min.Z >= -Tolerance && Local.Max.Z <= Surface.Clear + Tolerance;
    }

    /** Roots to dress the ship as, beyond the world's own. Fixed, so every
     *  test is deterministic; several, so a check that happens to hold for
     *  one ship is not mistaken for one that holds for every ship. */
    constexpr int32 Roots[] = { 1, 2, 3, 7, 11, 42, 1234, 20260925 };

    bool LoadMarkers(FAutomationTestBase& Test, FMarkers& Out)
    {
        FString Error;
        const bool bLoaded = Load(Out, Error);
        if (!bLoaded)
        {
            Test.AddError(Error);
        }
        return bLoaded;
    }
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDressingHaulerWorldStartTest,
    "DeepSpace.Dressing.Hauler.WorldStart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The ship is dressed because play began, and by nothing else: no clutter
 * before BeginPlay, one Dress.Clutter actor after it, every marker the
 * layout exports found by its tag, and exactly the core's plan drawn. And
 * the same seed at a second world start is the same ship, instance for
 * instance -- two players aboard one universe see one set of mugs.
 */
bool FDressingHaulerWorldStartTest::RunTest(const FString& Parameters)
{
    using namespace DressingHaulerTestLocal;
    FMarkers Markers;
    if (!LoadMarkers(*this, Markers))
    {
        return false;
    }
    FScopedCVar LivedIn(TEXT("ds.Dress.LivedIn"));
    FScopedCVar Seed(TEXT("ds.Dress.Seed"));
    LivedIn.Set(1.0);
    Seed.Set(TEXT("-1"));

    TMap<FString, TArray<FTransform>> First;
    {
        FDressedHauler Ship(TEXT("DressingHaulerWorldStartA"), Markers);
        if (!TestNotNull(TEXT("a game world has a dressing subsystem"), Ship.Dressing))
        {
            return false;
        }
        TestEqual(TEXT("nothing is dressed before play begins"), Ship.ClutterBeforePlay, 0);
        const TArray<AActor*> Clutter = Ship.Clutter();
        TestEqual(TEXT("play begun, there is exactly one Dress.Clutter actor"), Clutter.Num(), 1);
        TestTrue(TEXT("and it is the subsystem's"), Clutter.Num() == 1 && Clutter[0] == Ship.Dressing->GetClutter());

        TArray<FDressSurface> Surfaces;
        TArray<FBox> KeepOut;
        Ship.Dressing->GatherSurfaces(Surfaces, KeepOut);
        TestEqual(TEXT("every surface the layout exports is found by its tag"), Surfaces.Num(), Markers.Surfaces.Num());
        TestEqual(TEXT("and every keep-out"), KeepOut.Num(), Markers.KeepOuts.Num());

        const TArray<FDressItem> Plan = ShipDressing::Dress(Markers.Surfaces, Markers.KeepOuts,
                                                            Ship.Dressing->GetDressSeed(), Ship.Dressing->GetRules());
        TSet<int32> Dressed;
        for (const FDressItem& Item : Plan)
        {
            Dressed.Add(Item.Surface);
        }
        AddInfo(FString::Printf(TEXT("World start: %d things on %d of %d surfaces, %d instances."), Plan.Num(), Dressed.Num(),
                                Markers.Surfaces.Num(), Ship.Dressing->GetInstanceCount()));
        TestTrue(TEXT("the ship is dressed: the plan for the real surfaces is not empty"), Plan.Num() > 0);
        TestTrue(TEXT("on more than one surface"), Dressed.Num() > 1);
        TestEqual(TEXT("one instance per part of everything the core plans for the layout's markers"),
                  Ship.Dressing->GetInstanceCount(), PartCount(Plan, Ship.Dressing->GetRules()));
        First = Drawing(Ship.Dressing->GetClutter());
    }

    {
        FDressedHauler Again(TEXT("DressingHaulerWorldStartB"), Markers);
        TestTrue(TEXT("the same seed at a second world start dresses the ship identically, instance for instance"),
                 !First.IsEmpty() && SameDrawing(First, Drawing(Again.Dressing->GetClutter())));
    }
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDressingHaulerSeedTest,
    "DeepSpace.Dressing.Hauler.Seed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * ds.Dress.Seed is another ship: set before play, the world starts dressed
 * as that root's universe would be, differently from the world's own and
 * from every other root tried, and identically to itself at another start.
 */
bool FDressingHaulerSeedTest::RunTest(const FString& Parameters)
{
    using namespace DressingHaulerTestLocal;
    FMarkers Markers;
    if (!LoadMarkers(*this, Markers))
    {
        return false;
    }
    FScopedCVar LivedIn(TEXT("ds.Dress.LivedIn"));
    FScopedCVar Seed(TEXT("ds.Dress.Seed"));
    LivedIn.Set(1.0);

    const auto Dress = [&](int32 Root, uint64* OutSeed = nullptr)
    {
        Seed.Set(FString::FromInt(Root));
        FDressedHauler Ship(*FString::Printf(TEXT("DressingHaulerSeed%d"), Root + 1), Markers);
        if (OutSeed)
        {
            *OutSeed = Ship.Dressing->GetDressSeed();
        }
        return Drawing(Ship.Dressing->GetClutter());
    };

    uint64 OwnSeed = 0;
    const TMap<FString, TArray<FTransform>> Own = Dress(-1, &OwnSeed);
    TArray<TMap<FString, TArray<FTransform>>> Others;
    for (const int32 Root : Roots)
    {
        uint64 Used = 0;
        Others.Add(Dress(Root, &Used));
        TestEqual(FString::Printf(TEXT("ds.Dress.Seed %d dresses from that root's dressing seed"), Root), Used,
                  ShipDressing::DressSeed(static_cast<uint64>(Root)));
    }

    int32 SameAsOwn = 0, SamePairs = 0;
    for (int32 I = 0; I < Others.Num(); ++I)
    {
        SameAsOwn += SameDrawing(Others[I], Own);
        for (int32 J = I + 1; J < Others.Num(); ++J)
        {
            SamePairs += SameDrawing(Others[I], Others[J]);
        }
    }
    // 20260925 is this universe's own root; set explicitly it is the same ship.
    const bool bOwnRootIsListed = OwnSeed == ShipDressing::DressSeed(20260925);
    TestEqual(TEXT("every other root dresses the ship differently from the world's own"), SameAsOwn, bOwnRootIsListed ? 1 : 0);
    TestEqual(TEXT("and from each other"), SamePairs, 0);
    TestTrue(TEXT("a root dresses the same ship at every world start"), SameDrawing(Dress(Roots[0]), Others[0]));
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDressingHaulerLivedInTest,
    "DeepSpace.Dressing.Hauler.LivedIn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * ds.Dress.LivedIn at 0.3, 1 and 2 -- freshly moved in, lived in, squalid --
 * each gives a count a person would believe, on every root tried. The
 * number of piles is Poisson with the sum of the surfaces' means, less what
 * full surfaces refuse: so each ship lies within four of its standard
 * deviations of that mean, and the average over the roots within two
 * standard errors above it and not far below; the three are in order, on
 * average and ship by ship; freshly moved in leaves some surface bare; and
 * squalid still leaves no surface past its cap.
 */
bool FDressingHaulerLivedInTest::RunTest(const FString& Parameters)
{
    using namespace DressingHaulerTestLocal;
    FMarkers Markers;
    if (!LoadMarkers(*this, Markers))
    {
        return false;
    }
    FScopedCVar LivedIn(TEXT("ds.Dress.LivedIn"));
    FScopedCVar Seed(TEXT("ds.Dress.Seed"));
    LivedIn.Set(1.0);
    Seed.Set(TEXT("-1"));

    FDressedHauler Ship(TEXT("DressingHaulerLivedIn"), Markers);
    const double Levels[] = { 0.3, 1.0, 2.0 };
    double MeanPiles[3] = { 0.0, 0.0, 0.0 };
    double MeanExpected[3] = { 0.0, 0.0, 0.0 };
    int32 OutOfOrder = 0, NoneBare = 0, PastCap = 0, Implausible = 0, Undrawn = 0;
    for (const int32 Root : Roots)
    {
        Seed.Set(FString::FromInt(Root));
        int32 Last = -1;
        for (int32 L = 0; L < 3; ++L)
        {
            LivedIn.Set(Levels[L]);    // redresses the running world in place
            TArray<FDressSurface> Surfaces;
            const TArray<FDressItem> Plan = Ship.Plan(&Surfaces);
            const FShipDressingRules Rules = Ship.Dressing->GetRules();
            Undrawn += Ship.Dressing->GetInstanceCount() != PartCount(Plan, Rules);

            const int32 Count = Piles(Plan);
            const double Expected = ExpectedPiles(Surfaces, Rules, Levels[L]);
            // Full surfaces refuse what will not fit; at squalid a shelf can
            // be out of room, so the floor drops by what the rules cannot
            // place, never the ceiling.
            const double Spread = 4.0 * FMath::Sqrt(Expected);
            const double Floor = FMath::Max(1.0, (L == 2 ? 0.75 : 1.0) * Expected - Spread);
            const bool bPlausible = Count >= Floor && Count <= Expected + Spread;
            Implausible += !bPlausible;
            if (!bPlausible || Root == Roots[0])
            {
                AddInfo(FString::Printf(TEXT("root %d, LivedIn %.1f: %d piles (%d things), expected %.1f, plausible %.1f..%.1f"),
                                        Root, Levels[L], Count, Plan.Num(), Expected, Floor, Expected + Spread));
            }
            MeanPiles[L] += Count / static_cast<double>(UE_ARRAY_COUNT(Roots));
            MeanExpected[L] = Expected;
            OutOfOrder += Count <= Last;
            Last = Count;

            TArray<int32> PerSurface;
            PerSurface.SetNumZeroed(Surfaces.Num());
            for (const FDressItem& Item : Plan)
            {
                PerSurface[Item.Surface] += Item.StackIndex == 0;
            }
            PastCap += PerSurface.ContainsByPredicate([](int32 N) { return N > DressGuarantees::PoissonMax; });
            if (L == 0)
            {
                NoneBare += !PerSurface.Contains(0);
            }
        }
    }
    AddInfo(FString::Printf(TEXT("Mean piles over %d roots: %.1f at 0.3, %.1f at 1, %.1f at 2."),
                            static_cast<int32>(UE_ARRAY_COUNT(Roots)), MeanPiles[0], MeanPiles[1], MeanPiles[2]));
    TestEqual(TEXT("every level on every root draws exactly its plan"), Undrawn, 0);
    TestEqual(TEXT("every ship's count at 0.3, 1 and 2 is one Poisson would give"), Implausible, 0);
    TestEqual(TEXT("and on every root 0.3 < 1 < 2"), OutOfOrder, 0);
    // Averaged over the roots, the count is the rules' mean less what full
    // surfaces refuse: never more than the mean allows (two standard errors
    // over it), and never so much less that the rules stop describing the
    // ship. Squalid fills surfaces, which is where the floor's slack goes.
    for (int32 L = 0; L < 3; ++L)
    {
        const double StandardError = FMath::Sqrt(MeanExpected[L] / UE_ARRAY_COUNT(Roots));
        TestTrue(FString::Printf(TEXT("at LivedIn %.1f the mean count, %.1f, is what the rules' mean of %.1f gives, less what will not fit"),
                                 Levels[L], MeanPiles[L], MeanExpected[L]),
                 MeanPiles[L] <= MeanExpected[L] + 2.0 * StandardError && MeanPiles[L] >= 0.55 * MeanExpected[L]);
    }
    TestTrue(TEXT("on average, lived in is about three times freshly moved in"),
             MeanPiles[1] > 2.0 * MeanPiles[0] && MeanPiles[1] < 4.5 * MeanPiles[0]);
    TestTrue(TEXT("and squalid is busier than lived in, but not past twice"),
             MeanPiles[2] > 1.4 * MeanPiles[1] && MeanPiles[2] <= 2.2 * MeanPiles[1]);
    TestEqual(TEXT("freshly moved in, some surface is always still bare"), NoneBare, 0);
    TestEqual(TEXT("squalid, no surface holds more than its cap"), PastCap, 0);
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDressingHaulerOnItsSurfaceTest,
    "DeepSpace.Dressing.Hauler.OnItsSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Nothing drawn is anywhere but on a surface: every instance, measured from
 * its mesh's bounds, lies inside a surface's footprint and under its clear,
 * none is on or near the floor, and none touches a door's, the console's,
 * the slide run's or the crawlway's keep-out. Checked squalid and past it,
 * on every root, since a busier ship is the one that pushes against every
 * edge.
 */
bool FDressingHaulerOnItsSurfaceTest::RunTest(const FString& Parameters)
{
    using namespace DressingHaulerTestLocal;
    FMarkers Markers;
    if (!LoadMarkers(*this, Markers))
    {
        return false;
    }
    FScopedCVar LivedIn(TEXT("ds.Dress.LivedIn"));
    FScopedCVar Seed(TEXT("ds.Dress.Seed"));
    LivedIn.Set(2.0);
    Seed.Set(TEXT("-1"));

    FDressedHauler Ship(TEXT("DressingHaulerOnItsSurface"), Markers);
    // The floor is z = 0 (ADR 0005). Touching it means resting within a
    // centimetre of it; the lowest real surface, the airlock bench, is 45 up.
    constexpr double FloorZ = 0.0;
    int32 Checked = 0, Outside = 0, OnTheFloor = 0, InAKeepOut = 0;
    FString FirstOutside, FirstLow;
    for (const double Level : { 2.0, DressGuarantees::MaxLivedIn })
    {
        LivedIn.Set(Level);
        for (const int32 Root : Roots)
        {
            Seed.Set(FString::FromInt(Root));
            for (const FBox& Box : InstanceBoxes(Ship.Dressing->GetClutter()))
            {
                ++Checked;
                if (!Markers.Surfaces.ContainsByPredicate([&Box](const FDressSurface& S) { return InsideColumn(Box, S, 0.05); }))
                {
                    ++Outside;
                    FirstOutside = FirstOutside.IsEmpty() ? Box.ToString() : FirstOutside;
                }
                if (Box.Min.Z < FloorZ + 1.0)
                {
                    ++OnTheFloor;
                    FirstLow = FirstLow.IsEmpty() ? Box.ToString() : FirstLow;
                }
                InAKeepOut += Markers.KeepOuts.ContainsByPredicate([&Box](const FBox& Zone)
                {
                    return Box.Min.X < Zone.Max.X - 0.05 && Zone.Min.X < Box.Max.X - 0.05
                        && Box.Min.Y < Zone.Max.Y - 0.05 && Zone.Min.Y < Box.Max.Y - 0.05
                        && Box.Min.Z < Zone.Max.Z - 0.05 && Zone.Min.Z < Box.Max.Z - 0.05;
                });
            }
        }
    }
    AddInfo(FString::Printf(TEXT("%d instances checked across %d roots at LivedIn 2 and %.0f."), Checked,
                            static_cast<int32>(UE_ARRAY_COUNT(Roots)), DressGuarantees::MaxLivedIn));
    TestTrue(TEXT("there was clutter to check"), Checked > 0);
    TestEqual(FString::Printf(TEXT("every instance lies on a real surface, under its clear (first stray: %s)"), *FirstOutside), Outside, 0);
    TestEqual(FString::Printf(TEXT("none touches the floor (first: %s)"), *FirstLow), OnTheFloor, 0);
    // A tripwire, not a proof: no real surface meets a keep-out today, since
    // the layout already keeps furniture out of doorways and the slide run.
    // It fires the day a surface is moved into one; the guard itself is
    // proven with a probe zone in DeepSpace.Ship.Dressing.World.
    TestEqual(TEXT("none is in a door's, the console's, the slide run's or the crawlway's keep-out"), InAKeepOut, 0);
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDressingHaulerScreensClearTest,
    "DeepSpace.Dressing.Hauler.ScreensClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Nothing is ever left over the laptop or the chart, or between either and
 * whoever reads it (plan conflict 16). The check is made against each
 * screen's own floor rectangle from the layout, not against the excludes
 * the markers carry, so an exclude lost anywhere between the layout and the
 * instances shows. And it is made on roots where, with the excludes taken
 * away, something would stand there: otherwise a clear screen could just be
 * a lucky one.
 */
bool FDressingHaulerScreensClearTest::RunTest(const FString& Parameters)
{
    using namespace DressingHaulerTestLocal;
    FMarkers Markers;
    if (!LoadMarkers(*this, Markers))
    {
        return false;
    }
    FScopedCVar LivedIn(TEXT("ds.Dress.LivedIn"));
    FScopedCVar Seed(TEXT("ds.Dress.Seed"));
    LivedIn.Set(DressGuarantees::MaxLivedIn);
    Seed.Set(TEXT("-1"));

    FDressedHauler Ship(TEXT("DressingHaulerScreensClear"), Markers);
    const FShipDressingRules Rules = Ship.Dressing->GetRules();

    // The same surfaces with every exclude stripped: where the dressing
    // would put things if nothing kept them off the screens.
    TArray<FDressSurface> Bare = Markers.Surfaces;
    for (FDressSurface& Surface : Bare)
    {
        Surface.Excludes.Reset();
    }
    struct FScreenCase
    {
        const TCHAR* Name;
        FBox2D Rect;
        TArray<int32> Roots;
    };
    TArray<FScreenCase> Screens = { { TEXT("the laptop"), Markers.Laptop.Exclude, {} },
                                    { TEXT("the chart"), Markers.Chart.Exclude, {} } };
    for (int32 Root = 1; Root <= 400; ++Root)
    {
        const TArray<FDressItem> Unkept = ShipDressing::Dress(Bare, Markers.KeepOuts, ShipDressing::DressSeed(Root), Rules);
        for (FScreenCase& Screen : Screens)
        {
            const bool bWouldCover = Screen.Roots.Num() < 4 && Unkept.ContainsByPredicate([&](const FDressItem& Item)
            {
                return Intersects2D(ShipDressing::ItemBounds(Item, Bare[Item.Surface], Rules), Screen.Rect, 0.05);
            });
            if (bWouldCover)
            {
                Screen.Roots.Add(Root);
            }
        }
    }

    for (const FScreenCase& Screen : Screens)
    {
        if (!TestTrue(FString::Printf(TEXT("without its exclude, the dressing would cover %s on some root"), Screen.Name),
                      Screen.Roots.Num() > 0))
        {
            continue;
        }
        int32 Covered = 0, Instances = 0;
        for (const int32 Root : Screen.Roots)
        {
            Seed.Set(FString::FromInt(Root));
            for (const FBox& Box : InstanceBoxes(Ship.Dressing->GetClutter()))
            {
                ++Instances;
                Covered += Intersects2D(Box, Screen.Rect, 0.05);
            }
        }
        AddInfo(FString::Printf(TEXT("%s: roots %s, %d instances drawn."), Screen.Name,
                                *FString::JoinBy(Screen.Roots, TEXT(", "), [](int32 R) { return FString::FromInt(R); }), Instances));
        TestTrue(FString::Printf(TEXT("the ship is dressed on those roots (%s)"), Screen.Name), Instances > 0);
        TestEqual(FString::Printf(TEXT("and nothing stands over %s or between it and its reader"), Screen.Name), Covered, 0);
    }
    return true;
}

#endif
