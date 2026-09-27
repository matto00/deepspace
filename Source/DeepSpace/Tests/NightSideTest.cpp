#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipFlightSurface.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Tests/SkyTestWorld.h"
#include "UI/TargetMarker.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNightSideIsDrawnTest,
    "DeepSpace.Sky.NightSideIsDrawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    using namespace SkyTestWorld;

    /**
     * The .03 AU question's one source: a Sun and an Earth at 1 AU, written
     * out by hand as procgen would give them. Its sky is derived through
     * LocalSystem::Here, never built beside it, so the proxy and the target
     * cannot disagree about where the world is. The star sits in another
     * chunk, so every distance is taken through the chunk index.
     */
    FStarSystem SunAndEarth()
    {
        FStarSystem System;
        System.Stub.Id = FSystemId{ FInt64Vector(3, -2, 0), 1 };
        System.Stub.Seed = 0xEA27;
        System.Stub.Position = FUniversePosition(FInt64Vector(3, -2, 0), FVector(1.0e12, 5.0e12, 0.0));
        System.Stub.Name = TEXT("Night");
        System.Stub.Class = EStarClass::G;
        System.Stub.LuminositySolar = 1.0;
        System.Stub.TemperatureK = UniverseUnits::SolarTemperatureK;
        System.Star.Class = EStarClass::G;
        System.Star.MassSolar = 1.0;
        System.Star.RadiusSolar = 1.0;
        System.Star.LuminositySolar = 1.0;
        System.Star.TemperatureK = UniverseUnits::SolarTemperatureK;

        FPlanet& Earth = System.Planets.AddDefaulted_GetRef();
        Earth.Id = FBodyId{ System.Stub.Id, 0 };
        Earth.Designation = TEXT("Night I");
        Earth.Kind = EPlanetKind::Terrestrial;
        Earth.MassEarth = 1.0;
        Earth.RadiusEarth = 1.0;
        Earth.EquilibriumK = 255.0;
        Earth.SemiMajorAxisAU = 1.0;
        Earth.PhaseRad = 0.0;
        return System;
    }

    /** The disc-averaged emission of a body's material: Brightness times
     *  lerp(shaded, 1, PointBlend), with the shaded term averaging to the
     *  phase over the disc (SkyProjection::LambertPhase). For a resolved
     *  disc Brightness alone is the surface's and ignores the phase: the
     *  dark is in the shading, so it is this, not the scalar, that says how
     *  much light the world sends the eye. */
    double DiscEmission(UMaterialInstanceDynamic* Instance, double Phase)
    {
        const double Brightness = Instance->K2_GetScalarParameterValue(SkyMaterial::Brightness);
        const double Blend = Instance->K2_GetScalarParameterValue(SkyMaterial::PointBlend);
        return Brightness * (Blend + (1.0 - Blend) * Phase);
    }

    /** The helm's view: 103 degrees across 3840 pixels, the 4K display the
     *  .03 AU reading was made on. */
    const double HelmPixelAngle = 2.0 * FMath::Tan(FMath::DegreesToRadians(0.5 * 103.0)) / 3840.0;
}

bool FNightSideIsDrawnTest::RunTest(const FString& Parameters)
{
    FSkyWorld Test(TEXT("NightSideIsDrawnWorld"));
    APlayerController* Player = Test.World ? Test.World->SpawnActor<APlayerController>() : nullptr;
    if (!TestNotNull(TEXT("a ship"), Test.Ship) || !TestNotNull(TEXT("a counter-frame"), Test.Frame)
        || !TestNotNull(TEXT("a sky"), Test.Sky) || !TestNotNull(TEXT("a player, for the view"), Player))
    {
        return false;
    }
    Test.BeginPlay();
    if (!TestNotNull(TEXT("the player has a camera"), Player->PlayerCameraManager.Get()))
    {
        return false;
    }
    // Headless there is no viewport to be wide, so the pixel angle is the
    // camera's field of view over the fallback width (ShipSky::PixelAngle).
    // The field of view that gives the helm's 4K pixel over that width:
    const double Fov = 2.0 * FMath::Atan(0.5 * HelmPixelAngle * ShipSky::FallbackWidthPixels);
    Player->PlayerCameraManager->SetFOV(static_cast<float>(FMath::RadiansToDegrees(Fov)));
    const double Pixel = Test.Sky->GetPixelAngle();
    TestEqual(TEXT("the sky sees the helm's 4K pixel"), Pixel, HelmPixelAngle, 1e-3 * HelmPixelAngle);

    const FStarSystem System = SunAndEarth();
    const FSkySystem Sky = LocalSystem::Here(TOptional<FStarSystem>(System));
    const int32 EarthIndex = 1;   // FromSystem puts the star first, then planets innermost first
    if (!TestEqual(TEXT("the sky has the star and the world"), Sky.Bodies.Num(), 2))
    {
        return false;
    }
    const double AU = UniverseUnits::CmPerAU;
    const FUniversePosition Earth = System.PlanetPosition(0);

    // 0.03 AU outside the Earth on the sun-planet line, 0.002 AU aside: the
    // Earth 3.7 degrees off the sun's centre, at a 176-degree phase angle,
    // nose on it -- "heading straight to it" from outside its orbit.
    const FVector Night(0.03 * AU, 0.002 * AU, 0.0);
    const FUniversePosition Ship = Earth + Night;
    const FQuat Facing = FRotationMatrix::MakeFromX(-Night).ToQuat();
    Test.Ship->PlaceShip(Ship, Facing);
    Test.Frame->SyncToShip();
    Test.Sky->DrawFrom(Sky);

    const FSkyFrame& Frame = Test.Sky->GetLastFrame();
    if (!TestEqual(TEXT("one view per body"), Frame.Bodies.Num(), Sky.Bodies.Num()))
    {
        return false;
    }
    const FSkyBodyView& View = Frame.Bodies[EarthIndex];
    const FQuat Counter = Test.Ship->GetCounterFrameTransform().GetRotation();
    const FVector TrueDirection = Counter.RotateVector((Earth - Ship).GetSafeNormal());

    // The geometry is the one described.
    {
        const FVector ToStar = (System.Stub.Position - Earth).GetSafeNormal();
        const double PhaseAngle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(ToStar, (Ship - Earth).GetSafeNormal())));
        TestEqual(TEXT("seen at a 176-degree phase angle"), PhaseAngle, 176.2, 0.1);
        TestEqual(TEXT("the Earth's true angular diameter is 0.163 degrees"),
                  FMath::RadiansToDegrees(2.0 * View.AngularRadius), 0.163, 0.001);
    }

    // The proxy is drawn: it exists, and nothing hides it.
    const UStaticMeshComponent* Proxy = Test.Sky->GetProxy(EarthIndex);
    if (!TestNotNull(TEXT("the Earth has a proxy"), Proxy))
    {
        return false;
    }
    TestTrue(TEXT("which is visible"), Proxy->IsVisible());
    TestFalse(TEXT("and not hidden in game"), Proxy->bHiddenInGame);

    // The right size: resolved, at its true angular radius, 4 px or more.
    TestEqual(TEXT("resolved, not a point"), View.PointBlend, 0.0);
    TestEqual(TEXT("drawn at its true angular radius"), View.DrawnAngularRadius, View.AngularRadius, 1e-9 * View.AngularRadius);
    const double Pixels = 2.0 * View.AngularRadius / Pixel;
    TestTrue(FString::Printf(TEXT("at 4 px or more across (%.2f)"), Pixels), Pixels >= 4.0);
    const FDrawnSphere Sphere = Drawn(Proxy, Test.Sphere);
    const double Seen = Subtense(Sphere, FVector::ZeroVector);
    TestTrue(FString::Printf(TEXT("the drawn sphere subtends its true diameter to a pixel (%.4f px off)"),
                             (Seen - 2.0 * View.AngularRadius) / Pixel),
             FMath::Abs(Seen - 2.0 * View.AngularRadius) < Pixel);

    // In the right place: within a pixel of the true direction, in the band.
    const double Off = FMath::Acos(FMath::Clamp(FVector::DotProduct(Sphere.Centre.GetSafeNormal(), TrueDirection), -1.0, 1.0));
    TestTrue(FString::Printf(TEXT("where the Earth is, to a pixel (%.4f px off)"), Off / Pixel), Off < Pixel);
    const FSkyViewParams Band;
    TestTrue(TEXT("its near side inside the depth band"), Sphere.Centre.Size() - Sphere.Radius >= Band.NearProxy * (1.0 - 1e-6));
    TestTrue(TEXT("and its far side"), Sphere.Centre.Size() + Sphere.Radius <= Band.FarProxy * (1.0 + 1e-6));

    // Next to the sun: in its glare.
    {
        const double FromStar = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            FVector::DotProduct(Frame.Bodies[0].Direction, View.Direction), -1.0, 1.0)));
        TestTrue(FString::Printf(TEXT("the star is within 5 degrees of it (%.2f)"), FromStar), FromStar < 5.0);
    }

    // Dark. The phase, the light's direction the material shades by, and the
    // light the disc sends, against the same world at full phase.
    UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Proxy->GetMaterial(0));
    if (!TestNotNull(TEXT("the Earth has its own material instance"), Instance))
    {
        return false;
    }
    TestTrue(FString::Printf(TEXT("its phase is under 0.02 (%.5f)"), View.Phase), View.Phase < 0.02);
    {
        const FLinearColor Light = Instance->K2_GetVectorParameterValue(SkyMaterial::LightDirection);
        const FVector LightWorld(Light.R, Light.G, Light.B);
        TestTrue(TEXT("the light it is shaded by comes from beyond it: its lit face points away"),
                 FVector::DotProduct(LightWorld.GetSafeNormal(), TrueDirection) > FMath::Cos(FMath::DegreesToRadians(10.0)));
    }
    const double DarkEmission = DiscEmission(Instance, View.Phase);

    // The target, from the same system and no subsystem: the fixture has no
    // procgen id for SetTarget to accept.
    const TOptional<FTargetView> Target = TargetMarker::View(System, FBodyId{ System.Stub.Id, 0 }, Ship, Facing,
                                                             FVector::ZeroVector, ShipFlight::DefaultFloorCm,
                                                             FShipFlightLimits().LinearAcceleration,
                                                             ShipFlight::DefaultHoldSeconds, false);
    if (TestTrue(TEXT("the target resolves in the fixture"), Target.IsSet()))
    {
        // Ship axes are world axes (ADR 0005).
        const double TargetOff = FMath::Acos(FMath::Clamp(FVector::DotProduct(Target->ShipLocalDir, Sphere.Centre.GetSafeNormal()), -1.0, 1.0));
        TestTrue(FString::Printf(TEXT("the bracket's direction is the proxy's, to a pixel (%.4f px off)"), TargetOff / Pixel),
                 TargetOff < Pixel);
        TestTrue(TEXT("and it is dead ahead, where the nose is"), Target->ShipLocalDir.Equals(FVector::ForwardVector, 1e-9));
        TestTrue(TEXT("its angular radius is the proxy's, to a pixel"), FMath::Abs(Target->AngularRadius - View.AngularRadius) < Pixel);
        TestTrue(TEXT("the target is on its night side"), Target->bNightSide);
        TestTrue(TEXT("and the line says so"), TargetMarker::Line(*Target).EndsWith(TEXT("NIGHT SIDE")));
    }

    // The same world, the same distance, sunward of it: full phase.
    {
        const FVector Day(-0.03 * AU, 0.002 * AU, 0.0);
        const FUniversePosition Sunward = Earth + Day;
        Test.Ship->PlaceShip(Sunward, FRotationMatrix::MakeFromX(-Day).ToQuat());
        Test.Frame->SyncToShip();
        Test.Sky->DrawFrom(Sky);
        const FSkyBodyView& Lit = Test.Sky->GetLastFrame().Bodies[EarthIndex];
        TestTrue(TEXT("sunward, nearly full"), Lit.Phase > 0.95);
        TestEqual(TEXT("and resolved alike"), Lit.PointBlend, 0.0);
        const double LitEmission = DiscEmission(Instance, Lit.Phase);
        TestTrue(FString::Printf(TEXT("the night side sends under 2%% of full phase's light (%.5f)"), DarkEmission / LitEmission),
                 LitEmission > 0.0 && DarkEmission < 0.02 * LitEmission);

        const TOptional<FTargetView> DayTarget = TargetMarker::View(System, FBodyId{ System.Stub.Id, 0 }, Sunward, FQuat::Identity,
                                                                    FVector::ZeroVector, ShipFlight::DefaultFloorCm,
                                                                    FShipFlightLimits().LinearAcceleration,
                                                                    ShipFlight::DefaultHoldSeconds, false);
        TestTrue(TEXT("sunward, the line says nothing of a night side"),
                 DayTarget && !DayTarget->bNightSide && !TargetMarker::Line(*DayTarget).EndsWith(TEXT("NIGHT SIDE")));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
