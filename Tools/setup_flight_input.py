"""Create the flight input actions and bind them in IMC_Default.

Keyboard flies and the mouse keeps looking: the pilot's head turns
independently of the ship, which is what makes a turn read as the ship
turning rather than the camera swinging.

    W / S   pitch (nose down / nose up)
    A / D   yaw
    Q / Z   roll
    Shift / Ctrl   throttle, which is a lever and stays where it is left
    F       the in-system drive, a second lever: pressed, it flips, and it
            stays where it is left like the throttle (sky decision 8)

F is checked against every other mapping in IMC_Default before it is bound,
and the script fails rather than bind a key something else already uses.

Idempotent: re-running rebuilds the mappings rather than appending to them.
The character Blueprint is compiled and saved once its actions are set, so
its class default object carries them (CLAUDE.md: a Blueprint saved against
an older C++ class keeps a stale template until it is recompiled).
Run with the editor closed:

    UnrealEditor-Cmd DeepSpace.uproject -run=pythonscript \
        -script=".../Tools/setup_flight_input.py" -unattended -nopause -nosplash
"""

import traceback

import unreal

ACTIONS_DIR = "/Game/Input/Actions"
IMC_PATH = "/Game/Input/IMC_Default"
CHARACTER_BP = "/Game/Blueprints/BP_DeepSpaceCharacter"

log = []


def note(line):
    log.append(line)
    unreal.log(line)


# There is no InputActionFactory exposed to Python, so a new action is a
# duplicate of an existing one with its value type changed. IA_Look is the
# template: an axis action with no modifiers of its own to inherit.
TEMPLATE_ACTION = f"{ACTIONS_DIR}/IA_Look"


def ensure_action(name, value_type):
    """Fetch the action, or make one. Never recreated: a rebuild would break
    every asset already pointing at it."""
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


# X pitch, Y yaw, Z roll -- the body axes FShipFlightCommand::AttitudeRate uses.
# A key drives X, so anything else needs a swizzle, exactly as IA_Move's
# forward keys do.
SWIZZLE = {
    "X": None,
    "Y": unreal.InputAxisSwizzle.YXZ,
    "Z": unreal.InputAxisSwizzle.ZYX,
}


def make_key(name):
    """FKey's Python constructor takes no arguments; its name goes in after."""
    key = unreal.Key()
    key.set_editor_property("key_name", name)
    return key


def make_mapping(imc, action, key_name, axis="X", negate=False):
    """Modifiers are UObjects and must be outered to the context, or they are
    transient and do not survive the save."""
    modifiers = []
    if negate:
        modifiers.append(unreal.new_object(unreal.InputModifierNegate, outer=imc))
    if SWIZZLE[axis] is not None:
        swizzle = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=imc)
        swizzle.set_editor_property("order", SWIZZLE[axis])
        modifiers.append(swizzle)

    mapping = unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property("action", action)
    mapping.set_editor_property("key", make_key(key_name))
    mapping.set_editor_property("modifiers", modifiers)
    note(f"  {key_name} -> {axis}{' (negated)' if negate else ''}")
    return mapping


DRIVE_KEY = "F"


def key_of(mapping):
    return str(mapping.get_editor_property("key").get_editor_property("key_name"))


def action_name(mapping):
    action = mapping.get_editor_property("action")
    return action.get_name() if action else "(no action)"


def main():
    attitude = ensure_action("IA_Attitude", unreal.InputActionValueType.AXIS3D)
    throttle = ensure_action("IA_Throttle", unreal.InputActionValueType.AXIS1D)
    drive = ensure_action("IA_Drive", unreal.InputActionValueType.BOOLEAN)

    imc = unreal.load_asset(IMC_PATH)
    if not imc:
        raise RuntimeError(f"no mapping context at {IMC_PATH}")

    # UE 5.8 keeps the real list under default_key_mappings; the context's own
    # `mappings` is the older, now-empty one, and map_key writes to that.
    data = imc.get_editor_property("default_key_mappings")
    ours = {attitude.get_path_name(), throttle.get_path_name(), drive.get_path_name()}

    # Drop our own mappings first so a re-run replaces rather than stacks.
    # Everything else in the context is left untouched.
    existing = list(data.get_editor_property("mappings"))
    kept = [m for m in existing
            if not (m.get_editor_property("action")
                    and m.get_editor_property("action").get_path_name() in ours)]
    note(f"cleared {len(existing) - len(kept)} existing flight mapping(s), kept {len(kept)}")

    # The flight keys share W/A/S/D with IA_Move on purpose -- seated, the
    # keys fly; standing, they walk -- but the drive's key must mean nothing
    # else, or pressing it at the helm would also do whatever else it does.
    clashes = [action_name(m) for m in kept if key_of(m) == DRIVE_KEY]
    if clashes:
        raise RuntimeError(f"{DRIVE_KEY} is already bound in IMC_Default to {', '.join(clashes)}; "
                           "choose another key for IA_Drive")
    note(f"{DRIVE_KEY} is free in IMC_Default")

    note("IA_Attitude:")
    kept.append(make_mapping(imc, attitude, "W", "X", negate=True))   # nose down
    kept.append(make_mapping(imc, attitude, "S", "X"))                # nose up
    kept.append(make_mapping(imc, attitude, "D", "Y"))                # yaw right
    kept.append(make_mapping(imc, attitude, "A", "Y", negate=True))
    kept.append(make_mapping(imc, attitude, "Z", "Z"))                # roll right
    kept.append(make_mapping(imc, attitude, "Q", "Z", negate=True))

    note("IA_Throttle:")
    kept.append(make_mapping(imc, throttle, "LeftShift", "X"))
    kept.append(make_mapping(imc, throttle, "LeftControl", "X", negate=True))

    # A press, not a hold: the handler flips the lever on Started.
    note("IA_Drive:")
    kept.append(make_mapping(imc, drive, DRIVE_KEY, "X"))

    data.set_editor_property("mappings", kept)
    imc.set_editor_property("default_key_mappings", data)

    unreal.EditorAssetLibrary.save_loaded_asset(imc, False)
    unreal.EditorAssetLibrary.save_loaded_asset(attitude, False)
    unreal.EditorAssetLibrary.save_loaded_asset(throttle, False)
    unreal.EditorAssetLibrary.save_loaded_asset(drive, False)

    # The character needs to be told which actions these are, and then
    # compiled, so the saved class default object is built against the C++
    # that declares drive_action rather than the one before it.
    bp = unreal.load_asset(CHARACTER_BP)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("attitude_action", attitude)
    cdo.set_editor_property("throttle_action", throttle)
    cdo.set_editor_property("drive_action", drive)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

    cdo = unreal.get_default_object(bp.generated_class())
    for prop, want in (("attitude_action", attitude), ("throttle_action", throttle), ("drive_action", drive)):
        got = cdo.get_editor_property(prop)
        if not got or got.get_path_name() != want.get_path_name():
            raise RuntimeError(f"BP_DeepSpaceCharacter.{prop} did not survive the compile: {got}")
    unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    note("BP_DeepSpaceCharacter: attitude_action, throttle_action, drive_action assigned, compiled, saved")
    note("DONE")


try:
    main()
except Exception:
    note("FAILED\n" + traceback.format_exc())
    raise
finally:
    with open(unreal.Paths.project_saved_dir() + "setup_flight_input.txt", "w") as f:
        f.write("\n".join(log) + "\n")
