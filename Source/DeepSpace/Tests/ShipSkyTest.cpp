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
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyStarfield.h"
#include "Tests/SkyTestFixtures.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ShipSkyTestLocal
{
    /** Where the pilot's eyes are, ship space: the port seat at the helm
     *  (sky spec, DeepSpace.Sky.ShipSky). The ship is the world origin, so
     *  this is also world space. */
    const FVector PilotEye(1585.0, -70.0, 170.0);

    /** A game world with its subsystems -- the universe's and the ship's --
     *  a counter-frame and a sky, both spawned before play begins, as the
     *  level build places them. */
    struct FSkyWorld
    {
        UWorld* World = nullptr;
        UShipSubsystem* Ship = nullptr;
        UUniverseSubsystem* Universe = nullptr;
        AShipCounterFrame* Frame = nullptr;
        AShipSky* Sky = nullptr;
        UStaticMesh* Sphere = nullptr;

        explicit FSkyWorld(const TCHAR* Name)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, Name);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
            Ship = World->GetSubsystem<UShipSubsystem>();
            Universe = World->GetSubsystem<UUniverseSubsystem>();

            Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
            Frame = World->SpawnActor<AShipCounterFrame>();
            Sky = World->SpawnActor<AShipSky>();
            if (Frame)
            {
                Frame->GetDistantStars()->SetStaticMesh(Sphere);
                Frame->GetNearStars()->SetStaticMesh(Sphere);
                Frame->DistantStarCount = 8;
                Frame->NearStarCount = 8;
            }
            if (Sky)
            {
                // What place_sky assigns, and all it assigns.
                Sky->BodyMesh = Sphere;
                Sky->BodyMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::BodyPath);
                Sky->StarMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarPath);
                Sky->PointStarMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarfieldPath);
            }
        }

        /** The subsystems' OnWorldBeginPlay -- where the opening placement
         *  happens -- and then every actor's BeginPlay. A test world has no
         *  game mode, and UWorld::BeginPlay reaches actors only through one,
         *  so the second half is the call its game state would make. */
        void BeginPlay()
        {
            World->InitializeActorsForPlay(FURL());
            World->BeginPlay();
            World->GetWorldSettings()->NotifyBeginPlay();
        }

        ~FSkyWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };

    /** A console variable set for one scope and put back after, so one test
     *  cannot tune another. */
    struct FScopedCVar
    {
        IConsoleVariable* Variable;
        FString Previous;

        FScopedCVar(const TCHAR* Name, float Value)
            : Variable(IConsoleManager::Get().FindConsoleVariable(Name))
        {
            check(Variable);
            Previous = Variable->GetString();
            Variable->Set(Value, ECVF_SetByCode);
        }

        ~FScopedCVar()
        {
            Variable->Set(*Previous, ECVF_SetByCode);
        }
    };

    float CVarFloat(const TCHAR* Name)
    {
        return IConsoleManager::Get().FindConsoleVariable(Name)->GetFloat();
    }

    /** A proxy's true sphere in the world: centre and radius, measured from
     *  the mesh's bounds, as the actor must place it. */
    struct FDrawnSphere
    {
        FVector Centre = FVector::ZeroVector;
        double Radius = 0.0;
    };

    FDrawnSphere Drawn(const UStaticMeshComponent* Proxy, const UStaticMesh* Mesh)
    {
        const FBox Box = Mesh->GetBoundingBox();
        const FTransform& Transform = Proxy->GetComponentTransform();
        return { Transform.TransformPosition(Box.GetCenter()), Box.GetExtent().GetMax() * Transform.GetScale3D().X };
    }

    double Subtense(const FDrawnSphere& Sphere, const FVector& Eye)
    {
        return 2.0 * FMath::Asin(Sphere.Radius / (Sphere.Centre - Eye).Size());
    }

    /** A position between stars in this universe, checked rather than
     *  assumed: at 0.004 systems per cubic light year, a 0.25 ly sphere
     *  holds one about once in three thousand. */
    FUniversePosition BetweenStars(const UUniverseSubsystem* Universe)
    {
        for (int32 Step = 0; Step < 64; ++Step)
        {
            const FUniversePosition Where = FUniversePosition()
                + FVector(1.7 + 0.37 * Step, -2.3, 0.9) * UniverseUnits::CmPerLightYear;
            if (!Universe || !Universe->GetSystemAt(Where))
            {
                return Where;
            }
        }
        return FUniversePosition();
    }

    int32 CountBodyProxies(const AShipSky* Sky)
    {
        TArray<UStaticMeshComponent*> Meshes;
        Sky->GetComponents(Meshes);
        return Meshes.Num() - 1;    // less NeighbourStars, itself a static mesh component
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
        TestTrue(TEXT("manual exposure at EV 0 is auto's middle grey at its +1 bias"),
            FMath::IsNearlyEqual(ShipSky::ManualExposureBias(0.0), FMath::Log2(0.36), 1e-12));
        TestTrue(TEXT("a scene one stop brighter gets one stop less exposure"),
            FMath::IsNearlyEqual(ShipSky::ManualExposureBias(-1.0) - ShipSky::ManualExposureBias(0.0), 1.0, 1e-12));

        FSkyNeighbour Sun;
        Sun.Luminosity = 1.0;
        Sun.Distance = ShipSky::FaintestFluxSunDistanceLy * UniverseUnits::CmPerLightYear;
        TestTrue(TEXT("a Sun at 70 ly is the faintest star drawn"), FMath::IsNearlyEqual(ShipSky::NeighbourFlux(Sun, 1.0), 1.0, 1e-9));
        Sun.Distance = 4.0 * UniverseUnits::CmPerLightYear;
        const double Honest = FMath::Square(ShipSky::FaintestFluxSunDistanceLy / 4.0);
        TestTrue(TEXT("honest at gamma 1: the inverse square"), FMath::IsNearlyEqual(ShipSky::NeighbourFlux(Sun, 1.0), Honest, 1e-9));
        TestTrue(TEXT("compressed at gamma 0.5: a Sun at 4 ly is about 18"),
            FMath::IsNearlyEqual(ShipSky::NeighbourFlux(Sun, 0.5), FMath::Sqrt(Honest), 1e-9));
        FSkyNeighbour Dwarf = Sun;
        Dwarf.Luminosity = 0.0017;
        TestEqual(TEXT("a Proxima is never missing: held at the faintest drawn"), ShipSky::NeighbourFlux(Dwarf, 0.5), 1.0);
        FSkyNeighbour Giant = Sun;
        Giant.Luminosity = 1.0e6;
        TestEqual(TEXT("and nothing is brighter than the starfield's brightest"), ShipSky::NeighbourFlux(Giant, 0.5), SkyStarfield::MaxFlux);

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

    // Between stars there is nothing to draw, and it draws nothing. Placed
    // explicitly, so the opening placement cannot decide this.
    Ship->PlaceShip(BetweenStars(Test.Universe), FQuat::Identity);
    Sky->SyncToShip();
    TestEqual(TEXT("between stars: no bodies"), Sky->GetProxyCount(), 0);
    TestFalse(TEXT("and no sun"), Sky->GetSun()->IsVisible());
    TestEqual(TEXT("and no neighbours"), Sky->GetNeighbourStars()->GetInstanceCount(), 0);

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
                FScopedCVar Mode(TEXT("ds.Sky.ExposureMode"), 2.0f);
                FScopedCVar Range(TEXT("ds.Sky.ExposureRange"), 1.0f);
                Sky->SyncToShip();
                TestTrue(TEXT("mode 2 is auto, held a range either side"), Settings.AutoExposureMethod == AEM_Histogram
                    && Settings.AutoExposureMinBrightness == 2.0f && Settings.AutoExposureMaxBrightness == 4.0f);
            }
        }
        Sky->SyncToShip();
    }

    // ds.Sky.Goto with nothing here to go to leaves the ship where it is.
    {
        const FUniversePosition Before = Ship->GetFlightState().GetUniversePosition();
        FOutputDeviceNull Quiet;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Sky.Goto 0 150"), Quiet, Test.World);
        TestTrue(TEXT("goto between stars does nothing"), Ship->GetFlightState().GetUniversePosition().DistanceTo(Before) == 0.0);
    }

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
        if (TestEqual(TEXT("one point per neighbour"), Neighbours->GetInstanceCount(), 2))
        {
            for (int32 Index = 0; Index < 2; ++Index)
            {
                FTransform Instance;
                Neighbours->GetInstanceTransform(Index, Instance, false);
                TestTrue(TEXT("a neighbour is in its true direction"),
                    Instance.GetLocation().GetSafeNormal().Equals(Fixture.Neighbours[Index].Direction, 1e-9));
                TestTrue(TEXT("on the dome"), FMath::IsNearlyEqual(Instance.GetLocation().Size(), AShipSky::DomeRadius, 1.0));
                const double Across = Instance.GetScale3D().X * 2.0 * Test.Sphere->GetBoundingBox().GetExtent().GetMax();
                TestTrue(TEXT("a point ds.Sky.PointPixels across"),
                    FMath::IsNearlyEqual(Across, ShipSky::PointDiameter(AShipSky::DomeRadius, Sky->GetPixelAngle(), CVarFloat(TEXT("ds.Sky.PointPixels"))), 1.0));

                const float* Data = &Neighbours->PerInstanceSMCustomData[Index * SkyMaterial::StarfieldCustomData];
                const FLinearColor Colour = SkyColour::Blackbody(Fixture.Neighbours[Index].TemperatureK);
                TestTrue(TEXT("coloured by its temperature"), FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataRed], Colour.R)
                    && FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataGreen], Colour.G) && FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataBlue], Colour.B));
                TestTrue(TEXT("as bright as a background star of its flux"), FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataBrightness],
                    AShipSky::PointStarBrightness(ShipSky::NeighbourFlux(Fixture.Neighbours[Index], CVarFloat(TEXT("ds.Sky.FluxGamma")))), 1e-6f));
            }
            const float* Data = Neighbours->PerInstanceSMCustomData.GetData();
            TestTrue(TEXT("a Sun at 4 ly outshines a red dwarf at 9"),
                Data[SkyMaterial::CustomDataBrightness] > Data[SkyMaterial::StarfieldCustomData + SkyMaterial::CustomDataBrightness]);
        }
    }

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

    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipSkyLiveTest,
    "DeepSpace.Sky.ShipSkyLive",
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
        const FVector Toward = (Nearest->Position - Here->Stub.Position).GetSafeNormal();
        TestTrue(TEXT("NeighbourStars instance 0 points at the nearest stub"), Instance.GetLocation().GetSafeNormal().Equals(Toward, 1e-6));
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
