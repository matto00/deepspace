#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFlightInputTest,
    "DeepSpace.Player.FlightInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The helm's hands, through the pawn, to the ship's levers (flight-feel
 * decisions 1-3). The levers are the ship's: the pawn hands over what its
 * keys did each frame and the ship moves whichever lever is live, in its own
 * tick. In cruise a hold sweeps at ds.Cruise.Sweep and the lever stays where
 * it is left, with a detent at zero only a fresh press leaves; under the
 * drive a press is one notch and a hold repeats; X stops both; and nobody
 * but the pilot moves anything.
 */
bool FFlightInputTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("FlightInputTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    ADeepSpaceCharacter* Player = World->SpawnActor<ADeepSpaceCharacter>();

    if (TestNotNull(TEXT("the world has a ship subsystem"), Ship) &&
        TestNotNull(TEXT("the character spawns"), Player))
    {
        const FShipFlightState& Flight = Ship->GetFlightState();
        const auto Throttle = [&]() { return Flight.GetCommand().Throttle; };
        const auto Notch = [&]() { return Flight.GetCommand().DriveNotch; };

        // One frame as the game runs it: the ship ticks before actors, so
        // what the pawn hands over this frame moves the lever next frame.
        const auto Frame = [&](float Seconds)
        {
            Player->Tick(Seconds);
            Ship->Tick(Seconds);
        };

        // The sweep rate is a tuning value; this pins the behaviour round it.
        const double Rate = ShipDriveLever::DefaultCruiseSweep;

        // Nobody is flying: input goes nowhere, and is not saved up.
        Player->TapLever(1);
        Player->HoldLever(1);
        Frame(1.0f);
        TestEqual(TEXT("a player who is not the pilot moves no lever"), Throttle(), 0.0);

        Ship->SetPilot(Player);
        Player->HoldLever(0);
        Frame(0.0f);
        TestEqual(TEXT("and a press made before sitting down is not spent after"), Throttle(), 0.0);

        // -- Cruise ---------------------------------------------------------
        // A fresh press arriving in the same frame as its held flag leaves
        // the detent, and the frame sweeps whatever its substep count.
        Player->TapLever(1);
        Player->HoldLever(1);
        Player->Tick(0.0f);
        Ship->Tick(1.0f);
        TestTrue(FString::Printf(TEXT("held for a second from zero, the lever sweeps at ds.Cruise.Sweep: %.3f"), Throttle()),
                 FMath::IsNearlyEqual(Throttle(), Rate, 1e-9));
        Player->Tick(0.0f);
        Ship->Tick(1.0f / 480.0f);
        TestTrue(TEXT("a frame shorter than one substep still sweeps it"), Throttle() > Rate);

        // Released: the lever stays put. This is the whole point.
        Player->HoldLever(0);
        for (int32 Second = 0; Second < 5; ++Second)
        {
            Frame(1.0f);
        }
        TestTrue(TEXT("releasing the lever leaves it where it was"), Throttle() > Rate && Throttle() < Rate + 0.01);

        // Held down through zero, it stops there; astern is a fresh press.
        Player->HoldLever(-1);
        for (int32 Second = 0; Second < 5; ++Second)
        {
            Frame(1.0f);
        }
        TestEqual(TEXT("held down, the lever stops at zero: the detent"), Throttle(), 0.0);
        Player->TapLever(-1);
        Frame(0.5f);
        Frame(0.5f);
        TestTrue(FString::Printf(TEXT("and a fresh press takes it astern: %.3f"), Throttle()), Throttle() < 0.0);
        for (int32 Second = 0; Second < 5; ++Second)
        {
            Frame(1.0f);
        }
        TestEqual(TEXT("which stops full astern"), Throttle(), -1.0);
        Player->HoldLever(0);

        // Attitude is held, not swept: it passes straight through.
        Player->SetFlightInput(FVector(0.0, 1.0, 0.0));
        Frame(0.1f);
        TestEqual(TEXT("attitude reaches the ship as given"), Flight.GetCommand().AttitudeRate.Y, 1.0);
        Player->SetFlightInput(FVector::ZeroVector);
        Frame(0.0f);

        // -- The drive -------------------------------------------------------
        TestFalse(TEXT("the drive starts off"), Ship->IsDriveEngaged());
        Player->PressDrive();
        TestTrue(TEXT("the pilot's drive key engages the drive"), Ship->IsDriveEngaged());
        TestEqual(TEXT("with its lever at STOP"), Notch(), 0);

        // A press and release inside one frame: the frame hands over one
        // press and no hold, and that is one notch.
        Player->TapLever(1);
        Player->HoldLever(0);
        Frame(1.0f / 30.0f);
        TestEqual(TEXT("a tap released inside one frame is one notch"), Notch(), 1);
        Player->TapLever(1);
        Player->TapLever(1);
        Frame(1.0f / 30.0f);
        TestEqual(TEXT("two taps in one frame are two notches"), Notch(), 3);
        Player->TapLever(-1);
        Frame(1.0f / 30.0f);
        TestTrue(TEXT("and Ctrl is one down -- below the ship, which is still spooling up"), Notch() < 3);

        // A hold repeats after a moment, at ds.Drive.Sweep: a press held one
        // second is 1 + 3 x 0.7 notches, whatever the frame rate.
        for (const float Hz : {30.0f, 144.0f})
        {
            // From rest, so each tap counts from STOP and not from a ship
            // still easing down.
            Player->HoldLever(0);
            Player->PressStop();
            for (int32 Second = 0; Second < 10; ++Second)
            {
                Frame(1.0f);
            }
            const int32 From = Notch();
            Player->TapLever(1);
            Player->HoldLever(1);
            for (int32 Index = 0; Index < FMath::RoundToInt32(Hz); ++Index)
            {
                Frame(1.0f / Hz);
            }
            Player->HoldLever(0);
            Frame(1.0f / Hz);
            TestEqual(FString::Printf(TEXT("at %.0f Hz a one-second hold from STOP is three notches"), Hz), Notch() - From, 3);
        }

        // Flying on does not disengage the drive or move its lever.
        const int32 Set = Notch();
        Player->SetFlightInput(FVector(0.0, 1.0, 0.0));
        Frame(0.5f);
        TestTrue(TEXT("and flying on does not disengage it"), Ship->IsDriveEngaged() && Notch() == Set);
        Player->SetFlightInput(FVector::ZeroVector);

        // F keeps each lever; X stops both.
        Player->PressDrive();
        TestFalse(TEXT("pressed again it is off"), Ship->IsDriveEngaged());
        TestEqual(TEXT("and its lever is kept"), Notch(), Set);
        Ship->SetFlightCommand(Player, 0.6f, FVector::ZeroVector);
        Player->PressStop();
        TestTrue(TEXT("X is all stop: both levers at STOP"), Notch() == 0 && Throttle() == 0.0);

        // A key held through X moves nothing until it is let go.
        Player->PressDrive();
        Player->HoldLever(1);
        Frame(0.2f);
        Player->PressStop();
        for (int32 Second = 0; Second < 3; ++Second)
        {
            Frame(1.0f);
        }
        TestEqual(TEXT("Shift held through X does not climb back up"), Notch(), 0);
        Player->HoldLever(0);
        Frame(0.1f);
        Player->TapLever(1);
        Frame(0.1f);
        TestEqual(TEXT("let go and pressed again, it does"), Notch(), 1);

        // Nobody else's key moves anything.
        Ship->ClearPilot();
        Player->PressDrive();
        Player->PressStop();
        Player->TapLever(1);
        Frame(0.1f);
        TestTrue(TEXT("a player who is not the pilot can neither toggle, nor stop, nor tap"),
                 Ship->IsDriveEngaged() && Notch() == 1);
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
