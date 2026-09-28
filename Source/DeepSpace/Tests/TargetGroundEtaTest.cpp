#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLanding.h"
#include "Ship/ShipSubsystem.h"
#include "Ship/ShipVerticalLever.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Tests/SkyTestWorld.h"
#include "UI/TargetMarker.h"
#include "Universe/UniverseUnits.h"
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetEtaFromAGiantTest, "DeepSpace.UI.TargetMarker.EtaFromAnotherWorldsRegime",
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

/*
 * The regime is any world's (decision 8), so a ship at an ocean's or a
 * giant's floor is in that world's regime. Cruising from there toward a
 * solid world elsewhere in the system, the target's ETA is still the law
 * above the regime -- cruise's braking curve to where it stops -- not the
 * approach law and skim cap of a ground the ship is nowhere near.
 */
bool FTargetEtaFromAGiantTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("TargetEtaFromAGiantWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);

    // A nearby system with a world that has no ground and one that has: the
    // pair where the solid world looks widest from the other, so the plan
    // view's velocity meets it.
    TOptional<FStarSystem> There;
    int32 Groundless = INDEX_NONE;
    int32 Solid = INDEX_NONE;
    double Widest = 0.0;
    for (const FStarSystemStub& Stub : Test.Universe->GetSystemsNear(Ship->GetFlightState().GetUniversePosition(), 12.0 * UniverseUnits::CmPerLightYear))
    {
        const TOptional<FStarSystem> System = Test.Universe->GetSystem(Stub.Id);
        const FSkySystem Sky = LocalSystem::Here(System);
        for (int32 G = 1; G < Sky.Bodies.Num(); ++G)
        {
            for (int32 S = 1; S < Sky.Bodies.Num(); ++S)
            {
                const FSkyBody& A = Sky.Bodies[G];
                const FSkyBody& B = Sky.Bodies[S];
                if (A.Kind == ESkyBodyKind::Star || B.Kind == ESkyBodyKind::Star || A.Ground != EGround::None || B.Ground != EGround::Solid)
                {
                    continue;
                }
                const double Width = B.Radius / A.Position.DistanceTo(B.Position);
                if (Width > Widest)
                {
                    Widest = Width;
                    There = System;
                    Groundless = G;
                    Solid = S;
                }
            }
        }
    }
    const FSkySystem Sky = LocalSystem::Here(There);
    const FSkyBody& Giant = Sky.Bodies[Groundless];
    const FSkyBody& World = Sky.Bodies[Solid];

    // 1 km over the groundless world's floor, where the horizon points at
    // the solid one: cruise in the regime flies the plan view, so the nose
    // toward it is the velocity toward it.
    const double Lift = Giant.Radius + UShipSubsystem::FloorFor(Giant) + 1.0e5;
    FVector Out = FVector::CrossProduct(World.Position - Giant.Position, FVector(0.0, 0.0, 1.0)).GetSafeNormal();
    for (int32 Pass = 0; Pass < 3; ++Pass)
    {
        const FVector Toward = (World.Position - (Giant.Position + Out * Lift)).GetSafeNormal();
        Out = (Out - Toward * (Out | Toward)).GetSafeNormal();
    }
    const FVector Toward = (World.Position - (Giant.Position + Out * Lift)).GetSafeNormal();
    Ship->PlaceShip(Giant.Position + Out * Lift, FRotationMatrix::MakeFromXZ(Toward, Out).ToQuat());
    Test.Step(1.0f / 60.0f);
    Ship->SetTarget(FBodyId{ There->Stub.Id, Solid - 1, -1 });
    Ship->SetFlightCommand(Pilot, 0.5f, FVector::ZeroVector);
    for (int32 Frame = 0; Frame < 120; ++Frame)
    {
        Test.Step(1.0f / 60.0f);
    }
    const FShipFlightState& Flight = Ship->GetFlightState();
    TestTrue(TEXT("the ship is in the groundless world's regime"), Flight.IsInNearRegime());
    TestFalse(TEXT("with no ground below it"), Flight.GetGroundAltitude().IsSet());
    const TOptional<FTargetView> View = Ship->GetTargetView(*There);
    AddInfo(FString::Printf(TEXT("%s: view %d, passing %.0f km, %.3g rad off the target's centre, which is %.3g rad wide, %.3g AU off"),
        *World.Id.ToString(), View.IsSet(), View && View->PassingCm ? *View->PassingCm / 1.0e5 : -1.0,
        FMath::Acos(FMath::Clamp(Flight.GetVelocity().GetSafeNormal() | (World.Position - Flight.GetUniversePosition()).GetSafeNormal(), -1.0, 1.0)),
        World.Radius / Flight.GetUniversePosition().DistanceTo(World.Position), Flight.GetUniversePosition().DistanceTo(World.Position) / 1.495978707e13));
    if (!TestTrue(FString::Printf(TEXT("the solid target has an ETA, cruising at %.0f m/s"), Flight.GetSpeed() / 100.0),
                  View.IsSet() && View->EtaSeconds.IsSet()))
    {
        return false;
    }

    FFlightSurface Surface;
    Surface.Centre = World.Position;
    Surface.Radius = World.Radius;
    Surface.bWorld = true;
    Surface.Ground = ShipGround::FromRelief(World.Relief);
    const FShipFlightLimits& Limits = Flight.GetLimits();
    const double Speed = Flight.GetSpeed();
    const TOptional<double> Hit = ShipFlight::RayToGround(Surface, Flight.GetUniversePosition(), Flight.GetVelocity() / Speed,
                                                          Limits.GearClearanceCm, 2.0 * Flight.GetUniversePosition().DistanceTo(World.Position));
    if (!TestTrue(TEXT("the velocity meets the target's ground"), Hit.IsSet()))
    {
        return false;
    }
    const double Expected = ShipFlight::SecondsToFloor(FMath::Max(0.0, *Hit - ShipLanding::ReachCm(Limits.GearClearanceCm)), Speed,
                                                       Limits.LinearAcceleration, 0.0);
    TestTrue(FString::Printf(TEXT("and it is cruise's braking curve, the law above the target's regime (%.3f vs %.3f s)"), *View->EtaSeconds, Expected),
             FMath::Abs(*View->EtaSeconds - Expected) <= 1.0e-3 * FMath::Max(1.0, 1.0e-9 * Expected));
    return true;
}

#endif
