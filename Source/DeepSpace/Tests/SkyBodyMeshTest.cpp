#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySphereMesh.h"
#include "StaticMeshResources.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyBodyMeshTest,
    "DeepSpace.Sky.BodyMesh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The sphere the bodies are drawn with. The first round's renders showed
 * the engine Sphere's polygon on the limb from 10,000 km down, and the
 * limb's curvature is the altitude cue the approach has; so the geometry is
 * checked to be a closed sphere the projection can use unchanged, the limb
 * is checked to stay under half a 4K pixel at the nearest the sky draws any
 * world, and the built asset is checked to be exactly that geometry,
 * switching LODs where the arithmetic says.
 */
bool FSkyBodyMeshTest::RunTest(const FString& Parameters)
{
    const double Pixel = SkySphereMesh::DesignPixelAngle();

    // -- The geometry --------------------------------------------------------
    {
        constexpr int32 N = 8;
        const SkySphereMesh::FGeometry Sphere = SkySphereMesh::Build(N);
        // A closed cube sphere is Euler's: V - E + F = 2 with F = 12 N^2
        // triangles and E = 18 N^2 edges, so V = 6 N^2 + 2 once the seams
        // are welded -- and not one more.
        TestEqual(TEXT("the seams are welded: 6 N^2 + 2 vertices"), Sphere.Positions.Num(), 6 * N * N + 2);
        TestEqual(TEXT("12 N^2 triangles"), Sphere.Indices.Num(), 3 * 12 * N * N);

        double Worst = 0.0;
        FBox Box(ForceInit);
        for (int32 Index = 0; Index < Sphere.Positions.Num(); ++Index)
        {
            const FVector P(Sphere.Positions[Index]);
            Worst = FMath::Max(Worst, FMath::Abs(P.Size() - SkySphereMesh::Radius));
            Worst = FMath::Max(Worst, (FVector(Sphere.Normals[Index]) - P / SkySphereMesh::Radius).Size() * SkySphereMesh::Radius);
            Box += P;
        }
        TestTrue(FString::Printf(TEXT("every vertex on the sphere, its normal the sphere's (%.2g cm off)"), Worst), Worst < 1e-4);
        TestTrue(TEXT("centre-origin, with the engine Sphere's bounds: the projection is unchanged"),
            Box.GetCenter().IsNearlyZero(1e-4) && Box.GetExtent().Equals(FVector(SkySphereMesh::Radius), 1e-4));

        // Closed and consistently wound: every edge is walked once each way,
        // and every triangle is wound as the engine's are, facing out.
        TMap<TPair<int32, int32>, int32> Edges;
        int32 Inward = 0;
        for (int32 Tri = 0; Tri < Sphere.Indices.Num(); Tri += 3)
        {
            const int32 C[3] = { Sphere.Indices[Tri], Sphere.Indices[Tri + 1], Sphere.Indices[Tri + 2] };
            for (int32 E = 0; E < 3; ++E)
            {
                ++Edges.FindOrAdd(TPair<int32, int32>(C[E], C[(E + 1) % 3]));
            }
            const FVector A(Sphere.Positions[C[0]]), B(Sphere.Positions[C[1]]), D(Sphere.Positions[C[2]]);
            Inward += (((B - A) ^ (D - A)) | (A + B + D)) < 0.0 ? 1 : 0;
        }
        int32 Unpaired = 0;
        for (const TPair<TPair<int32, int32>, int32>& Edge : Edges)
        {
            const int32* Back = Edges.Find(TPair<int32, int32>(Edge.Key.Value, Edge.Key.Key));
            Unpaired += (Edge.Value != 1 || !Back || *Back != 1) ? 1 : 0;
        }
        TestEqual(TEXT("closed: every edge shared by two triangles, walked once each way"), Unpaired, 0);
        TestEqual(TEXT("every triangle wound as the engine's are, (B - A) x (C - A) inward"), Inward, 12 * N * N);

        // Equal-angle cells: the widest edge is the diagonal of a cell
        // 90 / N degrees across, within the few per cent the cube's corners
        // add.
        const double Cell = 0.5 * UE_DOUBLE_PI / N;
        const double Longest = SkySphereMesh::LongestEdgeAngle(Sphere);
        TestTrue(FString::Printf(TEXT("the widest edge is a cell's diagonal (%.4f against %.4f)"), Longest, Cell * UE_DOUBLE_SQRT_2),
            Longest > Cell && Longest < 1.3 * Cell * UE_DOUBLE_SQRT_2);
    }

    // -- The arithmetic ------------------------------------------------------
    {
        TestTrue(TEXT("the design view is 4K at 90 degrees: 2 / 3840 rad a pixel"), FMath::IsNearlyEqual(Pixel, 2.0 / 3840.0, 1e-12));
        const double Edge = 0.01;
        const double D = SkySphereMesh::NearestDistanceFor(Edge, 0.5, Pixel);
        TestTrue(TEXT("NearestDistanceFor is FacetPixels solved for the distance"),
            FMath::IsNearlyEqual(SkySphereMesh::FacetPixels(Edge, D, Pixel), 0.5, 1e-9));
        TestTrue(TEXT("nearer, the facet shows more"), SkySphereMesh::FacetPixels(Edge, 1.0 + 0.5 * (D - 1.0), Pixel) > 0.5);
        TestTrue(TEXT("a sphere at twice its radius, full-width at 90 degrees, has the engine's screen size of 16:9's 0.89"),
            FMath::IsNearlyEqual(SkySphereMesh::ScreenSize(2.0, 90.0, 16.0 / 9.0), 16.0 / 18.0, 1e-9));

        // The engine Sphere, 32 segments round, for scale: at 1,000 km over
        // an Earth its facets stand a dozen pixels inside the limb.
        const double Earth = UniverseUnits::CmPerEarthRadius;
        const double Engine = SkySphereMesh::FacetPixels(2.0 * UE_DOUBLE_PI / 32.0, 1.0 + 1.0e8 / Earth, Pixel);
        TestTrue(FString::Printf(TEXT("the engine Sphere shows its facets at 1,000 km (%.1f px)"), Engine), Engine > 5.0);
    }

    // -- The contract's LODs, against the nearest the sky draws a world --------
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/sky_material_contract.json"));
    FString Text;
    TSharedPtr<FJsonObject> Contract;
    if (!TestTrue(TEXT("the contract loads"), FFileHelper::LoadFileToString(Text, *Path)
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Contract) && Contract.IsValid()))
    {
        return false;
    }
    TArray<int32> Cells;
    for (const TSharedPtr<FJsonValue>& Value : Contract->GetObjectField(TEXT("meshes"))->GetObjectField(TEXT("SM_SkyBody"))->GetArrayField(TEXT("cells_per_lod")))
    {
        Cells.Add(static_cast<int32>(Value->AsNumber()));
    }
    if (!TestTrue(TEXT("the contract gives SM_SkyBody several LODs"), Cells.Num() >= 2))
    {
        return false;
    }
    TArray<double> EdgeAngles;
    for (const int32 N : Cells)
    {
        EdgeAngles.Add(SkySphereMesh::LongestEdgeAngle(SkySphereMesh::Build(N)));
    }
    const TArray<float> Sizes = SkySphereMesh::LodScreenSizes(EdgeAngles);

    // LOD 0 at the worst the sky ever draws: 1.6e-3 of a radius up, where a
    // giant sits at the 100 km drive floor. And an Earth at the floor itself.
    // Solid worlds are drawn by the ground under 50 km (landing decision 7); LOD 0 at the rendered
    // floor is still what oceans, giants and every world above 50 km need.
    const FSkyViewParams View;
    const double Floor = 1.0 + View.MinRenderedAltitudeOfRadius;
    const double AtFloor = SkySphereMesh::FacetPixels(EdgeAngles[0], Floor, Pixel);
    TestTrue(FString::Printf(TEXT("LOD 0's limb stands %.2f px inside at the nearest any world is drawn: under half a 4K pixel"), AtFloor),
        AtFloor <= SkySphereMesh::MaxFacetPixels);
    const double Earth = SkySphereMesh::FacetPixels(EdgeAngles[0], 1.0 + 1.0e7 / UniverseUnits::CmPerEarthRadius, Pixel);
    TestTrue(FString::Printf(TEXT("and an Earth's at the 100 km drive floor, %.2f px"), Earth), Earth <= SkySphereMesh::MaxFacetPixels);
    for (int32 Lod = 1; Lod < Cells.Num(); ++Lod)
    {
        TestTrue(FString::Printf(TEXT("LOD %d is coarser than LOD %d"), Lod, Lod - 1), Cells[Lod] < Cells[Lod - 1]);
        TestTrue(FString::Printf(TEXT("and takes over at a smaller screen size (%.3f)"), Sizes[Lod]), Sizes[Lod] < Sizes[Lod - 1]);
        // Where it takes over, the view is the nearest it shows no facet.
        const double Nearest = SkySphereMesh::NearestDistanceFor(EdgeAngles[Lod], SkySphereMesh::MaxFacetPixels, Pixel);
        TestTrue(FString::Printf(TEXT("at which its own facet is half a pixel (LOD %d)"), Lod),
            FMath::IsNearlyEqual(SkySphereMesh::ScreenSize(Nearest, SkySphereMesh::DesignFovDegrees, SkySphereMesh::DesignAspect),
                static_cast<double>(Sizes[Lod]), 1e-5));
    }

    // -- The asset is that geometry ------------------------------------------
    const UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, SkyMaterial::BodyMeshPath);
    if (!TestNotNull(TEXT("SM_SkyBody is built (Tools/setup_sky_materials.py)"), Mesh))
    {
        return false;
    }
    const FBox Box = Mesh->GetBoundingBox();
    TestTrue(FString::Printf(TEXT("SM_SkyBody is centre-origin with the engine Sphere's bounds (%s)"), *Box.ToString()),
        Box.GetCenter().IsNearlyZero(1e-3) && Box.GetExtent().Equals(FVector(SkySphereMesh::Radius), 1e-3));
    if (TestEqual(TEXT("SM_SkyBody has the contract's LODs"), Mesh->GetNumLODs(), Cells.Num()))
    {
        for (int32 Lod = 0; Lod < Cells.Num(); ++Lod)
        {
            TestEqual(FString::Printf(TEXT("LOD %d is %d cells a face"), Lod, Cells[Lod]), Mesh->GetNumTriangles(Lod), 12 * Cells[Lod] * Cells[Lod]);
#if WITH_EDITORONLY_DATA
            if (Lod > 0)
            {
                TestTrue(FString::Printf(TEXT("LOD %d switches where SkySphereMesh says (%.4f)"), Lod, Sizes[Lod]),
                    FMath::IsNearlyEqual(Mesh->GetSourceModel(Lod).ScreenSize.Default, Sizes[Lod], 1e-5f));
            }
#endif
        }
    }
#if WITH_EDITORONLY_DATA
    TestFalse(TEXT("and its screen sizes are its own, not the engine's guesses"), Mesh->GetAutoComputeLODScreenSize());
#endif
    TestEqual(TEXT("one material slot"), Mesh->GetStaticMaterials().Num(), 1);
    return true;
}

#endif
