"""Author the sky's materials: M_SkyBody, M_SkyStar, M_SkyStarfield, M_SkyGlass,
and the parameter collection MPC_Sky that the glass reads.

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

def noise_band(g, position, offset, frequency, levels, filter_width, stretch):
    """One band of the face: GradientALU noise of `levels` octaves on
    position * frequency + offset, -1..1, mean zero.

    The noise gets its own pixel footprint through FilterWidth, which the
    engine's noise loop uses exactly as a fade: every octave is multiplied by
    saturate(1 - footprint * its frequency), so an octave is gone once its
    wavelength is under filter_pixels and all there by a few times that.
    That per-pixel fade is what makes the finer bands arrive as the world
    grows on screen -- and arrive near the ground before the horizon, where
    one pixel covers far more of it -- without anything counting pixels on
    the CPU. A faded octave contributes 0, the band's middle, so a world
    too small to hold its detail is exactly its coarse face.

    The frequency is applied here rather than as the node's Scale so the
    seed offset can be added after it: the offset is then a few hundred
    units against positions up to fifty thousand, and costs the finest band
    nothing in float precision. `stretch` is the most the position is
    stretched along any axis, so the footprint is taken at the highest
    frequency the band really has.
    """
    scaled = g.add(g.mul(position, g.constant(frequency)), offset)
    width = g.mul(g.mul(filter_width, g.constant(frequency)), stretch)
    noise = g.node(unreal.MaterialExpressionNoise,
                   scale=1.0, output_min=-1.0, output_max=1.0, levels=int(levels),
                   level_scale=float(CONSTANTS["level_scale"]), turbulence=False,
                   noise_function=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_ALU)
    # Position is the first input, connected by index: its pin is named for
    # a world-space origin it does not have here.
    g.link(scaled, noise, "")
    link_any(g, width, noise, ("FilterWidth", "Filter Width"))
    return noise


def link_any(g, source, target, names):
    """Connect to whichever spelling of a pin this engine uses."""
    for pin in names:
        if MEL.connect_material_expressions(source, "", target, pin):
            return
    raise RuntimeError("could not connect %s -> %s.%s" % (source.get_name(), target.get_name(), "/".join(names)))


def surface_face(g, mottle, detail, banding, seed):
    """The world's face: 1 + swing, the factor the shaded disc is multiplied
    by. Everything is a function of D, the unit direction to the pixel in
    object space -- normalize(LocalPosition) -- so the face is fixed to the
    body however the proxy is moved and rescaled each frame, and every
    frequency is in cycles per body radius, whatever mesh draws it.

        footprint = max(|ddx D|, |ddy D|) * filter_pixels
        offset    = SurfaceSeed.xyz            (where on the noise this world is)
        stretch   = lerp(1, belt_stretch, Banding)
        P         = D * (1, 1, stretch)

        coarse    = noise(P * continent_frequency + offset)
        rocky     = clamp(coarse * continent_contrast, -1, 1)
        belts     = sin(pi * pairs * (D.z + belt_warp * coarse)) * belt fade
        face      = lerp(rocky, belts, Banding) * Mottle
                  + sum_i detail_weight_i * noise(P * detail_frequency_i + offset_i) * Detail
        factor    = 1 + clamp(face, -max_swing, max_swing)

    Rock gets basins and highlands, steepened so they read as places with
    edges rather than weather; a giant gets belts parallel to its orbit (the
    universe's z is the system's pole) wandered by the same coarse noise,
    and the same detail bands stretched sixfold across the belts, so they
    streak along them as cloud does. Every term is centred on zero, so the
    disc keeps its flux on average through the resolve, and the clamp is
    the half-float guard: the face never more than doubles a pixel.
    """
    position = g.node(unreal.MaterialExpressionLocalPosition)
    direction = g.node(unreal.MaterialExpressionNormalize)
    g.link(position, direction, "", output_name="XYZ")

    ddx = g.unary(unreal.MaterialExpressionLength, g.unary(unreal.MaterialExpressionDDX, direction))
    ddy = g.unary(unreal.MaterialExpressionLength, g.unary(unreal.MaterialExpressionDDY, direction))
    footprint = g.mul(g.binary(unreal.MaterialExpressionMax, ddx, ddy), g.constant(CONSTANTS["filter_pixels"]))

    stretch = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=1.0, const_b=float(CONSTANTS["belt_stretch"]))
    g.link(banding, stretch, "Alpha")
    axes = g.binary(unreal.MaterialExpressionAppendVector,
                    g.node(unreal.MaterialExpressionConstant2Vector, r=1.0, g=1.0), stretch)
    offset = g.node(unreal.MaterialExpressionComponentMask, r=True, g=True, b=True, a=False)
    g.link(seed, offset)
    # The seed's w, from the parameter's own alpha pin: its default output
    # is only the colour's three channels.
    shape = g.node(unreal.MaterialExpressionMultiply, const_b=float(CONSTANTS["belt_pairs_range"]))
    g.link(seed, shape, "A", output_name="A")
    stretched = g.mul(direction, axes)

    def band_offset(index):
        # Each band from its own corner of the noise, so no band's features
        # sit on the one below's and the octaves read as separate scales.
        return g.add(offset, g.colour((37.0 * index, 59.0 * index, 83.0 * index)))

    coarse = noise_band(g, stretched, band_offset(0), CONSTANTS["continent_frequency"], CONSTANTS["continent_levels"],
                        footprint, stretch)

    rocky = g.node(unreal.MaterialExpressionClamp, min_default=-1.0, max_default=1.0)
    g.link(g.mul(coarse, g.constant(CONSTANTS["continent_contrast"])), rocky, "")

    # Belts: pairs light and dark from pole to pole, how many chosen by the
    # seed's w. They fade on the same footprint rule as the noise, so a
    # giant a few pixels across is not a moire of stripes.
    pairs = g.add(shape, g.constant(CONSTANTS["belt_pairs_min"]))
    latitude = g.node(unreal.MaterialExpressionComponentMask, r=False, g=False, b=True, a=False)
    g.link(direction, latitude)
    wandered = g.add(latitude, g.mul(coarse, g.constant(CONSTANTS["belt_warp"])))
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

    fine = None
    for index, (frequency, weight) in enumerate(zip(CONSTANTS["detail_frequencies"], CONSTANTS["detail_weights"]), 1):
        band = noise_band(g, stretched, band_offset(index), frequency, CONSTANTS["detail_levels"], footprint, stretch)
        term = g.mul(band, g.constant(weight))
        fine = term if fine is None else g.add(fine, term)
    face = g.add(face, g.mul(fine, detail))

    swing = float(CONSTANTS["surface_max_swing"])
    bounded = g.node(unreal.MaterialExpressionClamp, min_default=-swing, max_default=swing)
    g.link(face, bounded, "")
    return g.add(bounded, g.constant(1.0))


def sky_body():
    """Planets and moons.

        shaded   = gain * saturate(N.L) * smoothstep(-w, w, N.L)
        disc     = shaded * face                      (surface_face)
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

    The face multiplies the lit disc only: the terminator, the limb and the
    rim are the lighting's, and are what they were.
    """
    asset = "M_SkyBody"
    material = fresh_material(asset)
    g = Graph(material)

    colour = g.vector("colour", (1.0, 1.0, 1.0, 1.0))
    light = g.vector("light_direction", (0.0, 0.0, 1.0, 0.0))
    rim = g.vector("rim", (0.0, 0.0, 0.0, 1.0))
    seed = g.vector("surface_seed", (0.0, 0.0, 0.0, 0.5))
    brightness = g.scalar("brightness", 1.0)
    point_blend = g.scalar("point_blend", 1.0)
    mottle = g.scalar("mottle", 0.35)
    detail = g.scalar("detail", 0.3)
    banding = g.scalar("banding", 0.0)

    normal = g.node(unreal.MaterialExpressionVertexNormalWS)
    n_dot_l = g.binary(unreal.MaterialExpressionDotProduct, normal, light)
    lambert = g.unary(unreal.MaterialExpressionSaturate, n_dot_l)

    width = float(CONSTANTS["terminator_width"])
    soft = g.node(unreal.MaterialExpressionSmoothStep, const_min=-width, const_max=width)
    g.link(n_dot_l, soft, "Value")

    shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))
    disc = g.mul(shaded, surface_face(g, mottle, detail, banding, seed))

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


def main():
    unreal.EditorAssetLibrary.make_directory(DIRECTORY)
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
    log("ok")


try:
    main()
finally:
    with open(os.path.join(unreal.Paths.project_saved_dir(), "setup_sky_materials.txt"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
