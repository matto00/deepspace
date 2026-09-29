#pragma once

#include "CoreMinimal.h"

/**
 * Every name C++ drives in the sky's materials, in one place.
 *
 * Mirrored by Tools/sky_material_contract.json, which
 * Tools/setup_sky_materials.py reads to author the materials, so no name is
 * typed twice. DeepSpace.Sky.MaterialContract holds the three together: the
 * names here equal the JSON's, and each loaded material exposes exactly its
 * parameters. SetScalarParameterValue on a misspelt name fails silently, and
 * without that test the symptom is a planet that never resolves.
 *
 * The materials are unlit, so each one's emissive is the final pixel, and
 * none of them is lit by the sun: planets shade themselves from their own
 * star's direction (sky decision 5).
 */
namespace SkyMaterial
{
    // Assets, authored by Tools/setup_sky_materials.py and assigned by
    // Tools/build_hauler.py (ADR 0002: C++ never loads them itself).
    inline const TCHAR* const BodyPath = TEXT("/Game/Materials/Sky/M_SkyBody.M_SkyBody");
    inline const TCHAR* const StarPath = TEXT("/Game/Materials/Sky/M_SkyStar.M_SkyStar");
    inline const TCHAR* const StarfieldPath = TEXT("/Game/Materials/Sky/M_SkyStarfield.M_SkyStarfield");
    inline const TCHAR* const GlassPath = TEXT("/Game/Materials/Sky/M_SkyGlass.M_SkyGlass");
    inline const TCHAR* const ParametersPath = TEXT("/Game/Materials/Sky/MPC_Sky.MPC_Sky");

    // The sphere every body is drawn with, built by the same script from
    // SkySphereMesh (Tools/sky_material_contract.json's "meshes").
    inline const TCHAR* const BodyMeshPath = TEXT("/Game/Materials/Sky/SM_SkyBody.SM_SkyBody");

    // The probes Eyes.WorldReliefParity draws (landing decision 1): the raw
    // face terms over a fixed patch at a fixed footprint, untonemapped.
    // M_SkyReliefProbe reaches them through the shared file.
    inline const TCHAR* const ReliefProbePath = TEXT("/Game/Materials/Sky/M_SkyReliefProbe.M_SkyReliefProbe");
    inline const FName ProbeFootprint = TEXT("ProbeFootprint"); // scalar: D units a pixel, times filter_pixels
    inline const FName ProbeSelect = TEXT("ProbeSelect");       // vector: one-hot, which terms the pixel carries
    inline const FName ProbeBias = TEXT("ProbeBias");           // vector: added to the pixel; the pipe check

    // The ground (landing decision 9): M_SkyGround, drawn by AWorldGround's
    // tiles, and M_SkyGroundProbe, Eyes.WorldReliefParity's ground case.
    inline const TCHAR* const GroundPath = TEXT("/Game/Materials/Sky/M_SkyGround.M_SkyGround");
    inline const TCHAR* const GroundProbePath = TEXT("/Game/Materials/Sky/M_SkyGroundProbe.M_SkyGroundProbe");
    inline const FName Morph = TEXT("Morph");                     // scalar: the relief's growth, 0 at 50 km to 1 at the drive floor
    inline const FName BandLimit = TEXT("BandLimit");             // scalar, custom primitive data: the tile's spacing over R
    inline const FName TilePivot = TEXT("TilePivot");             // vector, custom primitive data: the tile's pivot, cm
    inline const FName VertexBandLimit = TEXT("VertexBandLimit"); // scalar, the probe's: what the vertices carry

    /** Where BandLimit and TilePivot sit in a tile's custom primitive data. A
     *  wrong index fails as silently as a misspelt name -- every tile reads 0
     *  -- so the index is contract too: here, in the JSON's
     *  custom_primitive_data, and on M_SkyGround's parameter nodes. */
    inline constexpr int32 BandLimitPrimitiveIndex = 0;
    inline constexpr int32 TilePivotPrimitiveIndex = 1;               // 1..3

    // The shared file as a Custom node reaches it: its include, the function
    // it calls, and its pins, in order.
    inline const TCHAR* const WorldReliefInclude = TEXT("/Project/Private/WorldRelief.ush");
    inline const TCHAR* const WorldReliefEntry = TEXT("WR_SurfaceTerms");
    inline TArray<FName> WorldReliefInputs() { return { TEXT("Direction"), TEXT("Footprint"), TEXT("SeedOffset"), TEXT("Stretch"), TEXT("VertexBandLimit") }; }
    inline TArray<FName> WorldReliefOutputs() { return { TEXT("Continent"), TEXT("CraterAlbedo"), TEXT("CraterSlope") }; }

    // M_SkyBody: planets and moons.
    //   vectors Colour, LightDirection, Rim, SurfaceSeed, BodyAxisX, BodyAxisY, ShadowFrameX, ShadowFrameZ;
    //   scalars Brightness, PointBlend, Mottle, Detail, Banding, ReliefScale, Cratering, Shadows, ShadowMapFade;
    //   texture ShadowMap.
    inline const FName Colour = TEXT("Colour");                 // vector: albedo colour, or the star's
    inline const FName LightDirection = TEXT("LightDirection"); // vector: world space, body toward its star
    inline const FName Rim = TEXT("Rim");                       // vector: atmosphere rim, black for none
    inline const FName Brightness = TEXT("Brightness");         // scalar: per-pixel emission scale
    inline const FName PointBlend = TEXT("PointBlend");         // scalar: 1 a point, 0 a shaded disc
    inline const FName Mottle = TEXT("Mottle");                 // scalar: the coarse face's amplitude
    inline const FName Detail = TEXT("Detail");                 // scalar: the fine bands' amplitude
    inline const FName Banding = TEXT("Banding");               // scalar: 0 rocky ground, 1 a giant's belts
    inline const FName ReliefScale = TEXT("ReliefScale");       // scalar: the ground's own slope scale, FWorldRelief::SlopeScale
    inline const FName Cratering = TEXT("Cratering");           // scalar: how much of its craters a world has kept
    inline const FName SurfaceSeed = TEXT("SurfaceSeed");       // vector: xyz noise offset, w belt pairs; ShipSky::SurfaceSeed
    // The universe's X and Y axes in world space, the rows that turn a world
    // direction into the body's own axes. The proxy is drawn unturned (a GPU
    // instance rotation is 16-bit, and the ground amplifies its error by R/h;
    // SkyProjection::RenderedScaleBits), so the face turns with the ship here.
    inline const FName BodyAxisX = TEXT("BodyAxisX");           // vector: world space, unit
    inline const FName BodyAxisY = TEXT("BodyAxisY");           // vector: world space, unit, square to X

    // The cast shadow (the developer's ruling on slice (b)'s build: baked,
    // not marched). M_SkyBody reads the world's map, M_SkyGround the same map
    // blended into its vertices' shadow by Morph, both through one Custom node
    // over the shared file's WR_ShadowMapCoord (SunShadowMap::Sample is its
    // C++ mirror). The strength is ds.Sky.Shadows, lerp(1, shadow, Shadows).
    // ShadowMapFade fades the map alone in as it lands, and never the
    // ground's vertices: shadow = lerp(lerp(1, Vertex, Morph), node,
    // ShadowMapFade), which is lerp(lerp(1, map, fade), Vertex, Morph).
    inline const FName Shadows = TEXT("Shadows");           // scalar: the cast shadow's strength, 0..1
    inline const FName ShadowMapFade = TEXT("ShadowMapFade"); // scalar: the map's fade-in since it landed, 0..1; 0 without one
    inline const FName ShadowMap = TEXT("ShadowMap");       // texture: the world's map, G16, a mip per level (SunShadowMap)
    inline const FName ShadowFrameX = TEXT("ShadowFrameX"); // vector: the map's X axis, body axes; w its PsiLo, rad
    inline const FName ShadowFrameZ = TEXT("ShadowFrameZ"); // vector: its Z axis, the light, body axes; w its Step, rad
    inline const TCHAR* const ShadowProbePath = TEXT("/Game/Materials/Sky/M_SkyShadowProbe.M_SkyShadowProbe");
    /** What a world without a baked map reads: a white 16-bit texture, so the
     *  lookup is 1, no shadow. Authored by setup_sky_materials.py. */
    inline const TCHAR* const ShadowDefaultTexturePath = TEXT("/Game/Materials/Sky/T_SkyShadowWhite.T_SkyShadowWhite");
    /** The shadow node's include is WorldReliefInclude; it calls this, and takes these pins, in order. */
    inline const TCHAR* const ShadowCoordEntry = TEXT("WR_ShadowMapCoord");
    inline TArray<FName> ShadowInputs() { return { TEXT("Direction"), TEXT("Footprint"), TEXT("FrameX"), TEXT("FrameZ"), TEXT("ShadowMap"), TEXT("Vertex"), TEXT("Morph") }; }

    // M_SkyStar: the local star, the motes, navigation's course marker.
    //   vector Colour; scalar Brightness.

    // MPC_Sky, read by M_SkyGlass. M_SkyGlass has no parameters of its own:
    // its veil colour is a constant of the graph, tuned in the authoring
    // script, because nothing at runtime has a reason to change it.
    inline const FName InteriorLight = TEXT("InteriorLight");   // scalar: 0..1, the lights' satisfaction
    inline const FName Veil = TEXT("Veil");                     // scalar: the glass's reflection strength

    // M_SkyStarfield: no parameters. PerInstanceCustomData 0..3 = R, G, B,
    // brightness, so 3,000 stars are one draw call.
    inline constexpr int32 StarfieldCustomData = 4;
    inline constexpr int32 CustomDataRed = 0;
    inline constexpr int32 CustomDataGreen = 1;
    inline constexpr int32 CustomDataBlue = 2;
    inline constexpr int32 CustomDataBrightness = 3;

    // Each asset's parameters, exactly: the test checks the JSON against
    // these and every loaded asset against the JSON, so a parameter added on
    // one side and not the other is a red test, not a silent no-op.
    inline TArray<FName> BodyScalars() { return { Brightness, PointBlend, Mottle, Detail, Banding, ReliefScale, Cratering, Shadows, ShadowMapFade }; }
    inline TArray<FName> BodyVectors() { return { Colour, LightDirection, Rim, SurfaceSeed, BodyAxisX, BodyAxisY, ShadowFrameX, ShadowFrameZ }; }
    inline TArray<FName> BodyTextures() { return { ShadowMap }; }
    inline TArray<FName> StarScalars() { return { Brightness }; }
    inline TArray<FName> StarVectors() { return { Colour }; }
    inline TArray<FName> ParameterScalars() { return { InteriorLight, Veil }; }
    inline TArray<FName> ProbeScalars() { return { Banding, ProbeFootprint }; }
    inline TArray<FName> ProbeVectors() { return { SurfaceSeed, ProbeSelect, ProbeBias }; }
    inline TArray<FName> GroundScalars() { return { Brightness, Mottle, Detail, Cratering, ReliefScale, Morph, BandLimit, Shadows, ShadowMapFade }; }
    inline TArray<FName> GroundVectors() { return { Colour, LightDirection, SurfaceSeed, TilePivot, ShadowFrameX, ShadowFrameZ }; }
    inline TArray<FName> GroundTextures() { return { ShadowMap }; }
    inline TArray<FName> GroundProbeScalars() { return { Cratering, ReliefScale, VertexBandLimit, ProbeFootprint }; }
    inline TArray<FName> GroundProbeVectors() { return { SurfaceSeed, ProbeBias }; }
    inline TArray<FName> ShadowProbeScalars() { return { ProbeFootprint }; }
    inline TArray<FName> ShadowProbeVectors() { return { ShadowFrameX, ShadowFrameZ, ProbeBias }; }
    inline TArray<FName> ShadowProbeTextures() { return { ShadowMap }; }

    /**
     * M_SkyBody's shaded term is this times saturate(N.L). A Lambert sphere's
     * cosine averaged over its visible disc at full phase is 2/3, so the gain
     * makes the disc's average exactly SkyProjection::LambertPhase -- the
     * point term's brightness -- and a body keeps its total flux through the
     * resolve. A graph constant, so the JSON carries it and the test holds
     * the two to it.
     */
    inline constexpr double LambertDiscGain = 1.5;

    /**
     * M_SkyBody's face multiplies the shaded disc by 1 + swing, and the
     * graph clamps the swing to this whatever Mottle and Detail are set to:
     * a world is never more than 1.9 times its smooth disc, nor darker than
     * a tenth of it. The bound is what keeps the surface detail from being
     * the thing that finds the half-float ceiling the sky already stays a
     * decade under (ds.Sky.StarSurface). A graph constant, held like the
     * gain.
     */
    inline constexpr double SurfaceMaxSwing = 0.9;

    /** How many noise units SurfaceSeed's offset spans on each axis: far
     *  more than the coarse band's cycle, so two offsets are two unrelated
     *  faces rather than one face slid a little. */
    inline constexpr double SurfaceOffsetSpan = 256.0;
}
