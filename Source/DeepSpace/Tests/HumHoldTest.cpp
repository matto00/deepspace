#include "Misc/AutomationTest.h"
#include "Ship/ShipHumComponent.h"
#include "Ship/ShipHumVoice.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing decision 5's hum: a hold term in watts delivered, never
 * satisfaction, that reaches cruise's hiss only at the 3 g cap and never
 * exceeds it, and is silent everywhere a ship can be parked.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHumHoldTest, "DeepSpace.Ship.HumHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FHumHoldTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    TestEqual(TEXT("no watts held, no hold hiss"), ShipHum::HoldTerm(0.0f, 150.0f, 0.35f, 0.35f), 0.0f);
    TestTrue(TEXT("a 1 g hover hisses at a third of cruise"), FMath::IsNearlyEqual(ShipHum::HoldTerm(150.0f, 150.0f, 0.35f, 0.35f), 0.35f / 3.0f, 1e-6f));
    TestTrue(TEXT("the 3 g cap reaches cruise's hiss"), FMath::IsNearlyEqual(ShipHum::HoldTerm(450.0f, 150.0f, 0.35f, 0.35f), 0.35f, 1e-6f));
    TestEqual(TEXT("and a louder ds.Hum.HoldHiss never passes cruise's"), ShipHum::HoldTerm(450.0f, 150.0f, 1.0f, 0.35f), 0.35f);
    TestEqual(TEXT("Push takes the largest way the boosters work"), ShipHum::Push(0.0f, 2.0e5f, 0.0f, 1.0f, 0.35f, 0.2f), 0.2f);
    TestEqual(TEXT("and the old reading is unchanged with no hold"), ShipHum::Push(1.0e5f, 2.0e5f, 0.0f, 1.0f, 0.35f, 0.0f),
              ShipHum::Push(1.0e5f, 2.0e5f, 0.0f, 1.0f, 0.35f));

    FSkyWorld Test(TEXT("HumHoldWorld"));
    Test.BeginPlay();
    StockShip::Install(Test.Ship);
    const auto Settle = [&]() { for (int32 Frame = 0; Frame < 10; ++Frame) { Test.Step(1.0f / 60.0f); } };
    Settle();
    TestEqual(TEXT("parked at the opening, the hold is silent"), UShipHumComponent::AskShip(*Test.Ship).Push, 0.0f);

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Third = Here.Bodies[3];
    const FVector Out = (Test.Ship->GetFlightState().GetUniversePosition() - Third.Position).GetSafeNormal();
    Test.Ship->PlaceShip(Third.Position + Out * (Third.Radius + UShipSubsystem::FloorFor(Third) - 3.0e5), FQuat::Identity);
    Settle();
    const float Expected = ShipHum::HoldTerm(Test.Ship->GetHoldWatts(), UShipSubsystem::GetHoldWattsPerG(), 0.35f, 0.35f);
    TestTrue(FString::Printf(TEXT("hovering under Baemsekai III's floor it hisses with the hold (%.3f)"), Expected), Expected > 0.1f);
    TestTrue(TEXT("and the hum reads it"), UShipHumComponent::AskShip(*Test.Ship).Push >= Expected - 1e-6f);
    return true;
}

#endif
