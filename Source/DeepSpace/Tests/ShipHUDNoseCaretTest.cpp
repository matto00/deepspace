#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"
#include "Tests/SkyTestWorld.h"
#include "UI/ShipHUDWidget.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipHUDNoseCaretTest,
    "DeepSpace.UI.NoseCaret",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipHUDNoseCaretTestLocal
{
    /** The angle between two directions, accurate for tiny angles. */
    double AngleBetween(const FVector& A, const FVector& B)
    {
        return FMath::Atan2(FVector::CrossProduct(A, B).Size(), FVector::DotProduct(A, B));
    }

    /** One pixel of a 4K screen at 90 degrees: what "on the marker" means. */
    constexpr double Pixel4K = 2.0 / 3840.0;
}

/**
 * The nose caret sits on the teal course marker exactly when the ship is
 * aligned (nav decision 3), from any eye in the hull. A headless world has no
 * viewport to project into, so this checks the two directions the screen
 * projection is handed -- the caret's world point and the marker the
 * counter-frame draws -- and the rule for when the caret shows at all. Where
 * each lands in pixels is the engine's projection and a playtest's eye.
 */
bool FShipHUDNoseCaretTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipHUDNoseCaretTestLocal;

    FSkyWorld Test(TEXT("NoseCaretTestWorld"));
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("and a counter-frame"), Test.Frame))
    {
        return false;
    }
    Test.BeginPlay();
    UShipSubsystem* Ship = Test.Ship;
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    APawn* Crew = Test.World->SpawnActor<APawn>();

    // When it shows: flying, with somewhere to aim.
    Ship->SetPilot(Pilot);
    TestFalse(TEXT("with no course there is no caret"), UShipHUDWidget::ShowsNoseCaret(*Ship, Pilot));

    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (!TestTrue(TEXT("the chart has somewhere to go"), Chart.Num() > 0) || !TestTrue(TEXT("and it plots"), Ship->PlotCourse(Chart[0].Id)))
    {
        return false;
    }
    TestTrue(TEXT("plotted, the pilot has a caret"), UShipHUDWidget::ShowsNoseCaret(*Ship, Pilot));
    TestFalse(TEXT("anyone else aboard does not: they are not aiming"), UShipHUDWidget::ShowsNoseCaret(*Ship, Crew));
    TestFalse(TEXT("and nobody does from no pawn"), UShipHUDWidget::ShowsNoseCaret(*Ship, nullptr));
    Ship->ClearPilot();
    TestFalse(TEXT("out of the seat, the pilot's caret goes"), UShipHUDWidget::ShowsNoseCaret(*Ship, Pilot));
    Ship->SetPilot(Pilot);

    // Where it points: the ship's nose, from wherever the eye is, and onto
    // the marker exactly when aligned.
    const FVector Course = Ship->GetCourseDirection().Get(FVector::ForwardVector);
    const FUniversePosition Here = Ship->GetFlightState().GetUniversePosition();
    const FVector Galley(-600.0, 260.0, 150.0);
    const auto Offset = [&](const FQuat& Orientation, const FVector& Eye)
    {
        Ship->PlaceShip(Here, Orientation);
        Test.Step(0.0f);
        const UStaticMeshComponent* Marker = Test.Frame->GetCourseMarker();
        if (!Marker || !Marker->IsVisible())
        {
            return -1.0;
        }
        const FVector ToMarker = Marker->Bounds.Origin - Eye;
        const FVector ToCaret = UShipHUDWidget::NoseCaretWorldPoint(Eye) - Eye;
        return AngleBetween(ToMarker, ToCaret);
    };

    // Rolled about the nose, so an answer that took any axis but the nose
    // would move.
    const FQuat OnCourse = FRotationMatrix::MakeFromX(Course).ToQuat() * FQuat(FVector::ForwardVector, 1.1);
    for (const FVector& Eye : { PilotEye, Galley })
    {
        const double Aligned = Offset(OnCourse, Eye);
        TestTrue(FString::Printf(TEXT("aligned, the caret is on the marker from (%.0f, %.0f, %.0f): %.2g rad"), Eye.X, Eye.Y, Eye.Z, Aligned),
            Aligned >= 0.0 && Aligned < Pixel4K);
    }

    const double Ten = FMath::DegreesToRadians(10.0);
    const double Yawed = Offset(OnCourse * FQuat(FVector::UpVector, Ten), PilotEye);
    TestTrue(FString::Printf(TEXT("yawed 10 degrees off, the caret is 10 degrees from the marker (%.4f)"), FMath::RadiansToDegrees(Yawed)),
        FMath::Abs(Yawed - Ten) < Pixel4K);
    const double Pitched = Offset(OnCourse * FQuat(FVector::RightVector, Ten), Galley);
    TestTrue(FString::Printf(TEXT("pitched 10 degrees off, likewise (%.4f)"), FMath::RadiansToDegrees(Pitched)),
        FMath::Abs(Pitched - Ten) < Pixel4K);

    // Between stars there is no marker, and so no caret.
    {
        FScopedCVar Instant(TEXT("ds.Nav.ChargeSeconds"), 0.0f);
        Ship->PlaceShip(Here, OnCourse);
        Ship->SetJumpEngaged(true);
        for (int32 Tick = 0; Tick < 4 && !Ship->IsInTransit(); ++Tick)
        {
            Ship->Tick(0.05f);
        }
        if (TestTrue(TEXT("aligned, engaged and charged, the jump fires"), Ship->IsInTransit()))
        {
            TestFalse(TEXT("between stars the caret goes"), UShipHUDWidget::ShowsNoseCaret(*Ship, Pilot));
        }
    }
    return true;
}

#endif
