"""
Generates Content/Maps/L_Hauler from Tools/hauler_layout.py.

The .umap is a binary asset: opaque to git and to code review. The layout
module is the readable source of truth, so the level is a derived artifact.
Change a number there and re-run rather than nudging actors by hand.

Validate the layout first — it takes about a second and needs no editor:

    python3 Tools/validate_hauler.py

Then build:

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
        "$PWD/DeepSpace.uproject" \
        -run=pythonscript -script="$PWD/Tools/build_hauler.py" \
        -unattended -nopause -nosplash -NoLiveCoding

Note that `unreal.log` output does not reach stdout under the commandlet, so
this writes its summary to Saved/hauler_build.txt instead.
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hauler_layout as L

MAP_PATH = "/Game/Maps/L_Hauler"
CUBE = "/Game/LevelPrototyping/Meshes/SM_Cube"
SPHERE = "/Engine/BasicShapes/Sphere"
CONSOLE_BP = "/Game/Blueprints/BP_ShipConsole"
GAMEMODE_BP = "/Game/Blueprints/BP_DeepSpaceGameMode"

STAR_MATERIAL = "/Game/Materials/M_Star"
GLASS_MATERIAL = "/Game/Materials/M_Glass"
STAR_COUNT = 160
STAR_RADIUS = 12000.0

# Actors the script owns. Anything with this prefix is destroyed and rebuilt on
# each run, so the script stays idempotent and the layout module stays the truth.
TAG = "hauler_"

# Template leftovers with no place in a ship interior.
TEMPLATE_CRUFT = ("Floor", "SM_SkySphere")

# Space has no atmosphere, clouds or haze. Removing these leaves black, which
# is what the window should look out on.
SPACE_STRIPS = (unreal.SkyAtmosphere, unreal.VolumetricCloud, unreal.ExponentialHeightFog)


def make_material(path, name, configure):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    folder, _ = path.rsplit("/", 1)
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.Material, unreal.MaterialFactoryNew())
    configure(mat)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


def configure_star(mat):
    """Unlit and brighter than white: nothing out there lights a star for us."""
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    colour = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    colour.set_editor_property("constant", unreal.LinearColor(4.0, 4.0, 4.2, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def configure_glass(mat):
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    tint = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    tint.set_editor_property("constant", unreal.LinearColor(0.02, 0.03, 0.05, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        tint, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionConstant, -400, 200)
    opacity.set_editor_property("r", 0.08)
    unreal.MaterialEditingLibrary.connect_material_property(
        opacity, "", unreal.MaterialProperty.MP_OPACITY)


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
    """With SkyAtmosphere gone, a real-time-capture SkyLight warns on every
    load because it has nothing to capture. In space there is no sky bounce
    to model anyway, so capture it once and leave it."""
    tamed = 0
    for actor in actor_sub.get_all_level_actors():
        if isinstance(actor, unreal.SkyLight):
            component = actor.get_component_by_class(unreal.SkyLightComponent)
            component.set_editor_property("real_time_capture", False)
            component.set_editor_property(
                "source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            component.set_editor_property("intensity", 0.05)
            tamed += 1
    return tamed


def pivot_offset(mesh):
    """Local-space centre of a mesh's bounds.

    Meshes do not agree on where their origin sits: SM_Cube's is at its
    minimum corner (0,0,0 to 100,100,100) while Engine Sphere's is centred
    (-50 to +50). The layout speaks in centres, so the builder measures each
    mesh and compensates rather than assuming either convention.
    """
    box = mesh.get_bounding_box()
    return ((box.min.x + box.max.x) / 2.0,
            (box.min.y + box.max.y) / 2.0,
            (box.min.z + box.max.z) / 2.0)


def spawn_mesh(actor_sub, mesh, label, loc, scale, material=None):
    off = pivot_offset(mesh)
    placed = unreal.Vector(loc[0] - off[0] * scale[0],
                           loc[1] - off[1] * scale[1],
                           loc[2] - off[2] * scale[2])
    actor = actor_sub.spawn_actor_from_class(
        unreal.StaticMeshActor, placed, unreal.Rotator(0, 0, 0))
    actor.set_actor_label(label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.static_mesh_component.set_static_mesh(mesh)
    # Blockout geometry never moves; Static lets it take baked lighting.
    actor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
    if material is not None:
        actor.static_mesh_component.set_material(0, material)
    return actor


def scatter_stars(actor_sub, sphere, material):
    """Golden-angle spiral: spreads points evenly where uniform random clumps."""
    import math
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(STAR_COUNT):
        y = 1.0 - (i / float(STAR_COUNT - 1)) * 2.0
        r = math.sqrt(max(0.0, 1.0 - y * y))
        theta = golden * i
        loc = (math.cos(theta) * r * STAR_RADIUS,
               y * STAR_RADIUS,
               math.sin(theta) * r * STAR_RADIUS + 500.0)
        spawn_mesh(actor_sub, sphere, TAG + "star_%03d" % i, loc,
                   (0.3, 0.3, 0.3), material)


def place_lights(actor_sub):
    for n, (x, y, z, intensity, radius) in enumerate(L.LIGHTS):
        light = actor_sub.spawn_actor_from_class(
            unreal.PointLight, unreal.Vector(x, y, z), unreal.Rotator(0, 0, 0))
        light.set_actor_label(TAG + "light_%d" % n)
        component = light.get_component_by_class(unreal.PointLightComponent)
        component.set_editor_property("intensity_units",
                                      unreal.LightUnits.CANDELAS)
        component.set_editor_property("intensity", intensity)
        component.set_editor_property("attenuation_radius", float(radius))
        component.set_editor_property("light_color", unreal.Color(255, 246, 230))


def build():
    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        level_sub.load_level(MAP_PATH)
    else:
        level_sub.new_level(MAP_PATH)

    removed = clear_previous(actor_sub)
    tamed = tame_sky_light(actor_sub)

    cube = unreal.EditorAssetLibrary.load_asset(CUBE)
    sphere = unreal.EditorAssetLibrary.load_asset(SPHERE)
    if cube is None or sphere is None:
        raise RuntimeError("Could not load the blockout meshes")

    glass = make_material(GLASS_MATERIAL, "M_Glass", configure_glass)

    for label, loc, scale in L.BOXES:
        material = glass if label == "cockpit_window" else None
        spawn_mesh(actor_sub, cube, TAG + label, loc, scale, material)

    scatter_stars(actor_sub, sphere, make_material(STAR_MATERIAL, "M_Star", configure_star))
    place_lights(actor_sub)

    console_cls = unreal.EditorAssetLibrary.load_blueprint_class(CONSOLE_BP)
    console = actor_sub.spawn_actor_from_class(
        console_cls, unreal.Vector(*L.CONSOLE_LOC), unreal.Rotator(*L.CONSOLE_ROT))
    console.set_actor_label(TAG + "console")

    start = actor_sub.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(*L.PLAYER_START_LOC), unreal.Rotator(0, 0, 0))
    start.set_actor_label(TAG + "player_start")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property(
        "default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(GAMEMODE_BP))

    level_sub.save_current_level()

    summary = ("L_Hauler built: removed %d, %d boxes, %d stars, %d lights, "
               "%d sky light(s) tamed." % (removed, len(L.BOXES), STAR_COUNT,
                                           len(L.LIGHTS), tamed))
    with open(os.path.join(unreal.Paths.project_saved_dir(), "hauler_build.txt"), "w") as f:
        f.write(summary + "\n")
    unreal.log(summary)


build()
