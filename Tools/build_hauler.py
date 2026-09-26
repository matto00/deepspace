"""
Builds Content/Maps/L_Hauler from Tools/hauler_layout.py.

The .umap is a build output. The layout is the source of truth: change it and
re-run, never nudge built actors by hand. Validate first -- it takes about a
second and needs no editor:

    python3 Tools/validate_hauler.py

Then build, and check the result matches the layout:

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/build_hauler.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/hauler_build.txt

`unreal.log` does not reach stdout under the commandlet, so the summary is
written to Saved/hauler_build.txt.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hauler_layout as L
import placement as PL

MAP_PATH = "/Game/Maps/L_Hauler"
MATERIAL_DIR = "/Game/Materials"
CONSOLE_BP = "/Game/Blueprints/BP_ShipConsole"
GAMEMODE_BP = "/Game/Blueprints/BP_DeepSpaceGameMode"
GRID_MATERIAL = "/Game/LevelPrototyping/Materials/M_PrototypeGrid"

MESHES = {
    "cube": "/Game/LevelPrototyping/Meshes/SM_Cube",
    "chamfer": "/Game/LevelPrototyping/Meshes/SM_ChamferCube",
    "cylinder": "/Game/LevelPrototyping/Meshes/SM_Cylinder",
}
SPHERE = PL.SPHERE

# Every actor the script owns carries this prefix and is rebuilt each run.
TAG = "hauler_"

# The consumer group the ship's lights belong to; ShipPower::Lights in C++.
LIGHTS_TAG = PL.LIGHTS_TAG
TEMPLATE_CRUFT = ("Floor", "SM_SkySphere")
# Everything the level template brought from a planet's surface. The sun is
# among them: there is exactly one, AShipSky's, and it is where the local star
# is (sky decision 5). The SkyLight goes too. It had no cubemap to light with,
# so it added nothing, and a sky light is the one other way the outside could
# reach the deck; nothing but the sun through the glass should.
SPACE_STRIPS = (unreal.SkyAtmosphere, unreal.VolumetricCloud, unreal.ExponentialHeightFog,
                unreal.DirectionalLight, unreal.SkyLight)

TEAL = (0.05, 0.55, 0.55)

# Clean retro-future. Panelled roles are instances of the template's
# world-aligned grid material: seams are computed from world position, so they
# stay a constant size on boxes scaled to any proportion, and line up across
# neighbouring boxes. (surface colour, seam colour, panel size cm, roughness)
PANELLED = {
    "wall":      ((0.78, 0.80, 0.82), (0.60, 0.62, 0.65), 120, 0.45),
    "floor":     ((0.52, 0.51, 0.49), (0.38, 0.37, 0.36), 100, 0.65),
    "ceiling":   ((0.85, 0.86, 0.88), (0.70, 0.71, 0.73), 120, 0.50),
    "furniture": ((0.70, 0.71, 0.72), (0.58, 0.59, 0.60), 60,  0.35),
    "trim":      (TEAL,               (0.03, 0.40, 0.40), 60,  0.35),
    "seal":      ((0.40, 0.42, 0.45), (0.05, 0.45, 0.45), 40,  0.40),
    # The skirting boots and trolleys leave on every bulkhead: dark,
    # grey-green, rough (lived-in decision 12).
    "kick":      ((0.20, 0.23, 0.21), (0.14, 0.16, 0.15), 60,  0.75),
    # Wear, drawn per furniture piece at world start by the dressing
    # subsystem, which swaps a whole piece to one of these. Faded is bleached
    # and yellowed by years of the same lamps; replaced is a newer panel in a
    # slightly different, cooler grey, because it was swapped out.
    "furniture_faded":    ((0.78, 0.77, 0.73), (0.68, 0.67, 0.64), 60, 0.55),
    "furniture_replaced": ((0.66, 0.70, 0.74), (0.54, 0.58, 0.62), 60, 0.25),
}

# The dressing's plain roles: what clutter is made of, selected by name at
# runtime by UShipDressingSubsystem (MI_Ship_<role>), so they are authored
# here although nothing in the level wears them -- ADR 0006's runtime path,
# pre-authored instances selected and parameterised. Seam equals surface: a
# plain colour, with no new material asset. (colour, roughness)
PLAIN = {
    "ceramic":      ((0.82, 0.80, 0.74), 0.30),
    "metal":        ((0.50, 0.52, 0.55), 0.35),
    "rubber":       ((0.08, 0.08, 0.09), 0.90),
    "paper":        ((0.86, 0.83, 0.72), 0.80),
    # The ship's issue colours, drab on purpose: the few bright things
    # aboard are the personal ones.
    "fabric_olive": ((0.24, 0.27, 0.14), 0.85),
    "fabric_navy":  ((0.08, 0.11, 0.22), 0.85),
    "fabric_rust":  ((0.45, 0.17, 0.08), 0.85),
    "fabric_ochre": ((0.65, 0.45, 0.10), 0.85),
    "paint_red":    ((0.55, 0.07, 0.05), 0.50),
    "paint_yellow": ((0.80, 0.60, 0.05), 0.50),
}
PANELLED.update({role: (colour, colour, 60, roughness) for role, (colour, roughness) in PLAIN.items()})
# Emissive roles: an unlit colour, brighter than 1 to read as a light source.
# Each room's lamp panels glow its mood, so the panel and the light under it
# are one colour, taken from one number.
EMISSIVE = {
    "accent": (TEAL[0] * 4, TEAL[1] * 4, TEAL[2] * 4),
    "screen": (0.02, 0.10, 0.12),
}
EMISSIVE.update({PL.lamp_role(room): PL.lamp_emissive(mood)
                 for room, mood in L.ROOM_MOOD.items()})


# -- materials ---------------------------------------------------------------

def _asset(path):
    return unreal.EditorAssetLibrary.load_asset(path) if \
        unreal.EditorAssetLibrary.does_asset_exist(path) else None


def _create(name, cls, factory):
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_DIR, cls, factory)


def _save(asset):
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)


def panelled_instance(role, surface, seam, panel, roughness):
    """A material instance of the template grid, (re)configured every build so
    the numbers in PANELLED are always what is on screen."""
    path = "%s/MI_Ship_%s" % (MATERIAL_DIR, role)
    mi = _asset(path) or _create("MI_Ship_" + role, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, unreal.EditorAssetLibrary.load_asset(GRID_MATERIAL))
    colour = lambda c: unreal.LinearColor(c[0], c[1], c[2], 1.0)
    mel.set_material_instance_vector_parameter_value(mi, "SurfaceColor", colour(surface))
    mel.set_material_instance_vector_parameter_value(mi, "GridColor", colour(seam))
    # Hide the template's sub-grid: one seam per panel reads as panelling,
    # five reads as graph paper.
    mel.set_material_instance_vector_parameter_value(mi, "SubGridColor", colour(surface))
    mel.set_material_instance_scalar_parameter_value(mi, "Grid Size", float(panel))
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(roughness))
    mel.set_material_instance_static_switch_parameter_value(mi, "ObjectAligned", False)
    mel.set_material_instance_static_switch_parameter_value(mi, "Grid", True)
    mel.update_material_instance(mi)
    _save(mi)
    return mi


def emissive_base():
    """One unlit material with a colour parameter; emissive roles instance it."""
    path = MATERIAL_DIR + "/M_ShipEmissive"
    existing = _asset(path)
    if existing:
        return existing
    mat = _create("M_ShipEmissive", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    param = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -300, 0)
    param.set_editor_property("parameter_name", "Colour")
    param.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    unreal.MaterialEditingLibrary.connect_material_property(
        param, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    _save(mat)
    return mat


def emissive_instance(role, colour, base):
    path = "%s/MI_Ship_%s" % (MATERIAL_DIR, role)
    mi = _asset(path) or _create("MI_Ship_" + role, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, base)
    mel.set_material_instance_vector_parameter_value(
        mi, "Colour", unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
    mel.update_material_instance(mi)
    _save(mi)
    return mi


def sky_asset(name, required=True):
    """One of the sky's authored assets. They are made by
    Tools/setup_sky_materials.py, not here, so a missing one means that has
    not run: say so rather than build a level that draws every star alike."""
    existing = _asset(PL.sky_package(name))
    if existing or not required:
        return existing
    raise RuntimeError("%s is missing; run Tools/setup_sky_materials.py first" % PL.sky_package(name))


def materials():
    """Role name -> material. Surfaces name a role, never a material, so
    swapping in real textures later is a change here alone."""
    out = {role: panelled_instance(role, *spec) for role, spec in PANELLED.items()}
    base = emissive_base()
    out.update({role: emissive_instance(role, c, base) for role, c in EMISSIVE.items()})
    # The sky's glass: unlit and translucent, so the lamps add nothing to it
    # and the stars behind stay honest under fixed exposure (sky decision 6).
    out["glass"] = sky_asset("M_SkyGlass")
    return out


# -- actors ------------------------------------------------------------------

def mesh_bounds(mesh):
    b = mesh.get_bounding_box()
    return (b.min.x, b.min.y, b.min.z), (b.max.x, b.max.y, b.max.z)


def spawn_box(actor_sub, box, mesh, material):
    """Place `mesh` so it exactly fills `box`, whatever the mesh's pivot.

    The three meshes disagree about their origin: SM_Cube's is at a corner,
    SM_ChamferCube's at its centre, SM_Cylinder's at its base. So scale comes
    from the mesh's measured extent and the offset from its measured centre;
    no convention is assumed. No rotation is ever needed: every part has been
    rotated into an axis-aligned size already, and a cube, chamfered cube or
    cylinder turned 90 degrees about Z is the same shape with its X and Y sizes
    swapped.
    """
    lo, hi = mesh_bounds(mesh)
    scale = [box.size[a] / (hi[a] - lo[a]) for a in range(3)]
    mesh_centre = [(lo[a] + hi[a]) / 2.0 for a in range(3)]
    location = unreal.Vector(*[box.centre[a] - mesh_centre[a] * scale[a] for a in range(3)])
    actor = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, location,
                                             unreal.Rotator(0, 0, 0))
    actor.set_actor_label(TAG + box.label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_material(0, material)
    # Every glowing box -- a ceiling panel, or a prop's lamp part, which
    # resolve_props has already renamed to its room's lamp_ role -- is found
    # by UShipLightingSubsystem through this tag and dimmed with the lights.
    # It can stay Static: C++ drives its Colour through a dynamic material
    # instance and never moves it.
    if box.role.startswith("lamp_"):
        actor.set_editor_property("tags", [unreal.Name(PL.LAMPS_TAG)])
    # Every furniture part may wear, and wears with the rest of its piece:
    # the dressing subsystem draws one bucket per Piece.<prop>_<n> and swaps
    # the material of every part carrying it. The draw is seeded, so the
    # layout has no say in it; this only says which parts are one piece.
    if box.role == "furniture" and box.label.startswith("prop_"):
        actor.set_editor_property("tags", [unreal.Name(PL.WEAR_TAG),
                                           unreal.Name(PL.PIECE_TAG_PREFIX + PL.piece_of(box.label))])
    # The sunlight on the deck is the shape of the windows, which it can only
    # be if the glass in them casts no shadow (sky decision 5).
    if box.role == "glass":
        component.set_editor_property("cast_shadow", False)
        # And it is found as glass by tag: the target bracket's trace treats a
        # hit on an actor tagged GLASS_TAG as the glass and anything else as
        # a wall, so the bracket is drawn only where the target can be seen
        # (system map spec, decision 7). ShipTags::Glass in C++.
        actor.set_editor_property("tags", [unreal.Name(PL.GLASS_TAG)])
    return actor


def clear_previous(actor_sub):
    removed = 0
    for actor in actor_sub.get_all_level_actors():
        label = actor.get_actor_label()
        if (label.startswith(TAG) or label in TEMPLATE_CRUFT
                or isinstance(actor, SPACE_STRIPS)
                or isinstance(actor, unreal.PlayerStart)):
            actor_sub.destroy_actor(actor)
            removed += 1
    return removed


def place_lights(actor_sub, lights):
    for light in lights:
        actor = actor_sub.spawn_actor_from_class(
            unreal.PointLight, unreal.Vector(*light.location), unreal.Rotator(0, 0, 0))
        actor.set_actor_label(TAG + light.label)
        # An actor tag, not a label: labels are for humans and indices change
        # whenever the layout does, but a tag is a contract the generator can
        # keep and C++ can rely on. UShipLightingSubsystem finds every light
        # in the ship this way and nothing else.
        actor.set_editor_property("tags", [unreal.Name(LIGHTS_TAG)])
        c = actor.get_component_by_class(unreal.PointLightComponent)
        # Movable, because the lights dim: a Static light is baked and cannot
        # change intensity or colour at runtime at all.
        c.set_mobility(unreal.ComponentMobility.MOVABLE)
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        c.set_editor_property("intensity", float(light.intensity))
        c.set_editor_property("attenuation_radius", float(light.radius))
        # The colour itself, never bUseTemperature: the lighting subsystem
        # lerps this colour to amber as a light browns out, and a temperature
        # would tint on top of it, doubly orange and unverifiable.
        r, g, b = light.colour
        c.set_editor_property("light_color", unreal.Color(r=r, g=g, b=b, a=255))
        c.set_editor_property("use_temperature", False)
        # Only the practicals cast shadows. The ceiling grid is soft, even
        # fill, which is also what keeps thirty-odd lights cheap.
        c.set_editor_property("cast_shadows", bool(light.shadows))


def place_counter_frame(actor_sub, sphere):
    """The parent of everything outside the hull (ADR 0005).

    The ship never moves: the universe is drawn through the inverse of where
    the ship is and which way it points, and this actor carries the rotation
    half of that. Its starfield is generated in C++ at BeginPlay, so the stars
    are runtime state belonging to the ship's frame rather than rows in a
    binary .umap. All this script does is hand it the mesh and materials --
    asset assignment only, per ADR 0002.

    Each layer has its own material because each asks something different of
    it. The dome is the galaxy: M_SkyStarfield reads every star's colour and
    brightness from its instance data, and any other material draws all
    3,000 alike. The motes are dust at cruise: M_SkyStar has the Brightness
    they fade by under the drive, and the course marker is tinted from it.
    """
    frame = actor_sub.spawn_actor_from_class(
        unreal.ShipCounterFrame, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    frame.set_actor_label(TAG + "counterframe")
    for layer, material in (("distant_stars", "M_SkyStarfield"), ("near_stars", "M_SkyStar")):
        component = frame.get_editor_property(layer)
        component.set_static_mesh(sphere)
        component.set_material(0, sky_asset(material))
    return frame


def place_sky(actor_sub):
    """Everything outside the glass that is somewhere: the local star, its
    planets, the neighbours, the one sun on the deck and the fixed exposure
    (sky spec). AShipSky builds all of it at runtime from what the ship
    subsystem answers; the level holds one of it, at the origin, with its
    assets. It attaches itself to the counter-frame at BeginPlay, so where
    it is placed does not matter, and the origin says so.

    MPC_Sky is the glass veil's. It is required: a sky built without it
    writes nothing to the glass, and the veil sits at the collection's
    lit-room defaults whatever the lights do, with nothing in play to say so.

    The bodies are drawn with SM_SkyBody, not the engine Sphere the
    counter-frame's points use: a world close enough to fill the glass shows
    the engine Sphere's polygon on its limb, and the limb is how near it is.
    """
    sky = actor_sub.spawn_actor_from_class(
        unreal.ShipSky, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    sky.set_actor_label(TAG + "sky")
    sky.set_editor_property("body_mesh", sky_asset("SM_SkyBody"))
    sky.set_editor_property("body_material", sky_asset("M_SkyBody"))
    sky.set_editor_property("star_material", sky_asset("M_SkyStar"))
    sky.set_editor_property("point_star_material", sky_asset("M_SkyStarfield"))
    sky.set_editor_property("sky_parameters", sky_asset("MPC_Sky"))
    return sky


def place_nav_screen(actor_sub, ship):
    """The chart, over the starboard desk screen (nav spec B3). The three
    seat tunables are the chair playtest's knobs, per instance, so a nudge is
    this function and a level rebuild rather than C++: use_distance_cm lands
    the body on the starboard chair's centre (cockpit x 175 against the glass
    at 301), seat_height_cm is the cushion's top, and view_distance_cm is how
    far the eyes lean in to read. Everything else -- the panel's size, the
    widget, the reach volume -- is the class's, per ADR 0002."""
    chart = actor_sub.spawn_actor_from_class(
        unreal.ShipNavScreen, unreal.Vector(*ship.nav_screen_location),
        unreal.Rotator(0, 0, ship.nav_screen_yaw))
    chart.set_actor_label(TAG + "nav_screen")
    chart.set_editor_property("use_distance_cm", 126.0)
    chart.set_editor_property("seat_height_cm", 55.0)
    chart.set_editor_property("view_distance_cm", 60.0)
    return chart


def place_map_screen(actor_sub, ship):
    """The system map, over the middle desk screen (system map spec, decision
    1). It has no chair and no reach volume: it is read and clicked from the
    helm, and zoomed from the chart chair beside it (decision 13), so the one
    tunable it takes is view_distance_cm, how far the eyes lean in when the
    chart chair zooms it -- the chart's 60, so the two frame alike. Its size,
    its draw size and its widget are the class's, per ADR 0002.

    AShipMapScreen is C++ (Ship/ShipMapScreen.h). An editor built without it
    would save a level with no map and nothing in play would say why, so a
    missing class stops the build before the level is saved, as a missing
    sky asset does."""
    if not hasattr(unreal, "ShipMapScreen"):
        raise RuntimeError("AShipMapScreen is not compiled into this editor; build the map "
                           "screen's C++ before rebuilding the level")
    screen = actor_sub.spawn_actor_from_class(
        unreal.ShipMapScreen, unreal.Vector(*ship.map_screen_location),
        unreal.Rotator(0, 0, ship.map_screen_yaw))
    screen.set_actor_label(TAG + "map_screen")
    screen.set_editor_property("view_distance_cm", float(L.MAP_VIEW_DISTANCE))
    return screen


DRESS_EDGES = {"-x": unreal.DressEdge.NEG_X, "+x": unreal.DressEdge.POS_X}
DRESS_USES = {"centre": unreal.DressUse.CENTRE, "+y": unreal.DressUse.POS_Y, "-y": unreal.DressUse.NEG_Y}


def place_surfaces(actor_sub, ship):
    """Where the dressing may leave things: one AShipDressingSurface per
    surface the layout exports, tagged Dress.Surface (lived-in decision 1b).
    The markers carry data and no logic (ADR 0002); UShipDressingSubsystem
    finds them by the tag at world start and dresses them in C++. They are
    written in the same run as the furniture they lie on, so the two cannot
    disagree without verify_level.py failing."""
    for m in ship.surfaces:
        actor = actor_sub.spawn_actor_from_class(
            unreal.ShipDressingSurface, unreal.Vector(*m.location), unreal.Rotator(0, 0, m.yaw))
        actor.set_actor_label(TAG + m.label)
        actor.set_editor_property("tags", [unreal.Name(PL.SURFACE_TAG)])
        actor.set_editor_property("room", unreal.Name(m.room))
        actor.set_editor_property("kind", unreal.Name(m.kind))
        actor.set_editor_property("ordinal", int(m.ordinal))
        actor.set_editor_property("size", unreal.Vector2D(float(m.size[0]), float(m.size[1])))
        actor.set_editor_property("back", DRESS_EDGES[m.back])
        actor.set_editor_property("use", DRESS_USES[m.use])
        actor.set_editor_property("clear", float(m.clear))
        actor.set_editor_property("excludes", [
            unreal.Box2D(min=unreal.Vector2D(float(lo[0]), float(lo[1])),
                         max=unreal.Vector2D(float(hi[0]), float(hi[1])))
            for lo, hi in m.excludes])


def place_keep_outs(actor_sub, ship):
    """What the dressing may never touch, as AShipDressingKeepOut boxes
    tagged Dress.KeepOut: each door's and the console's keep-clear zone, the
    slide run, the crawlway. validate_hauler.py cannot see runtime clutter,
    so ShipDressing::Dress enforces these itself."""
    for k in ship.keep_outs:
        centre = [(k.lo[a] + k.hi[a]) / 2.0 for a in range(3)]
        actor = actor_sub.spawn_actor_from_class(
            unreal.ShipDressingKeepOut, unreal.Vector(*centre), unreal.Rotator(0, 0, 0))
        actor.set_actor_label(TAG + k.label)
        actor.set_editor_property("tags", [unreal.Name(PL.KEEP_OUT_TAG)])
        actor.set_editor_property("reason", unreal.Name(k.label[len("keepout_"):]))
        actor.set_editor_property("size", unreal.Vector(*[float(k.hi[a] - k.lo[a]) for a in range(3)]))


HUM_KINDS = {"reactor": unreal.ShipHumKind.REACTOR, "air": unreal.ShipHumKind.AIR}


def place_hum_sources(actor_sub, ship):
    """Where the ship is heard from: the reactor, and each room's air. The
    script says where each stands and which voice it is and nothing more;
    the voice is C++ (ADR 0002). The hum component is the actor's root, so
    there is no child to strand at the origin (the Static-child trap)."""
    for label, location, kind in ship.hum_sources:
        actor = actor_sub.spawn_actor_from_class(
            unreal.ShipHumSource, unreal.Vector(*location), unreal.Rotator(0, 0, 0))
        actor.set_actor_label(TAG + label)
        actor.set_editor_property("kind", HUM_KINDS[kind])


def build():
    ship = L.generate()                      # raises PlanError on a bad plan

    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        level_sub.load_level(MAP_PATH)
    else:
        level_sub.new_level(MAP_PATH)

    removed = clear_previous(actor_sub)

    mats = materials()
    meshes = {k: unreal.EditorAssetLibrary.load_asset(p) for k, p in MESHES.items()}
    for box in ship.boxes:
        spawn_box(actor_sub, box, meshes[box.mesh], mats[box.role])

    place_lights(actor_sub, ship.lights)
    place_hum_sources(actor_sub, ship)
    place_surfaces(actor_sub, ship)
    place_keep_outs(actor_sub, ship)
    sphere = unreal.EditorAssetLibrary.load_asset(SPHERE)
    place_counter_frame(actor_sub, sphere)
    place_sky(actor_sub)

    console = actor_sub.spawn_actor_from_class(
        unreal.EditorAssetLibrary.load_blueprint_class(CONSOLE_BP),
        unreal.Vector(*ship.console_location), unreal.Rotator(0, 0, ship.console_yaw))
    console.set_actor_label(TAG + "console")
    console_mesh = console.get_component_by_class(unreal.StaticMeshComponent)
    if console_mesh:
        console_mesh.set_material(0, mats["furniture"])

    # The laptop on the galley table: the allocation editor. A C++ actor with
    # a world-space widget on it; the script only hands it meshes and a
    # material, per ADR 0002.
    laptop = actor_sub.spawn_actor_from_class(
        unreal.ShipLaptop, unreal.Vector(*ship.laptop_location),
        unreal.Rotator(0, 0, ship.laptop_yaw))
    laptop.set_actor_label(TAG + "laptop")
    for part in ("base", "lid"):
        component = laptop.get_editor_property(part)
        component.set_static_mesh(meshes["chamfer"])
        component.set_material(0, mats["furniture"])
    laptop.fit_parts()

    place_nav_screen(actor_sub, ship)
    place_map_screen(actor_sub, ship)

    start = actor_sub.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(*ship.player_start), unreal.Rotator(0, 0, 0))
    start.set_actor_label(TAG + "player_start")

    # A C++ actor with no mesh of its own: it sits over the pilot_seat prop
    # and carries the interaction that puts the ship in pilot mode.
    seat = actor_sub.spawn_actor_from_class(
        unreal.PilotSeat, unreal.Vector(*ship.pilot_seat_location),
        unreal.Rotator(0, 0, ship.pilot_seat_yaw))
    seat.set_actor_label(TAG + "pilot_seat")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property(
        "default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(GAMEMODE_BP))

    level_sub.save_current_level()

    summary = ("L_Hauler built: removed %d, placed %d boxes, %d lights (%d practical, "
               "shadowed), %d lamp boxes tagged %s, %d glass panes casting no shadow, "
               "tagged %s, %d hum sources, %d dressing surfaces, %d keep-outs, %d furniture "
               "parts tagged %s, one chart, one map, one counter-frame, one sky."
               % (removed, len(ship.boxes), len(ship.lights),
                  sum(1 for light in ship.lights if light.shadows),
                  sum(1 for box in ship.boxes if box.role.startswith("lamp_")), PL.LAMPS_TAG,
                  sum(1 for box in ship.boxes if box.role == "glass"), PL.GLASS_TAG,
                  len(ship.hum_sources), len(ship.surfaces), len(ship.keep_outs),
                  sum(1 for box in ship.boxes if box.role == "furniture"), PL.WEAR_TAG))
    with open(os.path.join(unreal.Paths.project_saved_dir(), "hauler_build.txt"), "w") as f:
        f.write(summary + "\n")
    unreal.log(summary)


build()
