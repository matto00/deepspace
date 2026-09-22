"""
Checks the built level matches the layout.

`validate_hauler.py` asks whether the design is sound. This asks whether the
level *is* the design. Keep both: during milestone 1 every box was placed half
its own size off, because SM_Cube's pivot is at a corner, and the layout
validated perfectly throughout. Only measuring the built actors catches that.

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/verify_level.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/verify_level.txt
"""

import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hauler_layout as L

MAP_PATH = "/Game/Maps/L_Hauler"
TAG = "hauler_"
TOLERANCE = 1.0  # cm


def main():
    ship = L.generate()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP_PATH)
    actors = {a.get_actor_label(): a for a in
              unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
    failures = []

    for box in ship.boxes:
        actor = actors.get(TAG + box.label)
        if actor is None:
            failures.append("MISSING %s" % box.label)
            continue
        origin, extent = actor.get_actor_bounds(False)
        got_c = (origin.x, origin.y, origin.z)
        got_e = (extent.x, extent.y, extent.z)
        for a, axis in enumerate("xyz"):
            if abs(got_c[a] - box.centre[a]) > TOLERANCE:
                failures.append("%s centre.%s is %.1f, layout says %.1f"
                                % (box.label, axis, got_c[a], box.centre[a]))
            if abs(got_e[a] - box.size[a] / 2.0) > TOLERANCE:
                failures.append("%s half-size.%s is %.1f, layout says %.1f"
                                % (box.label, axis, got_e[a], box.size[a] / 2.0))

    for label, want in (("console", ship.console_location),
                        ("player_start", ship.player_start),
                        ("pilot_seat", ship.pilot_seat_location)):
        actor = actors.get(TAG + label)
        if actor is None:
            failures.append("MISSING " + label)
            continue
        p = actor.get_actor_location()
        for a, axis in enumerate("xyz"):
            if abs((p.x, p.y, p.z)[a] - want[a]) > TOLERANCE:
                failures.append("%s.%s is %.1f, layout says %.1f"
                                % (label, axis, (p.x, p.y, p.z)[a], want[a]))

    built_lights = [k for k in actors if k.startswith(TAG + "light_")]
    if len(built_lights) != len(ship.lights):
        failures.append("%d lights built, layout has %d" % (len(built_lights), len(ship.lights)))

    lines = ["Checked %d boxes and %d lights against the layout."
             % (len(ship.boxes), len(ship.lights)), ""]
    if failures:
        lines.append("FAIL (%d):" % len(failures))
        lines += ["  - " + f for f in failures[:40]]
        if len(failures) > 40:
            lines.append("  ... and %d more" % (len(failures) - 40))
    else:
        lines.append("PASS: the built level matches the layout within %.1f cm." % TOLERANCE)
    with open(os.path.join(unreal.Paths.project_saved_dir(), "verify_level.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


main()
