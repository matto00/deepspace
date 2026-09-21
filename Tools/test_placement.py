#!/usr/bin/env python3
"""
Tests for props and placement: rotation, stacking, containment, and derived
lights and mounts.

    python3 Tools/test_placement.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import props as P
from floorplan import FloorPlan, PlanError, Room
from placement import (Mount, Place, resolve_lights, resolve_mount,
                       resolve_props, LIGHT_SPACING)

ROOM = Room("r", 1000, 2000, 400, 300, 250)
PLAN = FloorPlan([ROOM])


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
    big = FloorPlan([Room("big", 0, 0, 3000, 3000, 500)])
    for name in P.PROPS:
        resolve_props(big, [Place(name, "big", (1500, 1500))])


def test_lights_follow_the_grid_spacing():
    lights, lamps = resolve_lights(PLAN)
    # 400 x 300 at 300 cm spacing: 2 x 1.
    assert len(lights) == 2 and len(lamps) == 2
    assert all(l.location[2] == ROOM.height - 20 for l in lights)
    assert LIGHT_SPACING == 300


def test_mount_sits_on_the_wall_facing_in():
    (x, y, z), yaw = resolve_mount(PLAN, Mount("r", "starboard", 120))
    assert yaw == 90 and z == 120
    assert x == ROOM.x + ROOM.w / 2.0
    assert ROOM.y < y < ROOM.y + ROOM.d          # inside the room, near the wall
    assert ROOM.y + ROOM.d - y < 20


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
