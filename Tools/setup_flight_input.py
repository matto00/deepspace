"""Create the flight input actions and bind them in IMC_Default.

Keyboard flies and the mouse keeps looking: the pilot's head turns
independently of the ship, which is what makes a turn read as the ship
turning rather than the camera swinging.

    W / S   pitch (nose down / nose up)
    A / D   yaw
    Q / Z   roll
    Shift / Ctrl   the live lever, up and down (IA_LeverUp, IA_LeverDown):
            under the drive a press is one notch and a hold repeats; in
            cruise a hold sweeps, and a fresh press leaves the detent at zero.
            The levers are the ship's and stay where they are left.
    F       which lever is live, the drive's or cruise's (IA_Drive); each
            keeps its own setting across it
    X       all stop: both levers to STOP (IA_Stop)
    Tab     the next world as the target, on the zoomed system map
            (IA_CycleTarget; the system map spec's decision 13)

All four are Boolean presses: the character counts Started and reads held
from Triggered and Completed, so a tap released inside one frame is still a
press (flight-feel decision 3). IA_Throttle, the old axis lever the pawn
swept, is dropped from the context and deleted.

F, X and Tab are checked against every other mapping in IMC_Default before
they are bound, and Shift and Ctrl against everything but the walking
actions, which they share as the old throttle did: seated they are the
lever, standing Shift is sprint. The script fails rather than bind a key
something else already uses.

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


# The keys that must mean nothing else in the context: pressed at the helm,
# they must not also do whatever else they do. The flight keys share
# W/A/S/D with IA_Move on purpose -- seated, the keys fly; standing, they
# walk -- and so are not here.
PRESS_KEYS = {
    "IA_LeverUp": "LeftShift",
    "IA_LeverDown": "LeftControl",
    "IA_Drive": "F",
    "IA_Stop": "X",
    "IA_CycleTarget": "Tab",
}

# The Blueprint property each action is assigned to.
PROPERTIES = {
    "IA_Attitude": "attitude_action",
    "IA_LeverUp": "lever_up_action",
    "IA_LeverDown": "lever_down_action",
    "IA_Drive": "drive_action",
    "IA_Stop": "stop_action",
    "IA_CycleTarget": "cycle_target_action",
}

RETIRED = f"{ACTIONS_DIR}/IA_Throttle"

# What a helm key may share, and only on the lever keys: the walking actions,
# which do nothing while seated, as W/A/S/D share with IA_Move. Shift is
# sprint standing and the lever seated, as it was when it was the throttle.
SHARES_WITH_WALKING = {"LeftShift", "LeftControl"}
WALKING = {"IA_Move", "IA_Sprint", "IA_Crouch", "IA_Jump"}


def key_of(mapping):
    return str(mapping.get_editor_property("key").get_editor_property("key_name"))


def action_name(mapping):
    action = mapping.get_editor_property("action")
    return action.get_name() if action else "(no action)"


def main():
    actions = {"IA_Attitude": ensure_action("IA_Attitude", unreal.InputActionValueType.AXIS3D)}
    for name in PRESS_KEYS:
        actions[name] = ensure_action(name, unreal.InputActionValueType.BOOLEAN)
    retired = unreal.load_asset(RETIRED)

    imc = unreal.load_asset(IMC_PATH)
    if not imc:
        raise RuntimeError(f"no mapping context at {IMC_PATH}")

    # UE 5.8 keeps the real list under default_key_mappings; the context's own
    # `mappings` is the older, now-empty one, and map_key writes to that.
    data = imc.get_editor_property("default_key_mappings")
    ours = {action.get_path_name() for action in actions.values()}
    if retired:
        ours.add(retired.get_path_name())

    # Drop our own mappings first -- the retired throttle's with them -- so a
    # re-run replaces rather than stacks. Everything else is left untouched.
    existing = list(data.get_editor_property("mappings"))
    kept = [m for m in existing
            if not (m.get_editor_property("action")
                    and m.get_editor_property("action").get_path_name() in ours)]
    note(f"cleared {len(existing) - len(kept)} existing flight mapping(s), kept {len(kept)}")

    for name, key in PRESS_KEYS.items():
        clashes = [action_name(m) for m in kept if key_of(m) == key
                   and not (key in SHARES_WITH_WALKING and action_name(m) in WALKING)]
        if clashes:
            raise RuntimeError(f"{key} is already bound in IMC_Default to {', '.join(clashes)}; "
                               f"choose another key for {name}")
        note(f"{key} is free in IMC_Default" + (" but for walking" if key in SHARES_WITH_WALKING else ""))

    note("IA_Attitude:")
    attitude = actions["IA_Attitude"]
    kept.append(make_mapping(imc, attitude, "W", "X", negate=True))   # nose down
    kept.append(make_mapping(imc, attitude, "S", "X"))                # nose up
    kept.append(make_mapping(imc, attitude, "D", "Y"))                # yaw right
    kept.append(make_mapping(imc, attitude, "A", "Y", negate=True))
    kept.append(make_mapping(imc, attitude, "Z", "Z"))                # roll right
    kept.append(make_mapping(imc, attitude, "Q", "Z", negate=True))

    # Presses, not axes: the character counts Started and reads the hold from
    # Triggered and Completed. No modifiers -- a Boolean has nothing to negate.
    for name, key in PRESS_KEYS.items():
        note(f"{name}:")
        kept.append(make_mapping(imc, actions[name], key, "X"))

    data.set_editor_property("mappings", kept)
    imc.set_editor_property("default_key_mappings", data)

    unreal.EditorAssetLibrary.save_loaded_asset(imc, False)
    for action in actions.values():
        unreal.EditorAssetLibrary.save_loaded_asset(action, False)

    # The character needs to be told which actions these are, and then
    # compiled, so the saved class default object is built against the C++
    # that declares them -- and no longer carries the throttle's
    # ThrottleAction and ThrottleSweepRate, which that C++ removed (ADR 0002's
    # second amendment).
    bp = unreal.load_asset(CHARACTER_BP)
    cdo = unreal.get_default_object(bp.generated_class())
    for name, prop in PROPERTIES.items():
        cdo.set_editor_property(prop, actions[name])
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)

    cdo = unreal.get_default_object(bp.generated_class())
    for name, prop in PROPERTIES.items():
        got = cdo.get_editor_property(prop)
        want = actions[name]
        if not got or got.get_path_name() != want.get_path_name():
            raise RuntimeError(f"BP_DeepSpaceCharacter.{prop} did not survive the compile: {got}")
    unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    note("BP_DeepSpaceCharacter: " + ", ".join(PROPERTIES.values()) + " assigned, compiled, saved")

    # The old throttle action, once nothing points at it: the context has
    # dropped its mappings and the Blueprint has been saved without it.
    if retired:
        del retired
        if unreal.EditorAssetLibrary.delete_asset(RETIRED):
            note("deleted IA_Throttle")
        else:
            raise RuntimeError("could not delete IA_Throttle; something still references it")
    note("DONE")


try:
    main()
except Exception:
    note("FAILED\n" + traceback.format_exc())
    raise
finally:
    with open(unreal.Paths.project_saved_dir() + "setup_flight_input.txt", "w") as f:
        f.write("\n".join(log) + "\n")
