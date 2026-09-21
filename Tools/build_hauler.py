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

MAP_PATH = "/Game/Maps/L_Hauler"
MATERIAL_DIR = "/Game/Materials"
CONSOLE_BP = "/Game/Blueprints/BP_ShipConsole"
GAMEMODE_BP = "/Game/Blueprints/BP_DeepSpaceGameMode"
GRID_MATERIAL = "/Game/LevelPrototyping/Materials/M_PrototypeGrid"
GLASS_MATERIAL = "/Game/Materials/M_Glass"
STAR_MATERIAL = "/Game/Materials/M_Star"

MESHES = {
    "cube": "/Game/LevelPrototyping/Meshes/SM_Cube",
    "chamfer": "/Game/LevelPrototyping/Meshes/SM_ChamferCube",
    "cylinder": "/Game/LevelPrototyping/Meshes/SM_Cylinder",
}
SPHERE = "/Engine/BasicShapes/Sphere"

# Every actor the script owns carries this prefix and is rebuilt each run.
TAG = "hauler_"
TEMPLATE_CRUFT = ("Floor", "SM_SkySphere")
SPACE_STRIPS = (unreal.SkyAtmosphere, unreal.VolumetricCloud, unreal.ExponentialHeightFog)

STAR_COUNT = 160
STAR_RADIUS = 12000.0

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
EMISSIVE = {
    "accent": (TEAL[0] * 4, TEAL[1] * 4, TEAL[2] * 4),
    "screen": (0.02, 0.10, 0.12),
    "lamp":   (6.0, 6.2, 6.5),
}
LIGHT_COLOUR = unreal.Color(r=255, g=247, b=235, a=255)   # neutral-cool white


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


def star_material():
    existing = _asset(STAR_MATERIAL)
    if existing:
        return existing
    raise RuntimeError(STAR_MATERIAL + " is missing; it is created by the milestone 1 build")


def materials():
    """Role name -> material. Surfaces name a role, never a material, so
    swapping in real textures later is a change here alone."""
    out = {role: panelled_instance(role, *spec) for role, spec in PANELLED.items()}
    base = emissive_base()
    out.update({role: emissive_instance(role, c, base) for role, c in EMISSIVE.items()})
    out["glass"] = unreal.EditorAssetLibrary.load_asset(GLASS_MATERIAL)
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


def tame_sky_light(actor_sub):
    """No atmosphere to capture in space, so a real-time-capture SkyLight only
    warns. Capture once, and keep it faint."""
    for actor in actor_sub.get_all_level_actors():
        if isinstance(actor, unreal.SkyLight):
            c = actor.get_component_by_class(unreal.SkyLightComponent)
            c.set_editor_property("real_time_capture", False)
            c.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            c.set_editor_property("intensity", 0.05)


def place_lights(actor_sub, lights):
    for light in lights:
        actor = actor_sub.spawn_actor_from_class(
            unreal.PointLight, unreal.Vector(*light.location), unreal.Rotator(0, 0, 0))
        actor.set_actor_label(TAG + light.label)
        c = actor.get_component_by_class(unreal.PointLightComponent)
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        c.set_editor_property("intensity", float(light.intensity))
        c.set_editor_property("attenuation_radius", float(light.radius))
        c.set_editor_property("light_color", LIGHT_COLOUR)
        # Soft, even fill: many overlapping lights, none casting shadows. Also
        # what keeps thirty-odd lights cheap.
        c.set_editor_property("cast_shadows", False)


def scatter_stars(actor_sub, sphere, material):
    """Golden-angle spiral: even coverage where uniform random clumps."""
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(STAR_COUNT):
        y = 1.0 - (i / float(STAR_COUNT - 1)) * 2.0
        r = math.sqrt(max(0.0, 1.0 - y * y))
        t = golden * i
        loc = unreal.Vector(math.cos(t) * r * STAR_RADIUS, y * STAR_RADIUS,
                            math.sin(t) * r * STAR_RADIUS + 500.0)
        star = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, loc,
                                                unreal.Rotator(0, 0, 0))
        star.set_actor_label(TAG + "star_%03d" % i)
        star.set_actor_scale3d(unreal.Vector(0.3, 0.3, 0.3))
        star.static_mesh_component.set_static_mesh(sphere)
        star.static_mesh_component.set_material(0, material)


def build():
    ship = L.generate()                      # raises PlanError on a bad plan

    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        level_sub.load_level(MAP_PATH)
    else:
        level_sub.new_level(MAP_PATH)

    removed = clear_previous(actor_sub)
    tame_sky_light(actor_sub)

    mats = materials()
    meshes = {k: unreal.EditorAssetLibrary.load_asset(p) for k, p in MESHES.items()}
    for box in ship.boxes:
        spawn_box(actor_sub, box, meshes[box.mesh], mats[box.role])

    place_lights(actor_sub, ship.lights)
    scatter_stars(actor_sub, unreal.EditorAssetLibrary.load_asset(SPHERE), star_material())

    console = actor_sub.spawn_actor_from_class(
        unreal.EditorAssetLibrary.load_blueprint_class(CONSOLE_BP),
        unreal.Vector(*ship.console_location), unreal.Rotator(0, 0, ship.console_yaw))
    console.set_actor_label(TAG + "console")
    console_mesh = console.get_component_by_class(unreal.StaticMeshComponent)
    if console_mesh:
        console_mesh.set_material(0, mats["furniture"])

    start = actor_sub.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(*ship.player_start), unreal.Rotator(0, 0, 0))
    start.set_actor_label(TAG + "player_start")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property(
        "default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(GAMEMODE_BP))

    level_sub.save_current_level()

    summary = ("L_Hauler built: removed %d, placed %d boxes, %d lights, %d stars."
               % (removed, len(ship.boxes), len(ship.lights), STAR_COUNT))
    with open(os.path.join(unreal.Paths.project_saved_dir(), "hauler_build.txt"), "w") as f:
        f.write(summary + "\n")
    unreal.log(summary)


build()
