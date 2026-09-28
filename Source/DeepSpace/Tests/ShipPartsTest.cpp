#include "Misc/AutomationTest.h"
#include <limits>
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipPartCatalogue.h"
#include "Ship/ShipParts.h"
#include "Tests/ShipPartsJson.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The wear and upgrades spec's pure core: the bays, the ratings, the stock
 * ship as numbers (ruling 1: today's ship, bit for bit), and the one rule for
 * a console variable over a fitted part (decision 6).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsArithmeticTest, "DeepSpace.Ship.Parts.Arithmetic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPartsArithmeticTest::RunTest(const FString& Parameters)
{
    // -- the stock ship is today's ship ----------------------------------------
    const FShipRatings Stock = FShipRatings::Stock();
    TestEqual(TEXT("the stock reactor makes 1400 W"), Stock.ReactorWatts, 1400.0);
    TestEqual(TEXT("the lights want 300 W"), Stock.LightsWant, 300.0);
    TestEqual(TEXT("the boosters want 450 W"), Stock.BoostersWant, 450.0);
    TestEqual(TEXT("and push cruise's own 2 km/s^2"), Stock.LinearAcceleration, FShipFlightLimits::Cruise().LinearAcceleration);
    TestEqual(TEXT("the jump winds on 380 W"), Stock.WindingWant, 380.0);
    TestEqual(TEXT("in the settled 45 s"), Stock.ChargeSeconds, FShipFlightState::JumpChargeSeconds);
    TestEqual(TEXT("the drive follows its lever at 3 notches a second"), Stock.DriveResponse, ShipDriveLever::DefaultResponse);
    TestEqual(TEXT("and the chart reaches 12 ly"), Stock.RangeLy, 12.0);

    // -- every rating reads back, has a name, and has one owner ----------------
    TestEqual(TEXT("eight ratings"), ShipParts::AllRatings().Num(), 8);
    for (const EShipRating Rating : ShipParts::AllRatings())
    {
        const FString Name = ShipParts::RatingName(Rating).ToString();
        FShipRatings Moved = Stock;
        Moved.Set(Rating, 1234.5);
        TestEqual(FString::Printf(TEXT("%s reads back what was set"), *Name), Moved.Get(Rating), 1234.5);
        TestTrue(FString::Printf(TEXT("%s round-trips through its name"), *Name),
                 ShipParts::RatingFromName(Name) == TOptional<EShipRating>(Rating));
        TestTrue(FString::Printf(TEXT("%s is owned by a core bay"), *Name), ShipBay::IsCore(ShipParts::OwnerOf(Rating)));
    }
    TestFalse(TEXT("a name no rating has is none"), ShipParts::RatingFromName(TEXT("TopSpeed")).IsSet());
    TestTrue(TEXT("the drive owns its response"), ShipParts::OwnerOf(EShipRating::DriveResponse) == EShipBay::Drive);
    TestTrue(TEXT("the sensors own the range"), ShipParts::OwnerOf(EShipRating::RangeLy) == EShipBay::Sensors);

    // -- a part's ratings over stock; a rating it lacks reads stock ------------
    FShipPartSpec TwinCore;
    TwinCore.Id = TEXT("Reactor.TwinCore");
    TwinCore.Bay = EShipBay::Reactor;
    TwinCore.Ratings.Add(EShipRating::ReactorWatts, 1800.0);
    const FShipRatings Fitted = ShipParts::RatingsOf({ TwinCore });
    TestEqual(TEXT("the twin core rates 1800 W"), Fitted.ReactorWatts, 1800.0);
    TestEqual(TEXT("and leaves the lights' want at stock, never zero"), Fitted.LightsWant, Stock.LightsWant);
    for (const EShipRating Rating : ShipParts::AllRatings())
    {
        TestEqual(FString::Printf(TEXT("no parts reads stock %s"), *ShipParts::RatingName(Rating).ToString()),
                  ShipParts::RatingsOf({}).Get(Rating), Stock.Get(Rating));
    }

    // -- a CVar over a part (decision 6; review focus 1) -----------------------
    TestEqual(TEXT("-1, the default, reads the part"), ShipParts::Effective(12.0, -1.0f), 12.0);
    TestEqual(TEXT("15 reads 15"), ShipParts::Effective(12.0, 15.0f), 15.0);
    TestEqual(TEXT("0 is an override too: the tests' instant charge"), ShipParts::Effective(45.0, 0.0f), 0.0);
    TestEqual(TEXT("any negative value reads the part, never itself"), ShipParts::Effective(12.0, -5.0f), 12.0);

    // -- the bays ---------------------------------------------------------------
    TestEqual(TEXT("six core bays and two auxiliary slots"), ShipBay::All().Num(), 8);
    TestFalse(TEXT("None is not a bay"), ShipBay::All().Contains(EShipBay::None));
    for (const EShipBay Bay : ShipBay::All())
    {
        const FName Name = ShipBay::Name(Bay);
        TestTrue(FString::Printf(TEXT("%s round-trips through its name"), *Name.ToString()),
                 ShipBay::FromName(Name) == TOptional<EShipBay>(Bay));
        TestTrue(FString::Printf(TEXT("%s is core or aux, never both"), *Name.ToString()), ShipBay::IsCore(Bay) != ShipBay::IsAux(Bay));
    }
    TestFalse(TEXT("a bay the ship has not got is none"), ShipBay::FromName(TEXT("Galley")).IsSet());
    TestFalse(TEXT("and neither is None"), ShipBay::FromName(NAME_None).IsSet());
    TestFalse(TEXT("None is neither core nor aux"), ShipBay::IsCore(EShipBay::None) || ShipBay::IsAux(EShipBay::None));
    TestEqual(TEXT("draws are booked by bay"), ShipBay::DrawKey(EShipBay::Lights), FName(TEXT("Bay.Lights")));
    TestEqual(TEXT("a core bay's stock part is <Bay>.Stock"), ShipBay::StockPartId(EShipBay::Reactor), FName(TEXT("Reactor.Stock")));
    TestEqual(TEXT("life support's too"), ShipBay::StockPartId(EShipBay::LifeSupport), FName(TEXT("LifeSupport.Stock")));
    TestEqual(TEXT("an aux slot has none"), ShipBay::StockPartId(EShipBay::Aux1), FName(NAME_None));
    TestEqual(TEXT("the sensors' plate says NAV"), ShipBay::PlateLabel(EShipBay::Sensors), FString(TEXT("NAV")));
    TestEqual(TEXT("an aux plate says AUX"), ShipBay::PlateLabel(EShipBay::Aux2), FString(TEXT("AUX")));

    // -- the plain state: one entry per bay, found by name ---------------------
    FShipLoadoutState Empty = ShipParts::EmptyLoadout();
    TestEqual(TEXT("an empty loadout lists every bay"), Empty.Bays.Num(), ShipBay::All().Num());
    TestEqual(TEXT("and no spares"), Empty.Spares.Num(), 0);
    for (const EShipBay Bay : ShipBay::All())
    {
        const FShipBayState* Entry = ShipParts::FindBay(Empty, Bay);
        TestTrue(FString::Printf(TEXT("%s is found by name and holds nothing"), *ShipBay::Name(Bay).ToString()),
                 Entry && Entry->Bay == ShipBay::Name(Bay) && Entry->Part.PartId.IsNone() && Entry->LivesDrawn == 0);
    }
    TestNull(TEXT("None is not in it"), ShipParts::FindBay(Empty, EShipBay::None));
    return true;
}

/*
 * A module is a part (decision 1): it says which bay it fits, and a part
 * whose bay was never set reads None rather than the reactor.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsAssetSpecTest, "DeepSpace.Ship.Parts.AssetSpec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPartsAssetSpecTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("a part whose bay was never set reads None, never the reactor"),
             GetDefault<UShipModuleDataAsset>()->Bay == EShipBay::None);

    UShipModuleDataAsset* Part = NewObject<UShipModuleDataAsset>();
    Part->ModuleId = TEXT("Drive.QuickLever");
    Part->Bay = EShipBay::Drive;
    Part->PowerDraw = 25.0f;
    Part->Words = FText::FromString(TEXT("Takes the lever before you have let go of it."));
    Part->Ratings.Add(EShipRating::DriveResponse, 4.5);

    const FShipPartSpec Spec = Part->GetSpec();
    TestEqual(TEXT("the spec carries the id"), Spec.Id, FName(TEXT("Drive.QuickLever")));
    TestTrue(TEXT("and the bay"), Spec.Bay == EShipBay::Drive);
    TestEqual(TEXT("and the draw"), Spec.Draw, 25.0);
    TestTrue(TEXT("and exactly the ratings it sets"),
             Spec.Ratings.Num() == 1 && Spec.Ratings.Contains(EShipRating::DriveResponse) && Spec.Ratings[EShipRating::DriveResponse] == 4.5);

    UShipPartCatalogue* Catalogue = NewObject<UShipPartCatalogue>();
    Catalogue->Parts.Add(Part);
    TestTrue(TEXT("a catalogue holds its parts by soft pointer"), Catalogue->Parts.Num() == 1 && Catalogue->Parts[0].Get() == Part);
    return true;
}

/*
 * Decision 7 over the whole catalogue: no loadout has a right answer. Every
 * combination is whole at rest under the stock reactor; every upgrade is at
 * least as open as stock on every axis of its bay; no part in a bay
 * dominates another (equal numbers do not); a core part rates only its own
 * bay; an aux part rates nothing and wants nothing at rest (decision 8).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsCatalogueRulesTest, "DeepSpace.Ship.Parts.CatalogueRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipPartsTestLocal
{
    FShipPartSpec Part(const TCHAR* Id, EShipBay Bay, double Draw, const TArray<TPair<EShipRating, double>>& Ratings = {})
    {
        FShipPartSpec Spec;
        Spec.Id = Id;
        Spec.Bay = Bay;
        Spec.Draw = Draw;
        for (const TPair<EShipRating, double>& Rated : Ratings)
        {
            Spec.Ratings.Add(Rated.Key, Rated.Value);
        }
        return Spec;
    }

    bool Says(const TArray<FString>& Problems, const TCHAR* Needle)
    {
        return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
    }
}

bool FShipPartsCatalogueRulesTest::RunTest(const FString& Parameters)
{
    using namespace ShipPartsTestLocal;
    const ShipPartsJson::FCatalogue Catalogue = ShipPartsJson::Read();
    TestTrue(FString::Printf(TEXT("the catalogue reads cleanly (%s)"), *FString::Join(Catalogue.Problems, TEXT("; "))),
             Catalogue.Problems.IsEmpty());
    const TArray<FShipPartSpec> Specs = ShipPartsJson::Specs(Catalogue);
    TestEqual(TEXT("six stock parts and the two examples"), Specs.Num(), 8);
    const TArray<FString> Problems = ShipParts::Validate(Specs);
    TestTrue(FString::Printf(TEXT("every rule holds over every row (%s)"), *FString::Join(Problems, TEXT("; "))), Problems.IsEmpty());

    // The stock loadout, the heaviest there is, asks 1370 W at rest.
    double Heaviest = 0.0;
    for (const EShipBay Bay : ShipBay::All())
    {
        double Worst = 0.0;
        for (const FShipPartSpec& Spec : Specs)
        {
            Worst = Spec.Bay == Bay ? FMath::Max(Worst, ShipParts::AtRestWatts(Spec)) : Worst;
        }
        Heaviest += Worst;
    }
    TestEqual(TEXT("the heaviest loadout asks 1370 W at rest"), Heaviest, 1370.0);

    // Each rule, broken once, is refused by name.
    const auto Refused = [&](const TCHAR* What, const FShipPartSpec& Added, const TCHAR* Needle)
    {
        TArray<FShipPartSpec> With = Specs;
        With.Add(Added);
        const TArray<FString> Found = ShipParts::Validate(With);
        TestTrue(FString::Printf(TEXT("%s is refused (%s)"), What, *FString::Join(Found, TEXT("; "))), Says(Found, Needle));
    };
    Refused(TEXT("a brighter lights part, wanting 350 W"),
            Part(TEXT("Lights.Bright"), EShipBay::Lights, 120.0, { { EShipRating::LightsWant, 350.0 } }), TEXT("at rest"));
    Refused(TEXT("a shorter array"),
            Part(TEXT("Sensors.Short"), EShipBay::Sensors, 200.0, { { EShipRating::RangeLy, 10.0 } }), TEXT("less open than stock"));
    Refused(TEXT("a slower drive than the quick lever on one axis and no better on any"),
            Part(TEXT("Drive.Middling"), EShipBay::Drive, 0.0, { { EShipRating::DriveResponse, 4.0 } }), TEXT("dominates"));
    Refused(TEXT("a reactor that rates the range"),
            Part(TEXT("Reactor.Odd"), EShipBay::Reactor, 0.0, { { EShipRating::RangeLy, 20.0 } }), TEXT("owns"));
    Refused(TEXT("an aux part with a number"),
            Part(TEXT("Aux.Booster"), EShipBay::Aux1, 0.0, { { EShipRating::RangeLy, 20.0 } }), TEXT("verbs"));
    Refused(TEXT("an aux part that draws at rest"),
            Part(TEXT("Aux.Lamp"), EShipBay::Aux1, 40.0), TEXT("wants nothing"));
    Refused(TEXT("a second part under a known id"),
            Part(TEXT("Reactor.TwinCore"), EShipBay::Reactor, 0.0, { { EShipRating::ReactorWatts, 1800.0 } }), TEXT("share the id"));
    Refused(TEXT("a part that fits no bay"), Part(TEXT("Nowhere.Part"), EShipBay::None, 0.0), TEXT("fits no bay"));
    Refused(TEXT("a part with no id"),
            Part(TEXT(""), EShipBay::Lights, 120.0, { { EShipRating::LightsWant, 300.0 } }), TEXT("has no id"));
    Refused(TEXT("a lights part that draws -50 W, supply from a bay that is not the reactor"),
            Part(TEXT("Lights.Sink"), EShipBay::Lights, -50.0, { { EShipRating::LightsWant, 300.0 } }), TEXT("a negative draw"));
    Refused(TEXT("a draw that is not a number"),
            Part(TEXT("Lights.Nan"), EShipBay::Lights, std::numeric_limits<double>::quiet_NaN(), { { EShipRating::LightsWant, 300.0 } }), TEXT("not a number"));

    // Every rating is positive and finite: none reads as "more open" by being
    // negative, zero or NaN.
    Refused(TEXT("a drive that charges in -1 s"),
            Part(TEXT("Drive.Instant"), EShipBay::Drive, 0.0, { { EShipRating::ChargeSeconds, -1.0 } }), TEXT("positive, finite"));
    Refused(TEXT("a drive that winds on -50 W"),
            Part(TEXT("Drive.Giver"), EShipBay::Drive, 0.0, { { EShipRating::WindingWant, -50.0 } }), TEXT("positive, finite"));
    Refused(TEXT("lights that want nothing"),
            Part(TEXT("Lights.Dark"), EShipBay::Lights, 120.0, { { EShipRating::LightsWant, 0.0 } }), TEXT("positive, finite"));
    Refused(TEXT("a range that is not a number"),
            Part(TEXT("Sensors.Nan"), EShipBay::Sensors, 200.0, { { EShipRating::RangeLy, std::numeric_limits<double>::quiet_NaN() } }), TEXT("positive, finite"));

    // Every axis of decision 7, by direction: worse than stock on it alone is
    // refused by its name. Each probe is better on another axis, so without
    // the axis it would pass (or read only as dominating the quick lever).
    Refused(TEXT("a drive quicker than the quick lever that draws 25 W"),
            Part(TEXT("Drive.Hungry"), EShipBay::Drive, 25.0, { { EShipRating::DriveResponse, 5.0 } }),
            TEXT("less open than stock on its draw"));
    Refused(TEXT("a drive quicker than the quick lever that takes 60 s to charge"),
            Part(TEXT("Drive.SlowCharge"), EShipBay::Drive, 0.0,
                 { { EShipRating::DriveResponse, 5.0 }, { EShipRating::ChargeSeconds, 60.0 } }),
            TEXT("less open than stock on ChargeSeconds"));
    Refused(TEXT("a booster cluster that wants 470 W, still whole at rest"),
            Part(TEXT("Boosters.Hungry"), EShipBay::Boosters, 0.0, { { EShipRating::BoostersWant, 470.0 } }),
            TEXT("less open than stock on BoostersWant"));
    Refused(TEXT("a booster cluster that pushes 1.5 km/s^2"),
            Part(TEXT("Boosters.Weak"), EShipBay::Boosters, 0.0, { { EShipRating::LinearAcceleration, 150000.0 } }),
            TEXT("less open than stock on LinearAcceleration"));
    {
        TArray<FShipPartSpec> Without = Specs;
        Without.RemoveAll([](const FShipPartSpec& Spec) { return Spec.Id == FName(TEXT("Sensors.Stock")); });
        TestTrue(TEXT("a core bay with no stock part is refused"), Says(ShipParts::Validate(Without), TEXT("no stock part")));
    }

    // Equal numbers do not dominate: a part that differs only in character is legal.
    {
        TArray<FShipPartSpec> Twins = Specs;
        Twins.Add(Part(TEXT("Drive.QuickLeverTwin"), EShipBay::Drive, 0.0,
                       { { EShipRating::WindingWant, 380.0 }, { EShipRating::ChargeSeconds, 45.0 }, { EShipRating::DriveResponse, 4.5 } }));
        const TArray<FString> Found = ShipParts::Validate(Twins);
        TestTrue(FString::Printf(TEXT("two parts with equal numbers are both legal (%s)"), *FString::Join(Found, TEXT("; "))), Found.IsEmpty());
    }
    // And better than stock on those same axes is legal, so each axis's
    // direction is pinned both ways.
    {
        TArray<FShipPartSpec> Opener = Specs;
        Opener.Add(Part(TEXT("Lights.Frugal"), EShipBay::Lights, 100.0, { { EShipRating::LightsWant, 300.0 } }));
        Opener.Add(Part(TEXT("Drive.QuickCharge"), EShipBay::Drive, 0.0, { { EShipRating::ChargeSeconds, 30.0 } }));
        Opener.Add(Part(TEXT("Boosters.Frugal"), EShipBay::Boosters, 0.0, { { EShipRating::BoostersWant, 400.0 } }));
        Opener.Add(Part(TEXT("Boosters.Strong"), EShipBay::Boosters, 0.0, { { EShipRating::LinearAcceleration, 250000.0 } }));
        const TArray<FString> Found = ShipParts::Validate(Opener);
        TestTrue(FString::Printf(TEXT("a lower draw, a quicker charge, a lower want and a harder push are legal (%s)"),
                                 *FString::Join(Found, TEXT("; "))), Found.IsEmpty());
    }
    // And two parts that trade one axis for another neither dominates.
    {
        TArray<FShipPartSpec> Traded = Specs;
        Traded.Add(Part(TEXT("Drive.Frugal"), EShipBay::Drive, 0.0, { { EShipRating::WindingWant, 300.0 } }));
        const TArray<FString> Found = ShipParts::Validate(Traded);
        TestTrue(FString::Printf(TEXT("a lower-want drive beside the quick lever is legal (%s)"), *FString::Join(Found, TEXT("; "))), Found.IsEmpty());
    }
    return true;
}

/*
 * The catalogue on three sides, as DeepSpace.Sky.MaterialContract holds the
 * sky (decision 5): Tools/ship_parts.json, the assets the script authored
 * from it, and FShipRatings::Stock() against the stock rows. A settled
 * number goes into the JSON and the script is re-run; this fails until both
 * are done.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsContractTest, "DeepSpace.Ship.Parts.Contract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPartsContractTest::RunTest(const FString& Parameters)
{
    const ShipPartsJson::FCatalogue Catalogue = ShipPartsJson::Read();
    if (!TestTrue(TEXT("the catalogue reads"), Catalogue.Problems.IsEmpty() && Catalogue.Rows.Num() > 0))
    {
        return false;
    }

    // -- the assets are the JSON ------------------------------------------------
    for (const ShipPartsJson::FRow& Row : Catalogue.Rows)
    {
        const FString Id = Row.Spec.Id.ToString();
        const FString Path = ShipPartsJson::ObjectPath(Catalogue, Row.Asset);
        const UShipModuleDataAsset* Part = LoadObject<UShipModuleDataAsset>(nullptr, *Path);
        if (!TestNotNull(FString::Printf(TEXT("%s is authored at %s"), *Id, *Path), Part))
        {
            continue;
        }
        TestEqual(FString::Printf(TEXT("%s: its id"), *Id), Part->ModuleId, Row.Spec.Id);
        TestTrue(FString::Printf(TEXT("%s: its bay"), *Id), Part->Bay == Row.Spec.Bay);
        TestEqual(FString::Printf(TEXT("%s: its draw"), *Id), static_cast<double>(Part->PowerDraw), Row.Spec.Draw);
        TestEqual(FString::Printf(TEXT("%s: its name"), *Id), Part->DisplayName.ToString(), Row.Name);
        TestEqual(FString::Printf(TEXT("%s: its words"), *Id), Part->Words.ToString(), Row.Words);
        TestEqual(FString::Printf(TEXT("%s: as many ratings"), *Id), Part->Ratings.Num(), Row.Spec.Ratings.Num());
        for (const TPair<EShipRating, double>& Rated : Row.Spec.Ratings)
        {
            const double* Held = Part->Ratings.Find(Rated.Key);
            TestTrue(FString::Printf(TEXT("%s: %s is %g"), *Id, *ShipParts::RatingName(Rated.Key).ToString(), Rated.Value),
                     Held && *Held == Rated.Value);
        }
    }

    // -- the catalogue asset lists exactly them, in the JSON's order -------------
    const FString CataloguePath = ShipPartsJson::ObjectPath(Catalogue, Catalogue.CatalogueAsset);
    if (const UShipPartCatalogue* Asset = LoadObject<UShipPartCatalogue>(nullptr, *CataloguePath);
        TestNotNull(FString::Printf(TEXT("the catalogue is authored at %s"), *CataloguePath), Asset))
    {
        TestEqual(TEXT("it lists every row and nothing else"), Asset->Parts.Num(), Catalogue.Rows.Num());
        for (int32 Index = 0; Index < FMath::Min(Asset->Parts.Num(), Catalogue.Rows.Num()); ++Index)
        {
            TestEqual(FString::Printf(TEXT("row %d is %s"), Index, *Catalogue.Rows[Index].Asset),
                      Asset->Parts[Index].ToSoftObjectPath().ToString(), ShipPartsJson::ObjectPath(Catalogue, Catalogue.Rows[Index].Asset));
        }
    }

    // -- the stock rows are FShipRatings::Stock(), every owned rating explicit ---
    const FShipRatings Stock = FShipRatings::Stock();
    for (const EShipBay Bay : ShipBay::All())
    {
        if (!ShipBay::IsCore(Bay))
        {
            continue;
        }
        const ShipPartsJson::FRow* Row = Catalogue.Rows.FindByPredicate([Bay](const ShipPartsJson::FRow& Candidate)
        {
            return Candidate.Spec.Id == ShipBay::StockPartId(Bay);
        });
        if (!TestNotNull(FString::Printf(TEXT("the %s bay has a stock row"), *ShipBay::Name(Bay).ToString()), Row))
        {
            continue;
        }
        for (const EShipRating Rating : ShipParts::AllRatings())
        {
            if (ShipParts::OwnerOf(Rating) != Bay)
            {
                continue;
            }
            const double* Rated = Row->Spec.Ratings.Find(Rating);
            TestTrue(FString::Printf(TEXT("%s rates %s as Stock() does (%g)"), *Row->Spec.Id.ToString(),
                                     *ShipParts::RatingName(Rating).ToString(), Stock.Get(Rating)),
                     Rated && *Rated == Stock.Get(Rating));
        }
    }

    // -- today's three draws, carried over exactly ------------------------------
    double Draws = 0.0;
    for (const TCHAR* Id : { TEXT("LifeSupport.Stock"), TEXT("Lights.Stock"), TEXT("Sensors.Stock") })
    {
        const ShipPartsJson::FRow* Row = Catalogue.Rows.FindByPredicate([Id](const ShipPartsJson::FRow& Candidate)
        {
            return Candidate.Spec.Id == FName(Id);
        });
        Draws += Row ? Row->Spec.Draw : 0.0;
    }
    TestEqual(TEXT("life support, lights and sensors draw 620 W together, as the hand-made modules did"), Draws, 620.0);
    return true;
}

#endif
