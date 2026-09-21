"""
Generates Content/Maps/L_Hauler from scratch.

The .umap is a binary asset: opaque to git and to code review. This script is
the readable source of truth for the layout, so the level becomes a derived
artifact. Change a number here and re-run rather than nudging actors by hand.

Run headlessly:

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
        "$PWD/DeepSpace.uproject" \
        -run=pythonscript -script="$PWD/Tools/build_hauler.py" \
        -unattended -nopause -nosplash -NoLiveCoding

or from the editor's Output Log (Cmd box set to Python):

    exec(open('/home/matt/Development/deepspace/Tools/build_hauler.py').read())

Geometry conventions: Unreal units are centimetres; X is forward, Y right,
Z up. SM_Cube is 100 units on a side with a centred pivot, so scale*100 is
size in cm and a slab's centre sits half its thickness off the surface.
"""

import unreal

MAP_PATH = "/Game/Maps/L_Hauler"
CUBE = "/Game/LevelPrototyping/Meshes/SM_Cube"
CONSOLE_BP = "/Game/Blueprints/BP_ShipConsole"
GAMEMODE_BP = "/Game/Blueprints/BP_DeepSpaceGameMode"

WALL = 0.1    # 10 cm thick
HEIGHT = 2.5  # 250 cm interior
MID = 125     # centre height of a full-height wall

# (label, location, scale)
BOXES = [
    # --- Corridor: 150 wide, 800 long, running +X from the aft bulkhead.
    ("corridor_floor",      (400, 0, -5),      (8, 1.5, WALL)),
    ("corridor_ceiling",    (400, 0, 255),     (8, 1.5, WALL)),
    ("corridor_aft_wall",   (-5, 0, MID),      (WALL, 1.5, HEIGHT)),
    # Side walls double as each room's near wall; the gaps are the doorways.
    ("corridor_wall_r_1",   (150, 80, MID),    (3, WALL, HEIGHT)),
    ("corridor_wall_r_2",   (610, 80, MID),    (3.8, WALL, HEIGHT)),
    ("corridor_wall_l_1",   (140, -80, MID),   (2.8, WALL, HEIGHT)),
    ("corridor_wall_l_2",   (600, -80, MID),   (4, WALL, HEIGHT)),

    # --- Engineering: 300 x 400, off the corridor to starboard.
    ("eng_floor",           (350, 275, -5),    (3, 4, WALL)),
    ("eng_ceiling",         (350, 275, 255),   (3, 4, WALL)),
    ("eng_wall_aft",        (195, 275, MID),   (WALL, 4, HEIGHT)),
    ("eng_wall_fore",       (505, 275, MID),   (WALL, 4, HEIGHT)),
    ("eng_wall_far",        (350, 480, MID),   (3, WALL, HEIGHT)),

    # --- Bunk: 250 x 300, to port.
    ("bunk_floor",          (325, -225, -5),   (2.5, 3, WALL)),
    ("bunk_ceiling",        (325, -225, 255),  (2.5, 3, WALL)),
    ("bunk_wall_aft",       (195, -225, MID),  (WALL, 3, HEIGHT)),
    ("bunk_wall_fore",      (455, -225, MID),  (WALL, 3, HEIGHT)),
    ("bunk_wall_far",       (325, -380, MID),  (2.5, WALL, HEIGHT)),
    ("bunk_bed",            (250, -250, 25),   (0.9, 2, 0.5)),

    # --- Cockpit: 300 x 300 at the fore end.
    ("cockpit_floor",       (950, 0, -5),      (3, 3, WALL)),
    ("cockpit_ceiling",     (950, 0, 255),     (3, 3, WALL)),
    ("cockpit_wall_port",   (950, -155, MID),  (3, WALL, HEIGHT)),
    ("cockpit_wall_stbd",   (950, 155, MID),   (3, WALL, HEIGHT)),
    ("cockpit_wall_aft_p",  (795, -112, MID),  (WALL, 0.75, HEIGHT)),
    ("cockpit_wall_aft_s",  (795, 112, MID),   (WALL, 0.75, HEIGHT)),
    # Window is the Z=100..180 gap between these two.
    ("cockpit_wall_lower",  (1105, 0, 50),     (WALL, 3, 1.0)),
    ("cockpit_wall_upper",  (1105, 0, 215),    (WALL, 3, 0.7)),
]

CONSOLE_LOC = (350, 470, 120)
# The panel's thin axis is X and its face points -X; yaw 90 turns it to -Y,
# so it faces into engineering from the far wall.
CONSOLE_ROT = (0, 0, 90)
PLAYER_START_LOC = (100, 0, 90)


# Actors the script owns. Anything with this prefix is destroyed and rebuilt on
# each run, so the script stays idempotent and the numbers above stay the truth.
TAG = "hauler_"

# Template leftovers that have no place in a ship interior. Removed once; they
# are not recreated.
TEMPLATE_CRUFT = ("Floor", "SM_SkySphere")

# Space has no atmosphere, clouds or haze. Removing these leaves black, which
# is what the window should look out on.
SPACE_STRIPS = (
    unreal.SkyAtmosphere,
    unreal.VolumetricCloud,
    unreal.ExponentialHeightFog,
)

STAR_MATERIAL = "/Game/Materials/M_Star"
STAR_COUNT = 160
STAR_RADIUS = 12000.0


def ensure_star_material():
    """An unlit, brighter-than-white material. Unlit so stars are not dimmed
    by the absence of any light source out there."""
    if unreal.EditorAssetLibrary.does_asset_exist(STAR_MATERIAL):
        return unreal.EditorAssetLibrary.load_asset(STAR_MATERIAL)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset("M_Star", "/Game/Materials", unreal.Material,
                             unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    colour = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
    colour.set_editor_property("constant", unreal.LinearColor(4.0, 4.0, 4.2, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


def clear_previous(actor_sub):
    removed = 0
    for actor in actor_sub.get_all_level_actors():
        label = actor.get_actor_label()
        if label.startswith(TAG) or label in TEMPLATE_CRUFT:
            actor_sub.destroy_actor(actor)
            removed += 1
        elif isinstance(actor, SPACE_STRIPS):
            actor_sub.destroy_actor(actor)
            removed += 1
        elif isinstance(actor, unreal.PlayerStart):
            actor_sub.destroy_actor(actor)
            removed += 1
    return removed


def spawn_mesh(actor_sub, mesh, label, loc, scale, rot=(0, 0, 0)):
    actor = actor_sub.spawn_actor_from_class(
        unreal.StaticMeshActor, unreal.Vector(*loc), unreal.Rotator(*rot))
    actor.set_actor_label(label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    actor.static_mesh_component.set_static_mesh(mesh)
    # Blockout geometry never moves; Static lets it take baked lighting.
    actor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
    return actor


def scatter_stars(actor_sub, sphere, material):
    """Points on a sphere via the golden-angle spiral, which spreads them
    evenly without the clumping that uniform random gives."""
    import math
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(STAR_COUNT):
        y = 1.0 - (i / float(STAR_COUNT - 1)) * 2.0
        r = math.sqrt(max(0.0, 1.0 - y * y))
        theta = golden * i
        loc = (math.cos(theta) * r * STAR_RADIUS,
               y * STAR_RADIUS,
               math.sin(theta) * r * STAR_RADIUS + 500.0)
        star = spawn_mesh(actor_sub, sphere, TAG + "star_%03d" % i, loc,
                          (0.3, 0.3, 0.3))
        star.static_mesh_component.set_material(0, material)


def build():
    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        level_sub.load_level(MAP_PATH)
    else:
        level_sub.new_level(MAP_PATH)

    removed = clear_previous(actor_sub)

    cube = unreal.EditorAssetLibrary.load_asset(CUBE)
    sphere = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    if cube is None or sphere is None:
        raise RuntimeError("Could not load the blockout meshes")

    for label, loc, scale in BOXES:
        spawn_mesh(actor_sub, cube, TAG + label, loc, scale)

    scatter_stars(actor_sub, sphere, ensure_star_material())

    console_cls = unreal.EditorAssetLibrary.load_blueprint_class(CONSOLE_BP)
    console = actor_sub.spawn_actor_from_class(
        console_cls, unreal.Vector(*CONSOLE_LOC), unreal.Rotator(*CONSOLE_ROT))
    console.set_actor_label(TAG + "console")

    start = actor_sub.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(*PLAYER_START_LOC), unreal.Rotator(0, 0, 0))
    start.set_actor_label(TAG + "player_start")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property(
        "default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(GAMEMODE_BP))

    level_sub.save_current_level()
    unreal.log("L_Hauler: removed %d, placed %d boxes + %d stars."
               % (removed, len(BOXES), STAR_COUNT))


build()
