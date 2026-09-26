#include "Ship/ShipDressingSubsystem.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/OutputDevice.h"
#include "Ship/ShipDressingKeepOut.h"
#include "Ship/ShipDressingSurface.h"
#include "Universe/UniverseSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogShipDressing, Log, All);

namespace
{
    void RedressEveryWorld(IConsoleVariable*);

    // The two numbers a playtest pokes. Read at every dress, never cached,
    // and setting either redresses every running world in place, so the
    // density is judged standing in the galley rather than across a rebuild.
    TAutoConsoleVariable<float> CVarLivedIn(
        TEXT("ds.Dress.LivedIn"), 1.0f,
        TEXT("How lived in the ship looks: scales every surface's mean count. 0.3 freshly moved in, ")
        TEXT("1 lived in, 2 squalid; 0 bare. Setting it redresses."),
        FConsoleVariableDelegate::CreateStatic(&RedressEveryWorld), ECVF_Default);

    TAutoConsoleVariable<int32> CVarSeed(
        TEXT("ds.Dress.Seed"), -1,
        TEXT("Dress as a universe of this root seed would be dressed; -1 for the world's own. Setting it redresses."),
        FConsoleVariableDelegate::CreateStatic(&RedressEveryWorld), ECVF_Default);

    void RedressEveryWorld(IConsoleVariable*)
    {
        if (!GEngine)
        {
            return;
        }
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            UWorld* World = Context.World();
            if (World && World->IsGameWorld() && World->HasBegunPlay())
            {
                if (UShipDressingSubsystem* Dressing = World->GetSubsystem<UShipDressingSubsystem>())
                {
                    Dressing->Redress();
                }
            }
        }
    }

    void RedressCommand(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        int32 Seed = -1;
        if (Args.Num() > 0 && !LexTryParseString(Seed, *Args[0]))
        {
            Out.Logf(TEXT("ds.Dress.Redress: '%s' is not a seed. 'ds.Dress.Redress <seed>', or no argument for the world's own."), *Args[0]);
            return;
        }
        Seed = FMath::Max(-1, Seed);
        if (CVarSeed.GetValueOnGameThread() != Seed)
        {
            CVarSeed->Set(Seed, ECVF_SetByConsole);    // redresses every world
        }
        else if (UShipDressingSubsystem* Dressing = UShipDressingSubsystem::Get(World))
        {
            Dressing->Redress();
        }
        if (const UShipDressingSubsystem* Dressing = UShipDressingSubsystem::Get(World))
        {
            Out.Logf(TEXT("ds.Dress.Redress: %s seed, %d instances."),
                     Seed < 0 ? TEXT("the world's own") : *FString::Printf(TEXT("root %d's"), Seed), Dressing->GetInstanceCount());
        }
    }

    void DescribeCommand(const TArray<FString>&, UWorld* World, FOutputDevice& Out)
    {
        const UShipDressingSubsystem* Dressing = UShipDressingSubsystem::Get(World);
        if (!Dressing)
        {
            Out.Log(TEXT("ds.Dress.Describe: no dressing here; press Play first."));
            return;
        }
        TArray<FString> Lines;
        Dressing->Describe().ParseIntoArrayLines(Lines);
        for (const FString& Line : Lines)
        {
            Out.Log(Line);
        }
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice RedressCmd(
        TEXT("ds.Dress.Redress"),
        TEXT("Redress the ship in place. 'ds.Dress.Redress <seed>' dresses it as that root seed's universe would; ")
        TEXT("no argument goes back to the world's own."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RedressCommand));

    FAutoConsoleCommandWithWorldArgsAndOutputDevice DescribeCmd(
        TEXT("ds.Dress.Describe"),
        TEXT("Every dressing surface and what is on it, one line each."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&DescribeCommand));

    /** The piece a furniture part belongs to, from its Piece.<prop>_<n> tag. */
    FString PieceOf(const AActor& Actor)
    {
        for (const FName& Tag : Actor.Tags)
        {
            const FString Text = Tag.ToString();
            if (Text.StartsWith(ShipDressingTags::PieceTagPrefix))
            {
                return Text.RightChop(ShipDressingTags::PieceTagPrefix.Len());
            }
        }
        return FString();
    }
}

UShipDressingSubsystem* UShipDressingSubsystem::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
    return World ? World->GetSubsystem<UShipDressingSubsystem>() : nullptr;
}

bool UShipDressingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UShipDressingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UUniverseSubsystem>();
    Super::Initialize(Collection);
}

void UShipDressingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    Redress();
}

uint64 UShipDressingSubsystem::GetDressSeed() const
{
    const int32 Override = CVarSeed.GetValueOnGameThread();
    if (Override >= 0)
    {
        return ShipDressing::DressSeed(static_cast<uint64>(Override));
    }
    const UWorld* World = GetWorld();
    const UUniverseSubsystem* Universe = World ? World->GetSubsystem<UUniverseSubsystem>() : nullptr;
    return ShipDressing::DressSeed(Universe ? Universe->GetRootSeed() : FallbackRoot);
}

FShipDressingRules UShipDressingSubsystem::GetRules() const
{
    FShipDressingRules Rules;
    Rules.LivedIn = FMath::Clamp(static_cast<double>(CVarLivedIn.GetValueOnGameThread()), 0.0, DressGuarantees::MaxLivedIn);
    return Rules;
}

void UShipDressingSubsystem::GatherSurfaces(TArray<FDressSurface>& OutSurfaces, TArray<FBox>& OutKeepOut) const
{
    OutSurfaces.Reset();
    OutKeepOut.Reset();
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }
    for (TActorIterator<AShipDressingSurface> It(World); It; ++It)
    {
        if (!It->ActorHasTag(ShipDressingTags::SurfaceTag))
        {
            continue;
        }
        FDressSurface& Surface = OutSurfaces.AddDefaulted_GetRef();
        Surface.Room = It->Room;
        Surface.Kind = It->Kind;
        Surface.Ordinal = It->Ordinal;
        Surface.ToWorld = FTransform(It->GetActorQuat(), It->GetActorLocation());
        Surface.Size = It->Size;
        Surface.Back = It->Back;
        Surface.Use = It->Use;
        Surface.Clear = It->Clear;
        for (const FBox2D& Exclude : It->Excludes)
        {
            // Rebuilt from its corners: a box assigned from Python may arrive
            // without its valid flag, and the corners are the contract.
            Surface.Excludes.Add(FBox2D(Exclude.Min, Exclude.Max));
        }
    }
    for (TActorIterator<AShipDressingKeepOut> It(World); It; ++It)
    {
        if (It->ActorHasTag(ShipDressingTags::KeepOutTag))
        {
            OutKeepOut.Add(It->GetBox());
        }
    }
}

const TCHAR* UShipDressingSubsystem::MeshPath(EDressMesh Mesh)
{
    switch (Mesh)
    {
    case EDressMesh::Chamfer:
        return TEXT("/Game/LevelPrototyping/Meshes/SM_ChamferCube.SM_ChamferCube");
    case EDressMesh::Cylinder:
        return TEXT("/Game/LevelPrototyping/Meshes/SM_Cylinder.SM_Cylinder");
    case EDressMesh::Cube:
    default:
        return TEXT("/Game/LevelPrototyping/Meshes/SM_Cube.SM_Cube");
    }
}

FString UShipDressingSubsystem::MaterialPath(FName Role)
{
    const FString Name = FString::Printf(TEXT("MI_Ship_%s"), *Role.ToString());
    return FString::Printf(TEXT("/Game/Materials/%s.%s"), *Name, *Name);
}

FName UShipDressingSubsystem::WearRole(EDressWear Wear)
{
    switch (Wear)
    {
    case EDressWear::Faded:
        return FName(TEXT("furniture_faded"));
    case EDressWear::Replaced:
        return FName(TEXT("furniture_replaced"));
    case EDressWear::Standard:
    default:
        return NAME_None;
    }
}

AActor* UShipDressingSubsystem::GetClutter() const
{
    return Clutter.Get();
}

int32 UShipDressingSubsystem::GetInstanceCount() const
{
    int32 Count = 0;
    if (const AActor* Actor = Clutter.Get())
    {
        TInlineComponentArray<UInstancedStaticMeshComponent*> Layers(Actor);
        for (const UInstancedStaticMeshComponent* Layer : Layers)
        {
            Count += Layer->GetInstanceCount();
        }
    }
    return Count;
}

void UShipDressingSubsystem::Undress()
{
    if (AActor* Actor = Clutter.Get())
    {
        Actor->Destroy();
    }
    Clutter.Reset();

    for (const FDressWornSlot& Slot : Worn)
    {
        if (UMeshComponent* Mesh = Slot.Mesh.Get())
        {
            Mesh->SetMaterial(Slot.Slot, Slot.Original);
        }
    }
    Worn.Reset();
}

void UShipDressingSubsystem::Redress()
{
    Undress();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const uint64 Seed = GetDressSeed();
    const FShipDressingRules Rules = GetRules();
    ApplyWear(Seed, Rules);

    TArray<FDressSurface> Surfaces;
    TArray<FBox> KeepOut;
    GatherSurfaces(Surfaces, KeepOut);
    const TArray<FDressItem> Plan = ShipDressing::Dress(Surfaces, KeepOut, Seed, Rules);
    if (Plan.IsEmpty())
    {
        return;
    }

    FActorSpawnParameters Params;
    Params.ObjectFlags |= RF_Transient;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
    if (!Actor)
    {
        return;
    }
    Actor->Tags.Add(ShipDressingTags::ClutterTag);
#if WITH_EDITOR
    Actor->SetActorLabel(TEXT("Dress_Clutter"));
#endif

    // Movable throughout: a Static child of a movable root never has its
    // world transform updated (CLAUDE.md), and a root spawned at runtime is
    // an easy place to make that mistake. The ship never moves (ADR 0005),
    // so Movable costs nothing.
    USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    Actor->SetRootComponent(Root);
    Actor->AddInstanceComponent(Root);
    Root->RegisterComponent();

    TMap<TPair<uint8, FName>, UInstancedStaticMeshComponent*> Layers;
    TMap<uint8, FBox> Bounds;
    for (const FDressItem& Item : Plan)
    {
        const FDressTemplate* Template = Rules.FindTemplate(Item.Template);
        if (!Template)
        {
            continue;
        }
        for (const FDressPart& Part : Template->Parts)
        {
            const FName Role = ShipDressing::PartRole(Part, Item.Colour);
            const TPair<uint8, FName> Key(static_cast<uint8>(Part.Mesh), Role);
            UInstancedStaticMeshComponent* Layer = Layers.FindRef(Key);
            if (!Layer)
            {
                UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, MeshPath(Part.Mesh));
                if (!Mesh)
                {
                    continue;
                }
                Layer = NewObject<UInstancedStaticMeshComponent>(Actor);
                Layer->SetMobility(EComponentMobility::Movable);
                Layer->SetupAttachment(Root);
                Layer->SetStaticMesh(Mesh);
                if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath(Role)))
                {
                    Layer->SetMaterial(0, Material);
                }
                else
                {
                    UE_LOG(LogShipDressing, Warning, TEXT("No %s: run Tools/build_hauler.py, which authors every dressing role."),
                           *MaterialPath(Role));
                }
                // Scenery the capsule cannot reach: out of the laptop cursor's
                // trace, the E trace and the camera's sweep alike.
                Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Layer->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
                Layer->SetGenerateOverlapEvents(false);
                Layer->SetCanEverAffectNavigation(false);
                Layer->CanCharacterStepUpOn = ECB_No;
                Actor->AddInstanceComponent(Layer);
                Layer->RegisterComponent();
                Layers.Add(Key, Layer);
                Bounds.Add(Key.Key, Mesh->GetBoundingBox());
            }

            // Scale and offset from the mesh's measured bounds, never an
            // assumed pivot: SM_Cube's is a corner, SM_ChamferCube's its
            // centre, SM_Cylinder's its base. build_hauler.spawn_box's
            // arithmetic, turned with the item.
            const FBox MeshBox = Bounds.FindChecked(Key.Key);
            const FVector Extent = MeshBox.GetSize();
            if (Extent.GetMin() <= UE_SMALL_NUMBER)
            {
                continue;
            }
            const FVector Scale = Part.Size / Extent;
            const FQuat Turn = Item.World.GetRotation();
            const FVector Centre = Item.World.TransformPosition(Part.At);
            const FVector Location = Centre - Turn.RotateVector(MeshBox.GetCenter() * Scale);
            Layer->AddInstance(FTransform(Turn, Location, Scale), /*bWorldSpace*/ true);
        }
    }
    Clutter = Actor;
}

void UShipDressingSubsystem::ApplyWear(uint64 Seed, const FShipDressingRules& Rules)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    // One draw per piece, so a whole desk wears together however many
    // boxes it is built from.
    TMap<FString, TArray<AActor*>> Pieces;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->ActorHasTag(ShipDressingTags::WearTag))
        {
            continue;
        }
        const FString Piece = PieceOf(**It);
        if (!Piece.IsEmpty())
        {
            Pieces.FindOrAdd(Piece).Add(*It);
        }
    }

    TMap<FName, UMaterialInterface*> Materials;
    for (const TPair<FString, TArray<AActor*>>& Piece : Pieces)
    {
        const FName Role = WearRole(ShipDressing::Wear(Seed, Piece.Key, Rules));
        if (Role.IsNone())
        {
            continue;
        }
        UMaterialInterface*& Material = Materials.FindOrAdd(Role);
        if (!Material)
        {
            Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath(Role));
        }
        if (!Material)
        {
            continue;
        }
        for (AActor* Part : Piece.Value)
        {
            TInlineComponentArray<UMeshComponent*> Meshes(Part);
            for (UMeshComponent* Mesh : Meshes)
            {
                for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
                {
                    FDressWornSlot& Record = Worn.AddDefaulted_GetRef();
                    Record.Mesh = Mesh;
                    Record.Slot = Slot;
                    Record.Original = Mesh->GetMaterial(Slot);
                    Mesh->SetMaterial(Slot, Material);
                }
            }
        }
    }
}

FString UShipDressingSubsystem::Describe() const
{
    TArray<FDressSurface> Surfaces;
    TArray<FBox> KeepOut;
    GatherSurfaces(Surfaces, KeepOut);
    const FShipDressingRules Rules = GetRules();
    const TArray<FDressItem> Plan = ShipDressing::Dress(Surfaces, KeepOut, GetDressSeed(), Rules);

    FString Out = FString::Printf(TEXT("Dressing: %d surfaces, %d keep-outs, LivedIn %.2f, %d things.\n"),
                                  Surfaces.Num(), KeepOut.Num(), Rules.LivedIn, Plan.Num());
    for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
    {
        const FDressSurface& Surface = Surfaces[Index];
        TArray<FString> Things;
        for (const FDressItem& Item : Plan)
        {
            if (Item.Surface == Index)
            {
                Things.Add(Item.Colour.IsNone() ? Item.Template.ToString()
                                                : FString::Printf(TEXT("%s (%s)"), *Item.Template.ToString(), *Item.Colour.ToString()));
            }
        }
        const FVector Where = Surface.ToWorld.GetLocation();
        Out += FString::Printf(TEXT("  %s #%d in the %s at (%.0f, %.0f, %.0f): %s\n"), *Surface.Kind.ToString(), Surface.Ordinal,
                               *Surface.Room.ToString(), Where.X, Where.Y, Where.Z,
                               Things.IsEmpty() ? TEXT("bare") : *FString::Join(Things, TEXT(", ")));
    }
    return Out;
}
