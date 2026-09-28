#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipGravity.h"
#include "Ship/ShipSubsystem.h"
#include "Ship/ShipVerticalLever.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "UI/ShipHUDWidget.h"
#include "UI/TargetMarker.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing slice (b) as the next playtest will try it: through the pawn's
 * hands and IMC_Default where keys are involved, on the fixture worlds of
 * home (Baemsekai IV, barren, 0.84 + 0.09 g; Baemsekai III, barren, 1.66 +
 * 0.31 g), read back in the HUD's words. Siblings, never a group.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestKeysLiftTheShipTest, "DeepSpace.Playtest.KeysLiftTheShip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestHoverHoldsTest, "DeepSpace.Playtest.HoverHoldsWhenPilotStands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestStarvedSinkTest, "DeepSpace.Playtest.StarvedSinkLandsGently",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestHeavyClimbTest, "DeepSpace.Playtest.HeavyWorldStillClimbs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestParkedTest, "DeepSpace.Playtest.ParkedShipNeverDrifts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestDescendsInTimeTest, "DeepSpace.Playtest.DescendsInTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestEtaToGroundTest, "DeepSpace.Playtest.EtaCountsDownToTheGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace PlaytestLandingLocal
{
    using namespace SkyTestWorld;
    constexpr float Dt = 1.0f / 60.0f;

    /** Home's body by index (0 the star; IV is 4), its name checked. */
    const FSkyBody* HomeBody(FAutomationTestBase& Test, const FSkySystem& Here, int32 Index, const TCHAR* Name)
    {
        const bool bFound = Here.Bodies.IsValidIndex(Index) && Here.Bodies[Index].Id.ToString() == Name;
        return Test.TestTrue(FString::Printf(TEXT("home's body %d is %s"), Index, Name), bFound) ? &Here.Bodies[Index] : nullptr;
    }

    /** The ship AglCm over Body's ground, level, on the side the ship was on. */
    void PlaceOver(UShipSubsystem* Ship, const FSkyBody& Body, double AglCm)
    {
        const FGroundFieldRef Ground = ShipGround::FromRelief(Body.Relief);
        const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Body.Position).GetSafeNormal();
        const FVector Heading = FVector::CrossProduct(Out, FVector(0.3, 0.9, 0.1)).GetSafeNormal();
        Ship->PlaceShip(Body.Position + Out * (Body.Radius + Ground->Height(FVector3d(Out), 0.0) + AglCm),
                        FRotationMatrix::MakeFromXZ(Heading, Out).ToQuat());
    }

    /** One frame as the game runs it: the pawn hands its keys over, the ship
     *  applies them. */
    void Frame(ADeepSpaceCharacter* Player, UShipSubsystem* Ship, float Seconds = Dt)
    {
        Player->Tick(Seconds);
        Ship->Tick(Seconds);
    }

    /** The key IMC_Default binds Action to, or none. */
    TOptional<FKey> KeyOf(const UInputMappingContext* Context, const TCHAR* ActionPath)
    {
        const UInputAction* Action = LoadObject<UInputAction>(nullptr, ActionPath);
        if (!Context || !Action)
        {
            return {};
        }
        for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
        {
            if (Mapping.Action == Action)
            {
                return Mapping.Key;
            }
        }
        return {};
    }
}

bool FPlaytestKeysLiftTheShipTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
    const TOptional<FKey> Up = KeyOf(Context, TEXT("/Game/Input/Actions/IA_VerticalUp.IA_VerticalUp"));
    const TOptional<FKey> Down = KeyOf(Context, TEXT("/Game/Input/Actions/IA_VerticalDown.IA_VerticalDown"));
    TestTrue(TEXT("IMC_Default binds Space to the vertical lever's up"), Up.IsSet() && *Up == EKeys::SpaceBar);
    TestTrue(TEXT("and C to its down"), Down.IsSet() && *Down == EKeys::C);
    const UClass* Blueprint = LoadClass<ADeepSpaceCharacter>(nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
    TestTrue(TEXT("and BP_DeepSpaceCharacter carries both actions"), Blueprint
             && Blueprint->GetDefaultObject<ADeepSpaceCharacter>()->HasVerticalActions());

    FSkyWorld Test(TEXT("PlaytestKeysLiftWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody* Fourth = HomeBody(*this, Here, 4, TEXT("Baemsekai IV"));
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>();
    if (!Fourth || !TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    PlaceOver(Ship, *Fourth, 2.0e5);
    Ship->SetPilot(Player);
    Frame(Player, Ship, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();

    Player->TapVertical(1);
    Player->HoldVertical(1);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    TestTrue(FString::Printf(TEXT("Space pressed and held a second: the ship climbs (%.2f m/s)"), Flight.GetVerticalSpeed() / 100.0),
             Flight.GetVerticalSpeed() > 0.0);
    TestTrue(TEXT("the motion line says CLIMB"), UShipHUDWidget::MotionLineOf(*Ship).Ink.Contains(TEXT("CLIMB")));

    Player->TapVertical(-1);
    Player->HoldVertical(-1);
    for (int32 Tick = 0; Tick < 300; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    TestTrue(TEXT("C held stops at HOVER from a climb"), Flight.GetCommand().Vertical == 0.0);
    Player->TapVertical(-1);
    Player->HoldVertical(-1);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    TestTrue(TEXT("and a fresh C sinks"), Flight.GetVerticalSpeed() < 0.0);
    return true;
}

bool FPlaytestHoverHoldsTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestHoverWorld"));
    // The helm, spawned before play as every seat is.
    APilotSeat* Helm = Test.World->SpawnActor<APilotSeat>(FVector::ZeroVector, FRotator::ZeroRotator);
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkySystem Here = LocalSystem::Here(Test.World);   // held: HomeBody points into it
    const FSkyBody* Fourth = HomeBody(*this, Here, 4, TEXT("Baemsekai IV"));
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>(FVector(-150.0, 0.0, 100.0), FRotator::ZeroRotator);
    if (!Fourth || !TestNotNull(TEXT("the helm spawns"), Helm) || !TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    PlaceOver(Ship, *Fourth, 5.0e4);
    Player->SitIn(Helm);
    if (!TestTrue(TEXT("the pilot is at the helm"), Player->IsSeated()))
    {
        return false;
    }
    Frame(Player, Ship, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();

    // The hover set by hand: C for a second, then Space held back up to the
    // detent -- the lever stops at HOVER from a sink.
    Player->TapVertical(-1);
    Player->HoldVertical(-1);
    for (int32 Tick = 0; Tick < 60; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    TestTrue(TEXT("C sank the ship"), Flight.GetVerticalSpeed() < 0.0);
    Player->TapVertical(1);
    Player->HoldVertical(1);
    for (int32 Tick = 0; Tick < 300; ++Tick) { Frame(Player, Ship); }
    Player->HoldVertical(0);
    TestEqual(TEXT("Space held from a sink stops at HOVER"), Flight.GetCommand().Vertical, 0.0);
    for (int32 Tick = 0; Tick < 10 * 60; ++Tick) { Frame(Player, Ship); }

    // E, as the player stands: the character's own interact, out of the
    // helm's seat, and the ship's pilot released.
    Player->PressInteract();
    TestFalse(TEXT("E stands the pilot up"), Player->IsSeated());
    const FUniversePosition Start = Flight.GetUniversePosition();
    for (int32 Tick = 0; Tick < 60 * 60; ++Tick)
    {
        Frame(Player, Ship);
    }
    TestEqual(TEXT("standing up left the lever at HOVER"), Flight.GetCommand().Vertical, 0.0);
    TestTrue(FString::Printf(TEXT("a minute after the pilot stands, the hover has not moved (%.3f cm)"),
                             Flight.GetUniversePosition().DistanceTo(Start)),
             Flight.GetUniversePosition().DistanceTo(Start) < 1.0);
    const FString Corner = UShipHUDWidget::AltitudeLineText(*Ship).ToString();
    TestTrue(FString::Printf(TEXT("and the corner says so (\"%s\")"), *Corner), Corner.Contains(TEXT("ABOVE GROUND")) && Corner.Contains(TEXT("HOVERING")));
    TestTrue(TEXT("the hold is paid while it hovers"), Ship->GetHoldWatts() > 0.0f);
    return true;
}

bool FPlaytestStarvedSinkTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestStarvedSinkWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkySystem Here = LocalSystem::Here(Test.World);   // held: HomeBody points into it
    const FSkyBody* Third = HomeBody(*this, Here, 3, TEXT("Baemsekai III"));
    if (!Third)
    {
        return false;
    }
    PlaceOver(Ship, *Third, 3.0e4);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();
    bool bSaidSinking = false;
    double RestFor = 0.0;
    double Seconds = 0.0;
    for (; Seconds < 400.0 && RestFor < 2.0; Seconds += Dt)
    {
        Test.Step(Dt);
        bSaidSinking |= UShipHUDWidget::AltitudeLineText(*Ship).ToString().Contains(TEXT("SINKING 2 M/S"));
        RestFor = Flight.GetSpeed() < 0.5 ? RestFor + Dt : 0.0;
    }
    TestTrue(TEXT("starved, the ship sinks, and the corner says SINKING 2 M/S -- a fact, not a warning"), bSaidSinking);
    TestTrue(FString::Printf(TEXT("and the ground catches it, at rest %.0f s in"), Seconds), RestFor >= 2.0);
    TestTrue(TEXT("never more than a centimetre in"), Flight.GetGroundLog().LeastClearance >= -1.0);
    TestTrue(FString::Printf(TEXT("touching at no more than 0.5 m/s (%.3f)"), Flight.GetGroundLog().WorstContactSpeed / 100.0),
             Flight.GetGroundLog().WorstContactSpeed <= Flight.GetLimits().TouchdownSpeed * (1.0 + 1e-6));
    const FString Corner = UShipHUDWidget::AltitudeLineText(*Ship).ToString();
    TestTrue(FString::Printf(TEXT("resting with a foot on the ground, and saying so: \"%s\""), *Corner),
             Corner.Contains(TEXT("ABOVE GROUND")) && Corner.Contains(TEXT("HOVERING")) && FMath::Abs(Flight.GetFootprintClearance().Get(-1.0e9)) < 1.0);
    return true;
}

bool FPlaytestHeavyClimbTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestHeavyClimbWorld"));
    APilotSeat* Helm = Test.World->SpawnActor<APilotSeat>(FVector::ZeroVector, FRotator::ZeroRotator);
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkySystem Here = LocalSystem::Here(Test.World);   // held: HomeBody points into it
    const FSkyBody* Third = HomeBody(*this, Here, 3, TEXT("Baemsekai III"));
    if (!Third)
    {
        return false;
    }
    ADeepSpaceCharacter* Player = Test.World->SpawnActor<ADeepSpaceCharacter>(FVector(-150.0, 0.0, 100.0), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("the helm spawns"), Helm) || !TestNotNull(TEXT("the pilot spawns"), Player))
    {
        return false;
    }
    Player->SitIn(Helm);
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    const FShipFlightState& Flight = Ship->GetFlightState();
    // Space pressed and held for thirty seconds: the lever sweeps to its top
    // in four, and the climb is whatever gravity lets it be.
    const auto Climb = [&]()
    {
        Frame(Player, Ship, 0.0f);
        Player->TapVertical(1);
        Player->HoldVertical(1);
        double Fastest = 0.0;
        for (int32 Tick = 0; Tick < 60 * 30; ++Tick)
        {
            Frame(Player, Ship);
            Fastest = FMath::Max(Fastest, Flight.GetVerticalSpeed());
        }
        Player->HoldVertical(0);
        return Fastest;
    };

    PlaceOver(Ship, *Third, 1.0e5);
    const double Fastest = Climb();
    const double Gs = Flight.GetLocalGravity().Size() / ShipFlight::StandardGravityCmS2;
    const double Top = ShipVerticalLever::ClimbTop(2.0e4, Gs * ShipFlight::StandardGravityCmS2, 0.25);
    // III's own 1.66 g and the star's 0.31 g at right angles to it (the ship
    // opens with the star to starboard): 1.69 g, so 200 m/s / 1.69.
    TestTrue(FString::Printf(TEXT("over Baemsekai III (%.2f g), starved, Space climbs at about 118 m/s (%.1f of %.1f)"), Gs, Fastest / 100.0, Top / 100.0),
             FMath::IsNearlyEqual(Fastest, Top, 0.02 * Top) && Top > 1.1e4 && Top < 1.3e4);

    // A synthetic 3.3 g world, decision 5's heaviest (14.9 M_E): no world
    // near home is that heavy, so Baemsekai III's pull is topped up by a
    // well at its centre until the ship, 1 km over its ground, feels 3.3 g.
    PlaceOver(Ship, *Third, 1.0e5);
    Frame(Player, Ship, 0.0f);
    const double R = Flight.GetUniversePosition().DistanceTo(Third->Position);
    Ship->AddWellForTest(FGravityWell{ Third->Position, (3.3 * ShipFlight::StandardGravityCmS2 - Flight.GetLocalGravity().Size()) * R * R, Third->Radius });
    Player->TapVertical(-1);
    Player->HoldVertical(-1);
    for (int32 Tick = 0; Tick < 5 * 60; ++Tick) { Frame(Player, Ship); }   // back to HOVER at the detent
    Player->HoldVertical(0);
    PlaceOver(Ship, *Third, 1.0e5);
    const double Heavy = Climb();
    const double HeavyGs = Flight.GetLocalGravity().Size() / ShipFlight::StandardGravityCmS2;
    const double HeavyTop = ShipVerticalLever::ClimbTop(2.0e4, HeavyGs * ShipFlight::StandardGravityCmS2, 0.25);
    TestTrue(FString::Printf(TEXT("the synthetic world pulls 3.3 g where the climb is flown (%.3f g)"), HeavyGs),
             FMath::IsNearlyEqual(HeavyGs, 3.3, 0.05));
    TestTrue(FString::Printf(TEXT("at 3.3 g, starved, Space climbs at about 61 m/s (%.1f of %.1f)"), Heavy / 100.0, HeavyTop / 100.0),
             FMath::IsNearlyEqual(Heavy, HeavyTop, 0.02 * HeavyTop) && HeavyTop > 5.9e3 && HeavyTop < 6.2e3);
    return true;
}

bool FPlaytestParkedTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestParkedWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const FSkyBody* Fourth = HomeBody(*this, Here, 4, TEXT("Baemsekai IV"));
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (!Fourth || !TestTrue(TEXT("a star on the chart to wind toward"), Chart.Num() > 0))
    {
        return false;
    }
    Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
    Ship->PlotCourse(Chart[0].Id);
    Ship->SetJumpEngaged(true);
    const auto Park = [&](const FUniversePosition& Where, const TCHAR* Label)
    {
        // Facing away from the course, so the charged jump never finds it in
        // the cone and the fold never opens.
        const FVector Away = -Ship->GetCourseDirection().Get(FVector::ForwardVector);
        Ship->PlaceShip(Where, FRotationMatrix::MakeFromX(Away).ToQuat());
        Test.Step(Dt);
        const FUniversePosition Start = Ship->GetFlightState().GetUniversePosition();
        for (int32 Frame = 0; Frame < 1200; ++Frame)
        {
            Test.Step(0.5f);
        }
        const double Drift = Ship->GetFlightState().GetUniversePosition().DistanceTo(Start);
        TestTrue(FString::Printf(TEXT("%s, starved, the jump winding, ten minutes: it drifts %.4f cm"), Label, Drift), Drift < 1.0);
        TestFalse(TEXT("and it never folded"), Ship->IsInTransit());
    };
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth->Position).GetSafeNormal();
    Park(Fourth->Position + Out * (Fourth->Radius + UShipSubsystem::FloorFor(*Fourth) + 100.0), TEXT("at Baemsekai IV's drive floor"));
    Park(Fourth->Position + Out * 0.25 * UniverseUnits::CmPerAU, TEXT("between worlds"));
    return true;
}

bool FPlaytestDescendsInTimeTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestDescentWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const FSkySystem Here = LocalSystem::Here(Test.World);   // held: HomeBody points into it
    const FSkyBody* Fourth = HomeBody(*this, Here, 4, TEXT("Baemsekai IV"));
    if (!Fourth)
    {
        return false;
    }
    const FGroundFieldRef Ground = ShipGround::FromRelief(Fourth->Relief);
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Fourth->Position).GetSafeNormal();
    const double Local = Ground->Height(FVector3d(Out), 0.0);
    const double DriveFloor = UShipSubsystem::FloorFor(*Fourth);
    PlaceOver(Ship, *Fourth, DriveFloor - Local);
    Test.Step(Dt);
    const FShipFlightState& Flight = Ship->GetFlightState();
    const double H = Flight.GetFootprintClearance().Get(0.0);
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetVerticalLever(Pilot, -1.0);
    double Seconds = 0.0;
    for (; Seconds < 400.0 && Flight.GetFootprintClearance().Get(1.0e9) > 1.0; Seconds += Dt)
    {
        Test.Step(Dt);
    }
    const double Expected = (H - 8.0e4) / 2.0e4 + 4.0 * FMath::Loge(8.0e4 / 200.0) + 4.0;
    AddInfo(FString::Printf(TEXT("from %.1f km over the local ground: %.1f s (expected %.1f)"), H / 1.0e5, Seconds, Expected));
    TestTrue(FString::Printf(TEXT("C from the drive floor to the ground in (H - 800 m) / 200 m/s + 28 s, within 10%% (%.1f vs %.1f)"), Seconds, Expected),
             FMath::Abs(Seconds - Expected) <= 0.1 * Expected);
    TestTrue(TEXT("the vision's a minute or two"), Seconds >= 60.0 && Seconds <= 130.0);
    return true;
}

bool FPlaytestEtaToGroundTest::RunTest(const FString& Parameters)
{
    using namespace PlaytestLandingLocal;
    FSkyWorld Test(TEXT("PlaytestEtaGroundWorld"));
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    StockShip::Install(Ship);
    const TOptional<FStarSystem> Home = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
    const FSkySystem Here = LocalSystem::Here(Test.World);   // held: HomeBody points into it
    const FSkyBody* Fourth = HomeBody(*this, Here, 4, TEXT("Baemsekai IV"));
    if (!Home || !Fourth)
    {
        return false;
    }
    Ship->SetTarget(FBodyId{ Home->Stub.Id, 3, -1 });
    PlaceOver(Ship, *Fourth, 3.0e6);
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetVerticalLever(Pilot, -1.0);
    for (int32 Frame = 0; Frame < 30; ++Frame)
    {
        Test.Step(Dt);
    }
    double Previous = -1.0;
    double Worst = 0.0;
    int32 Samples = 0;
    for (int32 Second = 0; Second < 200; ++Second)
    {
        for (int32 Frame = 0; Frame < 60; ++Frame)
        {
            Test.Step(Dt);
        }
        const TOptional<FTargetView> View = Ship->GetTargetView(*Home);
        if (!View || !View->EtaSeconds || *View->EtaSeconds < 10.0)
        {
            break;
        }
        if (Previous > 0.0)
        {
            Worst = FMath::Max(Worst, FMath::Abs((Previous - *View->EtaSeconds) - 1.0));
            ++Samples;
        }
        Previous = *View->EtaSeconds;
    }
    TestTrue(FString::Printf(TEXT("from above the drive floor to the ground the ETA falls a second a second (%d samples, worst %.3f s off)"), Samples, Worst),
             Samples > 30 && Worst <= 0.1);
    return true;
}

#endif
