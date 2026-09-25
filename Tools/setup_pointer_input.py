"""Create IA_Point and bind it to the left mouse button in IMC_Default.

The pointer that drives the ship's screens needs a click, and a click is the
one thing the existing input set has no action for: everything aboard is
driven from the keyboard, and the mouse only looks.

Left mouse button rather than E: E is "interact with the thing you are
standing at" and already means "stand up" while seated. A screen is pointed
at, not walked up to and operated, and conflating the two would make the
console's power switch and its buttons the same key.

Idempotent: re-running rebuilds the mapping rather than appending to it. Run
with the editor closed:

    UnrealEditor-Cmd DeepSpace.uproject -run=pythonscript \
        -script=".../Tools/setup_pointer_input.py" -unattended -nopause -nosplash
"""

import unreal

ACTIONS_DIR = "/Game/Input/Actions"
IMC_PATH = "/Game/Input/IMC_Default"
CHARACTER_BP = "/Game/Blueprints/BP_DeepSpaceCharacter"
TEMPLATE_ACTION = f"{ACTIONS_DIR}/IA_Look"

log = []


def note(line):
    log.append(line)
    unreal.log(line)


def ensure_action(name, value_type):
    """Never recreated: a rebuild would break every asset pointing at it."""
    path = f"{ACTIONS_DIR}/{name}"
    existing = unreal.load_asset(path)
    if existing:
        existing.set_editor_property("value_type", value_type)
        note(f"reused {name}")
        return existing
    if not unreal.EditorAssetLibrary.duplicate_asset(TEMPLATE_ACTION, path):
        raise RuntimeError(f"could not create {path}")
    action = unreal.load_asset(path)
    action.set_editor_property("value_type", value_type)
    note(f"created {name}")
    return action


def make_key(name):
    key = unreal.Key()
    key.set_editor_property("key_name", name)
    return key


def main():
    point = ensure_action("IA_Point", unreal.InputActionValueType.BOOLEAN)

    imc = unreal.load_asset(IMC_PATH)
    if not imc:
        raise RuntimeError(f"no mapping context at {IMC_PATH}")

    # UE 5.8 keeps the real list under default_key_mappings; the context's own
    # `mappings` is the older, now-empty one.
    data = imc.get_editor_property("default_key_mappings")
    existing = list(data.get_editor_property("mappings"))
    kept = [m for m in existing
            if not (m.get_editor_property("action")
                    and m.get_editor_property("action").get_path_name() == point.get_path_name())]
    note(f"cleared {len(existing) - len(kept)} existing pointer mapping(s), kept {len(kept)}")

    mapping = unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property("action", point)
    mapping.set_editor_property("key", make_key("LeftMouseButton"))
    kept.append(mapping)
    note("IA_Point: LeftMouseButton")

    data.set_editor_property("mappings", kept)
    imc.set_editor_property("default_key_mappings", data)
    unreal.EditorAssetLibrary.save_loaded_asset(imc, False)
    unreal.EditorAssetLibrary.save_loaded_asset(point, False)

    bp = unreal.load_asset(CHARACTER_BP)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("point_action", point)
    unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    note("BP_DeepSpaceCharacter: point_action assigned")

    with open(unreal.Paths.project_saved_dir() + "setup_pointer_input.txt", "w") as f:
        f.write("\n".join(log) + "\n")


main()
