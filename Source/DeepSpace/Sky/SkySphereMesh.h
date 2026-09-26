#pragma once

#include "CoreMinimal.h"

/**
 * The sphere every body is drawn with, as arithmetic: SM_SkyBody's geometry
 * and the screen sizes its LODs switch at. Tools/setup_sky_materials.py
 * authors the asset through UDeepSpaceEditorScripting::BuildSkySphere, which
 * builds each LOD from Build and switches them at LodScreenSizes, so the
 * asset is exactly what these functions say and DeepSpace.Sky.BodyMesh can
 * hold it to them.
 *
 * Why a mesh of our own. The engine's Sphere is 960 triangles, 32 around, and
 * its polygon shows on the limb from about 10,000 km down -- where the limb's
 * curvature is the clearest cue to altitude the approach has. The proxies are
 * homotheties of the true sphere about the ship (SkyProjection), so a facet
 * subtends on screen exactly what it would on the real world, and a mesh fine
 * enough for the nearest the sky ever draws a world is fine enough for all of
 * them.
 *
 * An equal-angle cube sphere rather than a latitude-longitude one: its cells
 * are the same size to within a few per cent everywhere, where a UV sphere
 * dense enough at its equator wastes most of its triangles on its poles. And
 * a vertex sits on each axis, so the bounds are the sphere's exactly and
 * AShipSky, which sizes every proxy from the mesh's bounds, needs nothing
 * changed.
 *
 * Pure: no UObject. Centre-origin, like the engine's Sphere.
 */
namespace SkySphereMesh
{
    struct DEEPSPACE_API FGeometry
    {
        TArray<FVector3f> Positions;

        /** Unit, outward: the sphere's own normal at each vertex, exactly. */
        TArray<FVector3f> Normals;

        /** Per-face cube coordinates in [0, 1]. Nothing samples them -- the
         *  body's face is taken from its object-space position -- but a
         *  static mesh must have a UV channel to build tangents from. */
        TArray<FVector2f> UVs;

        /** Three per triangle, wound as the engine's own meshes are: seen
         *  from outside, (B - A) x (C - A) points into the sphere. */
        TArray<int32> Indices;
    };

    /** The radius SM_SkyBody is built at, cm: the engine Sphere's, so the
     *  asset is a drop-in for it. */
    inline constexpr float Radius = 50.0f;

    /**
     * An equal-angle cube sphere of Radius: each face of the cube split into
     * CellsPerEdge x CellsPerEdge cells at equal angles from its centre,
     * pushed onto the sphere, each cell two triangles split along its shorter
     * diagonal, and the seams welded
     * so the six faces share their edge vertices exactly.
     */
    DEEPSPACE_API FGeometry Build(int32 CellsPerEdge, float InRadius = Radius);

    /** The widest angle any triangle edge of Geometry spans at the centre,
     *  rad: the chord whose sagitta is the facet the limb can show. */
    DEEPSPACE_API double LongestEdgeAngle(const FGeometry& Geometry);

    /**
     * How far, in pixels, a chord EdgeAngle wide stands inside the true limb,
     * seen from DistanceOverRadius radii from the centre: the chord's sagitta,
     * R (1 - cos(EdgeAngle / 2)), over the distance to the limb,
     * R sqrt(d^2 - 1), over PixelAngle. The sagitta points at the centre,
     * which at the limb is square to the line of sight, so all of it shows.
     */
    DEEPSPACE_API double FacetPixels(double EdgeAngle, double DistanceOverRadius, double PixelAngle);

    /** The nearest DistanceOverRadius at which a chord EdgeAngle wide stands
     *  MaxPixels inside the limb: FacetPixels solved for the distance. */
    DEEPSPACE_API double NearestDistanceFor(double EdgeAngle, double MaxPixels, double PixelAngle);

    /**
     * The engine's screen size for a sphere DistanceOverRadius radii off, the
     * number a static mesh's LODs switch on: its bounding sphere's projected
     * diameter, 2 ScreenMultiple / d, where ScreenMultiple is the larger of
     * the projection's two half-scales -- max(1, aspect) / (2 tan(FOV / 2))
     * for a horizontal field of view FovDegrees.
     */
    DEEPSPACE_API double ScreenSize(double DistanceOverRadius, double FovDegrees, double Aspect);

    /**
     * The view the mesh is sized for: 4K, 16:9, the helm's 90 degrees, and a
     * facet never more than half a pixel inside the limb. The nearest any
     * world is drawn is FSkyViewParams::MinRenderedAltitudeOfRadius, 1.6e-3
     * of its radius -- 112 km over a giant, where a 100 km drive floor over
     * anything larger than 62,500 km is drawn from -- and that altitude, the
     * worst the sky has, is what LOD 0 must hold.
     */
    inline constexpr double DesignWidthPixels = 3840.0;
    inline constexpr double DesignFovDegrees = 90.0;
    inline constexpr double DesignAspect = 16.0 / 9.0;
    inline constexpr double MaxFacetPixels = 0.5;

    /** 2 tan(FOV / 2) / width: the design view's radians per pixel. */
    DEEPSPACE_API double DesignPixelAngle();

    /**
     * The screen size at which each LOD, built with CellsPerEdge[i] cells,
     * takes over from the finer one before it: the size of the sphere at the
     * nearest distance its own longest edge stays under MaxFacetPixels in the
     * design view. LOD 0 gets the size of a sphere touching the eye, which no
     * view exceeds, so it is used whenever LOD 1 would show a facet.
     * EdgeAngles are the LODs' LongestEdgeAngle, finest first.
     */
    DEEPSPACE_API TArray<float> LodScreenSizes(TConstArrayView<double> EdgeAngles);
}
