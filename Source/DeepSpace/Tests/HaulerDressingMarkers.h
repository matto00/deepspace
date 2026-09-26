#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Ship/ShipDressing.h"
#include "Ship/ShipDressingKeepOut.h"
#include "Ship/ShipDressingSurface.h"

/**
 * The hauler's real dressing markers, spawned into a test world exactly as
 * Tools/build_hauler.py's place_surfaces and place_keep_outs spawn them into
 * L_Hauler: same classes, same tags, same fields.
 *
 * They come from Tools/dressing_markers.json, which is hauler_layout's
 * generate() written out, and which test_dressing_markers.py holds equal to
 * it. The level itself cannot be loaded here: it is regenerated on main and
 * never committed from a branch. So this is the layout the level is built
 * from, with no hand-typed link, which DressingTestFixtures.h (hand-typed,
 * allowed to lag) is not.
 * Test scaffolding: nothing outside Tests/ may include this.
 */
namespace HaulerDressingMarkers
{
    /** A floor-plan rectangle, world, and where the screen on it stands. */
    struct FScreen
    {
        FVector Location = FVector::ZeroVector;
        FBox2D Exclude = FBox2D(ForceInit);
    };

    struct FMarkers
    {
        TArray<FDressSurface> Surfaces;
        TArray<FName> SurfaceLabels;
        TArray<FBox> KeepOuts;
        TArray<FName> KeepOutLabels;
        FScreen Laptop;
        FScreen Chart;
    };

    inline FVector Vec(const TArray<TSharedPtr<FJsonValue>>& A)
    {
        return FVector(A[0]->AsNumber(), A[1]->AsNumber(), A.Num() > 2 ? A[2]->AsNumber() : 0.0);
    }

    inline FBox2D Rect(const TArray<TSharedPtr<FJsonValue>>& A)
    {
        const FVector Lo = Vec(A[0]->AsArray());
        const FVector Hi = Vec(A[1]->AsArray());
        return FBox2D(FVector2D(Lo.X, Lo.Y), FVector2D(Hi.X, Hi.Y));
    }

    inline FScreen Screen(const FJsonObject& Object)
    {
        FScreen Out;
        Out.Location = Vec(Object.GetArrayField(TEXT("location")));
        Out.Exclude = Rect(Object.GetArrayField(TEXT("exclude")));
        return Out;
    }

    /** False, with Error said, if the file is missing or not the shape
     *  Tools/dressing_markers.py writes. */
    inline bool Load(FMarkers& Out, FString& Error)
    {
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/dressing_markers.json"));
        FString Text;
        TSharedPtr<FJsonObject> Root;
        if (!FFileHelper::LoadFileToString(Text, *Path)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            Error = FString::Printf(TEXT("%s does not load: run python3 Tools/dressing_markers.py"), *Path);
            return false;
        }

        const TMap<FString, EDressEdge> Edges = { { TEXT("-x"), EDressEdge::NegX }, { TEXT("+x"), EDressEdge::PosX } };
        const TMap<FString, EDressUse> Uses = {
            { TEXT("centre"), EDressUse::Centre }, { TEXT("+y"), EDressUse::PosY }, { TEXT("-y"), EDressUse::NegY } };
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("surfaces")))
        {
            const FJsonObject& S = *Value->AsObject();
            const EDressEdge* Back = Edges.Find(S.GetStringField(TEXT("back")));
            const EDressUse* Use = Uses.Find(S.GetStringField(TEXT("use")));
            if (!Back || !Use)
            {
                Error = FString::Printf(TEXT("%s: an edge or use the markers cannot carry"), *S.GetStringField(TEXT("label")));
                return false;
            }
            FDressSurface& Surface = Out.Surfaces.AddDefaulted_GetRef();
            Surface.Room = FName(*S.GetStringField(TEXT("room")));
            Surface.Kind = FName(*S.GetStringField(TEXT("kind")));
            Surface.Ordinal = static_cast<int32>(S.GetNumberField(TEXT("ordinal")));
            Surface.ToWorld = FTransform(FRotator(0.0, S.GetNumberField(TEXT("yaw")), 0.0).Quaternion(),
                                         Vec(S.GetArrayField(TEXT("location"))));
            const FVector Size = Vec(S.GetArrayField(TEXT("size")));
            Surface.Size = FVector2D(Size.X, Size.Y);
            Surface.Back = *Back;
            Surface.Use = *Use;
            Surface.Clear = static_cast<float>(S.GetNumberField(TEXT("clear")));
            for (const TSharedPtr<FJsonValue>& Exclude : S.GetArrayField(TEXT("excludes")))
            {
                Surface.Excludes.Add(Rect(Exclude->AsArray()));
            }
            Out.SurfaceLabels.Add(FName(*S.GetStringField(TEXT("label"))));
        }
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("keep_outs")))
        {
            const FJsonObject& K = *Value->AsObject();
            Out.KeepOuts.Add(FBox(Vec(K.GetArrayField(TEXT("lo"))), Vec(K.GetArrayField(TEXT("hi")))));
            Out.KeepOutLabels.Add(FName(*K.GetStringField(TEXT("label"))));
        }
        Out.Laptop = Screen(*Root->GetObjectField(TEXT("laptop")));
        Out.Chart = Screen(*Root->GetObjectField(TEXT("chart")));
        if (Out.Surfaces.IsEmpty() || Out.KeepOuts.IsEmpty())
        {
            Error = TEXT("dressing_markers.json has no surfaces or no keep-outs");
            return false;
        }
        return true;
    }

    /**
     * place_surfaces and place_keep_outs, in C++: every marker spawned and
     * tagged as the level build leaves it. Call before World->BeginPlay(),
     * which is when the dressing subsystem looks for them.
     */
    inline void Spawn(UWorld* World, const FMarkers& Markers)
    {
        for (const FDressSurface& Surface : Markers.Surfaces)
        {
            AShipDressingSurface* Marker = World->SpawnActor<AShipDressingSurface>(
                Surface.ToWorld.GetLocation(), Surface.ToWorld.GetRotation().Rotator());
            Marker->Tags.Add(ShipDressingTags::SurfaceTag);
            Marker->Room = Surface.Room;
            Marker->Kind = Surface.Kind;
            Marker->Ordinal = Surface.Ordinal;
            Marker->Size = Surface.Size;
            Marker->Back = Surface.Back;
            Marker->Use = Surface.Use;
            Marker->Clear = Surface.Clear;
            Marker->Excludes = Surface.Excludes;
        }
        for (int32 Index = 0; Index < Markers.KeepOuts.Num(); ++Index)
        {
            const FBox& Box = Markers.KeepOuts[Index];
            AShipDressingKeepOut* Zone = World->SpawnActor<AShipDressingKeepOut>(Box.GetCenter(), FRotator::ZeroRotator);
            Zone->Tags.Add(ShipDressingTags::KeepOutTag);
            Zone->Reason = Markers.KeepOutLabels[Index];
            Zone->Size = Box.GetSize();
        }
    }

    /** Everything drawn, as (mesh | material) -> every instance's world
     *  transform in the order it was added: two ships are the same ship when
     *  this is equal, whatever order their components were made in. */
    inline TMap<FString, TArray<FTransform>> Drawing(const AActor* Clutter)
    {
        TMap<FString, TArray<FTransform>> Out;
        if (!Clutter)
        {
            return Out;
        }
        TInlineComponentArray<UInstancedStaticMeshComponent*> Layers(Clutter);
        for (const UInstancedStaticMeshComponent* Layer : Layers)
        {
            TArray<FTransform>& Instances = Out.FindOrAdd(
                GetPathNameSafe(Layer->GetStaticMesh()) + TEXT(" | ") + GetPathNameSafe(Layer->GetMaterial(0)));
            for (int32 I = 0; I < Layer->GetInstanceCount(); ++I)
            {
                FTransform T;
                Layer->GetInstanceTransform(I, T, /*bWorldSpace*/ true);
                Instances.Add(T);
            }
        }
        return Out;
    }

    inline bool SameDrawing(const TMap<FString, TArray<FTransform>>& A, const TMap<FString, TArray<FTransform>>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }
        for (const TPair<FString, TArray<FTransform>>& Layer : A)
        {
            const TArray<FTransform>* Other = B.Find(Layer.Key);
            if (!Other || Other->Num() != Layer.Value.Num())
            {
                return false;
            }
            for (int32 I = 0; I < Layer.Value.Num(); ++I)
            {
                if (!Layer.Value[I].Equals((*Other)[I], 0.0))
                {
                    return false;
                }
            }
        }
        return true;
    }
}
