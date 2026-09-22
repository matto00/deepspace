"""
Sets up the player's input actions and Blueprint defaults for movement and
the body.

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/setup_character.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/setup_character.txt

Does everything around the character that is asset *assignment* -- the kind
of Blueprint work the project's rules allow -- so it is reviewable here rather
than clicked in the editor:

  * IA_Sprint and IA_Crouch, bound to Left Shift and C in IMC_Default;
  * the ABP_DeepSpaceBody shell, parented to UDeepSpaceAnimInstance on the
    mannequin skeleton (its six-node graph is wired by hand: Python cannot
    reasonably build anim graphs);
  * BP_DeepSpaceCharacter's defaults: those two actions, the mannequin as its
    body, and the Animation Blueprint as its anim class.

Idempotent. Run import_animations.py first: the mesh and skeleton come from
the template, but the Animation Blueprint is only useful once the clips exist.
"""

import os
import traceback

import unreal

ACTIONS = "/Game/Input/Actions"
MAPPING_CONTEXT = "/Game/Input/IMC_Default"
CHARACTER_BP = "/Game/Blueprints/BP_DeepSpaceCharacter"
BODY_ABP_DIR = "/Game/Characters/DeepSpace"
BODY_ABP = "ABP_DeepSpaceBody"
BODY_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"

# action name -> key, both digital
BINDINGS = {"IA_Sprint": "LeftShift", "IA_Crouch": "C"}

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
LOG = []


def log(*parts):
    LOG.append(" ".join(str(p) for p in parts))


def input_action(name):
    path = "%s/%s" % (ACTIONS, name)
    action = unreal.load_asset(path) if lib.does_asset_exist(path) else \
        tools.create_asset(name, ACTIONS, unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    lib.save_loaded_asset(action, only_if_is_dirty=False)
    return action


def bind(context, action, key_name):
    # Unmap first, so re-running never stacks duplicate bindings.
    context.unmap_all_keys_from_action(action)
    key = unreal.Key()
    key.set_editor_property("key_name", key_name)
    context.map_key(action, key)
    log("bound", action.get_name(), "->", key_name)


def body_anim_blueprint(skeleton):
    path = "%s/%s" % (BODY_ABP_DIR, BODY_ABP)
    if lib.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("parent_class", unreal.DeepSpaceAnimInstance)
    factory.set_editor_property("target_skeleton", skeleton)
    abp = tools.create_asset(BODY_ABP, BODY_ABP_DIR, unreal.AnimBlueprint, factory)
    lib.save_loaded_asset(abp, only_if_is_dirty=False)
    log("created", path, "(graph to be wired by hand)")
    return abp


def main():
    try:
        context = unreal.load_asset(MAPPING_CONTEXT)
        actions = {}
        for name, key in BINDINGS.items():
            actions[name] = input_action(name)
            bind(context, actions[name], key)
        lib.save_loaded_asset(context, only_if_is_dirty=False)

        mesh = unreal.load_asset(BODY_MESH)
        abp = body_anim_blueprint(mesh.skeleton)

        bp = unreal.load_asset(CHARACTER_BP)
        defaults = unreal.get_default_object(bp.generated_class())
        defaults.set_editor_property("sprint_action", actions["IA_Sprint"])
        defaults.set_editor_property("crouch_action", actions["IA_Crouch"])
        body = defaults.get_editor_property("mesh")
        body.set_skeletal_mesh_asset(mesh)
        body.set_editor_property("anim_class", abp.generated_class())
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        lib.save_loaded_asset(bp, only_if_is_dirty=False)
        log("BP_DeepSpaceCharacter: sprint, crouch, body mesh and anim class assigned")
        log("DONE")
    except Exception:
        log("FAILED\n" + traceback.format_exc())
    with open(os.path.join(unreal.Paths.project_saved_dir(), "setup_character.txt"), "w") as f:
        f.write("\n".join(LOG) + "\n")


main()
