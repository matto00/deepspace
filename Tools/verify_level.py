"""
Compares the built level against Tools/hauler_layout.py.

`validate_hauler.py` checks the layout is *sound*; this checks the level
actually *matches* it. The distinction is not academic: SM_Cube's pivot sits
at its minimum corner rather than its centre, so an early build placed every
box half its own size away from where the layout said, while the layout itself
validated perfectly. Only measuring the built actors catches that class of bug.

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
        "$PWD/DeepSpace.uproject" \
        -run=pythonscript -script="$PWD/Tools/verify_level.py" \
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


def report(lines):
    path = os.path.join(unreal.Paths.project_saved_dir(), "verify_level.txt")
    with open(path, "w") as handle:
        handle.write("\n".join(lines) + "\n")


def main():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP_PATH)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    actors = {a.get_actor_label(): a for a in actor_sub.get_all_level_actors()}
    failures = []
    checked = 0

    for label, centre, scale in L.BOXES:
        actor = actors.get(TAG + label)
        if actor is None:
            failures.append("MISSING %s" % label)
            continue

        origin, extent = actor.get_actor_bounds(False)
        want_extent = [s * 100.0 / 2.0 for s in scale]
        got_centre = (origin.x, origin.y, origin.z)
        got_extent = (extent.x, extent.y, extent.z)

        for axis, name in enumerate("xyz"):
            if abs(got_centre[axis] - centre[axis]) > TOLERANCE:
                failures.append(
                    "%s centre.%s = %.1f, layout says %.1f (off by %.1f)"
                    % (label, name, got_centre[axis], centre[axis],
                       got_centre[axis] - centre[axis]))
            if abs(got_extent[axis] - want_extent[axis]) > TOLERANCE:
                failures.append(
                    "%s half-size.%s = %.1f, layout says %.1f"
                    % (label, name, got_extent[axis], want_extent[axis]))
        checked += 1

    for label, loc in (("console", L.CONSOLE_LOC), ("player_start", L.PLAYER_START_LOC)):
        actor = actors.get(TAG + label)
        if actor is None:
            failures.append("MISSING %s" % label)
            continue
        pos = actor.get_actor_location()
        for axis, name in enumerate("xyz"):
            if abs((pos.x, pos.y, pos.z)[axis] - loc[axis]) > TOLERANCE:
                failures.append("%s.%s = %.1f, layout says %.1f"
                                % (label, name, (pos.x, pos.y, pos.z)[axis], loc[axis]))

    lights = [k for k in actors if k.startswith(TAG + "light_")]
    if len(lights) != len(L.LIGHTS):
        failures.append("%d lights in level, layout says %d" % (len(lights), len(L.LIGHTS)))

    lines = ["Checked %d boxes, %d lights against the layout." % (checked, len(lights))]
    if failures:
        lines.append("")
        lines.append("FAIL (%d):" % len(failures))
        lines.extend("  - " + f for f in failures[:40])
        if len(failures) > 40:
            lines.append("  ... and %d more" % (len(failures) - 40))
    else:
        lines.append("")
        lines.append("PASS: built level matches the layout within %.1f cm." % TOLERANCE)
    report(lines)


main()
