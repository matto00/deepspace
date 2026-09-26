#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"
#include "Ship/ShipCounterFrame.h"
#include "Ship/ShipSubsystem.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipCounterFrameJumpTest,
    "DeepSpace.Ship.CounterFrameJump",
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

    /** The near field's instances, in world space. */
    TArray<FTransform> Motes(const AShipCounterFrame* Frame)
    {
        TArray<FTransform> Out;
        for (int32 Index = 0; Index < Frame->GetNearStars()->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Frame->GetNearStars()->GetInstanceTransform(Index, Instance, true);
            Out.Add(Instance);
        }
        return Out;
    }
}

/**
 * What the counter-frame does across a jump (nav decision 8): the course
 * marker on the dome at the star's true direction, the dome and the marker
 * hidden between stars while the motes stretch into streaks, the near field
 * scattered again round the arrival -- and the dome itself untouched, because
 * the unmoved background is what makes the rest read as somewhere else.
 */
bool FShipCounterFrameJumpTest::RunTest(const FString& Parameters)
{
    FScopedCVar Instant(TEXT("ds.Nav.ChargeSeconds"), 0.0f);

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CounterFrameJumpTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>();
    AShipCounterFrame* Frame = World->SpawnActor<AShipCounterFrame>();
    const TOptional<FStarSystem> Home = Universe ? Universe->GetSystem(Universe->GetStartSystem()) : TOptional<FStarSystem>();

    if (TestNotNull(TEXT("the world has a ship"), Ship) && TestNotNull(TEXT("and a counter-frame"), Frame)
        && TestTrue(TEXT("and a start system"), Home.IsSet()))
    {
        UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        Frame->GetDistantStars()->SetStaticMesh(Sphere);
        Frame->GetNearStars()->SetStaticMesh(Sphere);
        Frame->DistantStarCount = 64;
        Frame->NearStarCount = 48;
        Frame->NearFieldRadius = 10000.0;

        const FNavPlacement Opening = NavStart::OpeningPlacement(*Home);
        Ship->PlaceShip(Opening.Position, Opening.Orientation);
        Frame->RebuildStarfield();
        Frame->SyncToShip();
        TestEqual(TEXT("built for the serial the ship is on"), Frame->GetBuiltForSerial(), Ship->GetJumpSerial());
        TestNull(TEXT("with no course there is no marker"), Frame->GetCourseMarker());

        FTransform DomeBefore;
        Frame->GetDistantStars()->GetInstanceTransform(0, DomeBefore, false);

        // Plot, and the marker appears on the dome at the star's true
        // direction, sized in pixels.
        const TArray<FStarSystemStub> Chart = Ship->GetChart();
        if (!TestTrue(TEXT("somewhere to go"), Chart.Num() > 0) || !TestTrue(TEXT("plotted"), Ship->PlotCourse(Chart[0].Id)))
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
            return false;
        }
        Frame->SyncToShip();
        UStaticMeshComponent* Marker = Frame->GetCourseMarker();
        const TOptional<FVector> Course = Ship->GetCourseDirection();
        if (TestNotNull(TEXT("plotting a course makes the marker"), Marker) && TestTrue(TEXT("toward a star"), Course.IsSet()))
        {
            const FVector Where = Marker->GetRelativeLocation();
            TestTrue(TEXT("the marker sits at the course's true direction"), Where.GetSafeNormal().Equals(*Course, 1e-6));
            TestTrue(TEXT("on the dome"), FMath::IsNearlyEqual(Where.Size(), Frame->GetDomeRadius(), Frame->GetDomeRadius() * 1e-6));
            const double Diameter = Marker->GetRelativeScale3D().X * 2.0 * Sphere->GetBounds().BoxExtent.GetMax();
            const double Pixels = Diameter / (Frame->GetPixelAngle() * Frame->GetDomeRadius());
            TestTrue(FString::Printf(TEXT("six pixels across: %.3f"), Pixels), FMath::IsNearlyEqual(Pixels, 6.0, 1e-6));
            TestTrue(TEXT("and shown"), Marker->IsVisible());
        }

        // Aimed and engaged, with an instant charge: the fold opens.
        Ship->PlaceShip(Opening.Position, FRotationMatrix::MakeFromX(*Course).ToQuat());
        Ship->SetJumpEngaged(true);
        Ship->Tick(0.05f);
        Ship->Tick(0.05f);
        if (TestTrue(TEXT("the jump has begun"), Ship->IsInTransit()))
        {
            // Well into the transit, so the streaks are long.
            for (int32 Step = 0; Step < 100 && Ship->IsInTransit() && Ship->GetTransitProgress() < 0.4; ++Step)
            {
                Ship->Tick(0.25f);
            }
            Frame->SyncToShip();
            TestFalse(TEXT("between stars the dome is hidden"), Frame->GetDistantStars()->IsVisible());
            TestFalse(TEXT("and so is the marker"), Marker && Marker->IsVisible());

            bool bStretched = true;
            bool bInField = true;
            for (const FTransform& Mote : Motes(Frame))
            {
                const FVector Scale = Mote.GetScale3D();
                bStretched &= Scale.X > 10.0 * Scale.Y && FMath::IsNearlyEqual(Scale.Y, Scale.Z, 1e-6);
                bInField &= Mote.GetLocation().GetAbsMax() <= Frame->NearFieldRadius + 1.0;
            }
            TestTrue(TEXT("the motes are streaks along the ship's forward"), bStretched);
            TestTrue(TEXT("still wrapped into the field"), bInField);
        }

        for (int32 Step = 0; Step < 100 && Ship->GetJumpSerial() == 0; ++Step)
        {
            Ship->Tick(0.25f);
        }
        TestEqual(TEXT("the ship has arrived"), Ship->GetJumpSerial(), 1);
        Frame->SyncToShip();
        TestEqual(TEXT("the near field is scattered for the new serial"), Frame->GetBuiltForSerial(), 1);

        bool bAround = true;
        bool bRound = true;
        for (const FTransform& Mote : Motes(Frame))
        {
            bAround &= Mote.GetLocation().GetAbsMax() <= Frame->NearFieldRadius + 1.0;
            bRound &= Mote.GetScale3D().Equals(FVector(Frame->NearStarScale), 1e-9);
        }
        TestTrue(TEXT("after a jump every mote is round the ship, not light years behind"), bAround);
        TestTrue(TEXT("and round again, not a streak"), bRound);
        TestTrue(TEXT("the dome is back"), Frame->GetDistantStars()->IsVisible());
        TestFalse(TEXT("and with the course done the marker is not"), Marker && Marker->IsVisible());

        FTransform DomeAfter;
        Frame->GetDistantStars()->GetInstanceTransform(0, DomeAfter, false);
        TestTrue(TEXT("the dome has not moved across the jump"), DomeAfter.Equals(DomeBefore, 1e-3));
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
