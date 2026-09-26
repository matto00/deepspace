#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkyStarfield.h"
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
        const double Gamma = FSkyViewParams().FluxGamma;
        const double MeshDiameter = 2.0 * Sphere->GetBounds().BoxExtent.GetMax();
        const double Diameter = Dome->DistantStarPixels * Dome->GetPixelAngle() * Dome->DistantStarRadius;

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
            bCompressed &= FMath::IsNearlyEqual(static_cast<double>(Data[SkyMaterial::CustomDataBrightness]),
                                                SkyProjection::Compress(Expected[Index].Flux, Gamma), 1e-5);
        }
        TestTrue(TEXT("each star is where the universe's starfield puts it"), bWhereTheGalaxyPutsThem);
        TestTrue(TEXT("each is two pixels across at the dome"), bTwoPixels);
        TestTrue(TEXT("coloured by its blackbody"), bColoured);
        TestTrue(TEXT("and as bright as its compressed flux"), bCompressed);
        Dome->Destroy();
    }

    // The motes fade with speed: full at cruise less a tenth, gone under the
    // drive, where wrapping every frame would strobe.
    UMaterialInterface* StarMaterial = LoadObject<UMaterialInterface>(nullptr, SkyMaterial::StarPath);
    const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>();
    if (Ship && TestNotNull(TEXT("M_SkyStar exists"), StarMaterial) && TestNotNull(TEXT("and a universe"), Universe))
    {
        AShipCounterFrame* Motes = World->SpawnActor<AShipCounterFrame>();
        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        Motes->GetNearStars()->SetStaticMesh(Sphere);
        Motes->GetNearStars()->SetMaterial(0, StarMaterial);
        Motes->DistantStarCount = 16;

        float Authored = 0.0f;
        StarMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(SkyMaterial::Brightness), Authored);

        const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem());
        if (TestTrue(TEXT("there is a start system to drive at"), Home.IsSet()))
        {
            const FNavPlacement Opening = NavStart::OpeningPlacement(*Home);
            Ship->PlaceShip(Opening.Position, Opening.Orientation);
            Motes->RebuildStarfield();
            Motes->SyncToShip();

            const auto Brightness = [Motes]()
            {
                float Value = -1.0f;
                if (const UMaterialInterface* Material = Motes->GetNearStars()->GetMaterial(0))
                {
                    Material->GetScalarParameterValue(FHashedMaterialParameterInfo(SkyMaterial::Brightness), Value);
                }
                return Value;
            };
            TestTrue(TEXT("the motes drive a runtime copy of their material"),
                     Cast<UMaterialInstanceDynamic>(Motes->GetNearStars()->GetMaterial(0)) != nullptr);
            TestEqual(TEXT("parked, the motes are as bright as authored"), Brightness(), Authored);
            TestTrue(TEXT("and shown"), Motes->GetNearStars()->IsVisible());

            APawn* Pilot = World->SpawnActor<APawn>();
            Ship->SetPilot(Pilot);
            Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
            Ship->SetDriveEngaged(Pilot, true);
            Ship->Tick(0.1f);
            Motes->SyncToShip();
            TestTrue(TEXT("under the drive the ship is past the fade"), Ship->GetShipSpeed() > 2.0e5f);
            TestEqual(TEXT("and the motes are dark"), Brightness(), 0.0f);
            TestFalse(TEXT("and hidden"), Motes->GetNearStars()->IsVisible());

            // Off the drive the speed is clamped to cruise, 200 m/s, a tenth
            // of the way to the fade.
            Ship->SetDriveEngaged(Pilot, false);
            Motes->SyncToShip();
            TestTrue(TEXT("at cruise the motes are nine tenths as bright"),
                     FMath::IsNearlyEqual(Brightness(), 0.9f * Authored, 1e-4f));
            TestTrue(TEXT("and shown again"), Motes->GetNearStars()->IsVisible());
            Ship->ClearPilot();
        }
        Motes->Destroy();
    }

    // Which materials it is drawn with, which is the level's to assign and
    // invisible under -nullrhi: milestone 1's M_Star reads no custom data and
    // has no Brightness, so on it every star draws alike and the motes pop.
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

        // The fade swaps the motes onto a runtime copy; that is still M_SkyStar.
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
