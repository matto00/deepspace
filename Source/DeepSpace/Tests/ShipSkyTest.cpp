#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyStarfield.h"
#include "Tests/SkyTestFixtures.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ShipSkyTestLocal
{
    using namespace SkyTestWorld;

    int32 CountBodyProxies(const AShipSky* Sky)
    {
        TArray<UStaticMeshComponent*> Meshes;
        Sky->GetComponents(Meshes);
        return Meshes.Num() - 1;    // less NeighbourStars, itself a static mesh component
    }

    /** The colour a proxy's material was built with, or magenta if it has
     *  none: RebuildFor is the only place it is written. */
    FLinearColor BuiltColour(const AShipSky* Sky, int32 Index)
    {
        const UStaticMeshComponent* Proxy = Sky->GetProxy(Index);
        UMaterialInstanceDynamic* Instance = Proxy ? Cast<UMaterialInstanceDynamic>(Proxy->GetMaterial(0)) : nullptr;
        return Instance ? Instance->K2_GetVectorParameterValue(SkyMaterial::Colour) : FLinearColor(1.0f, 0.0f, 1.0f);
    }

    /** Where a neighbour really is from Ship: through universe positions,
     *  not through the sky's arithmetic. */
    FVector TrueDirection(const FSkyNeighbour& Neighbour, const FUniversePosition& Star, const FUniversePosition& Ship)
    {
        return ((Star + Neighbour.Direction * Neighbour.Distance) - Ship).GetSafeNormal();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipSkyTest,
    "DeepSpace.Sky.ShipSky",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipSkyTest::RunTest(const FString& Parameters)
{
    using namespace ShipSkyTestLocal;

    // -- The pure half --------------------------------------------------------
    {
        // Exposure, against the engine's own arithmetic rather than this
        // file's: the HDR visualisation reports log2((average / 0.18) /
        // LuminanceMax), LuminanceMax is 1 at the default lens attenuation,
        // auto exposure scales the picture by 2^bias / (average / 0.18), and
        // manual with no physical camera by 2^bias.
        {
            const double Average = 0.3;                                 // cd/m^2, the galley's estimate
            const double SceneEV = FMath::Log2(Average / 0.18);
            const double AutoScale = FMath::Pow(2.0, ShipSky::AutoExposureDefaultBias) / (Average / 0.18);
            const double ManualScale = FMath::Pow(2.0, ShipSky::ManualExposureBias(SceneEV));
            TestTrue(FString::Printf(TEXT("manual shows a %.2f EV scene exactly as auto does (%.4f vs %.4f)"), SceneEV, ManualScale, AutoScale),
                FMath::IsNearlyEqual(ManualScale, AutoScale, 1e-12));
            TestTrue(TEXT("manual exposure at EV 0 is auto's default bias, +1"),
                FMath::IsNearlyEqual(ShipSky::ManualExposureBias(0.0), 1.0, 1e-12));
            TestTrue(TEXT("a scene one stop brighter gets one stop less exposure"),
                FMath::IsNearlyEqual(ShipSky::ManualExposureBias(-1.0) - ShipSky::ManualExposureBias(0.0), 1.0, 1e-12));
            TestEqual(TEXT("the default bias the formula assumes is the engine's"),
                static_cast<double>(FPostProcessSettings().AutoExposureBias), ShipSky::AutoExposureDefaultBias);
        }

        FSkyNeighbour Sun;
        Sun.Luminosity = 1.0;
        Sun.Distance = ShipSky::FaintestFluxSunDistanceLy * UniverseUnits::CmPerLightYear;
        TestTrue(TEXT("a Sun at 70 ly is the faintest star drawn"), FMath::IsNearlyEqual(ShipSky::NeighbourFlux(Sun), 1.0, 1e-9));
        Sun.Distance = 4.0 * UniverseUnits::CmPerLightYear;
        TestTrue(TEXT("a Sun at 4 ly is 310 on the starfield's honest scale, as Alpha Centauri is among the brightest"),
            FMath::IsNearlyEqual(ShipSky::NeighbourFlux(Sun), 309.76, 1e-9));
        FSkyNeighbour Dwarf = Sun;
        Dwarf.Luminosity = 0.0017;
        TestEqual(TEXT("a Proxima is never missing: held at the faintest drawn"), ShipSky::NeighbourFlux(Dwarf), 1.0);
        FSkyNeighbour Giant = Sun;
        Giant.Luminosity = 1.0e6;
        TestEqual(TEXT("and nothing is brighter than the starfield's brightest"), ShipSky::NeighbourFlux(Giant), SkyStarfield::MaxFlux);

        // A point's brightness, against literal CVar values: one function for
        // every point at infinity, compressed flux times the faint end's
        // emission times the scene's unit.
        {
            FScopedCVar Faint(TEXT("ds.Sky.StarfieldFaint"), 0.01f);
            FScopedCVar Radiance(TEXT("ds.Sky.Radiance"), 3.0f);
            {
                FScopedCVar Gamma(TEXT("ds.Sky.FluxGamma"), 1.0f);
                TestTrue(TEXT("honest: flux 4 at Faint 0.01 and Radiance 3 is 0.12"),
                    FMath::IsNearlyEqual(AShipSky::PointStarBrightness(4.0), 0.12f, 1e-6f));
            }
            FScopedCVar Gamma(TEXT("ds.Sky.FluxGamma"), 0.5f);
            TestTrue(TEXT("compressed at gamma 0.5: flux 4 is 0.06"), FMath::IsNearlyEqual(AShipSky::PointStarBrightness(4.0), 0.06f, 1e-6f));
            TestTrue(TEXT("and the faintest star is 0.03"), FMath::IsNearlyEqual(AShipSky::PointStarBrightness(1.0), 0.03f, 1e-6f));
        }

        // A neighbour re-referred from the star to the ship. At the system's
        // edge, 0.25 ly out square to a neighbour 4 ly off, it has moved by
        // atan(0.25 / 4), 3.6 degrees.
        {
            FSkyNeighbour Ahead;
            Ahead.Direction = FVector::ForwardVector;
            Ahead.Distance = 4.0 * UniverseUnits::CmPerLightYear;
            const FVector StarFromShip(0.0, 0.25 * UniverseUnits::CmPerLightYear, 0.0);
            const FSkyNeighbour Seen = ShipSky::NeighbourFromShip(Ahead, StarFromShip);
            TestTrue(TEXT("from the edge, a neighbour is where it is from the ship"),
                Seen.Direction.Equals(FVector(4.0, 0.25, 0.0).GetSafeNormal(), 1e-12));
            TestTrue(TEXT("3.6 degrees off where it is from the star"),
                FMath::IsNearlyEqual(FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Seen.Direction, Ahead.Direction))), 3.576, 1e-3));
            TestTrue(TEXT("and at its distance from the ship"),
                FMath::IsNearlyEqual(Seen.Distance / UniverseUnits::CmPerLightYear, FMath::Sqrt(16.0625), 1e-12));
            const FSkyNeighbour AtStar = ShipSky::NeighbourFromShip(Ahead, FVector::ZeroVector);
            TestTrue(TEXT("at the star, nothing moves"), AtStar.Direction.Equals(Ahead.Direction, 1e-15) && AtStar.Distance == Ahead.Distance);
        }

        const FSkySystem Fixture = SkyTestFixtures::System();
        TestEqual(TEXT("a body by index"), ShipSky::FindBody(Fixture, TEXT("2")), SkyTestFixtures::HomeIndex);
        TestEqual(TEXT("a body by name, spaces and all"), ShipSky::FindBody(Fixture, TEXT("Fixture IIa")), SkyTestFixtures::MoonIndex);
        TestEqual(TEXT("no such index"), ShipSky::FindBody(Fixture, TEXT("9")), INDEX_NONE);
        TestEqual(TEXT("no such name"), ShipSky::FindBody(Fixture, TEXT("Kessa")), INDEX_NONE);

        // Goto: 150 km over the home planet's day side, facing it.
        const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
        const FSkyBody& Star = Fixture.Bodies[SkyTestFixtures::StarIndex];
        const TOptional<FNavPlacement> Orbit = ShipSky::GotoPlacement(Fixture, SkyTestFixtures::HomeIndex, 1.5e7, SkyTestFixtures::Opening());
        if (TestTrue(TEXT("goto places over a body"), Orbit.IsSet()))
        {
            const FVector ToBody = Home.Position - Orbit->Position;
            const FVector Nose = Orbit->Orientation.GetForwardVector();
            TestTrue(TEXT("at its altitude above the surface"), FMath::IsNearlyEqual(ToBody.Size(), Home.Radius + 1.5e7, 1.0));
            TestTrue(TEXT("facing it"), FVector::DotProduct(Nose, ToBody.GetSafeNormal()) > 1.0 - 1e-9);
            TestTrue(TEXT("on its day side: the star behind"), FVector::DotProduct(Nose, (Star.Position - Orbit->Position).GetSafeNormal()) < -0.99);
            TestTrue(TEXT("with the system's up kept up"), Orbit->Orientation.GetUpVector().Z > 0.99);
        }
        const double ThirtyAU = 30.0 * UniverseUnits::CmPerAU;
        const TOptional<FNavPlacement> Far = ShipSky::GotoPlacement(Fixture, SkyTestFixtures::StarIndex, ThirtyAU, SkyTestFixtures::Opening());
        if (TestTrue(TEXT("goto places off a star"), Far.IsSet()))
        {
            TestTrue(TEXT("30 AU from its surface"), FMath::IsNearlyEqual((Star.Position - Far->Position).Size(), Star.Radius + ThirtyAU, 10.0));
            TestTrue(TEXT("on the side the ship was already on"),
                FVector::DotProduct((Far->Position - Star.Position).GetSafeNormal(), (SkyTestFixtures::Opening() - Star.Position).GetSafeNormal()) > 1.0 - 1e-9);
        }
        TestFalse(TEXT("goto refuses a body the system does not have"), ShipSky::GotoPlacement(Fixture, 7, 1.0, SkyTestFixtures::Opening()).IsSet());
    }

    // -- The actor, as the level build places it ------------------------------
    FSkyWorld Test(TEXT("ShipSkyTestWorld"));
    if (!TestNotNull(TEXT("the sky spawns"), Test.Sky) || !TestNotNull(TEXT("with a counter-frame"), Test.Frame)
        || !TestNotNull(TEXT("and a ship"), Test.Ship) || !TestNotNull(TEXT("the engine sphere loads"), Test.Sphere))
    {
        return false;
    }
    AShipSky* Sky = Test.Sky;
    UShipSubsystem* Ship = Test.Ship;

    // It follows the ship, so everything is Movable: a Static child of a
    // movable root never has its world transform updated.
    TestEqual(TEXT("the root is movable"), Sky->GetRootComponent()->Mobility.GetValue(), EComponentMobility::Movable);
    TestEqual(TEXT("the sun is movable"), Sky->GetSun()->Mobility.GetValue(), EComponentMobility::Movable);
    TestEqual(TEXT("the neighbours are movable"), Sky->GetNeighbourStars()->Mobility.GetValue(), EComponentMobility::Movable);
    TestEqual(TEXT("the exposure is movable"), Sky->GetExposure()->Mobility.GetValue(), EComponentMobility::Movable);
    TestTrue(TEXT("the sun casts shadows: the only sunlight inside is the shape of the windows"), Sky->GetSun()->CastShadows != 0);
    TestTrue(TEXT("the exposure is unbound: everywhere, with no volume"), Sky->GetExposure()->bUnbound != 0);
    TestEqual(TEXT("the neighbours carry colour and brightness per instance"),
        Sky->GetNeighbourStars()->NumCustomDataFloats, SkyMaterial::StarfieldCustomData);

    Test.BeginPlay();

    TestEqual(TEXT("at BeginPlay it attaches to the counter-frame"), Sky->GetAttachParentActor(), static_cast<AActor*>(Test.Frame));
    TestTrue(TEXT("with its local space the counter-frame's"),
        Sky->GetRootComponent()->GetRelativeTransform().Equals(FTransform::Identity, 1e-9));
    TestTrue(TEXT("with no player the pixel angle is 90 degrees over 1920"),
        FMath::IsNearlyEqual(Sky->GetPixelAngle(), 2.0 / 1920.0, 1e-15));
    TestTrue(TEXT("the dome is behind every proxy the band can make"), Sky->GetDomeRadius() > FSkyViewParams().FarProxy);

    // Exposure is fixed, and exactly ds.Sky.Exposure.
    {
        const FPostProcessSettings& Settings = Sky->GetExposure()->Settings;
        TestTrue(TEXT("exposure is manual"), Settings.bOverride_AutoExposureMethod && Settings.AutoExposureMethod == AEM_Manual);
        TestTrue(TEXT("with no physical camera in it"),
            Settings.bOverride_AutoExposureApplyPhysicalCameraExposure && !Settings.AutoExposureApplyPhysicalCameraExposure);
        TestTrue(TEXT("at ds.Sky.Exposure"), Settings.bOverride_AutoExposureBias
            && FMath::IsNearlyEqual(Settings.AutoExposureBias, static_cast<float>(ShipSky::ManualExposureBias(CVarFloat(TEXT("ds.Sky.Exposure")))), 1e-5f));
        TestTrue(TEXT("no lens flares"), Settings.bOverride_LensFlareIntensity && Settings.LensFlareIntensity == 0.0f);
        TestTrue(TEXT("local exposure leaves the contrast alone"), Settings.bOverride_LocalExposureHighlightContrastScale
            && Settings.LocalExposureHighlightContrastScale == 1.0f && Settings.LocalExposureShadowContrastScale == 1.0f);

        {
            FScopedCVar EV(TEXT("ds.Sky.Exposure"), 3.0f);
            Sky->SyncToShip();
            TestTrue(TEXT("ds.Sky.Exposure is read at use"),
                FMath::IsNearlyEqual(Settings.AutoExposureBias, static_cast<float>(ShipSky::ManualExposureBias(3.0)), 1e-5f));
            {
                FScopedCVar Mode(TEXT("ds.Sky.ExposureMode"), 0.0f);
                Sky->SyncToShip();
                TestFalse(TEXT("mode 0 hands exposure back to the engine"), Settings.bOverride_AutoExposureMethod != 0);
            }
            {
                // Under manual the picture is scaled by 2^bias; under auto,
                // for a scene at EV x inside its limits, by 2^(bias - x). A
                // scene at ds.Sky.Exposure must look the same in both, or
                // switching to the fallback jumps the picture by stops.
                const float ManualBias = Settings.AutoExposureBias;
                FScopedCVar Mode(TEXT("ds.Sky.ExposureMode"), 2.0f);
                FScopedCVar Range(TEXT("ds.Sky.ExposureRange"), 1.0f);
                Sky->SyncToShip();
                TestTrue(TEXT("mode 2 is auto, held a range either side"), Settings.AutoExposureMethod == AEM_Histogram
                    && Settings.AutoExposureMinBrightness == 2.0f && Settings.AutoExposureMaxBrightness == 4.0f);
                const float Centre = 0.5f * (Settings.AutoExposureMinBrightness + Settings.AutoExposureMaxBrightness);
                TestTrue(FString::Printf(TEXT("modes 1 and 2 centre on the same EV (manual 2^%.3f, auto 2^%.3f)"),
                    ManualBias, Settings.AutoExposureBias - Centre),
                    Settings.bOverride_AutoExposureBias && FMath::IsNearlyEqual(Settings.AutoExposureBias - Centre, ManualBias, 1e-5f));
            }
        }
        Sky->SyncToShip();
    }

    TestNotNull(TEXT("ds.Sky.Goto is a console command"), IConsoleManager::Get().FindConsoleObject(TEXT("ds.Sky.Goto")));

    // -- A fixture drawn from the opening --------------------------------------
    FSkySystem Fixture = SkyTestFixtures::System();
    {
        // Two neighbours, nearest first as LocalSystem gives them: a Sun at
        // 4 ly and a red dwarf at 9.
        FSkyNeighbour& Near = Fixture.Neighbours.AddDefaulted_GetRef();
        Near.SystemId = TEXT("Near");
        Near.Direction = FVector(0.3, 0.9, 0.1).GetSafeNormal();
        Near.Distance = 4.0 * UniverseUnits::CmPerLightYear;
        Near.Luminosity = 1.0;
        Near.TemperatureK = 5800.0;
        FSkyNeighbour& Dim = Fixture.Neighbours.AddDefaulted_GetRef();
        Dim.SystemId = TEXT("Dim");
        Dim.Direction = FVector(-0.5, 0.2, -0.8).GetSafeNormal();
        Dim.Distance = 9.0 * UniverseUnits::CmPerLightYear;
        Dim.Luminosity = 0.02;
        Dim.TemperatureK = 3300.0;
    }

    Ship->PlaceShip(SkyTestFixtures::Opening(), FQuat::Identity);
    Test.Frame->SyncToShip();
    Sky->RebuildFor(Fixture);
    TestEqual(TEXT("one proxy per body"), Sky->GetProxyCount(), Fixture.Bodies.Num());
    Sky->DrawFrom(Fixture);

    {
        const UStaticMeshComponent* Home = Sky->GetProxy(SkyTestFixtures::HomeIndex);
        if (TestNotNull(TEXT("the home planet has a proxy"), Home))
        {
            TestEqual(TEXT("proxies hang off the sky"), Home->GetAttachParent(), Sky->GetRootComponent());
            TestEqual(TEXT("proxies are movable"), Home->Mobility.GetValue(), EComponentMobility::Movable);
            TestEqual(TEXT("proxies touch nothing"), Home->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
            TestFalse(TEXT("and shadow nothing"), Home->CastShadow != 0);
            TestTrue(TEXT("and are never distance-culled"), Home->bNeverDistanceCull != 0);
            TestFalse(TEXT("and no reflection capture sees them"), Home->bVisibleInReflectionCaptures != 0);
            TestFalse(TEXT("and no sky light captures them"), Home->bVisibleInRealTimeSkyCaptures != 0);

            // The opening shot, measured where the pilot's eyes are rather
            // than at the ship's origin: an Earth 40,000 km off is 18.3
            // degrees, and the eye's 16 m costs it 0.03%.
            const FSkyBody& True = Fixture.Bodies[SkyTestFixtures::HomeIndex];
            const FUniversePosition Eye = Ship->GetFlightState().WorldToUniverse(PilotEye);
            const double Expected = 2.0 * FMath::Asin(True.Radius / (True.Position - Eye).Size());
            const double Seen = Subtense(Drawn(Home, Test.Sphere), PilotEye);
            TestTrue(FString::Printf(TEXT("the home planet subtends %.3f deg from the pilot seat (true %.3f)"),
                FMath::RadiansToDegrees(Seen), FMath::RadiansToDegrees(Expected)),
                FMath::Abs(Seen / Expected - 1.0) < 1e-3);
            TestTrue(TEXT("which is the 18.3 degrees the shot was framed for"), FMath::Abs(FMath::RadiansToDegrees(Expected) - 18.35) < 0.05);

            // Its material gets the projection's numbers through the
            // contract's names; a misspelt name would leave the default.
            const FSkyBodyView& View = Sky->GetLastFrame().Bodies[SkyTestFixtures::HomeIndex];
            UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Home->GetMaterial(0));
            if (TestNotNull(TEXT("the planet has its own material instance"), Instance))
            {
                const float Radiance = CVarFloat(TEXT("ds.Sky.Radiance"));
                TestTrue(TEXT("its brightness, as given, in the scene's unit"),
                    FMath::IsNearlyEqual(Instance->K2_GetScalarParameterValue(SkyMaterial::Brightness), static_cast<float>(View.Brightness) * Radiance, 1e-4f));
                TestEqual(TEXT("resolved: a disc"), Instance->K2_GetScalarParameterValue(SkyMaterial::PointBlend), 0.0f);
                TestEqual(TEXT("its mottle"), Instance->K2_GetScalarParameterValue(SkyMaterial::Mottle), CVarFloat(TEXT("ds.Sky.Mottle")));
                TestTrue(TEXT("its colour"), Instance->K2_GetVectorParameterValue(SkyMaterial::Colour).Equals(True.Colour));
                TestTrue(TEXT("its rim"), Instance->K2_GetVectorParameterValue(SkyMaterial::Rim).Equals(True.Rim));
            }
        }

        // A point stays a point: the giant at 5 AU is drawn at the minimum,
        // and the minimum is read at use.
        {
            FScopedCVar Pixels(TEXT("ds.Sky.PointPixels"), 3.0f);
            Sky->DrawFrom(Fixture);
            const UStaticMeshComponent* Giant = Sky->GetProxy(SkyTestFixtures::GiantIndex);
            const double Seen = Subtense(Drawn(Giant, Test.Sphere), FVector::ZeroVector);
            TestTrue(TEXT("a point is drawn ds.Sky.PointPixels across"), FMath::IsNearlyEqual(Seen, 3.0 * Sky->GetPixelAngle(), 1e-3 * Seen));
            UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Giant->GetMaterial(0));
            TestTrue(TEXT("and as a point"), Instance && Instance->K2_GetScalarParameterValue(SkyMaterial::PointBlend) == 1.0f);
        }
    }

    // Turned, the universe turns: the proxies, the sun and the planets'
    // light all swing with the counter-frame. The ship itself is never
    // written.
    {
        const FQuat Attitude(FVector(0.2, -0.5, 1.0).GetSafeNormal(), 0.9);
        Ship->PlaceShip(SkyTestFixtures::Opening(), Attitude);
        Test.Frame->SyncToShip();

        const FShipFlightState& Flight = Ship->GetFlightState();
        const FUniversePosition Position = Flight.GetUniversePosition();
        const FQuat Orientation = Flight.GetUniverseOrientation();
        const FVector Velocity = Flight.GetVelocity();
        const FVector Spin = Flight.GetAngularVelocity();
        const double Charge = Flight.GetJumpCharge();

        FScopedCVar Lux(TEXT("ds.Sky.SunLux"), 123.0f);
        Sky->DrawFrom(Fixture);

        TestTrue(TEXT("drawing never moves the ship"), Flight.GetUniversePosition() == Position);
        TestTrue(TEXT("or turns it"), Flight.GetUniverseOrientation() == Orientation);
        TestTrue(TEXT("or changes its motion"), Flight.GetVelocity() == Velocity && Flight.GetAngularVelocity() == Spin);
        TestTrue(TEXT("or its charge"), Flight.GetJumpCharge() == Charge);

        const FSkyFrame& Frame = Sky->GetLastFrame();
        const FQuat Counter = Ship->GetCounterFrameTransform().GetRotation();
        TestTrue(TEXT("the sun shines from the star, turned with the counter-frame"),
            Sky->GetSun()->GetForwardVector().Equals(Counter.RotateVector(-Frame.SunDirection), 1e-5));
        TestTrue(TEXT("the sun is up"), Sky->GetSun()->IsVisible());
        TestTrue(TEXT("its intensity is ds.Sky.SunLux times the compressed irradiance, read at use"),
            FMath::IsNearlyEqual(Sky->GetSun()->Intensity, 123.0f * static_cast<float>(Frame.SunIrradiance * Frame.SunVisibleFraction), 1e-3f));
        TestTrue(TEXT("a Sun at 1 AU lights the deck at about the reference"), FMath::IsNearlyEqual(Frame.SunIrradiance, 1.0, 1e-3));
        TestTrue(TEXT("its colour is the star's"), Sky->GetSun()->GetLightColor().Equals(Fixture.Bodies[SkyTestFixtures::StarIndex].Colour, 0.01f));
        TestTrue(TEXT("its penumbra is the star's true width"),
            FMath::IsNearlyEqual(Sky->GetSun()->LightSourceAngle, static_cast<float>(FMath::RadiansToDegrees(2.0 * Frame.Bodies[SkyTestFixtures::StarIndex].AngularRadius)), 1e-4f));

        const UStaticMeshComponent* Home = Sky->GetProxy(SkyTestFixtures::HomeIndex);
        const FVector Seen = Drawn(Home, Test.Sphere).Centre.GetSafeNormal();
        TestTrue(TEXT("the planet is where the counter-frame turns it"),
            Seen.Equals(Counter.RotateVector(Frame.Bodies[SkyTestFixtures::HomeIndex].Direction), 1e-6));

        UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Home->GetMaterial(0));
        if (Instance)
        {
            const FLinearColor Light = Instance->K2_GetVectorParameterValue(SkyMaterial::LightDirection);
            TestTrue(TEXT("its light comes from its star, in world space"),
                FVector(Light.R, Light.G, Light.B).Equals(Counter.RotateVector(Frame.Bodies[SkyTestFixtures::HomeIndex].LightDirection), 1e-5));
        }
        const UMaterialInstanceDynamic* StarInstance = Cast<UMaterialInstanceDynamic>(Sky->GetProxy(SkyTestFixtures::StarIndex)->GetMaterial(0));
        TestTrue(TEXT("the star's material is the star's"), StarInstance && StarInstance->Parent == Sky->StarMaterial);
    }

    // The neighbours, on the dome, nearest first, drawn like background stars.
    {
        UInstancedStaticMeshComponent* Neighbours = Sky->GetNeighbourStars();
        TestFalse(TEXT("no reflection capture sees the neighbours"), Neighbours->bVisibleInReflectionCaptures != 0);
        TestFalse(TEXT("and no sky light captures them"), Neighbours->bVisibleInRealTimeSkyCaptures != 0);

        FScopedCVar Faint(TEXT("ds.Sky.StarfieldFaint"), 0.01f);
        FScopedCVar Radiance(TEXT("ds.Sky.Radiance"), 3.0f);
        FScopedCVar Gamma(TEXT("ds.Sky.FluxGamma"), 0.5f);
        Sky->DrawFrom(Fixture);
        const FUniversePosition ShipAt = Ship->GetFlightState().GetUniversePosition();

        if (TestEqual(TEXT("one point per neighbour"), Neighbours->GetInstanceCount(), 2))
        {
            for (int32 Index = 0; Index < 2; ++Index)
            {
                const FSkyNeighbour& Neighbour = Fixture.Neighbours[Index];
                FTransform Instance;
                Neighbours->GetInstanceTransform(Index, Instance, false);
                TestTrue(TEXT("a neighbour is in its true direction from the ship"),
                    Instance.GetLocation().GetSafeNormal().Equals(TrueDirection(Neighbour, SkyTestFixtures::StarPosition(), ShipAt), 1e-9));
                TestTrue(TEXT("on the dome"), FMath::IsNearlyEqual(Instance.GetLocation().Size(), AShipSky::DomeRadius, 1.0));
                const double Across = Instance.GetScale3D().X * 2.0 * Test.Sphere->GetBoundingBox().GetExtent().GetMax();
                TestTrue(TEXT("a point ds.Sky.PointPixels across"),
                    FMath::IsNearlyEqual(Across, ShipSky::PointDiameter(AShipSky::DomeRadius, Sky->GetPixelAngle(), CVarFloat(TEXT("ds.Sky.PointPixels"))), 1.0));

                const float* Data = &Neighbours->PerInstanceSMCustomData[Index * SkyMaterial::StarfieldCustomData];
                const FLinearColor Colour = SkyColour::Blackbody(Fixture.Neighbours[Index].TemperatureK);
                TestTrue(TEXT("coloured by its temperature"), FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataRed], Colour.R)
                    && FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataGreen], Colour.G) && FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataBlue], Colour.B));
                // Written out rather than asked of the sky: the honest flux,
                // L (70.4 ly / D)^2 in [1, 400], square-rooted, times 0.01
                // and 3.
                const double Ly = ((SkyTestFixtures::StarPosition() + Neighbour.Direction * Neighbour.Distance) - ShipAt).Size()
                    / UniverseUnits::CmPerLightYear;
                const double Flux = FMath::Clamp(Neighbour.Luminosity * FMath::Square(70.4 / Ly), 1.0, 400.0);
                TestTrue(FString::Printf(TEXT("as bright as a background star of flux %.2f"), Flux),
                    FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataBrightness], static_cast<float>(FMath::Sqrt(Flux) * 0.03), 1e-6f));
            }
            const float* Data = Neighbours->PerInstanceSMCustomData.GetData();
            TestTrue(TEXT("a Sun at 4 ly outshines a red dwarf at 9"),
                Data[SkyMaterial::CustomDataBrightness] > Data[SkyMaterial::StarfieldCustomData + SkyMaterial::CustomDataBrightness]);
        }
    }

    // Parked in a planet's shadow the deck goes dark: the sun stays where it
    // is and gives nothing. Out of it, the light is back, with no memory of
    // having gone.
    {
        const FSkyBody& Star = Fixture.Bodies[SkyTestFixtures::StarIndex];
        const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
        const FVector Shadow = (Home.Position - Star.Position).GetSafeNormal();
        FScopedCVar Lux(TEXT("ds.Sky.SunLux"), 50.0f);

        Ship->PlaceShip(Home.Position + Shadow * (Home.Radius + 1.0e10), FQuat::Identity);
        Test.Frame->SyncToShip();
        Sky->DrawFrom(Fixture);
        TestEqual(TEXT("behind the planet none of the sun shows"), Sky->GetLastFrame().SunVisibleFraction, 0.0);
        TestTrue(TEXT("and the sunlight on the deck is gone"), Sky->GetSun()->Intensity == 0.0f && Sky->GetLastFrame().SunIrradiance > 0.5);

        Ship->PlaceShip(Home.Position + Shadow * -(Home.Radius + 1.0e10), FQuat::Identity);
        Test.Frame->SyncToShip();
        Sky->DrawFrom(Fixture);
        TestTrue(TEXT("on its day side the sun is whole again, and lights the deck"),
            Sky->GetLastFrame().SunVisibleFraction == 1.0 && Sky->GetSun()->Intensity > 40.0f);
    }

    // The veil: the room's light and the reflection's strength, into MPC_Sky
    // every frame, asked of the ship and stored nowhere. With the lights off
    // the glass reflects nothing and every star shows.
    {
        UMaterialParameterCollectionInstance* Collection = Sky->SkyParameters
            ? Test.World->GetParameterCollectionInstance(Sky->SkyParameters) : nullptr;
        if (TestNotNull(TEXT("the sky writes into MPC_Sky, as place_sky assigns it"), Collection))
        {
            const auto Read = [Collection](FName Name)
            {
                float Value = -1.0f;
                Collection->GetScalarParameterValue(Name, Value);
                return Value;
            };
            FScopedCVar Veil(TEXT("ds.Sky.Veil"), 0.4f);
            Ship->SetLightsOn(true);
            Sky->SyncToShip();
            const float Fed = Ship->GetConsumerSatisfaction(ShipPower::Lights);
            TestEqual(TEXT("lit, the glass reflects the room as brightly as the lights are fed"), Read(SkyMaterial::InteriorLight), Fed);
            TestEqual(TEXT("at ds.Sky.Veil, read at use"), Read(SkyMaterial::Veil), 0.4f);

            // Part-starved, the lights dim, and the reflection follows them
            // all the way down rather than only at the ends. A module drawing
            // most of the reactor off the top leaves the lights short but not
            // dark: a weight of 0 would take them out of the split entirely,
            // and a satisfaction of exactly 0 cannot tell following from any
            // curve that merely shares the endpoints.
            UShipModuleDataAsset* Hog = NewObject<UShipModuleDataAsset>();
            Hog->ModuleId = TEXT("Test.VeilHog");
            Hog->PowerDraw = 0.6f * Ship->GetReactorOutput();
            TestTrue(TEXT("a heavy module installs"), Ship->InstallModule(Hog));
            Ship->Tick(0.016f);
            Sky->SyncToShip();
            const float Starved = Ship->GetConsumerSatisfaction(ShipPower::Lights);
            TestTrue(FString::Printf(TEXT("a heavy draw dims the lights part way (%.3f from %.3f)"), Starved, Fed),
                Starved > 0.1f && Starved < 0.9f * Fed);
            TestEqual(TEXT("and the reflection dims exactly with them"), Read(SkyMaterial::InteriorLight), Starved);
            Ship->RemoveModule(Hog);
            Ship->Tick(0.016f);

            Ship->SetLightsOn(false);
            Ship->Tick(0.016f);
            Sky->SyncToShip();
            TestEqual(TEXT("lights off, the glass reflects nothing"), Read(SkyMaterial::InteriorLight), 0.0f);
            Ship->SetLightsOn(true);
            Ship->Tick(0.016f);
            Sky->SyncToShip();
            TestEqual(TEXT("and lights on, the room is back in the glass"), Read(SkyMaterial::InteriorLight),
                Ship->GetConsumerSatisfaction(ShipPower::Lights));
        }
    }
    Ship->PlaceShip(SkyTestFixtures::Opening(), FQuat::Identity);
    Test.Frame->SyncToShip();

    // A different system replaces the proxies rather than adding to them.
    {
        const TArray<TWeakObjectPtr<UStaticMeshComponent>> Old = { Sky->GetProxy(0), Sky->GetProxy(1) };
        FSkySystem Smaller = Fixture;
        Smaller.Bodies.SetNum(3);
        Sky->RebuildFor(Smaller);
        TestEqual(TEXT("a rebuild for three bodies makes three"), Sky->GetProxyCount(), 3);
        TestEqual(TEXT("and leaves no others behind"), CountBodyProxies(Sky), 3);
        TestTrue(TEXT("the old proxies are gone"), !Old[0].IsValid() || !Old[0]->IsRegistered());

        // And a system that no longer matches the proxies is rebuilt for,
        // never drawn into the wrong ones.
        Sky->DrawFrom(Fixture);
        TestEqual(TEXT("drawing a system of another size rebuilds for it"), Sky->GetProxyCount(), Fixture.Bodies.Num());
        TestEqual(TEXT("still without leaking"), CountBodyProxies(Sky), Fixture.Bodies.Num());
    }

    // Out at the system's edge, where the drive carries a ship leaving the
    // planets, a neighbour is degrees off where it is from the star.
    {
        const FSkyNeighbour& Near = Fixture.Neighbours[0];
        const FVector Square = FVector::CrossProduct(Near.Direction, FVector::UpVector).GetSafeNormal();
        const FUniversePosition Edge = SkyTestFixtures::StarPosition() + Square * (0.24 * UniverseUnits::CmPerLightYear);
        Ship->PlaceShip(Edge, FQuat::Identity);
        Test.Frame->SyncToShip();
        Sky->DrawFrom(Fixture);

        FTransform Instance;
        Sky->GetNeighbourStars()->GetInstanceTransform(0, Instance, false);
        const FVector Seen = Instance.GetLocation().GetSafeNormal();
        TestTrue(TEXT("from the edge, the nearest neighbour is where it is from the ship"),
            Seen.Equals(TrueDirection(Near, SkyTestFixtures::StarPosition(), Edge), 1e-9));
        TestTrue(FString::Printf(TEXT("which is %.2f degrees from where it is from the star"),
            FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Seen, Near.Direction)))),
            FVector::DotProduct(Seen, Near.Direction) < FMath::Cos(FMath::DegreesToRadians(3.0)));
    }

    // The cache and transit, through SyncTo: the answers LocalSystem's
    // null-world branch never gives -- a serial that moves, a ship between
    // stars -- given by hand.
    Ship->PlaceShip(SkyTestFixtures::Opening(), FQuat::Identity);
    Test.Frame->SyncToShip();
    {
        using namespace SkyTestFixtures;
        Sky->SyncTo(Fixture, 10, false);
        TestEqual(TEXT("synced: one proxy per body"), Sky->GetProxyCount(), Fixture.Bodies.Num());

        FSkySystem Recoloured = Fixture;
        Recoloured.Bodies[HomeIndex].Colour = FLinearColor(0.8f, 0.2f, 0.1f);
        Sky->SyncTo(Recoloured, 11, false);
        TestTrue(TEXT("a serial that moves rebuilds, though the name, the star and the size are the same"),
            BuiltColour(Sky, HomeIndex).Equals(Recoloured.Bodies[HomeIndex].Colour));

        FSkySystem Renamed = Recoloured;
        Renamed.SystemId = TEXT("Elsewhere");
        Renamed.Bodies[HomeIndex].Colour = FLinearColor(0.1f, 0.7f, 0.2f);
        Sky->SyncTo(Renamed, 11, false);
        TestTrue(TEXT("another system of the same size is rebuilt for with no serial: by its name"),
            BuiltColour(Sky, HomeIndex).Equals(Renamed.Bodies[HomeIndex].Colour));

        FSkySystem Moved = Renamed;
        for (FSkyBody& Body : Moved.Bodies)
        {
            Body.Position += FVector(3.0e13, 0.0, 0.0);
        }
        Moved.Bodies[HomeIndex].Colour = FLinearColor(0.3f, 0.3f, 0.9f);
        Sky->SyncTo(Moved, 11, false);
        TestTrue(TEXT("and by where its star is, a name being no identity"),
            BuiltColour(Sky, HomeIndex).Equals(Moved.Bodies[HomeIndex].Colour));

        // Between stars, nothing of the system is drawn -- and nothing is
        // rebuilt, since the serial has not moved.
        const TWeakObjectPtr<UStaticMeshComponent> Before = Sky->GetProxy(HomeIndex);
        TestTrue(TEXT("before transit the proxies are shown"), AllProxies(Sky, true));
        TestTrue(TEXT("and the sun is up"), Sky->GetSun()->IsVisible());
        TestTrue(TEXT("and the neighbours are shown"), Sky->GetNeighbourStars()->IsVisible());
        Sky->SyncTo(Moved, 11, true);
        TestTrue(TEXT("in transit every proxy is hidden"), AllProxies(Sky, false));
        TestFalse(TEXT("and the sun is down"), Sky->GetSun()->IsVisible());
        TestFalse(TEXT("and the neighbours are hidden"), Sky->GetNeighbourStars()->IsVisible());
        TestTrue(TEXT("and nothing was rebuilt"), Before.Get() == Sky->GetProxy(HomeIndex));

        Sky->SyncTo(Moved, 11, false);
        TestTrue(TEXT("out of transit, the same proxies are shown again"), AllProxies(Sky, true) && Before.Get() == Sky->GetProxy(HomeIndex));
        TestTrue(TEXT("and the sun is up again"), Sky->GetSun()->IsVisible());
        TestTrue(TEXT("and the neighbours are shown again"), Sky->GetNeighbourStars()->IsVisible());

        // Arrival: transit ends in the same tick the serial moves, into
        // another system.
        Sky->SyncTo(Moved, 11, true);
        Sky->SyncTo(Fixture, 12, false);
        TestTrue(TEXT("arriving rebuilds for the new system"), BuiltColour(Sky, HomeIndex).Equals(Fixture.Bodies[HomeIndex].Colour));
        TestTrue(TEXT("with its proxies shown"), AllProxies(Sky, true));
        TestTrue(TEXT("its sun up"), Sky->GetSun()->IsVisible());
        TestTrue(TEXT("and its neighbours shown"), Sky->GetNeighbourStars()->IsVisible() && Sky->GetNeighbourStars()->GetInstanceCount() == 2);

        // An empty system -- what LocalSystem answers with no universe, or
        // for a position no system reaches -- draws nothing at all.
        Sky->SyncTo(FSkySystem(), 13, false);
        TestEqual(TEXT("an empty system: no bodies"), Sky->GetProxyCount(), 0);
        TestEqual(TEXT("none left behind"), CountBodyProxies(Sky), 0);
        TestFalse(TEXT("no sun"), Sky->GetSun()->IsVisible());
        TestEqual(TEXT("and no neighbours"), Sky->GetNeighbourStars()->GetInstanceCount(), 0);
    }

    // ds.Sky.Goto, given LocalSystem's answers.
    {
        FOutputDeviceNull Quiet;
        const TArray<FString> ToMoon = { TEXT("Fixture"), TEXT("IIa"), TEXT("150") };
        const FUniversePosition Before = Ship->GetFlightState().GetUniversePosition();
        AShipSky::Goto(*Ship, Fixture, /*bInTransit*/ true, ToMoon, Quiet);
        TestTrue(TEXT("goto refuses between stars"), Ship->GetFlightState().GetUniversePosition() == Before);
        AShipSky::Goto(*Ship, FSkySystem(), false, TArray<FString>{ TEXT("0"), TEXT("150") }, Quiet);
        TestTrue(TEXT("goto in an empty system does nothing"), Ship->GetFlightState().GetUniversePosition() == Before);
        AShipSky::Goto(*Ship, Fixture, false, ToMoon, Quiet);
        const FSkyBody& Moon = Fixture.Bodies[SkyTestFixtures::MoonIndex];
        TestTrue(TEXT("goto joins a name the console split at its space, and goes 150 km over it"),
            FMath::IsNearlyEqual(Moon.Position.DistanceTo(Ship->GetFlightState().GetUniversePosition()) - Moon.Radius, 150.0 * UniverseUnits::CmPerKm, 10.0));
    }

    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipSkyLiveTest,
    "DeepSpace.Sky.LiveShipSky",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The sky in a world where LocalSystem answers for real: the live branch
 * (GetSystemAt the ship's position through FromSystem, the jump serial and
 * transit from UShipSubsystem) and the opening placement in
 * UShipSubsystem::OnWorldBeginPlay. Everything here is asked of the
 * subsystems, never of a fixture, so it goes green only once both are in the
 * tree.
 */
bool FShipSkyLiveTest::RunTest(const FString& Parameters)
{
    using namespace ShipSkyTestLocal;

    FSkyWorld Test(TEXT("ShipSkyLiveTestWorld"));
    if (!TestNotNull(TEXT("the sky spawns"), Test.Sky) || !TestNotNull(TEXT("with a ship"), Test.Ship)
        || !TestNotNull(TEXT("and a universe"), Test.Universe))
    {
        return false;
    }
    Test.BeginPlay();

    AShipSky* Sky = Test.Sky;
    const FShipFlightState& Flight = Test.Ship->GetFlightState();
    const TOptional<FStarSystem> Here = Test.Universe->GetSystemAt(Flight.GetUniversePosition());
    if (!TestTrue(TEXT("the opening placement puts the ship in its start system"), Here.IsSet()))
    {
        return false;
    }
    TestTrue(TEXT("which is the start system"), Here->Stub.Id == Test.Universe->GetStartSystem());

    Test.Frame->SyncToShip();
    Sky->SyncToShip();
    TestEqual(TEXT("one proxy for the star and each planet of the system the ship is in"), Sky->GetProxyCount(), 1 + Here->Planets.Num());

    // The opening shot: the largest planet, measured from the pilot seat,
    // at the angle the real planet subtends from there.
    int32 Largest = INDEX_NONE;
    for (int32 Index = 0; Index < Here->Planets.Num(); ++Index)
    {
        if (Largest == INDEX_NONE || Here->Planets[Index].RadiusEarth > Here->Planets[Largest].RadiusEarth)
        {
            Largest = Index;
        }
    }
    if (TestTrue(TEXT("home has a planet"), Largest != INDEX_NONE) && Sky->GetProxy(1 + Largest))
    {
        const double Radius = Here->Planets[Largest].RadiusEarth * UniverseUnits::CmPerEarthRadius;
        const FUniversePosition Eye = Flight.WorldToUniverse(PilotEye);
        const double Expected = 2.0 * FMath::Asin(Radius / (Here->PlanetPosition(Largest) - Eye).Size());
        const double Seen = Subtense(Drawn(Sky->GetProxy(1 + Largest), Test.Sphere), PilotEye);
        TestTrue(FString::Printf(TEXT("the opening planet subtends %.3f deg from the pilot seat (true %.3f)"),
            FMath::RadiansToDegrees(Seen), FMath::RadiansToDegrees(Expected)), FMath::Abs(Seen / Expected - 1.0) < 1e-3);
        TestTrue(TEXT("and it is dead ahead"), (Drawn(Sky->GetProxy(1 + Largest), Test.Sphere).Centre - PilotEye).GetSafeNormal().X > 0.99);
    }
    TestTrue(TEXT("the sun is up"), Sky->GetSun()->IsVisible());
    TestTrue(TEXT("off to starboard"), Sky->GetSun()->GetForwardVector().Y < -0.99);

    // Procgen's one visible change, moved here (plan conflict 5): the real
    // neighbours are in the sky, and the nearest is instance 0.
    const TArray<FStarSystemStub> Stubs = Test.Universe->GetSystemsNear(Flight.GetUniversePosition(), 20.0 * UniverseUnits::CmPerLightYear);
    const FStarSystemStub* Nearest = Stubs.FindByPredicate([&Here](const FStarSystemStub& Stub) { return Stub.Id != Here->Stub.Id; });
    if (TestNotNull(TEXT("home has a neighbour within 20 ly"), Nearest)
        && TestTrue(TEXT("the neighbours are drawn"), Sky->GetNeighbourStars()->GetInstanceCount() > 0))
    {
        FTransform Instance;
        Sky->GetNeighbourStars()->GetInstanceTransform(0, Instance, false);
        // From the ship, not the star: at the opening, a few AU out, the
        // two differ by about 1e-5 rad.
        const FVector Toward = (Nearest->Position - Flight.GetUniversePosition()).GetSafeNormal();
        TestTrue(TEXT("NeighbourStars instance 0 points at the nearest stub, from the ship"), Instance.GetLocation().GetSafeNormal().Equals(Toward, 1e-9));
    }

    // ds.Sky.Goto, through the console, onto the system LocalSystem answers.
    {
        FOutputDeviceNull Quiet;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Sky.Goto 1 150"), Quiet, Test.World);
        const FSkySystem System = LocalSystem::Current(Test.World);
        if (TestTrue(TEXT("goto lands in the same system"), System.Bodies.Num() > 1))
        {
            const double Altitude = System.Bodies[1].Position.DistanceTo(Flight.GetUniversePosition()) - System.Bodies[1].Radius;
            TestTrue(TEXT("150 km over planet 1"), FMath::IsNearlyEqual(Altitude, 150.0 * UniverseUnits::CmPerKm, 10.0));
        }
    }
    return true;
}

#endif
