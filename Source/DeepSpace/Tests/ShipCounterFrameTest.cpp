#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkyStarfield.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCounterFrameTest,
    "DeepSpace.Ship.CounterFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipCounterFrameTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CounterFrameTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    AShipCounterFrame* Frame = World->SpawnActor<AShipCounterFrame>();

    if (TestNotNull(TEXT("the counter-frame spawns"), Frame) && TestNotNull(TEXT("with a ship"), Ship))
    {
        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        Frame->GetDistantStars()->SetStaticMesh(Sphere);
        Frame->GetNearStars()->SetStaticMesh(Sphere);

        Frame->DistantStarCount = 16;
        Frame->NearStarCount = 32;
        Frame->NearFieldRadius = 10000.0;
        Frame->RebuildStarfield();

        // Both layers exist, and the distant one is a shell at its radius.
        TestEqual(TEXT("the distant layer is placed"),
                  Frame->GetDistantStars()->GetInstanceCount(), 16);
        TestEqual(TEXT("the near layer is placed"),
                  Frame->GetNearStars()->GetInstanceCount(), 32);

        // Instances are stored in single precision, so at 250,000 km "on the
        // shell" means to a part in a million: a few tens of metres, far
        // below a pixel.
        bool bAllOnShell = true;
        for (int32 Index = 0; Index < Frame->GetDistantStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetDistantStars()->GetInstanceTransform(Index, Instance, false);
            bAllOnShell &= FMath::IsNearlyEqual(Instance.GetLocation().Size(), Frame->DistantStarRadius,
                                                Frame->DistantStarRadius * 1e-6);
        }
        TestTrue(TEXT("every distant star sits on the shell"), bAllOnShell);

        // The counter-frame carries the ship's attitude, inverted -- and
        // nothing else. Its translation staying at zero is the load-bearing
        // part: children must never inherit a large coordinate.
        const FQuat Attitude(FVector::UpVector, 0.7);
        Ship->PlaceShip(FUniversePosition::FromVector(FVector(4.0e9, -2.0e9, 1.0e9)), Attitude);
        Frame->SyncToShip();
        TestTrue(TEXT("the counter-frame is the ship's attitude inverted"),
                 Frame->GetActorQuat().Equals(Attitude.Inverse(), 1e-5));
        TestEqual(TEXT("and never translates"), Frame->GetActorLocation(), FVector::ZeroVector);

        // Distant stars are direction only: flying a million kilometres does
        // not move them, which is exactly why they cannot show speed.
        FTransform BeforeFlight;
        Frame->GetDistantStars()->GetInstanceTransform(0, BeforeFlight, false);
        Ship->PlaceShip(FUniversePosition::FromVector(FVector(4.0e9 + 1.0e11, -2.0e9, 1.0e9)), Attitude);
        Frame->SyncToShip();
        FTransform AfterFlight;
        Frame->GetDistantStars()->GetInstanceTransform(0, AfterFlight, false);
        TestEqual(TEXT("a distant star does not translate with the ship"),
                  AfterFlight.GetLocation(), BeforeFlight.GetLocation());

        // Near stars are the layer that makes translation visible. Advance the
        // ship 1000 cm along world +X and every mote that did not wrap must
        // have slid exactly 1000 cm the other way.
        Ship->PlaceShip(FUniversePosition::FromVector(FVector::ZeroVector), FQuat::Identity);
        Frame->RebuildStarfield();
        Frame->SyncToShip();

        TArray<FVector> Before;
        for (int32 Index = 0; Index < Frame->GetNearStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetNearStars()->GetInstanceTransform(Index, Instance, true);
            Before.Add(Instance.GetLocation());
        }

        Ship->PlaceShip(FUniversePosition::FromVector(FVector(1000.0, 0.0, 0.0)), FQuat::Identity);
        Frame->SyncToShip();

        int32 Parallaxed = 0;
        for (int32 Index = 0; Index < Frame->GetNearStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetNearStars()->GetInstanceTransform(Index, Instance, true);
            if ((Instance.GetLocation() - Before[Index]).Equals(FVector(-1000.0, 0.0, 0.0), 1e-3))
            {
                ++Parallaxed;
            }
        }
        TestTrue(TEXT("the near field slides past as the ship advances"), Parallaxed > 24);

        // And it keeps doing so: fly far past the field and the motes have
        // wrapped round rather than being left behind.
        Ship->PlaceShip(FUniversePosition::FromVector(FVector(3.7e6, -1.1e6, 8.0e5)), FQuat::Identity);
        Frame->SyncToShip();

        bool bAllInField = true;
        for (int32 Index = 0; Index < Frame->GetNearStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetNearStars()->GetInstanceTransform(Index, Instance, true);
            const FVector Local = Instance.GetLocation();
            bAllInField &= Local.GetAbsMax() <= Frame->NearFieldRadius + 1.0;
        }
        TestTrue(TEXT("the near field wraps and stays around the ship"), bAllInField);
    }

    // The dome is the galaxy (sky decision 4): 3,000 stars at 250,000 km,
    // behind everything the sky draws, from the universe's own seed, each
    // two pixels across with its colour and brightness in custom data.
    {
        AShipCounterFrame* Dome = World->SpawnActor<AShipCounterFrame>();
        const AShipCounterFrame* Defaults = GetDefault<AShipCounterFrame>();
        TestEqual(TEXT("the dome holds 3,000 stars"), Defaults->DistantStarCount, 3000);
        TestEqual(TEXT("at 250,000 km"), Defaults->DistantStarRadius, 2.5e10);

        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        Dome->GetDistantStars()->SetStaticMesh(Sphere);
        Dome->RebuildStarfield();

        UInstancedStaticMeshComponent* Stars = Dome->GetDistantStars();
        TestEqual(TEXT("every star is placed"), Stars->GetInstanceCount(), 3000);
        TestEqual(TEXT("with four floats of custom data each"),
                  Stars->PerInstanceSMCustomData.Num(), 3000 * SkyMaterial::StarfieldCustomData);

        const TArray<FSkyStar> Expected = SkyStarfield::Generate(
            LocalSystem::StarfieldSeed(World, static_cast<uint64>(Dome->StarSeed)), 3000);
        const double MeshDiameter = 2.0 * Sphere->GetBounds().BoxExtent.GetMax();
        const double Diameter = ShipSky::PointDiameter(Dome->DistantStarRadius, Dome->GetPixelAngle(), AShipSky::PointPixels());

        bool bWhereTheGalaxyPutsThem = true;
        bool bTwoPixels = true;
        bool bColoured = true;
        bool bCompressed = true;
        for (int32 Index = 0; Index < Expected.Num(); ++Index)
        {
            FTransform Instance;
            Stars->GetInstanceTransform(Index, Instance, false);
            bWhereTheGalaxyPutsThem &= Instance.GetLocation().GetSafeNormal().Equals(Expected[Index].Direction, 1e-5);
            bTwoPixels &= FMath::IsNearlyEqual(Instance.GetScale3D().X * MeshDiameter, Diameter, Diameter * 1e-4);

            const float* Data = &Stars->PerInstanceSMCustomData[Index * SkyMaterial::StarfieldCustomData];
            const FLinearColor Colour = SkyColour::Blackbody(Expected[Index].TemperatureK);
            bColoured &= FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataRed], Colour.R, 1e-6f)
                && FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataGreen], Colour.G, 1e-6f)
                && FMath::IsNearlyEqual(Data[SkyMaterial::CustomDataBlue], Colour.B, 1e-6f);
            bCompressed &= Data[SkyMaterial::CustomDataBrightness] == AShipSky::PointStarBrightness(Expected[Index].Flux);
        }
        TestTrue(TEXT("each star is where the universe's starfield puts it"), bWhereTheGalaxyPutsThem);
        TestTrue(TEXT("each is two pixels across at the dome"), bTwoPixels);
        TestTrue(TEXT("coloured by its blackbody"), bColoured);
        TestTrue(TEXT("and as bright as the sky draws a point of its flux"), bCompressed);
        Dome->Destroy();
    }

    // The dust's law (flight-feel decision 8), pure: honest to the knee,
    // then a slow log climb to DustTop at the drive's top, each mote
    // stretched by the same fraction of that scale.
    {
        const double Knee = ShipDust::DefaultKnee;
        const double DustTop = ShipDust::DefaultDustTop;
        const double MaxStretch = ShipDust::DefaultStretch;
        const double Top = ShipDriveLever::DefaultTopLight * ShipDriveLever::LightCmPerSecond;
        const double CruiseTop = FShipFlightLimits::Cruise().MaxSpeed;
        TestEqual(TEXT("the drive's top is 0.1 c"), Top, 0.1 * ShipDriveLever::LightCmPerSecond);

        bool bHonest = true;
        for (const double Speed : { 0.0, 1.0, 2.0e4, 1.0e5, 1.999e5, Knee })
        {
            bHonest &= ShipDust::SeenSpeed(Speed, Knee, DustTop, Top) == Speed;
            bHonest &= ShipDust::Stretch(Speed, Knee, Top, MaxStretch) == 1.0;
        }
        TestTrue(TEXT("up to the knee the dust is streamed at the true speed, unstretched"), bHonest);
        TestEqual(TEXT("cruise's first tenth of its top, 2 km/s, is seen at its true speed"),
                  ShipDust::SeenSpeed(0.1 * CruiseTop, Knee, DustTop, Top), 0.1 * CruiseTop);

        // Cruise's top decade, 2 to 20 km/s, now runs through the log part:
        // 20 km/s is a quarter of the way from the knee to 0.1 c, seen at
        // 2.2 km/s and drawn 1.6 times long.
        const double SeenCruiseTop = ShipDust::SeenSpeed(CruiseTop, Knee, DustTop, Top);
        TestTrue(FString::Printf(TEXT("cruise's top, 20 km/s, is past the knee: seen at %.1f m/s, drawn %.2f times long"),
                                 SeenCruiseTop / 100.0, ShipDust::Stretch(CruiseTop, Knee, Top, MaxStretch)),
                 SeenCruiseTop > Knee && SeenCruiseTop < DustTop
                 && FMath::IsNearlyEqual(ShipDust::LogFraction(CruiseTop, Knee, Top), FMath::Loge(10.0) / FMath::Loge(Top / Knee), 1e-12));

        // The invariant decision 8 keeps: the drive never looks slower than
        // cruise. Its first notch is cruise's top, seen alike; every notch
        // above it is seen faster; and nothing cruise can do is seen faster
        // than the drive's bottom notch.
        bool bDriveNeverSlower = true;
        for (int32 Notch = 1; Notch <= ShipDriveLever::TableNotches(); ++Notch)
        {
            const double Seen = ShipDust::SeenSpeed(ShipDriveLever::NotchSpeed(Notch), Knee, DustTop, Top);
            bDriveNeverSlower &= Notch == 1 ? Seen == SeenCruiseTop : Seen > SeenCruiseTop;
        }
        for (double Cruising = 1.0; Cruising <= CruiseTop; Cruising *= 1.1)
        {
            bDriveNeverSlower &= ShipDust::SeenSpeed(Cruising, Knee, DustTop, Top)
                <= ShipDust::SeenSpeed(ShipDriveLever::NotchSpeed(1), Knee, DustTop, Top);
        }
        TestTrue(TEXT("the drive never looks slower than cruise: every notch seen at least as fast as cruise's top"), bDriveNeverSlower);
        TestTrue(TEXT("continuous at the knee"),
                 FMath::IsNearlyEqual(ShipDust::SeenSpeed(Knee * (1.0 + 1e-9), Knee, DustTop, Top), Knee, Knee * 1e-8));

        bool bRising = true;
        bool bUnderTop = true;
        bool bStretchBounded = true;
        double Previous = ShipDust::SeenSpeed(Knee, Knee, DustTop, Top);
        double PreviousStretch = 1.0;
        for (int32 Step = 1; Step <= 400; ++Step)
        {
            const double Speed = Knee * FMath::Pow(Top / Knee, Step / 400.0);
            const double Seen = ShipDust::SeenSpeed(Speed, Knee, DustTop, Top);
            const double Stretch = ShipDust::Stretch(Speed, Knee, Top, MaxStretch);
            bRising &= Seen > Previous && Stretch > PreviousStretch;
            bUnderTop &= Seen <= DustTop * (1.0 + 1e-12);
            bStretchBounded &= Stretch <= MaxStretch * (1.0 + 1e-12);
            Previous = Seen;
            PreviousStretch = Stretch;
        }
        TestTrue(TEXT("above the knee the seen speed and the stretch strictly rise, to the top"), bRising);
        TestTrue(TEXT("the seen speed never passes DustTop"), bUnderTop);
        TestTrue(TEXT("and the stretch never passes DustStretch"), bStretchBounded);
        TestTrue(TEXT("at 0.1 c, the drive's top, the dust is seen at DustTop"),
                 FMath::IsNearlyEqual(ShipDust::SeenSpeed(Top, Knee, DustTop, Top), DustTop, DustTop * 1e-9));
        TestTrue(TEXT("stretched DustStretch times"),
                 FMath::IsNearlyEqual(ShipDust::Stretch(Top, Knee, Top, MaxStretch), MaxStretch, 1e-9));
        TestEqual(TEXT("and past the top, no faster"), ShipDust::SeenSpeed(10.0 * Top, Knee, DustTop, Top), DustTop);
        TestTrue(TEXT("the middle of the scale is the geometric middle"),
                 FMath::IsNearlyEqual(ShipDust::SeenSpeed(FMath::Sqrt(Knee * Top), Knee, DustTop, Top),
                                      FMath::Sqrt(Knee * DustTop), 1e-3));

        // A top at or under the knee: the whole lever is honest, and above
        // the knee nothing climbs.
        TestEqual(TEXT("a lever that ends under the knee leaves the dust flat past it"),
                  ShipDust::SeenSpeed(3.0e5, Knee, DustTop, 1.0e5), Knee);
        TestEqual(TEXT("and unstretched"), ShipDust::Stretch(3.0e5, Knee, 1.0e5, MaxStretch), 1.0);
    }

    // The dust through the frame: shown at every speed, streamed at the seen
    // speed along the true velocity, stretched along it, and never drawn
    // outside its field.
    UMaterialInterface* StarMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarPath);
    const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>();
    if (Ship && TestNotNull(TEXT("M_SkyStar exists"), StarMaterial) && TestNotNull(TEXT("and a universe"), Universe))
    {
        AShipCounterFrame* Motes = World->SpawnActor<AShipCounterFrame>();
        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        Motes->GetNearStars()->SetStaticMesh(Sphere);
        Motes->GetNearStars()->SetMaterial(0, StarMaterial);
        Motes->DistantStarCount = 16;
        Motes->NearStarCount = 64;
        const double Radius = Motes->NearFieldRadius;
        const double Knee = ShipDust::DefaultKnee;

        float Authored = 0.0f;
        StarMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(SkyMaterial::Brightness), Authored);

        const auto Brightness = [Motes]()
        {
            float Value = -1.0f;
            if (const UMaterialInterface* Material = Motes->GetNearStars()->GetMaterial(0))
            {
                Material->GetScalarParameterValue(FHashedMaterialParameterInfo(SkyMaterial::Brightness), Value);
            }
            return Value;
        };

        // Every mote drawn where the dust is, through the ship's rotation, and
        // inside the field in the field's own axes.
        const auto CheckDrawn = [this, Motes, Ship, Radius](const TCHAR* Case, double ExpectedStretch, const FVector& Along)
        {
            const FShipFlightState& Flight = Ship->GetFlightState();
            const TConstArrayView<FVector> Field = Motes->GetDustField();
            bool bWhere = true;
            bool bInField = true;
            bool bStretched = true;
            bool bAlong = true;
            for (int32 Index = 0; Index < Field.Num(); ++Index)
            {
                FTransform Instance;
                Motes->GetNearStars()->GetInstanceTransform(Index, Instance, true);
                bWhere &= Instance.GetLocation().Equals(Flight.UniverseDirectionToWorld(Field[Index]), 0.05);
                const FVector InFieldAxes = Flight.GetUniverseOrientation().RotateVector(Instance.GetLocation());
                bInField &= InFieldAxes.GetAbsMax() <= Radius + 1.0 && Field[Index].GetAbsMax() <= Radius;
                const FVector Scale = Instance.GetScale3D();
                bStretched &= FMath::IsNearlyEqual(Scale.X, Motes->NearStarScale * ExpectedStretch, 1e-4 * Scale.X)
                    && FMath::IsNearlyEqual(Scale.Y, Motes->NearStarScale, 1e-6) && FMath::IsNearlyEqual(Scale.Z, Motes->NearStarScale, 1e-6);
                if (!Along.IsZero())
                {
                    bAlong &= Instance.GetRotation().GetForwardVector().Equals(Flight.UniverseDirectionToWorld(Along), 1e-4);
                }
            }
            TestTrue(FString::Printf(TEXT("%s: every mote is drawn where the dust is, through the ship's rotation"), Case), bWhere);
            TestTrue(FString::Printf(TEXT("%s: and none outside the field"), Case), bInField);
            TestTrue(FString::Printf(TEXT("%s: stretched %.3f times"), Case, ExpectedStretch), bStretched);
            TestTrue(FString::Printf(TEXT("%s: along the velocity"), Case), bAlong);
        };

        // One frame's motion: how far and which way the dust moved against
        // how far the ship went. Only motes that did not wrap are compared.
        struct FFrameMotion
        {
            FVector Moved = FVector::ZeroVector;    // the ship, universe axes
            TArray<FVector> Deltas;                 // the dust, unwrapped only
        };
        const auto OneFrame = [Motes, Ship, Radius](float DeltaSeconds)
        {
            FFrameMotion Motion;
            const TArray<FVector> Before(Motes->GetDustField());
            const FUniversePosition From = Ship->GetFlightState().GetUniversePosition();
            Ship->Tick(DeltaSeconds);
            Motes->SyncToShip();
            Motion.Moved = Ship->GetFlightState().GetUniversePosition() - From;
            const TConstArrayView<FVector> After = Motes->GetDustField();
            for (int32 Index = 0; Index < Before.Num(); ++Index)
            {
                const FVector Delta = After[Index] - Before[Index];
                if (Delta.GetAbsMax() < Radius)
                {
                    Motion.Deltas.Add(Delta);
                }
            }
            return Motion;
        };

        const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem());
        if (TestTrue(TEXT("there is a start system to fly from"), Home.IsSet()))
        {
            // Nose straight out from the star, so nothing is on its path and
            // the lever alone sets the speed.
            const FNavPlacement Opening = NavStart::OpeningPlacement(*Home);
            const FVector Outward = (Opening.Position - Home->Stub.Position).GetSafeNormal();
            Ship->PlaceShip(Opening.Position, FRotationMatrix::MakeFromX(Outward).ToQuat());
            Motes->RebuildStarfield();
            Motes->SyncToShip();

            TestTrue(TEXT("the motes draw a runtime copy of their material"),
                     Cast<UMaterialInstanceDynamic>(Motes->GetNearStars()->GetMaterial(0)) != nullptr);
            TestEqual(TEXT("parked, the motes are as bright as authored"), Brightness(), Authored);
            TestTrue(TEXT("and shown"), Motes->GetNearStars()->IsVisible());
            CheckDrawn(TEXT("parked"), 1.0, FVector::ZeroVector);

            APawn* Pilot = World->SpawnActor<APawn>();
            Ship->SetPilot(Pilot);
            Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
            Ship->SetDriveEngaged(Pilot, true);
            Ship->SetDriveLever(Pilot, Ship->GetFlightState().GetDriveNotchCount() - 1);
            for (int32 Tick = 0; Tick < 150; ++Tick)
            {
                Ship->Tick(0.1f);
                Motes->SyncToShip();
            }
            const FShipFlightState& Flight = Ship->GetFlightState();
            const double Top = Flight.GetLimits().DriveTop;
            TestTrue(FString::Printf(TEXT("the ship is at the drive's top, 0.1 c (%.6g cm/s)"), Flight.GetSpeed()),
                     FMath::IsNearlyEqual(Flight.GetSpeed(), 0.1 * ShipDriveLever::LightCmPerSecond, ShipDriveLever::LightCmPerSecond * 1e-7)
                     && Top == Flight.GetSpeed());
            TestEqual(TEXT("with nothing holding it"), Flight.GetHold(), EFlightHold::Free);
            TestEqual(TEXT("at 0.1 c the motes are as bright as authored"), Brightness(), Authored);
            TestTrue(TEXT("and shown: the drive never looks slower than cruise"), Motes->GetNearStars()->IsVisible());

            const FFrameMotion AtTop = OneFrame(1.0f / 60.0f);
            const double Seen = ShipDust::SeenSpeed(Flight.GetSpeed(), Knee, ShipDust::DefaultDustTop, Top);
            const FVector Expected = -AtTop.Moved * (Seen / Flight.GetSpeed());
            int32 AtSeen = 0;
            for (const FVector& Delta : AtTop.Deltas)
            {
                AtSeen += Delta.Equals(Expected, 1e-3 * Expected.Size()) ? 1 : 0;
            }
            TestTrue(FString::Printf(TEXT("at 0.1 c the ship went %.4g cm in the frame"), AtTop.Moved.Size()), AtTop.Moved.Size() > 1.0e7);
            TestTrue(FString::Printf(TEXT("and the dust streamed back along its path at the seen 3 km/s: %d of %d unwrapped motes"),
                                     AtSeen, AtTop.Deltas.Num()),
                     AtTop.Deltas.Num() > 32 && AtSeen == AtTop.Deltas.Num());
            TestTrue(FString::Printf(TEXT("%.1f cm, 50 m a 60 Hz frame and under the strobe limit"), Expected.Size()),
                     FMath::IsNearlyEqual(Expected.Size(), ShipDust::DefaultDustTop * AtTop.Moved.Size() / Flight.GetSpeed(), 1.0)
                     && Expected.Size() < 0.5 * 12000.0);
            CheckDrawn(TEXT("at 0.1 c"), ShipDust::DefaultStretch, Flight.GetVelocity().GetSafeNormal());

            // The law's three knobs are the developer's playtest dials
            // (flight-feel decision 8), so each is read at use: turned to
            // values no default holds, the same frame at 0.1 c streams the dust
            // at the tuned top and stretches it the tuned length.
            {
                const SkyTestWorld::FScopedCVar TunedKnee(TEXT("ds.Sky.DustKnee"), 1.0f);
                const SkyTestWorld::FScopedCVar TunedTop(TEXT("ds.Sky.DustTop"), 2.5f);
                const SkyTestWorld::FScopedCVar TunedStretch(TEXT("ds.Sky.DustStretch"), 4.0f);
                const FFrameMotion Tuned = OneFrame(1.0f / 60.0f);
                const FVector TunedExpected = -Tuned.Moved * (2.5e5 / Flight.GetSpeed());
                int32 AtTuned = 0;
                for (const FVector& Delta : Tuned.Deltas)
                {
                    AtTuned += Delta.Equals(TunedExpected, 1e-3 * TunedExpected.Size()) ? 1 : 0;
                }
                TestTrue(FString::Printf(TEXT("with ds.Sky.DustTop at 2.5 km/s the dust streams at it at 0.1 c: %d of %d unwrapped motes"),
                                         AtTuned, Tuned.Deltas.Num()),
                         Tuned.Deltas.Num() > 32 && AtTuned == Tuned.Deltas.Num());
                CheckDrawn(TEXT("at 0.1 c, ds.Sky.DustStretch 4"), 4.0, Flight.GetVelocity().GetSafeNormal());
            }
            Motes->SyncToShip();
            CheckDrawn(TEXT("at 0.1 c, the dials put back"), ShipDust::DefaultStretch, Flight.GetVelocity().GetSafeNormal());

            // Off the drive the ship spools down to cruise's top, 20 km/s,
            // which is past the knee: still a representation, streamed at
            // the seen speed and drawn long, a little less so than the drive.
            Ship->SetDriveEngaged(Pilot, false);
            for (int32 Tick = 0; Tick < 300 && Ship->GetFlightState().GetMode() != EFlightMode::Cruise; ++Tick)
            {
                Ship->Tick(0.1f);
                Motes->SyncToShip();
            }
            for (int32 Tick = 0; Tick < 50; ++Tick)
            {
                Ship->Tick(0.1f);
                Motes->SyncToShip();
            }
            TestTrue(FString::Printf(TEXT("back at cruise's top (%.1f cm/s)"), Flight.GetSpeed()),
                     FMath::IsNearlyEqual(Flight.GetSpeed(), Flight.GetLimits().MaxSpeed, 0.01 * Flight.GetLimits().MaxSpeed));
            {
                const FFrameMotion AtCruiseTop = OneFrame(1.0f / 60.0f);
                const double CruiseSeen = ShipDust::SeenSpeed(Flight.GetSpeed(), Knee, ShipDust::DefaultDustTop, Top);
                const FVector CruiseExpected = -AtCruiseTop.Moved * (CruiseSeen / Flight.GetSpeed());
                int32 AtCruiseSeen = 0;
                for (const FVector& Delta : AtCruiseTop.Deltas)
                {
                    AtCruiseSeen += Delta.Equals(CruiseExpected, 1e-3 * CruiseExpected.Size()) ? 1 : 0;
                }
                TestTrue(FString::Printf(TEXT("at cruise's top the dust streams at the seen %.1f m/s: %d of %d unwrapped motes"),
                                         CruiseSeen / 100.0, AtCruiseSeen, AtCruiseTop.Deltas.Num()),
                         AtCruiseTop.Deltas.Num() > 32 && AtCruiseSeen == AtCruiseTop.Deltas.Num()
                         && CruiseSeen < Seen && CruiseSeen > Knee);
                CheckDrawn(TEXT("at cruise's top"), ShipDust::Stretch(Flight.GetSpeed(), Knee, Top, ShipDust::DefaultStretch),
                           Flight.GetVelocity().GetSafeNormal());
            }

            // A turn under cruise's inertia, about both of the body's axes
            // that swing the nose (Y and Z), so it swings at 0.36 rad/s: faster
            // than the boosters' 2 km/s^2 can turn a 20 km/s velocity, 0.1
            // rad/s. The ship slides on along its old path, and the dust
            // streams along the slide, which is where the ship is going, not
            // the nose.
            TestTrue(TEXT("the pilot turns"), Ship->SetFlightCommand(Pilot, 1.0f, FVector(0.0, 1.0, 1.0)));
            for (int32 Tick = 0; Tick < 30; ++Tick)
            {
                Ship->Tick(0.05f);
                Motes->SyncToShip();
            }
            const FVector Nose = Flight.GetUniverseOrientation().GetForwardVector();
            const FVector Heading = Flight.GetVelocity().GetSafeNormal();
            const double Slide = FMath::Acos(FMath::Clamp(FVector::DotProduct(Nose, Heading), -1.0, 1.0));
            TestTrue(FString::Printf(TEXT("the ship slides %.3f rad off its nose"), Slide), Slide > 0.05);

            // Cruise's top is above the knee, so the slide is streamed at the
            // seen speed, and each mote drawn long along the velocity, not the
            // nose. Under the knee a slide is round, and the two cannot be told
            // apart.
            const FFrameMotion Sliding = OneFrame(1.0f / 60.0f);
            const double SlideSpeed = Flight.GetSpeed();
            const double SlideSeen = ShipDust::SeenSpeed(SlideSpeed, Knee, ShipDust::DefaultDustTop, Flight.GetLimits().DriveTop);
            TestTrue(FString::Printf(TEXT("above the 2 km/s knee cruise's %.1f m/s is seen at %.1f m/s"), SlideSpeed / 100.0, SlideSeen / 100.0),
                     SlideSeen < 0.9 * SlideSpeed);
            bool bAlongSlide = Sliding.Deltas.Num() > 32;
            bool bSeenInSlide = Sliding.Deltas.Num() > 32;
            for (const FVector& Delta : Sliding.Deltas)
            {
                const FVector Way = -Delta.GetSafeNormal();
                bAlongSlide &= FVector::DotProduct(Way, Heading) > FMath::Cos(0.01)
                    && FVector::DotProduct(Way, Nose) < FMath::Cos(0.5 * Slide);
                bSeenInSlide &= FMath::IsNearlyEqual(Delta.Size(), Sliding.Moved.Size() * SlideSeen / SlideSpeed,
                                                     1e-3 * Delta.Size());
            }
            TestTrue(TEXT("in the slide the dust streams along the velocity, not the nose"), bAlongSlide);
            TestTrue(TEXT("as far as the seen speed carries it"), bSeenInSlide);
            const double SlideStretch = ShipDust::Stretch(SlideSpeed, Knee, Flight.GetLimits().DriveTop, ShipDust::DefaultStretch);
            TestTrue(FString::Printf(TEXT("and stretched above 1 (%.4f)"), SlideStretch), SlideStretch > 1.05);
            CheckDrawn(TEXT("in the slide"), SlideStretch, Flight.GetVelocity().GetSafeNormal());

            // Slowed under the knee, cruise's dust is honest and round again:
            // the lever at 0.6 is 380 m/s.
            Ship->SetFlightCommand(Pilot, 0.6f, FVector::ZeroVector);
            for (int32 Tick = 0; Tick < 150; ++Tick)
            {
                Ship->Tick(0.1f);
                Motes->SyncToShip();
            }
            TestTrue(FString::Printf(TEXT("cruising under the knee (%.1f m/s)"), Flight.GetSpeed() / 100.0),
                     Flight.GetSpeed() > 0.0 && Flight.GetSpeed() < Knee
                     && FMath::IsNearlyEqual(Flight.GetSpeed(), Flight.GetLeverSpeed(), 1e-6 * Flight.GetSpeed()));
            const FFrameMotion AtCruise = OneFrame(1.0f / 60.0f);
            int32 Honest = 0;
            for (const FVector& Delta : AtCruise.Deltas)
            {
                Honest += Delta.Equals(-AtCruise.Moved, 1e-6) ? 1 : 0;
            }
            TestTrue(FString::Printf(TEXT("under the knee the dust moves exactly as far as the ship, the other way: %d of %d"),
                                     Honest, AtCruise.Deltas.Num()),
                     AtCruise.Deltas.Num() > 32 && Honest == AtCruise.Deltas.Num());
            TestEqual(TEXT("under the knee the motes are as bright as authored"), Brightness(), Authored);
            CheckDrawn(TEXT("under the knee"), 1.0, FVector::ZeroVector);
            Ship->ClearPilot();
        }
        Motes->Destroy();
    }

    // Which materials it is drawn with, which is the level's to assign and
    // invisible under -nullrhi: milestone 1's M_Star reads no custom data and
    // has no Colour, so on it every star draws alike and the marker is white.
    UMaterialInterface* StarfieldMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarfieldPath);
    UMaterialInterface* OldStar = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Star.M_Star"));
    if (Ship && TestNotNull(TEXT("M_SkyStarfield exists"), StarfieldMaterial)
        && TestNotNull(TEXT("M_SkyStar exists for the material check"), StarMaterial)
        && TestNotNull(TEXT("and milestone 1's M_Star"), OldStar))
    {
        AShipCounterFrame* Checked = World->SpawnActor<AShipCounterFrame>();
        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        Checked->GetDistantStars()->SetStaticMesh(Sphere);
        Checked->GetNearStars()->SetStaticMesh(Sphere);
        Checked->DistantStarCount = 16;

        Checked->GetDistantStars()->SetMaterial(0, OldStar);
        Checked->GetNearStars()->SetMaterial(0, OldStar);
        TestEqual(TEXT("M_Star on both layers is two problems"), Checked->FindMaterialProblems().Num(), 2);

        Checked->GetDistantStars()->SetMaterial(0, StarfieldMaterial);
        TestEqual(TEXT("M_SkyStarfield on the dome answers one"), Checked->FindMaterialProblems().Num(), 1);
        Checked->GetDistantStars()->SetMaterial(0, StarMaterial);
        Checked->GetNearStars()->SetMaterial(0, StarfieldMaterial);
        TestEqual(TEXT("the two swapped are still two"), Checked->FindMaterialProblems().Num(), 2);

        Checked->GetDistantStars()->SetMaterial(0, StarfieldMaterial);
        Checked->GetNearStars()->SetMaterial(0, StarMaterial);
        TestEqual(TEXT("M_SkyStarfield on the dome and M_SkyStar on the motes is none"),
                  Checked->FindMaterialProblems().Num(), 0);

        // The sync swaps the motes onto a runtime copy; that is still M_SkyStar.
        Checked->RebuildStarfield();
        Checked->SyncToShip();
        TestTrue(TEXT("the motes now draw a runtime copy"),
                 Cast<UMaterialInstanceDynamic>(Checked->GetNearStars()->GetMaterial(0)) != nullptr);
        TestEqual(TEXT("which is still M_SkyStar"), Checked->FindMaterialProblems().Num(), 0);
        Checked->Destroy();
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
