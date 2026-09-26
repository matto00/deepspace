"""Author the sky's materials: M_SkyBody, M_SkyStar, M_SkyStarfield, M_SkyGlass.

Every parameter name comes from Tools/sky_material_contract.json, the same
list Source/DeepSpace/Sky/SkyMaterialContract.h holds, so no Unreal name is
typed twice: this script wires by *role* ("brightness", "point_blend") and
reads the name each role has. DeepSpace.Sky.MaterialContract then loads the
assets and checks each exposes exactly its contract's parameters, because
SetScalarParameterValue on a misspelt name fails silently.

Materials cannot be compiled at runtime, so these are assets; but they are
generated, like the level: each run clears every graph and rebuilds it, so
tune them here, never in the editor. All four are unlit -- the emissive is
the final pixel and no light touches them (sky decision 5).

Slice 2 drops in two things, and the script is shaped for both:
- MPC_Sky, the parameter collection: the contract lists it under
  "collections" marked "pending"; author_collection() already builds it and
  runs as soon as the marker is removed.
- The glass veil: glass_veil() returns the term M_SkyGlass adds to its
  emissive, and returns nothing until then.

Run with the editor closed, through the machine-wide lock:

    . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" \\
        -unattended -nopause -nosplash -NoLiveCoding

unreal.log does not reach stdout under the commandlet; the report is in
Saved/setup_sky_materials.txt.
"""

import json
import os

import unreal

MEL = unreal.MaterialEditingLibrary
HERE = os.path.dirname(os.path.abspath(__file__))
CONTRACT = json.load(open(os.path.join(HERE, "sky_material_contract.json")))
DIRECTORY = CONTRACT["directory"]
CONSTANTS = CONTRACT["constants"]

REPORT = []


def log(line):
    REPORT.append(line)
    unreal.log(line)


def name(role):
    """The Unreal name of a contract role -- the only way this script names a
    parameter."""
    return CONTRACT["parameters"][role]["name"]


# -- assets --------------------------------------------------------------------

def clear_graph(material, asset):
    """Delete every node. delete_all_material_expressions alone leaves some
    behind -- a rebuilt M_SkyBody was found carrying 50 nodes, duplicate
    parameters among them, from two runs that had failed part-way -- so
    delete one at a time until none is left, and refuse to build on a graph
    that will not empty."""
    MEL.delete_all_material_expressions(material)
    for _ in range(8):
        remaining = list(MEL.get_material_expressions(material))
        if not remaining:
            return
        for expression in remaining:
            MEL.delete_material_expression(material, expression)
    raise RuntimeError("%s: %d nodes will not delete" % (asset, MEL.get_num_material_expressions(material)))


def fresh_material(asset):
    """The material at DIRECTORY/asset with an empty graph, created if new."""
    path = "%s/%s" % (DIRECTORY, asset)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.EditorAssetLibrary.load_asset(path)
        clear_graph(material, asset)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset, DIRECTORY, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    return material


def allow_instanced(material):
    """Instanced meshes need the usage compiled in, or they draw the default
    material in a packaged game."""
    if hasattr(MEL, "set_material_usage"):
        MEL.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    else:
        material.set_editor_property("used_with_instanced_static_meshes", True)


def finish(material, asset):
    MEL.layout_material_expressions(material)
    # In a rendering editor the compiler's verdict comes back here. Under a
    # commandlet no shaders compile and it is always empty, which is why
    # DeepSpace.Sky.MaterialContract runs the translator on every graph.
    errors = MEL.recompile_material(material) or []
    if errors:
        raise RuntimeError("%s does not compile: %s" % (asset, "; ".join(str(e) for e in errors)))
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    scalars = sorted(str(n) for n in MEL.get_scalar_parameter_names(material))
    vectors = sorted(str(n) for n in MEL.get_vector_parameter_names(material))
    log("%s: %d nodes; scalars %s, vectors %s" % (asset, MEL.get_num_material_expressions(material), scalars, vectors))
    expected = CONTRACT["materials"][asset]["parameters"]
    want_scalars = sorted(name(r) for r in expected if CONTRACT["parameters"][r]["type"] == "scalar")
    want_vectors = sorted(name(r) for r in expected if CONTRACT["parameters"][r]["type"] == "vector")
    if scalars != want_scalars or vectors != want_vectors:
        raise RuntimeError("%s does not match its contract: want scalars %s, vectors %s"
                           % (asset, want_scalars, want_vectors))


# -- graph helpers -------------------------------------------------------------

class Graph:
    """A thin wrapper so a graph reads as its arithmetic."""

    def __init__(self, material):
        self.material = material
        self.parameters = {}

    def node(self, cls, **properties):
        expression = MEL.create_material_expression(self.material, cls, 0, 0)
        for key, value in properties.items():
            expression.set_editor_property(key, value)
        return expression

    def link(self, source, target, input_name="", output_name=""):
        if not MEL.connect_material_expressions(source, output_name, target, input_name):
            raise RuntimeError("could not connect %s.%s -> %s.%s"
                               % (source.get_name(), output_name, target.get_name(), input_name))

    def scalar(self, role, default):
        expression = self.node(unreal.MaterialExpressionScalarParameter,
                               parameter_name=name(role), default_value=float(default))
        self.parameters[role] = expression
        return expression

    def vector(self, role, default):
        expression = self.node(unreal.MaterialExpressionVectorParameter,
                               parameter_name=name(role),
                               default_value=unreal.LinearColor(*default))
        self.parameters[role] = expression
        return expression

    def constant(self, value):
        return self.node(unreal.MaterialExpressionConstant, r=float(value))

    def colour(self, rgb):
        return self.node(unreal.MaterialExpressionConstant3Vector,
                         constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    def binary(self, cls, a, b):
        expression = self.node(cls)
        self.link(a, expression, "A")
        self.link(b, expression, "B")
        return expression

    def mul(self, a, b):
        return self.binary(unreal.MaterialExpressionMultiply, a, b)

    def add(self, a, b):
        return self.binary(unreal.MaterialExpressionAdd, a, b)

    def unary(self, cls, a, **properties):
        expression = self.node(cls, **properties)
        self.link(a, expression)
        return expression

    def emissive(self, source):
        if not MEL.connect_material_property(source, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
            raise RuntimeError("could not connect the emissive")

    def opacity(self, source):
        if not MEL.connect_material_property(source, "", unreal.MaterialProperty.MP_OPACITY):
            raise RuntimeError("could not connect the opacity")


# -- the materials -------------------------------------------------------------

def sky_body():
    """Planets and moons.

        shaded   = gain * saturate(N.L) * smoothstep(-w, w, N.L)
        disc     = shaded * (1 + Mottle * noise(object position))
        emissive = Colour * Brightness * lerp(disc, 1, PointBlend)
                 + Rim * Brightness * fresnel * saturate(N.L) * (1 - PointBlend)

    N is the world-space vertex normal and L is LightDirection, the world-space
    direction from this body to *its* star, which the actor writes per body:
    a directional light has one direction, and a planet off the sun line
    would show the wrong phase under it. The gain is the contract's
    lambert_disc_gain, 1.5, which makes the lit disc average exactly
    SkyProjection::LambertPhase -- the point's brightness -- so the resolve
    keeps total flux. The smoothstep only rounds the terminator's last few
    percent. The night side is black: an unlit world is a hole in the stars,
    which is itself a way of seeing it.
    """
    asset = "M_SkyBody"
    material = fresh_material(asset)
    g = Graph(material)

    colour = g.vector("colour", (1.0, 1.0, 1.0, 1.0))
    light = g.vector("light_direction", (0.0, 0.0, 1.0, 0.0))
    rim = g.vector("rim", (0.0, 0.0, 0.0, 1.0))
    brightness = g.scalar("brightness", 1.0)
    point_blend = g.scalar("point_blend", 1.0)
    mottle = g.scalar("mottle", 0.15)

    normal = g.node(unreal.MaterialExpressionVertexNormalWS)
    n_dot_l = g.binary(unreal.MaterialExpressionDotProduct, normal, light)
    lambert = g.unary(unreal.MaterialExpressionSaturate, n_dot_l)

    width = float(CONSTANTS["terminator_width"])
    soft = g.node(unreal.MaterialExpressionSmoothStep, const_min=-width, const_max=width)
    g.link(n_dot_l, soft, "Value")

    shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))

    # Object space, so the mottle stays on the world however the proxy is
    # scaled each frame; centred on zero so it moves no flux on average.
    position = g.node(unreal.MaterialExpressionLocalPosition)
    noise = g.node(unreal.MaterialExpressionNoise,
                   scale=float(CONSTANTS["mottle_scale"]), output_min=-1.0, output_max=1.0,
                   levels=4, turbulence=False)
    # Position is the first input; its pin is named for a world-space origin
    # it does not have here, so it is connected by index, not by name.
    g.link(position, noise, "", output_name="XYZ")
    mottled = g.add(g.mul(noise, mottle), g.constant(1.0))
    disc = g.mul(shaded, mottled)

    blend = g.node(unreal.MaterialExpressionLinearInterpolate, const_b=1.0)
    g.link(disc, blend, "A")
    g.link(point_blend, blend, "Alpha")
    body = g.mul(g.mul(colour, brightness), blend)

    fresnel = g.node(unreal.MaterialExpressionFresnel, exponent=float(CONSTANTS["rim_exponent"]))
    resolved = g.unary(unreal.MaterialExpressionOneMinus, point_blend)
    rim_term = g.mul(g.mul(rim, brightness), g.mul(g.mul(fresnel, lambert), resolved))

    g.emissive(g.add(body, rim_term))
    finish(material, asset)


def sky_star():
    """The local star, the motes, and navigation's course marker: a flat
    disc of constant surface brightness (sky decision 2). Bloom does the
    rest -- it scales with total energy, so a near star glares and a far one
    glints without any code.

        emissive = Colour * Brightness
    """
    asset = "M_SkyStar"
    material = fresh_material(asset)
    g = Graph(material)
    g.emissive(g.mul(g.vector("colour", (1.0, 1.0, 1.0, 1.0)), g.scalar("brightness", 1.0)))
    allow_instanced(material)
    finish(material, asset)


def sky_starfield():
    """The 3,000 background stars in one draw call. Colour and brightness are
    per-instance custom data, floats 0-3, written by the counter-frame; the
    material has no parameters at all.

        emissive = (Custom0, Custom1, Custom2) * Custom3
    """
    asset = "M_SkyStarfield"
    material = fresh_material(asset)
    g = Graph(material)
    count = int(CONTRACT["materials"][asset]["custom_data"])
    custom = [g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=i,
                     const_default_value=1.0) for i in range(count)]
    rg = g.binary(unreal.MaterialExpressionAppendVector, custom[0], custom[1])
    rgb = g.binary(unreal.MaterialExpressionAppendVector, rg, custom[2])
    g.emissive(g.mul(rgb, custom[3]))
    allow_instanced(material)
    finish(material, asset)


def glass_veil(graph):
    """The veil, VeilColour * MPC_Sky.Veil * MPC_Sky.InteriorLight: the room
    reflected in the glass, so a lit ship hides the faint stars (sky decision
    6). Slice 2. Returns the node to add to the glass's emissive, or None."""
    return None


def sky_glass():
    """The cockpit and galley windows: translucent and unlit, a faint tint
    and little else, so what is outside reads as outside.

        emissive = tint (+ the veil, slice 2); opacity = glass_opacity
    """
    asset = "M_SkyGlass"
    material = fresh_material(asset)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    g = Graph(material)
    emissive = g.colour(CONSTANTS["glass_tint"])
    veil = glass_veil(g)
    if veil is not None:
        emissive = g.add(emissive, veil)
    g.emissive(emissive)
    g.opacity(g.constant(CONSTANTS["glass_opacity"]))
    finish(material, asset)


def author_collection(asset, entry):
    """A Material Parameter Collection: global shader scalars any material can
    read and C++ can set once a frame."""
    path = "%s/%s" % (DIRECTORY, asset)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        collection = unreal.EditorAssetLibrary.load_asset(path)
    else:
        collection = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset, DIRECTORY, unreal.MaterialParameterCollection,
            unreal.MaterialParameterCollectionFactoryNew())
    scalars = []
    for role in entry["parameters"]:
        parameter = unreal.CollectionScalarParameter()
        parameter.set_editor_property("parameter_name", name(role))
        parameter.set_editor_property("default_value", 0.0)
        scalars.append(parameter)
    collection.set_editor_property("scalar_parameters", scalars)
    collection.set_editor_property("vector_parameters", [])
    unreal.EditorAssetLibrary.save_loaded_asset(collection, only_if_is_dirty=False)
    log("%s: scalars %s" % (asset, [name(r) for r in entry["parameters"]]))


def main():
    unreal.EditorAssetLibrary.make_directory(DIRECTORY)
    for asset, entry in CONTRACT["collections"].items():
        if "pending" in entry:
            log("%s: pending (%s)" % (asset, entry["pending"]))
        else:
            author_collection(asset, entry)
    sky_body()
    sky_star()
    sky_starfield()
    sky_glass()
    log("ok")


try:
    main()
finally:
    with open(os.path.join(unreal.Paths.project_saved_dir(), "setup_sky_materials.txt"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
