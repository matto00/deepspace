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

    // M_SkyBody: planets and moons.
    //   vectors Colour, LightDirection, Rim; scalars Brightness, PointBlend, Mottle.
    inline const FName Colour = TEXT("Colour");                 // vector: albedo colour, or the star's
    inline const FName LightDirection = TEXT("LightDirection"); // vector: world space, body toward its star
    inline const FName Rim = TEXT("Rim");                       // vector: atmosphere rim, black for none
    inline const FName Brightness = TEXT("Brightness");         // scalar: per-pixel emission scale
    inline const FName PointBlend = TEXT("PointBlend");         // scalar: 1 a point, 0 a shaded disc
    inline const FName Mottle = TEXT("Mottle");                 // scalar: noise amplitude

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
    inline TArray<FName> BodyScalars() { return { Brightness, PointBlend, Mottle }; }
    inline TArray<FName> BodyVectors() { return { Colour, LightDirection, Rim }; }
    inline TArray<FName> StarScalars() { return { Brightness }; }
    inline TArray<FName> StarVectors() { return { Colour }; }
    inline TArray<FName> ParameterScalars() { return { InteriorLight, Veil }; }

    /**
     * M_SkyBody's shaded term is this times saturate(N.L). A Lambert sphere's
     * cosine averaged over its visible disc at full phase is 2/3, so the gain
     * makes the disc's average exactly SkyProjection::LambertPhase -- the
     * point term's brightness -- and a body keeps its total flux through the
     * resolve. A graph constant, so the JSON carries it and the test holds
     * the two to it.
     */
    inline constexpr double LambertDiscGain = 1.5;
}
