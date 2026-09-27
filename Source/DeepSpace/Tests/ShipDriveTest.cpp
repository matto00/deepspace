#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDriveTest,
    "DeepSpace.Ship.Drive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    struct FScopedCVar
    {
        IConsoleVariable* Variable = nullptr;
        FString Previous;

        FScopedCVar(const TCHAR* Name, float Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name))
        {
            if (Variable)
            {
                Previous = Variable->GetString();
                Variable->Set(*FString::SanitizeFloat(Value), ECVF_SetByCode);
            }
        }

        ~FScopedCVar()
        {
            if (Variable)
            {
                Variable->Set(*Previous, ECVF_SetByCode);
            }
        }
    };
}

/**
 * The drive and its levers as the subsystem wires them (flight-feel
 * decisions 1-6): the pilot's hands and nobody else's; each lever kept across
 * F and standing up; X stops both; the ease's response, thrust, hold and top
 * handed over from the CVars at use; a tap under the cap slows the ship at
 * once; starved boosters take four times as long, and no longer; and the
 * surfaces the flight law reads are every body here and the edge, each at
 * FloorFor; Shift after X counts from the ship's speed; and an empty helm
 * holds no key. FShipFlightState's own tests cover the law; this covers what
 * the subsystem feeds it.
 */
bool FShipDriveTest::RunTest(const FString& Parameters)
{
    constexpr double Km = UniverseUnits::CmPerKm;
    constexpr double Light = ShipDriveLever::LightCmPerSecond;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("DriveTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>();
    if (TestNotNull(TEXT("the world has a ship"), Ship) && TestNotNull(TEXT("and a universe"), Universe))
    {
        APawn* Pilot = World->SpawnActor<APawn>();
        APawn* Passenger = World->SpawnActor<APawn>();
        const FShipFlightState& Flight = Ship->GetFlightState();
        const int32 Top = Flight.GetDriveNotchCount() - 1;

        // -- The pilot's hands and nobody else's ------------------------------
        const auto Refused = [&](APawn* Who)
        {
            return !Ship->SetDriveEngaged(Who, true) && !Ship->SetHelmInput(Who, FHelmInput())
                && !Ship->AllStop(Who) && !Ship->SetDriveLever(Who, 3) && !Ship->SetFlightCommand(Who, 0.5f, FVector::ZeroVector);
        };
        TestTrue(TEXT("an unpiloted ship refuses every helm write"), Refused(Pilot));
        Ship->SetPilot(Pilot);
        TestTrue(TEXT("a passenger's too"), Refused(Passenger));
        TestTrue(TEXT("and nothing moved"), !Ship->IsDriveEngaged() && Flight.GetCommand().DriveNotch == 0
                 && Flight.GetCommand().Throttle == 0.0);

        // -- Each lever keeps its place, across F and standing up -------------
        TestTrue(TEXT("the pilot sets the drive lever"), Ship->SetDriveLever(Pilot, 12));
        TestTrue(TEXT("and cruise's"), Ship->SetFlightCommand(Pilot, 0.5f, FVector::ZeroVector));
        TestTrue(TEXT("and engages the drive"), Ship->SetDriveEngaged(Pilot, true));
        TestTrue(TEXT("engaged"), Ship->IsDriveEngaged());
        Ship->SetFlightCommand(Pilot, 0.5f, FVector(0.0, 1.0, 0.0));
        Ship->SetHelmInput(Pilot, FHelmInput());
        TestTrue(TEXT("attitude input leaves the drive engaged, its lever where it was"),
                 Ship->IsDriveEngaged() && Flight.GetCommand().DriveNotch == 12);
        Ship->SetDriveEngaged(Pilot, false);
        TestTrue(TEXT("F to cruise keeps the drive lever"), Flight.GetCommand().DriveNotch == 12 && Flight.GetCommand().Throttle == 0.5);
        Ship->SetDriveEngaged(Pilot, true);
        TestTrue(TEXT("and F back keeps cruise's"), Flight.GetCommand().DriveNotch == 12 && Flight.GetCommand().Throttle == 0.5);
        Ship->ClearPilot();
        TestTrue(TEXT("standing up keeps both, and the drive engaged"),
                 Ship->IsDriveEngaged() && Flight.GetCommand().DriveNotch == 12 && Flight.GetCommand().Throttle == 0.5);
        Ship->SetPilot(Pilot);

        // -- X stops both --------------------------------------------------------
        TestTrue(TEXT("the pilot's X is taken"), Ship->AllStop(Pilot));
        TestTrue(TEXT("both levers are at STOP"), Flight.GetCommand().DriveNotch == 0 && Flight.GetCommand().Throttle == 0.0);
        TestTrue(TEXT("and the mode is untouched"), Ship->IsDriveEngaged());

        // -- The ease's inputs, from the allocation and the CVars ---------------
        Ship->Tick(0.01f);
        TestEqual(TEXT("fed boosters give the drive full thrust"), Flight.GetLimits().DriveThrust, 1.0);
        TestEqual(TEXT("ds.Drive.Response's default is 3 notches a second"), Flight.GetLimits().DriveResponse, 3.0);
        TestEqual(TEXT("ds.Drive.HoldSeconds' default is 4 s"), Flight.GetLimits().HoldSeconds, 4.0);
        TestTrue(TEXT("ds.Drive.Top's default is 1 c"), FMath::IsNearlyEqual(Flight.GetLimits().DriveTop, Light, 1e-6 * Light));
        Ship->SetConsumerWeight(ShipPower::Boosters, 0.0f);
        Ship->Tick(0.01f);
        TestTrue(TEXT("starved boosters hand the ease a quarter thrust"), FMath::IsNearlyEqual(Flight.GetLimits().DriveThrust, 0.25, 1e-6));
        TestEqual(TEXT("and the response unscaled"), Flight.GetLimits().DriveResponse, 3.0);
        TestTrue(TEXT("and the top untouched"), FMath::IsNearlyEqual(Flight.GetLimits().DriveTop, Light, 1e-6 * Light));
        Ship->SetConsumerWeight(ShipPower::Boosters, 1.0f);
        {
            FScopedCVar Response(TEXT("ds.Drive.Response"), 1.5f);
            FScopedCVar Hold(TEXT("ds.Drive.HoldSeconds"), 6.0f);
            FScopedCVar Half(TEXT("ds.Drive.Top"), 0.5f);
            Ship->Tick(0.01f);
            TestEqual(TEXT("ds.Drive.Response is read at use"), Flight.GetLimits().DriveResponse, 1.5);
            TestEqual(TEXT("ds.Drive.HoldSeconds is read at use"), Flight.GetLimits().HoldSeconds, 6.0);
            TestEqual(TEXT("ds.Drive.Top 0.5 drops the top notch"), Flight.GetDriveNotchCount(), Top);
        }
        {
            FScopedCVar Crawl(TEXT("ds.Drive.Top"), 1.0e-9f);
            Ship->Tick(0.01f);
            TestEqual(TEXT("ds.Drive.Top below 1 km/s reads as 1 km/s"), Flight.GetLimits().DriveTop, ShipDriveLever::NotchSpeed(1));
            TestEqual(TEXT("and the lever keeps STOP and its first notch"), Flight.GetDriveNotchCount(), 2);
        }
        {
            FScopedCVar Faster(TEXT("ds.Drive.Top"), 10.0f);
            Ship->Tick(0.01f);
            TestTrue(TEXT("ds.Drive.Top above 1 c reads as 1 c"), FMath::IsNearlyEqual(Flight.GetLimits().DriveTop, Light, 1e-6 * Light));
            TestEqual(TEXT("and adds no notch"), Flight.GetDriveNotchCount(), Top + 1);
        }
        Ship->Tick(0.01f);
        TestEqual(TEXT("and let go of"), Flight.GetLimits().DriveResponse, 3.0);

        // -- FloorFor ----------------------------------------------------------
        {
            FSkyBody Earth;
            Earth.Kind = ESkyBodyKind::Planet;
            Earth.Radius = UniverseUnits::CmPerEarthRadius;
            FSkyBody Giant = Earth;
            Giant.Radius = 7.1492e9;
            FSkyBody Sun;
            Sun.Kind = ESkyBodyKind::Star;
            Sun.Radius = UniverseUnits::CmPerSolarRadius;
            const FSkyViewParams View;
            TestTrue(FString::Printf(TEXT("an Earth's floor is the sky's, 10.2 km (%.2f km)"), UShipSubsystem::FloorFor(Earth) / Km),
                     UShipSubsystem::FloorFor(Earth) == SkyProjection::RenderedFloor(Earth.Radius, View)
                     && FMath::IsNearlyEqual(UShipSubsystem::FloorFor(Earth) / Km, 10.2, 0.05));
            TestTrue(FString::Printf(TEXT("a giant's is the sky's too, 112 km (%.1f km)"), UShipSubsystem::FloorFor(Giant) / Km),
                     FMath::IsNearlyEqual(UShipSubsystem::FloorFor(Giant) / Km, 114.4, 0.5));
            TestEqual(TEXT("a star's is one stellar radius"), UShipSubsystem::FloorFor(Sun), Sun.Radius);
            TestEqual(TEXT("the edge's is ds.Flight.Floor, 10 km"), UShipSubsystem::EdgeFloor(), 10.0 * Km);
            FSkyBody Pebble = Earth;
            Pebble.Radius = 1.0e7;
            TestEqual(TEXT("a small world's is ds.Flight.Floor, never under it"), UShipSubsystem::FloorFor(Pebble), 10.0 * Km);
            {
                FScopedCVar Floor(TEXT("ds.Flight.Floor"), 20.0f);
                FScopedCVar StarFloor(TEXT("ds.Flight.StarFloorRadii"), 2.0f);
                TestEqual(TEXT("ds.Flight.Floor 20 raises an Earth's floor above the sky's"), UShipSubsystem::FloorFor(Earth), 20.0 * Km);
                TestEqual(TEXT("and leaves a giant's, whose sky floor is higher"),
                          UShipSubsystem::FloorFor(Giant), SkyProjection::RenderedFloor(Giant.Radius, View));
                TestEqual(TEXT("and moves the edge's"), UShipSubsystem::EdgeFloor(), 20.0 * Km);
                TestEqual(TEXT("ds.Flight.StarFloorRadii 2 is two stellar radii"), UShipSubsystem::FloorFor(Sun), 2.0 * Sun.Radius);
            }
        }

        const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem());
        if (TestTrue(TEXT("there is a start system"), Home.IsSet()))
        {
            const FNavPlacement Opening = NavStart::OpeningPlacement(*Home);

            // -- The surfaces, and the room --------------------------------------
            Ship->PlaceShip(Opening.Position, Opening.Orientation);
            Ship->Tick(0.0f);
            const FSkySystem Here = LocalSystem::Here(World);
            TestEqual(TEXT("the flight law reads every body here and the edge"), Flight.GetSurfaces().Num(), Here.Bodies.Num() + 1);
            double Least = TNumericLimits<double>::Max();
            const FUniversePosition Where = Flight.GetUniversePosition();
            for (const FSkyBody& Body : Here.Bodies)
            {
                Least = FMath::Min(Least, Where.DistanceTo(Body.Position) - Body.Radius - UShipSubsystem::FloorFor(Body));
            }
            Least = FMath::Min(Least, Here.EdgeRadius - UShipSubsystem::EdgeFloor() - Where.DistanceTo(Here.Bodies[0].Position));
            TestTrue(FString::Printf(TEXT("the room is the least distance over every surface less its own floor: %.0f km against %.0f"),
                                     Flight.GetRoom() / Km, Least / Km),
                     FMath::IsNearlyEqual(Flight.GetRoom(), Least, 1.0));

            // -- A quarter thrust takes four times as long, through the subsystem --
            // Nose away from everything near, so the cap has no say.
            const auto TapSeconds = [&](float BoosterWeight)
            {
                Ship->SetConsumerWeight(ShipPower::Boosters, BoosterWeight);
                Ship->PlaceShip(Opening.Position, Opening.Orientation * FQuat(FVector::UpVector, UE_DOUBLE_PI));
                Ship->SetDriveLever(Pilot, 5);
                for (int32 Second = 0; Second < 30; ++Second)
                {
                    Ship->Tick(1.0f);
                }
                FHelmInput Tap;
                Tap.UpPresses = 1;
                Ship->SetHelmInput(Pilot, Tap);
                int32 Frames = 0;
                do
                {
                    Ship->Tick(1.0f / 60.0f);
                    ++Frames;
                }
                while (Flight.GetDrivePosition() < 5.95 && Frames < 60 * 60);
                return Frames / 60.0;
            };
            const double Fed = TapSeconds(1.0f);
            TestEqual(TEXT("a tap is one notch"), Flight.GetCommand().DriveNotch, 6);
            const double Starved = TapSeconds(0.0f);
            TestTrue(FString::Printf(TEXT("a one-notch tap at a quarter thrust takes four times as long, not sixteen and not 1.4: %.3f s against %.3f"),
                                     Starved, Fed),
                     FMath::IsNearlyEqual(Starved / Fed, 4.0, 0.1));
            Ship->SetConsumerWeight(ShipPower::Boosters, 1.0f);

            // -- A tap under the cap slows the ship at once -----------------------
            Ship->AllStop(Pilot);
            for (int32 Second = 0; Second < 30; ++Second)
            {
                Ship->Tick(1.0f);
            }
            Ship->PlaceShip(Opening.Position, Opening.Orientation);
            Ship->SetDriveLever(Pilot, Top);
            for (int32 Frame = 0; Frame < 20 * 30; ++Frame)
            {
                Ship->Tick(1.0f / 30.0f);
            }
            TestEqual(TEXT("closing on the opening's world at 1 c, the cap holds it off"),
                      static_cast<int32>(Flight.GetHold()), static_cast<int32>(EFlightHold::HoldingOff));
            const double Held = Flight.GetDrivePosition();
            FHelmInput Slower;
            Slower.DownPresses = 1;
            Ship->SetHelmInput(Pilot, Slower);
            Ship->Tick(1.0f / 30.0f);
            TestTrue(FString::Printf(TEXT("one Ctrl lands the lever a notch below the ship, not below 1 c: notch %d at position %.3f"),
                                     Flight.GetCommand().DriveNotch, Held),
                     Flight.GetCommand().DriveNotch == FMath::CeilToInt32(Held) - 1 && Flight.GetCommand().DriveNotch < Top - 1);
            for (int32 Frame = 0; Frame < 3 * 30; ++Frame)
            {
                Ship->Tick(1.0f / 30.0f);
            }
            TestTrue(TEXT("and the ship is at that notch or below it within three seconds"),
                     Flight.GetSpeed() <= ShipDriveLever::NotchSpeed(Flight.GetCommand().DriveNotch) * (1.0 + 1e-9));

            // -- Shift stops a fall where it is (decision 3) ---------------------
            // At 1 c with the nose away from everything, X, and a moment into
            // the ease down one Shift: the notch above the ship, not the notch
            // above STOP, so the ship stops slowing rather than falling on to
            // 1 km/s. This is the subsystem's wiring of TapUp; the lever's
            // own test covers TapUp.
            Ship->PlaceShip(Opening.Position, Opening.Orientation * FQuat(FVector::UpVector, UE_DOUBLE_PI));
            Ship->SetDriveLever(Pilot, Top);
            for (int32 Frame = 0; Frame < 20 * 30 && Flight.GetDrivePosition() < Top - 1e-3; ++Frame)
            {
                Ship->Tick(1.0f / 30.0f);
            }
            TestTrue(FString::Printf(TEXT("away from everything the ship reaches 1 c (position %.3f)"), Flight.GetDrivePosition()),
                     Flight.GetDrivePosition() > Top - 1e-3);
            Ship->AllStop(Pilot);
            for (int32 Frame = 0; Frame < 30; ++Frame)
            {
                Ship->Tick(1.0f / 30.0f);
            }
            const double Falling = Flight.GetDrivePosition();
            const double FallingSpeed = Flight.GetSpeed();
            FHelmInput Faster;
            Faster.UpPresses = 1;
            Ship->SetHelmInput(Pilot, Faster);
            Ship->Tick(1.0f / 30.0f);
            TestTrue(FString::Printf(TEXT("a second after X the ship is still well up the lever (position %.3f)"), Falling),
                     Falling > 3.0 && Falling < Top - 1.0);
            TestEqual(FString::Printf(TEXT("one Shift a second after X lands the notch above the ship at position %.3f"), Falling),
                      Flight.GetCommand().DriveNotch, FMath::FloorToInt32(Falling) + 1);
            for (int32 Frame = 0; Frame < 5 * 30; ++Frame)
            {
                Ship->Tick(1.0f / 30.0f);
            }
            TestTrue(FString::Printf(TEXT("and the ship stops falling: %.4g km/s against %.4g at the press"),
                                     Flight.GetSpeed() / Km, FallingSpeed / Km),
                     Flight.GetSpeed() >= FallingSpeed);

            // -- An empty helm holds no key -----------------------------------------
            // A pilot who stands up holding Shift leaves the pawn's held flag
            // behind, and the pawn stops handing over once it is not flying:
            // ClearPilot drops it, or the empty helm would sweep the cruise
            // lever to full by itself.
            Ship->AllStop(Pilot);
            Ship->SetDriveEngaged(Pilot, false);
            Ship->SetFlightCommand(Pilot, 0.5f, FVector::ZeroVector);
            FHelmInput Holding;
            Holding.bUpHeld = true;
            Ship->SetHelmInput(Pilot, Holding);
            Ship->Tick(0.1f);
            const double Left = Flight.GetCommand().Throttle;
            TestTrue(FString::Printf(TEXT("held, cruise's lever sweeps up from 0.5 (%.3f)"), Left), Left > 0.5);
            Ship->ClearPilot();
            for (int32 Frame = 0; Frame < 2 * 30; ++Frame)
            {
                Ship->Tick(1.0f / 30.0f);
            }
            TestEqual(TEXT("and standing up with the key held leaves the lever where it was"),
                      Flight.GetCommand().Throttle, Left);
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
