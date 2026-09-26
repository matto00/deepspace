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


def fade(g, width, frequency):
    """saturate(1 - width * frequency): the engine noise loop's own fade,
    written out for the vector noise, which has no FilterWidth. An octave of
    this frequency is gone once its wavelength is under filter_pixels and
    all there by a few times that."""
    return g.unary(unreal.MaterialExpressionSaturate,
                   g.unary(unreal.MaterialExpressionOneMinus, g.mul(width, g.constant(frequency))))


def mask(g, source, channels, output_name=""):
    """A ComponentMask of `channels` ("rgb", "a") from source."""
    node = g.node(unreal.MaterialExpressionComponentMask,
                  r="r" in channels, g="g" in channels, b="b" in channels, a="a" in channels)
    g.link(source, node, "", output_name=output_name)
    return node


def detail_band(g, stretched, offset, frequency, width):
    """One detail band as simplex noise with its gradient, from the vector
    noise's Perlin Gradient: rgb the gradient in noise space, a the value,
    -1..1. The one evaluation feeds both the face (a) and the relief (rgb),
    so the ground that lights up and the ground that tilts are the same
    ground. Faded by the footprint, as the scalar bands fade themselves."""
    scaled = g.add(g.mul(stretched, g.constant(frequency)), offset)
    noise = g.node(unreal.MaterialExpressionVectorNoise,
                   noise_function=unreal.VectorNoiseFunction.VNF_GRADIENT_ALU)
    g.link(scaled, noise, "")
    return noise, fade(g, width, frequency)


def crater_band(g, direction, offset, frequency, footprint):
    """One band of craters: a Voronoi cell per crater site, its seed the
    crater's centre, and whether the site holds a crater at all from the
    cell's own hash, so the lattice the seeds are jittered from never shows.

    The cells are 3D and the sphere slices them, so a seed off the surface
    makes a smaller, shallower crater than one on it: sizes vary within a
    band without anything drawing them. Across bands the cells shrink by
    four and their number on the surface grows by sixteen, so the count of
    craters wider than D goes as D^-2 -- the size-frequency law of the
    Moon's and Mercury's highlands. That is the distribution; nothing here
    is uniform but where on the noise a world is.

    Returns (face, slope): the crater's albedo -- darker floor, brighter rim
    -- for the face, and its height's gradient for the relief. Over q, the
    distance from the centre in crater radii, the height is

        q < 1         depth * (q^2 - 1 + rim)          the bowl
        1 <= q < 1.5  depth * rim * (3 - 2q)^2         the rim falling away

    whose slope is depth * 2q inside and -4 depth rim (3 - 2q) outside, along
    the direction away from the centre."""
    radius = float(CONSTANTS["crater_radius"])
    depth = float(CONSTANTS["crater_depth"])
    rim = float(CONSTANTS["crater_rim"])

    scaled = g.add(g.mul(direction, g.constant(frequency)), offset)
    cells = g.node(unreal.MaterialExpressionVectorNoise,
                   noise_function=unreal.VectorNoiseFunction.VNF_VORONOI_ALU, quality=1)
    g.link(scaled, cells, "")
    centre = mask(g, cells, "rgb")
    distance = mask(g, cells, "a")

    # Every seed is within 0.26 of its lattice corner, so the corner --
    # floor(seed + 0.5) -- names the cell, and the hash of it decides once
    # and for good whether this site is a crater.
    site = g.add(centre, g.constant(0.5))
    hashed = g.node(unreal.MaterialExpressionVectorNoise,
                    noise_function=unreal.VectorNoiseFunction.VNF_CELLNOISE_ALU)
    g.link(site, hashed, "")
    held = g.node(unreal.MaterialExpressionStep, const_x=0.0)
    g.link(mask(g, hashed, "r"), held, "Y")
    g.link(g.constant(CONSTANTS["crater_keep"]), held, "X")

    q = g.mul(distance, g.constant(1.0 / radius))
    outside = g.node(unreal.MaterialExpressionStep, const_y=1.0)
    g.link(q, outside, "X")
    inside = g.unary(unreal.MaterialExpressionOneMinus, outside)
    falling = g.unary(unreal.MaterialExpressionSaturate,
                      g.add(g.mul(q, g.constant(-2.0)), g.constant(3.0)))

    wall = g.add(g.mul(g.mul(q, g.constant(2.0 * depth)), inside),
                 g.mul(g.mul(falling, g.constant(-4.0 * depth * rim)), outside))
    # Away from the centre, unit -- over a floored distance rather than
    # normalised, because at the exact centre a normalised zero is NaN, and a
    # NaN pixel is a black hole the bloom spreads. The wall's slope is 0 there.
    apart = g.binary(unreal.MaterialExpressionMax, distance, g.constant(1.0e-4))
    away = g.binary(unreal.MaterialExpressionDivide, g.binary(unreal.MaterialExpressionSubtract, scaled, centre), apart)

    floor_dark = g.mul(g.unary(unreal.MaterialExpressionOneMinus, g.mul(q, q)),
                       g.mul(inside, g.constant(-float(CONSTANTS["crater_floor_dark"]))))
    rim_bright = g.mul(g.mul(falling, outside), g.constant(float(CONSTANTS["crater_rim_bright"])))

    weight = g.mul(held, fade(g, footprint, frequency))
    return g.mul(g.add(floor_dark, rim_bright), weight), g.mul(g.mul(away, wall), weight)


def surface(g, knobs, seed):
    """The world's face and relief, both from the object-space noise: the
    face is the factor the shaded disc is multiplied by, 1 + swing, and the
    relief is the gradient of a height field the normal is tilted by.
    Everything is a function of D, the unit direction to the pixel in object
    space -- normalize(LocalPosition) -- so both are fixed to the body however
    the proxy is moved and rescaled each frame, and every frequency is in
    cycles per body radius, whatever mesh draws it.

        footprint = max(|ddx D|, |ddy D|) * filter_pixels
        offset    = SurfaceSeed.xyz            (where on the noise this world is)
        pairs     = SurfaceSeed.w              (a giant's belts, from its day)
        stretch   = lerp(1, belt_stretch, Banding)
        P         = D * (1, 1, stretch)

        coarse    = noise(P * continent_frequency + offset)
        rocky     = clamp(coarse * continent_contrast, -1, 1)
        belts     = sin(pi * pairs * (D.z + belt_warp * coarse)) * belt fade
        band_i    = simplex(P * detail_frequency_i + offset_i) * fade_i * detail_weight_i
        craters   = Cratering * lerp(maria_cratering, 1, highland) * sum_c crater_c
        face      = lerp(rocky, belts, Banding) * Mottle + sum_i band_i.a * Detail + craters.albedo
        factor    = 1 + clamp(face, -max_swing, max_swing)
        slope     = Relief * lerp(1, relief_giant, Banding) * sum_i band_i.rgb * (1, 1, stretch)
                  + craters.slope

    Rock gets basins and highlands, steepened so they read as places with
    edges rather than weather, and craters on the highlands, fewer in the
    basins, as the maria of the Moon have; a giant gets belts parallel to its
    orbit (the universe's z is the system's pole) wandered by the same coarse
    noise, the same detail stretched sixfold across the belts so it streaks
    as cloud does, and a third of rock's relief, the billow of cloud tops.

    Each band's height is its value over its frequency -- its amplitude goes
    with its wavelength -- so its slope is the same at every scale: the
    ground the screen can hold has the same relief close in as far out, and
    the finer it gets the more of it there is, which is what says how near.
    The face is centred on zero, so the disc keeps its flux on average, and
    the clamp is the half-float guard: the face never more than doubles a
    pixel, and the relief only turns a unit normal.
    """
    mottle, detail, banding, relief, cratering = knobs
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
    # The seed's w, a giant's belt pairs, from the parameter's own alpha pin:
    # its default output is only the colour's three channels.
    pairs = g.node(unreal.MaterialExpressionMultiply, const_b=1.0)
    g.link(seed, pairs, "A", output_name="A")
    stretched = g.mul(direction, axes)

    def band_offset(index):
        # Each band from its own corner of the noise, so no band's features
        # sit on the one below's and the octaves read as separate scales.
        return g.add(offset, g.colour((37.0 * index, 59.0 * index, 83.0 * index)))

    coarse = noise_band(g, stretched, band_offset(0), CONSTANTS["continent_frequency"], CONSTANTS["continent_levels"],
                        footprint, stretch)

    rocky = g.node(unreal.MaterialExpressionClamp, min_default=-1.0, max_default=1.0)
    g.link(g.mul(coarse, g.constant(CONSTANTS["continent_contrast"])), rocky, "")

    # Belts: pairs light and dark from pole to pole, as many as the giant's
    # day gives it. They fade on the same footprint rule as the noise, so a
    # giant a few pixels across is not a moire of stripes.
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

    # One octave per detail band, every one at the contract's weight, so a
    # band arriving on screen reads as strongly at the approach's floor as at
    # its start. The value and gradient sum together, four channels at once.
    width = g.mul(footprint, stretch)
    fine = None
    for index, (frequency, weight) in enumerate(zip(CONSTANTS["detail_frequencies"], CONSTANTS["detail_weights"]), 1):
        noise, faded = detail_band(g, stretched, band_offset(index), frequency, width)
        term = g.mul(noise, g.mul(faded, g.constant(weight)))
        fine = term if fine is None else g.add(fine, term)
    face = g.add(face, g.mul(mask(g, fine, "a"), detail))

    # Craters, where the look says the ground keeps them, and more on the
    # highlands than in the basins.
    crater_face = None
    crater_slope = None
    for index, frequency in enumerate(CONSTANTS["crater_frequencies"], 1):
        albedo, slope = crater_band(g, direction, band_offset(100 + index), frequency, footprint)
        crater_face = albedo if crater_face is None else g.add(crater_face, albedo)
        crater_slope = slope if crater_slope is None else g.add(crater_slope, slope)
    highland = g.unary(unreal.MaterialExpressionSaturate, g.add(g.mul(rocky, g.constant(0.5)), g.constant(0.5)))
    marked = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=float(CONSTANTS["maria_cratering"]), const_b=1.0)
    g.link(highland, marked, "Alpha")
    crater_gain = g.mul(cratering, marked)
    face = g.add(face, g.mul(crater_face, crater_gain))

    swing = float(CONSTANTS["surface_max_swing"])
    bounded = g.node(unreal.MaterialExpressionClamp, min_default=-swing, max_default=swing)
    g.link(face, bounded, "")
    factor = g.add(bounded, g.constant(1.0))

    cloud = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=1.0, const_b=float(CONSTANTS["relief_giant"]))
    g.link(banding, cloud, "Alpha")
    slope = g.add(g.mul(g.mul(mask(g, fine, "rgb"), axes), g.mul(relief, cloud)),
                  g.mul(crater_slope, crater_gain))
    return factor, slope, direction


def relief_normal(g, direction, slope):
    """The world-space normal of the relief: the sphere's own normal, D --
    exact at every pixel, so no facet of the mesh can show in the shading --
    tilted against the slope's part along the surface, and unit length,

        n = normalize(D - (slope - (slope . D) D)),

    carried to world space, where LightDirection is. Unit, so N.L can never
    exceed 1 whatever the relief: it turns the light, it cannot add any."""
    along = g.mul(g.binary(unreal.MaterialExpressionDotProduct, slope, direction), direction)
    tangential = g.binary(unreal.MaterialExpressionSubtract, slope, along)
    local = g.unary(unreal.MaterialExpressionNormalize, g.binary(unreal.MaterialExpressionSubtract, direction, tangential))
    world = g.node(unreal.MaterialExpressionTransform,
                   transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
                   transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    g.link(local, world, "")
    return g.unary(unreal.MaterialExpressionNormalize, world)


def sky_body():
    """Planets and moons.

        N        = relief_normal                      (surface)
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
             g.scalar("relief", 0.2), g.scalar("cratering", 0.0))

    factor, slope, direction = surface(g, knobs, seed)
    normal = relief_normal(g, direction, slope)
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
    log("ok")


try:
    main()
finally:
    with open(os.path.join(unreal.Paths.project_saved_dir(), "setup_sky_materials.txt"), "w") as f:
        f.write("\n".join(REPORT) + "\n")
