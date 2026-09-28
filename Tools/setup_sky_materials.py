"""Author the sky's assets: the materials M_SkyBody, M_SkyStar, M_SkyStarfield
and M_SkyGlass, the parameter collection MPC_Sky that the glass reads, and
SM_SkyBody, the sphere the bodies are drawn with.

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

The collections are authored first, because a material that reads one
holds it by the id of each parameter, not its name: M_SkyGlass is built
against the MPC_Sky this same run leaves on disk. A collection marked
"pending" in the contract is skipped, and so is any term that reads it.

Run with the editor closed, through the machine-wide lock:

    . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" \\
        -unattended -nopause -nosplash -NoLiveCoding

unreal.log does not reach stdout under the commandlet; the report is in
Saved/setup_sky_materials.txt.
"""

import collections
import json
import os

import unreal

MEL = unreal.MaterialEditingLibrary
HERE = os.path.dirname(os.path.abspath(__file__))
CONTRACT = json.load(open(os.path.join(HERE, "sky_material_contract.json")))
DIRECTORY = CONTRACT["directory"]
CONSTANTS = CONTRACT["constants"]
SHARED = CONTRACT["shared_relief"]

# The raw terms a face is composed from, whichever noise made them.
Terms = collections.namedtuple("Terms", "coarse fine crater_face crater_slope")

# The Custom node's body: one call into the shared file, its outputs pinned.
# HLSL only -- the file itself is the shared part.
SHARED_CODE = (
    "WR_Terms T = %s(Direction.x, Direction.y, Direction.z, Footprint, SeedOffset.x, SeedOffset.y, SeedOffset.z, Stretch, VertexBandLimit);\n"
    "Continent = T.Continent;\n"
    "CraterAlbedo = T.CraterAlbedo;\n"
    "CraterSlope = float3(T.CraterSlopeX, T.CraterSlopeY, T.CraterSlopeZ);\n"
    "return float4(T.DetailSlopeX, T.DetailSlopeY, T.DetailSlopeZ, T.Detail);\n" % SHARED["entry"])

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

    def collection_scalar(self, collection, role):
        """A scalar from a Material Parameter Collection. The collection is
        set before the name: the expression resolves the name to the
        parameter's id when the name changes, and that id -- not the name --
        is what is saved and what the material reads on load."""
        expression = self.node(unreal.MaterialExpressionCollectionParameter,
                               collection=collection, parameter_name=name(role))
        self.parameters[role] = expression
        return expression

    def opacity(self, source):
        if not MEL.connect_material_property(source, "", unreal.MaterialProperty.MP_OPACITY):
            raise RuntimeError("could not connect the opacity")


# -- the materials -------------------------------------------------------------

def mask(g, source, channels, output_name=""):
    """A ComponentMask of `channels` ("rgb", "a") from source."""
    node = g.node(unreal.MaterialExpressionComponentMask,
                  r="r" in channels, g="g" in channels, b="b" in channels, a="a" in channels)
    g.link(source, node, "", output_name=output_name)
    return node


def body_axes(g):
    """The universe's axes in world space, from BodyAxisX and BodyAxisY, and
    Z as their cross product: the rows that turn a world direction into the
    body's own axes. The proxy is drawn unturned -- its rotation is the
    world's identity, which a GPU instance transform keeps exactly, where
    the counter-frame's rotation came back 16-bit and the ground under the
    ship took its error times R / h -- so the face is turned with the ship
    here, by full-float parameters. Returns (x, y, z) as vector nodes."""
    x = g.vector("body_axis_x", (1.0, 0.0, 0.0, 0.0))
    y = g.vector("body_axis_y", (0.0, 1.0, 0.0, 0.0))
    x3 = mask(g, x, "rgb")
    y3 = mask(g, y, "rgb")
    z3 = g.binary(unreal.MaterialExpressionCrossProduct, x3, y3)
    return x3, y3, z3


def to_body(g, axes, world):
    """A world-space vector in the body's axes: its dot with each row."""
    x, y, z = axes
    dx = g.binary(unreal.MaterialExpressionDotProduct, x, world)
    dy = g.binary(unreal.MaterialExpressionDotProduct, y, world)
    dz = g.binary(unreal.MaterialExpressionDotProduct, z, world)
    return g.binary(unreal.MaterialExpressionAppendVector, g.binary(unreal.MaterialExpressionAppendVector, dx, dy), dz)


def to_world(g, axes, body):
    """A body-axes vector in world space: the rows weighted by its parts."""
    x, y, z = axes
    along = lambda row, channel: g.mul(row, mask(g, body, channel))
    return g.add(g.add(along(x, "r"), along(y, "g")), along(z, "b"))


def seed_offset(g, seed):
    """SurfaceSeed's xyz: where on the noise this world is."""
    offset = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=True, a=False)
    g.link(seed, offset)
    return offset


def stretch_of(g, banding):
    """lerp(1, belt_stretch, Banding): how far a giant's detail is drawn out
    across its belts, so it streaks as cloud does."""
    stretch = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=1.0, const_b=float(CONSTANTS["belt_stretch"]))
    g.link(banding, stretch, "Alpha")
    return stretch


def stretch_axes(g, stretch):
    """(1, 1, stretch), which stretches a direction along the pole."""
    return g.binary(unreal.MaterialExpressionAppendVector,
                    g.node(unreal.MaterialExpressionConstant2Vector, r=1.0, g=1.0), stretch)


def body_direction(g, axes):
    """D, the unit direction to the pixel in the body's own axes --
    normalize(LocalPosition), turned by BodyAxisX/Y -- and the pixel's
    footprint on it, max(|ddx D|, |ddy D|) * filter_pixels. The mesh is
    unturned, so its object space has the world's axes."""
    position = g.node(unreal.MaterialExpressionLocalPosition)
    seen = g.node(unreal.MaterialExpressionNormalize)
    g.link(position, seen, "", output_name="XYZ")
    direction = to_body(g, axes, seen)
    ddx = g.unary(unreal.MaterialExpressionLength, g.unary(unreal.MaterialExpressionDDX, direction))
    ddy = g.unary(unreal.MaterialExpressionLength, g.unary(unreal.MaterialExpressionDDY, direction))
    footprint = g.mul(g.binary(unreal.MaterialExpressionMax, ddx, ddy), g.constant(CONSTANTS["filter_pixels"]))
    return direction, footprint


def shared_terms(g, direction, footprint, seed, stretch, vertex_band_limit=None):
    """The same raw terms from Shaders/Private/WorldRelief.ush -- the file the
    C++ compiles too (landing decision 1) -- through one Custom node that
    includes it and calls its entry point. Every band the file carries.
    vertex_band_limit is what a tile's vertices carry (radius units); the
    slopes keep only the rest. None is the orbit: no vertices, 1.0."""
    custom = g.node(unreal.MaterialExpressionCustom)
    custom.set_editor_property("description", "WorldRelief")
    custom.set_editor_property("code", SHARED_CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("include_file_paths", [SHARED["include"]])
    pins = []
    for input_name in SHARED["inputs"]:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", input_name)
        pins.append(pin)
    custom.set_editor_property("inputs", pins)
    outputs = []
    for output_name, kind in SHARED["outputs"]:
        pin = unreal.CustomOutput()
        pin.set_editor_property("output_name", output_name)
        pin.set_editor_property("output_type", getattr(unreal.CustomMaterialOutputType, kind))
        outputs.append(pin)
    custom.set_editor_property("additional_outputs", outputs)
    limit = vertex_band_limit if vertex_band_limit is not None else g.constant(1.0)
    for input_name, source in zip(SHARED["inputs"], (direction, footprint, seed_offset(g, seed), stretch, limit)):
        g.link(source, custom, input_name)
    return Terms(mask(g, custom, "r", output_name="Continent"), custom,
                 mask(g, custom, "r", output_name="CraterAlbedo"),
                 mask(g, custom, "rgb", output_name="CraterSlope"))


def surface(g, knobs, seed, direction, footprint, terms):
    """The world's face and relief, composed from its raw terms: the face is
    the factor the shaded disc is multiplied by, 1 + swing, and the relief is
    the gradient of a height field the normal is tilted by.

        stretch   = lerp(1, belt_stretch, Banding)
        coarse, fine, craters = terms(D, footprint, SurfaceSeed, stretch)
        rocky     = clamp(coarse * continent_contrast, -1, 1)
        belts     = sin(pi * pairs * (D.z + belt_warp * coarse)) * belt fade
        craters   = Cratering * lerp(maria_cratering, 1, highland) * crater terms
        face      = lerp(rocky, belts, Banding) * Mottle + fine.a * Detail + craters.albedo
        factor    = 1 + clamp(face, -max_swing, max_swing)
        slope     = ReliefScale * (fine.rgb * (1, 1, stretch) + Cratering * craters.slope)

    terms is shared_terms: the shared file.
    Rock gets basins and highlands with craters, their albedo fewer in the
    basins, their height the same everywhere; a giant gets belts wandered by
    the same coarse noise. ReliefScale is the ground's own slope scale
    (FWorldRelief::SlopeScale, landing decision 3), a giant's fixed billow,
    or 0 for an ocean: ShipSky::ReliefScaleOf writes it.
    The face is centred on zero, so the disc keeps its flux on average, and
    the clamp is the half-float guard."""
    mottle, detail, banding, relief_scale, cratering = knobs
    stretch = stretch_of(g, banding)
    t = terms(g, direction, footprint, seed, stretch)

    rocky = g.node(unreal.MaterialExpressionClamp, min_default=-1.0, max_default=1.0)
    g.link(g.mul(t.coarse, g.constant(CONSTANTS["continent_contrast"])), rocky, "")

    # The seed's w, a giant's belt pairs, from the parameter's own alpha pin:
    # its default output is only the colour's three channels.
    pairs = g.node(unreal.MaterialExpressionMultiply, const_b=1.0)
    g.link(seed, pairs, "A", output_name="A")
    latitude = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    g.link(direction, latitude)
    wandered = g.add(latitude, g.mul(t.coarse, g.constant(CONSTANTS["belt_warp"])))
    # Sine with period 2 is sin(pi x).
    belts = g.node(unreal.MaterialExpressionSine, period=2.0)
    g.link(g.mul(wandered, pairs), belts)
    belt_fade = g.unary(unreal.MaterialExpressionSaturate,
                        g.unary(unreal.MaterialExpressionOneMinus, g.mul(footprint, pairs)))
    banded = g.mul(belts, belt_fade)

    kind = g.node(unreal.MaterialExpressionLinearInterpolate)
    g.link(rocky, kind, "A")
    g.link(banded, kind, "B")
    g.link(banding, kind, "Alpha")
    face = g.mul(kind, mottle)
    face = g.add(face, g.mul(mask(g, t.fine, "a"), detail))

    highland = g.unary(unreal.MaterialExpressionSaturate, g.add(g.mul(rocky, g.constant(0.5)), g.constant(0.5)))
    marked = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=float(CONSTANTS["maria_cratering"]), const_b=1.0)
    g.link(highland, marked, "Alpha")
    crater_gain = g.mul(cratering, marked)
    face = g.add(face, g.mul(t.crater_face, crater_gain))

    swing = float(CONSTANTS["surface_max_swing"])
    bounded = g.node(unreal.MaterialExpressionClamp, min_default=-swing, max_default=swing)
    g.link(face, bounded, "")
    factor = g.add(bounded, g.constant(1.0))

    # The ground's own slope (landing decision 3): ReliefScale x the detail
    # bands' gradient and x Cratering the craters', everywhere -- the maria
    # still thin the craters' albedo, never their height.
    slope = g.add(g.mul(g.mul(mask(g, t.fine, "rgb"), stretch_axes(g, stretch)), relief_scale),
                  g.mul(t.crater_slope, g.mul(cratering, relief_scale)))
    return factor, slope


def probe_direction(g):
    """D over the contract's fixed patch, from the render target's UV:
    normalize(centre + (u - 0.5) span east + (v - 0.5) span north)."""
    patch = CONSTANTS["probe_patch"]
    uv = g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    across = g.mul(g.add(mask(g, uv, "r"), g.constant(-0.5)), g.constant(patch["span"]))
    up = g.mul(g.add(mask(g, uv, "g"), g.constant(-0.5)), g.constant(patch["span"]))
    point = g.add(g.add(g.colour(patch["centre"]), g.mul(g.colour(patch["east"]), across)),
                  g.mul(g.colour(patch["north"]), up))
    return g.unary(unreal.MaterialExpressionNormalize, point)


def relief_probe(asset, terms):
    """A probe for Eyes.WorldReliefParity: the raw terms over the fixed patch
    at the footprint ProbeFootprint says, untonemapped (landing decision 1).

        pixel = ProbeBias.rgb + ProbeSelect.r * (coarse, fine.a, crater albedo)
                              + ProbeSelect.g * fine.rgb
                              + ProbeSelect.b * crater slope
                              + ProbeSelect.a * D

    One-hot selection multiplies by exactly 1 or 0, which float keeps exactly,
    so each pass reads back one set of terms unaltered. Selecting nothing
    draws the bias alone: the test's check of the pipe itself."""
    material = fresh_material(asset)
    # The terms are signed, and the material template clamps emissive at 0
    # unless the material says otherwise (MATERIAL_ALLOW_NEGATIVE_EMISSIVECOLOR):
    # the pipe check read -0.375 back as 0 until this was set.
    material.set_editor_property("allow_negative_emissive_color", True)
    g = Graph(material)
    seed = g.vector("surface_seed", (0.0, 0.0, 0.0, 0.0))
    banding = g.scalar("banding", 0.0)
    footprint = g.scalar("probe_footprint", 0.0)
    select = g.vector("probe_select", (1.0, 0.0, 0.0, 0.0))
    bias = g.vector("probe_bias", (0.0, 0.0, 0.0, 0.0))
    direction = probe_direction(g)
    t = terms(g, direction, footprint, seed, stretch_of(g, banding))
    first = g.binary(unreal.MaterialExpressionAppendVector,
                     g.binary(unreal.MaterialExpressionAppendVector, t.coarse, mask(g, t.fine, "a")), t.crater_face)
    passes = (first, mask(g, t.fine, "rgb"), t.crater_slope, direction)

    def selected(channel):
        # The alpha is its own pin: the default output is the colour's three.
        return mask(g, select, "r", output_name="A") if channel == "a" else mask(g, select, channel)

    out = mask(g, bias, "rgb")
    for channel, value in zip("rgba", passes):
        out = g.add(out, g.mul(value, selected(channel)))
    g.emissive(out)
    finish(material, asset)


def relief_normal(g, direction, slope, axes):
    """The world-space normal of the relief: the sphere's own normal, D --
    exact at every pixel, so no facet of the mesh can show in the shading --
    tilted against the slope's part along the surface, and unit length,

        n = normalize(D - (slope - (slope . D) D)),

    carried from the body's axes to world space, where LightDirection is.
    Unit, so N.L can never exceed 1 whatever the relief: it turns the light,
    it cannot add any."""
    along = g.mul(g.binary(unreal.MaterialExpressionDotProduct, slope, direction), direction)
    tangential = g.binary(unreal.MaterialExpressionSubtract, slope, along)
    local = g.unary(unreal.MaterialExpressionNormalize, g.binary(unreal.MaterialExpressionSubtract, direction, tangential))
    return g.unary(unreal.MaterialExpressionNormalize, to_world(g, axes, local))


def sky_body():
    """Planets and moons.

        N        = relief_normal                      (surface)
    (the face's every band from Shaders/Private/WorldRelief.ush, through one Custom node: landing decision 1)
        shaded   = gain * saturate(N.L) * smoothstep(-w, w, N.L)
        disc     = shaded * face                      (surface)
        emissive = Colour * Brightness * lerp(disc, 1, PointBlend)
                 + Rim * Brightness * fresnel * saturate(N.L) * (1 - PointBlend)

    N is the relief's normal in world space and L is LightDirection, the
    world-space direction from this body to *its* star, which the actor
    writes per body: a directional light has one direction, and a planet off
    the sun line would show the wrong phase under it. The gain is the
    contract's lambert_disc_gain, 1.5, which makes the lit disc average
    exactly SkyProjection::LambertPhase -- the point's brightness -- so the
    resolve keeps total flux. The smoothstep only rounds the terminator's
    last few percent. The night side is black: an unlit world is a hole in
    the stars, which is itself a way of seeing it.

    The relief shows where the light is low, as ground does: at the
    terminator a slope turns a pixel toward the sun or away from it, and the
    line breaks up into highlands catching the light and crater walls in
    shadow; under a high sun the same slopes change little. The rim is the
    atmosphere's, from the smooth sphere, and is what it was.
    """
    asset = "M_SkyBody"
    material = fresh_material(asset)
    g = Graph(material)

    colour = g.vector("colour", (1.0, 1.0, 1.0, 1.0))
    light = g.vector("light_direction", (0.0, 0.0, 1.0, 0.0))
    rim = g.vector("rim", (0.0, 0.0, 0.0, 1.0))
    seed = g.vector("surface_seed", (0.0, 0.0, 0.0, 0.0))
    brightness = g.scalar("brightness", 1.0)
    point_blend = g.scalar("point_blend", 1.0)
    knobs = (g.scalar("mottle", 0.35), g.scalar("detail", 0.3), g.scalar("banding", 0.0),
             g.scalar("relief_scale", 0.0), g.scalar("cratering", 0.0))

    axes = body_axes(g)
    direction, footprint = body_direction(g, axes)
    factor, slope = surface(g, knobs, seed, direction, footprint, shared_terms)
    normal = relief_normal(g, direction, slope, axes)
    n_dot_l = g.binary(unreal.MaterialExpressionDotProduct, normal, light)
    lambert = g.unary(unreal.MaterialExpressionSaturate, n_dot_l)

    width = float(CONSTANTS["terminator_width"])
    soft = g.node(unreal.MaterialExpressionSmoothStep, const_min=-width, const_max=width)
    g.link(n_dot_l, soft, "Value")

    shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))
    disc = g.mul(shaded, factor)

    blend = g.node(unreal.MaterialExpressionLinearInterpolate, const_b=1.0)
    g.link(disc, blend, "A")
    g.link(point_blend, blend, "Alpha")
    body = g.mul(g.mul(colour, brightness), blend)

    # The rim is the atmosphere's and follows the smooth limb: its own
    # N.L, from the vertex normal, so the relief never speckles the sky.
    smooth = g.unary(unreal.MaterialExpressionSaturate,
                     g.binary(unreal.MaterialExpressionDotProduct, g.node(unreal.MaterialExpressionVertexNormalWS), light))
    fresnel = g.node(unreal.MaterialExpressionFresnel, exponent=float(CONSTANTS["rim_exponent"]))
    resolved = g.unary(unreal.MaterialExpressionOneMinus, point_blend)
    rim_term = g.mul(g.mul(rim, brightness), g.mul(g.mul(fresnel, smooth), resolved))

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


def glass_veil(graph, collection):
    """The veil, VeilColour * MPC_Sky.Veil * MPC_Sky.InteriorLight: the room
    reflected in the glass, so a lit ship hides the faint stars and a dark one
    shows them all (sky decision 6). Returns the node to add to the glass's
    emissive, or None while MPC_Sky is pending.

    veil_colour is set by the spec's target: with the lights fully fed and
    ds.Sky.Veil at 1, the veil lands on screen at the pixel value of a flux-4
    star, so that about one star in eight shows through a lit room. A flux-4
    star is PointStarBrightness(4) = sqrt(4) * 0.01 * 3 = 0.06. Translucency
    blends the glass's emissive in at its opacity, 0.12, so the veil's own
    emissive is 0.06 / 0.12 = 0.5. The colour leans warm: what a window
    reflects is the room, and the rooms are lit mostly warm. Tune it in
    play with ds.Sky.Veil, and write the settled factor back here.
    """
    if collection is None:
        return None
    colour = graph.colour(CONSTANTS["veil_colour"])
    veil = graph.collection_scalar(collection, "veil")
    interior = graph.collection_scalar(collection, "interior_light")
    return graph.mul(graph.mul(colour, veil), interior)


def sky_glass(collections):
    """The cockpit and galley windows: translucent and unlit, a faint tint,
    and the room's reflection over whatever is outside.

        emissive = tint + veil; opacity = glass_opacity
    """
    asset = "M_SkyGlass"
    material = fresh_material(asset)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    g = Graph(material)
    emissive = g.colour(CONSTANTS["glass_tint"])
    veil = glass_veil(g, collections.get("MPC_Sky"))
    if veil is not None:
        emissive = g.add(emissive, veil)
    g.emissive(emissive)
    g.opacity(g.constant(CONSTANTS["glass_opacity"]))
    finish(material, asset)


def author_collection(asset, entry):
    """A Material Parameter Collection: global shader scalars any material can
    read and C++ can set once a frame. The defaults are what a material sees
    where nothing writes them -- the editor's own viewport, before play --
    and for MPC_Sky that is the lit ship, veil and all."""
    path = "%s/%s" % (DIRECTORY, asset)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        collection = unreal.EditorAssetLibrary.load_asset(path)
    else:
        collection = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            asset, DIRECTORY, unreal.MaterialParameterCollection,
            unreal.MaterialParameterCollectionFactoryNew())
    # A parameter that already exists keeps its entry, and with it its id:
    # a material holds a collection parameter by id and looks the name up
    # on load, so an id made fresh each run would turn every material that
    # reads this collection into one reading None, silently.
    existing = {str(p.get_editor_property("parameter_name")): p
                for p in collection.get_editor_property("scalar_parameters")}
    defaults = entry.get("defaults", {})
    scalars = []
    for role in entry["parameters"]:
        parameter = existing.get(name(role)) or unreal.CollectionScalarParameter()
        parameter.set_editor_property("parameter_name", name(role))
        parameter.set_editor_property("default_value", float(defaults.get(role, 0.0)))
        scalars.append(parameter)
    collection.set_editor_property("scalar_parameters", scalars)
    collection.set_editor_property("vector_parameters", [])
    unreal.EditorAssetLibrary.save_loaded_asset(collection, only_if_is_dirty=False)
    kept = [name(r) for r in entry["parameters"] if name(r) in existing]
    log("%s: scalars %s (ids kept for %s)" % (asset, [name(r) for r in entry["parameters"]], kept))
    return collection


def sky_body_mesh():
    """SM_SkyBody, the sphere every body is drawn with: an equal-angle cube
    sphere, one LOD per entry of the contract's cells_per_lod, built in C++
    (UDeepSpaceEditorScripting::BuildSkySphere, from SkySphereMesh) because
    Python can make a quarter of a million triangles only one call at a
    time. Rebuilt in place, so the level's reference to it survives."""
    asset = "SM_SkyBody"
    cells = [int(n) for n in CONTRACT["meshes"][asset]["cells_per_lod"]]
    mesh = unreal.DeepSpaceEditorScripting.build_sky_sphere("%s/%s" % (DIRECTORY, asset), cells)
    if mesh is None:
        raise RuntimeError("%s: BuildSkySphere made nothing" % asset)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    box = mesh.get_bounding_box()
    log("%s: %d LODs of %s cells, %d triangles at LOD 0; bounds %s .. %s"
        % (asset, mesh.get_num_lods(), cells, mesh.get_num_triangles(0), box.min, box.max))


def main():
    unreal.EditorAssetLibrary.make_directory(DIRECTORY)
    sky_body_mesh()
    collections = {}
    for asset, entry in CONTRACT["collections"].items():
        if "pending" in entry:
            log("%s: pending (%s)" % (asset, entry["pending"]))
        else:
            collections[asset] = author_collection(asset, entry)
    sky_body()
    sky_star()
    sky_starfield()
    sky_glass(collections)
    relief_probe("M_SkyReliefProbe", shared_terms)
    log("ok")


try:
    main()
finally:
    with open(os.path.join(unreal.Paths.project_saved_dir(), "setup_sky_materials.txt"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
