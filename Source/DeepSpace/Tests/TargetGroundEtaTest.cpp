#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Tests/SkyTestWorld.h"
#include "UI/TargetMarker.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Sign-off item 13: in cruise over a solid target the ETA names the moment
 * the ship reaches the ground, at every altitude -- never the drive floor,
 * which a cruising ship now passes through with nothing happening. Under
 * the drive it is still the drive floor's.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetGroundEtaTest, "DeepSpace.UI.TargetMarker.GroundEta",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTargetGroundEtaTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("TargetGroundEtaWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    const TOptional<FStarSystem> Home = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
    if (!TestTrue(TEXT("the ship is home"), Home.IsSet()))
    {
        return false;
    }
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody& Fourth = Here.Bodies[4];
    TestTrue(TEXT("home's IV is solid"), Fourth.Ground == EGround::Solid);
    Ship->SetTarget(FBodyId{ Home->Stub.Id, 3, -1 });

    const FGroundFieldRef Ground = ShipGround::FromRelief(Fourth.Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    const FVector3d D(Out);
    const double Local = Ground->Height(D, 0.0);
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + Local + 3.0e6),
                    FRotationMatrix::MakeFromXZ(FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal(), Out).ToQuat());
    Ship->SetVerticalLever(Pilot, -1.0);
    for (int32 Frame = 0; Frame < 120; ++Frame)
    {
        Test.Step(1.0f / 60.0f);
    }
    const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
    if (!TestTrue(TEXT("the target has an ETA, sinking at 200 m/s from 30 km"), View.IsSet() && View->EtaSeconds.IsSet()))
    {
        return false;
    }
    const double Agl = Ship->GetFlightState().GetFootprintClearance().Get(0.0);
    const double Expected = (Agl - 8.0e4) / 2.0e4 + 4.0 * FMath::Loge(8.0e4 / 200.0) + 4.0;
    TestTrue(FString::Printf(TEXT("and it is the time to the ground, (H - 800 m) / 200 m/s + 28 s (%.1f vs %.1f s)"), *View->EtaSeconds, Expected),
             FMath::IsNearlyEqual(*View->EtaSeconds, Expected, 0.05 * Expected));
    TestTrue(TEXT("not the drive floor's, which is sooner"), *View->EtaSeconds > (Agl - (UShipSubsystem::FloorFor(Fourth) - Local)) / 2.0e4 + 5.0);
    return true;
}

#endif
