#include "Algo/Reverse.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipDressing.h"
#include "Ship/ShipDressingRules.h"
#include "Ship/ShipDressingSubsystem.h"
#include "Tests/DressingTestFixtures.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDressingDeterminismTest,
    "DeepSpace.Ship.Dressing.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDressingCorpusTest,
    "DeepSpace.Ship.Dressing.Corpus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDressingShapeTest,
    "DeepSpace.Ship.Dressing.Shape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDressingCatalogueTest,
    "DeepSpace.Ship.Dressing.Catalogue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    constexpr double Tol = 0.01;   // cm

    FString KeyOf(const FDressSurface& Surface)
    {
        return FString::Printf(TEXT("%s|%s|%d"), *Surface.Room.ToString(), *Surface.Kind.ToString(), Surface.Ordinal);
    }

    FString Describe(const FDressItem& Item)
    {
        const FVector W = Item.World.GetLocation();
        return FString::Printf(TEXT("%s/%s at (%.4f, %.4f) base %.4f turns %d world (%.4f, %.4f, %.4f)"),
                               *Item.Template.ToString(), *Item.Colour.ToString(), Item.At.X, Item.At.Y, Item.Base,
                               Item.Turns, W.X, W.Y, W.Z);
    }

    /** Everything each surface got, keyed by what the surface is rather
     *  than where it came in the list. */
    TMap<FString, TArray<FString>> ByKey(TConstArrayView<FDressSurface> Surfaces, const TArray<FDressItem>& Plan)
    {
        TMap<FString, TArray<FString>> Out;
        for (const FDressSurface& Surface : Surfaces)
        {
            Out.Add(KeyOf(Surface));
        }
        for (const FDressItem& Item : Plan)
        {
            Out.FindOrAdd(KeyOf(Surfaces[Item.Surface])).Add(Describe(Item));
        }
        return Out;
    }

    bool Identical(const TArray<FDressItem>& A, const TArray<FDressItem>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }
        for (int32 I = 0; I < A.Num(); ++I)
        {
            if (A[I].Surface != B[I].Surface || A[I].Template != B[I].Template || A[I].Colour != B[I].Colour
                || A[I].At != B[I].At || A[I].Base != B[I].Base || A[I].Turns != B[I].Turns
                || A[I].StackIndex != B[I].StackIndex || !A[I].World.Equals(B[I].World, 0.0))
            {
                return false;
            }
        }
        return true;
    }

    /** An item's own footprint on its surface, as it lies. */
    FBox2D Footprint(const FDressItem& Item, const FShipDressingRules& Rules)
    {
        const FVector2D Half = ShipDressing::TurnedHalfExtent(*Rules.FindTemplate(Item.Template), Item.Turns);
        return FBox2D(Item.At - Half, Item.At + Half);
    }

    bool Overlap(const FBox2D& A, const FBox2D& B)
    {
        return A.Min.X < B.Max.X - Tol && B.Min.X < A.Max.X - Tol && A.Min.Y < B.Max.Y - Tol && B.Min.Y < A.Max.Y - Tol;
    }

    bool Overlap(const FBox& A, const FBox& B)
    {
        return A.Min.X < B.Max.X - Tol && B.Min.X < A.Max.X - Tol && A.Min.Y < B.Max.Y - Tol && B.Min.Y < A.Max.Y - Tol
            && A.Min.Z < B.Max.Z - Tol && B.Min.Z < A.Max.Z - Tol;
    }

    struct FMoments
    {
        double Sum = 0.0;
        double SumSq = 0.0;
        int32 N = 0;
        void Add(double X) { Sum += X; SumSq += X * X; ++N; }
        double Mean() const { return N ? Sum / N : 0.0; }
        double Var() const { return N ? SumSq / N - Mean() * Mean() : 0.0; }
    };
}

// -----------------------------------------------------------------------------

/**
 * The same surfaces, keep-outs, seed and rules give the same plan, exactly;
 * the order the markers are found in changes nothing; a different seed is a
 * different life; and the bunk's surfaces are no business of the galley's
 * (lived-in decisions 1b, 4).
 */
bool FShipDressingDeterminismTest::RunTest(const FString& Parameters)
{
    const FShipDressingRules Rules;
    const TArray<FDressSurface> Surfaces = DressingFixtures::Hauler();
    const TArray<FBox> KeepOut = DressingFixtures::HaulerKeepOut();
    const uint64 Seed = ShipDressing::DressSeed(20260925);

    const TArray<FDressItem> Plan = ShipDressing::Dress(Surfaces, KeepOut, Seed, Rules);
    TestTrue(FString::Printf(TEXT("the hauler is dressed (%d things)"), Plan.Num()), Plan.Num() > 5);
    TestTrue(TEXT("the same inputs give the same plan, exactly"),
             Identical(Plan, ShipDressing::Dress(Surfaces, KeepOut, Seed, Rules)));

    // Found in another order: reversed, then rotated by three.
    TArray<FDressSurface> Shuffled;
    for (int32 I = Surfaces.Num() - 1; I >= 0; --I)
    {
        Shuffled.Add(Surfaces[(I + 3) % Surfaces.Num()]);
    }
    TestTrue(TEXT("the fixture really was shuffled"), KeyOf(Shuffled[0]) != KeyOf(Surfaces[0]));
    const TMap<FString, TArray<FString>> Want = ByKey(Surfaces, Plan);
    const TMap<FString, TArray<FString>> Got = ByKey(Shuffled, ShipDressing::Dress(Shuffled, KeepOut, Seed, Rules));
    for (const TPair<FString, TArray<FString>>& Entry : Want)
    {
        const TArray<FString>* Other = Got.Find(Entry.Key);
        TestTrue(FString::Printf(TEXT("%s gets the same things whatever order the surfaces come in"), *Entry.Key),
                 Other && *Other == Entry.Value);
    }

    TArray<FBox> Reordered = KeepOut;
    Algo::Reverse(Reordered);
    TestTrue(TEXT("and whatever order the keep-outs come in"),
             Identical(Plan, ShipDressing::Dress(Surfaces, Reordered, Seed, Rules)));

    TestFalse(TEXT("a different seed is a different life on the same floor plan"),
              Identical(Plan, ShipDressing::Dress(Surfaces, KeepOut, ShipDressing::DressSeed(20260926), Rules)));

    // Stream isolation: take the bunk away and the galley is untouched.
    TArray<FDressSurface> NoBunk = Surfaces.FilterByPredicate([](const FDressSurface& S) { return S.Room != FName(TEXT("bunk")); });
    TestEqual(TEXT("the fixture has two bunk surfaces"), Surfaces.Num() - NoBunk.Num(), 2);
    const TMap<FString, TArray<FString>> Without = ByKey(NoBunk, ShipDressing::Dress(NoBunk, KeepOut, Seed, Rules));
    int32 GalleyThings = 0;
    for (const TPair<FString, TArray<FString>>& Entry : Want)
    {
        if (Entry.Key.StartsWith(TEXT("galley|")))
        {
            GalleyThings += Entry.Value.Num();
            TestTrue(FString::Printf(TEXT("%s is exactly as it was with the bunk gone"), *Entry.Key),
                     Without.Contains(Entry.Key) && Without[Entry.Key] == Entry.Value);
        }
    }
    TestTrue(TEXT("and the galley had something on it to keep"), GalleyThings > 0);

    // A spelling the engine happens to hand back does not reseed anything.
    TArray<FDressSurface> Shouted = Surfaces;
    for (FDressSurface& S : Shouted)
    {
        S.Room = FName(*S.Room.ToString().ToUpper());
    }
    TestEqual(TEXT("a surface's seed does not depend on the case of its room's name"),
              ShipDressing::SurfaceSeed(Seed, Shouted[0]), ShipDressing::SurfaceSeed(Seed, Surfaces[0]));
    return true;
}

// -----------------------------------------------------------------------------

/**
 * ADR 0006's generator property test for the dressing, and the intent guard
 * validate_hauler.py can no longer run, because clutter is spawned at
 * runtime: across 500 seeds every item lies wholly inside its surface,
 * rests on it or on the thing below, stands no taller than its clear,
 * covers no exclude, overlaps nothing, and touches no keep-out -- no door's
 * zone, not the console's, not the slide run, not the crawlway -- and
 * nothing is ever on the floor.
 */
bool FShipDressingCorpusTest::RunTest(const FString& Parameters)
{
    FShipDressingRules Rules;
    TArray<FDressSurface> Surfaces = DressingFixtures::Hauler();
    const int32 Doorway = Surfaces.Add(DressingFixtures::IntoTheDoorway());
    const int32 Floor = Surfaces.Add(DressingFixtures::OnTheFloor());
    TArray<FBox> KeepOut = DressingFixtures::HaulerKeepOut();
    KeepOut.Add(DressingFixtures::TheDoorway());

    TArray<int32> PerSurface;
    PerSurface.SetNumZeroed(Surfaces.Num());
    int32 Failures = 0;
    const auto Fail = [&](const FString& What)
    {
        if (++Failures <= 12)
        {
            AddError(What);
        }
    };

    for (int32 Corpus = 0; Corpus < 500; ++Corpus)
    {
        const TArray<FDressItem> Plan = ShipDressing::Dress(Surfaces, KeepOut, ShipDressing::DressSeed(Corpus), Rules);
        TArray<int32> Piles;
        Piles.SetNumZeroed(Surfaces.Num());

        for (int32 I = 0; I < Plan.Num(); ++I)
        {
            const FDressItem& Item = Plan[I];
            const FDressSurface& Surface = Surfaces[Item.Surface];
            const FDressTemplate* Template = Rules.FindTemplate(Item.Template);
            const FString Where = FString::Printf(TEXT("seed %d, %s: %s"), Corpus, *KeyOf(Surface), *Describe(Item));
            if (!Template)
            {
                Fail(Where + TEXT(" names no template"));
                continue;
            }
            ++PerSurface[Item.Surface];
            Piles[Item.Surface] += Item.StackIndex == 0 ? 1 : 0;

            // Measured from the parts as they lie, in the surface's frame.
            const FBox World = ShipDressing::ItemBounds(Item, Surface, Rules);
            const FBox Local = World.InverseTransformBy(Surface.ToWorld);
            const FVector2D Half = Surface.Size * 0.5;
            if (Local.Min.X < -Half.X - Tol || Local.Max.X > Half.X + Tol || Local.Min.Y < -Half.Y - Tol || Local.Max.Y > Half.Y + Tol)
            {
                Fail(FString::Printf(TEXT("%s overhangs its %.0f x %.0f surface: (%.2f, %.2f)-(%.2f, %.2f)"), *Where,
                                     Surface.Size.X, Surface.Size.Y, Local.Min.X, Local.Min.Y, Local.Max.X, Local.Max.Y));
            }
            if (FMath::Abs(Item.Base - Item.StackIndex * Template->Height()) > Tol || FMath::Abs(Local.Min.Z - Item.Base) > Tol)
            {
                Fail(FString::Printf(TEXT("%s does not rest on its surface or the thing below (bottom %.3f)"), *Where, Local.Min.Z));
            }
            if (Local.Max.Z > Surface.Clear + Tol)
            {
                Fail(FString::Printf(TEXT("%s stands %.1f tall under a %.0f clear"), *Where, Local.Max.Z, Surface.Clear));
            }
            if (World.Min.Z < DressGuarantees::MinRestHeightCm)
            {
                Fail(Where + TEXT(" is on the floor"));
            }
            const FBox2D Own = Footprint(Item, Rules);
            for (const FBox2D& Exclude : Surface.Excludes)
            {
                if (Overlap(Own, Exclude))
                {
                    Fail(Where + TEXT(" covers an exclude"));
                }
            }
            for (const FBox& Zone : KeepOut)
            {
                if (Overlap(World, Zone))
                {
                    Fail(FString::Printf(TEXT("%s touches the keep-out (%.0f, %.0f, %.0f)-(%.0f, %.0f, %.0f)"), *Where,
                                         Zone.Min.X, Zone.Min.Y, Zone.Min.Z, Zone.Max.X, Zone.Max.Y, Zone.Max.Z));
                }
            }
            for (int32 J = I + 1; J < Plan.Num(); ++J)
            {
                const FDressItem& Other = Plan[J];
                if (Other.Surface == Item.Surface && Other.At != Item.At && Overlap(Own, Footprint(Other, Rules)))
                {
                    Fail(FString::Printf(TEXT("%s overlaps %s"), *Where, *Describe(Other)));
                }
            }
        }
        for (int32 S = 0; S < Surfaces.Num(); ++S)
        {
            if (Piles[S] > DressGuarantees::PoissonMax)
            {
                Fail(FString::Printf(TEXT("seed %d: %s holds %d piles, over the cap"), Corpus, *KeyOf(Surfaces[S]), Piles[S]));
            }
        }
    }
    TestEqual(TEXT("no item in 500 ships broke a guarantee"), Failures, 0);
    for (int32 S = 0; S < Surfaces.Num(); ++S)
    {
        AddInfo(FString::Printf(TEXT("%s: %.2f things a ship"), *KeyOf(Surfaces[S]), PerSurface[S] / 500.0));
    }

    // Each guard bit on something it could have let through, or it proves
    // nothing: half the probe counter is in the doorway and half is not.
    TestTrue(FString::Printf(TEXT("the counter reaching into the doorway is still dressed on its free half (%d things)"),
                             PerSurface[Doorway]),
             PerSurface[Doorway] > 100);
    TestEqual(TEXT("the table on the floor gets nothing, ever"), PerSurface[Floor], 0);
    for (int32 S = 0; S < DressingFixtures::Hauler().Num(); ++S)
    {
        TestTrue(FString::Printf(TEXT("%s is dressed in some of the 500 (%d things)"), *KeyOf(Surfaces[S]), PerSurface[S]),
                 PerSurface[S] > 0);
    }

    Rules.LivedIn = 0.0;
    int32 Bare = 0;
    for (int32 Corpus = 0; Corpus < 50; ++Corpus)
    {
        Bare += ShipDressing::Dress(Surfaces, KeepOut, ShipDressing::DressSeed(Corpus), Rules).Num();
    }
    TestEqual(TEXT("LivedIn 0 places nothing"), Bare, 0);
    return true;
}

// -----------------------------------------------------------------------------

/**
 * The priors are the ones decision 3 says, so nobody quietly turns a Beta
 * back into a Unit() because it looked simpler (ADR 0008's amendment).
 * The draws are tested before overlap rejection can skew them; then the
 * plan itself is checked to lean the same way, so the draws are the ones
 * Dress uses. Fixed seeds throughout: deterministic, not flaky.
 */
bool FShipDressingShapeTest::RunTest(const FString& Parameters)
{
    const FShipDressingRules Rules;
    constexpr int32 N = 20000;
    const uint64 Root = GenSeed::Derive(20260925, GenSeed::Label("dressing.shape"));

    // Beta(2, 4): mean 1/3, variance 8/252. Beta(3, 3): 1/2, 9/252. Beta(4, 2): 2/3, 8/252.
    FMoments FromPlusY, FromMinusY, Centre, Back;
    for (int32 I = 0; I < N; ++I)
    {
        FGenStream A(GenSeed::Derive(Root, GenSeed::Label("along+y"), I));
        FromPlusY.Add(1.0 - ShipDressing::DrawAlong(A, EDressUse::PosY, Rules));
        FGenStream B(GenSeed::Derive(Root, GenSeed::Label("along-y"), I));
        FromMinusY.Add(ShipDressing::DrawAlong(B, EDressUse::NegY, Rules));
        FGenStream C(GenSeed::Derive(Root, GenSeed::Label("centre"), I));
        Centre.Add(ShipDressing::DrawAlong(C, EDressUse::Centre, Rules));
        FGenStream D(GenSeed::Derive(Root, GenSeed::Label("back"), I));
        Back.Add(ShipDressing::DrawBack(D, Rules));
    }
    const auto Check = [this](const TCHAR* What, const FMoments& M, double Mean, double Var)
    {
        TestTrue(FString::Printf(TEXT("%s: mean %.4f, want %.4f +- 0.02"), What, M.Mean(), Mean), FMath::Abs(M.Mean() - Mean) <= 0.02);
        TestTrue(FString::Printf(TEXT("%s: variance %.4f, want %.4f +- 0.004"), What, M.Var(), Var), FMath::Abs(M.Var() - Var) <= 0.004);
    };
    Check(TEXT("distance from the +y use end is Beta(2, 4)"), FromPlusY, 1.0 / 3.0, 8.0 / 252.0);
    Check(TEXT("distance from the -y use end is Beta(2, 4)"), FromMinusY, 1.0 / 3.0, 8.0 / 252.0);
    Check(TEXT("a surface used from the middle is Beta(3, 3)"), Centre, 0.5, 9.0 / 252.0);
    Check(TEXT("depth towards the back edge is Beta(4, 2)"), Back, 2.0 / 3.0, 8.0 / 252.0);

    // Wear: Beta(5, 2) cut at 0.5 and 0.9, each about 11%.
    int32 Replaced = 0, Faded = 0;
    const uint64 Seed = ShipDressing::DressSeed(20260925);
    for (int32 I = 0; I < 5000; ++I)
    {
        const EDressWear Wear = ShipDressing::Wear(Seed, FString::Printf(TEXT("piece_%d"), I), Rules);
        Replaced += Wear == EDressWear::Replaced;
        Faded += Wear == EDressWear::Faded;
    }
    TestTrue(FString::Printf(TEXT("about 11%% of pieces were replaced (%.1f%%)"), Replaced / 50.0), FMath::Abs(Replaced / 5000.0 - 0.109) <= 0.03);
    TestTrue(FString::Printf(TEXT("about 11%% have faded (%.1f%%)"), Faded / 50.0), FMath::Abs(Faded / 5000.0 - 0.114) <= 0.03);
    TestTrue(TEXT("one piece wears the same way every time it is asked"),
             ShipDressing::Wear(Seed, TEXT("desk_0"), Rules) == ShipDressing::Wear(Seed, TEXT("desk_0"), Rules));

    // How many: Poisson(lambda x LivedIn). On a shelf too big to reject
    // anything, the mean number of piles is the kind's lambda, scaled --
    // squalid included. The rack's lambda is 2, so even at LivedIn 2 the cap
    // of 8 trims the mean by only a few hundredths.
    FDressSurface Vast = DressingFixtures::Surface(TEXT("probe"), TEXT("wall_rack.shelf_0"), 0, FVector(0, 0, 90), 0,
                                                   FVector2D(3000, 3000), EDressEdge::NegX, EDressUse::Centre, 70);
    const double Lambda = Rules.FindKind(Vast.Kind)->Lambda;
    for (const double LivedIn : { 0.5, 1.0, 2.0 })
    {
        FShipDressingRules Scaled = Rules;
        Scaled.LivedIn = LivedIn;
        FMoments Piles;
        for (int32 I = 0; I < 2000; ++I)
        {
            Vast.Ordinal = I;
            int32 Count = 0;
            const TArray<FDressSurface> One = { Vast };
            for (const FDressItem& Item : ShipDressing::Dress(One, TArray<FBox>(), Seed, Scaled))
            {
                Count += Item.StackIndex == 0;
            }
            Piles.Add(Count);
        }
        TestTrue(FString::Printf(TEXT("LivedIn %.1f: %.3f things on average, want lambda %.1f x %.1f"), LivedIn, Piles.Mean(), Lambda, LivedIn),
                 FMath::Abs(Piles.Mean() - Lambda * LivedIn) <= 0.12 * LivedIn + 0.05);
        TestTrue(FString::Printf(TEXT("LivedIn %.1f: and they vary as counts do (variance %.3f)"), LivedIn, Piles.Var()),
                 FMath::Abs(Piles.Var() - Lambda * LivedIn) <= 0.25 * Lambda * LivedIn);
    }

    // The plan leans the way the draws do: on the counter, used from +y and
    // backed against -x, things gather at the +y end and against the wall.
    // What is on it follows the counter's mix, and piles are geometric.
    const TArray<FDressSurface> Hauler = DressingFixtures::Hauler();
    const int32 Counter = Hauler.IndexOfByPredicate([](const FDressSurface& S) { return S.Kind == FName(TEXT("counter.top")); });
    const int32 Desk = Hauler.IndexOfByPredicate([](const FDressSurface& S) { return S.Kind == FName(TEXT("desk.top")); });
    FMoments Along, Depth, BookPile;
    int32 Mugs = 0, OnCounter = 0, Coloured = 0, Olive = 0, TallestPile = 0;
    TMap<FVector2D, int32> Pile;
    for (int32 Corpus = 0; Corpus < 500; ++Corpus)
    {
        const TArray<FDressItem> Plan = ShipDressing::Dress(Hauler, DressingFixtures::HaulerKeepOut(), ShipDressing::DressSeed(Corpus), Rules);
        Pile.Reset();
        for (const FDressItem& Item : Plan)
        {
            if (!Item.Colour.IsNone())
            {
                ++Coloured;
                Olive += Item.Colour == FName(TEXT("olive"));
            }
            if (Item.Surface == Counter && Item.StackIndex == 0)
            {
                const FDressSurface& S = Hauler[Counter];
                Along.Add((Item.At.Y + 0.5 * S.Size.Y) / S.Size.Y);
                Depth.Add((0.5 * S.Size.X - Item.At.X) / S.Size.X);
                ++OnCounter;
                Mugs += Item.Template == FName(TEXT("mug"));
            }
            if (Item.Surface == Desk && Item.Template == FName(TEXT("book")))
            {
                Pile.FindOrAdd(Item.At) += 1;
            }
        }
        for (const TPair<FVector2D, int32>& Books : Pile)
        {
            BookPile.Add(Books.Value);
            TallestPile = FMath::Max(TallestPile, Books.Value);
        }
    }
    AddInfo(FString::Printf(TEXT("counter: along %.3f, back %.3f, mugs %.3f; olive %.3f; book piles %.3f"),
                            Along.Mean(), Depth.Mean(), double(Mugs) / OnCounter, double(Olive) / Coloured, BookPile.Mean()));
    TestTrue(FString::Printf(TEXT("the counter's things gather at its +y use end (%.3f of the way along)"), Along.Mean()), Along.Mean() > 0.58);
    TestTrue(FString::Printf(TEXT("and against its back wall (%.3f of the way back)"), Depth.Mean()), Depth.Mean() > 0.56);
    TestTrue(FString::Printf(TEXT("mugs are the counter's commonest thing, 4 in 13 (%.3f)"), double(Mugs) / OnCounter),
             FMath::Abs(double(Mugs) / OnCounter - 4.0 / 13.0) <= 0.06);
    TestTrue(FString::Printf(TEXT("olive is the ship's issue colour, 5 in 11 (%.3f)"), double(Olive) / Coloured),
             FMath::Abs(double(Olive) / Coloured - 5.0 / 11.0) <= 0.06);
    TestTrue(FString::Printf(TEXT("a pile of books is geometric at 1/2, capped at 3: mean %.3f, want 1.75"), BookPile.Mean()),
             FMath::Abs(BookPile.Mean() - 1.75) <= 0.12);
    TestTrue(FString::Printf(TEXT("and never taller than the cap (%d)"), TallestPile), TallestPile >= 2 && TallestPile <= Rules.StackCap);
    return true;
}

// -----------------------------------------------------------------------------

/**
 * Every template is something that can rest on a surface, every material it
 * names exists, and every thing in every mix fits somewhere it may go --
 * otherwise a mug taller than every shelf it is allowed on would be dropped
 * silently, forever. The material check is the dressing's version of the
 * sky's material contract: a misspelt role fails here, rather than drawing a
 * mug in the default material in play.
 */
bool FShipDressingCatalogueTest::RunTest(const FString& Parameters)
{
    const FShipDressingRules Rules;

    for (const EDressMesh Mesh : { EDressMesh::Cube, EDressMesh::Chamfer, EDressMesh::Cylinder })
    {
        const UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, UShipDressingSubsystem::MeshPath(Mesh));
        TestTrue(FString::Printf(TEXT("%s loads, and has a size to measure"), UShipDressingSubsystem::MeshPath(Mesh)),
                 Asset && Asset->GetBoundingBox().GetSize().GetMin() > 0.0);
    }

    TSet<FName> Roles;
    for (const FDressTemplate& Template : Rules.Templates)
    {
        const FString Name = Template.Name.ToString();
        TestTrue(Name + TEXT(" has parts"), Template.Parts.Num() > 0);
        FBox Bounds(ForceInit);
        for (const FDressPart& Part : Template.Parts)
        {
            TestTrue(Name + TEXT(": every part has a size"), Part.Size.GetMin() > 0.0);
            Bounds += FBox(Part.At - Part.Size * 0.5, Part.At + Part.Size * 0.5);
            if (Part.Role == FName(TEXT("fabric")))
            {
                for (const FDressWeight& Colour : Rules.Colours)
                {
                    Roles.Add(ShipDressing::PartRole(Part, Colour.Name));
                }
            }
            else
            {
                Roles.Add(Part.Role);
            }
        }
        TestTrue(FString::Printf(TEXT("%s rests on its origin (lowest part at %.3f)"), *Name, Bounds.Min.Z), FMath::Abs(Bounds.Min.Z) <= Tol);
        TestTrue(FString::Printf(TEXT("%s is centred on its footprint (%.2f..%.2f, %.2f..%.2f)"), *Name,
                                 Bounds.Min.X, Bounds.Max.X, Bounds.Min.Y, Bounds.Max.Y),
                 FMath::Abs(Bounds.Min.X + Bounds.Max.X) <= Tol && FMath::Abs(Bounds.Min.Y + Bounds.Max.Y) <= Tol);
        TestTrue(Name + TEXT(": its footprint is what the generator keeps clear"),
                 FMath::IsNearlyEqual(Template.HalfExtent().X, Bounds.Max.X, Tol) && FMath::IsNearlyEqual(Template.HalfExtent().Y, Bounds.Max.Y, Tol)
                 && FMath::IsNearlyEqual(Template.Height(), Bounds.Max.Z, Tol));
    }
    Roles.Add(UShipDressingSubsystem::WearRole(EDressWear::Faded));
    Roles.Add(UShipDressingSubsystem::WearRole(EDressWear::Replaced));
    for (const FName& Role : Roles)
    {
        TestNotNull(FString::Printf(TEXT("%s loads (Tools/build_hauler.py authors it)"), *UShipDressingSubsystem::MaterialPath(Role)),
                    LoadObject<UMaterialInterface>(nullptr, *UShipDressingSubsystem::MaterialPath(Role)));
    }

    const TArray<FDressSurface> Hauler = DressingFixtures::Hauler();
    TSet<const FDressKind*> Used;
    for (const FDressSurface& Surface : Hauler)
    {
        const FDressKind* Kind = Rules.FindKind(Surface.Kind);
        TestNotNull(FString::Printf(TEXT("%s has rules"), *Surface.Kind.ToString()), Kind);
        Used.Add(Kind);
    }
    for (const FDressKind& Kind : Rules.Kinds)
    {
        TestTrue(FString::Printf(TEXT("%s's rules dress a surface the hauler has"), *Kind.Kind.ToString()), Used.Contains(&Kind));
        TestTrue(FString::Printf(TEXT("%s's mean is inside what Poisson takes at the highest LivedIn"), *Kind.Kind.ToString()),
                 Kind.Lambda > 0.0 && Kind.Lambda * DressGuarantees::MaxLivedIn <= FGenStream::MaxPoissonMean);
        for (const FDressWeight& Entry : Kind.Mix)
        {
            const FDressTemplate* Template = Rules.FindTemplate(Entry.Name);
            if (!TestNotNull(FString::Printf(TEXT("%s's %s is a template"), *Kind.Kind.ToString(), *Entry.Name.ToString()), Template))
            {
                continue;
            }
            const FVector2D Half = Template->HalfExtent();
            const bool bFits = Hauler.ContainsByPredicate([&](const FDressSurface& S)
            {
                return Rules.FindKind(S.Kind) == &Kind && Template->Height() <= S.Clear
                       && ((2 * Half.X <= S.Size.X && 2 * Half.Y <= S.Size.Y) || (2 * Half.Y <= S.Size.X && 2 * Half.X <= S.Size.Y));
            });
            TestTrue(FString::Printf(TEXT("%s fits on at least one %s"), *Entry.Name.ToString(), *Kind.Kind.ToString()), bFits);
        }
    }
    return true;
}

#endif
