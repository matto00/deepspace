#include "Editor/DeepSpaceEditorScripting.h"

#include "Animation/BlendSpace.h"
#include "Engine/StaticMesh.h"
#include "Sky/SkySphereMesh.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#endif

bool UDeepSpaceEditorScripting::RebuildBlendSpace(UBlendSpace* BlendSpace)
{
#if WITH_EDITOR
    if (!BlendSpace)
    {
        return false;
    }
    BlendSpace->ValidateSampleData();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

UStaticMesh* UDeepSpaceEditorScripting::BuildSkySphere(const FString& PackageName, const TArray<int32>& CellsPerLod)
{
#if WITH_EDITOR
    if (CellsPerLod.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName))
    {
        return nullptr;
    }
    UPackage* Package = CreatePackage(*PackageName);
    const FString AssetName = FPackageName::GetShortName(PackageName);
    UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *AssetName);
    if (!Mesh)
    {
        Mesh = LoadObject<UStaticMesh>(Package, *AssetName, nullptr, LOAD_NoWarn | LOAD_Quiet);
    }
    const bool bNew = Mesh == nullptr;
    if (bNew)
    {
        Mesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    }

    Mesh->PreEditChange(nullptr);
    Mesh->SetNumSourceModels(0);
    Mesh->GetStaticMaterials().Reset();
    const FName Slot(TEXT("Body"));
    Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, Slot, Slot));

    TArray<double> EdgeAngles;
    for (int32 Lod = 0; Lod < CellsPerLod.Num(); ++Lod)
    {
        const SkySphereMesh::FGeometry Geometry = SkySphereMesh::Build(CellsPerLod[Lod]);
        EdgeAngles.Add(SkySphereMesh::LongestEdgeAngle(Geometry));

        FStaticMeshSourceModel& Source = Mesh->AddSourceModel();
        // The normals are the sphere's own and must survive the build; there
        // is no lightmap, no distance field and no Lumen card for a thing
        // nothing lights and nothing sees but the eye.
        Source.BuildSettings.bRecomputeNormals = false;
        Source.BuildSettings.bRecomputeTangents = true;
        Source.BuildSettings.bUseMikkTSpace = true;
        Source.BuildSettings.bRemoveDegenerates = false;
        Source.BuildSettings.bGenerateLightmapUVs = false;
        Source.BuildSettings.DistanceFieldResolutionScale = 0.0f;
        Source.BuildSettings.MaxLumenMeshCards = 0;

        FMeshDescription* Description = Mesh->CreateMeshDescription(Lod);
        FStaticMeshAttributes Attributes(*Description);
        Attributes.Register();
        TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
        TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
        TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
        UVs.SetNumChannels(1);

        const FPolygonGroupID Group = Description->CreatePolygonGroup();
        Attributes.GetPolygonGroupMaterialSlotNames()[Group] = Slot;

        // One instance per vertex: the normal and the cube coordinate are the
        // vertex's own, shared by every triangle round it.
        const int32 Count = Geometry.Positions.Num();
        Description->ReserveNewVertices(Count);
        Description->ReserveNewVertexInstances(Count);
        Description->ReserveNewTriangles(Geometry.Indices.Num() / 3);
        TArray<FVertexInstanceID> Instances;
        Instances.Reserve(Count);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FVertexID Vertex = Description->CreateVertex();
            Positions[Vertex] = Geometry.Positions[Index];
            const FVertexInstanceID Instance = Description->CreateVertexInstance(Vertex);
            Normals[Instance] = Geometry.Normals[Index];
            UVs.Set(Instance, 0, Geometry.UVs[Index]);
            Instances.Add(Instance);
        }
        for (int32 Tri = 0; Tri + 2 < Geometry.Indices.Num(); Tri += 3)
        {
            const FVertexInstanceID Corners[3] = {
                Instances[Geometry.Indices[Tri]], Instances[Geometry.Indices[Tri + 1]], Instances[Geometry.Indices[Tri + 2]] };
            Description->CreateTriangle(Group, Corners);
        }
        Mesh->CommitMeshDescription(Lod);
    }

    const TArray<float> Sizes = SkySphereMesh::LodScreenSizes(EdgeAngles);
    Mesh->SetAutoComputeLODScreenSize(false);
    for (int32 Lod = 0; Lod < Sizes.Num(); ++Lod)
    {
        Mesh->GetSourceModel(Lod).ScreenSize.Default = Sizes[Lod];
    }
    FMeshNaniteSettings Nanite = Mesh->GetNaniteSettings();
    Nanite.bEnabled = false;
    Mesh->SetNaniteSettings(Nanite);
    Mesh->bSupportRayTracing = false;

    Mesh->Build(true);
    Mesh->PostEditChange();
    Mesh->MarkPackageDirty();
    if (bNew)
    {
        FAssetRegistryModule::AssetCreated(Mesh);
    }
    return Mesh;
#else
    return nullptr;
#endif
}
