#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/ShipLaptop.h"
#include "Ship/ShipNavScreen.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScreenStandUpTest,
    "DeepSpace.Ship.ScreenStandUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** A blocking box, as solid to a body as the ship's furniture is. */
    AActor* SpawnBlock(UWorld* World, const FVector& Centre, const FVector& Extent)
    {
        AActor* Block = World->SpawnActor<AActor>();
        UBoxComponent* Box = NewObject<UBoxComponent>(Block);
        Box->InitBoxExtent(Extent);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Block->SetRootComponent(Box);
        Box->RegisterComponent();
        Block->SetActorLocation(Centre);
        return Block;
    }

    /** The ship's seats, cm off the floor: the pilot_seat cushion the
     *  chart is read from, and the galley's bench the laptop is. Heights of
     *  props, not of the screens: a screen seats the body on the floor under
     *  its chair, and what matters here is what a player can climb onto. */
    constexpr double ChairCushionCm = 55.0;
    constexpr double BenchCm = 45.0;

    /** A chair: a box under the screen's use transform, its top the cushion
     *  -- which is what this returns. */
    FVector SpawnChairUnder(UWorld* World, const AShipScreen* Screen, double CushionCm)
    {
        const FVector Floor = Screen->GetUseTransform().GetLocation();
        SpawnBlock(World, FVector(Floor.X, Floor.Y, Floor.Z + CushionCm * 0.5), FVector(30.0, 30.0, CushionCm * 0.5));
        return FVector(Floor.X, Floor.Y, Floor.Z + CushionCm);
    }

    ADeepSpaceCharacter* SpawnStanding(UWorld* World, const FVector2D& At, double FloorZ = 0.0)
    {
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        // Feet 2 cm off the floor at Z = 0, as the movement component floats them.
        const float HalfHeight = GetDefault<ADeepSpaceCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        return World->SpawnActor<ADeepSpaceCharacter>(FVector(At.X, At.Y, FloorZ + HalfHeight + 2.0),
                                                      FRotator::ZeroRotator, Spawn);
    }

    /**
     * Stood up properly: on the floor, overlapping nothing a body collides
     * with, off the chair, and with the eyes somewhere a camera may be.
     */
    void CheckStanding(FAutomationTestBase& Test, const FString& What, UWorld* World,
                       ADeepSpaceCharacter* Player, const FVector& Seat, double FloorZ = 0.0)
    {
        const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
        const FVector Centre = Player->GetActorLocation();
        const float Bottom = Centre.Z - Capsule->GetScaledCapsuleHalfHeight();

        Test.TestTrue(FString::Printf(TEXT("%s: the feet are on the floor (%.1f cm, floor at %.0f)"),
                                      *What, Bottom, FloorZ),
                      Bottom >= FloorZ - 0.1 && Bottom <= FloorZ + 5.0);

        const FCollisionQueryParams Params(SCENE_QUERY_STAT(StandUpTest), false, Player);
        Test.TestFalse(What + TEXT(": the capsule overlaps nothing"),
                       World->OverlapBlockingTestByChannel(
                           Centre, FQuat::Identity, ECC_Pawn,
                           FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),
                                                        Capsule->GetScaledCapsuleHalfHeight()),
                           Params));

        // The chair is 60 cm square; clear of it means the body is beside
        // it, not on it.
        Test.TestTrue(What + TEXT(": off the chair"),
                      FVector::Dist2D(Centre, Seat) > 30.0 + Capsule->GetScaledCapsuleRadius() - 1.0);

        // The camera was framing the screen and is attached to the capsule,
        // so the teleport carried it with it. It must be back at the body --
        // not left in front of it, and not inside anything.
        const FVector Eye = Player->GetEyeLocation();
        Test.TestTrue(What + TEXT(": the eyes are back with the body"),
                      FVector::Dist2D(Eye, Centre) < Capsule->GetScaledCapsuleRadius() + 20.0);
        Test.TestFalse(What + TEXT(": and not inside anything"),
                       World->OverlapBlockingTestByChannel(Eye, FQuat::Identity, ECC_Camera,
                                                           FCollisionShape::MakeSphere(5.0f), Params));
    }
}

/**
 * Getting up from a screen. The developer, at the chart chair: "when i leave
 * the chart chair it puts me on top and i have to crouch to get off." The
 * seat is on the chair, and standing up used to leave the body where it sat.
 * It now goes back to where the player stood when they sat down, or to the
 * nearest clear floor to it when that is taken -- and never through a wall
 * to get there.
 */
bool FScreenStandUpTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ScreenStandUpTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    // A floor, its top at Z = 0 as the ship's is.
    SpawnBlock(World, FVector(0.0, 0.0, -10.0), FVector(2000.0, 2000.0, 10.0));

    // The chart faces -X at yaw 0, so its chair is 126 cm toward -X.
    AShipNavScreen* Chart = World->SpawnActor<AShipNavScreen>(FVector(0.0, 0.0, 105.0), FRotator::ZeroRotator);
    // The laptop on a table well away from it, with its bench.
    AShipLaptop* Laptop = World->SpawnActor<AShipLaptop>(FVector(0.0, 800.0, 75.0), FRotator::ZeroRotator);

    if (!TestNotNull(TEXT("the chart spawns"), Chart) || !TestNotNull(TEXT("the laptop spawns"), Laptop))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }
    SpawnBlock(World, FVector(15.0, 800.0, 37.0), FVector(40.0, 60.0, 37.0));
    const FVector ChartSeat = SpawnChairUnder(World, Chart, ChairCushionCm);
    const FVector LaptopSeat = SpawnChairUnder(World, Laptop, BenchCm);

    // 1. The spot the player left is free: they go back to it.
    {
        const FVector2D Stood(ChartSeat.X - 124.0, 0.0);
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood);
        if (TestNotNull(TEXT("the player spawns"), Player))
        {
            Player->UseScreen(Chart);
            TestTrue(TEXT("sitting at the chart puts the body on its chair"),
                     Player->IsInScreenChair() && Player->GetActorLocation().Z > ChartSeat.Z);
            // A frame seated: the camera goes out to frame the screen.
            Player->PlaceCamera(0.016f, Player->GetViewRotation());

            Player->StopUsingScreen();
            CheckStanding(*this, TEXT("chart, spot free"), World, Player, ChartSeat);
            TestTrue(TEXT("chart, spot free: back where they stood"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) < 1.0);
            Player->Destroy();
        }
    }

    // 2. The spot is taken, and a bulkhead stands 50 cm behind it: they go
    //    to the nearest clear floor on *this* side of the bulkhead.
    {
        const FVector2D Stood(ChartSeat.X - 124.0, 0.0);
        const double BulkheadFace = Stood.X - 45.0;
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood);
        if (TestNotNull(TEXT("the player spawns again"), Player))
        {
            Player->UseScreen(Chart);
            Player->PlaceCamera(0.016f, Player->GetViewRotation());

            // Something arrives where they were standing while they read --
            // clear of the floor, like a shelf or a hung locker, so the
            // floor is still there and only the body's room is taken.
            AActor* Taken = SpawnBlock(World, FVector(Stood, 115.0), FVector(40.0, 40.0, 85.0));
            AActor* Bulkhead = SpawnBlock(World, FVector(BulkheadFace - 5.0, 0.0, 150.0), FVector(5.0, 600.0, 150.0));

            Player->StopUsingScreen();
            CheckStanding(*this, TEXT("chart, spot taken"), World, Player, ChartSeat);
            TestTrue(TEXT("chart, spot taken: not where they stood, which is occupied"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) > 40.0);
            TestTrue(TEXT("chart, spot taken: near it, beside the chart"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) < 150.0);
            TestTrue(FString::Printf(TEXT("chart, spot taken: this side of the bulkhead (x %.0f, face at %.0f)"),
                                     Player->GetActorLocation().X, BulkheadFace),
                     Player->GetActorLocation().X - Player->GetCapsuleComponent()->GetScaledCapsuleRadius() > BulkheadFace);

            Taken->Destroy();
            Bulkhead->Destroy();
            Player->Destroy();
        }
    }

    // 3. The laptop goes the same way: up from the bench, not onto it.
    {
        const FVector2D Stood(LaptopSeat.X - 90.0, LaptopSeat.Y + 20.0);
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood);
        if (TestNotNull(TEXT("the player spawns at the laptop"), Player))
        {
            Player->UseScreen(Laptop);
            TestTrue(TEXT("sitting at the laptop puts the body on its bench"),
                     Player->IsInScreenChair() && Player->GetActorLocation().Z > LaptopSeat.Z);
            Player->PlaceCamera(0.016f, Player->GetViewRotation());

            Player->StopUsingScreen();
            CheckStanding(*this, TEXT("laptop"), World, Player, LaptopSeat);
            TestTrue(TEXT("laptop: back where they stood"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) < 1.0);
            Player->Destroy();
        }
    }

    // The ring search's own rule, that a spot is floor only at the height of
    // the floor under the seat: not a table top beside them, and not the
    // deck below an edge. Both need the remembered spot taken and the
    // nearest free spots, the side away from the seat first, to be the wrong
    // kind of ground -- cases 1 to 3 never reach a ring spot over anything
    // but floor, so they say nothing about it.
    //
    // The rings are 30 cm apart (StandRingStepCm). Something 40 cm square
    // where the player stood keeps a body off the whole first ring and off
    // the second only toward it, so the second ring's spots straight away
    // from the seat and square to either side are the first that fit.
    constexpr double RingStepCm = 30.0;
    const double Radius = GetDefault<ADeepSpaceCharacter>()->GetCapsuleComponent()->GetScaledCapsuleRadius();

    // 4. A table beside the spot, its edge 45 cm from it on the side away
    //    from the chart: the second ring's first spot is over the table top,
    //    and the first clear floor is square to either side.
    {
        const FVector2D Stood(ChartSeat.X - 124.0, 0.0);
        const double TableEdge = Stood.X - 45.0;
        constexpr double TableTop = 79.0;
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood);
        if (TestNotNull(TEXT("the player spawns by the table"), Player))
        {
            Player->UseScreen(Chart);
            Player->PlaceCamera(0.016f, Player->GetViewRotation());

            AActor* Taken = SpawnBlock(World, FVector(Stood, 115.0), FVector(20.0, 20.0, 85.0));
            AActor* Table = SpawnBlock(World, FVector(TableEdge - 150.0, 0.0, TableTop * 0.5),
                                       FVector(150.0, 300.0, TableTop * 0.5));

            Player->StopUsingScreen();
            CheckStanding(*this, TEXT("beside a table"), World, Player, ChartSeat);
            TestTrue(FString::Printf(TEXT("beside a table: beside it, not on it (x %.0f, edge at %.0f)"),
                                     Player->GetActorLocation().X, TableEdge),
                     Player->GetActorLocation().X - Radius > TableEdge - 1.0);
            TestTrue(TEXT("beside a table: near where they stood"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) < 2.0 * RingStepCm + 1.0);

            Taken->Destroy();
            Table->Destroy();
            Player->Destroy();
        }
    }

    // 5. A chart near an edge where the deck steps 60 cm down, just short of
    //    the second ring on the side away from the chart. A body stood in the
    //    drop would fit and be reachable; it is still the wrong answer,
    //    because it is not the floor the seat is on. The edge is set so a
    //    capsule on that spot clears it by 6 cm, which is what makes the
    //    wrong answer available at all. Off the main floor, which would
    //    otherwise fill the drop.
    {
        constexpr double DropDepth = 60.0;
        AShipNavScreen* EdgeChart = World->SpawnActor<AShipNavScreen>(FVector(0.0, -3000.0, 105.0),
                                                                      FRotator::ZeroRotator);
        if (TestNotNull(TEXT("the chart by the edge spawns"), EdgeChart))
        {
            const FVector EdgeSeat = SpawnChairUnder(World, EdgeChart, ChairCushionCm);
            const FVector2D Stood(EdgeSeat.X - 124.0, EdgeSeat.Y);
            const double DeckEdge = Stood.X - (2.0 * RingStepCm - Radius - 6.0);
            const double DeckFar = EdgeChart->GetActorLocation().X + 100.0;
            SpawnBlock(World, FVector((DeckEdge + DeckFar) * 0.5, Stood.Y, -10.0),
                       FVector((DeckFar - DeckEdge) * 0.5, 300.0, 10.0));
            SpawnBlock(World, FVector(DeckEdge - 300.0, Stood.Y, -DropDepth - 10.0), FVector(300.0, 300.0, 10.0));

            ADeepSpaceCharacter* Player = SpawnStanding(World, Stood);
            if (TestNotNull(TEXT("the player spawns by the edge"), Player))
            {
                Player->UseScreen(EdgeChart);
                Player->PlaceCamera(0.016f, Player->GetViewRotation());

                AActor* Taken = SpawnBlock(World, FVector(Stood, 115.0), FVector(20.0, 20.0, 85.0));

                Player->StopUsingScreen();
                CheckStanding(*this, TEXT("by an edge"), World, Player, EdgeSeat);
                TestTrue(FString::Printf(TEXT("by an edge: on the deck, not below its edge (x %.0f, edge at %.0f)"),
                                         Player->GetActorLocation().X, DeckEdge),
                         Player->GetActorLocation().X > DeckEdge);

                Taken->Destroy();
                Player->Destroy();
            }
        }
    }

    // 6. The developer's own report, again: "it puts me on top and i have to
    //    crouch to get off." Jump is bound, so a player can climb onto the
    //    chart's chair and sit down from up there. The spot they left is
    //    then the cushion, which is clear and fits a body -- and is not
    //    floor. They must stand up beside the chair, on the floor.
    {
        const FVector2D Stood(ChartSeat.X - 5.0, 0.0);
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood, ChartSeat.Z);
        if (TestNotNull(TEXT("the player spawns on the chair"), Player))
        {
            Player->UseScreen(Chart);
            Player->PlaceCamera(0.016f, Player->GetViewRotation());

            Player->StopUsingScreen();
            CheckStanding(*this, TEXT("climbed onto the chair"), World, Player, ChartSeat);
            TestTrue(TEXT("climbed onto the chair: beside it"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) < 150.0);
            Player->Destroy();
        }
    }

    // 7. The same climb at the laptop. Its bench is 45 cm (BenchCm), the
    //    galley's benches in the level, and lower than
    //    the chart's chair: a floor band that let anything up to 50 cm count
    //    as floor would stand this player on the bench, which is the
    //    developer's symptom moved to the laptop.
    {
        const FVector2D Stood(LaptopSeat.X - 5.0, LaptopSeat.Y);
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood, LaptopSeat.Z);
        if (TestNotNull(TEXT("the player spawns on the laptop's bench"), Player))
        {
            Player->UseScreen(Laptop);
            Player->PlaceCamera(0.016f, Player->GetViewRotation());

            Player->StopUsingScreen();
            CheckStanding(*this, TEXT("climbed onto the laptop's bench"), World, Player, LaptopSeat);
            TestTrue(TEXT("climbed onto the laptop's bench: beside it"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) < 150.0);
            Player->Destroy();
        }
    }

    // 8. A low plinth where the player stood, 12 cm: a deck plate, a step up
    //    to a console, a crate lid. Walking steps onto it without a thought,
    //    and it is still not the floor the seat is on -- the band's upper
    //    edge is 5 cm because a floor is flat to within a few centimetres
    //    and anything taller is something stood on. This pins that edge
    //    close to its value, where the chair and the bench pin it only
    //    below 45 cm. 40 cm square, so the second ring clears it.
    {
        constexpr double PlinthHeight = 12.0;
        const FVector2D Stood(ChartSeat.X - 124.0, 0.0);
        AActor* Plinth = SpawnBlock(World, FVector(Stood, PlinthHeight * 0.5),
                                    FVector(20.0, 20.0, PlinthHeight * 0.5));
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood, PlinthHeight);
        if (TestNotNull(TEXT("the player spawns on the plinth"), Player))
        {
            Player->UseScreen(Chart);
            Player->PlaceCamera(0.016f, Player->GetViewRotation());

            Player->StopUsingScreen();
            CheckStanding(*this, TEXT("stood on a low plinth"), World, Player, ChartSeat);
            TestTrue(TEXT("stood on a low plinth: beside it"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) < 150.0);
            Player->Destroy();
        }
        Plinth->Destroy();
    }

    // 9 and 10. Reaching a spot is walking to it, not seeing it. The spot is
    //    taken and fenced in by furniture 75 cm high -- table height, below
    //    the capsule's centre, so a line at that height sees over all of it
    //    -- with one gap, on +Y. The first ring that fits is outside the
    //    fence, so whether the body gets out there says whether the reach
    //    sweep fits through the gap. The gap is 4 cm either side of a body's
    //    width: narrower, and no spot can be walked to; wider, and the spot
    //    straight through the gap must be. Between them they pin the sweep
    //    at the standing capsule's own size -- a thinner probe fits gaps no
    //    body fits, a fatter one refuses doorways a body walks through.
    //    The fence is 8 cm thick, so the spot through the gap clears it.
    auto StandInsideFence = [&](const TCHAR* What, double Gap) -> TOptional<FVector>
    {
        const FVector2D Stood(ChartSeat.X - 124.0, 0.0);
        constexpr double Inner = 45.0;
        constexpr double Thick = 8.0;
        constexpr double Height = 75.0;
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood);
        if (!TestNotNull(FString::Printf(TEXT("%s: the player spawns inside the fence"), What), Player))
        {
            return {};
        }
        Player->UseScreen(Chart);
        Player->PlaceCamera(0.016f, Player->GetViewRotation());

        TArray<AActor*> Placed;
        Placed.Add(SpawnBlock(World, FVector(Stood, 115.0), FVector(20.0, 20.0, 85.0)));
        const double Mid = Inner + Thick * 0.5;
        const double Span = Inner + Thick;
        for (const double Side : {-1.0, 1.0})
        {
            Placed.Add(SpawnBlock(World, FVector(Stood.X + Side * Mid, Stood.Y, Height * 0.5),
                                  FVector(Thick * 0.5, Span, Height * 0.5)));
        }
        Placed.Add(SpawnBlock(World, FVector(Stood.X, Stood.Y - Mid, Height * 0.5),
                              FVector(Span, Thick * 0.5, Height * 0.5)));
        // The side with the gap: two lengths, Gap apart.
        const double Piece = (2.0 * Span - Gap) * 0.5;
        for (const double Side : {-1.0, 1.0})
        {
            Placed.Add(SpawnBlock(World, FVector(Stood.X + Side * (Gap * 0.5 + Piece * 0.5), Stood.Y + Mid,
                                                 Height * 0.5),
                                  FVector(Piece * 0.5, Thick * 0.5, Height * 0.5)));
        }

        Player->StopUsingScreen();
        const FVector At = Player->GetActorLocation();
        const FVector Offset(At.X - Stood.X, At.Y - Stood.Y, 0.0);
        const bool bOutside = FMath::Abs(Offset.X) >= Inner || FMath::Abs(Offset.Y) >= Inner;
        if (bOutside)
        {
            CheckStanding(*this, What, World, Player, ChartSeat);
        }

        for (AActor* Actor : Placed)
        {
            Actor->Destroy();
        }
        Player->Destroy();
        return Offset;
    };

    {
        const double Gap = 2.0 * Radius - 4.0;
        TestTrue(TEXT("fenced: the fence's gap is narrower than a body"), Gap < 2.0 * Radius);
        if (const TOptional<FVector> Offset = StandInsideFence(TEXT("fenced, gap narrower than a body"), Gap))
        {
            TestTrue(FString::Printf(TEXT("fenced, gap narrower than a body: not across the furniture "
                                          "(%.0f, %.0f from where they stood)"),
                                     Offset->X, Offset->Y),
                     FMath::Abs(Offset->X) < 45.0 && FMath::Abs(Offset->Y) < 45.0);
        }
    }
    {
        const double Gap = 2.0 * Radius + 4.0;
        if (const TOptional<FVector> Offset = StandInsideFence(TEXT("fenced, gap wider than a body"), Gap))
        {
            TestTrue(FString::Printf(TEXT("fenced, gap wider than a body: out through the gap "
                                          "(%.0f, %.0f from where they stood)"),
                                     Offset->X, Offset->Y),
                     Offset->Y > 45.0 + 8.0 && FMath::Abs(Offset->X) < Gap * 0.5);
        }
    }

    // 11. Sitting, zooming, going back and standing move the view up to 60 cm
    //    in one frame. The camera manager must be told each is a cut, or
    //    temporal AA and motion blur build that frame from a history of
    //    somewhere else.
    //    Its own chart, with no chair block under it: this character has no
    //    mesh, so its eyes are at its feet, and at the chair they would be
    //    inside the block, where no look reaches the glass to zoom it.
    {
        AShipNavScreen* CutChart = World->SpawnActor<AShipNavScreen>(FVector(600.0, -1500.0, 105.0), FRotator::ZeroRotator);
        const FVector2D Stood(CutChart->GetUseTransform().GetLocation().X - 124.0, -1500.0);
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood);
        APlayerController* Controller = World->SpawnActor<APlayerController>();
        if (TestNotNull(TEXT("the player spawns with a controller"), Player) &&
            TestNotNull(TEXT("and the controller spawns"), Controller))
        {
            // The world is not initialised for play, so nothing has spawned
            // the camera manager yet; the controller's own call does.
            if (!Controller->PlayerCameraManager)
            {
                Controller->SpawnPlayerCameraManager();
            }
            Controller->Possess(Player);
            APlayerCameraManager* Camera = Controller->PlayerCameraManager;
            if (TestNotNull(TEXT("the controller has a camera manager"), Camera) &&
                TestTrue(TEXT("and possesses the player"), Player->GetController() == Controller))
            {
                // The viewport clears it after drawing; headless, nothing
                // draws, so each step starts it clear by hand.
                Camera->bGameCameraCutThisFrame = false;
                Player->UseScreen(CutChart);
                TestTrue(TEXT("sitting down is a camera cut"), Camera->bGameCameraCutThisFrame);

                // E, looking at the chart: the zoom. Looked at by hand:
                // UseScreen aims from where a real body's eyes settle, and
                // this one's, meshless, are at its feet.
                Controller->SetControlRotation(
                    (CutChart->GetScreen()->GetComponentLocation() - Player->GetEyeLocation()).Rotation());
                Player->PlaceCamera(0.016f, Player->GetViewRotation());
                Camera->bGameCameraCutThisFrame = false;
                Player->PressInteract();
                TestTrue(TEXT("zooming the chart is a camera cut"), Camera->bGameCameraCutThisFrame);
                TestTrue(TEXT("and the camera is at the screen in that same frame"),
                         FVector::Dist(Player->GetEyeLocation(), CutChart->GetViewTransform().GetLocation()) < 1.0);

                Camera->bGameCameraCutThisFrame = false;
                Player->PlaceCamera(0.016f, Player->GetViewRotation());
                TestFalse(TEXT("a frame sat still is not a cut"), Camera->bGameCameraCutThisFrame);

                Player->PressInteract();
                TestTrue(TEXT("going back to the seat is a camera cut"), Camera->bGameCameraCutThisFrame);
                TestTrue(TEXT("and the camera has left the screen in that same frame"),
                         FVector::Dist(Player->GetEyeLocation(), CutChart->GetViewTransform().GetLocation()) > 30.0);

                Player->PressInteract();
                Camera->bGameCameraCutThisFrame = false;
                Player->StopUsingScreen();
                TestTrue(TEXT("standing up from a zoom is a camera cut"), Camera->bGameCameraCutThisFrame);
            }
            Controller->UnPossess();
            Controller->Destroy();
            Player->Destroy();
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
