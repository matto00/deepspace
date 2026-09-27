#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipLaptop.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipNavScreen.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "Universe/StarSystem.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

// One test and no children: a test path with children becomes a group, and a
// group silently stops running its own body.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FChartLayoutTest,
    "DeepSpace.UI.ChartLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ChartLayoutTestLocal
{
    /** hauler_layout's NAV_SCREEN glass and CHART_EYE, resolved to world
     *  space (the cockpit's (x, y) is the world's (x + 1410, y - 200)). */
    const FVector ChartGlass(1711.0, 85.0, 105.0);
    const FVector ChartEye(1604.0, 83.0, 125.0);

    /** The 4K display the sizes are judged on, pixels across. */
    constexpr double DisplayWidth = 3840.0;

    /** Where each widget is, laid out as Slate lays it out on a panel of the
     *  given size: a prepass for the desired sizes, then every arrangement
     *  down the tree. No renderer is needed, so this holds under -nullrhi,
     *  which cannot paint or hit-test. */
    void Arrange(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, TMap<const SWidget*, FGeometry>& Out)
    {
        Out.Add(&Widget.Get(), Geometry);
        FArrangedChildren Children(EVisibility::Visible);
        Widget->ArrangeChildren(Geometry, Children);
        for (int32 Index = 0; Index < Children.Num(); ++Index)
        {
            Arrange(Children[Index].Widget, Children[Index].Geometry, Out);
        }
    }

    /** One word on the glass as laid out: where, how big, and how big it
     *  asked to be. */
    struct FWord
    {
        FString Text;
        FVector2D At;
        FVector2D Size;
        FVector2D Wants;
        UTextBlock* Block = nullptr;

        FVector2D End() const { return At + Size; }
    };

    TArray<FWord> LayOut(UUserWidget& Screen, const FVector2D& Draw)
    {
        const TSharedRef<SWidget> Root = Screen.TakeWidget();
        Root->SlatePrepass(1.0f);
        TMap<const SWidget*, FGeometry> Laid;
        Arrange(Root, FGeometry::MakeRoot(Draw, FSlateLayoutTransform()), Laid);

        TArray<FWord> Words;
        Screen.WidgetTree->ForEachWidget([&Words, &Laid](UWidget* Widget)
        {
            UTextBlock* Text = Cast<UTextBlock>(Widget);
            const TSharedPtr<SWidget> Built = Widget->GetCachedWidget();
            const FGeometry* Geometry = Built ? Laid.Find(Built.Get()) : nullptr;
            if (Text && Geometry && !Text->GetText().IsEmpty())
            {
                Words.Add({ Text->GetText().ToString(), Geometry->GetAbsolutePosition(), Geometry->GetAbsoluteSize(),
                            Built->GetDesiredSize(), Text });
            }
        });
        return Words;
    }

    const FWord* Find(const TArray<FWord>& Words, const FString& Text)
    {
        return Words.FindByPredicate([&Text](const FWord& Word) { return Word.Text == Text; });
    }

    /** Sets whichever block now reads From to read To. */
    void Rewrite(UUserWidget& Screen, const FString& From, const FString& To)
    {
        Screen.WidgetTree->ForEachWidget([&From, &To](UWidget* Widget)
        {
            if (UTextBlock* Text = Cast<UTextBlock>(Widget); Text && Text->GetText().ToString() == From)
            {
                Text->SetText(FText::FromString(To));
            }
        });
    }
}

/**
 * The chart's layout, at the map's standard (the second playtest: "the jump
 * menu has spacing issues"). The tree is laid out as Slate lays it out, with
 * no renderer, and every word on the glass is held to three things: it is on
 * the panel, it is given at least the room it asks for (nothing clipped, and
 * nothing run into its neighbour), and no two words overlap. Each row's
 * columns line up with every other row's. Then every word is rewritten to the
 * widest the chart can print -- the longest name in the corpus, the widest
 * class, a two-line course to a named world -- and held to the same.
 *
 * Its draw size is held to how it is seen: from its own chair's eye the panel
 * spans at least its pixels on the 4K display, so it is never minified where
 * it is read. And the chart is drivable from its own chair, and only there.
 */
bool FChartLayoutTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ChartLayoutTestLocal;

    FSkyWorld Test(TEXT("ChartLayoutWorld"));
    AShipNavScreen* Screen = Test.World->SpawnActor<AShipNavScreen>(ChartGlass, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the chart spawns"), Screen))
    {
        return false;
    }
    Test.BeginPlay();
    Screen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);
    UNavigationWidget* Chart = Cast<UNavigationWidget>(Screen->GetScreen()->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the chart's panel made its widget"), Chart))
    {
        return false;
    }
    Test.Ship->Tick(0.1f);
    Chart->RefreshFromShip();
    const FVector2D Draw = Screen->GetScreen()->GetDrawSize();

    // -- the pixels are the chair's ----------------------------------------
    {
        const FFloatProperty* Fov = FindFProperty<FFloatProperty>(ADeepSpaceCharacter::StaticClass(), TEXT("FieldOfView"));
        const double Degrees = Fov ? Fov->GetPropertyValue_InContainer(GetDefault<ADeepSpaceCharacter>()) : 103.0;
        const double PixelsPerTan = 0.5 * DisplayWidth / FMath::Tan(FMath::DegreesToRadians(0.5 * Degrees));
        const FVector2D Panel(68.0, 68.0 * Draw.Y / Draw.X);
        const FVector ToGlass = ChartGlass - ChartEye;
        const double Across = ToGlass.X;
        const double Down = FMath::Atan2(-ToGlass.Z, ToGlass.X);
        const FVector2D Seen(2.0 * PixelsPerTan * 0.5 * Panel.X / Across,
                             2.0 * PixelsPerTan * 0.5 * Panel.Y * FMath::Cos(Down) / ToGlass.Size());
        AddInfo(FString::Printf(TEXT("from its chair the chart spans %.0f x %.0f screen px at %.0f degrees; drawn at %.0f x %.0f"),
                                Seen.X, Seen.Y, Degrees, Draw.X, Draw.Y));
        TestTrue(FString::Printf(TEXT("it is never minified where it is read (%.0f x %.0f seen, %.0f x %.0f drawn)"),
                                 Seen.X, Seen.Y, Draw.X, Draw.Y),
                 Seen.X >= Draw.X && Seen.Y >= Draw.Y);
        TestTrue(TEXT("nor magnified past 1.25 times, so text stays crisp"),
                 Seen.X <= 1.25 * Draw.X && Seen.Y <= 1.25 * Draw.Y);
    }

    // -- who may drive it, from where: class decisions, not placed ones -----
    TestTrue(TEXT("the chart is drivable from its own chair, unzoomed"), GetDefault<AShipNavScreen>()->IsDrivableFromChartChair());
    TestFalse(TEXT("and still not from the helm"), GetDefault<AShipNavScreen>()->IsDrivableSeated());
    TestTrue(TEXT("the map is drivable from both seats"),
             GetDefault<AShipMapScreen>()->IsDrivableSeated() && GetDefault<AShipMapScreen>()->IsDrivableFromChartChair());
    TestFalse(TEXT("the laptop from neither"),
              GetDefault<AShipLaptop>()->IsDrivableSeated() || GetDefault<AShipLaptop>()->IsDrivableFromChartChair());

    const auto CheckWords = [this, &Draw](const TArray<FWord>& Words, const TCHAR* When)
    {
        TestTrue(FString::Printf(TEXT("%s: there are words to check (%d)"), When, Words.Num()), Words.Num() >= 10);
        for (const FWord& Word : Words)
        {
            AddInfo(FString::Printf(TEXT("%s: '%s' at (%.0f, %.0f) %.0f x %.0f, wants %.0f x %.0f"), When, *Word.Text,
                                    Word.At.X, Word.At.Y, Word.Size.X, Word.Size.Y, Word.Wants.X, Word.Wants.Y));
            TestTrue(FString::Printf(TEXT("%s: '%s' is on the panel"), When, *Word.Text),
                     Word.At.X >= -0.5 && Word.At.Y >= -0.5 && Word.End().X <= Draw.X + 0.5 && Word.End().Y <= Draw.Y + 0.5);
            TestTrue(FString::Printf(TEXT("%s: '%s' is given the room it asks for (%.0f x %.0f of %.0f x %.0f)"), When,
                                     *Word.Text, Word.Size.X, Word.Size.Y, Word.Wants.X, Word.Wants.Y),
                     Word.Size.X + 0.5 >= Word.Wants.X && Word.Size.Y + 0.5 >= Word.Wants.Y);
        }
        for (int32 A = 0; A < Words.Num(); ++A)
        {
            for (int32 B = A + 1; B < Words.Num(); ++B)
            {
                const FBox2D First(Words[A].At, Words[A].End());
                const FBox2D Second(Words[B].At, Words[B].End());
                const bool bApart = First.Max.X <= Second.Min.X + 0.5 || Second.Max.X <= First.Min.X + 0.5
                    || First.Max.Y <= Second.Min.Y + 0.5 || Second.Max.Y <= First.Min.Y + 0.5;
                if (!bApart)
                {
                    AddError(FString::Printf(TEXT("%s: '%s' and '%s' overlap"), When, *Words[A].Text, *Words[B].Text));
                }
            }
        }
    };

    // -- as the ship has it: the rows line up -------------------------------
    TArray<FStarSystemStub> Stubs = Test.Ship->GetChart();
    if (!TestTrue(FString::Printf(TEXT("the chart has a full list to lay out (%d)"), Stubs.Num()),
                  Stubs.Num() >= UNavigationWidget::RowCount))
    {
        return false;
    }
    Stubs.SetNum(UNavigationWidget::RowCount);
    Test.Ship->PlotCourse(Stubs[1].Id);
    Chart->RefreshFromShip();
    {
        const TArray<FWord> Words = LayOut(*Chart, Draw);
        CheckWords(Words, TEXT("as it opens"));

        TArray<double> NameLefts;
        TArray<double> DistanceRights;
        TArray<double> ClassLefts;
        TArray<double> RowTops;
        const FUniversePosition Where = Test.Ship->GetFlightState().GetUniversePosition();
        for (const FStarSystemStub& Stub : Stubs)
        {
            const FWord* Name = Find(Words, Stub.Name);
            const FWord* Distance = Find(Words, NavText::Distance(Where.DistanceTo(Stub.Position)));
            if (!TestTrue(FString::Printf(TEXT("%s's name and distance are on the glass"), *Stub.Name), Name && Distance))
            {
                continue;
            }
            NameLefts.Add(Name->At.X);
            DistanceRights.Add(Distance->At.X + Distance->Wants.X);
            RowTops.Add(Name->At.Y);
        }
        for (const FWord& Word : Words)
        {
            for (const EStarClass Class : { EStarClass::M, EStarClass::K, EStarClass::G, EStarClass::F, EStarClass::A, EStarClass::B })
            {
                if (Word.Text == NavText::StarClass(Class))
                {
                    ClassLefts.Add(Word.At.X);
                }
            }
        }
        const auto Spread = [](const TArray<double>& Values)
        {
            return Values.Num() ? FMath::Max(Values) - FMath::Min(Values) : 0.0;
        };
        TestEqual(TEXT("every row's class was found"), ClassLefts.Num(), Stubs.Num());
        TestTrue(FString::Printf(TEXT("the names start at one x in every row (spread %.1f px)"), Spread(NameLefts)),
                 NameLefts.Num() == Stubs.Num() && Spread(NameLefts) < 0.5);
        TestTrue(FString::Printf(TEXT("the plotted name does not shift: its mark has a column (spread %.1f px)"), Spread(NameLefts)),
                 Spread(NameLefts) < 0.5);
        TestTrue(FString::Printf(TEXT("the distances end at one x (spread %.1f px)"), Spread(DistanceRights)),
                 Spread(DistanceRights) < 1.0);
        TestTrue(FString::Printf(TEXT("the classes start at one x (spread %.1f px)"), Spread(ClassLefts)),
                 Spread(ClassLefts) < 0.5);
        bool bEven = RowTops.Num() == Stubs.Num();
        for (int32 Index = 2; bEven && Index < RowTops.Num(); ++Index)
        {
            bEven = FMath::IsNearlyEqual(RowTops[Index] - RowTops[Index - 1], RowTops[1] - RowTops[0], 0.5);
        }
        TestTrue(TEXT("and the rows are evenly spaced"), bEven);

        const FWord* Mark = Find(Words, UNavigationWidget::PlottedMark);
        TestTrue(TEXT("the plotted row carries the mark, left of its name"),
                 Mark && NameLefts.Num() > 1 && Mark->End().X <= NameLefts[0] + 0.5);
    }

    // -- the widest it can print --------------------------------------------
    // The longest names in the corpus (Saved/procgen_corpus.tsv), the widest
    // class, a named world's in-system course with the longest bearing, the
    // longer toggle label and the longest jump word.
    {
        const FUniversePosition Where = Test.Ship->GetFlightState().GetUniversePosition();
        const TCHAR* const Longest[] = { TEXT("Sharsathhaith"), TEXT("Drorzardaith"), TEXT("Trethnaermor"),
                                         TEXT("Tithrainvosh"), TEXT("Kishkitraesh"), TEXT("Kishpathdail") };
        for (int32 Index = 0; Index < Stubs.Num(); ++Index)
        {
            Rewrite(*Chart, Stubs[Index].Name, Longest[Index]);
            Rewrite(*Chart, NavText::StarClass(Stubs[Index].Class), NavText::StarClass(EStarClass::F));
            Rewrite(*Chart, NavText::Distance(Where.DistanceTo(Stubs[Index].Position)), TEXT("12.0 ly"));
        }
        // Every row visited: the column the chart leaves empty for a
        // system never reached -- the blocks with no words, in the list's
        // dim, which only the visited column is.
        int32 VisitedColumns = 0;
        Chart->WidgetTree->ForEachWidget([&VisitedColumns](UWidget* Widget)
        {
            UTextBlock* Text = Cast<UTextBlock>(Widget);
            if (Text && Text->GetText().IsEmpty()
                && Text->GetColorAndOpacity().GetSpecifiedColor().Equals(UShipScreenWidget::Dim))
            {
                Text->SetText(FText::FromString(NavText::Visited(true)));
                ++VisitedColumns;
            }
        });
        TestEqual(TEXT("a visited column in every row"), VisitedColumns, UNavigationWidget::RowCount);
        const FString Here = Chart->GetHereText().ToString();
        Rewrite(*Chart, Here, NavText::Place(TEXT("Sharsathhaith"), EStarClass::F, true));
        Rewrite(*Chart, Chart->GetJumpText().ToString(), NavText::JumpWord(EJumpState::Transit) + TEXT("."));
        Rewrite(*Chart, Chart->GetCourseText().ToString(),
                FString(UNavigationWidget::PlottedMark) + TEXT(" Kokraisaeth · Sharsathhaith III") + NavText::Separator
                    + UNavigationWidget::InSystemWords + NavText::Separator + TEXT("90° to starboard, 89° up."));
        Rewrite(*Chart, Chart->GetEngageLabel().ToString(), TEXT("STAND DOWN"));
        CheckWords(LayOut(*Chart, Draw), TEXT("at its widest"));
    }

    return true;
}

#endif
