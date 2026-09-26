#!/usr/bin/env python3
"""
Tests for props and placement: rotation, stacking, containment, and derived
lights and mounts.

    python3 Tools/test_placement.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hauler_layout as L
import props as P
from floorplan import FloorPlan, PlanError, Room
from placement import (KEEP_OUT_TAG, LAMPS_TAG, LIGHTS_TAG, PIECE_TAG_PREFIX, SKY_DIRECTORY,
                       SURFACE_TAG, WEAR_TAG, Mood, Mount, Place, Practical, kelvin_to_rgb,
                       lamp_emissive, lamp_role, piece_of, resolve_lights, resolve_mount,
                       resolve_point, resolve_practicals, resolve_props, resolve_surfaces,
                       sky_asset, LIGHT_SPACING)

ROOM = Room("r", 1000, 2000, 400, 300, 250)
PLAN = FloorPlan([ROOM])
MOODS = {"r": Mood(6500, 1.0)}
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def near(a, b):
    return all(abs(x - y) < 1e-6 for x, y in zip(a, b))


def test_rotate_quarter_turn_swaps_size_and_turns_position():
    at, size = P.rotate((10, 0, 5), (40, 20, 8), 90)
    assert near(at, (0, 10, 5)) and near(size, (20, 40, 8))


def test_rotate_half_turn_negates_position_and_keeps_size():
    at, size = P.rotate((10, 3, 5), (40, 20, 8), 180)
    assert near(at, (-10, -3, 5)) and near(size, (40, 20, 8))


def test_rotate_rejects_non_right_angles():
    try:
        P.rotate((0, 0, 0), (1, 1, 1), 45)
    except ValueError:
        return
    raise AssertionError("expected ValueError")


def test_placement_is_relative_to_the_room_corner():
    P.PROPS["_probe"] = [P.Part("cube", (0, 0, 10), (20, 20, 20), "furniture")]
    try:
        box, = resolve_props(PLAN, [Place("_probe", "r", (100, 50))])
        assert near(box.centre, (1100, 2050, 10)), box.centre
    finally:
        del P.PROPS["_probe"]


def test_prop_parts_are_labelled_by_prop_instance_and_part():
    # build_hauler.py and verify_level.py find actors by these labels, so the
    # format is the join between them.
    P.PROPS["_probe"] = [P.Part("cube", (0, 0, 10), (20, 20, 20), "furniture"),
                         P.Part("cube", (0, 0, 30), (20, 20, 20), "furniture")]
    try:
        boxes = resolve_props(PLAN, [Place("_probe", "r", (100, 50)),
                                     Place("_probe", "r", (200, 50))])
        assert [b.label for b in boxes] == ["prop__probe_0_0", "prop__probe_0_1",
                                            "prop__probe_1_0", "prop__probe_1_1"], \
            [b.label for b in boxes]
    finally:
        del P.PROPS["_probe"]


def test_level_stacks_by_the_props_own_height():
    P.PROPS["_probe"] = [P.Part("cube", (0, 0, 30), (20, 20, 60), "furniture")]
    try:
        _, top = resolve_props(PLAN, [Place("_probe", "r", (100, 50)),
                                      Place("_probe", "r", (100, 50), level=1)])
        assert near(top.centre, (1100, 2050, 90)), top.centre
    finally:
        del P.PROPS["_probe"]


def test_part_through_a_wall_is_rejected():
    P.PROPS["_probe"] = [P.Part("cube", (0, 0, 10), (100, 20, 20), "furniture")]
    try:
        resolve_props(PLAN, [Place("_probe", "r", (20, 50))])
    except PlanError as e:
        assert "leaves room" in str(e)
    else:
        raise AssertionError("expected PlanError")
    finally:
        del P.PROPS["_probe"]


def test_part_through_the_ceiling_is_rejected():
    P.PROPS["_probe"] = [P.Part("cube", (0, 0, 10), (20, 20, 20), "furniture")]
    try:
        resolve_props(PLAN, [Place("_probe", "r", (100, 50), elevation=240)])
    except PlanError as e:
        assert "leaves room" in str(e)
    else:
        raise AssertionError("expected PlanError")
    finally:
        del P.PROPS["_probe"]


def test_unknown_prop_and_room_are_rejected():
    for place in (Place("no_such_prop", "r", (0, 0)), Place("bed", "no_such_room", (0, 0))):
        try:
            resolve_props(PLAN, [place])
        except PlanError:
            continue
        raise AssertionError("expected PlanError for %r" % (place,))


def test_every_template_prop_fits_a_generous_room():
    # Each where it hangs: a ceiling prop up against the ceiling, a wall prop
    # with its outline's -x edge on the room's aft wall, anything else on the
    # floor in the middle.
    big = FloorPlan([Room("big", 0, 0, 3000, 3000, 500)])
    for name in P.PROPS:
        anchor = P.ANCHOR.get(name)
        if anchor == "ceiling":
            place = Place(name, "big", (1500, 1500), elevation=500 - P.height(name))
        elif anchor == "wall":
            place = Place(name, "big", (-P.footprint(name)[0][0], 1500))
        else:
            place = Place(name, "big", (1500, 1500))
        resolve_props(big, [place])


# -- anchoring ---------------------------------------------------------------

def test_a_counter_floating_in_the_room_is_rejected():
    # ADR 0008's probe found exactly this: a wall prop in the middle of the
    # floor validates as sound, so the anchor has to be checked on its own.
    try:
        # Turned along the room's length: 300 cm of counter in a 400 cm room,
        # touching neither end nor either side.
        resolve_props(PLAN, [Place("counter", "r", (200, 150), facing=90)])
    except PlanError as e:
        assert "touches no wall" in str(e), e
    else:
        raise AssertionError("expected PlanError")


def test_a_ceiling_panel_on_the_floor_is_rejected():
    try:
        resolve_props(PLAN, [Place("overhead_panel", "r", (200, 150))])
    except PlanError as e:
        assert "touches no ceiling" in str(e), e
    else:
        raise AssertionError("expected PlanError")


def test_a_counter_against_its_wall_is_accepted():
    resolve_props(PLAN, [Place("counter", "r", (30, 150))])
    resolve_props(PLAN, [Place("overhead_panel", "r", (200, 150), elevation=ROOM.height - 22)])


def test_every_anchor_names_a_prop():
    assert set(P.ANCHOR) <= set(P.PROPS), set(P.ANCHOR) - set(P.PROPS)
    assert set(P.ANCHOR.values()) <= {"ceiling", "wall"}


def test_lights_follow_the_grid_spacing():
    lights, lamps = resolve_lights(PLAN, MOODS)
    # 400 x 300 at 300 cm spacing: 2 x 1.
    assert len(lights) == 2 and len(lamps) == 2
    assert all(l.location[2] == ROOM.height - 20 for l in lights)
    assert LIGHT_SPACING == 300


# -- colour and moods ----------------------------------------------------

def test_warm_light_runs_red_over_green_over_blue():
    # And blue is still there: a 2700 K lamp is amber, not a sodium lamp.
    r, g, b = kelvin_to_rgb(2700)
    assert r > g > b > 0, (r, g, b)


def test_daylight_is_white_within_three_percent():
    for c in kelvin_to_rgb(6500):
        assert abs(c - 255) <= 0.03 * 255, kelvin_to_rgb(6500)


def test_colour_cools_monotonically_with_temperature():
    # Blue rises and red never does as a lamp runs hotter, across every
    # temperature the ship uses: a mood cannot come out warmer than one
    # listed below it.
    # Strictly: every mood lies between 1900 K and 6600 K, where the fit's
    # green and blue both climb, so two moods that differ in kelvin must
    # differ in colour. Non-decreasing alone passes a curve that clamps every
    # ship temperature to the same blue.
    temps = sorted(set(m.kelvin for m in L.ROOM_MOOD.values()))
    cols = [kelvin_to_rgb(k) for k in temps]
    for (r0, g0, b0), (r1, g1, b1) in zip(cols, cols[1:]):
        assert b1 > b0 and g1 > g0 and r1 <= r0, list(zip(temps, cols))


def test_a_room_without_a_mood_is_rejected():
    try:
        resolve_lights(PLAN, {})
    except PlanError as e:
        assert "no mood" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_a_rooms_lights_burn_at_its_mood():
    lights, _ = resolve_lights(PLAN, {"r": Mood(2700, 0.5)})
    full, _ = resolve_lights(PLAN, MOODS)
    for dim, bright in zip(lights, full):
        assert dim.colour == kelvin_to_rgb(2700)
        assert abs(dim.intensity - 0.5 * bright.intensity) < 1e-9
        assert not dim.shadows


def test_every_room_in_the_ship_has_a_mood():
    ship = L.generate()
    assert set(L.ROOM_MOOD) == set(ship.plan.rooms), \
        set(L.ROOM_MOOD) ^ set(ship.plan.rooms)


def test_lamp_roles_match_the_room_they_are_in():
    # Every glowing box, ceiling panel or a prop's bulb, wears the lamp role
    # of the room it sits in, so it glows that room's colour and no other.
    ship = L.generate()
    lamps = [b for b in ship.boxes if b.role.startswith("lamp")]
    assert lamps
    for b in lamps:
        rooms = [n for n, r in ship.plan.rooms.items()
                 if r.x <= b.centre[0] <= r.x + r.w and r.y <= b.centre[1] <= r.y + r.d]
        assert len(rooms) == 1 and b.role == lamp_role(rooms[0]), (b.label, b.role, rooms)


def test_a_prop_lamp_part_takes_its_rooms_role():
    box = [b for b in resolve_props(PLAN, [Place("desk_lamp", "r", (100, 100))])
           if b.role.startswith("lamp")]
    assert [b.role for b in box] == ["lamp_r"], [b.role for b in box]


def test_lamp_panels_glow_the_colour_of_their_rooms_light():
    # The panel is emissive in linear; the light is sRGB. Taken back to sRGB
    # at the same peak, the panel's colour is the light's to within rounding.
    def to_srgb(c):
        return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055
    for room, mood in L.ROOM_MOOD.items():
        glow = lamp_emissive(mood)
        scale = 6.0 * mood.scale
        back = [255 * to_srgb(c / scale) for c in glow]
        for got, want in zip(back, kelvin_to_rgb(mood.kelvin)):
            assert abs(got - want) < 0.5, (room, back, kelvin_to_rgb(mood.kelvin))


def test_the_cockpit_is_the_dimmest_room():
    # Scale is only felt in contrast: the window must outshine the room.
    dimmest = min(L.ROOM_MOOD, key=lambda r: L.ROOM_MOOD[r].scale)
    assert dimmest == "cockpit", dimmest


# -- practicals ----------------------------------------------------------

def test_practicals_are_the_only_shadowed_lights():
    ship = L.generate()
    shadowed = sorted(l.label for l in ship.lights if l.shadows)
    want = sorted("light_practical_%s_%s" % (p.place.room, p.place.prop)
                  for p in L.PRACTICALS)
    assert shadowed == want, shadowed
    assert len(want) == 3


def test_practicals_are_ship_lights_and_so_carry_the_lights_tag():
    # build_hauler.place_lights tags every light in ship.lights with
    # LIGHTS_TAG and makes it Movable; verify_level.py checks both on the
    # built actors. A practical outside that list would be a lamp the power
    # split could never dim.
    ship = L.generate()
    labels = [l.label for l in ship.lights]
    for p in L.PRACTICALS:
        assert "light_practical_%s_%s" % (p.place.room, p.place.prop) in labels
    assert len(labels) == len(set(labels))


def test_the_lights_tag_is_the_one_the_cpp_dims():
    with open(os.path.join(ROOT, "Source/DeepSpace/Ship/ShipPowerState.cpp")) as f:
        cpp = f.read()
    assert 'Lights(TEXT("%s"))' % LIGHTS_TAG in cpp, LIGHTS_TAG


def test_sky_asset_paths_are_the_ones_the_cpp_loads():
    """The level assigns what C++ checks for: every *Path in
    SkyMaterialContract.h is sky_asset of its own asset name, and every
    material and collection the contract authors has one."""
    import json
    import re
    with open(os.path.join(ROOT, "Source/DeepSpace/Sky/SkyMaterialContract.h")) as f:
        header = f.read()
    paths = re.findall(r'\w+Path = TEXT\("([^"]+)"\)', header)
    assert paths, "no paths found in SkyMaterialContract.h"
    for path in paths:
        name = path.rsplit(".", 1)[1]
        assert sky_asset(name) == path, (sky_asset(name), path)
    with open(os.path.join(ROOT, "Tools/sky_material_contract.json")) as f:
        contract = json.load(f)
    authored = list(contract["materials"]) + list(contract["collections"])
    for name in authored:
        assert sky_asset(name) in paths, name
    assert SKY_DIRECTORY == "/Game/Materials/Sky"


def test_a_practical_burns_at_its_rooms_kelvin():
    ship = L.generate()
    by_label = {l.label: l for l in ship.lights}
    for p in L.PRACTICALS:
        light = by_label["light_practical_%s_%s" % (p.place.room, p.place.prop)]
        assert light.colour == kelvin_to_rgb(L.ROOM_MOOD[p.place.room].kelvin)


def test_a_practical_light_turns_and_lifts_with_its_prop():
    P.HEADS["_probe"] = (10, 0, 30)
    P.PROPS["_probe"] = [P.Part("cube", (0, 0, 20), (4, 4, 40), "furniture")]
    try:
        _, (light,) = resolve_practicals(
            PLAN, [Practical(Place("_probe", "r", (100, 50), facing=90, elevation=70), 150, 1.0)],
            MOODS)
        assert near(light.location, (1100, 2060, 100)), light.location
        assert light.shadows and light.radius == 150
    finally:
        del P.HEADS["_probe"], P.PROPS["_probe"]


def test_a_practical_without_a_head_is_rejected():
    try:
        resolve_practicals(PLAN, [Practical(Place("bed", "r", (200, 150)), 100, 1.0)], MOODS)
    except PlanError as e:
        assert "no head" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_every_head_sits_just_under_its_props_glowing_part():
    # The light is where the bulb is: under a lamp part, inside its footprint,
    # within a few centimetres, so the glow and the light cannot drift apart.
    for name, (hx, hy, hz) in P.HEADS.items():
        bulbs = [p for p in P.PROPS[name] if p.role == "lamp"]
        assert len(bulbs) == 1, name
        (bx, by, bz), (sx, sy, sz) = bulbs[0].at, bulbs[0].size
        assert abs(hx - bx) <= sx / 2 and abs(hy - by) <= sy / 2, name
        assert 0 < (bz - sz / 2) - hz <= 5, name


def test_placements_is_every_prop_lamps_included():
    # PLACEMENTS is the one list a consumer reads. The lamps live in
    # PRACTICALS because each also carries a light, and a list that lost them
    # would lose the galley counter, the ship's main dressing surface, and
    # leave the lamp bases on the desk and bench for clutter to go through.
    for p in L.PRACTICALS:
        assert p.place in L.PLACEMENTS, p.place
    assert [p.prop for p in L.PLACEMENTS].count("counter") == 1
    assert len(L.PLACEMENTS) == len(L.FURNITURE) + len(L.PRACTICALS)
    # And it is what the ship is built from, not a parallel copy of it.
    ship = L.generate()
    built = sorted(b.label for b in ship.boxes if b.label.startswith("prop_"))
    want = sorted(b.label for b in resolve_props(ship.plan, L.PLACEMENTS))
    assert built == want, set(built) ^ set(want)


def test_every_practical_rests_on_a_surface():
    # Its lowest part's underside is the top of something beneath it, so a
    # lamp neither floats over the desk nor sinks into it.
    ship = L.generate()
    places = L.PLACEMENTS
    for i, place in enumerate(places):
        if place not in [x.place for x in L.PRACTICALS] or place.elevation == 0:
            continue
        n = sum(1 for q in places[:i] if q.prop == place.prop)
        parts = [b for b in ship.boxes if b.label.startswith("prop_%s_%d_" % (place.prop, n))]
        low = min(parts, key=lambda b: b.centre[2] - b.size[2] / 2)
        bottom = low.centre[2] - low.size[2] / 2
        under = [b for b in ship.boxes if b not in parts
                 and abs(b.centre[2] + b.size[2] / 2 - bottom) < 0.01
                 and abs(b.centre[0] - low.centre[0]) <= (b.size[0] - low.size[0]) / 2
                 and abs(b.centre[1] - low.centre[1]) <= (b.size[1] - low.size[1]) / 2]
        assert under, "%s stands on nothing at z %.1f" % (place.prop, bottom)


def test_mount_sits_on_the_wall_facing_in():
    (x, y, z), yaw = resolve_mount(PLAN, Mount("r", "starboard", 120))
    assert yaw == 90 and z == 120
    assert x == ROOM.x + ROOM.w / 2.0
    assert ROOM.y < y < ROOM.y + ROOM.d          # inside the room, near the wall
    assert ROOM.y + ROOM.d - y < 20


def test_the_lamps_tag_is_the_one_the_cpp_dims():
    with open(os.path.join(ROOT, "Source/DeepSpace/Ship/ShipLightingSubsystem.cpp")) as f:
        cpp = f.read()
    assert 'LampsTag(TEXT("%s"))' % LAMPS_TAG in cpp, LAMPS_TAG


def test_every_glowing_box_is_a_lamp_the_lighting_can_find():
    # build_hauler tags a box LAMPS_TAG exactly when its role starts lamp_.
    # That has to be every ceiling panel and every prop part that glows, or
    # something stays lit over a browned-out room.
    ship = L.generate()
    lamps = [b for b in ship.boxes if b.role.startswith("lamp_")]
    panels = [b for b in lamps if b.label.startswith("lamp_")]
    parts = [b for b in lamps if b.label.startswith("prop_")]
    assert len(panels) == sum(1 for l in ship.lights if not l.shadows)
    assert len(parts) == len(L.PRACTICALS), [b.label for b in parts]
    assert not [b for b in ship.boxes if b.role == "lamp"]


# -- the chart ---------------------------------------------------------------

def _chart_prop(ship):
    """The desk screen prop the chart covers: the screen box whose y-span
    holds the chart's y."""
    _, y, _ = ship.nav_screen_location
    screens = [b for b in ship.boxes if b.role == "screen"
               and abs(b.centre[1] - y) <= b.size[1] / 2.0]
    assert len(screens) == 1, [b.label for b in screens]
    return screens[0]


def test_the_chart_is_in_the_cockpit_facing_aft():
    ship = L.generate()
    x, y, z = ship.nav_screen_location
    r = ship.plan.room("cockpit")
    assert r.x < x < r.x + r.w and r.y < y < r.y + r.d and 0 < z < r.height
    assert ship.nav_screen_yaw == 0
    assert L.NAV_SCREEN[0] == "cockpit"


def test_the_chart_is_read_from_the_starboard_pilot_seat():
    # Within 20 cm sideways of the chair: the seat follows the glass's
    # normal, so further off and the body sits beside the cushion.
    ship = L.generate()
    seats = [p for p in L.FURNITURE if p.prop == "pilot_seat" and p.room == "cockpit"]
    starboard = max(seats, key=lambda p: p.at[1])
    assert starboard.at == L.NAV_SCREEN_CHAIR[1]
    _, seat_y, _ = resolve_point(ship.plan, "cockpit", starboard.at)
    assert abs(ship.nav_screen_location[1] - seat_y) <= 20, ship.nav_screen_location
    # And not the helm's seat: the chart is the other chair.
    assert starboard.at != L.PILOT_SEAT[1]


def test_the_chart_stands_proud_of_the_desk_screen_behind_it():
    # At least 1 cm. The reach volume starts 0.5 cm behind the glass, and
    # the prop blocks Visibility: closer, and the prop takes the bezel.
    ship = L.generate()
    prop = _chart_prop(ship)
    aft_face = prop.centre[0] - prop.size[0] / 2.0
    assert aft_face - ship.nav_screen_location[0] >= 1.0, (aft_face, ship.nav_screen_location)
    # Level with the prop, so the chart covers the screen it replaces.
    lo, hi = prop.centre[2] - prop.size[2] / 2.0, prop.centre[2] + prop.size[2] / 2.0
    assert lo < ship.nav_screen_location[2] < hi


def test_the_chart_width_is_the_one_the_cpp_draws():
    with open(os.path.join(ROOT, "Source/DeepSpace/Ship/ShipNavScreen.cpp")) as f:
        cpp = f.read()
    assert "PanelWidthCm = %d.0f;" % L.NAV_SCREEN_WIDTH in cpp, L.NAV_SCREEN_WIDTH


def test_the_chart_exclude_covers_the_chart_and_reaches_its_chair():
    # Plan conflict 16: the footprint plus the laptop's margin, and the strip
    # to the chair. Slice 3's clutter reads it; nothing may sit on the chart.
    ship = L.generate()
    (x0, y0), (x1, y1) = ship.nav_screen_exclude
    x, y, _ = ship.nav_screen_location
    half = L.NAV_SCREEN_WIDTH / 2.0
    m = L.NAV_SCREEN_MARGIN
    assert x0 <= x - m and x1 >= x + m
    assert y0 <= y - half - m and y1 >= y + half + m
    chair_x, _, _ = resolve_point(ship.plan, *L.NAV_SCREEN_CHAIR)
    assert x0 <= chair_x
    # It is on the desk's starboard half, never over the helm's side.
    helm_y = resolve_point(ship.plan, L.PILOT_SEAT[0], L.PILOT_SEAT[1])[1]
    assert y0 > helm_y


# -- dressing surfaces ---------------------------------------------------------

def _surface(ship, kind, ordinal=0):
    found = [m for m in ship.surfaces if m.kind == kind and m.ordinal == ordinal]
    assert len(found) == 1, (kind, ordinal, [m.label for m in ship.surfaces])
    return found[0]


def _world_rect(m):
    """A surface marker's plan outline, world."""
    (lo, hi) = P.rotate_rect((-m.size[0] / 2.0, -m.size[1] / 2.0),
                             (m.size[0] / 2.0, m.size[1] / 2.0), m.yaw)
    x, y, _ = m.location
    return (x + lo[0], y + lo[1]), (x + hi[0], y + hi[1])


def _local(m, point):
    """A world (x, y) in a surface marker's own frame."""
    (x, y, _), _ = P.rotate((point[0] - m.location[0], point[1] - m.location[1], 0), (0, 0, 0),
                            (360 - m.yaw) % 360)
    return x, y


def test_every_accepted_kind_is_a_surface_and_every_room_says_what_it_takes():
    kinds = {"%s.%s" % (prop, s.name) for prop, surfaces in P.SURFACES.items() for s in surfaces}
    for room, accepted in L.ROOM_DRESSING.items():
        assert set(accepted) <= kinds, (room, set(accepted) - kinds)
    assert set(L.ROOM_DRESSING) == {r.name for r in L.ROOMS}


def test_the_corridor_and_the_crawlway_take_nothing():
    # The slide run and the crouch-only height are what those rooms are for.
    assert L.ROOM_DRESSING["corridor"] == () and L.ROOM_DRESSING["crawlway"] == ()
    ship = L.generate()
    assert not [m for m in ship.surfaces if m.room in ("corridor", "crawlway")]


def test_every_surface_the_rooms_accept_is_exported_once():
    ship = L.generate()
    want = sorted((p.room, "%s.%s" % (p.prop, s.name)) for p in L.PLACEMENTS
                  for s in P.SURFACES.get(p.prop, ())
                  if "%s.%s" % (p.prop, s.name) in L.ROOM_DRESSING[p.room])
    got = sorted((m.room, m.kind) for m in ship.surfaces)
    assert got == want, (got, want)
    labels = [m.label for m in ship.surfaces]
    assert len(labels) == len(set(labels)), labels
    keys = [(m.room, m.kind, m.ordinal) for m in ship.surfaces]
    assert len(keys) == len(set(keys)), keys


def test_surfaces_are_read_from_placements_so_the_counter_is_dressed():
    # The counter is a practical. Given FURNITURE, it and its affinity-4 top
    # would vanish from the ship without a single error.
    ship = L.generate()
    counter = _surface(ship, "counter.top")
    (place,) = [p for p in L.PLACEMENTS if p.prop == "counter"]
    x, y, _ = resolve_point(ship.plan, place.room, place.at)
    assert counter.location[:2] == (x, y) and counter.location[2] == 90


def test_every_surface_lies_on_its_prop_at_its_props_height():
    # Inside the prop's own plan outline, and level with the top of one of its
    # parts, so nothing is dressed onto air beside the table or into its top.
    ship = L.generate()
    for m in ship.surfaces:
        prop = m.kind.split(".")[0]
        parts = [b for b in ship.boxes if b.label.startswith("prop_%s_%d_" % (prop, m.ordinal))]
        assert parts, m.label
        (x0, y0), (x1, y1) = _world_rect(m)
        assert x0 >= min(b.centre[0] - b.size[0] / 2.0 for b in parts) - 1e-6, m.label
        assert x1 <= max(b.centre[0] + b.size[0] / 2.0 for b in parts) + 1e-6, m.label
        assert y0 >= min(b.centre[1] - b.size[1] / 2.0 for b in parts) - 1e-6, m.label
        assert y1 <= max(b.centre[1] + b.size[1] / 2.0 for b in parts) + 1e-6, m.label
        tops = [b.centre[2] + b.size[2] / 2.0 for b in parts]
        assert any(abs(t - m.location[2]) < 0.01 for t in tops), (m.label, m.location[2], tops)


def test_nothing_stands_in_a_surfaces_free_column():
    # The column above a surface, up to its clear, less its excludes, holds no
    # part of any prop: not a lamp's base or arm, not the next shelf, not the
    # uppers over the counter. What clutter may fill is genuinely empty.
    ship = L.generate()
    props = [b for b in ship.boxes if b.label.startswith("prop_")]
    for m in ship.surfaces:
        (x0, y0), (x1, y1) = _world_rect(m)
        z0, z1 = m.location[2], m.location[2] + m.clear
        for b in props:
            lo = [b.centre[a] - b.size[a] / 2.0 for a in range(3)]
            hi = [b.centre[a] + b.size[a] / 2.0 for a in range(3)]
            ix = (max(lo[0], x0), min(hi[0], x1))
            iy = (max(lo[1], y0), min(hi[1], y1))
            iz = (max(lo[2], z0), min(hi[2], z1))
            if min(ix[1] - ix[0], iy[1] - iy[0], iz[1] - iz[0]) <= 1e-6:
                continue
            a = _local(m, (ix[0], iy[0]))
            c = _local(m, (ix[1], iy[1]))
            lo2 = (min(a[0], c[0]), min(a[1], c[1]))
            hi2 = (max(a[0], c[0]), max(a[1], c[1]))
            assert any(e[0][0] - 1e-6 <= lo2[0] and e[0][1] - 1e-6 <= lo2[1]
                       and hi2[0] <= e[1][0] + 1e-6 and hi2[1] <= e[1][1] + 1e-6
                       for e in m.excludes), "%s stands in %s's free column" % (b.label, m.label)


def test_a_lamp_moved_takes_its_exclude_with_it():
    # The rule, not a list: every placement resting on a surface is excluded
    # by its footprint plus the margin. Move the desk lamp and the exclude
    # follows it; take it away and the desk is free.
    plan = FloorPlan(L.ROOMS, L.DOORS, L.WINDOWS, L.SEALS)
    lamp = [p for p in L.PLACEMENTS if p.prop == "desk_lamp"][0]
    rest = [p for p in L.PLACEMENTS if p is not lamp]
    moved = lamp._replace(at=(lamp.at[0], lamp.at[1] - 60))
    def desk(placements):
        return [m for m in resolve_surfaces(plan, placements, L.ROOM_DRESSING)
                if m.kind == "desk.top"][0]
    here, there, gone = desk(rest + [lamp]), desk(rest + [moved]), desk(rest)
    assert len(here.excludes) == 1 and len(there.excludes) == 1 and gone.excludes == ()
    # The desk is turned half round, so world -y is the desk's +y.
    assert abs(there.excludes[0][1][1] - min(60.0, here.excludes[0][1][1] + 60)) < 1e-6, \
        (here.excludes, there.excludes)


def test_the_galley_table_carries_the_laptops_exclude():
    # Its footprint plus the margin, and the strip on its user's side right
    # to the table's edge: nothing between the reader and the screen.
    ship = L.generate()
    table = _surface(ship, "galley_table.top")
    lx, ly, _ = ship.laptop_location
    corners = []
    (fx0, fy0), (fx1, fy1) = L.LAPTOP_FOOTPRINT
    for fx, fy in ((fx0, fy0), (fx1, fy1)):
        (wx, wy, _), _ = P.rotate((fx, fy, 0), (0, 0, 0), ship.laptop_yaw)
        corners.append(_local(table, (lx + wx, ly + wy)))
    lo = (min(c[0] for c in corners), min(c[1] for c in corners))
    hi = (max(c[0] for c in corners), max(c[1] for c in corners))
    m = L.DRESS_MARGIN
    covering = [e for e in table.excludes
                if e[0][0] <= lo[0] - m + 1e-6 and e[0][1] <= lo[1] - m + 1e-6
                and e[1][0] >= hi[0] + m - 1e-6 and e[1][1] >= hi[1] + m - 1e-6]
    assert covering, (table.excludes, lo, hi)
    # The user sits on the laptop's -X side: world -y here, so the strip
    # reaches the table's edge on that side.
    user_edge = _local(table, (lx, ly - 1000))
    edge = -table.size[0] / 2.0 if user_edge[0] < 0 else table.size[0] / 2.0
    assert any(abs(e[0][0] - edge) < 1e-6 or abs(e[1][0] - edge) < 1e-6 for e in covering), \
        (covering, edge)


def test_the_chart_exclude_reaches_the_cockpit_desk():
    # Plan conflict 16, resolved onto the surface it lies over.
    ship = L.generate()
    wing = _surface(ship, "cockpit_desk.wing_stbd")
    assert wing.excludes, wing
    port = _surface(ship, "cockpit_desk.wing_port")
    assert port.excludes == (), port


def test_the_laptop_footprint_is_the_cpp_laptops():
    with open(os.path.join(ROOT, "Source/DeepSpace/Ship/ShipLaptop.cpp")) as f:
        cpp = f.read()
    (x0, y0), (x1, y1) = L.LAPTOP_FOOTPRINT
    assert "BaseSize(%d.0f, %d.0f" % (-2 * x0, y1 - y0) in cpp, L.LAPTOP_FOOTPRINT


def test_a_non_ascii_room_or_kind_is_rejected():
    plan = FloorPlan([Room("r\u00e9", 0, 0, 400, 300, 250)])
    try:
        resolve_surfaces(plan, [Place("desk", "r\u00e9", (100, 100))], {"r\u00e9": ("desk.top",)})
    except PlanError as e:
        assert "ASCII" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_every_surface_is_well_above_the_floor():
    # Surface clutter never touches the floor. The C++ refuses anything below
    # its guarantee; the layout should never ask.
    ship = L.generate()
    assert all(m.location[2] >= 20 for m in ship.surfaces), \
        [(m.label, m.location[2]) for m in ship.surfaces]


def test_every_surface_is_turned_in_quarter_turns_with_edges_it_knows():
    ship = L.generate()
    for m in ship.surfaces:
        assert m.yaw in (0, 90, 180, 270) and m.back in ("+x", "-x") \
            and m.use in ("centre", "+y", "-y"), m


def test_keep_outs_are_every_door_the_console_the_slide_run_and_the_crawlway():
    ship = L.generate()
    labels = [k.label for k in ship.keep_outs]
    doors = [k for k in ship.keep_clear if k[0] != "console"]
    assert labels.count("keepout_console") == 1
    assert len([l for l in labels if l.startswith("keepout_door_")]) == len(doors) == len(L.DOORS)
    for zone in ship.keep_clear:
        assert any(k.lo == zone[1] and k.hi == zone[2] for k in ship.keep_outs), zone
    for label, room in (("keepout_slide_run", L.SLIDE_ROOM), ("keepout_crawlway", "crawlway")):
        (k,) = [k for k in ship.keep_outs if k.label == label]
        r = ship.plan.room(room)
        assert k.lo == (r.x, r.y, 0) and k.hi == (r.x + r.w, r.y + r.d, r.height), k
    assert len(labels) == len(set(labels))


def test_the_dressing_tags_are_the_ones_the_cpp_finds():
    with open(os.path.join(ROOT, "Source/DeepSpace/Ship/ShipDressingTypes.cpp")) as f:
        cpp = f.read()
    for name, tag in (("SurfaceTag", SURFACE_TAG), ("KeepOutTag", KEEP_OUT_TAG),
                      ("WearTag", WEAR_TAG), ("PieceTagPrefix", PIECE_TAG_PREFIX)):
        assert '%s(TEXT("%s"))' % (name, tag) in cpp, (name, tag)


def test_every_exported_kind_has_rules_in_the_cpp():
    # A kind the generator has no rules for is dressed with nothing, silently.
    with open(os.path.join(ROOT, "Source/DeepSpace/Ship/ShipDressingRules.cpp")) as f:
        cpp = f.read()
    for m in L.generate().surfaces:
        prop = m.kind.split(".")[0]
        assert 'Kind(TEXT("%s")' % m.kind in cpp or 'Kind(TEXT("%s")' % prop in cpp, m.kind


def test_every_furniture_part_is_a_piece():
    ship = L.generate()
    for b in ship.boxes:
        if b.role == "furniture":
            assert b.label.startswith("prop_"), b.label
    assert piece_of("prop_cockpit_desk_0_3") == "cockpit_desk_0"
    assert piece_of("prop_bed_0_0") == "bed_0"


# -- the hum -----------------------------------------------------------------

def test_no_two_hum_sources_share_a_point():
    # Each source seeds its noise from its world position rounded to 1 cm;
    # two at one point hiss the same noise and comb into a whistle.
    ship = L.generate()
    points = [tuple(round(c) for c in loc) for _, loc, _ in ship.hum_sources]
    assert len(points) == len(set(points)), points
    labels = [label for label, _, _ in ship.hum_sources]
    assert len(labels) == len(set(labels)), labels


def test_there_is_one_reactor_voice_at_the_reactor():
    ship = L.generate()
    reactors = [s for s in ship.hum_sources if s[2] == "reactor"]
    assert len(reactors) == 1 and reactors[0][0] == "hum_reactor"
    (reactor,) = [p for p in L.PLACEMENTS if p.prop == "reactor"]
    x, y, _ = resolve_point(ship.plan, reactor.room, reactor.at)
    assert reactors[0][1][:2] == (x, y), (reactors[0][1], (x, y))
    assert {s[2] for s in ship.hum_sources} == {"reactor", "air"}


def test_every_room_has_its_air_under_its_own_ceiling():
    ship = L.generate()
    air = {label: loc for label, loc, kind in ship.hum_sources if kind == "air"}
    assert sorted(air) == sorted("hum_" + r.name for r in L.ROOMS)
    for r in L.ROOMS:
        x, y, z = air["hum_" + r.name]
        assert r.x < x < r.x + r.w and r.y < y < r.y + r.d, r.name
        assert 0 < z < r.height and r.height - z <= 30, (r.name, z)


def main():
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_")]
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print("  ok    " + name)
        except Exception as e:                     # noqa: BLE001
            failed += 1
            print("  FAIL  %s: %s: %s" % (name, type(e).__name__, e))
    print("\n%d passed, %d failed" % (len(tests) - failed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
