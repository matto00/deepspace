#!/usr/bin/env python3
"""
Tests for the floor plan generator, at the level where it is most likely to
be wrong: wall derivation, opening carving, and rectangle merging.

    python3 Tools/test_floorplan.py

Plain python3, no framework, so it runs anywhere the generator does.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from floorplan import (CELL, SLAB, LINTEL, Door, FloorPlan, PlanError, Room,
                       Seal, Window, merge_rectangles)


def walls(plan, role="wall"):
    return [b for b in plan.boxes() if b.role == role]


def solid_at(plan, x, y, z):
    """Is point (x, y, z) inside any emitted box?"""
    for b in plan.boxes():
        if all(abs((x, y, z)[k] - b.centre[k]) < b.size[k] / 2.0 for k in range(3)):
            return True
    return False


# Two 100 x 100 rooms side by side in x, one wall-thickness apart.
A = Room("a", 0, 0, 100, 100, 250)
B = Room("b", 110, 0, 100, 100, 250)


def test_shared_wall_is_emitted_once():
    plan = FloorPlan([A, B])
    # The shared wall is the column of cells at x = 100..110.
    shared = [c for c in plan.wall_height if c[0] == 10 and 0 <= c[1] < 10]
    assert len(shared) == 10, shared
    # Nothing is emitted twice: total wall-segment cell count equals the number
    # of distinct wall cells, because each cell has exactly one segment.
    covered = sum(b.size[0] * b.size[1] / CELL ** 2 for b in walls(plan))
    assert covered == len(plan.wall_height), (covered, len(plan.wall_height))


def test_outer_corners_are_filled():
    plan = FloorPlan([A])
    # Diagonal neighbour of the interior's corner. With 4-neighbour wall
    # detection this is left empty: a 10 cm notch at every corner.
    assert (-1, -1) in plan.wall_height
    assert solid_at(plan, -5, -5, 100)


def test_l_shaped_join_does_not_leave_a_gap():
    # An L: a corridor running +x, a room above its far end.
    corridor = Room("c", 0, 0, 300, 100, 250)
    room = Room("r", 200, 110, 100, 100, 250)
    plan = FloorPlan([corridor, room], doors=[Door("c", "r", 60, 200)])
    # The inside corner where the room's port wall meets the corridor's
    # starboard wall must be solid.
    assert solid_at(plan, 195, 105, 100)


def test_door_carves_its_width_and_height_and_leaves_a_lintel():
    plan = FloorPlan([A, B], doors=[Door("a", "b", 60, 200, centre=50)])
    # Inside the opening: air from floor to door height.
    assert not solid_at(plan, 105, 50, 100)
    assert not solid_at(plan, 105, 50, 195)
    # Just outside its width: still wall.
    assert solid_at(plan, 105, 15, 100)
    assert solid_at(plan, 105, 85, 100)
    # Above it: a trim lintel, then wall to the top.
    assert any(b.role == "trim" and b.centre[2] == 200 + LINTEL / 2.0 for b in plan.boxes())
    assert solid_at(plan, 105, 50, 240)
    # Below the floor: the threshold stays, so the two floors are joined.
    assert solid_at(plan, 105, 50, -5)


def test_window_leaves_sill_glass_and_lintel():
    plan = FloorPlan([A], windows=[Window("a", "fore", 60, 100, 180)])
    assert solid_at(plan, 105, 50, 50)                         # sill: wall
    glass = [b for b in plan.boxes() if b.role == "glass"]
    assert len(glass) == 1 and glass[0].centre[2] == 140 and glass[0].size[2] == 80
    assert any(b.role == "trim" and b.centre[2] == 180 + LINTEL / 2.0 for b in plan.boxes())


def test_seal_is_solid():
    plan = FloorPlan([A], seals=[Seal("a", "port", 60, 200)])
    assert any(b.role == "seal" for b in plan.boxes())
    assert solid_at(plan, 50, -5, 100)


def test_merge_tiles_exactly():
    # An irregular shape with a hole: merging must cover every cell exactly
    # once and nothing else.
    cells = {(i, j) for i in range(6) for j in range(5)} - {(2, 2), (3, 2), (5, 0)}
    rects = merge_rectangles(cells)
    covered = [(i, j) for (i0, j0, i1, j1) in rects
               for i in range(i0, i1) for j in range(j0, j1)]
    assert len(covered) == len(set(covered)), "a cell is covered twice"
    assert set(covered) == cells, "gap or overreach"


def test_merge_is_deterministic():
    cells = {(i, j) for i in range(7) for j in range(3)} - {(3, 1)}
    assert merge_rectangles(cells) == merge_rectangles(set(cells))


def test_door_between_rooms_without_shared_wall_is_rejected():
    far = Room("far", 500, 0, 100, 100, 250)
    try:
        FloorPlan([A, far], doors=[Door("a", "far", 60, 200)])
    except PlanError as e:
        assert "do not share a wall" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_door_taller_than_a_room_is_rejected():
    low = Room("low", 110, 0, 100, 100, 110)
    try:
        FloorPlan([A, low], doors=[Door("a", "low", 60, 200)])
    except PlanError as e:
        assert "taller than room 'low'" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_overlapping_rooms_are_rejected():
    try:
        FloorPlan([A, Room("b", 50, 50, 100, 100, 250)])
    except PlanError as e:
        assert "overlap" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_off_grid_room_is_rejected():
    try:
        FloorPlan([Room("a", 0, 0, 105, 100, 250)])
    except PlanError as e:
        assert "off the" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_window_on_an_interior_wall_is_rejected():
    try:
        FloorPlan([A, B], windows=[Window("a", "fore", 60, 100, 180)])
    except PlanError as e:
        assert "not an exterior wall" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_centred_opening_snaps_to_the_grid():
    # A 150 cm wall and a 60 cm door: exact centring puts the edges at 45 and
    # 105, off the grid. Omitting the centre must still succeed, on the grid.
    plan = FloorPlan([Room("a", 0, 0, 100, 150, 250), Room("b", 110, 0, 100, 150, 250)],
                     doors=[Door("a", "b", 60, 200)])
    door = [op for op in plan.openings if op[0] == "door"][0]
    ys = sorted(c[1] for c in door[1])
    assert len(ys) == 6 and ys[0] * CELL % CELL == 0
    assert abs((ys[0] * CELL + (ys[-1] + 1) * CELL) / 2.0 - 75) <= CELL / 2.0


def test_explicit_centre_off_the_grid_is_rejected():
    try:
        FloorPlan([A, B], doors=[Door("a", "b", 60, 200, centre=45)])
    except PlanError as e:
        assert "off the grid" in str(e)
    else:
        raise AssertionError("expected PlanError")


def test_explicit_centre_with_an_odd_cell_width_is_accepted():
    # 50 cm is five cells: centred at 75 its edges are 50 and 100, on the grid.
    plan = FloorPlan([Room("a", 0, 0, 100, 150, 250), Room("b", 110, 0, 100, 150, 250)],
                     doors=[Door("a", "b", 50, 200, centre=75)])
    door = [op for op in plan.openings if op[0] == "door"][0]
    ys = sorted(c[1] for c in door[1])
    assert ys[0] * CELL == 50 and (ys[-1] + 1) * CELL == 100, ys


def test_wall_takes_the_height_of_the_taller_room():
    tall = Room("tall", 110, 0, 100, 100, 500)
    plan = FloorPlan([A, tall])
    assert plan.wall_height[(10, 5)] == 500


def main():
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_")]
    failed = 0
    for name, fn in tests:
        try:
            fn()
            print("  ok    " + name)
        except Exception as e:                     # noqa: BLE001 -- report every failure
            failed += 1
            print("  FAIL  %s: %s: %s" % (name, type(e).__name__, e))
    print("\n%d passed, %d failed" % (len(tests) - failed, failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
