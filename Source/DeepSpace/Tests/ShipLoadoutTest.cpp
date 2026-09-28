#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
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

#endif
