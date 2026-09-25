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
LIGHTS_TAG = "Power.Lights"


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
                        ("pilot_seat", ship.pilot_seat_location),
                        ("laptop", ship.laptop_location)):
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

    # The tag is the contract C++ addresses generated actors by (never the
    # name, never the index), so an untagged light is a light the ship cannot
    # dim. Mobility too: a Static light is baked and cannot change at all.
    for label in built_lights:
        actor = actors[label]
        if LIGHTS_TAG not in [str(t) for t in actor.get_editor_property("tags")]:
            failures.append("%s is not tagged %s" % (label, LIGHTS_TAG))
        component = actor.get_component_by_class(unreal.PointLightComponent)
        if component.get_editor_property("mobility") == unreal.ComponentMobility.STATIC:
            failures.append("%s is a Static light and can never dim" % label)

    # The starfield used to be 160 actors in the map. It is now generated in
    # C++ on the counter-frame at BeginPlay, so what the level must contain is
    # one counter-frame, at the origin and unrotated -- the rotation is applied
    # at runtime and a built-in one would be silently composed with it.
    stars = [k for k in actors if k.startswith(TAG + "star_")]
    if stars:
        failures.append("%d star actors remain; the starfield is generated in C++ now" % len(stars))

    frame = actors.get(TAG + "counterframe")
    if frame is None:
        failures.append("MISSING counterframe")
    else:
        p = frame.get_actor_location()
        if max(abs(p.x), abs(p.y), abs(p.z)) > TOLERANCE:
            failures.append("counterframe is at (%.1f, %.1f, %.1f), not the origin" % (p.x, p.y, p.z))
        r = frame.get_actor_rotation()
        if max(abs(r.pitch), abs(r.yaw), abs(r.roll)) > TOLERANCE:
            failures.append("counterframe is rotated (%.1f, %.1f, %.1f), not at identity"
                            % (r.pitch, r.yaw, r.roll))

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
