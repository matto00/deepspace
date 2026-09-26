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
}
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
    # The sunlight on the deck is the shape of the windows, which it can only
    # be if the glass in them casts no shadow (sky decision 5).
    if box.role == "glass":
        component.set_editor_property("cast_shadow", False)
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


def place_sky(actor_sub, sphere):
    """Everything outside the glass that is somewhere: the local star, its
    planets, the neighbours, the one sun on the deck and the fixed exposure
    (sky spec). AShipSky builds all of it at runtime from what the ship
    subsystem answers; the level holds one of it, at the origin, with its
    assets. It attaches itself to the counter-frame at BeginPlay, so where
    it is placed does not matter, and the origin says so.

    MPC_Sky is the glass veil's, which lands in slice 2. Until the collection
    exists the slot stays empty and the sky writes nothing to it.
    """
    sky = actor_sub.spawn_actor_from_class(
        unreal.ShipSky, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    sky.set_actor_label(TAG + "sky")
    sky.set_editor_property("body_mesh", sphere)
    sky.set_editor_property("body_material", sky_asset("M_SkyBody"))
    sky.set_editor_property("star_material", sky_asset("M_SkyStar"))
    sky.set_editor_property("point_star_material", sky_asset("M_SkyStarfield"))
    parameters = sky_asset("MPC_Sky", required=False)
    if parameters:
        sky.set_editor_property("sky_parameters", parameters)
    return sky


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
    sphere = unreal.EditorAssetLibrary.load_asset(SPHERE)
    place_counter_frame(actor_sub, sphere)
    place_sky(actor_sub, sphere)

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
               "shadowed), %d glass panes casting no shadow, one counter-frame, one sky."
               % (removed, len(ship.boxes), len(ship.lights),
                  sum(1 for light in ship.lights if light.shadows),
                  sum(1 for box in ship.boxes if box.role == "glass")))
    with open(os.path.join(unreal.Paths.project_saved_dir(), "hauler_build.txt"), "w") as f:
        f.write(summary + "\n")
    unreal.log(summary)


build()
