#include "Misc/AutomationTest.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 5: gravity is felt as watts, and only under a solid
 * world's drive floor, airborne -- the one place the pilot chose to go. The
 * hold is one want on the boosters, paid first inside their share; the
 * manoeuvre keeps what is left. Everywhere a ship can be parked, the stock
 * ship stays whole (the 2026-09-26 reactor ruling).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipSplitBoostersTest, "DeepSpace.Ship.Power.SplitBoosters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipParkedIsWholeTest, "DeepSpace.Ship.Power.ParkedIsWhole",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HoldPowerLocal
{
    constexpr double G = ShipFlight::StandardGravityCmS2;

    /** The stock ship's split, as decision 5's table does it: 1400 W less
     *  620 W of modules, lights 300, boosters 450 + the hold, the engine 380
     *  while the jump winds, every weight 1. */
    ShipPower::FBoosterSplit Stock(double Gs, bool bWinding, float& OutShare)
    {
        FShipPowerState Power;
        Power.SetReactorOutput(1400.0f);
        Power.AddDraw(TEXT("Modules"), 620.0f);
        const float Hold = ShipPower::HoldWant(Gs * G, 1.0e6, ShipPower::DefaultHoldWattsPerG, true);
        Power.SetConsumer(ShipPower::Lights, 300.0f, 1.0f);
        Power.SetConsumer(ShipPower::Boosters, 450.0f + Hold, 1.0f);
        Power.SetConsumer(ShipPower::Engine, bWinding ? 380.0f : 0.0f, 1.0f);
        OutShare = Power.GetShare(ShipPower::Boosters);
        return ShipPower::SplitBoosters(OutShare, Hold, 450.0f);
    }
}

bool FShipSplitBoostersTest::RunTest(const FString& Parameters)
{
    using namespace HoldPowerLocal;
    TestEqual(TEXT("150 W a g"), ShipPower::HoldWant(1.0 * G, 1.0e6, 150.0f, true), 150.0f);
    TestEqual(TEXT("capped at 3 g"), ShipPower::HoldWant(5.0 * G, 1.0e6, 150.0f, true), 450.0f);
    TestEqual(TEXT("ramped in over the first kilometre under the floor"), ShipPower::HoldWant(1.0 * G, 5.0e4, 150.0f, true), 75.0f);
    TestEqual(TEXT("nothing at or above the floor"), ShipPower::HoldWant(3.0 * G, 0.0, 150.0f, true), 0.0f);
    TestEqual(TEXT("nothing landed (slice c's seam)"), ShipPower::HoldWant(3.0 * G, 1.0e6, 150.0f, false), 0.0f);

    struct FRow { const TCHAR* Over; double Gs; bool bWinding; float Share; float Fed; float Feed; };
    const FRow Rows[] = {
        { TEXT("Baemsekai IV, about 0.9 g"), 0.9, false, 480.0f, 1.0f, 345.0f / 450.0f },
        { TEXT("Baemsekai III, 2 g"), 2.0, false, 480.0f, 1.0f, 180.0f / 450.0f },
        { TEXT("a 3 g world"), 3.0, false, 480.0f, 1.0f, 30.0f / 450.0f },
        { TEXT("a 3 g world, the jump winding"), 3.0, true, 260.0f, 260.0f / 450.0f, 0.0f },
    };
    for (const FRow& Row : Rows)
    {
        float Share = 0.0f;
        const ShipPower::FBoosterSplit Split = Stock(Row.Gs, Row.bWinding, Share);
        TestTrue(FString::Printf(TEXT("%s: the boosters get %.0f W (%.1f)"), Row.Over, Row.Share, Share), FMath::IsNearlyEqual(Share, Row.Share, 1.0f));
        TestTrue(FString::Printf(TEXT("%s: the hold is fed %.2f (%.3f)"), Row.Over, Row.Fed, Split.HoldFed), FMath::IsNearlyEqual(Split.HoldFed, Row.Fed, 0.005f));
        TestTrue(FString::Printf(TEXT("%s: the manoeuvre gets %.2f of its want (%.3f)"), Row.Over, Row.Feed, Split.ManoeuvreFeed),
                 FMath::IsNearlyEqual(Split.ManoeuvreFeed, Row.Feed, 0.005f));
    }
    const ShipPower::FBoosterSplit None = ShipPower::SplitBoosters(300.0f, 0.0f, 450.0f);
    TestTrue(TEXT("with no hold, the boosters are exactly as before: feed is share over want"),
             None.HoldFed == 1.0f && None.HoldWatts == 0.0f && FMath::IsNearlyEqual(None.ManoeuvreFeed, 300.0f / 450.0f, 1e-6f));
    return true;
}

bool FShipParkedIsWholeTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("ParkedIsWholeWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const auto Settle = [&]() { for (int32 Frame = 0; Frame < 10; ++Frame) { Test.Step(1.0f / 60.0f); } };
    const auto Whole = [&](const TCHAR* Where)
    {
        TestEqual(FString::Printf(TEXT("%s: no hold is wanted"), Where), Ship->GetHoldWant(), 0.0f);
        TestEqual(FString::Printf(TEXT("%s: the lights are whole"), Where), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
        TestEqual(FString::Printf(TEXT("%s: the boosters are whole"), Where), Ship->GetConsumerSatisfaction(ShipPower::Boosters), 1.0f);
    };
    Settle();
    Whole(TEXT("at the opening placement"));

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const auto ParkOver = [&](int32 Body, double OverFloorCm)
    {
        const FSkyBody& World = Here.Bodies[Body];
        const FVector Out = (Ship->GetFlightState().GetUniversePosition() - World.Position).GetSafeNormal();
        Ship->PlaceShip(World.Position + Out * (World.Radius + UShipSubsystem::FloorFor(World) + OverFloorCm), FQuat::Identity);
        Settle();
    };
    ParkOver(3, 1.0e3);
    Whole(TEXT("at Baemsekai III's drive floor"));
    ParkOver(1, 1.0e3);
    Whole(TEXT("at Baemsekai I's drive floor, the star pulling 3.9 g"));
    // An ocean's floor, where the ship can never land to get relief. Home
    // (Baemsekai) has none -- its five worlds are barren or terrestrial -- so
    // the ship goes to Sova, 5.31 ly out, whose fifth world is an ocean
    // (Saved/procgen_describe.txt). A missing fixture fails here, loudly.
    {
        const FUniversePosition HomeAt = Ship->GetFlightState().GetUniversePosition();
        const TArray<FStarSystemStub> Near = Test.Universe->GetSystemsNear(HomeAt, 12.0 * UniverseUnits::CmPerLightYear);
        const FStarSystemStub* Sova = Near.FindByPredicate([](const FStarSystemStub& Stub) { return Stub.Name == TEXT("Sova"); });
        if (TestNotNull(TEXT("Sova, the nearest system with an ocean, is on the chart"), Sova))
        {
            Ship->PlaceShip(Sova->Position + FVector(UniverseUnits::CmPerAU, 0.0, 0.0), FQuat::Identity);
            Settle();
            const FSkySystem There = LocalSystem::Here(Test.World);
            const FSkyBody* Ocean = There.Bodies.FindByPredicate([](const FSkyBody& Body)
            {
                return Body.Kind != ESkyBodyKind::Star && Body.Ground != EGround::Solid;
            });
            if (TestNotNull(TEXT("Sova has a world with no ground (Sova V, an ocean)"), Ocean))
            {
                const FVector Up = (Ship->GetFlightState().GetUniversePosition() - Ocean->Position).GetSafeNormal();
                Ship->PlaceShip(Ocean->Position + Up * (Ocean->Radius + UShipSubsystem::FloorFor(*Ocean) + 1.0e3), FQuat::Identity);
                Settle();
                Whole(*FString::Printf(TEXT("at %s's floor, which it can never land under"), *Ocean->Id.ToString()));

                // Under that floor, where only a solid world's would tax: 3 km
                // under the ocean's sphere the depth that scopes the hold is
                // still 0, and the tick that reads it there wants nothing.
                Ship->PlaceShip(Ocean->Position + Up * (Ocean->Radius + UShipSubsystem::FloorFor(*Ocean) - 3.0e5), FQuat::Identity);
                const FShipFlightState& Flight = Ship->GetFlightState();
                const double Clear = Flight.GetUniversePosition().DistanceTo(Ocean->Position) - Ocean->Radius - UShipSubsystem::FloorFor(*Ocean);
                TestTrue(FString::Printf(TEXT("placed 3 km under %s's floor (%.0f m)"), *Ocean->Id.ToString(), Clear / 100.0),
                         FMath::IsNearlyEqual(Clear, -3.0e5, 1.0e3));
                TestEqual(FString::Printf(TEXT("under %s's floor the hold's depth is 0: an ocean has no ground to be under"), *Ocean->Id.ToString()),
                          Flight.GetDepthUnderDriveFloor(), 0.0);
                Test.Step(1.0f / 60.0f);
                TestEqual(FString::Printf(TEXT("and under %s's floor no hold is wanted"), *Ocean->Id.ToString()), Ship->GetHoldWant(), 0.0f);
            }
        }
        // Home again, for the hold's own checks.
        Ship->PlaceShip(HomeAt, FQuat::Identity);
        Settle();
    }

    ParkOver(3, -3.0e5);
    const double Gs = Ship->GetFlightState().GetLocalGravity().Size() / ShipFlight::StandardGravityCmS2;
    TestTrue(FString::Printf(TEXT("3 km under Baemsekai III's floor the hold wants 150 W a g (%.1f W at %.2f g)"), Ship->GetHoldWant(), Gs),
             FMath::IsNearlyEqual(Ship->GetHoldWant(), static_cast<float>(150.0 * FMath::Min(Gs, 3.0)), 1.0f));
    TestTrue(TEXT("the boosters' want shows it: 450 W and the hold"),
             FMath::IsNearlyEqual(Ship->GetConsumerWant(ShipPower::Boosters), 450.0f + Ship->GetHoldWant(), 0.01f));
    TestEqual(TEXT("and the lights stay whole while hovering"), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
    TestTrue(TEXT("fed, the hold sinks nothing"), Ship->GetFlightState().GetLimits().SinkBias < 1.0e-3);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    Settle();
    TestTrue(FString::Printf(TEXT("starved of it, the sink is 2 m/s (%.1f cm/s)"), Ship->GetFlightState().GetLimits().SinkBias),
             FMath::IsNearlyEqual(Ship->GetFlightState().GetLimits().SinkBias, 200.0, 1.0));
    return true;
}

#endif
