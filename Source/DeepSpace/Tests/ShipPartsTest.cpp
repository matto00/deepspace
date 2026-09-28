#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipParts.h"

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

#endif
