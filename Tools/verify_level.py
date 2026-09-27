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
import placement as PL

MAP_PATH = "/Game/Maps/L_Hauler"
TAG = "hauler_"
TOLERANCE = 1.0  # cm
MARKER_TOLERANCE = 0.5  # cm: the dressing reads its surfaces from these
COLOUR_TOLERANCE = 2  # of 255, per channel
LIGHTS_TAG = PL.LIGHTS_TAG


def main():
    ship = L.generate()
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP_PATH)
    every = list(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
    actors = {a.get_actor_label(): a for a in every}
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
                        ("laptop", ship.laptop_location),
                        ("nav_screen", ship.nav_screen_location),
                        ("map_screen", ship.map_screen_location)):
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

    # Each light is where the layout puts it, in its room's colour, and casts
    # shadows only if it is a practical. The colour is checked on the light,
    # not on a temperature: the lighting subsystem browns out from this one
    # number, and a temperature tint would hide on top of it.
    for light in ship.lights:
        actor = actors.get(TAG + light.label)
        if actor is None:
            failures.append("MISSING " + light.label)
            continue
        p = actor.get_actor_location()
        for a, axis in enumerate("xyz"):
            if abs((p.x, p.y, p.z)[a] - light.location[a]) > TOLERANCE:
                failures.append("%s.%s is %.1f, layout says %.1f"
                                % (light.label, axis, (p.x, p.y, p.z)[a], light.location[a]))
        c = actor.get_component_by_class(unreal.PointLightComponent)
        got = c.get_editor_property("light_color")
        if any(abs(g - w) > COLOUR_TOLERANCE for g, w in zip((got.r, got.g, got.b), light.colour)):
            failures.append("%s is colour (%d, %d, %d), layout says %s"
                            % (light.label, got.r, got.g, got.b, light.colour))
        if c.get_editor_property("use_temperature"):
            failures.append("%s uses a temperature, which tints its colour twice" % light.label)
        if bool(c.get_editor_property("cast_shadows")) != bool(light.shadows):
            failures.append("%s %s shadows; the layout says it %s"
                            % (light.label,
                               "casts" if c.get_editor_property("cast_shadows") else "does not cast",
                               "should" if light.shadows else "should not"))
        if abs(c.get_editor_property("intensity") - light.intensity) > 1e-3:
            failures.append("%s intensity is %.3f, layout says %.3f"
                            % (light.label, c.get_editor_property("intensity"), light.intensity))
        if abs(c.get_editor_property("attenuation_radius") - light.radius) > TOLERANCE:
            failures.append("%s radius is %.1f, layout says %.1f"
                            % (light.label, c.get_editor_property("attenuation_radius"),
                               light.radius))

    # A lamp box wears its own room's lamp material, or it glows another
    # room's colour over this room's light.
    for box in ship.boxes:
        if not box.role.startswith("lamp_"):
            continue
        actor = actors.get(TAG + box.label)
        if actor is None:
            continue                        # already reported above
        material = actor.static_mesh_component.get_material(0)
        want = "MI_Ship_" + box.role
        if material is None or material.get_name() != want:
            failures.append("%s wears %s, not %s"
                            % (box.label, material.get_name() if material else "nothing", want))
        # And carries the tag the lighting finds it by, or it glows at full
        # over a room that has browned out.
        if PL.LAMPS_TAG not in [str(t) for t in actor.get_editor_property("tags")]:
            failures.append("%s is not tagged %s" % (box.label, PL.LAMPS_TAG))

    # Only lamps carry it: anything else tagged would be dimmed as a lamp.
    lamp_labels = {TAG + b.label for b in ship.boxes if b.role.startswith("lamp_")}
    for actor in every:
        if (PL.LAMPS_TAG in [str(t) for t in actor.get_editor_property("tags")]
                and actor.get_actor_label() not in lamp_labels):
            failures.append("%s is tagged %s but is not a lamp"
                            % (actor.get_actor_label(), PL.LAMPS_TAG))

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

    failures += check_counter_frame(every)
    failures += check_sky(every)
    failures += check_glass(ship, every, actors)
    failures += check_nav_screen(ship, every, actors)
    failures += check_map_screen(ship, every, actors)
    failures += check_hum(ship, every, actors)
    failures += check_surfaces(ship, every, actors)
    failures += check_keep_outs(ship, every, actors)
    failures += check_wear_tags(ship, every, actors)

    lines = ["Checked %d boxes and %d lights (%d practical) against the layout, "
             "the counter-frame, the sky, the glass and its tag, the chart, the map, "
             "%d hum sources, "
             "%d dressing surfaces, %d keep-outs and the wear tags."
             % (len(ship.boxes), len(ship.lights),
                sum(1 for light in ship.lights if light.shadows),
                len(ship.hum_sources), len(ship.surfaces), len(ship.keep_outs)), ""]
    if failures:
        lines.append("FAIL (%d):" % len(failures))
        lines += ["  - " + f for f in failures[:40]]
        if len(failures) > 40:
            lines.append("  ... and %d more" % (len(failures) - 40))
    else:
        lines.append("PASS: the built level matches the layout within %.1f cm." % TOLERANCE)
    with open(os.path.join(unreal.Paths.project_saved_dir(), "verify_level.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    # The report is the detail; the exit code is what a script or a queue
    # can act on without reading it.
    if failures:
        sys.exit(1)


def path_of(asset):
    return asset.get_path_name() if asset else "nothing"


def of_class(every, cls):
    """Actors found by what they are, never by what they are called."""
    return [a for a in every if isinstance(a, cls)]


def check_counter_frame(every):
    """One counter-frame, at the origin and unrotated -- the rotation is
    applied at runtime and a built-in one would be silently composed with it
    -- with the dome on M_SkyStarfield and the motes on M_SkyStar. Any other
    dome material ignores each star's colour and brightness, so every star
    draws alike; any other mote material has no Brightness, so the fade pops
    and the course marker cannot be tinted. Neither shows under -nullrhi."""
    failures = []
    frames = of_class(every, unreal.ShipCounterFrame)
    if len(frames) != 1:
        return ["%d counter-frames, want exactly one" % len(frames)]
    frame = frames[0]
    p = frame.get_actor_location()
    if max(abs(p.x), abs(p.y), abs(p.z)) > TOLERANCE:
        failures.append("counterframe is at (%.1f, %.1f, %.1f), not the origin" % (p.x, p.y, p.z))
    r = frame.get_actor_rotation()
    if max(abs(r.pitch), abs(r.yaw), abs(r.roll)) > TOLERANCE:
        failures.append("counterframe is rotated (%.1f, %.1f, %.1f), not at identity"
                        % (r.pitch, r.yaw, r.roll))
    for layer, want in (("distant_stars", PL.sky_asset("M_SkyStarfield")),
                        ("near_stars", PL.sky_asset("M_SkyStar"))):
        component = frame.get_editor_property(layer)
        got = path_of(component.get_material(0))
        if got != want:
            failures.append("counterframe %s draws with %s, not %s" % (layer, got, want))
        mesh = path_of(component.get_editor_property("static_mesh"))
        if mesh != "%s.Sphere" % PL.SPHERE:
            failures.append("counterframe %s is %s, not the engine sphere" % (layer, mesh))
    return failures


def check_sky(every):
    """One AShipSky with its assets; no sun and no sky light but its own
    (sky decision 5). A second DirectionalLight would light the deck from a
    direction no star is in."""
    failures = []
    skies = of_class(every, unreal.ShipSky)
    if len(skies) != 1:
        failures.append("%d skies, want exactly one" % len(skies))
    for sky in skies[:1]:
        for slot, want in (("body_mesh", PL.sky_asset("SM_SkyBody")),
                           ("body_material", PL.sky_asset("M_SkyBody")),
                           ("star_material", PL.sky_asset("M_SkyStar")),
                           ("point_star_material", PL.sky_asset("M_SkyStarfield"))):
            got = path_of(sky.get_editor_property(slot))
            if got != want:
                failures.append("sky %s is %s, not %s" % (slot, got, want))
        # The veil's collection: without it the glass never answers the
        # lights, and nothing in play says so.
        got = path_of(sky.get_editor_property("sky_parameters"))
        if got != PL.sky_asset("MPC_Sky"):
            failures.append("sky sky_parameters is %s, not %s" % (got, PL.sky_asset("MPC_Sky")))
    for cls in (unreal.DirectionalLight, unreal.SkyLight, unreal.SkyAtmosphere,
                unreal.VolumetricCloud, unreal.ExponentialHeightFog):
        for actor in of_class(every, cls):
            failures.append("%s (%s) remains; the sky owns the only light from outside"
                            % (actor.get_actor_label(), cls.__name__))
    return failures


def check_glass(ship, every, actors):
    """Every pane on M_SkyGlass, casting no shadow: the sunlight on the deck
    is the shape of the windows only if the glass lets it through. And every
    pane tagged GLASS_TAG, and nothing else."""
    failures = []
    want = PL.sky_asset("M_SkyGlass")
    for box in ship.boxes:
        if box.role != "glass":
            continue
        actor = actors.get(TAG + box.label)
        if actor is None:
            continue                        # reported as MISSING already
        component = actor.static_mesh_component
        got = path_of(component.get_material(0))
        if got != want:
            failures.append("%s wears %s, not %s" % (box.label, got, want))
        if component.get_editor_property("cast_shadow"):
            failures.append("%s casts a shadow, so no sunlight comes through it" % box.label)
        # The target bracket's trace knows the glass by this tag alone: an
        # untagged pane is a wall to it, and hides the target behind glass.
        if PL.GLASS_TAG not in tags_of(actor):
            failures.append("%s is not tagged %s" % (box.label, PL.GLASS_TAG))
    # And only glass carries it, or the bracket is drawn through a wall.
    glass_labels = {TAG + b.label for b in ship.boxes if b.role == "glass"}
    for actor in every:
        if PL.GLASS_TAG in tags_of(actor) and actor.get_actor_label() not in glass_labels:
            failures.append("%s is tagged %s but is not glass" % (actor.get_actor_label(), PL.GLASS_TAG))
    return failures


def check_nav_screen(ship, every, actors):
    """One chart, facing aft, with its glass at least 1 cm proud of the
    built desk screen prop behind it. The chart's reach volume starts 0.5 cm
    behind the glass and the prop blocks Visibility: a mount that crept back
    past the prop's face would hand every bezel trace to the prop. Checked
    against the built prop's bounds, not the layout's, because the layout
    agreeing with itself is what hid milestone 1's pivot bug."""
    failures = []
    charts = of_class(every, unreal.ShipNavScreen)
    if len(charts) != 1:
        return ["%d charts, want exactly one" % len(charts)]
    chart = charts[0]
    if chart.get_actor_label() != TAG + "nav_screen":
        failures.append("the chart is labelled %s, not %snav_screen"
                        % (chart.get_actor_label(), TAG))
    r = chart.get_actor_rotation()
    if abs(((r.yaw - ship.nav_screen_yaw) + 180) % 360 - 180) > 1.0 or max(abs(r.pitch), abs(r.roll)) > 1.0:
        failures.append("the chart is rotated (%.1f, %.1f, %.1f); the layout says yaw %s"
                        % (r.pitch, r.yaw, r.roll, ship.nav_screen_yaw))
    failures += check_proud(ship, actors, chart, ship.nav_screen_location, "chart")
    for name, want in (("use_distance_cm", float(L.NAV_SCREEN_USE_DISTANCE)),
                       ("view_distance_cm", 60.0)):
        got = chart.get_editor_property(name)
        if abs(got - want) > 1e-3:
            failures.append("the chart's %s is %.1f, build_hauler sets %.1f" % (name, got, want))
    return failures


# How far a desk screen's glass stands proud of its prop, cm, at the least,
# and the float noise the check forgives on that boundary.
PROUD_CM = 1.0
PROUD_EPSILON_CM = 1e-3


def check_proud(ship, actors, screen, location, name):
    """A desk screen's glass stands at least 1 cm proud of the built desk
    screen prop behind it. Its reach and its pointer traces start at the
    glass and the prop blocks Visibility: a mount that crept back past the
    prop's face would hand every trace to the prop. Checked against the
    built prop's bounds, not the layout's, because the layout agreeing with
    itself is what hid milestone 1's pivot bug."""
    _, y, _ = location
    behind = [b for b in ship.boxes if b.role == "screen" and abs(b.centre[1] - y) <= b.size[1] / 2.0]
    if len(behind) != 1:
        return ["%d desk screens behind the %s, want one" % (len(behind), name)]
    prop = actors.get(TAG + behind[0].label)
    if prop is None:                    # a missing one is reported already
        return []
    origin, extent = prop.get_actor_bounds(False)
    aft_face = origin.x - extent.x
    x = screen.get_actor_location().x
    # The layout puts both panels exactly 1 cm proud, so the built value sits
    # on the boundary, and get_actor_bounds on a scaled SM_Cube carries
    # float noise: a strict 1.0 would go red with nothing changed. A
    # thousandth of a centimetre is noise; a panel sunk back is not.
    if aft_face - x < PROUD_CM - PROUD_EPSILON_CM:
        return ["the %s's glass is at x %.2f, %.2f cm proud of the desk screen at %.2f; it "
                "must be at least 1" % (name, x, aft_face - x, aft_face)]
    return []


def check_map_screen(ship, every, actors):
    """One system map, labelled as build_hauler labels it, facing aft over
    the middle desk screen and 1 cm proud of it, leaning in to the chart's
    60 cm when the chart chair zooms it. Found by class: a second map, or a
    map that is not an AShipMapScreen, is a screen nobody meant."""
    cls = getattr(unreal, "ShipMapScreen", None)
    if cls is None:
        return ["AShipMapScreen is not compiled into this editor, so the level can hold no map"]
    maps = of_class(every, cls)
    if len(maps) != 1:
        return ["%d map screens, want exactly one" % len(maps)]
    screen = maps[0]
    failures = []
    if screen.get_actor_label() != TAG + "map_screen":
        failures.append("the map is labelled %s, not %smap_screen" % (screen.get_actor_label(), TAG))
    r = screen.get_actor_rotation()
    if abs(((r.yaw - ship.map_screen_yaw) + 180) % 360 - 180) > 1.0 or max(abs(r.pitch), abs(r.roll)) > 1.0:
        failures.append("the map is rotated (%.1f, %.1f, %.1f); the layout says yaw %s"
                        % (r.pitch, r.yaw, r.roll, ship.map_screen_yaw))
    failures += check_proud(ship, actors, screen, ship.map_screen_location, "map")
    got = screen.get_editor_property("view_distance_cm")
    if abs(got - L.MAP_VIEW_DISTANCE) > 1e-3:
        failures.append("the map's view_distance_cm is %.1f, build_hauler sets %.1f"
                        % (got, L.MAP_VIEW_DISTANCE))
    return failures


def check_hum(ship, every, actors):
    """One hum source per layout entry, each where the layout puts it and
    the voice it names; exactly one reactor. A source out of place is not
    only in the wrong room: it seeds its noise from where it stands, so two
    that collide hiss the same noise into a whistle."""
    failures = []
    kinds = {"reactor": unreal.ShipHumKind.REACTOR, "air": unreal.ShipHumKind.AIR}
    built = of_class(every, unreal.ShipHumSource)
    if len(built) != len(ship.hum_sources):
        failures.append("%d hum sources built, layout has %d" % (len(built), len(ship.hum_sources)))
    for label, location, kind in ship.hum_sources:
        actor = actors.get(TAG + label)
        if actor is None:
            failures.append("MISSING " + label)
            continue
        if not isinstance(actor, unreal.ShipHumSource):
            failures.append("%s is a %s, not a ShipHumSource" % (label, actor.get_class().get_name()))
            continue
        p = actor.get_actor_location()
        for a, axis in enumerate("xyz"):
            if abs((p.x, p.y, p.z)[a] - location[a]) > TOLERANCE:
                failures.append("%s.%s is %.1f, layout says %.1f"
                                % (label, axis, (p.x, p.y, p.z)[a], location[a]))
        if actor.get_editor_property("kind") != kinds[kind]:
            failures.append("%s is %s, layout says %s" % (label, actor.get_editor_property("kind"), kind))
    reactors = [a for a in built if a.get_editor_property("kind") == kinds["reactor"]]
    if len(reactors) != 1:
        failures.append("%d reactor voices, want exactly one" % len(reactors))
    return failures


def tags_of(actor):
    return [str(t) for t in actor.get_editor_property("tags")]


EDGES = {"-x": "NEG_X", "+x": "POS_X"}
USES = {"centre": "CENTRE", "+y": "POS_Y", "-y": "NEG_Y"}


def check_surfaces(ship, every, actors):
    """Every dressing marker is where the layout's surface is, and says what
    it says: location within 0.5 cm, yaw, room, kind, ordinal, size, edges,
    clear and every exclude. The C++ reads only these, so this is the place
    the layout and the dressing are held together; tagged Dress.Surface, or
    the generator never finds it."""
    failures = []
    built = of_class(every, unreal.ShipDressingSurface)
    if len(built) != len(ship.surfaces):
        failures.append("%d dressing surfaces built, layout has %d" % (len(built), len(ship.surfaces)))
    for m in ship.surfaces:
        actor = actors.get(TAG + m.label)
        if actor is None or not isinstance(actor, unreal.ShipDressingSurface):
            failures.append("MISSING dressing surface " + m.label)
            continue
        p = actor.get_actor_location()
        for a, axis in enumerate("xyz"):
            if abs((p.x, p.y, p.z)[a] - m.location[a]) > MARKER_TOLERANCE:
                failures.append("%s.%s is %.2f, layout says %.2f"
                                % (m.label, axis, (p.x, p.y, p.z)[a], m.location[a]))
        r = actor.get_actor_rotation()
        if abs(((r.yaw - m.yaw) + 180) % 360 - 180) > 0.5 or max(abs(r.pitch), abs(r.roll)) > 0.5:
            failures.append("%s is rotated (%.1f, %.1f, %.1f); the layout says yaw %s"
                            % (m.label, r.pitch, r.yaw, r.roll, m.yaw))
        if PL.SURFACE_TAG not in tags_of(actor):
            failures.append("%s is not tagged %s" % (m.label, PL.SURFACE_TAG))
        for name, want in (("room", m.room), ("kind", m.kind)):
            got = str(actor.get_editor_property(name))
            if got != want:
                failures.append("%s %s is %s, layout says %s" % (m.label, name, got, want))
        if actor.get_editor_property("ordinal") != m.ordinal:
            failures.append("%s ordinal is %s, layout says %s"
                            % (m.label, actor.get_editor_property("ordinal"), m.ordinal))
        size = actor.get_editor_property("size")
        if abs(size.x - m.size[0]) > 1e-3 or abs(size.y - m.size[1]) > 1e-3:
            failures.append("%s size is (%.1f, %.1f), layout says %s" % (m.label, size.x, size.y, m.size))
        if abs(actor.get_editor_property("clear") - m.clear) > 1e-3:
            failures.append("%s clear is %.1f, layout says %s"
                            % (m.label, actor.get_editor_property("clear"), m.clear))
        if actor.get_editor_property("back") != getattr(unreal.DressEdge, EDGES[m.back]):
            failures.append("%s back is %s, layout says %s" % (m.label, actor.get_editor_property("back"), m.back))
        if actor.get_editor_property("use") != getattr(unreal.DressUse, USES[m.use]):
            failures.append("%s use is %s, layout says %s" % (m.label, actor.get_editor_property("use"), m.use))
        excludes = list(actor.get_editor_property("excludes"))
        if len(excludes) != len(m.excludes):
            failures.append("%s has %d excludes, layout says %d" % (m.label, len(excludes), len(m.excludes)))
        else:
            for got, (lo, hi) in zip(excludes, m.excludes):
                g = (got.min.x, got.min.y, got.max.x, got.max.y)
                if max(abs(a - b) for a, b in zip(g, (lo[0], lo[1], hi[0], hi[1]))) > 1e-3:
                    failures.append("%s exclude is %s, layout says %s" % (m.label, g, (lo, hi)))
    return failures


def check_keep_outs(ship, every, actors):
    """Every zone the dressing may never touch, as a tagged box exactly the
    layout's: each door's and the console's keep-clear zone, the slide run,
    the crawlway."""
    failures = []
    built = of_class(every, unreal.ShipDressingKeepOut)
    if len(built) != len(ship.keep_outs):
        failures.append("%d keep-outs built, layout has %d" % (len(built), len(ship.keep_outs)))
    for k in ship.keep_outs:
        actor = actors.get(TAG + k.label)
        if actor is None or not isinstance(actor, unreal.ShipDressingKeepOut):
            failures.append("MISSING keep-out " + k.label)
            continue
        if PL.KEEP_OUT_TAG not in tags_of(actor):
            failures.append("%s is not tagged %s" % (k.label, PL.KEEP_OUT_TAG))
        p = actor.get_actor_location()
        size = actor.get_editor_property("size")
        for a, axis in enumerate("xyz"):
            lo = (p.x, p.y, p.z)[a] - (size.x, size.y, size.z)[a] / 2.0
            hi = (p.x, p.y, p.z)[a] + (size.x, size.y, size.z)[a] / 2.0
            if abs(lo - k.lo[a]) > MARKER_TOLERANCE or abs(hi - k.hi[a]) > MARKER_TOLERANCE:
                failures.append("%s spans %.1f..%.1f in %s, layout says %s..%s"
                                % (k.label, lo, hi, axis, k.lo[a], k.hi[a]))
    return failures


def check_wear_tags(ship, every, actors):
    """Every furniture part carries Dress.Wear and the Piece.<prop>_<n> of
    the placement it belongs to, so a whole desk wears together; nothing
    else carries Dress.Wear, or a lamp panel or a screen could be swapped to
    a furniture colour."""
    failures = []
    furniture = {TAG + b.label: b for b in ship.boxes if b.role == "furniture"}
    for label, box in furniture.items():
        actor = actors.get(label)
        if actor is None:
            continue                        # reported as MISSING already
        tags = tags_of(actor)
        want = PL.PIECE_TAG_PREFIX + PL.piece_of(box.label)
        if PL.WEAR_TAG not in tags or want not in tags:
            failures.append("%s is tagged %s, want %s and %s" % (box.label, tags, PL.WEAR_TAG, want))
    for actor in every:
        if PL.WEAR_TAG in tags_of(actor) and actor.get_actor_label() not in furniture:
            failures.append("%s is tagged %s but is not furniture" % (actor.get_actor_label(), PL.WEAR_TAG))
    return failures


main()
