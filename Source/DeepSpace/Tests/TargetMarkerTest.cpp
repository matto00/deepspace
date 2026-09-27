#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipTags.h"
#include "UI/NavText.h"
#include "UI/ShipHUDWidget.h"
#include "UI/TargetMarker.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTargetMarkerViewTest,
    "DeepSpace.UI.TargetMarker.View",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTargetMarkerBearingTest,
    "DeepSpace.UI.TargetMarker.Bearing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTargetMarkerPlaceTest,
    "DeepSpace.UI.TargetMarker.Place",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTargetMarkerEtaTest,
    "DeepSpace.UI.TargetMarker.Eta",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FTargetSeenThroughGlassTest,
    "DeepSpace.Ship.TargetSeenThroughGlass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    const FString Degree = TEXT("°");

    /** Degrees of arc as radians, for readability in the cases. */
    double Deg(double Degrees)
    {
        return FMath::DegreesToRadians(Degrees);
    }

    /** A direction in ship axes AcrossDeg to starboard (negative: port) and
     *  UpDeg up, as NavTextTest builds them. */
    FVector FromAngles(double AcrossDeg, double UpDeg)
    {
        const double Across = Deg(AcrossDeg);
        const double Up = Deg(UpDeg);
        return FVector(FMath::Cos(Up) * FMath::Cos(Across), FMath::Cos(Up) * FMath::Sin(Across), FMath::Sin(Up));
    }

    /**
     * A red dwarf with three worlds, written out by hand: test data, not a
     * generator. The star sits off the universe origin, in another chunk, so
     * every distance is taken through the chunk index. Every world is at
     * phase 0, on +X of the star, so the star is on -X from each of them.
     */
    FStarSystem Kessa()
    {
        FStarSystem System;
        System.Stub.Id = FSystemId{ FInt64Vector(3, -2, 0), 7 };
        System.Stub.Seed = 0x5EED;
        System.Stub.Position = FUniversePosition(FInt64Vector(3, -2, 0), FVector(1.0e12, 5.0e12, 0.0));
        System.Stub.Name = TEXT("Kessa");
        System.Stub.Class = EStarClass::M;
        System.Star.Class = EStarClass::M;
        System.Star.RadiusSolar = 0.4;
        System.Star.LuminositySolar = 0.03;
        System.Star.TemperatureK = 3400.0;

        const auto AddWorld = [&System](const TCHAR* Numeral, EPlanetKind Kind, double RadiusEarth, double AU,
                                        const TCHAR* GivenName)
        {
            FPlanet& Planet = System.Planets.AddDefaulted_GetRef();
            Planet.Id = FBodyId{ System.Stub.Id, System.Planets.Num() - 1 };
            Planet.Designation = FString(TEXT("Kessa ")) + Numeral;
            Planet.GivenName = GivenName;
            Planet.Kind = Kind;
            Planet.RadiusEarth = RadiusEarth;
            Planet.SemiMajorAxisAU = AU;
        };
        AddWorld(TEXT("I"), EPlanetKind::Barren, 0.5, 0.1, TEXT(""));
        AddWorld(TEXT("II"), EPlanetKind::Terrestrial, 1.0, 0.214, TEXT(""));
        AddWorld(TEXT("III"), EPlanetKind::Ocean, 1.2, 0.34, TEXT("Halden"));
        return System;
    }

    double RadiusOf(const FStarSystem& System, int32 Orbit)
    {
        return System.Planets[Orbit].RadiusEarth * UniverseUnits::CmPerEarthRadius;
    }

    FBodyId Orbit(const FStarSystem& System, int32 Index)
    {
        return FBodyId{ System.Stub.Id, Index };
    }

    /** The boosters' full acceleration, cm/s^2, and the cap's hold. */
    const double Braking = FShipFlightLimits().LinearAcceleration;
    const double Hold = ShipFlight::DefaultHoldSeconds;
    const double Floor = ShipFlight::DefaultFloorCm;

    /** The view from Offset of orbit Index's centre, at rest, facing +X. */
    TOptional<FTargetView> ViewFrom(const FStarSystem& System, int32 Index, const FVector& Offset,
                                    const FQuat& Orientation = FQuat::Identity, const FVector& Velocity = FVector::ZeroVector)
    {
        return TargetMarker::View(System, Orbit(System, Index), System.PlanetPosition(Index) + Offset, Orientation,
                                  Velocity, Floor, Braking, Hold, false);
    }

    /** The speed of light, cm/s: the drive's top (ruling 1). */
    constexpr double C = 2.99792458e10;
}

bool FTargetMarkerViewTest::RunTest(const FString& Parameters)
{
    const FStarSystem System = Kessa();
    const double AU = UniverseUnits::CmPerAU;
    const double R = RadiusOf(System, 1);

    // Sunward of Kessa II by 0.03 AU, nose on it: the geometry by hand.
    {
        const double D = 0.03 * AU;
        const TOptional<FTargetView> View = ViewFrom(System, 1, FVector(-D, 0.0, 0.0));
        if (!TestTrue(TEXT("a world of the system the ship is in resolves"), View.IsSet()))
        {
            return false;
        }
        TestTrue(TEXT("it is the id asked for"), View->Id == Orbit(System, 1));
        TestEqual(TEXT("named as the map names it"), View->Name, FString(TEXT("Kessa II")));
        TestTrue(TEXT("dead ahead along the nose"), View->ShipLocalDir.Equals(FVector::ForwardVector, 1e-9));
        TestEqual(TEXT("its centre is 0.03 AU off"), View->CentreDistance, D, 1.0);
        TestEqual(TEXT("its surface a radius nearer"), View->SurfaceDistance, D - R, 1.0);
        TestEqual(TEXT("its angular radius is asin(R / d)"), View->AngularRadius, FMath::Asin(R / D), 1e-12);
        TestEqual(TEXT("a small far world is aimed at within the floor, not its disc"), View->AheadRadians,
                  TargetMarker::AheadFloor, 1e-15);
        TestTrue(TEXT("whose own radius is under a tenth of a degree there"), View->AngularRadius < Deg(0.1));
        TestEqual(TEXT("the star behind the ship: full phase"), View->LitFraction, 1.0, 1e-9);
        TestFalse(TEXT("and not the night side"), View->bNightSide);
        TestFalse(TEXT("at rest there is no time"), View->EtaSeconds.IsSet());
        TestFalse(TEXT("and no pass"), View->PassingCm.IsSet());
    }

    // Close in, the disc governs: two radii from its centre it is 30 degrees
    // of sky either way, and dead ahead anywhere on its face.
    {
        const TOptional<FTargetView> View = ViewFrom(System, 1, FVector(-2.0 * R, 0.0, 0.0));
        if (TestTrue(TEXT("near: resolves"), View.IsSet()))
        {
            TestEqual(TEXT("near: angular radius 30 degrees"), View->AngularRadius, Deg(30.0), 1e-9);
            TestEqual(TEXT("near: aimed at within its disc"), View->AheadRadians, View->AngularRadius, 1e-15);
        }
    }

    // The lit fraction, (1 + cos alpha) / 2, at 0, 90 and 180 degrees of
    // phase. The star is on -X from every world here.
    {
        const double D = 0.05 * AU;
        const TOptional<FTargetView> Quarter = ViewFrom(System, 1, FVector(0.0, D, 0.0));
        const TOptional<FTargetView> Behind = ViewFrom(System, 1, FVector(D, 0.0, 0.0));
        if (TestTrue(TEXT("both resolve"), Quarter.IsSet() && Behind.IsSet()))
        {
            TestEqual(TEXT("90 degrees: half lit"), Quarter->LitFraction, 0.5, 1e-9);
            TestFalse(TEXT("which is not the night side"), Quarter->bNightSide);
            TestEqual(TEXT("180 degrees: none of it"), Behind->LitFraction, 0.0, 1e-9);
            TestTrue(TEXT("which is"), Behind->bNightSide);
            TestTrue(TEXT("seen from beyond it, it lies back toward -X"),
                     Behind->ShipLocalDir.Equals(-FVector::ForwardVector, 1e-9));
        }
    }

    // NIGHT SIDE exactly past 0.15 lit: a phase angle of 134.4 degrees.
    {
        const double Edge = FMath::RadiansToDegrees(FMath::Acos(2.0 * TargetMarker::NightSideLit - 1.0));
        TestEqual(TEXT("the threshold is about 134 degrees of phase"), Edge, 134.43, 0.01);
        const auto AtPhase = [&](double AlphaDeg)
        {
            // Alpha is measured from the star's direction, -X.
            const double Theta = Deg(180.0 - AlphaDeg);
            return ViewFrom(System, 1, 0.05 * AU * FVector(FMath::Cos(Theta), FMath::Sin(Theta), 0.0));
        };
        const TOptional<FTargetView> Before = AtPhase(Edge - 0.2);
        const TOptional<FTargetView> After = AtPhase(Edge + 0.2);
        if (TestTrue(TEXT("both sides of the edge resolve"), Before.IsSet() && After.IsSet()))
        {
            TestTrue(TEXT("just before: a sliver over 0.15 lit"), Before->LitFraction > TargetMarker::NightSideLit);
            TestFalse(TEXT("just before: not the night side"), Before->bNightSide);
            TestFalse(TEXT("and the line does not say so"), TargetMarker::Line(*Before).EndsWith(TEXT("NIGHT SIDE")));
            TestTrue(TEXT("just past: under 0.15 lit"), After->LitFraction < TargetMarker::NightSideLit);
            TestTrue(TEXT("just past: the night side"), After->bNightSide);
            TestTrue(TEXT("and the line ends saying so"), TargetMarker::Line(*After).EndsWith(TEXT(" · NIGHT SIDE")));
        }
    }

    // Ship axes, not universe axes: turned to face +Y, a world on +X lies to
    // port. A mis-ordered rotation puts it to starboard.
    {
        const FQuat FacingY = FRotator(0.0, 90.0, 0.0).Quaternion();
        const TOptional<FTargetView> View = ViewFrom(System, 1, FVector(-0.05 * AU, 0.0, 0.0), FacingY);
        if (TestTrue(TEXT("turned: resolves"), View.IsSet()))
        {
            TestTrue(TEXT("turned: the world is to port"), View->ShipLocalDir.Equals(FVector(0.0, -1.0, 0.0), 1e-9));
            TestTrue(TEXT("and the line says so"),
                     TargetMarker::Line(*View).Contains(TEXT(" · 90") + Degree + TEXT(" to port · ")));
        }
    }

    // Inhabited worlds are named as the map names them.
    {
        const TOptional<FTargetView> View = ViewFrom(System, 2, FVector(-0.05 * AU, 0.0, 0.0));
        TestTrue(TEXT("given name, then designation"), View.IsSet() && View->Name == TEXT("Halden · Kessa III"));
    }

    // An id that names nothing here draws nothing, rather than the wrong
    // world: another system's, an orbit this one lacks, a moon, and any in
    // transit, when the sky is hidden and the line is empty.
    {
        const FUniversePosition Ship = System.PlanetPosition(1) + FVector(-0.05 * AU, 0.0, 0.0);
        const auto ViewOf = [&](const FBodyId& Id, bool bInTransit)
        {
            return TargetMarker::View(System, Id, Ship, FQuat::Identity, FVector::ZeroVector, Floor, Braking, Hold, bInTransit);
        };
        FBodyId Elsewhere = Orbit(System, 1);
        Elsewhere.System.Slot += 1;
        TestFalse(TEXT("another system's world names nothing here"), ViewOf(Elsewhere, false).IsSet());
        FBodyId OtherSector = Orbit(System, 1);
        OtherSector.System.Sector.Z += 1;
        TestFalse(TEXT("nor does one in another sector's same slot"), ViewOf(OtherSector, false).IsSet());
        TestFalse(TEXT("an orbit past the last names nothing"), ViewOf(Orbit(System, 3), false).IsSet());
        TestFalse(TEXT("nor a negative one"), ViewOf(Orbit(System, -1), false).IsSet());
        FBodyId Moon = Orbit(System, 1);
        Moon.Moon = 0;
        TestFalse(TEXT("nor a moon, which procgen does not make"), ViewOf(Moon, false).IsSet());
        TestFalse(TEXT("in transit, nothing at all"), ViewOf(Orbit(System, 1), true).IsSet());
        TestTrue(TEXT("and the same id out of transit is the world"), ViewOf(Orbit(System, 1), false).IsSet());
    }

    // The line, composed once: the mark, the name, the bearing, the
    // altitude's own words, then the time or the pass, then the night side.
    {
        FTargetView View;
        View.Name = TEXT("Kessa II");
        View.ShipLocalDir = FromAngles(0.1, 0.0);
        View.AheadRadians = Deg(0.01);
        View.SurfaceDistance = 0.214 * AU;
        TestEqual(TEXT("with nothing to add, it ends at the distance"), TargetMarker::Line(View),
                  TEXT("› Kessa II · 0.1") + Degree + TEXT(" to starboard · 0.214 AU"));
        TestEqual(TEXT("the distance is the altitude line's own words"), TargetMarker::Line(View),
                  TEXT("› Kessa II · 0.1") + Degree + TEXT(" to starboard · ") + UShipHUDWidget::AltitudeWords(0.214 * AU));

        View.EtaSeconds = 65.0;
        View.bNightSide = true;
        TestEqual(TEXT("then the time to arrival, then the night side"), TargetMarker::Line(View),
                  TEXT("› Kessa II · 0.1") + Degree + TEXT(" to starboard · 0.214 AU · ETA 65 S · NIGHT SIDE"));

        View.EtaSeconds.Reset();
        View.bNightSide = false;
        View.PassingCm = 0.031 * AU;
        TestEqual(TEXT("or how high it passes, in the same words"), TargetMarker::Line(View),
                  TEXT("› Kessa II · 0.1") + Degree + TEXT(" to starboard · 0.214 AU · PASSING 0.031 AU UP"));
    }

    // The prograde mark: the velocity in ship axes, and nothing under 1 m/s.
    {
        const FQuat FacingY = FRotator(0.0, 90.0, 0.0).Quaternion();
        TestTrue(TEXT("travelling +Y, facing +Y: prograde is the nose"),
                 TargetMarker::ProgradeShipLocal(FVector(0.0, 500.0, 0.0), FacingY).Equals(FVector::ForwardVector, 1e-9));
        TestTrue(TEXT("travelling +X, facing +Y: prograde is to port"),
                 TargetMarker::ProgradeShipLocal(FVector(500.0, 0.0, 0.0), FacingY).Equals(FVector(0.0, -1.0, 0.0), 1e-9));
        TestTrue(TEXT("it is a direction, whatever the speed"),
                 FMath::IsNearlyEqual(TargetMarker::ProgradeShipLocal(FVector(0.0, 0.0, -C), FacingY).Size(), 1.0, 1e-9));
        TestTrue(TEXT("at 1 m/s it shows"),
                 !TargetMarker::ProgradeShipLocal(FVector(TargetMarker::MinSpeed, 0.0, 0.0), FQuat::Identity).IsZero());
        TestTrue(TEXT("under it, hidden"),
                 TargetMarker::ProgradeShipLocal(FVector(0.99 * TargetMarker::MinSpeed, 0.0, 0.0), FQuat::Identity).IsZero());
    }
    return true;
}

bool FTargetMarkerBearingTest::RunTest(const FString& Parameters)
{
    // The world of the .03 AU question: an Earth's angular radius there.
    const double Small = Deg(0.08);
    const double Ahead = FMath::Max(Small, TargetMarker::AheadFloor);
    const double JumpCone = FNavTuning().ConeRadians;

    // The failure the developer described: 3 degrees off the nose is not on
    // the world, however the jump would word it.
    const FString ThreeOff = NavText::TargetBearing(FromAngles(-3.0, 0.0), Ahead);
    TestEqual(TEXT("3 degrees off does not read dead ahead"), ThreeOff, TEXT("3.0") + Degree + TEXT(" to port"));
    TestEqual(TEXT("where the jump's cone still calls it dead ahead, unchanged"),
              NavText::Bearing(FromAngles(-3.0, 0.0), JumpCone), FString(TEXT("dead ahead")));

    TestEqual(TEXT("0.2 degrees off a small world is on it: the floor governs"),
              NavText::TargetBearing(FromAngles(-0.2, 0.0), Ahead), FString(TEXT("dead ahead")));
    TestEqual(TEXT("0.4 degrees is not, in tenths"),
              NavText::TargetBearing(FromAngles(-0.4, 0.0), Ahead), TEXT("0.4") + Degree + TEXT(" to port"));
    TestEqual(TEXT("12.4 degrees is in whole degrees"),
              NavText::TargetBearing(FromAngles(-12.4, 0.0), Ahead), TEXT("12") + Degree + TEXT(" to port"));
    TestEqual(TEXT("a disc 5 degrees in radius, 3 off: the nose is on its face"),
              NavText::TargetBearing(FromAngles(-3.0, 0.0), FMath::Max(Deg(5.0), TargetMarker::AheadFloor)),
              FString(TEXT("dead ahead")));

    // The tenths stop at ten, chosen on the rounded value.
    TestEqual(TEXT("9.94 is 9.9"), NavText::TargetBearing(FromAngles(9.94, 0.0), Ahead),
              TEXT("9.9") + Degree + TEXT(" to starboard"));
    TestEqual(TEXT("9.96 is 10, never 10.0"), NavText::TargetBearing(FromAngles(9.96, 0.0), Ahead),
              TEXT("10") + Degree + TEXT(" to starboard"));

    // Two turns, each in its own precision; a level one names no height.
    TestEqual(TEXT("12 to port, 3.0 up"), NavText::TargetBearing(FromAngles(-12.0, 3.0), Ahead),
              TEXT("12") + Degree + TEXT(" to port, 3.0") + Degree + TEXT(" up"));
    TestEqual(TEXT("straight down by a little"), NavText::TargetBearing(FromAngles(0.0, -0.6), Ahead),
              TEXT("0.6") + Degree + TEXT(" down"));
    TestEqual(TEXT("not normalised by the caller"), NavText::TargetBearing(250.0 * FromAngles(0.0, 4.5), Ahead),
              TEXT("4.5") + Degree + TEXT(" up"));

    // Astern, as the jump says it.
    TestEqual(TEXT("dead astern"), NavText::TargetBearing(-FVector::ForwardVector, Ahead), FString(TEXT("astern")));
    TestEqual(TEXT("astern to starboard and down"), NavText::TargetBearing(FromAngles(120.0, -20.0), Ahead),
              FString(TEXT("astern, to starboard, down")));

    // Exactly on the boundary: within AheadRadians is ahead.
    TestEqual(TEXT("at the floor exactly: dead ahead"),
              NavText::TargetBearing(FromAngles(-0.25 + 1e-9, 0.0), TargetMarker::AheadFloor), FString(TEXT("dead ahead")));
    TestEqual(TEXT("just outside it: a bearing"),
              NavText::TargetBearing(FromAngles(-0.26, 0.0), TargetMarker::AheadFloor), TEXT("0.3") + Degree + TEXT(" to port"));
    TestEqual(TEXT("the floor is a quarter of a degree"), FMath::RadiansToDegrees(TargetMarker::AheadFloor), 0.25, 1e-12);
    return true;
}

bool FTargetMarkerPlaceTest::RunTest(const FString& Parameters)
{
    const FVector2D View(1920.0, 1080.0);
    const float Min = TargetMarker::DefaultMinPixels;
    const float Inset = TargetMarker::DefaultEdgeInset;
    const FVector Ahead = FVector::ForwardVector;

    const auto Bracketed = [this](const FTargetMark& Mark, const FVector2D& Centre, float Size, const TCHAR* What)
    {
        TestEqual(FString(What) + TEXT(": a bracket"), Mark.Shape, ETargetMarkShape::Bracket);
        TestTrue(FString(What) + TEXT(": centred on the world"), Mark.Centre.Equals(Centre, 1e-4));
        TestEqual(FString(What) + TEXT(": its size"), Mark.Size, Size, 1e-3f);
    };

    // A sub-pixel world, and four black pixels on the night side: the least
    // bracket, which can be found across the glass.
    Bracketed(TargetMarker::Place(true, FVector2D(900.0, 500.0), 0.3f, Ahead, View, Min, Inset, true, true),
              FVector2D(900.0, 500.0), Min, TEXT("sub-pixel"));
    Bracketed(TargetMarker::Place(true, FVector2D(900.0, 500.0), 2.0f, Ahead, View, Min, Inset, false, true),
              FVector2D(900.0, 500.0), Min, TEXT("four pixels, for a walker at the glass"));

    // A disc: its diameter, padded a side.
    Bracketed(TargetMarker::Place(true, FVector2D(1000.0, 400.0), 100.0f, Ahead, View, Min, Inset, true, true),
              FVector2D(1000.0, 400.0), 200.0f + 2.0f * TargetMarker::BracketPadding, TEXT("a disc"));

    // Past 80% of the short side the world is its own marker.
    {
        const float Limit = TargetMarker::HideFraction * 1080.0f;
        const float Under = 0.5f * (Limit - 1.0f) - TargetMarker::BracketPadding;
        const float Over = 0.5f * (Limit + 1.0f) - TargetMarker::BracketPadding;
        Bracketed(TargetMarker::Place(true, FVector2D(960.0, 540.0), Under, Ahead, View, Min, Inset, true, true),
                  FVector2D(960.0, 540.0), Limit - 1.0f, TEXT("just under 80%"));
        TestEqual(TEXT("just over 80% of the short side: nothing"),
                  TargetMarker::Place(true, FVector2D(960.0, 540.0), Over, Ahead, View, Min, Inset, true, true).Shape,
                  ETargetMarkShape::None);
        TestEqual(TEXT("the short side, not the long"),
                  TargetMarker::Place(true, FVector2D(960.0, 540.0), Over, Ahead, FVector2D(1080.0, 1920.0), Min, Inset, true, true).Shape,
                  ETargetMarkShape::None);
    }

    // Behind a wall, on the view: nothing, for anyone.
    TestEqual(TEXT("occluded: no bracket for the pilot"),
              TargetMarker::Place(true, FVector2D(900.0, 500.0), 0.3f, Ahead, View, Min, Inset, true, false).Shape,
              ETargetMarkShape::None);
    TestEqual(TEXT("occluded: none for a walker"),
              TargetMarker::Place(true, FVector2D(900.0, 500.0), 0.3f, Ahead, View, Min, Inset, false, false).Shape,
              ETargetMarkShape::None);
    // The view's edge is on it.
    TestEqual(TEXT("on the very edge it is still in view"),
              TargetMarker::Place(true, FVector2D(1920.0, 0.0), 0.3f, Ahead, View, Min, Inset, true, true).Shape,
              ETargetMarkShape::Bracket);

    const auto Chevron = [this](const FTargetMark& Mark, const FVector2D& Centre, const FVector2D& Pointing, const TCHAR* What)
    {
        TestEqual(FString(What) + TEXT(": a chevron"), Mark.Shape, ETargetMarkShape::Chevron);
        TestTrue(FString::Printf(TEXT("%s: on the inset edge (%s)"), What, *Mark.Centre.ToString()), Mark.Centre.Equals(Centre, 1e-3));
        TestTrue(FString::Printf(TEXT("%s: pointing the way to turn (%s)"), What, *Mark.Pointing.ToString()),
                 Mark.Pointing.Equals(Pointing, 1e-6));
    };

    // Off the view to starboard: the pilot gets a chevron on the right edge,
    // pointing right; nobody else gets anything.
    Chevron(TargetMarker::Place(true, FVector2D(2500.0, 540.0), 0.3f, FromAngles(70.0, 0.0), View, Min, Inset, true, true),
            FVector2D(1920.0 - Inset, 540.0), FVector2D(1.0, 0.0), TEXT("off to starboard"));
    TestEqual(TEXT("off the view, a walker gets nothing"),
              TargetMarker::Place(true, FVector2D(2500.0, 540.0), 0.3f, FromAngles(70.0, 0.0), View, Min, Inset, false, true).Shape,
              ETargetMarkShape::None);
    // It makes no claim about where the target is, so it is not occluded.
    Chevron(TargetMarker::Place(true, FVector2D(2500.0, 540.0), 0.3f, FromAngles(70.0, 0.0), View, Min, Inset, true, false),
            FVector2D(1920.0 - Inset, 540.0), FVector2D(1.0, 0.0), TEXT("off to starboard, behind a wall"));

    // Up and to port, along the diagonal: it meets the top inset first.
    {
        const FVector2D Way = FVector2D(-1.0, -1.0).GetSafeNormal();
        const double T = (540.0 - Inset) / FMath::Abs(Way.Y);
        Chevron(TargetMarker::Place(true, FVector2D(960.0 - 3000.0, 540.0 - 3000.0), 0.3f, FromAngles(-60.0, 50.0), View, Min,
                                    Inset, true, true),
                FVector2D(960.0, 540.0) + Way * T, Way, TEXT("up and to port"));
    }

    // Behind the camera the projection fails; the camera's axes say which
    // way: right is +X on the widget, up is -Y.
    Chevron(TargetMarker::Place(false, FVector2D::ZeroVector, 0.3f, FVector(-1.0, -0.5, 0.0), View, Min, Inset, true, true),
            FVector2D(Inset, 540.0), FVector2D(-1.0, 0.0), TEXT("behind, to port"));
    Chevron(TargetMarker::Place(false, FVector2D::ZeroVector, 0.3f, FVector(-1.0, 0.0, 0.8), View, Min, Inset, true, true),
            FVector2D(960.0, Inset), FVector2D(0.0, -1.0), TEXT("behind, overhead"));
    Chevron(TargetMarker::Place(false, FVector2D::ZeroVector, 0.3f, FVector(-1.0, 0.0, 0.0), View, Min, Inset, true, true),
            FVector2D(960.0, 1080.0 - Inset), FVector2D(0.0, 1.0), TEXT("dead astern"));
    // A projection that "succeeded" behind the camera would be mirrored; the
    // flag, not the numbers, decides.
    Chevron(TargetMarker::Place(false, FVector2D(900.0, 500.0), 0.3f, FVector(-1.0, 0.3, 0.0), View, Min, Inset, true, true),
            FVector2D(1920.0 - Inset, 540.0), FVector2D(1.0, 0.0), TEXT("behind, with a centre that looks on-view"));
    TestEqual(TEXT("behind, a walker gets nothing"),
              TargetMarker::Place(false, FVector2D::ZeroVector, 0.3f, FVector(-1.0, -0.5, 0.0), View, Min, Inset, false, true).Shape,
              ETargetMarkShape::None);

    TestEqual(TEXT("no view: nothing"),
              TargetMarker::Place(true, FVector2D(0.0, 0.0), 0.3f, Ahead, FVector2D::ZeroVector, Min, Inset, true, true).Shape,
              ETargetMarkShape::None);
    return true;
}

bool FTargetMarkerEtaTest::RunTest(const FString& Parameters)
{
    const FStarSystem System = Kessa();
    const double AU = UniverseUnits::CmPerAU;
    const double R = RadiusOf(System, 1);
    const double L = 0.2 * AU;
    const FVector From(-L, 0.0, 0.0);                   // sunward of Kessa II, 0.2 AU
    const FFlightSurface Surface{ System.PlanetPosition(1), R, Floor, false };

    // At 1 c straight at it: the ETA is the flight law's own, at the ray's
    // distance to the floor sphere.
    {
        const TOptional<FTargetView> View = ViewFrom(System, 1, From, FQuat::Identity, FVector(C, 0.0, 0.0));
        if (TestTrue(TEXT("at 1 c: resolves"), View.IsSet()) && TestTrue(TEXT("with a time"), View->EtaSeconds.IsSet()))
        {
            const double D = L - R - Floor;
            TestEqual(TEXT("the distance is to the floor, by hand"),
                      *ShipFlight::RayToFloor(Surface, System.PlanetPosition(1) + From, FVector(C, 0.0, 0.0)), D, 1.0);
            TestEqual(TEXT("the time is SecondsToFloor of it, at the present speed"), *View->EtaSeconds,
                      ShipFlight::SecondsToFloor(D, C, Braking, Hold), 1e-6);
            TestTrue(TEXT("which is more than the light time, since the cap brakes"), *View->EtaSeconds > D / C);
            TestFalse(TEXT("and there is no pass"), View->PassingCm.IsSet());
            TestTrue(TEXT("the line ends in its time"),
                     TargetMarker::Line(*View).EndsWith(TEXT(" · ETA ") + NavText::Duration(*View->EtaSeconds)));
        }
    }

    // The ETA's appearing is "will the drive bring me down on it?": exactly
    // when the ray meets the floor sphere. Its tangent from 0.2 AU:
    {
        const double Tangent = FMath::Asin((R + Floor) / L);
        const auto At = [&](double Angle)
        {
            return ViewFrom(System, 1, From, FQuat::Identity, 1.0e6 * FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0));
        };
        const TOptional<FTargetView> Inside = At(Tangent * (1.0 - 1e-6));
        const TOptional<FTargetView> Outside = At(Tangent * (1.0 + 1e-6));
        if (TestTrue(TEXT("both sides of the tangent resolve"), Inside.IsSet() && Outside.IsSet()))
        {
            TestTrue(TEXT("just inside the floor's tangent: a time"), Inside->EtaSeconds.IsSet());
            TestFalse(TEXT("and no pass"), Inside->PassingCm.IsSet());
            TestFalse(TEXT("just outside: no time to nowhere"), Outside->EtaSeconds.IsSet());
            TestTrue(TEXT("but a pass"), Outside->PassingCm.IsSet());
            if (Outside->PassingCm)
            {
                TestEqual(TEXT("at the floor's height, give or take the grazing"), *Outside->PassingCm, Floor, 1.0e3);
            }
        }

        // A clear miss: the closest approach, by hand, less the radius.
        const double Angle = Deg(1.0);
        const TOptional<FTargetView> Miss = At(Angle);
        if (TestTrue(TEXT("a miss resolves"), Miss.IsSet()) && TestTrue(TEXT("and passes"), Miss->PassingCm.IsSet()))
        {
            TestEqual(TEXT("passing at L sin(angle) less the radius"), *Miss->PassingCm, L * FMath::Sin(Angle) - R, 1.0e3);
            TestEqual(TEXT("worded as the altitude is"), TargetMarker::Line(*Miss),
                      TEXT("› Kessa II · ") + NavText::TargetBearing(FVector::ForwardVector, Miss->AheadRadians) + TEXT(" · ")
                          + UShipHUDWidget::AltitudeWords(L - R) + TEXT(" · PASSING ")
                          + UShipHUDWidget::AltitudeWords(*Miss->PassingCm) + TEXT(" UP"));
        }
    }

    // Neither at rest, under 1 m/s, or opening; at 1 m/s, a time.
    {
        const auto Toward = [&](double Speed) { return ViewFrom(System, 1, From, FQuat::Identity, FVector(Speed, 0.0, 0.0)); };
        const TOptional<FTargetView> Rest = Toward(0.0);
        const TOptional<FTargetView> Creep = Toward(0.99 * TargetMarker::MinSpeed);
        const TOptional<FTargetView> Walk = Toward(TargetMarker::MinSpeed);
        const TOptional<FTargetView> Away = Toward(-C);
        const TOptional<FTargetView> Across = ViewFrom(System, 1, From, FQuat::Identity, FVector(0.0, C, 0.0));
        if (TestTrue(TEXT("all resolve"), Rest && Creep && Walk && Away && Across))
        {
            TestTrue(TEXT("at rest: neither"), !Rest->EtaSeconds && !Rest->PassingCm);
            TestTrue(TEXT("under 1 m/s: neither"), !Creep->EtaSeconds && !Creep->PassingCm);
            TestTrue(TEXT("at 1 m/s: a time"), Walk->EtaSeconds.IsSet());
            TestTrue(TEXT("opening at 1 c: neither"), !Away->EtaSeconds && !Away->PassingCm);
            TestTrue(TEXT("square across it: neither, since it is not closing"), !Across->EtaSeconds && !Across->PassingCm);
            TestTrue(TEXT("the line with neither ends at the distance"),
                     TargetMarker::Line(*Rest).EndsWith(UShipHUDWidget::AltitudeWords(Rest->SurfaceDistance)));
        }
    }

    // Under the floor, heading in: arrived, and it says so rather than
    // nothing.
    {
        const TOptional<FTargetView> Under = ViewFrom(System, 1, FVector(-(R + 0.5 * Floor), 0.0, 0.0), FQuat::Identity,
                                                      FVector(1000.0, 0.0, 0.0));
        TestTrue(TEXT("under the floor, heading in: ETA 0 S"), Under && Under->EtaSeconds && *Under->EtaSeconds == 0.0
                 && TargetMarker::Line(*Under).EndsWith(TEXT("ETA 0 S")));
    }

    // With no braking at all, which only a CVar can arrange, the cap never
    // arrives, and a time that is not finite is no time.
    {
        const TOptional<FTargetView> NoBrakes = TargetMarker::View(System, Orbit(System, 1), System.PlanetPosition(1) + From,
                                                                   FQuat::Identity, FVector(C, 0.0, 0.0), Floor, 0.0, Hold, false);
        TestTrue(TEXT("no brakes: no time"), NoBrakes && !NoBrakes->EtaSeconds && !NoBrakes->PassingCm);
    }

    // The words (ruling 3): each unit chosen on the rounded value it prints.
    TestEqual(TEXT("52 S"), NavText::Duration(52.0), FString(TEXT("52 S")));
    TestEqual(TEXT("99 S"), NavText::Duration(99.4), FString(TEXT("99 S")));
    TestEqual(TEXT("99.6 s is not 100 S but 2 MIN"), NavText::Duration(99.6), FString(TEXT("2 MIN")));
    TestEqual(TEXT("12 MIN"), NavText::Duration(12.0 * 60.0), FString(TEXT("12 MIN")));
    TestEqual(TEXT("59 MIN"), NavText::Duration(59.4 * 60.0), FString(TEXT("59 MIN")));
    TestEqual(TEXT("59.6 min is not 60 MIN but 1.0 H"), NavText::Duration(59.6 * 60.0), FString(TEXT("1.0 H")));
    TestEqual(TEXT("4.2 H"), NavText::Duration(4.2 * 3600.0), FString(TEXT("4.2 H")));
    TestEqual(TEXT("47.9 H"), NavText::Duration(47.94 * 3600.0), FString(TEXT("47.9 H")));
    TestEqual(TEXT("47.96 h is not 48.0 H but 2 D"), NavText::Duration(47.96 * 3600.0), FString(TEXT("2 D")));
    TestEqual(TEXT("3 D"), NavText::Duration(3.0 * 86400.0), FString(TEXT("3 D")));
    TestEqual(TEXT("arrived: 0 S"), NavText::Duration(0.0), FString(TEXT("0 S")));
    TestEqual(TEXT("never negative"), NavText::Duration(-5.0), FString(TEXT("0 S")));
    TestTrue(TEXT("a time that never comes has no words"), NavText::Duration(std::numeric_limits<double>::infinity()).IsEmpty());
    TestTrue(TEXT("nor one that is not a number"), NavText::Duration(std::numeric_limits<double>::quiet_NaN()).IsEmpty());
    return true;
}

namespace
{
    /** A box on the ray: the engine cube, blocking everything unless told
     *  otherwise, tagged as glass or not. Placed by its measured bounds, not
     *  an assumed pivot. */
    AStaticMeshActor* SpawnBox(UWorld* World, UStaticMesh* Cube, const FVector& Centre, bool bGlass)
    {
        AStaticMeshActor* Box = World->SpawnActor<AStaticMeshActor>(Centre - Cube->GetBoundingBox().GetCenter(), FRotator::ZeroRotator);
        if (!Box)
        {
            return nullptr;
        }
        UStaticMeshComponent* Mesh = Box->GetStaticMeshComponent();
        Mesh->SetStaticMesh(Cube);
        Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
        if (bGlass)
        {
            Box->Tags.Add(ShipTags::Glass);
        }
        return Box;
    }
}

bool FTargetSeenThroughGlassTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TargetSeenThroughGlassWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };

    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("the engine cube loads"), Cube))
    {
        return false;
    }
    const double Half = Cube->GetBoundingBox().GetExtent().X;

    // Each case in a lane of its own along +X, 10 m apart, so no case's box
    // is on another's ray. The eye is at x 0 in every lane.
    const auto Lane = [](int32 Index) { return FVector(0.0, 1000.0 * Index, 0.0); };
    const FVector Along = FVector::ForwardVector;

    SpawnBox(World, Cube, Lane(1) + FVector(500.0, 0.0, 0.0), true);
    SpawnBox(World, Cube, Lane(2) + FVector(500.0, 0.0, 0.0), false);
    SpawnBox(World, Cube, Lane(3) + FVector(500.0, 0.0, 0.0), true);
    SpawnBox(World, Cube, Lane(3) + FVector(1500.0, 0.0, 0.0), false);
    SpawnBox(World, Cube, Lane(4) + FVector(500.0, 0.0, 0.0), false);
    SpawnBox(World, Cube, Lane(4) + FVector(1500.0, 0.0, 0.0), true);
    // A wall just inside 30 m, and one just beyond it.
    SpawnBox(World, Cube, Lane(5) + FVector(TargetMarker::GlassTraceCm - Half - 100.0, 0.0, 0.0), false);
    SpawnBox(World, Cube, Lane(6) + FVector(TargetMarker::GlassTraceCm + Half + 100.0, 0.0, 0.0), false);
    // The viewer's own body, just in front of the eye.
    AStaticMeshActor* Viewer = SpawnBox(World, Cube, Lane(7) + FVector(Half + 10.0, 0.0, 0.0), false);
    // Clutter blocks only the camera channel (the dressing's rule), so it
    // never hides the sky.
    AStaticMeshActor* Mug = SpawnBox(World, Cube, Lane(8) + FVector(500.0, 0.0, 0.0), false);
    if (!TestNotNull(TEXT("the viewer spawns"), Viewer) || !TestNotNull(TEXT("and the mug"), Mug))
    {
        return false;
    }
    Mug->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mug->GetStaticMeshComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);

    const auto Seen = [&](int32 Index, const FVector& Dir, const AActor* Who = nullptr)
    {
        return TargetMarker::SeenThroughGlass(World, Lane(Index), Dir, Who);
    };
    TestTrue(TEXT("nothing in the way: the sky"), Seen(0, Along));
    TestTrue(TEXT("the glass first: the sky"), Seen(1, Along));
    TestFalse(TEXT("a wall first: no sky"), Seen(2, Along));
    TestTrue(TEXT("glass, then a wall beyond it: the glass is what the eye meets"), Seen(3, Along));
    TestFalse(TEXT("a wall, then glass beyond it: the wall"), Seen(4, Along));
    TestFalse(TEXT("a wall just inside 30 m hides it"), Seen(5, Along));
    TestTrue(TEXT("one past 30 m does not: nothing is that far inside the hull"), Seen(6, Along));
    TestTrue(TEXT("the viewer never hides their own view"), Seen(7, Along, Viewer));
    TestFalse(TEXT("which is the viewer being ignored, not the box missing"), Seen(7, Along));
    TestTrue(TEXT("clutter that blocks only the camera never hides it"), Seen(8, Along));
    TestTrue(TEXT("the direction is a direction: looking the other way from a wall"), Seen(2, -Along));
    TestFalse(TEXT("and it is not a length: a short one still meets the wall"), Seen(2, 0.01 * Along));
    TestFalse(TEXT("no direction: no bracket, the safe failure"), Seen(0, FVector::ZeroVector));
    TestFalse(TEXT("no world: the same"), TargetMarker::SeenThroughGlass(nullptr, FVector::ZeroVector, Along, nullptr));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
