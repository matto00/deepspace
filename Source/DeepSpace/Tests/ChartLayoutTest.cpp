#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Layout/ArrangedChildren.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Rendering/SlateRenderer.h"
#include "Ship/ShipMapScreen.h"
#include "Ship/ShipNavScreen.h"
#include "Tests/SkyTestWorld.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "UI/SystemMapWidget.h"
#include "Universe/GenPriors.h"
#include "Universe/StarSystem.h"
#include "Universe/SystemNames.h"
#include "Universe/UniverseUnits.h"
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
    /** hauler_layout's NAV_SCREEN glass, resolved to world space (the
     *  cockpit's (x, y) is the world's (x + 1410, y - 200)). Only where the
     *  chart is spawned: its chair's eye is asked of the chart itself. */
    const FVector ChartGlass(1711.0, 85.0, 105.0);

    /** The map beside it, hauler_layout's MAP_SCREEN. */
    const FVector MapGlass(1711.0, 0.0, 105.0);

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

    /** Every font size a screen's words are set in. */
    TArray<float> FontSizes(UUserWidget& Screen)
    {
        TArray<float> Sizes;
        Screen.WidgetTree->ForEachWidget([&Sizes](UWidget* Widget)
        {
            if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
            {
                Sizes.AddUnique(Text->GetFont().Size);
            }
        });
        return Sizes;
    }

    /** A panel's width in the world, cm: its quad's scale times its pixels. */
    double PanelWidthCm(const AShipScreen& Screen)
    {
        return Screen.GetScreen()->GetComponentScale().X * Screen.GetScreen()->GetDrawSize().X;
    }

    /** Of Candidates, the one the font draws widest. */
    FString Widest(const TArray<FString>& Candidates, const FSlateFontInfo& Font)
    {
        const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        FString Best;
        double BestWidth = -1.0;
        for (const FString& Candidate : Candidates)
        {
            const double Width = Measure->Measure(Candidate, Font).X;
            if (Width > BestWidth)
            {
                Best = Candidate;
                BestWidth = Width;
            }
        }
        return Best;
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
 * The chart's layout, at the map's standard (the playtest note of 2026-09-27: "the jump
 * menu has spacing issues"). The tree is laid out as Slate lays it out, with
 * no renderer, and every word on the glass is held to three things: it is on
 * the panel, it is given at least the room it asks for (nothing clipped, and
 * nothing run into its neighbour), and no two words overlap. Each row's
 * columns line up with every other row's, and plotting a row moves no name.
 * Then every word is rewritten to the widest the chart can print -- asked of
 * the name tables (SystemNames::WidestName), NavText and the font, never
 * typed in -- and held to the same.
 *
 * Its draw size is held to how it is seen: from its own chair's eye -- the
 * chart's own seat, which place_nav_screen tunes per instance, and the
 * seated eye every seat shares -- the panel spans at least its pixels on the
 * 4K display, so it is never minified where it is read. And its type is the
 * map's: every size the chart sets is one the map sets, at the same
 * centimetres on the glass. Who may drive it from a seat is
 * DeepSpace.UI.SystemMapScreen's and DeepSpace.Ship.ChartChair's.
 */
bool FChartLayoutTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ChartLayoutTestLocal;

    FSkyWorld Test(TEXT("ChartLayoutWorld"));
    AShipNavScreen* Screen = Test.World->SpawnActor<AShipNavScreen>(ChartGlass, FRotator::ZeroRotator);
    AShipMapScreen* MapScreen = Test.World->SpawnActor<AShipMapScreen>(MapGlass, FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the chart spawns"), Screen) || !TestNotNull(TEXT("and the map beside it"), MapScreen))
    {
        return false;
    }
    Test.BeginPlay();
    Screen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);
    MapScreen->GetScreen()->TickComponent(0.016f, LEVELTICK_All, nullptr);
    UNavigationWidget* Chart = Cast<UNavigationWidget>(Screen->GetScreen()->GetUserWidgetObject());
    USystemMapWidget* Map = Cast<USystemMapWidget>(MapScreen->GetScreen()->GetUserWidgetObject());
    if (!TestNotNull(TEXT("the chart's panel made its widget"), Chart) || !TestNotNull(TEXT("and the map's"), Map))
    {
        return false;
    }
    Test.Ship->Tick(0.1f);
    Chart->RefreshFromShip();
    const FVector2D Draw = Screen->GetScreen()->GetDrawSize();

    // -- the pixels are the chair's ----------------------------------------
    {
        // The field of view the player's own view has, from the Blueprint
        // the game plays: a failure if it cannot be read, never a guess.
        UClass* CharacterClass = LoadClass<ADeepSpaceCharacter>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
        const FFloatProperty* Fov = FindFProperty<FFloatProperty>(ADeepSpaceCharacter::StaticClass(), TEXT("FieldOfView"));
        if (!TestNotNull(TEXT("BP_DeepSpaceCharacter loads"), CharacterClass)
            || !TestNotNull(TEXT("the character's FieldOfView is found"), Fov))
        {
            return false;
        }
        const double Degrees = Fov->GetPropertyValue_InContainer(CharacterClass->GetDefaultObject());

        // Where the eyes are in the chart's chair: the seat's anchor, as
        // UseScreen sits the body, and the seated eye above it.
        const FTransform Seat = Screen->GetUseTransform();
        const FVector Eye = FVector(Seat.GetLocation().X, Seat.GetLocation().Y, Screen->GetUseFloorZ())
            + FRotator(0.0f, Seat.Rotator().Yaw, 0.0f).RotateVector(ADeepSpaceCharacter::SeatedEyeOffset);
        const FVector Glass = Screen->GetScreen()->GetComponentLocation();
        const FVector Facing = Screen->GetScreen()->GetComponentTransform().GetUnitAxis(EAxis::X).GetSafeNormal2D();

        const double PixelsPerTan = 0.5 * DisplayWidth / FMath::Tan(FMath::DegreesToRadians(0.5 * Degrees));
        const double WidthCm = PanelWidthCm(*Screen);
        const FVector2D Panel(WidthCm, WidthCm * Draw.Y / Draw.X);
        const FVector ToGlass = Glass - Eye;
        const double Across = -FVector::DotProduct(ToGlass, Facing);
        const double Down = FMath::Atan2(-ToGlass.Z, Across);
        const FVector2D Seen(2.0 * PixelsPerTan * 0.5 * Panel.X / Across,
                             2.0 * PixelsPerTan * 0.5 * Panel.Y * FMath::Cos(Down) / ToGlass.Size());
        AddInfo(FString::Printf(TEXT("from its chair's eye, %.0f cm from the glass, the %.0f cm chart spans %.0f x %.0f screen px at %.0f degrees; drawn at %.0f x %.0f"),
                                Across, WidthCm, Seen.X, Seen.Y, Degrees, Draw.X, Draw.Y));
        TestTrue(TEXT("the chair's eye is in front of the glass"), Across > 0.0);
        TestTrue(FString::Printf(TEXT("it is never minified where it is read (%.0f x %.0f seen, %.0f x %.0f drawn)"),
                                 Seen.X, Seen.Y, Draw.X, Draw.Y),
                 Seen.X >= Draw.X && Seen.Y >= Draw.Y);
        TestTrue(TEXT("nor magnified past 1.25 times, so text stays crisp"),
                 Seen.X <= 1.25 * Draw.X && Seen.Y <= 1.25 * Draw.Y);
    }

    // -- one type across the desk ------------------------------------------
    // The chart and the map are read side by side, so a size is judged on
    // the glass, in centimetres: points times the panel's cm per pixel.
    // Every size either sets has a partner in the other within half a point
    // at the chart's scale -- the nearest whole point.
    {
        const double ChartCmPerPx = PanelWidthCm(*Screen) / Draw.X;
        const double MapCmPerPx = PanelWidthCm(*MapScreen) / MapScreen->GetScreen()->GetDrawSize().X;
        const TArray<float> ChartSizes = FontSizes(*Chart);
        const TArray<float> MapSizes = FontSizes(*Map);
        const auto HasPartner = [ChartCmPerPx](float Size, double CmPerPx, const TArray<float>& Others, double OtherCmPerPx)
        {
            return Others.ContainsByPredicate([&](float Other)
            {
                return FMath::Abs(Size * CmPerPx - Other * OtherCmPerPx) <= 0.5 * ChartCmPerPx;
            });
        };
        TestTrue(TEXT("both screens set words"), ChartSizes.Num() > 0 && MapSizes.Num() > 0);
        for (const float Size : ChartSizes)
        {
            TestTrue(FString::Printf(TEXT("the chart's %.1f pt (%.3f cm on the glass) is a size the map sets"), Size, Size * ChartCmPerPx),
                     HasPartner(Size, ChartCmPerPx, MapSizes, MapCmPerPx));
        }
        for (const float Size : MapSizes)
        {
            TestTrue(FString::Printf(TEXT("the map's %.1f pt (%.3f cm on the glass) is a size the chart sets"), Size, Size * MapCmPerPx),
                     HasPartner(Size, MapCmPerPx, ChartSizes, ChartCmPerPx));
        }
    }

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

    // Nothing plotted: where each name starts, for the plotted pass below.
    TArray<double> UnplottedNameLefts;
    Test.Ship->ClearCourse();
    Chart->RefreshFromShip();
    {
        const TArray<FWord> Words = LayOut(*Chart, Draw);
        TestNull(TEXT("with no course, no row carries the mark"), Find(Words, UNavigationWidget::PlottedMark));
        for (const FStarSystemStub& Stub : Stubs)
        {
            const FWord* Name = Find(Words, Stub.Name);
            UnplottedNameLefts.Add(Name ? Name->At.X : -1.0);
        }
    }

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
        // Row by row against the chart with nothing plotted: a plot that
        // moved every name alike would keep them in one column and still
        // shift them.
        bool bStill = NameLefts.Num() == UnplottedNameLefts.Num();
        for (int32 Index = 0; bStill && Index < NameLefts.Num(); ++Index)
        {
            bStill = UnplottedNameLefts[Index] >= 0.0 && FMath::IsNearlyEqual(NameLefts[Index], UnplottedNameLefts[Index], 0.5);
        }
        TestTrue(TEXT("plotting moves no name: every row's name starts where it did with nothing plotted"), bStill);
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

        // On the plotted row -- Stubs[1] -- level with its name and left of
        // it, never on another row or in the title.
        const FWord* Mark = Find(Words, UNavigationWidget::PlottedMark);
        const FWord* Plotted = Find(Words, Stubs[1].Name);
        const bool bOnItsRow = Mark && Plotted
            && Mark->At.Y + 0.5 * Mark->Size.Y > Plotted->At.Y && Mark->At.Y + 0.5 * Mark->Size.Y < Plotted->End().Y;
        TestTrue(TEXT("the plotted row carries the mark, level with its name"), bOnItsRow);
        TestTrue(TEXT("and left of it"), Mark && Plotted && Mark->End().X <= Plotted->At.X + 0.5);
    }

    // -- the widest it can print --------------------------------------------
    // Each asked of what makes it, measured in the chart's own font: the
    // widest name the tables can make (not the corpus's longest, which is
    // one seed's nearest systems), the widest class, distance within the
    // chart's range, numeral, jump word and bearing, and a named world's
    // in-system course built from them.
    {
        const TArray<FWord> Before = LayOut(*Chart, Draw);
        const FWord* Sample = Find(Before, Stubs[0].Name);
        if (!TestNotNull(TEXT("a name to take the chart's font from"), Sample))
        {
            return false;
        }
        const FSlateFontInfo Font = Sample->Block->GetFont();
        const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const FString Name = SystemNames::WidestName([&Measure, &Font](const FString& Piece)
        {
            return Piece.IsEmpty() ? 0.0 : double(Measure->Measure(Piece, Font).X);
        });

        TArray<FString> Classes;
        for (const EStarClass Class : { EStarClass::M, EStarClass::K, EStarClass::G, EStarClass::F, EStarClass::A, EStarClass::B })
        {
            Classes.Add(NavText::StarClass(Class));
        }
        const FString Class = Widest(Classes, Font);
        const EStarClass WidestClass = EStarClass(Classes.IndexOfByKey(Class));

        const float RangeLy = Test.Ship->GetChartRangeLy();
        TArray<FString> Distances;
        for (int32 Tenths = 0; Tenths <= FMath::CeilToInt(10.0 * RangeLy); ++Tenths)
        {
            Distances.Add(NavText::Distance(0.1 * Tenths * UniverseUnits::CmPerLightYear));
        }
        const FString Distance = Widest(Distances, Font);

        TArray<FString> Numerals;
        for (int32 Orbit = 0; Orbit < GenGuarantees::MaxPlanets; ++Orbit)
        {
            Numerals.Add(SystemNames::Designation(Name, Orbit));
        }
        FPlanet World;
        World.GivenName = Name;
        World.Designation = Widest(Numerals, Font);

        TArray<FString> JumpWords;
        for (const EJumpState State : { EJumpState::Idle, EJumpState::Winding, EJumpState::Ready, EJumpState::Transit })
        {
            JumpWords.Add(NavText::JumpWord(State, false) + TEXT("."));
            JumpWords.Add(NavText::JumpWord(State, true) + TEXT("."));
        }

        // Every bearing the words can give, a degree apart all round.
        TSet<FString> Bearings;
        for (int32 Yaw = -180; Yaw <= 180; ++Yaw)
        {
            for (int32 Pitch = -89; Pitch <= 89; ++Pitch)
            {
                Bearings.Add(NavText::Bearing(FRotator(double(Pitch), double(Yaw), 0.0).Vector(), Test.Ship->GetJumpConeRadians()));
            }
        }
        const FString Bearing = Widest(Bearings.Array(), Font);
        AddInfo(FString::Printf(TEXT("the widest: '%s', '%s', '%s', '%s', '%s'"), *Name, *Class, *Distance,
                                *NavText::WorldName(World), *Bearing));

        const FUniversePosition Where = Test.Ship->GetFlightState().GetUniversePosition();
        for (int32 Index = 0; Index < Stubs.Num(); ++Index)
        {
            Rewrite(*Chart, Stubs[Index].Name, Name);
            Rewrite(*Chart, NavText::StarClass(Stubs[Index].Class), Class);
            Rewrite(*Chart, NavText::Distance(Where.DistanceTo(Stubs[Index].Position)), Distance);
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
        Rewrite(*Chart, Here, NavText::Place(Name, WidestClass, true));
        Rewrite(*Chart, Chart->GetJumpText().ToString(), Widest(JumpWords, Font));
        Rewrite(*Chart, Chart->GetCourseText().ToString(),
                FString(UNavigationWidget::PlottedMark) + TEXT(" ") + NavText::WorldName(World) + NavText::Separator
                    + UNavigationWidget::InSystemWords + NavText::Separator + Bearing + TEXT("."));
        Rewrite(*Chart, Chart->GetEngageLabel().ToString(), TEXT("STAND DOWN"));
        CheckWords(LayOut(*Chart, Draw), TEXT("at its widest"));
    }

    return true;
}

#endif
