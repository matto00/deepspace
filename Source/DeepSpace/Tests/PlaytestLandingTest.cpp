#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipSubsystem.h"
#include "Ship/ShipVerticalLever.h"
#include "Sky/LocalSystem.h"
#include "Surface/GroundField.h"
#include "Tests/SkyTestWorld.h"
#include "Tests/StockShip.h"
#include "UI/ShipHUDWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Landing slice (b) as the next playtest will try it: through the pawn's
 * hands and IMC_Default where keys are involved, on the fixture worlds of
 * home (Baemsekai IV, barren, 0.84 + 0.09 g; Baemsekai III, barren, 1.66 +
 * 0.31 g), read back in the HUD's words. Siblings, never a group.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaytestKeysLiftTheShipTest, "DeepSpace.Playtest.KeysLiftTheShip",
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

#endif
