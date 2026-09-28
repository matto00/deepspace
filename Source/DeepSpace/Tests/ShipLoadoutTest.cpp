#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipParts.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/StarSystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Decision 6: the four numbers a part rates are read through the ship,
 * where a console variable overrides the fitted part for the session. -1,
 * the default, is the part; 0 or more is the override, 0 included; any other
 * negative is the part. What the flight and the jump run on is the same
 * number (review focus 1).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsCVarOverridesTest, "DeepSpace.Ship.Parts.CVarOverrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPartsCVarOverridesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("CVarOverridesWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Test.BeginPlay();
    const FShipRatings Rated = Ship->GetRatings();

    struct FCase
    {
        const TCHAR* CVar;
        TFunction<float()> Read;
        double Part;
    };
    const FCase Cases[] = {
        { TEXT("ds.Nav.RangeLy"), [Ship]() { return Ship->GetChartRangeLy(); }, Rated.RangeLy },
        { TEXT("ds.Nav.ChargeSeconds"), [Ship]() { return Ship->GetChargeSeconds(); }, Rated.ChargeSeconds },
        { TEXT("ds.Nav.WindingWant"), [Ship]() { return Ship->GetWindingWant(); }, Rated.WindingWant },
        { TEXT("ds.Drive.Response"), [Ship]() { return Ship->GetDriveResponse(); }, Rated.DriveResponse },
    };
    for (const FCase& Case : Cases)
    {
        TestEqual(FString::Printf(TEXT("%s defaults to -1, the part's"), Case.CVar), CVarFloat(Case.CVar), -1.0f);
        TestEqual(FString::Printf(TEXT("%s at -1 reads the fitted part"), Case.CVar), static_cast<double>(Case.Read()), Case.Part);
        {
            FScopedCVar Set(Case.CVar, 15.0f);
            TestEqual(FString::Printf(TEXT("%s 15 reads 15"), Case.CVar), Case.Read(), 15.0f);
        }
        {
            FScopedCVar Zero(Case.CVar, 0.0f);
            TestEqual(FString::Printf(TEXT("%s 0 is an override too, and reads 0"), Case.CVar), Case.Read(), 0.0f);
        }
        {
            FScopedCVar Negative(Case.CVar, -5.0f);
            TestEqual(FString::Printf(TEXT("%s -5 reads the part, never itself"), Case.CVar), static_cast<double>(Case.Read()), Case.Part);
        }
        TestEqual(FString::Printf(TEXT("%s put back reads the part again"), Case.CVar), static_cast<double>(Case.Read()), Case.Part);
    }

    // The flight runs on the same number the getter answers.
    {
        FScopedCVar Response(TEXT("ds.Drive.Response"), 1.5f);
        Ship->Tick(0.01f);
        TestEqual(TEXT("the drive's ease runs on the override"), Ship->GetFlightState().GetLimits().DriveResponse, 1.5);
    }
    Ship->Tick(0.01f);
    TestEqual(TEXT("and on the part's response once it is put back"), Ship->GetFlightState().GetLimits().DriveResponse, Rated.DriveResponse);

    // And so does the jump.
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (TestTrue(TEXT("the chart has a system to plot"), Chart.Num() > 0 && Ship->PlotCourse(Chart[0].Id)))
    {
        if (const TOptional<FVector> Course = Ship->GetCourseDirection())
        {
            // Facing away, so a charged jump waits rather than firing.
            Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(-*Course).ToQuat());
        }
        FScopedCVar Want(TEXT("ds.Nav.WindingWant"), 200.0f);
        TestTrue(TEXT("the jump engages"), Ship->SetJumpEngaged(true));
        Ship->Tick(0.01f);
        TestEqual(TEXT("winding, the engine asks the override"), Ship->GetConsumerWant(ShipPower::Engine), 200.0f);
        Ship->SetJumpEngaged(false);
        Ship->Tick(0.01f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsEmptyBayIsStockTest, "DeepSpace.Ship.Parts.EmptyBayIsStock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsOnePerBayTest, "DeepSpace.Ship.Parts.OnePerBay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsWantsFollowTheFitTest, "DeepSpace.Ship.Parts.WantsFollowTheFit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipLoadoutTestLocal
{
    /** A part made in code, as a test's fixture: never in the catalogue. */
    UShipModuleDataAsset* MakePart(const TCHAR* Id, EShipBay Bay, float Draw, const TArray<TPair<EShipRating, double>>& Ratings,
                                   const TCHAR* Name = TEXT("A part"), const TCHAR* Words = TEXT("Quiet."))
    {
        UShipModuleDataAsset* Part = NewObject<UShipModuleDataAsset>();
        Part->ModuleId = Id;
        Part->Bay = Bay;
        Part->PowerDraw = Draw;
        Part->DisplayName = FText::FromString(Name);
        Part->Words = FText::FromString(Words);
        for (const TPair<EShipRating, double>& Rated : Ratings)
        {
            Part->Ratings.Add(Rated.Key, Rated.Value);
        }
        return Part;
    }

    /** Everything taken off the top before any split: the total draw less
     *  every consumer's share. */
    float Draws(const UShipSubsystem& Ship)
    {
        return Ship.GetPowerDraw() - (Ship.GetConsumerShare(ShipPower::Lights) + Ship.GetConsumerShare(ShipPower::Boosters)
                                      + Ship.GetConsumerShare(ShipPower::Engine));
    }

    FString SpareIds(const UShipSubsystem& Ship)
    {
        TArray<FString> Ids;
        for (const FShipPartState& Spare : Ship.GetSpares())
        {
            Ids.Add(Spare.PartId.ToString());
        }
        return FString::Join(Ids, TEXT(","));
    }
}

/*
 * Decision 3: a test world has no game mode and so fits nothing, and an
 * empty core bay reads the stock part and draws nothing -- so a bare world's
 * ship is exactly the bare world's ship it always was.
 */
bool FShipPartsEmptyBayIsStockTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("EmptyBayIsStockWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Ship->Tick(0.01f);
    for (const EShipBay Bay : ShipBay::All())
    {
        TestNull(FString::Printf(TEXT("a bare world fits nothing in %s"), *ShipBay::Name(Bay).ToString()), Ship->GetFittedPart(Bay));
    }
    const FShipRatings Stock = FShipRatings::Stock();
    for (const EShipRating Rating : ShipParts::AllRatings())
    {
        TestEqual(FString::Printf(TEXT("an empty bay reads the stock %s"), *ShipParts::RatingName(Rating).ToString()),
                  Ship->GetRatings().Get(Rating), Stock.Get(Rating));
    }
    TestEqual(TEXT("the reactor is today's 1400 W"), Ship->GetReactorOutput(), 1400.0f);
    TestEqual(TEXT("the lights want today's 300 W"), Ship->GetConsumerWant(ShipPower::Lights), 300.0f);
    TestEqual(TEXT("the boosters want today's 450 W"), Ship->GetConsumerWant(ShipPower::Boosters), 450.0f);
    TestEqual(TEXT("and nothing draws off the top"), Draws(*Ship), 0.0f, 1e-2f);
    TestEqual(TEXT("so the ship draws the two wants, 750 W"), Ship->GetPowerDraw(), 750.0f, 1e-2f);
    TestEqual(TEXT("the chart reaches 12 ly"), Ship->GetChartRangeLy(), 12.0f);
    TestEqual(TEXT("the jump winds on 380 W"), Ship->GetWindingWant(), 380.0f);
    TestEqual(TEXT("in 45 s"), Ship->GetChargeSeconds(), 45.0f);
    TestEqual(TEXT("and the drive follows at 3 notches a second"), Ship->GetDriveResponse(), 3.0f);
    TestFalse(TEXT("a core bay's part cannot be removed, only swapped"), Ship->RemovePart(EShipBay::Reactor));
    TestEqual(TEXT("and there are no spares"), Ship->GetSpares().Num(), 0);
    return true;
}

/*
 * Decision 1: one part per bay. A fit swaps, and the displaced part is a
 * spare; swapping back restores every rating; two parts never draw in one
 * bay; a part whose bay was never set is refused. And the classes the
 * spec's tests leave out: the part a bay already holds (review focus 2), the
 * auxiliary slots (focus 3), and parts with no id or another part's
 * (focus 4).
 */
bool FShipPartsOnePerBayTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("OnePerBayWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    UShipModuleDataAsset* A = MakePart(TEXT("Reactor.A"), EShipBay::Reactor, 0.0f, { { EShipRating::ReactorWatts, 1500.0 } });
    UShipModuleDataAsset* B = MakePart(TEXT("Reactor.B"), EShipBay::Reactor, 50.0f, { { EShipRating::ReactorWatts, 1600.0 } });

    TestTrue(TEXT("a reactor part fits the reactor bay"), Ship->FitPart(A));
    TestEqual(TEXT("and the ship runs on it at once"), Ship->GetReactorOutput(), 1500.0f);
    const FShipRatings WithA = Ship->GetRatings();
    TestEqual(TEXT("a fit into an empty bay displaces nothing"), Ship->GetSpares().Num(), 0);

    TestTrue(TEXT("a second reactor part fits the same bay"), Ship->FitPart(B));
    TestTrue(TEXT("by swapping: B is fitted"), Ship->GetFittedPart(EShipBay::Reactor) == B);
    TestEqual(TEXT("and A is a spare"), SpareIds(*Ship), FString(TEXT("Reactor.A")));
    TestEqual(TEXT("one part draws in one bay: B's 50 W, never A's too"), Draws(*Ship), 50.0f, 1e-2f);

    TestTrue(TEXT("swapping back"), Ship->FitPart(A));
    for (const EShipRating Rating : ShipParts::AllRatings())
    {
        TestEqual(FString::Printf(TEXT("restores %s exactly"), *ShipParts::RatingName(Rating).ToString()),
                  Ship->GetRatings().Get(Rating), WithA.Get(Rating));
    }
    TestEqual(TEXT("and A draws nothing, so nothing does"), Draws(*Ship), 0.0f, 1e-2f);

    // Review focus 2: the part the bay already holds.
    const FString SparesBefore = SpareIds(*Ship);
    TestTrue(TEXT("fitting the part the bay already holds is taken"), Ship->FitPart(A));
    TestEqual(TEXT("and changes nothing: no spare is made of it"), SpareIds(*Ship), SparesBefore);
    TestEqual(TEXT("and nothing is booked twice"), Draws(*Ship), 0.0f, 1e-2f);

    // A part that fits no bay; review focus 4: no id, or another part's id.
    TestFalse(TEXT("a part whose bay was never set is refused"),
              Ship->FitPart(MakePart(TEXT("Nowhere.Part"), EShipBay::None, 10.0f, {})));
    TestFalse(TEXT("a part with no id is refused: None in a bay means empty"),
              Ship->FitPart(MakePart(TEXT(""), EShipBay::Reactor, 0.0f, { { EShipRating::ReactorWatts, 1900.0 } })));
    TestFalse(TEXT("a different part under a known part's id is refused"),
              Ship->FitPart(MakePart(TEXT("Reactor.B"), EShipBay::Reactor, 0.0f, { { EShipRating::ReactorWatts, 2000.0 } })));
    TestFalse(TEXT("nothing is refused"), Ship->FitPart(nullptr));
    TestTrue(TEXT("and none of them displaced the reactor"),
             Ship->GetFittedPart(EShipBay::Reactor) == A && Ship->GetReactorOutput() == 1500.0f);
    TestEqual(TEXT("or touched the spares"), SpareIds(*Ship), SparesBefore);
    TestEqual(TEXT("or drew anything"), Draws(*Ship), 0.0f, 1e-2f);

    // Review focus 3: the auxiliary slots (decision 8's rules 3 and 5).
    UShipModuleDataAsset* X = MakePart(TEXT("Aux.X"), EShipBay::Aux1, 0.0f, {});
    UShipModuleDataAsset* Y = MakePart(TEXT("Aux.Y"), EShipBay::Aux1, 0.0f, {});
    UShipModuleDataAsset* Z = MakePart(TEXT("Aux.Z"), EShipBay::Aux1, 0.0f, {});
    TestTrue(TEXT("an aux part goes to the first free slot"), Ship->FitPart(X) && Ship->GetFittedPart(EShipBay::Aux1) == X);
    TestTrue(TEXT("a second to the other"), Ship->FitPart(Y) && Ship->GetFittedPart(EShipBay::Aux2) == Y);
    TestTrue(TEXT("with both full, a third replaces Aux1"), Ship->FitPart(Z) && Ship->GetFittedPart(EShipBay::Aux1) == Z);
    TestTrue(TEXT("and the part it displaced is a spare"), SpareIds(*Ship).EndsWith(TEXT("Aux.X")));
    const FString AuxSpares = SpareIds(*Ship);
    TestTrue(TEXT("one of a kind: a part already in the other slot is taken"), Ship->FitPart(Y));
    TestTrue(TEXT("and changes nothing"), Ship->GetFittedPart(EShipBay::Aux1) == Z && Ship->GetFittedPart(EShipBay::Aux2) == Y
                                          && SpareIds(*Ship) == AuxSpares);
    TestTrue(TEXT("an aux slot's part can be removed"), Ship->RemovePart(EShipBay::Aux2));
    TestNull(TEXT("leaving the slot empty"), Ship->GetFittedPart(EShipBay::Aux2));
    TestTrue(TEXT("and the part a spare"), SpareIds(*Ship).EndsWith(TEXT("Aux.Y")));
    TestFalse(TEXT("an empty slot has nothing to remove"), Ship->RemovePart(EShipBay::Aux2));
    TestFalse(TEXT("a core bay's part is never removed"), Ship->RemovePart(EShipBay::Reactor));
    TestTrue(TEXT("and the reactor was never touched by any of it"), Ship->GetFittedPart(EShipBay::Reactor) == A);
    return true;
}

/*
 * Review focus 5. A fit pushes the supply and the lights' want at once, but
 * never the boosters' want: ApplyAllocation is its one writer (sign-off 29),
 * so it arrives on the next pass. Switched off, a new lights part asks
 * nothing until the switch. And test loads are not parts (decision 8): a
 * plain draw, replaced by name, in no bay.
 */
bool FShipPartsWantsFollowTheFitTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("WantsFollowTheFitWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Ship->Tick(0.01f);

    TestTrue(TEXT("a lower-want boosters part fits"),
             Ship->FitPart(MakePart(TEXT("Boosters.Frugal"), EShipBay::Boosters, 0.0f, { { EShipRating::BoostersWant, 400.0 } })));
    TestEqual(TEXT("the fit does not write the boosters' want"), Ship->GetConsumerWant(ShipPower::Boosters), 450.0f);
    Ship->Tick(0.01f);
    TestEqual(TEXT("ApplyAllocation writes the part's on its next pass"), Ship->GetConsumerWant(ShipPower::Boosters), 400.0f);

    Ship->SetLightsOn(false);
    TestTrue(TEXT("a lower-want lights part fits with the lights off"),
             Ship->FitPart(MakePart(TEXT("Lights.Frugal"), EShipBay::Lights, 90.0f, { { EShipRating::LightsWant, 250.0 } })));
    TestEqual(TEXT("switched off, it asks nothing"), Ship->GetConsumerWant(ShipPower::Lights), 0.0f);
    Ship->Tick(0.01f);
    TestEqual(TEXT("and still nothing after a pass"), Ship->GetConsumerWant(ShipPower::Lights), 0.0f);
    TestEqual(TEXT("though its fittings draw off the top all the same"), Draws(*Ship), 90.0f, 1e-2f);
    Ship->SetLightsOn(true);
    TestEqual(TEXT("switched on, it asks its own want"), Ship->GetConsumerWant(ShipPower::Lights), 250.0f);

    TestTrue(TEXT("a reactor part fits"),
             Ship->FitPart(MakePart(TEXT("Reactor.Big"), EShipBay::Reactor, 0.0f, { { EShipRating::ReactorWatts, 1700.0 } })));
    TestEqual(TEXT("and the supply follows at once"), Ship->GetReactorOutput(), 1700.0f);

    Ship->AddLoad(TEXT("Test.Hog"), 500.0f);
    TestEqual(TEXT("a test load draws off the top"), Draws(*Ship), 590.0f, 1e-2f);
    Ship->AddLoad(TEXT("Test.Hog"), 300.0f);
    TestEqual(TEXT("the same load again replaces it, never adds"), Draws(*Ship), 390.0f, 1e-2f);
    TestTrue(TEXT("it is in no bay"), Ship->GetFittedPart(EShipBay::Aux1) == nullptr && Ship->GetFittedPart(EShipBay::Aux2) == nullptr);
    TestTrue(TEXT("it can be taken off"), Ship->RemoveLoad(TEXT("Test.Hog")));
    TestEqual(TEXT("leaving the lights' fittings"), Draws(*Ship), 90.0f, 1e-2f);
    TestFalse(TEXT("and it is gone"), Ship->RemoveLoad(TEXT("Test.Hog")));
    return true;
}

#endif
