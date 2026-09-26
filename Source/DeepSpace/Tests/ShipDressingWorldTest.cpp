#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/StringOutputDevice.h"
#include "Ship/ShipDressing.h"
#include "Ship/ShipDressingConfig.h"
#include "Ship/ShipDressingKeepOut.h"
#include "Ship/ShipDressingSubsystem.h"
#include "Ship/ShipDressingSurface.h"
#include "Tests/DressingTestFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipDressingWorldTest,
    "DeepSpace.Ship.Dressing.World",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** A marker as Tools/build_hauler.py leaves one. */
    AShipDressingSurface* SpawnMarker(UWorld* World, const FDressSurface& Surface, bool bTagged)
    {
        AShipDressingSurface* Marker = World->SpawnActor<AShipDressingSurface>(
            Surface.ToWorld.GetLocation(), Surface.ToWorld.GetRotation().Rotator());
        Marker->Room = Surface.Room;
        Marker->Kind = Surface.Kind;
        Marker->Ordinal = Surface.Ordinal;
        Marker->Size = Surface.Size;
        Marker->Back = Surface.Back;
        Marker->Use = Surface.Use;
        Marker->Clear = Surface.Clear;
        Marker->Excludes = Surface.Excludes;
        if (bTagged)
        {
            Marker->Tags.Add(ShipDressingTags::SurfaceTag);
        }
        return Marker;
    }

    AShipDressingKeepOut* SpawnKeepOut(UWorld* World, const FBox& Box, bool bTagged)
    {
        AShipDressingKeepOut* Zone = World->SpawnActor<AShipDressingKeepOut>(Box.GetCenter(), FRotator::ZeroRotator);
        Zone->Size = Box.GetSize();
        Zone->Reason = TEXT("probe");
        if (bTagged)
        {
            Zone->Tags.Add(ShipDressingTags::KeepOutTag);
        }
        return Zone;
    }

    /** A furniture part, Static as the level's are, tagged as build_hauler tags one. */
    AStaticMeshActor* SpawnPart(UWorld* World, UStaticMesh* Cube, UMaterialInterface* Material, const FString& Piece, const FVector& Where)
    {
        AStaticMeshActor* Part = World->SpawnActor<AStaticMeshActor>(Where, FRotator::ZeroRotator);
        UStaticMeshComponent* Mesh = Part->GetStaticMeshComponent();
        Mesh->SetMobility(EComponentMobility::Static);
        Mesh->SetStaticMesh(Cube);
        Mesh->SetMaterial(0, Material);
        Part->Tags.Add(ShipDressingTags::WearTag);
        Part->Tags.Add(FName(*(ShipDressingTags::PieceTagPrefix + Piece)));
        return Part;
    }

    const UMaterialInterface* Wearing(const AStaticMeshActor* Part)
    {
        return Part->GetStaticMeshComponent()->GetMaterial(0);
    }

    TArray<AActor*> ClutterActors(UWorld* World)
    {
        TArray<AActor*> Out;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->ActorHasTag(ShipDressingTags::ClutterTag))
            {
                Out.Add(*It);
            }
        }
        return Out;
    }

    /** Every instance as it is drawn: its mesh's measured box through its
     *  instance transform, world. */
    TArray<FBox> InstanceBoxes(const AActor* Clutter, TArray<const UInstancedStaticMeshComponent*>* OutLayers = nullptr)
    {
        TArray<FBox> Out;
        if (!Clutter)
        {
            return Out;
        }
        TInlineComponentArray<UInstancedStaticMeshComponent*> Layers(Clutter);
        for (const UInstancedStaticMeshComponent* Layer : Layers)
        {
            if (OutLayers)
            {
                OutLayers->Add(Layer);
            }
            const FBox MeshBox = Layer->GetStaticMesh()->GetBoundingBox();
            for (int32 I = 0; I < Layer->GetInstanceCount(); ++I)
            {
                FTransform Instance;
                Layer->GetInstanceTransform(I, Instance, /*bWorldSpace*/ true);
                Out.Add(MeshBox.TransformBy(Instance));
            }
        }
        return Out;
    }

    bool Near(const FBox& A, const FBox& B, double Tolerance)
    {
        return (A.Min - B.Min).GetAbsMax() <= Tolerance && (A.Max - B.Max).GetAbsMax() <= Tolerance;
    }

    /** A CVar set for the length of a scope, then put back. */
    struct FScopedCVar
    {
        IConsoleVariable* Var;
        FString Was;
        FScopedCVar(const TCHAR* Name) : Var(IConsoleManager::Get().FindConsoleVariable(Name)), Was(Var ? Var->GetString() : FString()) {}
        void Set(const FString& Value) const { if (Var) { Var->Set(*Value, ECVF_SetByConsole); } }
        ~FScopedCVar() { Set(Was); }
    };
}

/**
 * The subsystem dresses a game world from tagged markers and nothing else,
 * as one transient actor of instances whose every part lands exactly where
 * the core's plan puts it -- measured from the meshes' own bounds, so the
 * pivot bug cannot come back in instanced form (ADR 0006's warning). It
 * wears whole pieces, keeps out of the tagged keep-outs, and redresses in
 * place, idempotently, when ds.Dress.* is touched (lived-in decision 1d).
 */
bool FShipDressingWorldTest::RunTest(const FString& Parameters)
{
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* Furniture = LoadObject<UMaterialInterface>(nullptr, *UShipDressingSubsystem::MaterialPath(TEXT("furniture")));
    // Every asset the test needs, before there is a world to leave half made:
    // a missing material returning early from inside the world's scope left
    // a game world that never began play to be ended, and the run hung.
    UMaterialInterface* Faded = LoadObject<UMaterialInterface>(nullptr, *UShipDressingSubsystem::MaterialPath(UShipDressingSubsystem::WearRole(EDressWear::Faded)));
    UMaterialInterface* Replaced = LoadObject<UMaterialInterface>(nullptr, *UShipDressingSubsystem::MaterialPath(UShipDressingSubsystem::WearRole(EDressWear::Replaced)));
    if (!TestNotNull(TEXT("the cube"), Cube) || !TestNotNull(TEXT("MI_Ship_furniture, from build_hauler.py"), Furniture)
        || !TestNotNull(TEXT("MI_Ship_furniture_faded, from build_hauler.py"), Faded)
        || !TestNotNull(TEXT("MI_Ship_furniture_replaced, from build_hauler.py"), Replaced))
    {
        return false;
    }

    FScopedCVar LivedIn(TEXT("ds.Dress.LivedIn"));
    FScopedCVar SeedVar(TEXT("ds.Dress.Seed"));
    LivedIn.Set(TEXT("1"));
    SeedVar.Set(TEXT("-1"));

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ShipDressingWorldTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        if (World->HasBegunPlay())
        {
            World->EndPlay(EEndPlayReason::RemovedFromWorld);
        }
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
    };

    UShipDressingSubsystem* Dressing = World->GetSubsystem<UShipDressingSubsystem>();
    if (!TestNotNull(TEXT("a game world has a dressing subsystem"), Dressing))
    {
        return false;
    }
    const uint64 Seed = Dressing->GetDressSeed();
    const FShipDressingRules Rules = Dressing->GetRules();

    // Two pieces: one the world's seed wears, one it leaves standard.
    FString WornPiece, PlainPiece;
    EDressWear WornAs = EDressWear::Standard;
    for (int32 K = 0; K < 200 && (WornPiece.IsEmpty() || PlainPiece.IsEmpty()); ++K)
    {
        const FString Piece = FString::Printf(TEXT("probe_%d"), K);
        const EDressWear Wear = ShipDressing::Wear(Seed, Piece, Rules);
        if (Wear != EDressWear::Standard && WornPiece.IsEmpty())
        {
            WornPiece = Piece;
            WornAs = Wear;
        }
        if (Wear == EDressWear::Standard && PlainPiece.IsEmpty())
        {
            PlainPiece = Piece;
        }
    }
    UMaterialInterface* WornMaterial = WornAs == EDressWear::Faded ? Faded : Replaced;
    if (!TestFalse(TEXT("some piece wears"), WornPiece.IsEmpty()) || !TestFalse(TEXT("and some piece does not"), PlainPiece.IsEmpty()))
    {
        return false;
    }

    // -- the level, as build_hauler.py would leave it, before play begins -----
    const TArray<FDressSurface> Hauler = DressingFixtures::Hauler();
    const auto Find = [&Hauler](const TCHAR* Kind) { return *Hauler.FindByPredicate([Kind](const FDressSurface& S) { return S.Kind == FName(Kind); }); };
    const TArray<FDressSurface> Tagged = { Find(TEXT("counter.top")), Find(TEXT("desk.top")), Find(TEXT("wall_rack.shelf_1")) };
    for (const FDressSurface& Surface : Tagged)
    {
        SpawnMarker(World, Surface, true);
    }
    // Untagged: a marker, and a keep-out over the whole desk. Neither may count.
    FDressSurface Stray = Find(TEXT("counter.top"));
    Stray.Ordinal = 9;
    Stray.ToWorld.SetLocation(FVector(840, 800, 90));
    SpawnMarker(World, Stray, false);
    SpawnKeepOut(World, FBox::BuildAABB(FVector(930, -160, 100), FVector(100, 100, 100)), false);
    // Tagged: a keep-out over the counter's +y half, the half it is used from.
    const FBox KeepOut = FBox(FVector(790, 280, 0), FVector(890, 480, 200));
    SpawnKeepOut(World, KeepOut, true);

    AStaticMeshActor* WornA = SpawnPart(World, Cube, Furniture, WornPiece, FVector(0, 0, 50));
    AStaticMeshActor* WornB = SpawnPart(World, Cube, Furniture, WornPiece, FVector(0, 200, 50));
    AStaticMeshActor* Plain = SpawnPart(World, Cube, Furniture, PlainPiece, FVector(0, 400, 50));

    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    World->GetWorldSettings()->NotifyBeginPlay();

    // -- one actor, the plan for the tagged markers only ----------------------
    TArray<AActor*> Actors = ClutterActors(World);
    TestEqual(TEXT("there is exactly one Dress.Clutter actor"), Actors.Num(), 1);
    AActor* Clutter = Dressing->GetClutter();
    TestTrue(TEXT("and it is the subsystem's"), Actors.Num() == 1 && Actors[0] == Clutter);
    TestTrue(TEXT("transient, so it is never saved into a level"), Clutter && Clutter->HasAnyFlags(RF_Transient));

    const TArray<FBox> Zones = { KeepOut };
    const TArray<FDressItem> Plan = ShipDressing::Dress(Tagged, Zones, Seed, Rules);
    TArray<FBox> Expected;
    for (const FDressItem& Item : Plan)
    {
        for (const FDressPart& Part : Rules.FindTemplate(Item.Template)->Parts)
        {
            Expected.Add(FBox(Part.At - Part.Size * 0.5, Part.At + Part.Size * 0.5).TransformBy(Item.World));
        }
    }
    TestTrue(FString::Printf(TEXT("the plan has something to draw (%d things)"), Plan.Num()), Plan.Num() > 0);
    TestEqual(TEXT("one instance per part of every thing the core planned for the tagged markers"),
              Dressing->GetInstanceCount(), Expected.Num());

    TArray<const UInstancedStaticMeshComponent*> Layers;
    const TArray<FBox> Drawn = InstanceBoxes(Clutter, &Layers);
    for (const UInstancedStaticMeshComponent* Layer : Layers)
    {
        TestTrue(FString::Printf(TEXT("%s has no collision: scenery, out of every trace"), *Layer->GetName()),
                 Layer->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        TestTrue(FString::Printf(TEXT("%s is Movable, as is its root"), *Layer->GetName()),
                 Layer->Mobility == EComponentMobility::Movable && Clutter->GetRootComponent()->Mobility == EComponentMobility::Movable);
        TestTrue(FString::Printf(TEXT("%s wears a material the level build made"), *Layer->GetName()),
                 Layer->GetMaterial(0) && Layer->GetMaterial(0)->GetPathName().StartsWith(TEXT("/Game/Materials/MI_Ship_")));
    }

    // Each instance, measured from its mesh and its transform, is exactly a
    // part the plan put there, and lies inside its marker's column.
    int32 Unplanned = 0, Outside = 0, InZone = 0;
    for (const FBox& Box : Drawn)
    {
        Unplanned += !Expected.ContainsByPredicate([&Box](const FBox& Want) { return Near(Box, Want, 0.05); });
        const bool bInside = Tagged.ContainsByPredicate([&Box](const FDressSurface& S)
        {
            const FBox Local = Box.InverseTransformBy(S.ToWorld);
            return Local.Min.X >= -0.5 * S.Size.X - 0.05 && Local.Max.X <= 0.5 * S.Size.X + 0.05
                && Local.Min.Y >= -0.5 * S.Size.Y - 0.05 && Local.Max.Y <= 0.5 * S.Size.Y + 0.05
                && Local.Min.Z >= -0.05 && Local.Max.Z <= S.Clear + 0.05;
        });
        Outside += !bInside;
        InZone += Box.Min.X < KeepOut.Max.X - 0.05 && KeepOut.Min.X < Box.Max.X - 0.05
               && Box.Min.Y < KeepOut.Max.Y - 0.05 && KeepOut.Min.Y < Box.Max.Y - 0.05
               && Box.Min.Z < KeepOut.Max.Z - 0.05 && KeepOut.Min.Z < Box.Max.Z - 0.05;
    }
    TestEqual(TEXT("every instance is a part the plan placed, where it placed it, whatever the mesh's pivot"), Unplanned, 0);
    TestEqual(TEXT("every instance lies within its marker's surface and under its clear"), Outside, 0);
    TestEqual(TEXT("and none in the tagged keep-out"), InZone, 0);
    const bool bDeskDressed = Plan.ContainsByPredicate([](const FDressItem& I) { return I.Surface == 1; });
    TestTrue(TEXT("the untagged keep-out over the desk keeps nothing off it"), bDeskDressed);

    // -- wear ---------------------------------------------------------------------
    TestTrue(FString::Printf(TEXT("%s wears %s"), *WornPiece, *GetNameSafe(WornMaterial)), Wearing(WornA) == WornMaterial);
    TestTrue(TEXT("and both of its parts wear it: a piece wears whole"), Wearing(WornB) == Wearing(WornA));
    TestTrue(FString::Printf(TEXT("%s keeps the level's furniture material"), *PlainPiece), Wearing(Plain) == Furniture);

    // -- redressing ------------------------------------------------------------------
    TArray<FTransform> Before;
    for (const UInstancedStaticMeshComponent* Layer : Layers)
    {
        for (int32 I = 0; I < Layer->GetInstanceCount(); ++I)
        {
            FTransform T;
            Layer->GetInstanceTransform(I, T, true);
            Before.Add(T);
        }
    }
    Dressing->Redress();
    Dressing->Redress();
    TestEqual(TEXT("redressed twice, there is still one clutter actor"), ClutterActors(World).Num(), 1);
    TArray<FTransform> After;
    {
        TArray<const UInstancedStaticMeshComponent*> Again;
        InstanceBoxes(Dressing->GetClutter(), &Again);
        for (const UInstancedStaticMeshComponent* Layer : Again)
        {
            for (int32 I = 0; I < Layer->GetInstanceCount(); ++I)
            {
                FTransform T;
                Layer->GetInstanceTransform(I, T, true);
                After.Add(T);
            }
        }
    }
    bool bSame = Before.Num() == After.Num();
    for (int32 I = 0; bSame && I < Before.Num(); ++I)
    {
        bSame = Before[I].Equals(After[I], 1e-6);
    }
    TestTrue(TEXT("with identical instance transforms"), bSame);
    TestTrue(TEXT("and the worn piece is worn once, not swapped from a swap"), Wearing(WornA) == WornMaterial);

    // ds.Dress.LivedIn redresses in place: at 0 the ship is bare.
    LivedIn.Set(TEXT("0"));
    TestEqual(TEXT("LivedIn 0 redresses the running world bare"), Dressing->GetInstanceCount(), 0);
    TestEqual(TEXT("with no clutter actor left behind"), ClutterActors(World).Num(), 0);
    LivedIn.Set(TEXT("2"));
    TestTrue(FString::Printf(TEXT("LivedIn 2 is busier than 1 (%d instances, was %d)"), Dressing->GetInstanceCount(), Expected.Num()),
             Dressing->GetInstanceCount() > Expected.Num());
    LivedIn.Set(TEXT("1"));
    TestEqual(TEXT("and back at 1 it is the same ship"), Dressing->GetInstanceCount(), Expected.Num());

    // ds.Dress.Seed redresses in place, and restores the wear it undoes.
    int32 Other = -1;
    for (int32 Root = 1; Root < 500 && Other < 0; ++Root)
    {
        if (ShipDressing::Wear(ShipDressing::DressSeed(Root), WornPiece, Rules) == EDressWear::Standard)
        {
            Other = Root;
        }
    }
    SeedVar.Set(FString::FromInt(Other));
    TestEqual(TEXT("another seed is another ship"), Dressing->GetDressSeed(), ShipDressing::DressSeed(static_cast<uint64>(Other)));
    TestTrue(TEXT("under which the worn piece is back in the level's material, both parts"),
             Wearing(WornA) == Furniture && Wearing(WornB) == Furniture);
    SeedVar.Set(TEXT("-1"));
    TestEqual(TEXT("-1 goes back to the world's own seed"), Dressing->GetDressSeed(), Seed);
    TestTrue(TEXT("and its wear"), Wearing(WornA) == WornMaterial && Wearing(WornB) == WornMaterial);

    // ds.Dress.Reload re-reads the file, not the cache, and redresses in
    // place. An edit that lives only in the cache bares every tagged kind;
    // the file has no such line, so a reload that read the file brings the
    // ship back as it was, and one that read the cache leaves it bare.
    {
        ON_SCOPE_EXIT { UShipDressingConfig::ReloadFromIni(); };
        const FString Section = UShipDressingConfig::StaticClass()->GetPathName();
        GConfig->SetArray(*Section, TEXT("Kinds"),
                          { TEXT("(Kind=\"counter.top\",Lambda=0)"), TEXT("(Kind=\"desk.top\",Lambda=0)"), TEXT("(Kind=\"wall_rack\",Lambda=0)") },
                          GGameIni);
        TestEqual(TEXT("the cache's edit is taken"), UShipDressingConfig::ApplyConfigCache().Num(), 0);
        Dressing->Redress();
        TestEqual(TEXT("and dresses the ship bare"), Dressing->GetInstanceCount(), 0);

        FStringOutputDevice Out;
        Out.SetAutoEmitLineTerminator(true);
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Dress.Reload"), Out, World);
        AddInfo(Out);
        TestEqual(TEXT("ds.Dress.Reload reads DefaultGame.ini from disk and redresses: the ship is as it was"),
                  Dressing->GetInstanceCount(), Expected.Num());
        TestTrue(TEXT("and says it took the file"), Out.Contains(TEXT("taken")));
    }
    return true;
}

#endif
