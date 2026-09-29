#include "Surface/TerrainGroundComponent.h"

#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneManagement.h"
#include "Sky/SkyMaterialContract.h"
#include "StaticMeshResources.h"

namespace
{
    /** One tile on the GPU: its buffers and vertex factory, filled on the
     *  game thread, initialised on the render thread. */
    struct FTileRender
    {
        FTileRender(ERHIFeatureLevel::Type FeatureLevel, const FTileBuild& Tile, float BandLimit)
            : VertexFactory(FeatureLevel, "FTerrainGroundSceneProxy")
            , Pivot(Tile.Pivot)
            , Box(UTerrainGroundComponent::LocalBoxOf(Tile).ShiftBy(-FVector(Tile.Pivot)))
        {
            const int32 Count = Tile.Positions.Num();
            Buffers.PositionVertexBuffer.Init(Count);
            Buffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
            Buffers.StaticMeshVertexBuffer.Init(Count, 3);
            Buffers.ColorVertexBuffer.Init(Count);
            for (int32 V = 0; V < Count; ++V)
            {
                const FVector3f Normal = Tile.Normals[V];
                const FVector3f Any = FMath::Abs(Normal.Z) < 0.9f ? FVector3f(0, 0, 1) : FVector3f(1, 0, 0);
                const FVector3f TangentX = FVector3f::CrossProduct(Any, Normal).GetSafeNormal();
                const FVector3f TangentY = FVector3f::CrossProduct(Normal, TangentX);
                Buffers.PositionVertexBuffer.VertexPosition(V) = Tile.Positions[V];
                Buffers.StaticMeshVertexBuffer.SetVertexTangents(V, TangentX, TangentY, Normal);
                // UV0.x: the vertex's cast shadow (TerrainTile::UV0Of).
                Buffers.StaticMeshVertexBuffer.SetVertexUV(V, 0, TerrainTile::UV0Of(Tile, V));
                Buffers.StaticMeshVertexBuffer.SetVertexUV(V, 1, TerrainTile::UV1Of(Tile, V));
                Buffers.StaticMeshVertexBuffer.SetVertexUV(V, 2, TerrainTile::UV2Of(Tile, V));
                Buffers.ColorVertexBuffer.VertexColor(V) = FColor::White;
            }
            // The band limit and the pivot, as custom primitive data: one
            // material instance serves every tile (decision 6).
            Custom.Data.SetNumZeroed(SkyMaterial::TilePivotPrimitiveIndex + 3);
            Custom.Data[SkyMaterial::BandLimitPrimitiveIndex] = BandLimit;
            Custom.Data[SkyMaterial::TilePivotPrimitiveIndex + 0] = static_cast<float>(Tile.Pivot.X);
            Custom.Data[SkyMaterial::TilePivotPrimitiveIndex + 1] = static_cast<float>(Tile.Pivot.Y);
            Custom.Data[SkyMaterial::TilePivotPrimitiveIndex + 2] = static_cast<float>(Tile.Pivot.Z);
        }

        void Init(FRHICommandListBase& RHICmdList)
        {
            Buffers.PositionVertexBuffer.InitResource(RHICmdList);
            Buffers.StaticMeshVertexBuffer.InitResource(RHICmdList);
            Buffers.ColorVertexBuffer.InitResource(RHICmdList);
            FLocalVertexFactory::FDataType Data;
            Buffers.PositionVertexBuffer.BindPositionVertexBuffer(&VertexFactory, Data);
            Buffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&VertexFactory, Data);
            Buffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&VertexFactory, Data);
            Buffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&VertexFactory, Data, 0);
            Buffers.ColorVertexBuffer.BindColorVertexBuffer(&VertexFactory, Data);
            VertexFactory.SetData(RHICmdList, Data);
            VertexFactory.InitResource(RHICmdList);
        }

        ~FTileRender()
        {
            Buffers.PositionVertexBuffer.ReleaseResource();
            Buffers.StaticMeshVertexBuffer.ReleaseResource();
            Buffers.ColorVertexBuffer.ReleaseResource();
            VertexFactory.ReleaseResource();
        }

        FStaticMeshVertexBuffers Buffers;
        FLocalVertexFactory VertexFactory;
        FCustomPrimitiveData Custom;
        FVector3d Pivot;
        /** Pivot-relative. */
        FBox Box;
        bool bShown = false;
    };

    /** A drawn tile's primitive data for one frame. With GPU Scene the
     *  renderer only copies the contents into the scene's primitive buffer
     *  (FRendererModule::AddMeshBatchToGPUScene), so no uniform buffer is
     *  created for it; without, it is created as FDynamicPrimitiveUniformBuffer
     *  would. */
    class FTilePrimitive : public FOneFrameResource
    {
    public:
        struct FBuffer : public TUniformBuffer<FPrimitiveUniformShaderParameters>
        {
            void Fill(FRHICommandListBase& RHICmdList, const FPrimitiveUniformShaderParameters& Parameters, bool bCreate)
            {
                if (bCreate)
                {
                    BufferUsage = UniformBuffer_SingleFrame;
                    SetContents(RHICmdList, Parameters);
                    InitResource(RHICmdList);
                }
                else
                {
                    SetContentsNoUpdate(Parameters);
                }
            }
        };
        virtual ~FTilePrimitive() override { Buffer.ReleaseResource(); }
        FBuffer Buffer;
    };

    class FTerrainGroundSceneProxy final : public FPrimitiveSceneProxy
    {
    public:
        using FTiles = TMap<FTileKey, TUniquePtr<FTileRender>>;

        FTerrainGroundSceneProxy(UTerrainGroundComponent* Component, FTiles&& InTiles)
            : FPrimitiveSceneProxy(Component)
            , Tiles(MoveTemp(InTiles))
            , MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetShaderPlatform()))
        {
            // Each drawn tile's primitive data goes to GPU Scene with its
            // mesh element; the proxy wants no uniform buffer of its own.
            EnableGPUSceneSupportFlags();
            Material = Component->GetMaterial(0);
            if (!Material)
            {
                Material = UMaterial::GetDefaultMaterial(MD_Surface);
            }
            for (const int32 Index : TerrainTile::Indices())
            {
                IndexBuffer.Indices.Add(static_cast<uint32>(Index));
            }
        }

        virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override
        {
            IndexBuffer.InitResource(RHICmdList);
            for (TPair<FTileKey, TUniquePtr<FTileRender>>& Pair : Tiles)
            {
                Pair.Value->Init(RHICmdList);
            }
        }

        virtual ~FTerrainGroundSceneProxy() override
        {
            Tiles.Reset();
            IndexBuffer.ReleaseResource();
        }

        // -- the render thread's end of the component's commands --------------

        void AddTile(FRHICommandListBase& RHICmdList, const FTileKey& Key, TUniquePtr<FTileRender>&& Tile)
        {
            Tile->Init(RHICmdList);
            Tiles.Add(Key, MoveTemp(Tile));
        }

        void RemoveTile(const FTileKey& Key)
        {
            Tiles.Remove(Key);
        }

        void SetShown(const TSet<FTileKey>& Keys)
        {
            for (TPair<FTileKey, TUniquePtr<FTileRender>>& Pair : Tiles)
            {
                Pair.Value->bShown = Keys.Contains(Pair.Key);
            }
        }

        virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
        {
            const FMatrix ComponentToWorld = GetLocalToWorld();
            const bool bCreateBuffers = DoesVFRequirePrimitiveUniformBuffer();
            for (const TPair<FTileKey, TUniquePtr<FTileRender>>& Pair : Tiles)
            {
                const FTileRender& Tile = *Pair.Value;
                if (!Tile.bShown)
                {
                    continue;
                }
                // The tile's pivot through the component's transform, in doubles:
                // the one place a tile's position meets the ship's.
                const FMatrix TileToWorld = FTranslationMatrix(Tile.Pivot) * ComponentToWorld;
                const FBoxSphereBounds LocalBounds(Tile.Box);
                const FBoxSphereBounds WorldBounds = LocalBounds.TransformBy(TileToWorld);
                FTilePrimitive* Primitive = nullptr;
                for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
                {
                    if (!(VisibilityMap & (1 << ViewIndex))
                        || !Views[ViewIndex]->GetCullingFrustum().IntersectBox(WorldBounds.Origin, WorldBounds.BoxExtent))
                    {
                        continue;
                    }
                    if (!Primitive)
                    {
                        Primitive = &Collector.AllocateOneFrameResource<FTilePrimitive>();
                        FPrimitiveUniformShaderParametersBuilder Builder;
                        Builder.Defaults()
                            .LocalToWorld(TileToWorld)
                            .PreviousLocalToWorld(TileToWorld)
                            .ActorWorldPosition(WorldBounds.Origin)
                            .WorldBounds(WorldBounds)
                            .LocalBounds(LocalBounds)
                            .ReceivesDecals(false)
                            .OutputVelocity(false)
                            .CastShadow(false)
                            .CastContactShadow(false)
                            .VisibleInRayTracing(false)
                            .VisibleInLumenScene(false)
                            .CustomPrimitiveData(&Tile.Custom);
                        Primitive->Buffer.Fill(Collector.GetRHICommandList(), Builder.Build(), bCreateBuffers);
                    }
                    FMeshBatch& Mesh = Collector.AllocateMesh();
                    Mesh.VertexFactory = &Tile.VertexFactory;
                    Mesh.MaterialRenderProxy = Material->GetRenderProxy();
                    Mesh.Type = PT_TriangleList;
                    Mesh.DepthPriorityGroup = SDPG_World;
                    Mesh.CastShadow = false;
                    Mesh.bCanApplyViewModeOverrides = false;
                    FMeshBatchElement& Element = Mesh.Elements[0];
                    Element.IndexBuffer = &IndexBuffer;
                    Element.FirstIndex = 0;
                    Element.NumPrimitives = IndexBuffer.Indices.Num() / 3;
                    Element.MinVertexIndex = 0;
                    Element.MaxVertexIndex = Tile.Buffers.PositionVertexBuffer.GetNumVertices() - 1;
                    Element.PrimitiveUniformBufferResource = &Primitive->Buffer;
                    Collector.AddMesh(ViewIndex, Mesh);
                }
            }
        }

        virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
        {
            FPrimitiveViewRelevance Result;
            Result.bDrawRelevance = IsShown(View);
            Result.bShadowRelevance = false;
            // The dynamic path: the static one cached a tile's batch but never
            // drew it in UE 5.8 (Eyes.TerrainBudget's base-colour check).
            Result.bStaticRelevance = false;
            Result.bDynamicRelevance = true;
            Result.bRenderInMainPass = ShouldRenderInMainPass();
            Result.bVelocityRelevance = false;
            MaterialRelevance.SetPrimitiveViewRelevance(Result);
            return Result;
        }

        // One primitive round the whole cut: an occlusion query on it is
        // never answered "hidden", only paid for. Each tile is culled
        // against the view's frustum instead.
        virtual bool CanBeOccluded() const override { return false; }
        virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
        virtual SIZE_T GetTypeHash() const override
        {
            static size_t UniquePointer;
            return reinterpret_cast<size_t>(&UniquePointer);
        }

    private:
        FTiles Tiles;
        UMaterialInterface* Material = nullptr;
        FDynamicMeshIndexBuffer32 IndexBuffer;
        FMaterialRelevance MaterialRelevance;
    };

    ERHIFeatureLevel::Type FeatureLevelOf(const UPrimitiveComponent* Component)
    {
        const UWorld* World = Component->GetWorld();
        return World && World->Scene ? World->Scene->GetFeatureLevel() : GMaxRHIFeatureLevel;
    }
}

UTerrainGroundComponent::UTerrainGroundComponent()
{
    SetCastShadow(false);
    bAffectDistanceFieldLighting = false;
    bAffectDynamicIndirectLighting = false;
    bVisibleInRayTracing = false;
    bNeverDistanceCull = true;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    SetMobility(EComponentMobility::Movable);
}

FBox UTerrainGroundComponent::LocalBoxOf(const FTileBuild& Tile)
{
    FBox Box(ForceInit);
    for (const FVector3f& Position : Tile.Positions)
    {
        Box += FVector(Position);
    }
    // The morph lowers vertices by up to their height along their direction.
    return Box.ExpandBy(FMath::Max(FMath::Abs(Tile.Range.MinCm), FMath::Abs(Tile.Range.MaxCm))).ShiftBy(FVector(Tile.Pivot));
}

void UTerrainGroundComponent::AddTile(const FTileRef& Tile, float BandLimit)
{
    const FTileKey Key = Tile->Key;
    if (const FEntry* Was = Tiles.Find(Key); Was && Was->bShown)
    {
        --ShownCount;
    }
    Tiles.Add(Key, FEntry{ Tile, BandLimit, LocalBoxOf(*Tile), false });
    if (FTerrainGroundSceneProxy* Proxy = static_cast<FTerrainGroundSceneProxy*>(SceneProxy))
    {
        // Filled here, as a tile's proxy was; initialised on the render thread.
        FTileRender* Render = new FTileRender(FeatureLevelOf(this), *Tile, BandLimit);
        ENQUEUE_RENDER_COMMAND(TerrainGroundAddTile)([Proxy, Key, Render](FRHICommandListImmediate& RHICmdList)
        {
            Proxy->AddTile(RHICmdList, Key, TUniquePtr<FTileRender>(Render));
        });
    }
}

void UTerrainGroundComponent::RemoveTile(const FTileKey& Key)
{
    const FEntry* Found = Tiles.Find(Key);
    if (!Found)
    {
        return;
    }
    const bool bWasShown = Found->bShown;
    Tiles.Remove(Key);
    if (bWasShown)
    {
        --ShownCount;
        RecomputeShownBox();
    }
    if (FTerrainGroundSceneProxy* Proxy = static_cast<FTerrainGroundSceneProxy*>(SceneProxy))
    {
        ENQUEUE_RENDER_COMMAND(TerrainGroundRemoveTile)([Proxy, Key](FRHICommandListImmediate&)
        {
            Proxy->RemoveTile(Key);
        });
    }
}

void UTerrainGroundComponent::ClearTiles()
{
    if (Tiles.IsEmpty())
    {
        return;
    }
    Tiles.Reset();
    ShownCount = 0;
    RecomputeShownBox();
    // Rare (a new world, a fold): the proxy is rebuilt empty.
    MarkRenderStateDirty();
}

void UTerrainGroundComponent::SetShown(const TArray<FTileKey>& Keys)
{
    const TSet<FTileKey> Wanted(Keys);
    bool bChanged = false;
    int32 Count = 0;
    for (TPair<FTileKey, FEntry>& Pair : Tiles)
    {
        const bool bShown = Wanted.Contains(Pair.Key);
        bChanged |= bShown != Pair.Value.bShown;
        Pair.Value.bShown = bShown;
        Count += bShown ? 1 : 0;
    }
    if (!bChanged)
    {
        return;
    }
    ShownCount = Count;
    RecomputeShownBox();
    if (FTerrainGroundSceneProxy* Proxy = static_cast<FTerrainGroundSceneProxy*>(SceneProxy))
    {
        ENQUEUE_RENDER_COMMAND(TerrainGroundSetShown)([Proxy, Wanted](FRHICommandListImmediate&)
        {
            Proxy->SetShown(Wanted);
        });
    }
}

void UTerrainGroundComponent::RecomputeShownBox()
{
    ShownBox = FBox(ForceInit);
    for (const TPair<FTileKey, FEntry>& Pair : Tiles)
    {
        if (Pair.Value.bShown)
        {
            ShownBox += Pair.Value.Box;
        }
    }
    UpdateBounds();
    MarkRenderTransformDirty();
}

bool UTerrainGroundComponent::IsTileShown(const FTileKey& Key) const
{
    const FEntry* Found = Tiles.Find(Key);
    return Found && Found->bShown;
}

const FTileBuild* UTerrainGroundComponent::GetTile(const FTileKey& Key) const
{
    const FEntry* Found = Tiles.Find(Key);
    return Found ? &Found->Tile.Get() : nullptr;
}

TArray<const FTileBuild*> UTerrainGroundComponent::GetTiles() const
{
    TArray<const FTileBuild*> Out;
    Out.Reserve(Tiles.Num());
    for (const TPair<FTileKey, FEntry>& Pair : Tiles)
    {
        Out.Add(&Pair.Value.Tile.Get());
    }
    return Out;
}

FVector UTerrainGroundComponent::GetTileWorldLocation(const FTileKey& Key) const
{
    const FEntry* Found = Tiles.Find(Key);
    return Found ? GetComponentTransform().TransformPosition(FVector(Found->Tile->Pivot)) : FVector::ZeroVector;
}

FPrimitiveSceneProxy* UTerrainGroundComponent::CreateSceneProxy()
{
    FTerrainGroundSceneProxy::FTiles Render;
    const ERHIFeatureLevel::Type FeatureLevel = FeatureLevelOf(this);
    for (const TPair<FTileKey, FEntry>& Pair : Tiles)
    {
        TUniquePtr<FTileRender> Tile = MakeUnique<FTileRender>(FeatureLevel, *Pair.Value.Tile, Pair.Value.BandLimit);
        Tile->bShown = Pair.Value.bShown;
        Render.Add(Pair.Key, MoveTemp(Tile));
    }
    return new FTerrainGroundSceneProxy(this, MoveTemp(Render));
}

FBoxSphereBounds UTerrainGroundComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    return ShownBox.IsValid ? FBoxSphereBounds(ShownBox).TransformBy(LocalToWorld) : FBoxSphereBounds(LocalToWorld.GetLocation(), FVector::ZeroVector, 0.0);
}
