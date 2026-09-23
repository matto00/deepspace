"""
Imports the Mixamo clips in SourceArt/Mixamo/, retargets them onto the
mannequin, and builds the body's blend spaces.

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/import_animations.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/import_animations.txt

Mixamo clips are on Mixamo's own skeleton. Retargeting maps them onto Unreal's
mannequin through two IK rigs and a retargeter, all generated here: the IK Rig
plugin recognises both skeletons, so their retarget chains and the pose
alignment (Mixamo is T-pose, the mannequin A-pose) are automatic.

Idempotent, and deliberately *updates in place*: every asset keeps its path
across runs, because ABP_DeepSpaceBody is wired by hand and references the
blend spaces. Deleting and recreating them would silently break it.

Every .fbx in SourceArt/Mixamo/ is imported, so a new download is picked up by
re-running; wiring it into a blend space means editing BLEND_SPACES below.
"""

import glob
import os
import traceback

import unreal

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR = os.path.join(PROJECT, "SourceArt", "Mixamo")
SOURCE_SKELETON_FBX = "y_bot.fbx"

ROOT = "/Game/Characters/DeepSpace"
IMPORT_DIR = ROOT + "/Mixamo"          # raw imports, on Mixamo's skeleton
RIG_DIR = ROOT + "/Rigs"
ANIM_DIR = ROOT + "/Anims"             # retargeted, on the mannequin
PREFIX = "RTG_"

MANNY = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
UNARMED = "/Game/Characters/Mannequins/Anims/Unarmed"

# name -> (axis label, axis max, [(animation path, axis value)])
# Axis values are ground speeds in cm/s, matching FMovementRules.
BLEND_SPACES = {
    "BS_DS_Locomotion": ("Speed", 600.0, [
        (UNARMED + "/MM_Idle", 0.0),
        (UNARMED + "/Walk/MF_Unarmed_Walk_Fwd", 300.0),
        (ANIM_DIR + "/" + PREFIX + "running", 600.0),
    ]),
    "BS_DS_Crouch": ("Speed", 150.0, [
        (ANIM_DIR + "/" + PREFIX + "crouching_idle", 0.0),
        (ANIM_DIR + "/" + PREFIX + "crouch_walk", 150.0),
    ]),
}

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
LOG = []


def log(*parts):
    LOG.append(" ".join(str(p) for p in parts))


def load_or_create(name, folder, cls, factory):
    path = "%s/%s" % (folder, name)
    if lib.does_asset_exist(path):
        return unreal.load_asset(path)
    return tools.create_asset(name, folder, cls, factory)


def import_fbx(filename, destination, skeleton=None):
    """Import one FBX, replacing any previous import at the same path."""
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = destination
    task.automated = True
    task.save = True
    task.replace_existing = True
    options = unreal.FbxImportUI()
    options.import_materials = False
    options.import_textures = False
    options.import_as_skeletal = True
    if skeleton is None:
        options.import_mesh = True
        options.import_animations = False
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    else:
        options.import_mesh = False
        options.import_animations = True
        options.skeleton = skeleton
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    task.options = options
    tools.import_asset_tasks([task])
    return [unreal.load_asset(p) for p in task.imported_object_paths]


def import_sources():
    """Y Bot for its skeleton, then every clip onto that skeleton."""
    imported = import_fbx(os.path.join(SOURCE_DIR, SOURCE_SKELETON_FBX), IMPORT_DIR)
    y_bot = next((a for a in imported if isinstance(a, unreal.SkeletalMesh)), None)
    if y_bot is None:
        raise RuntimeError("%s produced no skeletal mesh" % SOURCE_SKELETON_FBX)

    clips = []
    for path in sorted(glob.glob(os.path.join(SOURCE_DIR, "*.fbx"))):
        if os.path.basename(path) == SOURCE_SKELETON_FBX:
            continue
        name = os.path.splitext(os.path.basename(path))[0]
        seqs = [a for a in import_fbx(path, IMPORT_DIR + "/" + name, y_bot.skeleton)
                if isinstance(a, unreal.AnimSequence)]
        if not seqs:
            raise RuntimeError("%s produced no animation" % os.path.basename(path))
        clips += seqs
        log("imported", name, "%.2fs" % seqs[0].get_play_length())
    return y_bot, clips


def ik_rig(name, mesh):
    """An IK rig whose retarget chains are generated from the skeleton."""
    rig = load_or_create(name, RIG_DIR, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    controller = unreal.IKRigController.get_controller(rig)
    controller.set_skeletal_mesh(mesh)
    if not controller.apply_auto_generated_retarget_definition():
        raise RuntimeError("could not auto-generate retarget chains for " + name)
    controller.apply_auto_fbik()
    lib.save_loaded_asset(rig, only_if_is_dirty=False)
    log("rig", name, "root", controller.get_retarget_root(),
        "chains", len(controller.get_retarget_chains()))
    return rig


def retargeter(source_rig, target_rig, source_mesh, target_mesh):
    rtg = load_or_create("RTG_Mixamo_To_Manny", RIG_DIR, unreal.IKRetargeter,
                         unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    source, target = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
    c.set_ik_rig(source, source_rig)
    c.set_ik_rig(target, target_rig)
    c.set_preview_mesh(source, source_mesh)
    c.set_preview_mesh(target, target_mesh)
    if c.get_num_retarget_ops() == 0:
        c.add_default_ops()
    c.assign_ik_rig_to_all_ops(source, source_rig)
    c.assign_ik_rig_to_all_ops(target, target_rig)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Mixamo's rest pose is a T-pose, the mannequin's an A-pose.
    c.auto_align_all_bones(target, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    lib.save_loaded_asset(rtg, only_if_is_dirty=False)
    return rtg


def retarget(clips, rtg, source_mesh, target_mesh):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.set_editor_property("assets_to_retarget",
                               [registry.get_asset_by_object_path(c.get_path_name()) for c in clips])
    inputs.set_editor_property("source_mesh", source_mesh)
    inputs.set_editor_property("target_mesh", target_mesh)
    inputs.set_editor_property("ik_retarget_asset", rtg)
    inputs.set_editor_property("target_path", ANIM_DIR)
    inputs.set_editor_property("prefix", PREFIX)
    inputs.set_editor_property("include_referenced_assets", False)
    inputs.set_editor_property("overwrite_existing_files", True)
    for data in unreal.IKRetargetBatchOperation.run_batch_retarget(inputs):
        asset = unreal.load_asset("%s.%s" % (data.package_name, data.asset_name))
        if isinstance(asset, unreal.AnimSequence):
            lib.save_loaded_asset(asset, only_if_is_dirty=False)
            log("retargeted", asset.get_name())


def blend_space(name, label, maximum, samples, skeleton):
    factory = unreal.BlendSpaceFactory1D()
    factory.set_editor_property("target_skeleton", skeleton)
    bs = load_or_create(name, ANIM_DIR, unreal.BlendSpace1D, factory)

    params = list(bs.get_editor_property("blend_parameters"))
    axis = params[0]
    axis.set_editor_property("display_name", label)
    axis.set_editor_property("min", 0.0)
    axis.set_editor_property("max", maximum)
    axis.set_editor_property("grid_num", 4)
    params[0] = axis
    bs.set_editor_property("blend_parameters", params)

    entries = []
    for path, value in samples:
        anim = unreal.load_asset(path)
        if anim is None:
            raise RuntimeError("%s: missing sample %s" % (name, path))
        sample = unreal.BlendSample()
        sample.set_editor_property("animation", anim)
        sample.set_editor_property("sample_value", unreal.Vector(value, 0.0, 0.0))
        entries.append(sample)
    bs.set_editor_property("sample_data", entries)

    # Without this the samples are stored but the interpolation table the
    # engine blends from is never built: see UDeepSpaceEditorScripting.
    if not unreal.DeepSpaceEditorScripting.rebuild_blend_space(bs):
        raise RuntimeError("could not rebuild " + name)
    lib.save_loaded_asset(bs, only_if_is_dirty=False)
    log("blend space", name, ", ".join("%s@%g" % (p.rsplit("/", 1)[-1], v) for p, v in samples))


def main():
    try:
        manny = unreal.load_asset(MANNY)
        y_bot, clips = import_sources()
        source_rig = ik_rig("IK_Mixamo", y_bot)
        target_rig = ik_rig("IK_Manny", manny)
        rtg = retargeter(source_rig, target_rig, y_bot, manny)
        retarget(clips, rtg, y_bot, manny)
        for name, (label, maximum, samples) in BLEND_SPACES.items():
            blend_space(name, label, maximum, samples, manny.skeleton)
        log("DONE")
    except Exception:
        log("FAILED\n" + traceback.format_exc())
    with open(os.path.join(unreal.Paths.project_saved_dir(), "import_animations.txt"), "w") as f:
        f.write("\n".join(LOG) + "\n")


main()
