#include "Algo/Reverse.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Core/DeepSpaceGameMode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Ship/ShipConsole.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipParts.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Tests/ShipPartsJson.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "UI/EngineeringConsoleWidget.h"
#include "UI/ShipHUDWidget.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsStockIsTodayTest, "DeepSpace.Ship.Parts.StockIsToday",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsRatingsFollowPartsTest, "DeepSpace.Ship.Parts.RatingsFollowParts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipLoadoutTestLocal
{
    UShipModuleDataAsset* LoadPart(const TCHAR* Asset)
    {
        return LoadObject<UShipModuleDataAsset>(nullptr, *FString::Printf(TEXT("/Game/Ship/Parts/%s.%s"), Asset, Asset));
    }

    /** Seconds from the drive's top to rest after an all stop, flown in a
     *  flight state of its own on Limits: no floor to hold it back, full
     *  thrust. -1 if it never reached the top. */
    double SecondsToRestFromTop(FShipFlightLimits Limits)
    {
        Limits.DriveThrust = 1.0;
        FShipFlightState Flight;
        Flight.SetLimits(Limits);
        FShipFlightCommand Command;
        Command.bDrive = true;
        Command.DriveNotch = Flight.GetDriveNotchCount() - 1;
        Flight.SetCommand(Command);
        constexpr double Step = 1.0 / 60.0;
        for (int32 Frame = 0; Frame < 60 * 120 && Flight.GetSpeed() < 0.999 * Limits.DriveTop; ++Frame)
        {
            Flight.Step(Step);
        }
        if (Flight.GetSpeed() < 0.999 * Limits.DriveTop)
        {
            return -1.0;
        }
        Command.DriveNotch = 0;
        Flight.SetCommand(Command);
        double Seconds = 0.0;
        while (Flight.GetSpeed() > 100.0 && Seconds < 120.0)
        {
            Flight.Step(Step);
            Seconds += Step;
        }
        return Seconds;
    }
}

/*
 * Ruling 1: the ship play flies is today's ship, bit for bit, now as six
 * parts. On StockShip::Install -- the Blueprint's list, which overrides the
 * C++ one -- the supply is 1400 W, the ship asks 1370 W at rest, the chart
 * reaches 12 ly and a full charge takes 45 s at full feed.
 */
bool FShipPartsStockIsTodayTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("StockIsTodayWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Test.BeginPlay();

    // The C++ list and the Blueprint's are the same six, so neither can be
    // stale. Without the Blueprint, StockShip::Modules() reads C++, and the
    // comparison below would hold C++ to itself.
    if (!TestNotNull(TEXT("BP_DeepSpaceGameMode loads, so its list is the one read"), StockShip::BlueprintMode()))
    {
        return false;
    }
    TArray<FString> Cpp;
    for (const TSoftObjectPtr<UShipModuleDataAsset>& Soft : GetDefault<ADeepSpaceGameMode>()->GetStartingModules())
    {
        Cpp.Add(Soft.ToSoftObjectPath().ToString());
    }
    TArray<FString> Play;
    for (const UShipModuleDataAsset* Part : StockShip::Modules())
    {
        Play.Add(FSoftObjectPath(Part).ToString());
    }
    TestEqual(TEXT("play starts with six parts"), Play.Num(), 6);
    TestEqual(TEXT("and the C++ default list is the Blueprint's"), FString::Join(Cpp, TEXT(",")), FString::Join(Play, TEXT(",")));

    TestEqual(TEXT("all six fit"), StockShip::Install(Ship), 6);
    for (const EShipBay Bay : ShipBay::All())
    {
        const UShipModuleDataAsset* Part = Ship->GetFittedPart(Bay);
        if (ShipBay::IsCore(Bay))
        {
            TestTrue(FString::Printf(TEXT("the %s bay holds %s"), *ShipBay::Name(Bay).ToString(), *ShipBay::StockPartId(Bay).ToString()),
                     Part && Part->ModuleId == ShipBay::StockPartId(Bay));
        }
        else
        {
            TestNull(FString::Printf(TEXT("%s is empty"), *ShipBay::Name(Bay).ToString()), Part);
        }
    }
    Ship->Tick(0.01f);
    TestEqual(TEXT("the supply is 1400 W"), Ship->GetReactorOutput(), 1400.0f);
    TestEqual(TEXT("620 W is drawn off the top"), Draws(*Ship), 620.0f, 1e-2f);
    TestEqual(TEXT("and the ship asks 1370 W at rest"), Ship->GetPowerDraw(), 1370.0f, 1e-2f);
    TestEqual(TEXT("whole at rest: the lights"), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
    TestEqual(TEXT("and the boosters"), Ship->GetConsumerSatisfaction(ShipPower::Boosters), 1.0f);
    TestEqual(TEXT("which push their full 2 km/s^2"), Ship->GetLinearAcceleration(), 2.0e5f);
    TestEqual(TEXT("the chart reaches 12 ly"), Ship->GetChartRangeLy(), 12.0f);
    TestEqual(TEXT("the jump winds on 380 W"), Ship->GetWindingWant(), 380.0f);
    TestEqual(TEXT("in 45 s at full feed"), Ship->GetChargeSeconds(), 45.0f);
    TestEqual(TEXT("and the drive follows at 3 notches a second"), Ship->GetDriveResponse(), 3.0f);
    TestEqual(TEXT("the view of the fitted parts lists the six"), Ship->GetInstalledModules().Num(), 6);
    return true;
}

/*
 * The two example upgrades change their bay's numbers and nothing else. The
 * twin core winds the jump fully fed with everything whole (sign-off 5); the
 * quick lever brings the ship to rest from 0.1 c sooner, measured through
 * the flight state (sign-off 24).
 */
bool FShipPartsRatingsFollowPartsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("RatingsFollowPartsWorld"));
    UShipSubsystem* Ship = Test.Ship;
    UShipModuleDataAsset* TwinCore = LoadPart(TEXT("DA_Reactor_TwinCore"));
    UShipModuleDataAsset* ReactorStock = LoadPart(TEXT("DA_Reactor_Stock"));
    UShipModuleDataAsset* QuickLever = LoadPart(TEXT("DA_Drive_QuickLever"));
    UShipModuleDataAsset* DriveStock = LoadPart(TEXT("DA_Drive_Stock"));
    if (!TestNotNull(TEXT("the world has a ship"), Ship) || !TestNotNull(TEXT("the twin core is authored"), TwinCore)
        || !TestNotNull(TEXT("the quick lever is authored"), QuickLever) || !TestNotNull(TEXT("and both stock parts"), ReactorStock)
        || !TestNotNull(TEXT("the stock drive"), DriveStock))
    {
        return false;
    }
    Test.BeginPlay();
    TestEqual(TEXT("the stock ship fits"), StockShip::Install(Ship), 6);

    // -- the twin core ------------------------------------------------------------
    TestTrue(TEXT("the twin core fits"), Ship->FitPart(TwinCore));
    TestEqual(TEXT("and the supply is 1800 W"), Ship->GetReactorOutput(), 1800.0f);
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (!TestTrue(TEXT("a course can be plotted"), Chart.Num() > 0 && Ship->PlotCourse(Chart[0].Id)))
    {
        return false;
    }
    if (const TOptional<FVector> Course = Ship->GetCourseDirection())
    {
        Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(-*Course).ToQuat());
    }
    TestTrue(TEXT("the jump engages"), Ship->SetJumpEngaged(true));
    Ship->Tick(0.01f);
    Ship->Tick(0.01f);
    TestTrue(TEXT("and winds"), Ship->GetJumpState() == EJumpState::Winding);
    TestEqual(TEXT("fully fed on the twin core, at the default split"), Ship->GetConsumerSatisfaction(ShipPower::Engine), 1.0f, 1e-4f);
    TestEqual(TEXT("with the lights whole"), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f, 1e-4f);
    TestEqual(TEXT("and the boosters whole"), Ship->GetConsumerSatisfaction(ShipPower::Boosters), 1.0f, 1e-4f);
    TestTrue(TEXT("the stock reactor fits back"), Ship->FitPart(ReactorStock));
    Ship->Tick(0.01f);
    TestTrue(TEXT("and on it the split bites again: the lights dim while the jump winds"),
             Ship->GetConsumerSatisfaction(ShipPower::Lights) < 1.0f - 1e-3f);
    Ship->SetJumpEngaged(false);
    Ship->Tick(0.01f);

    // -- the quick lever ------------------------------------------------------------
    TestTrue(TEXT("the quick lever fits"), Ship->FitPart(QuickLever));
    Ship->Tick(0.01f);
    TestEqual(TEXT("and the drive follows at 4.5 notches a second"), Ship->GetDriveResponse(), 4.5f);
    TestEqual(TEXT("which the flight is handed"), Ship->GetFlightState().GetLimits().DriveResponse, 4.5);
    TestEqual(TEXT("and the reactor's number is its own, untouched"), Ship->GetReactorOutput(), 1400.0f);
    const double Quick = SecondsToRestFromTop(Ship->GetFlightState().GetLimits());
    TestTrue(TEXT("the stock drive fits back"), Ship->FitPart(DriveStock));
    Ship->Tick(0.01f);
    TestEqual(TEXT("and the response is 3 again"), Ship->GetFlightState().GetLimits().DriveResponse, 3.0);
    const double Stock = SecondsToRestFromTop(Ship->GetFlightState().GetLimits());
    AddInfo(FString::Printf(TEXT("from 0.1 c to rest after X: %.2f s on the quick lever, %.2f s on the stock drive"), Quick, Stock));
    TestTrue(TEXT("both reach the top"), Quick > 0.0 && Stock > 0.0);
    TestTrue(TEXT("and the quick lever brings the ship to rest sooner"), Quick < Stock);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsNameplatesAreFactsTest, "DeepSpace.Ship.Parts.NameplatesAreFacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipLoadoutTestLocal
{
    template <typename TWidget>
    TWidget* MakeScreen(UWorld* World)
    {
        TWidget* Widget = NewObject<TWidget>(World);
        Widget->Initialize();
        Widget->TakeWidget();
        return Widget;
    }

    /** A plate's columns: runs of two or more spaces separate them. */
    TArray<FString> Columns(const FString& Line)
    {
        TArray<FString> Pieces;
        Line.ParseIntoArray(Pieces, TEXT("  "), true);
        TArray<FString> Out;
        for (FString& Piece : Pieces)
        {
            Piece.TrimStartAndEndInline();
            if (!Piece.IsEmpty())
            {
                Out.Add(Piece);
            }
        }
        return Out;
    }

    /** Every number in Text, as a value: digits, commas grouping them, one
     *  decimal point between digits. */
    TArray<double> Numbers(const FString& Text)
    {
        TArray<double> Found;
        FString Current;
        for (int32 Index = 0; Index <= Text.Len(); ++Index)
        {
            const TCHAR Char = Index < Text.Len() ? Text[Index] : TEXT(' ');
            const bool bJoin = !Current.IsEmpty() && (Char == TEXT(',') || Char == TEXT('.'))
                && Index + 1 < Text.Len() && FChar::IsDigit(Text[Index + 1]);
            if (FChar::IsDigit(Char) || bJoin)
            {
                if (Char != TEXT(','))
                {
                    Current.AppendChar(Char);
                }
            }
            else if (!Current.IsEmpty())
            {
                Found.Add(FCString::Atod(*Current));
                Current.Reset();
            }
        }
        return Found;
    }

    /** A word no plate may carry (decision 9), or empty. */
    FString Forbidden(const FString& Line)
    {
        if (Line.Contains(TEXT("%")))
        {
            return TEXT("%");
        }
        static const TCHAR* Words[] = {
            TEXT("mk"), TEXT("tier"), TEXT("ii"), TEXT("iii"), TEXT("iv"), TEXT("upgrade"), TEXT("upgraded"), TEXT("basic"),
            TEXT("stock"), TEXT("improved"), TEXT("better"), TEXT("best"), TEXT("standard"), TEXT("draw"), TEXT("drawn"),
            TEXT("spare"), TEXT("condition"), TEXT("worn"),
        };
        TArray<FString> Tokens;
        FString Token;
        for (int32 Index = 0; Index <= Line.Len(); ++Index)
        {
            const TCHAR Char = Index < Line.Len() ? Line[Index] : TEXT(' ');
            if (FChar::IsAlnum(Char))
            {
                Token.AppendChar(FChar::ToLower(Char));
            }
            else if (!Token.IsEmpty())
            {
                Tokens.Add(Token);
                Token.Reset();
            }
        }
        for (const TCHAR* Word : Words)
        {
            if (Tokens.Contains(Word))
            {
                return Word;
            }
        }
        return FString();
    }

    /** The one number decision 9's table says Part's plate carries, as
     *  printed (whole watts; tenths otherwise), and its unit. */
    double PlateFigure(EShipBay Bay, const UShipModuleDataAsset& Part, FString& Unit)
    {
        FShipRatings Rated = FShipRatings::Stock();
        ShipParts::Apply(Rated, Part.Ratings);
        const auto Tenths = [](double Value) { return FMath::RoundToDouble(10.0 * Value) / 10.0; };
        switch (Bay)
        {
        case EShipBay::Reactor:     Unit = TEXT("W"); return FMath::RoundToDouble(Rated.ReactorWatts);
        case EShipBay::Drive:       Unit = TEXT("notches/s"); return Tenths(Rated.DriveResponse);
        case EShipBay::Boosters:    Unit = TEXT("km/s²"); return Tenths(Rated.LinearAcceleration / 1.0e5);
        case EShipBay::Lights:      Unit = TEXT("W"); return FMath::RoundToDouble(Rated.LightsWant);
        case EShipBay::LifeSupport: Unit = TEXT("W"); return FMath::RoundToDouble(Part.PowerDraw);
        case EShipBay::Sensors:     Unit = TEXT("ly"); return Tenths(Rated.RangeLy);
        default:                    Unit.Reset(); return 0.0;
        }
    }

    void CheckPlate(FAutomationTestBase& Test, const FString& Line, EShipBay Bay, const UShipModuleDataAsset& Part, const TCHAR* When)
    {
        const FString Id = Part.ModuleId.ToString();
        const bool bAux = ShipBay::IsAux(Bay);
        const TArray<FString> Cols = Columns(Line);
        if (!Test.TestEqual(FString::Printf(TEXT("%s, %s: bay, name, %swords ('%s')"), When, *Id, bAux ? TEXT("") : TEXT("one figure, "), *Line),
                            Cols.Num(), bAux ? 3 : 4))
        {
            return;
        }
        Test.TestEqual(FString::Printf(TEXT("%s, %s: the bay"), When, *Id), Cols[0], ShipBay::PlateLabel(Bay));
        Test.TestEqual(FString::Printf(TEXT("%s, %s: its name"), When, *Id), Cols[1], Part.DisplayName.ToString());
        Test.TestEqual(FString::Printf(TEXT("%s, %s: its words"), When, *Id), Cols.Last(), Part.Words.ToString());
        const FString Word = Forbidden(Line);
        Test.TestTrue(FString::Printf(TEXT("%s, %s: no percentage, tier, comparison, total or condition (found '%s')"), When, *Id, *Word),
                      Word.IsEmpty());
        const TArray<double> InLine = Numbers(Line);
        if (bAux)
        {
            Test.TestEqual(FString::Printf(TEXT("%s, %s: an aux plate carries no number"), When, *Id), InLine.Num(), 0);
            return;
        }
        FString Unit;
        const double Want = PlateFigure(Bay, Part, Unit);
        Test.TestEqual(FString::Printf(TEXT("%s, %s: exactly one number on the line"), When, *Id), InLine.Num(), 1);
        Test.TestTrue(FString::Printf(TEXT("%s, %s: in %s ('%s')"), When, *Id, *Unit, *Cols[2]), Cols[2].EndsWith(TEXT(" ") + Unit));
        Test.TestTrue(FString::Printf(TEXT("%s, %s: the part's own %g"), When, *Id, Want),
                      InLine.Num() == 1 && FMath::IsNearlyEqual(InLine[0], Want, 1e-9));
    }
}

/*
 * Decision 9: one nameplate per fitted part, BAY Name figure Words, each
 * figure the part's own. Never a percentage, a tier, a comparison, a
 * condition or a symptom; no line for an empty slot; never the charge; and
 * no total drawn or headroom anywhere on the console (the lived-in spec's
 * decision 11). No console variable moves a plate.
 */
bool FShipPartsNameplatesAreFactsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("NameplatesWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    // Spawned before play begins, as every screen must be.
    const AShipConsole* ConsoleActor = Test.World->SpawnActor<AShipConsole>(FVector(0.0, 0.0, -10000.0), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the console actor spawns"), ConsoleActor))
    {
        return false;
    }
    Test.BeginPlay();
    TestEqual(TEXT("the stock ship fits"), StockShip::Install(Ship), 6);
    Ship->Tick(0.01f);
    UEngineeringConsoleWidget* Console = MakeScreen<UEngineeringConsoleWidget>(Test.World);
    const auto Shown = [&]()
    {
        Console->RefreshFromShip();
        return Console->GetShownText().ToString();
    };
    const auto CheckScreen = [&](const TCHAR* When)
    {
        const FString Text = Shown();
        TArray<FString> Lines;
        Text.ParseIntoArrayLines(Lines);
        TArray<EShipBay> Fitted;
        for (const EShipBay Bay : ShipBay::All())
        {
            if (Ship->GetFittedPart(Bay))
            {
                Fitted.Add(Bay);
            }
        }
        if (!TestEqual(FString::Printf(TEXT("%s: one plate per fitted part, in bay order, and none for an empty slot"), When),
                       Lines.Num(), Fitted.Num()))
        {
            return;
        }
        for (int32 Index = 0; Index < Lines.Num(); ++Index)
        {
            CheckPlate(*this, Lines[Index], Fitted[Index], *Ship->GetFittedPart(Fitted[Index]), When);
        }
        TestFalse(FString::Printf(TEXT("%s: never the charge"), When), Numbers(Text).Contains(static_cast<double>(Ship->GetChargeSeconds())));
    };

    CheckScreen(TEXT("the stock ship"));

    // Every row of the catalogue, as its own plate.
    const ShipPartsJson::FCatalogue Catalogue = ShipPartsJson::Read();
    for (const ShipPartsJson::FRow& Row : Catalogue.Rows)
    {
        const UShipModuleDataAsset* Part = LoadObject<UShipModuleDataAsset>(nullptr, *ShipPartsJson::ObjectPath(Catalogue, Row.Asset));
        if (TestNotNull(FString::Printf(TEXT("%s is authored"), *Row.Spec.Id.ToString()), Part))
        {
            CheckPlate(*this, UEngineeringConsoleWidget::Nameplate(Part->Bay, *Part), Part->Bay, *Part, TEXT("the catalogue"));
        }
    }

    // Each example upgrade, fitted.
    for (const TCHAR* Asset : { TEXT("DA_Reactor_TwinCore"), TEXT("DA_Drive_QuickLever") })
    {
        TestTrue(FString::Printf(TEXT("%s fits"), Asset), Ship->FitPart(LoadPart(Asset)));
        Ship->Tick(0.01f);
        CheckScreen(*FString::Printf(TEXT("with %s"), Asset));
    }

    // An aux part: its plate carries no number, and once it is stowed no line is left.
    TestTrue(TEXT("an aux part fits"), Ship->FitPart(MakePart(TEXT("Aux.Scope"), EShipBay::Aux1, 0.0f, {},
        TEXT("Long-focus telescope"), TEXT("Somebody scratched a chart into its hood."))));
    CheckScreen(TEXT("with an aux part"));
    TestTrue(TEXT("and is stowed"), Ship->RemovePart(EShipBay::Aux1));
    CheckScreen(TEXT("the aux part stowed"));

    // No total drawn and no headroom. A 13 W load makes both numbers no part
    // rates, so if either were printed it would be seen.
    Ship->AddLoad(TEXT("Test.Hog"), 13.0f);
    Ship->Tick(0.01f);
    const double Drawn = FMath::RoundToDouble(Ship->GetPowerDraw());
    const double Headroom = FMath::RoundToDouble(Ship->GetPowerHeadroom());
    TArray<double> Figures;
    for (const EShipBay Bay : ShipBay::All())
    {
        if (const UShipModuleDataAsset* Part = Ship->GetFittedPart(Bay))
        {
            FString Unit;
            Figures.Add(PlateFigure(Bay, *Part, Unit));
        }
    }
    AddInfo(FString::Printf(TEXT("drawn %.0f W, headroom %.0f W"), Drawn, Headroom));
    TestTrue(TEXT("the load makes the draw and the headroom numbers no plate carries"),
             !Figures.Contains(Drawn) && !Figures.Contains(Headroom));
    const TArray<double> OnScreen = Numbers(Shown());
    TestFalse(TEXT("no total drawn on the console"), OnScreen.Contains(Drawn));
    TestFalse(TEXT("and no headroom"), OnScreen.Contains(Headroom));

    // The HUD's power corner goes the same way (ruled on the plan,
    // 2026-09-27, with sign-off 11): the reactor's rating, and no SPARE.
    const FString Corner = UShipHUDWidget::PowerLineText(*Ship).ToString();
    TestFalse(FString::Printf(TEXT("the HUD shows no SPARE ('%s')"), *Corner), Corner.Contains(TEXT("SPARE")));
    TestFalse(TEXT("and no headroom"), Numbers(Corner).Contains(Headroom));
    TestTrue(TEXT("only the reactor's rating"),
             Numbers(Corner).Num() == 1 && Numbers(Corner)[0] == FMath::RoundToDouble(Ship->GetReactorOutput()));
    // And what the HUD actually draws, not just the helper: a HUD built and
    // ticked on this ship, every text block read.
    {
        UShipHUDWidget* HUD = NewObject<UShipHUDWidget>(Test.World);
        HUD->Initialize();
        HUD->TakeWidget();
        HUD->NativeTick(FGeometry(), 0.016f);
        bool bCornerDrawn = false;
        HUD->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (const UTextBlock* Block = Cast<UTextBlock>(Widget))
            {
                const FString Text = Block->GetText().ToString();
                bCornerDrawn |= Text == Corner;
                TestFalse(FString::Printf(TEXT("the ticked HUD's '%s' says no SPARE"), *Text), Text.Contains(TEXT("SPARE")));
            }
        });
        TestTrue(FString::Printf(TEXT("and the ticked HUD draws the corner '%s'"), *Corner), bCornerDrawn);
    }

    // The console actor's own readout, which BP_ShipConsole may still call:
    // the reactor's plate, and neither total.
    {
        const FString Readout = ConsoleActor->GetReadout().ToString();
        TestEqual(TEXT("AShipConsole::GetReadout is the reactor's nameplate"), Readout,
                  UEngineeringConsoleWidget::Nameplate(EShipBay::Reactor, *Ship->GetFittedPart(EShipBay::Reactor)));
        TestFalse(TEXT("with no DRAW or SPARE"), Readout.Contains(TEXT("DRAW")) || Readout.Contains(TEXT("SPARE")));
        TestFalse(TEXT("and neither the total drawn nor the headroom"), Numbers(Readout).Contains(Drawn) || Numbers(Readout).Contains(Headroom));
    }
    TestTrue(TEXT("the load comes off"), Ship->RemoveLoad(TEXT("Test.Hog")));

    // No console variable moves a plate.
    const FString Before = Shown();
    {
        FScopedCVar Range(TEXT("ds.Nav.RangeLy"), 15.0f);
        FScopedCVar Response(TEXT("ds.Drive.Response"), 1.0f);
        FScopedCVar Top(TEXT("ds.Drive.Top"), 0.05f);
        Ship->Tick(0.01f);
        TestEqual(TEXT("ds.Nav.RangeLy 15, ds.Drive.Response 1 and ds.Drive.Top 0.05 change no plate"), Shown(), Before);
    }
    Ship->Tick(0.01f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsCatalogueResolvesTest, "DeepSpace.Ship.Parts.CatalogueResolves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Decision 5: in a bare world, with no game mode and no asset-registry
 * scan, every id in Tools/ship_parts.json resolves through FindPart, and
 * nothing else does. By id, the first spare with that id is fitted before a
 * new one is made (decision 10), and the part a bay already holds is left
 * alone (review focus 2).
 */
bool FShipPartsCatalogueResolvesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("CatalogueResolvesWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    const ShipPartsJson::FCatalogue Catalogue = ShipPartsJson::Read();
    TArray<FString> JsonIds;
    for (const ShipPartsJson::FRow& Row : Catalogue.Rows)
    {
        JsonIds.Add(Row.Spec.Id.ToString());
        const UShipModuleDataAsset* Part = Ship->FindPart(Row.Spec.Id);
        if (TestNotNull(FString::Printf(TEXT("%s resolves"), *Row.Spec.Id.ToString()), Part))
        {
            TestEqual(FString::Printf(TEXT("%s is its own asset"), *Row.Spec.Id.ToString()),
                      FSoftObjectPath(Part).ToString(), ShipPartsJson::ObjectPath(Catalogue, Row.Asset));
        }
    }
    TArray<FString> CatalogueIds;
    for (const UShipModuleDataAsset* Part : Ship->GetCatalogue())
    {
        CatalogueIds.Add(Part->ModuleId.ToString());
    }
    TestEqual(TEXT("the catalogue is the JSON's rows, in order, and nothing else"),
              FString::Join(CatalogueIds, TEXT(",")), FString::Join(JsonIds, TEXT(",")));
    TestNull(TEXT("an id the JSON does not have does not resolve"), Ship->FindPart(TEXT("Reactor.Nonesuch")));
    TestNull(TEXT("and neither does no id"), Ship->FindPart(NAME_None));

    TestTrue(TEXT("the twin core fits by id"), Ship->FitPartById(TEXT("Reactor.TwinCore")));
    TestEqual(TEXT("and the supply is 1800 W"), Ship->GetReactorOutput(), 1800.0f);
    TestEqual(TEXT("an empty bay displaced nothing"), SpareIds(*Ship), FString());
    TestFalse(TEXT("an unknown id fits nothing"), Ship->FitPartById(TEXT("Reactor.Nonesuch")));
    TestTrue(TEXT("and changes nothing"), Ship->GetReactorOutput() == 1800.0f && Ship->GetSpares().IsEmpty());

    TestTrue(TEXT("the twin core by id again is taken"), Ship->FitPartById(TEXT("Reactor.TwinCore")));
    TestEqual(TEXT("and makes no spare of itself"), SpareIds(*Ship), FString());

    TestTrue(TEXT("the stock reactor by id"), Ship->FitPartById(TEXT("Reactor.Stock")));
    TestEqual(TEXT("puts the twin core in the spares"), SpareIds(*Ship), FString(TEXT("Reactor.TwinCore")));
    TestTrue(TEXT("and the twin core by id"), Ship->FitPartById(TEXT("Reactor.TwinCore")));
    TestEqual(TEXT("comes out of the spares, never conjured anew"), SpareIds(*Ship), FString(TEXT("Reactor.Stock")));
    TestEqual(TEXT("and runs the ship"), Ship->GetReactorOutput(), 1800.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsCommandsTest, "DeepSpace.Ship.Parts.Commands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipLoadoutTestLocal
{
    /** What a console command printed. */
    struct FHeard : public FOutputDevice
    {
        FString Text;

        virtual void Serialize(const TCHAR* Line, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            Text += Line;
            Text += TEXT("\n");
        }
    };

    FString Run(UWorld* World, const TCHAR* Command, const TArray<FString>& Args)
    {
        FHeard Heard;
        if (IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(Command))
        {
            Object->AsCommand()->Execute(Args, World, Heard);
        }
        else
        {
            Heard.Text = FString::Printf(TEXT("no command %s"), Command);
        }
        return Heard.Text;
    }
}

/*
 * Ruling 8: until landing and a sourcing spec exist, parts come from the
 * console. ds.Ship.Install fits the first spare with that id, else a new
 * one, and each install restores its own bay's number (slice 1's done-when).
 */
bool FShipPartsCommandsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("ShipCommandsWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Test.BeginPlay();
    TestEqual(TEXT("the stock ship fits"), StockShip::Install(Ship), 6);
    UWorld* World = Test.World;

    Run(World, TEXT("ds.Ship.Install"), { TEXT("Reactor.TwinCore") });
    TestEqual(TEXT("ds.Ship.Install Reactor.TwinCore: 1800 W"), Ship->GetReactorOutput(), 1800.0f);
    TestEqual(TEXT("and the stock reactor is a spare"), SpareIds(*Ship), FString(TEXT("Reactor.Stock")));
    const float Response = Ship->GetDriveResponse();

    Run(World, TEXT("ds.Ship.Install"), { TEXT("Reactor.Stock") });
    TestEqual(TEXT("ds.Ship.Install Reactor.Stock: 1400 W again"), Ship->GetReactorOutput(), 1400.0f);
    TestEqual(TEXT("the stock reactor came back from the spares, and the twin core went in"), SpareIds(*Ship), FString(TEXT("Reactor.TwinCore")));
    TestEqual(TEXT("and the drive's number is its own"), Ship->GetDriveResponse(), Response);

    Run(World, TEXT("ds.Ship.Install"), { TEXT("Drive.QuickLever") });
    TestEqual(TEXT("ds.Ship.Install Drive.QuickLever: 4.5 notches a second"), Ship->GetDriveResponse(), 4.5f);
    TestEqual(TEXT("and the reactor's number is its own"), Ship->GetReactorOutput(), 1400.0f);
    Run(World, TEXT("ds.Ship.Install"), { TEXT("Drive.Stock") });
    TestEqual(TEXT("ds.Ship.Install Drive.Stock: 3 again"), Ship->GetDriveResponse(), 3.0f);

    Run(World, TEXT("ds.Ship.Install"), { TEXT("twin-core"), TEXT("REACTOR") });
    TestEqual(TEXT("by display name, case-blind, split on spaces: the twin core"), Ship->GetReactorOutput(), 1800.0f);
    TestEqual(TEXT("out of the spares"), SpareIds(*Ship), FString(TEXT("Drive.QuickLever,Reactor.Stock")));

    // Review focus 2, by id: the part the bay already holds, with no spare of it.
    Run(World, TEXT("ds.Ship.Install"), { TEXT("Reactor.TwinCore") });
    TestEqual(TEXT("installing the fitted part changes no spare"), SpareIds(*Ship), FString(TEXT("Drive.QuickLever,Reactor.Stock")));
    TestEqual(TEXT("and no number"), Ship->GetReactorOutput(), 1800.0f);

    const FString Unknown = Run(World, TEXT("ds.Ship.Install"), { TEXT("Reactor.Nonesuch") });
    TestTrue(TEXT("an unknown part prints the usage"), Unknown.Contains(TEXT("ds.Ship.Install <part>")));
    TestTrue(TEXT("and names the parts there are"), Unknown.Contains(TEXT("Drive.QuickLever")));
    TestTrue(TEXT("and changes nothing"),
             Ship->GetReactorOutput() == 1800.0f && SpareIds(*Ship) == TEXT("Drive.QuickLever,Reactor.Stock"));

    const FString Listed = Run(World, TEXT("ds.Ship.Spares"), {});
    TestTrue(TEXT("ds.Ship.Spares lists each spare"), Listed.Contains(TEXT("Drive.QuickLever")) && Listed.Contains(TEXT("Reactor.Stock")));
    Run(World, TEXT("ds.Ship.Spares"), { TEXT("give"), TEXT("Boosters.Stock") });
    TestEqual(TEXT("ds.Ship.Spares give adds one"), Ship->GetSpares().Num(), 3);
    const FString Refused = Run(World, TEXT("ds.Ship.Spares"), { TEXT("give"), TEXT("Reactor.Nonesuch") });
    TestTrue(TEXT("giving an unknown part adds nothing"), Ship->GetSpares().Num() == 3 && Refused.Contains(TEXT("give <part>")));
    Run(World, TEXT("ds.Ship.Spares"), { TEXT("clear") });
    TestEqual(TEXT("ds.Ship.Spares clear empties them"), Ship->GetSpares().Num(), 0);

    const FString Described = Run(World, TEXT("ds.Ship.Describe"), {});
    TestTrue(TEXT("ds.Ship.Describe names what is in each bay"), Described.Contains(TEXT("Reactor.TwinCore")) && Described.Contains(TEXT("Sensors.Stock")));
    TArray<FString> DescribedLines;
    Described.ParseIntoArrayLines(DescribedLines);
    const FString* Aux1Line = DescribedLines.FindByPredicate([](const FString& Line) { return Line.StartsWith(TEXT("Aux1")); });
    TestTrue(TEXT("and says an empty aux slot is empty, with no stock part to read (decision 2)"),
             Aux1Line && Aux1Line->Contains(TEXT("empty")) && !Aux1Line->Contains(TEXT("stock")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsStateRoundTripsTest, "DeepSpace.Ship.Parts.StateRoundTrips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Decision 11: the loadout is plain and serialisable from slice 1, so the
 * save (slice 3) is a slice and not a rewrite. It goes through Unreal's own
 * struct serialiser and back equal, wear fields included. It restores by bay
 * name, never position, so a state in any order is the same loadout. And
 * what a state cannot name -- an unknown part, a part in the wrong bay, a
 * bay the ship has not got, a bay it lacks -- falls back to stock, counted.
 */
bool FShipPartsStateRoundTripsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    UScriptStruct* Struct = FShipLoadoutState::StaticStruct();

    FShipLoadoutState Written;
    {
        FSkyWorld First(TEXT("RoundTripFirstWorld"));
        TestEqual(TEXT("the stock ship fits"), StockShip::Install(First.Ship), 6);
        TestTrue(TEXT("the twin core fits"), First.Ship->FitPartById(TEXT("Reactor.TwinCore")));
        TestTrue(TEXT("and a spare quick lever is aboard"), First.Ship->AddSpare(TEXT("Drive.QuickLever")));
        Written = First.Ship->GetLoadoutState();
    }
    // The wear fields are slice 4's, but the struct carries them now: give
    // them values, so the round trip proves they travel.
    if (!TestEqual(TEXT("two spares: the stock reactor and the quick lever"), Written.Spares.Num(), 2))
    {
        return false;
    }
    Written.Spares[0].AgeJumps = 12.5;
    Written.Spares[0].LifeJumps = 151.25;
    Written.Spares[0].bHasLife = true;
    Written.Spares[0].Symptom = TEXT("Reactor.Stutter");
    Written.Spares[0].Repairs = 2;
    Written.Spares[0].bOriginal = true;
    ShipParts::FindBay(Written, EShipBay::Reactor)->LivesDrawn = 3;

    // -- through the struct serialiser and back ----------------------------------
    TArray<uint8> Bytes;
    {
        FMemoryWriter Writer(Bytes);
        FObjectAndNameAsStringProxyArchive Archive(Writer, false);
        Struct->SerializeItem(Archive, &Written, nullptr);
    }
    FShipLoadoutState Read;
    {
        FMemoryReader Reader(Bytes);
        FObjectAndNameAsStringProxyArchive Archive(Reader, false);
        Struct->SerializeItem(Archive, &Read, nullptr);
    }
    TestTrue(TEXT("written and read back, every field is equal"), Struct->CompareScriptStruct(&Written, &Read, PPF_None));
    // CompareScriptStruct walks only reflected fields, so a field that lost
    // its UPROPERTY would be skipped by the serialiser and the comparison
    // alike. Each wear field is read back by value.
    if (TestEqual(TEXT("read back, two spares"), Read.Spares.Num(), 2))
    {
        const FShipPartState& Back = Read.Spares[0];
        TestEqual(TEXT("AgeJumps travels"), Back.AgeJumps, 12.5);
        TestEqual(TEXT("LifeJumps travels"), Back.LifeJumps, 151.25);
        TestTrue(TEXT("bHasLife travels"), Back.bHasLife);
        TestEqual(TEXT("Symptom travels"), Back.Symptom, FName(TEXT("Reactor.Stutter")));
        TestEqual(TEXT("Repairs travels"), Back.Repairs, 2);
        TestTrue(TEXT("bOriginal travels"), Back.bOriginal);
        TestEqual(TEXT("PartId travels"), Back.PartId, Written.Spares[0].PartId);
    }
    {
        const FShipBayState* Reactor = ShipParts::FindBay(Read, EShipBay::Reactor);
        TestTrue(TEXT("LivesDrawn travels"), Reactor && Reactor->LivesDrawn == 3);
        TestTrue(TEXT("and the reactor bay's part"), Reactor && Reactor->Part.PartId == FName(TEXT("Reactor.TwinCore")));
    }

    // -- restored by name, in any order -----------------------------------------------
    {
        FShipLoadoutState Shuffled = Read;
        Algo::Reverse(Shuffled.Bays);
        FSkyWorld Second(TEXT("RoundTripSecondWorld"));
        TestEqual(TEXT("a state with its bays in any order restores with nothing falling back"), Second.Ship->RestoreLoadout(Shuffled), 0);
        TestTrue(TEXT("and is the loadout that was written"), Struct->CompareScriptStruct(&Second.Ship->GetLoadoutState(), &Read, PPF_None));
        TestEqual(TEXT("the twin core runs the ship"), Second.Ship->GetReactorOutput(), 1800.0f);
        TestEqual(TEXT("and today's 620 W is drawn"), Draws(*Second.Ship), 620.0f, 1e-2f);

        // Its own state, handed straight back: the argument aliases the
        // loadout it resets, and the spares must survive it.
        TestEqual(TEXT("the ship's own state restores with nothing falling back"),
                  Second.Ship->RestoreLoadout(Second.Ship->GetLoadoutState()), 0);
        TestTrue(TEXT("and is still the loadout that was written, spares and all"),
                 Struct->CompareScriptStruct(&Second.Ship->GetLoadoutState(), &Read, PPF_None));
        TestEqual(TEXT("both spares are still aboard"), Second.Ship->GetSpares().Num(), 2);
    }

    // -- a bay named twice, and an aux part in both slots -----------------------------
    {
        FSkyWorld Fourth(TEXT("RoundTripFourthWorld"));
        // An aux part the ship knows, so its id resolves (no aux part is in
        // the catalogue in slice 1).
        UShipModuleDataAsset* Scope = MakePart(TEXT("Aux.Scope"), EShipBay::Aux1, 0.0f, {});
        TestTrue(TEXT("an aux part fits"), Fourth.Ship->FitPart(Scope));
        Fourth.Ship->ClearSpares();

        FShipLoadoutState Twice = Read;
        FShipBayState SecondReactor;
        SecondReactor.Bay = ShipBay::Name(EShipBay::Reactor);
        SecondReactor.Part.PartId = TEXT("Reactor.Stock");
        SecondReactor.LivesDrawn = 9;
        Twice.Bays.Add(SecondReactor);
        ShipParts::FindBay(Twice, EShipBay::Aux1)->Part.PartId = TEXT("Aux.Scope");
        ShipParts::FindBay(Twice, EShipBay::Aux2)->Part.PartId = TEXT("Aux.Scope");

        TestEqual(TEXT("two entries fall back: the reactor bay's second entry, and the aux part's second slot"),
                  Fourth.Ship->RestoreLoadout(Twice), 2);
        const UShipModuleDataAsset* Reactor = Fourth.Ship->GetFittedPart(EShipBay::Reactor);
        TestTrue(TEXT("the reactor bay's first entry is the one restored"), Reactor && Reactor->ModuleId == FName(TEXT("Reactor.TwinCore")));
        TestEqual(TEXT("with its own lives drawn"), ShipParts::FindBay(Fourth.Ship->GetLoadoutState(), EShipBay::Reactor)->LivesDrawn, 3);
        TestTrue(TEXT("the aux part is in the first aux slot"), Fourth.Ship->GetFittedPart(EShipBay::Aux1) == Scope);
        TestTrue(TEXT("and one of a kind: never in the second too (decision 8)"), Fourth.Ship->GetFittedPart(EShipBay::Aux2) == nullptr);
    }

    // -- what a state cannot name falls back to stock, by name -----------------------
    {
        FShipLoadoutState Odd = Read;
        ShipParts::FindBay(Odd, EShipBay::Reactor)->Part.PartId = TEXT("Reactor.Nonesuch");
        ShipParts::FindBay(Odd, EShipBay::Drive)->Part.PartId = TEXT("Reactor.TwinCore");
        Odd.Bays.RemoveAll([](const FShipBayState& Entry) { return Entry.Bay == ShipBay::Name(EShipBay::Lights); });
        FShipBayState Galley;
        Galley.Bay = TEXT("Galley");
        Odd.Bays.Add(Galley);
        FShipPartState Unknown;
        Unknown.PartId = TEXT("Aux.Nonesuch");
        Odd.Spares.Add(Unknown);

        FSkyWorld Third(TEXT("RoundTripThirdWorld"));
        TestEqual(TEXT("five entries fall back: an unknown part, a part in the wrong bay, a missing bay, an unknown bay, an unknown spare"),
                  Third.Ship->RestoreLoadout(Odd), 5);
        const auto Holds = [&](EShipBay Bay, const TCHAR* Id)
        {
            const UShipModuleDataAsset* Part = Third.Ship->GetFittedPart(Bay);
            return Part && Part->ModuleId == FName(Id);
        };
        TestTrue(TEXT("the unknown reactor becomes the stock reactor"), Holds(EShipBay::Reactor, TEXT("Reactor.Stock")));
        TestTrue(TEXT("the reactor in the drive bay becomes the stock drive"), Holds(EShipBay::Drive, TEXT("Drive.Stock")));
        TestTrue(TEXT("the missing lights bay gets the stock lights"), Holds(EShipBay::Lights, TEXT("Lights.Stock")));
        TestEqual(TEXT("the spares keep what they can name, with their state"), SpareIds(*Third.Ship), FString(TEXT("Reactor.Stock,Drive.QuickLever")));
        TestEqual(TEXT("and the wear fields came with them"), Third.Ship->GetSpares()[0].AgeJumps, 12.5);
        TestEqual(TEXT("the ship draws today's 620 W"), Draws(*Third.Ship), 620.0f, 1e-2f);
    }
    return true;
}

#endif
