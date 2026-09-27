#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipHUDAltitudeTest,
    "DeepSpace.UI.HUDAltitude",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The altitude corner: how far the nearest surface is, whose it is, and
 * whether the ship is held as low as it goes. The developer could not tell
 * whether they were near enough to land; this is the line that says, so
 * every unit is checked either side of where it hands over, and the floor's
 * words are the flight state's own hold.
 */
bool FShipHUDAltitudeTest::RunTest(const FString& Parameters)
{
    const double Km = UniverseUnits::CmPerKm;
    const double AU = UniverseUnits::CmPerAU;
    const auto Words = [](double Cm) { return UShipHUDWidget::AltitudeWords(Cm); };

    // -- The units, at each boundary ----------------------------------------
    TestEqual(TEXT("on the surface"), Words(0.0), FString(TEXT("0 M")));
    TestEqual(TEXT("never below it"), Words(-5.0e4), FString(TEXT("0 M")));
    TestEqual(TEXT("metres under a kilometre"), Words(999.4 * 100.0), FString(TEXT("999 M")));
    TestEqual(TEXT("a kilometre, never 1000 M"), Words(999.6 * 100.0), FString(TEXT("1.0 KM")));
    TestEqual(TEXT("tenths of a kilometre close in"), Words(42.26 * Km), FString(TEXT("42.3 KM")));
    TestEqual(TEXT("just under a hundred"), Words(99.94 * Km), FString(TEXT("99.9 KM")));
    TestEqual(TEXT("a hundred, never 100.0 KM"), Words(99.96 * Km), FString(TEXT("100 KM")));
    TestEqual(TEXT("a hundred kilometres"), Words(100.0 * Km), FString(TEXT("100 KM")));
    TestEqual(TEXT("an Earth's floor"), Words(10.2 * Km), FString(TEXT("10.2 KM")));
    TestEqual(TEXT("thousands grouped"), Words(4213.0 * Km), FString(TEXT("4,213 KM")));
    TestEqual(TEXT("just under ten thousand"), Words(9999.4 * Km), FString(TEXT("9,999 KM")));
    TestEqual(TEXT("ten thousand, in thousands"), Words(9999.6 * Km), FString(TEXT("10 THOUSAND KM")));
    TestEqual(TEXT("the Moon's distance"), Words(384400.0 * Km), FString(TEXT("384 THOUSAND KM")));
    TestEqual(TEXT("just under a hundredth of an AU"), Words(0.00949 * AU), FString(TEXT("1,420 THOUSAND KM")));
    TestEqual(TEXT("a hundredth of an AU, in AU"), Words(0.00951 * AU), FString(TEXT("0.010 AU")));
    TestEqual(TEXT("thousandths under one"), Words(0.4567 * AU), FString(TEXT("0.457 AU")));
    TestEqual(TEXT("just under one"), Words(0.9994 * AU), FString(TEXT("0.999 AU")));
    TestEqual(TEXT("one, never 1.000 AU"), Words(0.9996 * AU), FString(TEXT("1.00 AU")));
    TestEqual(TEXT("hundredths under a hundred"), Words(5.2 * AU), FString(TEXT("5.20 AU")));
    TestEqual(TEXT("just under a hundred"), Words(99.994 * AU), FString(TEXT("99.99 AU")));
    TestEqual(TEXT("a hundred, whole"), Words(99.996 * AU), FString(TEXT("100 AU")));
    TestEqual(TEXT("far out, grouped"), Words(7912.3 * AU), FString(TEXT("7,912 AU")));

    // -- The line ------------------------------------------------------------
    // One word for what the cap is doing, from the flight state's own hold,
    // and nothing at all when it is doing nothing.
    const FString HoldingOff = FString(NavText::Separator) + TEXT("HOLDING OFF");
    const FString AtFloor = FString(NavText::Separator) + TEXT("AT THE FLOOR");
    const FString AtEdge = FString(NavText::Separator) + TEXT("AT THE EDGE");
    const auto Line = [&](double Cm, bool bEdge, EFlightHold Hold)
    {
        return UShipHUDWidget::AltitudeLine(Cm, TEXT("Kessa IV"), bEdge, Hold);
    };
    TestEqual(TEXT("above a world, by name"), Line(212.0 * Km, false, EFlightHold::Free), FString(TEXT("212 KM ABOVE Kessa IV")));
    TestEqual(TEXT("the edge is not a world"), Line(3400.0 * AU, true, EFlightHold::Free), FString(TEXT("3,400 AU TO THE EDGE")));
    TestEqual(TEXT("the cap taking speed away says so"),
        Line(2310.0 * Km, false, EFlightHold::HoldingOff), FString(TEXT("2,310 KM ABOVE Kessa IV")) + HoldingOff);
    TestEqual(TEXT("held at the floor, it says so"),
        Line(10.2 * Km, false, EFlightHold::AtFloor), FString(TEXT("10.2 KM ABOVE Kessa IV")) + AtFloor);
    TestEqual(TEXT("the edge holds off like a world"),
        Line(3400.0 * AU, true, EFlightHold::HoldingOff), FString(TEXT("3,400 AU TO THE EDGE")) + HoldingOff);
    TestEqual(TEXT("and at its floor it is the edge, not a floor"),
        Line(10.0 * Km, true, EFlightHold::AtFloor), FString(TEXT("10.0 KM TO THE EDGE")) + AtEdge);
    TestFalse(TEXT("the old words are gone"), Line(10.2 * Km, false, EFlightHold::AtFloor).Contains(TEXT("DRIVE FLOOR")));

    // -- Which hold is shown -------------------------------------------------
    // HOLDING OFF only past 5% under the lever: less is nothing anyone could
    // see in the speed, and a word that came and went with it would nag.
    const auto Shown = [](EFlightHold Hold, double Fraction) { return static_cast<int32>(UShipHUDWidget::ShownHold(Hold, Fraction)); };
    TestEqual(TEXT("free is free"), Shown(EFlightHold::Free, 0.0), static_cast<int32>(EFlightHold::Free));
    TestEqual(TEXT("held 4% under the lever, not said"), Shown(EFlightHold::HoldingOff, 0.04), static_cast<int32>(EFlightHold::Free));
    TestEqual(TEXT("held exactly 5% under, not said"), Shown(EFlightHold::HoldingOff, 0.05), static_cast<int32>(EFlightHold::Free));
    TestEqual(TEXT("held 6% under, said"), Shown(EFlightHold::HoldingOff, 0.06), static_cast<int32>(EFlightHold::HoldingOff));
    TestEqual(TEXT("held to a crawl, said"), Shown(EFlightHold::HoldingOff, 0.999), static_cast<int32>(EFlightHold::HoldingOff));
    TestEqual(TEXT("at the floor is always said"), Shown(EFlightHold::AtFloor, 1.0), static_cast<int32>(EFlightHold::AtFloor));

    // -- Asked of the ship, in a real system ---------------------------------
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("HUDAltitudeTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    if (TestNotNull(TEXT("the world has a ship"), Ship))
    {
        const FSkySystem Here = LocalSystem::Current(World);
        const int32 World0 = Here.Bodies.IndexOfByPredicate([](const FSkyBody& Body) { return Body.Kind != ESkyBodyKind::Star; });
        if (TestTrue(TEXT("the ship opens in a system with a world in it"), World0 != INDEX_NONE))
        {
            const FString Name = Here.Bodies[World0].Id.ToString();
            const auto PlaceAt = [&](double AltitudeCm)
            {
                const TOptional<FNavPlacement> Placement = ShipSky::GotoPlacement(
                    Here, World0, AltitudeCm, Ship->GetFlightState().GetUniversePosition());
                if (Placement)
                {
                    Ship->PlaceShip(Placement->Position, Placement->Orientation);
                }
                return UShipHUDWidget::AltitudeLineText(*Ship).ToString();
            };

            TestEqual(TEXT("250 km over a world, the corner says so, by the world's name"),
                PlaceAt(250.0 * Km), FString::Printf(TEXT("250 KM ABOVE %s"), *Name));
            TestEqual(TEXT("it is the drive's own measure"),
                LocalSystem::NearestSurfaceDistance(LocalSystem::Current(World), Ship->GetFlightState().GetUniversePosition()) / Km,
                250.0, 1.0e-3);

            // The words follow the flight state's hold, not the height: the
            // ship on the floor, the nose into it, the lever above STOP. At
            // the floor's height with the lever at STOP, only the height.
            const double ShipFloor = UShipSubsystem::FloorFor(Here.Bodies[World0]);
            APawn* Pilot = World->SpawnActor<APawn>();
            Ship->SetPilot(Pilot);
            const auto Settle = [&](int32 Frames)
            {
                for (int32 Frame = 0; Frame < Frames; ++Frame)
                {
                    Ship->Tick(1.0f / 30.0f);
                }
                return UShipHUDWidget::AltitudeLineText(*Ship).ToString();
            };
            PlaceAt(ShipFloor + 1.0 * Km);
            const FString AtRest = Settle(1);
            TestFalse(TEXT("at the floor's height with the lever at STOP, no floor words"), AtRest.EndsWith(AtFloor));
            TestFalse(TEXT("nor any hold"), AtRest.EndsWith(HoldingOff));

            // Cruise with its lever at STOP is not held either: the drive off
            // at the floor's height says neither word.
            Ship->SetFlightCommand(Pilot, 0.0f, FVector::ZeroVector);
            const FString Cruising = Settle(30);
            TestFalse(TEXT("with the drive off at the floor's height, no floor words"),
                Cruising.EndsWith(AtFloor) || Cruising.EndsWith(HoldingOff));

            Ship->SetDriveEngaged(Pilot, true);
            Ship->SetDriveLever(Pilot, Ship->GetFlightState().GetDriveNotchCount() - 1);
            TestTrue(TEXT("with the drive's lever up and the nose on the world, settled onto the floor, the corner says so"),
                Settle(20 * 30).EndsWith(AtFloor));

            // From 250 km, with the lever at 1 c: the ship is still coming
            // down, and the cap is what is holding it to tens of km/s.
            PlaceAt(250.0 * Km);
            const FString Coming = Settle(1);
            TestFalse(TEXT("not at the floor from 250 km, where the ship is still coming down"), Coming.EndsWith(AtFloor));
            const FString Held = Settle(4 * 30);
            TestTrue(FString::Printf(TEXT("coming down under the lever at 1 c, the cap holds it off, and says so ('%s')"), *Held),
                Held.EndsWith(HoldingOff));
            TestTrue(TEXT("while the flight state says it holds more than 5% under the lever"),
                Ship->GetFlightState().GetHold() == EFlightHold::HoldingOff
                && Ship->GetFlightState().GetHeldFraction() > UShipHUDWidget::HoldingOffShown);

            // Held a few percent under the lever, the corner says nothing:
            // the flight state does report HOLDING OFF, and it is the corner
            // that keeps it back, on the 5% rule. The lever at its first
            // notch, settled far out; then the ship put where the cap's
            // speed is 3.5% under that notch, and one frame flown.
            const double FirstNotch = ShipDriveLever::NotchSpeed(1);
            Ship->SetDriveLever(Pilot, 1);
            PlaceAt(250.0 * Km);
            Settle(8 * 30);
            TestEqual(TEXT("far out, settled on the first notch"), Ship->GetFlightState().GetSpeed(), FirstNotch, FirstNotch * 1.0e-3);
            const FShipFlightLimits& Limits = Ship->GetFlightState().GetLimits();
            const auto May = [&](double D)
            {
                return ShipFlight::MaySpeed(D, Limits.LinearAcceleration, Limits.HoldSeconds, FShipFlightState::FixedStep);
            };
            // The cap's own speed is the one that rises with room, so the
            // room that allows 96.5% of the notch is found by halving.
            double Low = 0.0;
            double High = 250.0 * Km;
            for (int32 Halving = 0; Halving < 80; ++Halving)
            {
                const double Mid = 0.5 * (Low + High);
                (May(Mid) < 0.965 * FirstNotch ? Low : High) = Mid;
            }
            PlaceAt(ShipFloor + High);
            const FString Barely = Settle(1);
            const double BarelyHeld = Ship->GetFlightState().GetHeldFraction();
            TestTrue(FString::Printf(TEXT("the flight state holds the ship off, a few percent under its lever (%.4f)"), BarelyHeld),
                Ship->GetFlightState().GetHold() == EFlightHold::HoldingOff
                && BarelyHeld > 0.0 && BarelyHeld <= UShipHUDWidget::HoldingOffShown);
            TestTrue(FString::Printf(TEXT("and the corner does not say so ('%s')"), *Barely),
                Barely.Contains(TEXT(" ABOVE ")) && !Barely.EndsWith(HoldingOff) && !Barely.EndsWith(AtFloor));

            // Turned away from it at the floor, the ship is leaving.
            const TOptional<FNavPlacement> Placement = ShipSky::GotoPlacement(
                Here, World0, ShipFloor + 1.0 * Km, Ship->GetFlightState().GetUniversePosition());
            if (TestTrue(TEXT("a placement at the floor"), Placement.IsSet()))
            {
                Ship->PlaceShip(Placement->Position, Placement->Orientation * FQuat(FVector::UpVector, UE_DOUBLE_PI));
                const FString Leaving = Settle(1);
                TestFalse(TEXT("nose away from the world at the floor, it is leaving, not held"),
                    Leaving.EndsWith(AtFloor) || Leaving.EndsWith(HoldingOff));
            }
            Ship->AllStop(Pilot);

            // Asked for only its surfaces: the same line from the system
            // without its neighbours as from the one with them.
            TestEqual(TEXT("the neighbours change nothing the altitude says"),
                UShipHUDWidget::AltitudeLineText(*Ship, LocalSystem::Current(World)).ToString(),
                UShipHUDWidget::AltitudeLineText(*Ship).ToString());
            // And the corner itself draws it: the built HUD, ticked, shows
            // the line the static seam gives.
            UShipHUDWidget* HUD = NewObject<UShipHUDWidget>(World);
            HUD->Initialize();
            HUD->TakeWidget();
            HUD->NativeTick(FGeometry(), 0.016f);
            const FString Expected = UShipHUDWidget::AltitudeLineText(*Ship).ToString();
            bool bDrawn = false;
            HUD->WidgetTree->ForEachWidget([&](UWidget* Widget)
            {
                const UTextBlock* Block = Cast<UTextBlock>(Widget);
                bDrawn |= Block && Block->GetText().ToString() == Expected;
            });
            TestTrue(FString::Printf(TEXT("the HUD's corner reads '%s'"), *Expected), bDrawn && Expected.Contains(TEXT(" ABOVE ")));

            TestTrue(TEXT("and Here carries none of them"), LocalSystem::Here(World).Neighbours.IsEmpty()
                && LocalSystem::Here(World).Bodies.Num() == Here.Bodies.Num());
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
