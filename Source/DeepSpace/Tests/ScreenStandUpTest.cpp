#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
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

    /** A chair: a box whose top is the screen's seat, under its use transform. */
    FVector SpawnChairUnder(UWorld* World, const AShipScreen* Screen)
    {
        const FVector Seat = Screen->GetUseTransform().GetLocation();
        SpawnBlock(World, FVector(Seat.X, Seat.Y, Seat.Z * 0.5), FVector(30.0, 30.0, Seat.Z * 0.5));
        return Seat;
    }

    ADeepSpaceCharacter* SpawnStanding(UWorld* World, const FVector2D& At)
    {
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        // Feet 2 cm off the floor at Z = 0, as the movement component floats them.
        const float HalfHeight = GetDefault<ADeepSpaceCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        return World->SpawnActor<ADeepSpaceCharacter>(FVector(At.X, At.Y, HalfHeight + 2.0), FRotator::ZeroRotator, Spawn);
    }

    /**
     * Stood up properly: on the floor, overlapping nothing a body collides
     * with, off the chair, and with the eyes somewhere a camera may be.
     */
    void CheckStanding(FAutomationTestBase& Test, const FString& What, UWorld* World,
                       ADeepSpaceCharacter* Player, const FVector& Seat)
    {
        const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
        const FVector Centre = Player->GetActorLocation();
        const float Bottom = Centre.Z - Capsule->GetScaledCapsuleHalfHeight();

        Test.TestTrue(FString::Printf(TEXT("%s: the feet are on the floor (%.1f cm)"), *What, Bottom),
                      Bottom >= -0.1f && Bottom <= 5.0f);

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
    const FVector ChartSeat = SpawnChairUnder(World, Chart);
    const FVector LaptopSeat = SpawnChairUnder(World, Laptop);

    // 1. The spot the player left is free: they go back to it.
    {
        const FVector2D Stood(ChartSeat.X - 124.0, 0.0);
        ADeepSpaceCharacter* Player = SpawnStanding(World, Stood);
        if (TestNotNull(TEXT("the player spawns"), Player))
        {
            Player->UseScreen(Chart);
            TestTrue(TEXT("sitting at the chart puts the body on its chair"),
                     Player->IsUsingScreen() && Player->GetActorLocation().Z > ChartSeat.Z);

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

            // Something arrives where they were standing while they read.
            AActor* Taken = SpawnBlock(World, FVector(Stood, 100.0), FVector(40.0, 40.0, 100.0));
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
                     Player->IsUsingScreen() && Player->GetActorLocation().Z > LaptopSeat.Z);

            Player->StopUsingScreen();
            CheckStanding(*this, TEXT("laptop"), World, Player, LaptopSeat);
            TestTrue(TEXT("laptop: back where they stood"),
                     FVector::Dist2D(Player->GetActorLocation(), FVector(Stood, 0.0)) < 1.0);
            Player->Destroy();
        }
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
