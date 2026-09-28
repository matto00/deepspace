#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Ship/ShipVerticalLever.h"
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

    // Under the drive floor, 8 km over the highest peak, cruising level with
    // the slowest sink the lever has: the path is down, but it misses the
    // ground by degrees. The floor sphere, which the ship is inside, would
    // say ETA 0 S; there is no arrival, so there is no time.
    const double LevelCm = UShipSubsystem::FloorFor(Fourth) - 2.0e5;
    const FVector Heading = FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
    Ship->SetVerticalLever(Pilot, 0.0);
    Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + LevelCm), FRotationMatrix::MakeFromXZ(Heading, Out).ToQuat());
    Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
    Ship->SetVerticalLever(Pilot, ShipVerticalLever::LeverOf(-10.0, ShipVerticalLever::DefaultTopCmPerSecond));
    for (int32 Frame = 0; Frame < 300; ++Frame)
    {
        Test.Step(1.0f / 60.0f);
    }
    const FShipFlightState& Flight = Ship->GetFlightState();
    const FVector ToCentre = Fourth.Position - Flight.GetUniversePosition();
    const double Down = Flight.GetVelocity() | ToCentre.GetSafeNormal();
    TestTrue(FString::Printf(TEXT("the level leg: inside the drive floor sphere, cruising, sinking a little (%.2f m/s down at %.1f m/s)"),
                             Down / 100.0, Flight.GetSpeed() / 100.0),
             ToCentre.Size() < Fourth.Radius + UShipSubsystem::FloorFor(Fourth) && Down > 0.0 && Flight.GetSpeed() > 1.0e3
             && Flight.GetMode() == EFlightMode::Cruise);
    const TOptional<FTargetView> Passing = Ship->GetTargetView(*Home);
    TestTrue(FString::Printf(TEXT("a path that misses the ground has no ETA (%s)"),
                             Passing && Passing->EtaSeconds ? *FString::Printf(TEXT("ETA %.1f s"), *Passing->EtaSeconds) : TEXT("none")),
             Passing.IsSet() && !Passing->EtaSeconds.IsSet());
    TestTrue(TEXT("and says how high it passes instead"), Passing.IsSet() && Passing->PassingCm.IsSet() && *Passing->PassingCm > 0.0);
    return true;
}

#endif
