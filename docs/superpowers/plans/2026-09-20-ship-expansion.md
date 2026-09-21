# Ship Expansion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Grow the hauler from four grey rooms to eight furnished rooms with a clean retro-future look, described as a floor plan whose walls are derived, and validated for both soundness and design intent.

**Architecture:** A pure-Python pipeline under `Tools/` turns a floor plan (rooms, doors, windows, seals, prop placements) into axis-aligned boxes. The same boxes feed a voxel validator that runs without the editor, an in-editor builder that realises them with the Unreal Python API, and a verifier that measures the built level against them. Nothing in the layout is a raw world coordinate.

**Tech Stack:** Python 3 (bare, for generation and validation; the editor's bundled 3.11 for building), Unreal Engine 5.8 `PythonScriptPlugin`, the First Person template's `LevelPrototyping` meshes and `M_PrototypeGrid` material.

**Spec:** `docs/superpowers/specs/2026-09-20-ship-expansion-design.md`

## Global Constraints

- Units are **centimetres**; **X fore, Y starboard, Z up**; floor at **Z = 0**.
- The rasteriser grid, and the wall thickness, is **10 cm**. Room coordinates and sizes must be multiples of it.
- Rooms are given by **minimum corner and size**. Everything placed in a room is **relative to that room's minimum corner**. Emitted boxes are **centre and size**.
- **No `unreal` import** in `floorplan.py`, `props.py`, `placement.py`, `hauler_layout.py`, `validate_hauler.py`, or either test file. Only `build_hauler.py` and `verify_level.py` import `unreal`.
- **Never assume a mesh's pivot.** `SM_Cube` is corner-origin (0…100), `SM_ChamferCube` is centre-origin (−50…50), `SM_Cylinder` is base-centre (XY −50…50, Z 0…100). Read the bounding box.
- Prop rotation is **90° steps only**. No prop part may sit **below its prop's origin**.
- Movement contract: **standing clearance 180 cm**, **crouched clearance 90 cm**, **capsule radius 30 cm** (approximating the default 34).
- All gameplay logic stays in C++; this plan touches no C++. Blueprints are not edited by script.
- `unreal.log` does not reach stdout under the commandlet: editor-side scripts write results to `Saved/`.
- **Close the editor before running any editor-side script.** A commandlet writing to `L_Hauler` while the editor has it open will conflict.

## Deviations from the spec

Prototyping every file in this plan before writing it surfaced the following. Each is justified in the task that implements it, and Task 5 records them in the spec.

| Spec said | Plan does | Why |
|---|---|---|
| Room table with `y = −75`, `85`, `395`, `−335`, `−385` | Shifted to `−80`, `80`, `390`, `−340`, `−390` | The spec's own values broke its own 10 cm grid rule. The generator rejected them. Sizes and adjacency are unchanged. |
| Cargo door 250 cm wide | 150 cm, the corridor's full width | The corridor and cargo bay share only 150 cm of wall. |
| Crawlway doors 80 cm | 90 cm, the crawlway's full width | An 80 cm door centred in a 90 cm duct puts its edges off the grid. |
| Reactor floor-to-ceiling | 220 cm tall | The derived light grid hangs lamps over the engineering bay. |
| `Door(a, b, along, …)` | `Door(a, b, width, height, centre=None)` | `centre` is a world coordinate along the wall. Omitting it centres the opening, snapped to the grid. |
| Placement resolution in `floorplan.py` | Its own `placement.py` | Keeps each file to one responsibility. |
| `M_ShipSurface` built from Python node graphs; `M_PrototypeGrid` as fallback | `M_PrototypeGrid` instances are primary; a small generated `M_ShipEmissive` covers emissive roles | The template grid is already world-aligned with colour, panel-size and roughness parameters: exactly the seam behaviour the spec planned to hand-build, and the spec's named riskiest part. |
| Emissive ceiling panels in the material | Lamp panels as geometry, one per derived light | Simpler, and each visible fixture corresponds to an actual light. |
| — | Validator models capsule width | A point-sized player walks through a 10 cm crack. |
| — | Validator bounds reachability to each room's floor | **Bug inherited from milestone 1:** seeding the Player Start's whole column included the roof above it, and roofs connect, so every room read as reachable across the top of the ship. |

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `Tools/floorplan.py` | Rooms, doors, windows, seals → structural boxes. Rasterise, derive walls, carve openings, merge. | 1 |
| `Tools/test_floorplan.py` | Tests for the above. | 1 |
| `Tools/props.py` | Prop templates as parts; rotation. | 2 |
| `Tools/placement.py` | Room-relative placement → world boxes; derived lights, lamps, mounts. | 2 |
| `Tools/test_placement.py` | Tests for the above. | 2 |
| `Tools/hauler_layout.py` | **Rewritten.** The ship: floor plan, placements, regions, contract constants, `generate()`. | 3 |
| `Tools/validate_hauler.py` | **Rewritten.** Soundness and intent checks. | 3 |
| `Tools/build_hauler.py` | **Rewritten.** Materials by role, boxes by mesh, lights, console, Player Start. | 4 |
| `Tools/verify_level.py` | **Rewritten.** Built level vs generated boxes. | 4 |
| `docs/playtest-checklist.md` | Updated for eight rooms. | 5 |
| `CLAUDE.md`, ADR 0004, the spec | Record the new pipeline and the deviations. | 5 |

---

### Task 1: The floor plan generator

The core of the sub-project: turning rooms into walls. A wall is any cell bordering a room's interior that is not itself interior, which gets shared walls, corners and T-junctions right with no special cases.

**Files:**
- Create: `Tools/floorplan.py`
- Create: `Tools/test_floorplan.py`

**Interfaces:**
- Produces:
  - `CELL = 10`, `SLAB = 10`, `LINTEL = 20`
  - `Room(name, x, y, w, d, height)`
  - `Door(a, b, width, height, centre=None)`
  - `Window(room, side, width, sill, head, centre=None)` — `side` is one of `"fore" "aft" "port" "starboard"`
  - `Seal(room, side, width, height, centre=None)`
  - `Box(label, centre, size, role, mesh="cube")` — `centre`, `size` are `(x, y, z)` tuples in cm
  - `class PlanError(ValueError)`
  - `class FloorPlan(rooms, doors=(), windows=(), seals=())` with:
    - `.rooms: dict[str, Room]`, `.room(name) -> Room`
    - `.openings: list[(kind, cells, z0, z1)]` — `kind` in `"door" "window" "seal"`, `cells` a list of `(i, j)`
    - `.wall_height: dict[(i, j), int]`
    - `.boxes() -> list[Box]` — roles emitted: `floor ceiling wall trim glass seal`
  - `merge_rectangles(cells) -> list[(i0, j0, i1, j1)]`, half-open

- [ ] **Step 1: Write the failing tests**

Create `Tools/test_floorplan.py`:

```python
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 Tools/test_floorplan.py`
Expected: `ModuleNotFoundError: No module named 'floorplan'`

- [ ] **Step 3: Write the generator**

Create `Tools/floorplan.py`:

```python
"""
Turns a floor plan -- rooms, doors, windows, seals -- into boxes.

Rooms are the design; walls are derived. The plan is rasterised onto a 10 cm
grid, and a wall is any cell bordering a room's interior that is not itself
interior. That single rule gets shared walls, corners and T-junctions right with
no special cases: two rooms one wall-thickness apart share exactly one wall, and
nothing is emitted twice. Computing wall segments per room edge and
de-duplicating them is the approach that gave milestone 1 its hull leak.

No `unreal` import: this runs in bare python3, so the generator can be tested
and the layout validated in about a second without starting the editor.

Conventions: centimetres; X fore, Y starboard, Z up; floor at Z = 0. Rooms are
given by minimum corner and size, like a floor plan. Emitted boxes are given by
centre and size, which is what the builder and validators consume.
"""

from collections import defaultdict, namedtuple

CELL = 10            # cm; the rasteriser's grid, and the wall thickness
SLAB = 10            # cm; floor and ceiling thickness
LINTEL = 20          # cm; the trim band over each opening

SIDES = ("fore", "aft", "port", "starboard")

Room = namedtuple("Room", "name x y w d height")
# centre: world coordinate of the opening's centre along the wall it sits in.
# None centres it on the wall (for a door, on the stretch the two rooms share).
Door = namedtuple("Door", "a b width height centre", defaults=(None,))
Window = namedtuple("Window", "room side width sill head centre", defaults=(None,))
Seal = namedtuple("Seal", "room side width height centre", defaults=(None,))

# label: stable, unique name. centre and size: (x, y, z) in cm. role: which
# material it wears -- the generator decides, the builder obeys.
Box = namedtuple("Box", "label centre size role mesh", defaults=("cube",))


class PlanError(ValueError):
    """The floor plan is not self-consistent. Raised before any geometry."""


def _cells(lo, length):
    return range(lo // CELL, (lo + length) // CELL)


def _on_grid(*values):
    return all(v % CELL == 0 for v in values)


class FloorPlan:
    def __init__(self, rooms, doors=(), windows=(), seals=()):
        self.rooms = {r.name: r for r in rooms}
        if len(self.rooms) != len(rooms):
            raise PlanError("duplicate room name")
        self.doors, self.windows, self.seals = list(doors), list(windows), list(seals)

        self.owner = {}                         # (i, j) -> room name, interiors only
        for room in rooms:
            if not _on_grid(room.x, room.y, room.w, room.d, room.height):
                raise PlanError("room '%s' is off the %d cm grid" % (room.name, CELL))
            for i in _cells(room.x, room.w):
                for j in _cells(room.y, room.d):
                    if (i, j) in self.owner:
                        raise PlanError("rooms '%s' and '%s' overlap"
                                        % (self.owner[(i, j)], room.name))
                    self.owner[(i, j)] = room.name

        # A wall cell borders an interior on any of its eight sides. Eight, not
        # four: with four, every outer corner is left as a missing 10 cm column.
        self.wall_height = {}                   # (i, j) -> tallest bordering room
        for (i, j), name in self.owner.items():
            h = self.rooms[name].height
            for di in (-1, 0, 1):
                for dj in (-1, 0, 1):
                    c = (i + di, j + dj)
                    if c not in self.owner:
                        self.wall_height[c] = max(self.wall_height.get(c, 0), h)

        # Each wall cell's vertical column, as (z0, z1, role) segments. Openings
        # replace a cell's default single segment.
        self.columns = {c: [(-SLAB, h + SLAB, "wall")] for c, h in self.wall_height.items()}
        self.openings = []                      # (kind, cells, z0, z1) for keep-clear etc.
        for door in self.doors:
            self._carve_door(door)
        for window in self.windows:
            self._carve_exterior(window, "window")
        for seal in self.seals:
            self._carve_exterior(seal, "seal")

    # -- openings ---------------------------------------------------------

    def _shared_wall(self, a, b):
        """Cells of the single wall separating rooms a and b, and its axis."""
        if a not in self.rooms or b not in self.rooms:
            raise PlanError("door names unknown room: %s / %s" % (a, b))
        cells = []
        for (i, j) in self.wall_height:
            for (di, dj) in ((1, 0), (0, 1)):
                pair = {self.owner.get((i - di, j - dj)), self.owner.get((i + di, j + dj))}
                if pair == {a, b}:
                    cells.append(((i, j), "y" if di else "x"))
        axes = {axis for _, axis in cells}
        if not cells or len(axes) != 1:
            raise PlanError("rooms '%s' and '%s' do not share a wall" % (a, b))
        # A wall running along x separates rooms stacked in y, and vice versa.
        return [c for c, _ in cells], axes.pop()

    def _span(self, cells, axis, width, centre, what):
        """The cells of `cells` an opening of `width` at `centre` covers."""
        k = 0 if axis == "x" else 1
        coords = sorted(c[k] for c in cells)
        lo, hi = coords[0] * CELL, (coords[-1] + 1) * CELL
        if not _on_grid(width):
            raise PlanError("%s: width %d is off the grid" % (what, width))
        if centre is None:
            # Centred, to the nearest cell. Exact centring is only possible
            # when (wall - width) is a multiple of twice the grid, and a
            # designer asking for "centred" should not have to arrange that.
            start = lo + ((hi - lo - width) // 2 // CELL) * CELL
        else:
            start = centre - width // 2
            if not _on_grid(start) or width % (2 * CELL):
                raise PlanError("%s: width %d at centre %d puts its edges off the grid"
                                % (what, width, centre))
        end = start + width
        if start < lo or end > hi:
            raise PlanError("%s: %d..%d falls outside its wall %d..%d" % (what, start, end, lo, hi))
        return [c for c in cells if start <= c[k] * CELL < end]

    def _set(self, cells, segments):
        for c in cells:
            top = self.wall_height[c] + SLAB
            self.columns[c] = [(z0, min(z1, top), role)
                               for z0, z1, role in segments if z0 < top]

    def _lintel_and_above(self, z):
        return [(z, z + LINTEL, "trim"), (z + LINTEL, 10 ** 6, "wall")]

    def _carve_door(self, door):
        cells, axis = self._shared_wall(door.a, door.b)
        what = "door %s-%s" % (door.a, door.b)
        for name in (door.a, door.b):
            if door.height > self.rooms[name].height:
                raise PlanError("%s: %d cm is taller than room '%s'" % (what, door.height, name))
        span = self._span(cells, axis, door.width, door.centre, what)
        # The below-floor part stays: it is the threshold between the floors.
        self._set(span, [(-SLAB, 0, "wall")] + self._lintel_and_above(door.height))
        self.openings.append(("door", span, 0, door.height))

    def _exterior_cells(self, room, side):
        r = self.rooms[room]
        i0, i1 = r.x // CELL, (r.x + r.w) // CELL
        j0, j1 = r.y // CELL, (r.y + r.d) // CELL
        if side == "fore":
            cells, axis = [(i1, j) for j in range(j0, j1)], "y"
        elif side == "aft":
            cells, axis = [(i0 - 1, j) for j in range(j0, j1)], "y"
        elif side == "starboard":
            cells, axis = [(i, j1) for i in range(i0, i1)], "x"
        elif side == "port":
            cells, axis = [(i, j0 - 1) for i in range(i0, i1)], "x"
        else:
            raise PlanError("unknown side '%s'" % side)
        return cells, axis

    def _carve_exterior(self, opening, kind):
        what = "%s on %s %s" % (kind, opening.room, opening.side)
        if opening.room not in self.rooms:
            raise PlanError("%s: unknown room" % what)
        cells, axis = self._exterior_cells(opening.room, opening.side)
        # Exterior means nothing lies beyond: the cell past the wall is no room.
        di, dj = {"fore": (1, 0), "aft": (-1, 0), "starboard": (0, 1), "port": (0, -1)}[opening.side]
        span = self._span(cells, axis, opening.width, opening.centre, what)
        for (i, j) in span:
            if (i + di, j + dj) in self.owner:
                raise PlanError("%s: not an exterior wall" % what)
        if kind == "window":
            if not 0 < opening.sill < opening.head <= self.rooms[opening.room].height:
                raise PlanError("%s: sill/head out of range" % what)
            self._set(span, [(-SLAB, opening.sill, "wall"), (opening.sill, opening.head, "glass")]
                      + self._lintel_and_above(opening.head))
            self.openings.append(("window", span, opening.sill, opening.head))
        else:
            if opening.height > self.rooms[opening.room].height:
                raise PlanError("%s: taller than its room" % what)
            self._set(span, [(-SLAB, 0, "wall"), (0, opening.height, "seal")]
                      + self._lintel_and_above(opening.height))
            self.openings.append(("seal", span, 0, opening.height))

    # -- output -----------------------------------------------------------

    def boxes(self):
        """Every structural box: floors, ceilings, and merged wall segments."""
        out = []
        for name in sorted(self.rooms):
            r = self.rooms[name]
            cx, cy = r.x + r.w / 2.0, r.y + r.d / 2.0
            out.append(Box("floor_" + name, (cx, cy, -SLAB / 2.0), (r.w, r.d, SLAB), "floor"))
            out.append(Box("ceiling_" + name, (cx, cy, r.height + SLAB / 2.0),
                           (r.w, r.d, SLAB), "ceiling"))

        by_segment = defaultdict(set)
        for cell, segments in self.columns.items():
            for seg in segments:
                if seg[1] > seg[0]:
                    by_segment[seg].add(cell)

        for (z0, z1, role) in sorted(by_segment):
            for n, (i0, j0, i1, j1) in enumerate(merge_rectangles(by_segment[(z0, z1, role)])):
                out.append(Box(
                    "%s_%d_%d_%d" % (role, z0, z1, n),
                    ((i0 + i1) * CELL / 2.0, (j0 + j1) * CELL / 2.0, (z0 + z1) / 2.0),
                    ((i1 - i0) * CELL, (j1 - j0) * CELL, z1 - z0),
                    role))
        return out

    def room(self, name):
        return self.rooms[name]


def merge_rectangles(cells):
    """Greedily pack a set of (i, j) cells into rectangles (i0, j0, i1, j1),
    half-open, that tile the set exactly: every cell covered once, no cell
    outside the set covered. Deterministic, so labels are stable across runs."""
    remaining = set(cells)
    out = []
    for (i, j) in sorted(cells):
        if (i, j) not in remaining:
            continue
        i1 = i
        while (i1 + 1, j) in remaining:
            i1 += 1
        j1 = j
        while all((k, j1 + 1) in remaining for k in range(i, i1 + 1)):
            j1 += 1
        for k in range(i, i1 + 1):
            for m in range(j, j1 + 1):
                remaining.discard((k, m))
        out.append((i, j, i1 + 1, j1 + 1))
    return out
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 Tools/test_floorplan.py`
Expected: `16 passed, 0 failed`

- [ ] **Step 5: Confirm the tests can fail**

A test suite that has never been seen failing proves little. Break the two properties that matter most and confirm each is caught, restoring after each:

```bash
cp Tools/floorplan.py /tmp/fp.bak
# 4-neighbour wall detection leaves a notch at every outer corner.
python3 - <<'EOF'
p = 'Tools/floorplan.py'; s = open(p).read()
s = s.replace("""            for di in (-1, 0, 1):
                for dj in (-1, 0, 1):
                    c = (i + di, j + dj)""", """            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    c = (i + di, j + dj)""")
open(p, 'w').write(s)
EOF
python3 Tools/test_floorplan.py | grep FAIL      # expect: test_outer_corners_are_filled
cp /tmp/fp.bak Tools/floorplan.py
# A merge that overreaches.
sed -i 's|while all((k, j1 + 1) in remaining for k in range(i, i1 + 1)):|while (i, j1 + 1) in remaining:|' Tools/floorplan.py
python3 Tools/test_floorplan.py | grep FAIL      # expect: test_merge_tiles_exactly among others
cp /tmp/fp.bak Tools/floorplan.py
python3 Tools/test_floorplan.py | tail -1       # expect: 16 passed, 0 failed
```

- [ ] **Step 6: Commit**

```bash
git add Tools/floorplan.py Tools/test_floorplan.py
git commit -m "feat: floor plan generator that derives walls from rooms"
```

---

### Task 2: Props and placement

Furniture as templates of primitive parts, and resolution of everything room-relative — props, lights, lamp panels, wall mounts — into world space.

**Files:**
- Create: `Tools/props.py`
- Create: `Tools/placement.py`
- Create: `Tools/test_placement.py`

**Interfaces:**
- Consumes: `Box`, `PlanError`, `FloorPlan`, `Room` from Task 1.
- Produces:
  - `props.Part(mesh, at, size, role)`; `props.MESHES = ("cube", "chamfer", "cylinder")`
  - `props.PROPS: dict[str, list[Part]]` — names used by Task 3: `pilot_seat cockpit_desk overhead_panel container_large container_small wall_rack reactor pipe_run workbench galley_table bench counter bed locker desk suit_locker airlock_bench conduit`
  - `props.rotate(at, size, facing) -> (at, size)`; `props.height(name) -> float`
  - `placement.Place(prop, room, at, facing=0, level=0, elevation=0)`
  - `placement.Region(name, room, at, posture)` — `posture` is `"stand"` or `"crouch"`
  - `placement.Mount(room, side, z)`
  - `placement.Light(label, location, intensity, radius)`
  - `placement.resolve_props(plan, placements) -> list[Box]` — labels `prop_<name>_<n>_<k>`
  - `placement.resolve_lights(plan) -> (list[Light], list[Box])` — lamp boxes have role `lamp`
  - `placement.resolve_mount(plan, mount) -> ((x, y, z), yaw)` — face points −X at yaw 0
  - `placement.resolve_point(plan, room, at, z=0) -> (x, y, z)`
  - `LIGHT_SPACING = 300`

- [ ] **Step 1: Write the failing tests**

Create `Tools/test_placement.py`:

```python
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
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 Tools/test_placement.py`
Expected: `ModuleNotFoundError: No module named 'props'`

- [ ] **Step 3: Write the prop templates**

Create `Tools/props.py`. The overhead panel's glowing strip is its *lowest* part and sits at the origin; an earlier draft hung it below the origin, which only worked because the panel is always lifted to the ceiling, and `test_every_template_prop_fits_a_generous_room` caught it.

```python
"""
Stand-in furniture, as templates of primitive parts.

A prop is a list of parts. Each part names a mesh, its centre relative to the
prop's origin (on the floor, at the prop's footprint centre), its size in
centimetres, and the material role it wears. Sizes, not scales: the builder
reads every mesh's bounding box, because the three meshes used here disagree
about where their origin is (SM_Cube at a corner, SM_ChamferCube at its centre,
SM_Cylinder at its base).

No part may sit below its prop's origin: a prop's lowest point is the floor it
stands on, or the height it is lifted to. A prop faces +X at facing 0. Placement rotates it in 90-degree steps only, so
every part stays axis-aligned and the validator can voxelise it exactly
(cylinders by their bounding box, a deliberate over-approximation).

No `unreal` import.
"""

from collections import namedtuple

Part = namedtuple("Part", "mesh at size role")

MESHES = ("cube", "chamfer", "cylinder")

PROPS = {
    # -- cockpit ---------------------------------------------------------
    "pilot_seat": [
        Part("cylinder", (0, 0, 20), (40, 40, 40), "furniture"),
        Part("chamfer", (0, 0, 47.5), (60, 60, 15), "furniture"),
        Part("chamfer", (-22.5, 0, 90), (15, 60, 70), "furniture"),
    ],
    # A desk across the cockpit with a wing at each end, and three screens
    # along its far edge facing the pilots.
    "cockpit_desk": [
        Part("chamfer", (0, 0, 40), (60, 260, 80), "furniture"),
        Part("chamfer", (-50, -155, 40), (100, 50, 80), "furniture"),
        Part("chamfer", (-50, 155, 40), (100, 50, 80), "furniture"),
        Part("cube", (20, -85, 105), (6, 70, 50), "screen"),
        Part("cube", (20, 0, 105), (6, 70, 50), "screen"),
        Part("cube", (20, 85, 105), (6, 70, 50), "screen"),
    ],
    # Hung from the ceiling: placed with an elevation of (room height - 22).
    # The glowing strip is its underside, so it is the part at the origin.
    "overhead_panel": [
        Part("cube", (0, 0, 1), (60, 160, 2), "accent"),
        Part("chamfer", (0, 0, 12), (80, 200, 20), "furniture"),
    ],

    # -- cargo bay -------------------------------------------------------
    "container_large": [
        Part("chamfer", (0, 0, 60), (240, 120, 120), "furniture"),
        Part("cube", (0, 0, 60), (244, 124, 10), "accent"),
    ],
    "container_small": [
        Part("chamfer", (0, 0, 60), (120, 120, 120), "furniture"),
        Part("cube", (0, 0, 60), (124, 124, 10), "accent"),
    ],
    "wall_rack": [
        Part("cube", (0, -95, 150), (50, 10, 300), "furniture"),
        Part("cube", (0, 95, 150), (50, 10, 300), "furniture"),
        Part("cube", (0, 0, 75), (50, 180, 5), "furniture"),
        Part("cube", (0, 0, 150), (50, 180, 5), "furniture"),
        Part("cube", (0, 0, 225), (50, 180, 5), "furniture"),
    ],

    # -- engineering -----------------------------------------------------
    # Stops short of the ceiling: the derived lights hang above it.
    "reactor": [
        Part("cylinder", (0, 0, 10), (160, 160, 20), "furniture"),
        Part("cylinder", (0, 0, 120), (120, 120, 200), "furniture"),
        Part("cylinder", (0, 0, 150), (126, 126, 20), "accent"),
    ],
    # Two floor-to-ceiling pipes joined by a conduit, flush to a wall. Its
    # length runs along +Y at facing 0.
    "pipe_run": [
        Part("cylinder", (7.5, 0, 125), (15, 15, 250), "furniture"),
        Part("cylinder", (7.5, 200, 125), (15, 15, 250), "furniture"),
        Part("chamfer", (7.5, 100, 210), (15, 200, 15), "furniture"),
    ],
    "workbench": [
        Part("cube", (-35, -75, 41), (6, 6, 82), "furniture"),
        Part("cube", (35, -75, 41), (6, 6, 82), "furniture"),
        Part("cube", (-35, 75, 41), (6, 6, 82), "furniture"),
        Part("cube", (35, 75, 41), (6, 6, 82), "furniture"),
        Part("chamfer", (0, 0, 86), (80, 160, 8), "furniture"),
        Part("cube", (-38, 0, 107.5), (4, 50, 35), "screen"),
    ],

    # -- galley ----------------------------------------------------------
    "galley_table": [
        Part("cylinder", (0, 0, 35.5), (20, 20, 71), "furniture"),
        Part("chamfer", (0, 0, 75), (90, 180, 8), "furniture"),
    ],
    "bench": [
        Part("chamfer", (0, 0, 22.5), (40, 180, 45), "furniture"),
    ],
    # Base cabinets with wall-hung upper cabinets; the uppers touch the wall
    # the counter is placed against.
    "counter": [
        Part("chamfer", (0, 0, 45), (60, 300, 90), "furniture"),
        Part("chamfer", (-12.5, 0, 185), (35, 300, 70), "furniture"),
    ],

    # -- bunk ------------------------------------------------------------
    "bed": [
        Part("chamfer", (0, 0, 15), (210, 100, 30), "furniture"),
        Part("chamfer", (0, 0, 40), (200, 90, 20), "furniture"),
        Part("chamfer", (-110, 0, 60), (10, 100, 60), "furniture"),
        Part("chamfer", (-80, 0, 56), (40, 60, 12), "furniture"),
    ],
    "locker": [
        Part("chamfer", (0, 0, 100), (60, 60, 200), "furniture"),
        Part("cube", (31, 20, 110), (2, 4, 40), "accent"),
    ],
    "desk": [
        Part("chamfer", (0, -40, 35.5), (55, 40, 71), "furniture"),
        Part("chamfer", (0, 0, 74), (60, 120, 6), "furniture"),
    ],

    # -- airlock ---------------------------------------------------------
    "suit_locker": [
        Part("chamfer", (0, 0, 105), (70, 80, 210), "furniture"),
        Part("cube", (36, 0, 130), (2, 50, 100), "screen"),
    ],
    "airlock_bench": [
        Part("chamfer", (0, 0, 22.5), (40, 120, 45), "furniture"),
    ],

    # -- corridor --------------------------------------------------------
    # Just under the ceiling, above every opening including the 240 cm cargo
    # door, so it neither crosses a doorway nor intrudes on the slide run.
    "conduit": [
        Part("chamfer", (700, 0, 244), (1400, 8, 8), "furniture"),
    ],
}


def rotate(at, size, facing):
    """A part's centre and size after turning the prop by `facing` degrees
    anticlockwise seen from above. Only 90-degree steps are allowed."""
    (x, y, z), (sx, sy, sz) = at, size
    if facing == 0:
        return (x, y, z), (sx, sy, sz)
    if facing == 90:
        return (-y, x, z), (sy, sx, sz)
    if facing == 180:
        return (-x, -y, z), (sx, sy, sz)
    if facing == 270:
        return (y, -x, z), (sy, sx, sz)
    raise ValueError("facing must be 0, 90, 180 or 270, not %r" % (facing,))


def height(name):
    """Top of a prop above its origin; what `level` stacks by."""
    return max(p.at[2] + p.size[2] / 2.0 for p in PROPS[name])
```

- [ ] **Step 4: Write placement resolution**

Create `Tools/placement.py`:

```python
"""
Resolves everything that sits in a room -- furniture, lights, the console, the
Player Start, named regions -- from room-relative terms into world positions.

Nothing in the layout is a raw world coordinate. Every hand-computed coordinate
is one that can silently disagree with the surface it belongs on; milestone 1's
pivot bug put every wall half its own size away from its floor. Positions are
given relative to a room's minimum corner instead, and move with the room.

No `unreal` import.
"""

import math
from collections import namedtuple

import props as P
from floorplan import Box, PlanError

# at: (x, y) of the prop's origin from the room's minimum corner.
# level: stack on n copies of itself. elevation: lift off the floor, in cm.
Place = namedtuple("Place", "prop room at facing level elevation", defaults=(0, 0, 0))

# A point a player must be able to reach, in a posture: "stand" or "crouch".
Region = namedtuple("Region", "name room at posture")

# A wall-mounted fixture: which wall, and how high its centre sits.
Mount = namedtuple("Mount", "room side z")

Light = namedtuple("Light", "label location intensity radius")

LIGHT_SPACING = 300     # cm; rooms get ceil(size / spacing) lights per axis
LAMP_SIZE = 60          # cm; the emissive ceiling panel under each light
LAMP_DEPTH = 2
PANEL_DEPTH = 8         # cm; how far a wall-mounted panel's centre sits off the wall


def _world(room, at):
    return room.x + at[0], room.y + at[1]


def resolve_props(plan, placements):
    """Every prop part as a Box in world space. Raises PlanError if a part
    leaves its room: through a wall, or through the ceiling."""
    out = []
    counts = {}
    for place in placements:
        if place.prop not in P.PROPS:
            raise PlanError("placement names unknown prop '%s'" % place.prop)
        if place.room not in plan.rooms:
            raise PlanError("placement of '%s' names unknown room '%s'" % (place.prop, place.room))
        room = plan.room(place.room)
        ox, oy = _world(room, place.at)
        oz = place.elevation + place.level * P.height(place.prop)
        n = counts.get(place.prop, 0)
        counts[place.prop] = n + 1

        for k, part in enumerate(P.PROPS[place.prop]):
            if part.mesh not in P.MESHES:
                raise PlanError("prop '%s' uses unknown mesh '%s'" % (place.prop, part.mesh))
            (px, py, pz), size = P.rotate(part.at, part.size, place.facing)
            centre = (ox + px, oy + py, oz + pz)
            lo = [centre[a] - size[a] / 2.0 for a in range(3)]
            hi = [centre[a] + size[a] / 2.0 for a in range(3)]
            eps = 0.01
            if (lo[0] < room.x - eps or hi[0] > room.x + room.w + eps
                    or lo[1] < room.y - eps or hi[1] > room.y + room.d + eps
                    or lo[2] < -eps or hi[2] > room.height + eps):
                raise PlanError("%s #%d part %d leaves room '%s'"
                                % (place.prop, n, k, place.room))
            out.append(Box("prop_%s_%d_%d" % (place.prop, n, k), centre, size,
                           part.role, part.mesh))
    return out


def resolve_lights(plan):
    """A light on a grid in every room, each with an emissive panel on the
    ceiling above it, so the ship reads as lit by fixtures rather than by
    nothing. Returns (lights, lamp boxes)."""
    lights, lamps = [], []
    for name in sorted(plan.rooms):
        r = plan.room(name)
        nx = max(1, math.ceil(r.w / float(LIGHT_SPACING)))
        ny = max(1, math.ceil(r.d / float(LIGHT_SPACING)))
        for i in range(nx):
            for j in range(ny):
                x = r.x + (i + 0.5) * r.w / nx
                y = r.y + (j + 0.5) * r.d / ny
                label = "%s_%d_%d" % (name, i, j)
                # Tall rooms get stronger, wider lights to reach the floor.
                reach = max(450.0, r.height * 1.6)
                lights.append(Light("light_" + label, (x, y, r.height - 20),
                                    3.0 * r.height / 250.0, reach))
                lamps.append(Box("lamp_" + label, (x, y, r.height - LAMP_DEPTH / 2.0),
                                 (LAMP_SIZE, LAMP_SIZE, LAMP_DEPTH), "lamp"))
    return lights, lamps


def resolve_mount(plan, mount):
    """World location and yaw for a panel fixed to a wall, facing into the
    room. The panel's face points -X at yaw 0, as AShipConsole's does."""
    r = plan.room(mount.room)
    cx, cy = r.x + r.w / 2.0, r.y + r.d / 2.0
    if mount.side == "starboard":
        return (cx, r.y + r.d - PANEL_DEPTH, mount.z), 90
    if mount.side == "port":
        return (cx, r.y + PANEL_DEPTH, mount.z), 270
    if mount.side == "fore":
        return (r.x + r.w - PANEL_DEPTH, cy, mount.z), 0
    if mount.side == "aft":
        return (r.x + PANEL_DEPTH, cy, mount.z), 180
    raise PlanError("unknown side '%s'" % mount.side)


def resolve_point(plan, room, at, z=0):
    r = plan.room(room)
    x, y = _world(r, at)
    return (x, y, z)
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python3 Tools/test_placement.py && python3 Tools/test_floorplan.py`
Expected: `11 passed, 0 failed` then `16 passed, 0 failed`

- [ ] **Step 6: Commit**

```bash
git add Tools/props.py Tools/placement.py Tools/test_placement.py
git commit -m "feat: prop templates and room-relative placement"
```

---

### Task 3: The ship and its validator

Replaces the hand-listed box layout with the eight-room floor plan, and replaces the validator with one that checks design intent as well as soundness. These change together because the old validator reads the old layout's `BOXES`; splitting them would leave `validate_hauler.py` broken between commits.

`build_hauler.py` and `verify_level.py` still read the old layout until Task 4. **Do not run them between Tasks 3 and 4.**

**Files:**
- Modify (full rewrite): `Tools/hauler_layout.py`
- Modify (full rewrite): `Tools/validate_hauler.py`

**Interfaces:**
- Consumes: everything from Tasks 1 and 2.
- Produces:
  - `hauler_layout.STAND_CLEARANCE = 180`, `CROUCH_CLEARANCE = 90`, `CAPSULE_RADIUS = 30`, `KEEP_CLEAR = 100`, `SLIDE_RUN = 1200`, `SLIDE_ROOM = "corridor"`
  - `hauler_layout.ROOMS`, `DOORS`, `WINDOWS`, `SEALS`, `PLACEMENTS`, `REGIONS`, `CONSOLE`, `PLAYER_START`
  - `hauler_layout.Ship(plan, boxes, lights, console_location, console_yaw, player_start, regions, keep_clear)`
  - `hauler_layout.generate() -> Ship` — raises `PlanError`. `regions` is `[(name, (x, y, z), posture)]`; `keep_clear` is `[(name, lo, hi)]`

- [ ] **Step 1: Replace the layout**

Replace the whole of `Tools/hauler_layout.py`:

```python
"""
The hauler, as a floor plan.

This is the source of truth for the ship. Rooms are the design; walls, floors,
ceilings, lintels, lights and lamp panels are derived from them by
`floorplan.py` and `placement.py`. Change a room here and re-run; never nudge
built actors in the editor.

Conventions: centimetres; X fore, Y starboard, Z up; floor at Z = 0. Rooms are
(x, y) minimum corner plus (w, d) size. Everything placed in a room is given
relative to that room's minimum corner.

No `unreal` import: `python3 Tools/validate_hauler.py` runs in about a second.
"""

import os
import sys
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from floorplan import Door, FloorPlan, Room, Seal, Window, CELL
from placement import (Mount, Place, Region, resolve_lights, resolve_mount,
                       resolve_point, resolve_props)

# -- The contract with the character -------------------------------------
# The ship is built for these. The character's standing capsule must be
# shorter than STAND_CLEARANCE, and its crouched capsule shorter than
# CROUCH_CLEARANCE and than the crawlway's doors, or the crawlway becomes
# impassable while this layout validates as fine.
STAND_CLEARANCE = 180
CROUCH_CLEARANCE = 90
CAPSULE_RADIUS = 30     # approximates the default 34 cm capsule on a 10 cm grid

KEEP_CLEAR = 100        # cm in front of every door, both sides, and the console
SLIDE_RUN = 1200        # cm of clear straight corridor, for sliding

# Every value is a multiple of the 10 cm grid. Rooms that share a wall sit
# exactly one cell apart: the corridor ends at y = 70, engineering starts at 80.
ROOMS = [
    Room("corridor",    0,    -80,  1400, 150, 250),
    Room("cockpit",     1410, -200, 350,  400, 250),
    Room("cargo_bay",   -810, -400, 800,  900, 500),
    Room("engineering", 400,  80,   400,  400, 250),
    Room("galley",      810,  80,   490,  400, 250),
    Room("crawlway",    0,    390,  390,  90,  110),
    Room("airlock",     100,  -340, 250,  250, 250),
    Room("bunk",        600,  -390, 400,  300, 250),
]

DOORS = [
    Door("corridor", "cockpit", 150, 220),
    # The corridor and cargo bay share only the corridor's 150 cm of wall, so
    # the cargo door is the corridor's full width.
    Door("corridor", "cargo_bay", 150, 240),
    Door("corridor", "engineering", 120, 220),
    Door("corridor", "galley", 150, 220),
    # Off-centre, so the suit lockers on the airlock's aft wall stay clear of it.
    Door("corridor", "airlock", 120, 220, centre=260),
    Door("corridor", "bunk", 120, 220),
    # The crawlway's full 90 cm width: the open end of a service duct.
    Door("cargo_bay", "crawlway", 90, 100),
    Door("crawlway", "engineering", 90, 100),
]

WINDOWS = [
    Window("cockpit", "fore", 300, 100, 180),
    Window("galley", "starboard", 200, 100, 170),
]

SEALS = [
    Seal("airlock", "port", 120, 220),
]

PLACEMENTS = [
    # Cockpit: pilots face fore, toward the window.
    Place("cockpit_desk",   "cockpit", (285, 200)),
    Place("pilot_seat",     "cockpit", (175, 130)),
    Place("pilot_seat",     "cockpit", (175, 270)),
    Place("overhead_panel", "cockpit", (175, 200), elevation=228),

    # Cargo bay: containers along port and starboard, some two high.
    Place("container_large", "cargo_bay", (150, 100)),
    Place("container_large", "cargo_bay", (150, 100), level=1),
    Place("container_large", "cargo_bay", (150, 250)),
    Place("container_small", "cargo_bay", (450, 100)),
    Place("container_small", "cargo_bay", (450, 100), level=1),
    Place("container_large", "cargo_bay", (150, 750)),
    Place("container_large", "cargo_bay", (150, 750), level=1),
    Place("container_small", "cargo_bay", (400, 800)),
    Place("wall_rack",       "cargo_bay", (650, 25), facing=90),

    # Engineering: the reactor at the centre, work along the walls.
    Place("reactor",   "engineering", (200, 200)),
    Place("pipe_run",  "engineering", (0, 60)),
    Place("workbench", "engineering", (360, 100), facing=180),

    # Galley.
    Place("counter",      "galley", (30, 200)),
    Place("galley_table", "galley", (300, 230), facing=90),
    Place("bench",        "galley", (300, 160), facing=90),
    Place("bench",        "galley", (300, 300), facing=90),

    # Bunk.
    Place("bed",    "bunk", (130, 60)),
    Place("locker", "bunk", (360, 40), facing=90),
    Place("desk",   "bunk", (330, 230), facing=180),

    # Airlock.
    Place("suit_locker",   "airlock", (35, 45)),
    Place("suit_locker",   "airlock", (35, 125)),
    Place("airlock_bench", "airlock", (230, 70)),

    # Corridor: only conduit, high on the starboard wall.
    Place("conduit", "corridor", (0, 146)),
]

REGIONS = [
    Region("corridor_aft",  "corridor",    (100, 75),  "stand"),
    Region("corridor_fore", "corridor",    (1300, 75), "stand"),
    Region("cockpit",       "cockpit",     (60, 200),  "stand"),
    Region("cargo_bay",     "cargo_bay",   (500, 400), "stand"),
    Region("engineering",   "engineering", (80, 150),  "stand"),
    Region("console_front", "engineering", (200, 340), "stand"),
    Region("galley",        "galley",      (420, 120), "stand"),
    Region("bunk",          "bunk",        (200, 180), "stand"),
    Region("airlock",       "airlock",     (125, 60),  "stand"),
    Region("crawlway",      "crawlway",    (195, 45),  "crouch"),
]

CONSOLE = Mount("engineering", "starboard", 120)
CONSOLE_WIDTH = 100

# The game opens with waking aboard your ship.
PLAYER_START = ("bunk", (200, 150), 100)

SLIDE_ROOM = "corridor"


Ship = namedtuple("Ship", "plan boxes lights console_location console_yaw "
                          "player_start regions keep_clear")


def generate():
    """The whole ship, resolved: every box, light and fixed point in world
    space. Raises floorplan.PlanError if the plan is not self-consistent."""
    plan = FloorPlan(ROOMS, DOORS, WINDOWS, SEALS)
    lights, lamps = resolve_lights(plan)
    boxes = plan.boxes() + lamps + resolve_props(plan, PLACEMENTS)

    console_location, console_yaw = resolve_mount(plan, CONSOLE)
    room, at, z = PLAYER_START
    player_start = resolve_point(plan, room, at, z)

    regions = [(r.name, resolve_point(plan, r.room, r.at), r.posture) for r in REGIONS]

    # Keep-clear zones: world-space (lo, hi) boxes no prop may enter.
    keep_clear = []
    for kind, cells, z0, z1 in plan.openings:
        if kind != "door":
            continue
        i_values = {c[0] for c in cells}
        j_values = {c[1] for c in cells}
        x0, x1 = min(i_values) * CELL, (max(i_values) + 1) * CELL
        y0, y1 = min(j_values) * CELL, (max(j_values) + 1) * CELL
        if len(i_values) == 1:              # wall runs along Y: clear in X
            x0, x1 = x0 - KEEP_CLEAR, x1 + KEEP_CLEAR
        else:                               # wall runs along X: clear in Y
            y0, y1 = y0 - KEEP_CLEAR, y1 + KEEP_CLEAR
        keep_clear.append(("door at (%d, %d)" % ((x0 + x1) / 2, (y0 + y1) / 2),
                           (x0, y0, z0), (x1, y1, z1)))

    cx, cy, _ = console_location
    half = CONSOLE_WIDTH / 2.0
    if console_yaw in (90, 270):
        dy = -KEEP_CLEAR if console_yaw == 90 else KEEP_CLEAR
        lo = (cx - half, min(cy, cy + dy), 0)
        hi = (cx + half, max(cy, cy + dy), STAND_CLEARANCE)
    else:
        dx = -KEEP_CLEAR if console_yaw == 0 else KEEP_CLEAR
        lo = (min(cx, cx + dx), cy - half, 0)
        hi = (max(cx, cx + dx), cy + half, STAND_CLEARANCE)
    keep_clear.append(("console", lo, hi))

    return Ship(plan, boxes, lights, console_location, console_yaw,
                player_start, regions, keep_clear)
```

- [ ] **Step 2: Check the ship generates**

Run:

```bash
python3 -c "
import sys; sys.path.insert(0, 'Tools')
import hauler_layout as L
from collections import Counter
s = L.generate()
print(len(s.boxes), 'boxes', dict(Counter(b.role for b in s.boxes)))
print(len(s.lights), 'lights; console', s.console_location, 'yaw', s.console_yaw)
print('start', s.player_start)"
```

Expected:

```
182 boxes {'floor': 8, 'ceiling': 8, 'wall': 54, 'seal': 1, 'trim': 11, 'glass': 2, 'lamp': 31, 'furniture': 50, 'screen': 6, 'accent': 11}
31 lights; console (600.0, 472, 120) yaw 90
start (800, -240, 100)
```

11 trim boxes is one lintel per opening: eight doors, two windows, one seal.

- [ ] **Step 3: Replace the validator**

Replace the whole of `Tools/validate_hauler.py`. Two details worth understanding before reading it:

- **Reachability is bounded to each room's floor.** Roofs are standable — solid below, open above — and every roof touches its neighbours. The milestone 1 validator seeded the Player Start's whole column, which included the roof over it, so it could reach any room by walking across the top of the ship. `start_seed` and `floor_hit` exist to prevent exactly that.
- **The player has width.** `standable` requires a 70 cm square to be clear around a cell, approximating the 68 cm capsule. Without it a single-cell crack reads as walkable.

```python
#!/usr/bin/env python3
"""
Validates the ship before it is ever opened in the editor.

    python3 Tools/validate_hauler.py

Exits non-zero on any failure, so it can gate a build.

Two kinds of check. *Soundness*: the floor plan is self-consistent, the hull is
sealed, every piece of geometry touches another. *Intent*: the spaces still do
what they were designed for -- the crawlway can only be entered crouching, the
corridor keeps a clear run long enough to slide down, and no furniture blocks a
door or the console. A ship can be perfectly playable and still fail on intent;
that is the point of checking it.

Method: rasterise every box onto a 10 cm grid, sampling at cell centres. Then

  * flood *air* in 3D from the Player Start: if it reaches the edge of the
    grid, the hull leaks;
  * find cells a player could stand in -- solid below, clear above for the
    posture's height, and clear for a capsule's width around -- and flood those
    in 2D, allowing a one-cell step, to test reachability per posture.

The capsule's width matters: without it a single-cell crack reads as walkable.
"""

import os
import sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import hauler_layout as L
from floorplan import CELL, PlanError

MARGIN = 3          # empty cells around the ship, so a leak has somewhere to go


class Grid:
    def __init__(self, boxes):
        lows = [min(b.centre[a] - b.size[a] / 2.0 for b in boxes) for a in range(3)]
        highs = [max(b.centre[a] + b.size[a] / 2.0 for b in boxes) for a in range(3)]
        self.origin = [(int(lows[a] // CELL) - MARGIN) * CELL for a in range(3)]
        self.dim = [int((highs[a] - self.origin[a]) // CELL) + MARGIN + 1 for a in range(3)]
        self.nx, self.ny, self.nz = self.dim
        self.solid = bytearray(self.nx * self.ny * self.nz)
        for b in boxes:
            self._fill(b)

    def index(self, i, j, k):
        return (i * self.ny + j) * self.nz + k

    def _cells(self, axis, lo, hi):
        # Cells whose centres fall inside [lo, hi). Sampling at centres keeps
        # a 10 cm wall exactly one cell thick, rather than smearing it to two.
        o = self.origin[axis]
        first = int(-(-((lo - o) / CELL - 0.5) // 1))
        last = int(((hi - o) / CELL - 0.5) // 1)
        if (last + 0.5) * CELL + o >= hi:
            last -= 1
        return range(max(0, first), min(self.dim[axis] - 1, last) + 1)

    def _fill(self, b):
        lo = [b.centre[a] - b.size[a] / 2.0 for a in range(3)]
        hi = [b.centre[a] + b.size[a] / 2.0 for a in range(3)]
        ks = self._cells(2, lo[2], hi[2])
        for i in self._cells(0, lo[0], hi[0]):
            for j in self._cells(1, lo[1], hi[1]):
                base = (i * self.ny + j) * self.nz
                for k in ks:
                    self.solid[base + k] = 1

    def cell_of(self, x, y, z):
        return tuple(int((v - self.origin[a]) // CELL) for a, v in enumerate((x, y, z)))

    def world(self, i, j, k):
        return tuple(self.origin[a] + (v + 0.5) * CELL for a, v in enumerate((i, j, k)))


# -- soundness ---------------------------------------------------------------

def check_hull(grid, ship, failures):
    si, sj, sk = grid.cell_of(*ship.player_start)
    start = grid.index(si, sj, sk)
    if grid.solid[start]:
        failures.append("Player Start %s is inside geometry." % (ship.player_start,))
        return
    nx, ny, nz = grid.dim
    seen = bytearray(len(grid.solid))
    seen[start] = 1
    queue = deque([(si, sj, sk)])
    while queue:
        i, j, k = queue.popleft()
        if i in (0, nx - 1) or j in (0, ny - 1) or k in (0, nz - 1):
            failures.append("Hull leaks to space: interior air reaches the grid edge "
                            "near %s." % (tuple(round(v) for v in grid.world(i, j, k)),))
            return
        for (a, b, c) in ((i + 1, j, k), (i - 1, j, k), (i, j + 1, k),
                          (i, j - 1, k), (i, j, k + 1), (i, j, k - 1)):
            n = grid.index(a, b, c)
            if not grid.solid[n] and not seen[n]:
                seen[n] = 1
                queue.append((a, b, c))


def check_components(boxes, failures):
    """Every box must touch another, and all must form one piece. An isolated
    box is a floating wall or a prop hanging in air."""
    tol = 0.01
    lo = [[b.centre[a] - b.size[a] / 2.0 for a in range(3)] for b in boxes]
    hi = [[b.centre[a] + b.size[a] / 2.0 for a in range(3)] for b in boxes]
    n = len(boxes)
    adj = [[] for _ in range(n)]
    for p in range(n):
        for q in range(p + 1, n):
            if all(lo[p][a] <= hi[q][a] + tol and lo[q][a] <= hi[p][a] + tol for a in range(3)):
                adj[p].append(q)
                adj[q].append(p)
    for p in range(n):
        if not adj[p]:
            failures.append("'%s' touches nothing." % boxes[p].label)
    seen, groups = [False] * n, 0
    for p in range(n):
        if seen[p]:
            continue
        groups += 1
        stack = [p]
        while stack:
            q = stack.pop()
            if not seen[q]:
                seen[q] = True
                stack.extend(adj[q])
    if groups > 1:
        failures.append("Geometry is in %d disjoint pieces." % groups)


# -- reachability ------------------------------------------------------------

def standable(grid, clearance, radius):
    """Cells a capsule of this height and radius could occupy: solid beneath,
    `clearance` of air above, and the same true of every cell within `radius`
    around it on the same level."""
    nx, ny, nz = grid.dim
    tall = int(clearance // CELL)
    solid = grid.solid
    base = bytearray(len(solid))
    for i in range(nx):
        for j in range(ny):
            col = (i * ny + j) * nz
            run = 0                                  # air cells from k upward
            for k in range(nz - 1, 0, -1):
                run = run + 1 if not solid[col + k] else 0
                if run >= tall and solid[col + k - 1]:
                    base[col + k] = 1
    r = int(radius // CELL)
    out = bytearray(len(solid))
    for idx in range(len(base)):
        if not base[idx]:
            continue
        k = idx % nz
        ij = idx // nz
        i, j = ij // ny, ij % ny
        if i - r < 0 or i + r >= nx or j - r < 0 or j + r >= ny:
            continue
        ok = True
        for di in range(-r, r + 1):
            row = ((i + di) * ny) * nz + k
            for dj in range(-r, r + 1):
                if not base[row + (j + dj) * nz]:
                    ok = False
                    break
            if not ok:
                break
        if ok:
            out[idx] = 1
    return out


def reach(grid, ok, seeds):
    """Flood walkable cells from seeds: four compass steps, each allowed to
    rise or drop one cell, so thresholds do not read as walls."""
    nx, ny, nz = grid.dim
    seen = bytearray(len(ok))
    queue = deque()
    for (i, j, k) in seeds:
        n = grid.index(i, j, k)
        if ok[n] and not seen[n]:
            seen[n] = 1
            queue.append((i, j, k))
    while queue:
        i, j, k = queue.popleft()
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            a, b = i + di, j + dj
            if not (0 <= a < nx and 0 <= b < ny):
                continue
            for c in (k, k + 1, k - 1):
                if 0 < c < nz:
                    n = grid.index(a, b, c)
                    if ok[n] and not seen[n]:
                        seen[n] = 1
                        queue.append((a, b, c))
    return seen


def floor_hit(grid, cells, x, y, top):
    """Is (x, y) reached at floor level, between the floor and `top`?

    Bounded above deliberately. Roofs are standable -- solid beneath, open
    above -- and every roof touches its neighbours, so an unbounded check
    counts a room as reached by walking across the top of the ship into it.
    """
    i, j, _ = grid.cell_of(x, y, 0)
    k_floor = grid.cell_of(0, 0, 0)[2]
    k_top = grid.cell_of(0, 0, top)[2]
    return any(cells[grid.index(i, j, k)] for k in range(k_floor, min(k_top, grid.nz)))


def start_seed(grid, ok, location):
    """The standable cell the Player Start drops onto. Only this one: seeding
    its whole column would include the roof above it."""
    i, j, k = grid.cell_of(*location)
    for kk in range(k, 0, -1):
        if ok[grid.index(i, j, kk)]:
            return [(i, j, kk)]
    return []


def check_reachability(grid, ship, failures):
    stand_ok = standable(grid, L.STAND_CLEARANCE, L.CAPSULE_RADIUS)
    crouch_ok = standable(grid, L.CROUCH_CLEARANCE, L.CAPSULE_RADIUS)
    stand_seed = start_seed(grid, stand_ok, ship.player_start)
    if not stand_seed:
        failures.append("Player Start %s has no room to stand." % (ship.player_start,))
        return None
    by_stand = reach(grid, stand_ok, stand_seed)
    by_crouch = reach(grid, crouch_ok, start_seed(grid, crouch_ok, ship.player_start))

    heights = {name: ship.plan.room(room).height for name, room in
               ((r.name, r.room) for r in L.REGIONS)}
    for name, (x, y, _), posture in ship.regions:
        top = heights[name]
        if posture == "stand":
            if not floor_hit(grid, by_stand, x, y, top):
                failures.append("Region '%s' at (%d, %d) cannot be reached standing."
                                % (name, x, y))
        elif posture == "crouch":
            if not floor_hit(grid, by_crouch, x, y, top):
                failures.append("Region '%s' at (%d, %d) cannot be reached even crouching."
                                % (name, x, y))
            if floor_hit(grid, by_stand, x, y, top):
                failures.append("Region '%s' at (%d, %d) can be reached standing: it no "
                                "longer requires crouching." % (name, x, y))
        else:
            failures.append("Region '%s' has unknown posture '%s'." % (name, posture))
    return by_stand


def check_slide_run(grid, ship, by_stand, failures):
    """The corridor exists to be slid down: its centreline must hold a clear,
    standing-height straight run of at least SLIDE_RUN."""
    if by_stand is None:
        return
    r = ship.plan.room(L.SLIDE_ROOM)
    cy = r.y + r.d / 2.0
    best = run = 0
    for x in range(r.x + CELL // 2, r.x + r.w, CELL):
        run = run + 1 if floor_hit(grid, by_stand, x, cy, r.height) else 0
        best = max(best, run)
    if best * CELL < L.SLIDE_RUN:
        failures.append("The %s's longest clear straight run is %d cm; sliding needs %d."
                        % (L.SLIDE_ROOM, best * CELL, L.SLIDE_RUN))


# -- intent ------------------------------------------------------------------

def check_keep_clear(ship, failures):
    props = [b for b in ship.boxes if b.label.startswith("prop_")]
    for name, lo, hi in ship.keep_clear:
        for b in props:
            blo = [b.centre[a] - b.size[a] / 2.0 for a in range(3)]
            bhi = [b.centre[a] + b.size[a] / 2.0 for a in range(3)]
            if all(blo[a] < hi[a] and lo[a] < bhi[a] for a in range(3)):
                failures.append("'%s' is in the keep-clear zone of the %s." % (b.label, name))


def check_console(grid, ship, failures):
    i, j, k = grid.cell_of(*ship.console_location)
    if grid.solid[grid.index(i, j, k)]:
        failures.append("The console at %s is buried in geometry." % (ship.console_location,))


def main():
    try:
        ship = L.generate()
    except PlanError as e:
        print("FAIL: the floor plan is not self-consistent:\n  - %s" % e)
        return 1

    grid = Grid(ship.boxes)
    failures = []
    check_hull(grid, ship, failures)
    check_components(ship.boxes, failures)
    check_console(grid, ship, failures)
    check_keep_clear(ship, failures)
    by_stand = check_reachability(grid, ship, failures)
    check_slide_run(grid, ship, by_stand, failures)

    print("%d boxes, %d lights, %d regions on a %d x %d x %d grid at %d cm."
          % (len(ship.boxes), len(ship.lights), len(ship.regions),
             grid.nx, grid.ny, grid.nz, CELL))
    if failures:
        print("\nFAIL (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        return 1
    print("\nPASS: plan consistent, hull sealed, one piece, every region reachable in "
          "its posture, crawlway crouch-only, %d cm slide run clear, doors and console "
          "unobstructed." % L.SLIDE_RUN)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run it on the ship**

Run: `time python3 Tools/validate_hauler.py`
Expected, in about a second:

```
182 boxes, 31 lights, 10 regions on a 266 x 99 x 59 grid at 10 cm.

PASS: plan consistent, hull sealed, one piece, every region reachable in its posture, crawlway crouch-only, 1200 cm slide run clear, doors and console unobstructed.
```

- [ ] **Step 5: Confirm each intent check can fail**

Each mutation below must produce the stated failure. Restore after each.

```bash
cp Tools/hauler_layout.py /tmp/layout.bak
m() { python3 Tools/validate_hauler.py | grep -E '^  - ' | head -2; cp /tmp/layout.bak Tools/hauler_layout.py; }

# Crawlway tall enough to stand in.
sed -i 's|Room("crawlway",    0,    390,  390,  90,  110)|Room("crawlway",    0,    390,  390,  90,  200)|; s|90, 100),|90, 190),|g' Tools/hauler_layout.py
m   # expect: Region 'crawlway' ... can be reached standing: it no longer requires crouching.

# A locker against the corridor wall: passable, but breaks the straight run.
sed -i 's|    Place("conduit", "corridor", (0, 146)),|    Place("conduit", "corridor", (0, 146)),\n    Place("locker", "corridor", (700, 50)),|' Tools/hauler_layout.py
m   # expect: The corridor's longest clear straight run is 640 cm; sliding needs 1200.

# A door narrower than the capsule.
sed -i 's|Door("corridor", "galley", 150, 220),|Door("corridor", "galley", 50, 220),|' Tools/hauler_layout.py
m   # expect: Region 'galley' at (1230, 200) cannot be reached standing.

# A container parked in front of a door.
sed -i 's|    Place("counter",      "galley", (30, 200)),|    Place("counter",      "galley", (30, 200)),\n    Place("container_small", "galley", (250, 70)),|' Tools/hauler_layout.py
m   # expect: 'prop_container_small_3_0' is in the keep-clear zone of the door at (1055, 75).

# The overhead panel lowered off the ceiling: floating.
sed -i 's|elevation=228|elevation=200|' Tools/hauler_layout.py
m   # expect: Geometry is in 2 disjoint pieces.

python3 Tools/validate_hauler.py | tail -1   # expect: PASS ...
```

The galley-door mutation is the regression test for the roof bug: before the fix, it passed.

- [ ] **Step 6: Commit**

```bash
git add Tools/hauler_layout.py Tools/validate_hauler.py
git commit -m "feat: eight-room hauler as a floor plan; validate design intent

The validator now checks posture-aware reachability with capsule width, that the
crawlway is crouch-only, that the corridor keeps a 12 m slide run, and that no
furniture blocks a door or the console. Also fixes a bug inherited from
milestone 1: reachability was seeded with the Player Start's whole column,
including the roof, so rooms could be reached across the top of the ship."
```

---

### Task 4: Materials, builder, and verifier

The editor side: realise the boxes with meshes and materials by role, then measure the result.

**Materials by role.** Panelled roles (`wall floor ceiling furniture trim seal`) are instances of the template's `M_PrototypeGrid`, which computes seams from world position. That is what matters here: the ship is cubes scaled to wildly different proportions, and UV-mapped seams would stretch with them. Emissive roles (`accent screen lamp`) instance a small generated unlit material, `M_ShipEmissive`. `glass` reuses milestone 1's `M_Glass`.

**No actor is ever rotated.** Every prop part is already rotated into an axis-aligned size by Task 2, and a cube, chamfered cube or cylinder turned 90° about Z is the same shape with its X and Y sizes swapped.

**Files:**
- Modify (full rewrite): `Tools/build_hauler.py`
- Modify (full rewrite): `Tools/verify_level.py`
- Generated: `Content/Maps/L_Hauler.umap`, `Content/Materials/MI_Ship_*.uasset`, `Content/Materials/M_ShipEmissive.uasset`

**Interfaces:**
- Consumes: `hauler_layout.generate()` and the `Ship` fields from Task 3; `Box.mesh` and `Box.role` from Tasks 1–2.
- Produces: actors labelled `hauler_<box.label>`, `hauler_light_<room>_<i>_<j>`, `hauler_console`, `hauler_player_start`, `hauler_star_<nnn>`.

- [ ] **Step 1: Replace the builder**

Replace the whole of `Tools/build_hauler.py`:

```python
"""
Builds Content/Maps/L_Hauler from Tools/hauler_layout.py.

The .umap is a build output. The layout is the source of truth: change it and
re-run, never nudge built actors by hand. Validate first -- it takes about a
second and needs no editor:

    python3 Tools/validate_hauler.py

Then build, and check the result matches the layout:

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/build_hauler.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/hauler_build.txt

`unreal.log` does not reach stdout under the commandlet, so the summary is
written to Saved/hauler_build.txt.
"""

import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hauler_layout as L

MAP_PATH = "/Game/Maps/L_Hauler"
MATERIAL_DIR = "/Game/Materials"
CONSOLE_BP = "/Game/Blueprints/BP_ShipConsole"
GAMEMODE_BP = "/Game/Blueprints/BP_DeepSpaceGameMode"
GRID_MATERIAL = "/Game/LevelPrototyping/Materials/M_PrototypeGrid"
GLASS_MATERIAL = "/Game/Materials/M_Glass"
STAR_MATERIAL = "/Game/Materials/M_Star"

MESHES = {
    "cube": "/Game/LevelPrototyping/Meshes/SM_Cube",
    "chamfer": "/Game/LevelPrototyping/Meshes/SM_ChamferCube",
    "cylinder": "/Game/LevelPrototyping/Meshes/SM_Cylinder",
}
SPHERE = "/Engine/BasicShapes/Sphere"

# Every actor the script owns carries this prefix and is rebuilt each run.
TAG = "hauler_"
TEMPLATE_CRUFT = ("Floor", "SM_SkySphere")
SPACE_STRIPS = (unreal.SkyAtmosphere, unreal.VolumetricCloud, unreal.ExponentialHeightFog)

STAR_COUNT = 160
STAR_RADIUS = 12000.0

TEAL = (0.05, 0.55, 0.55)

# Clean retro-future. Panelled roles are instances of the template's
# world-aligned grid material: seams are computed from world position, so they
# stay a constant size on boxes scaled to any proportion, and line up across
# neighbouring boxes. (surface colour, seam colour, panel size cm, roughness)
PANELLED = {
    "wall":      ((0.78, 0.80, 0.82), (0.60, 0.62, 0.65), 120, 0.45),
    "floor":     ((0.52, 0.51, 0.49), (0.38, 0.37, 0.36), 100, 0.65),
    "ceiling":   ((0.85, 0.86, 0.88), (0.70, 0.71, 0.73), 120, 0.50),
    "furniture": ((0.70, 0.71, 0.72), (0.58, 0.59, 0.60), 60,  0.35),
    "trim":      (TEAL,               (0.03, 0.40, 0.40), 60,  0.35),
    "seal":      ((0.40, 0.42, 0.45), (0.05, 0.45, 0.45), 40,  0.40),
}
# Emissive roles: an unlit colour, brighter than 1 to read as a light source.
EMISSIVE = {
    "accent": (TEAL[0] * 4, TEAL[1] * 4, TEAL[2] * 4),
    "screen": (0.02, 0.10, 0.12),
    "lamp":   (6.0, 6.2, 6.5),
}
LIGHT_COLOUR = unreal.Color(r=255, g=247, b=235, a=255)   # neutral-cool white


# -- materials ---------------------------------------------------------------

def _asset(path):
    return unreal.EditorAssetLibrary.load_asset(path) if \
        unreal.EditorAssetLibrary.does_asset_exist(path) else None


def _create(name, cls, factory):
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, MATERIAL_DIR, cls, factory)


def _save(asset):
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)


def panelled_instance(role, surface, seam, panel, roughness):
    """A material instance of the template grid, (re)configured every build so
    the numbers in PANELLED are always what is on screen."""
    path = "%s/MI_Ship_%s" % (MATERIAL_DIR, role)
    mi = _asset(path) or _create("MI_Ship_" + role, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, unreal.EditorAssetLibrary.load_asset(GRID_MATERIAL))
    colour = lambda c: unreal.LinearColor(c[0], c[1], c[2], 1.0)
    mel.set_material_instance_vector_parameter_value(mi, "SurfaceColor", colour(surface))
    mel.set_material_instance_vector_parameter_value(mi, "GridColor", colour(seam))
    # Hide the template's sub-grid: one seam per panel reads as panelling,
    # five reads as graph paper.
    mel.set_material_instance_vector_parameter_value(mi, "SubGridColor", colour(surface))
    mel.set_material_instance_scalar_parameter_value(mi, "Grid Size", float(panel))
    mel.set_material_instance_scalar_parameter_value(mi, "Roughness", float(roughness))
    mel.set_material_instance_static_switch_parameter_value(mi, "ObjectAligned", False)
    mel.set_material_instance_static_switch_parameter_value(mi, "Grid", True)
    mel.update_material_instance(mi)
    _save(mi)
    return mi


def emissive_base():
    """One unlit material with a colour parameter; emissive roles instance it."""
    path = MATERIAL_DIR + "/M_ShipEmissive"
    existing = _asset(path)
    if existing:
        return existing
    mat = _create("M_ShipEmissive", unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    param = unreal.MaterialEditingLibrary.create_material_expression(
        mat, unreal.MaterialExpressionVectorParameter, -300, 0)
    param.set_editor_property("parameter_name", "Colour")
    param.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    unreal.MaterialEditingLibrary.connect_material_property(
        param, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    _save(mat)
    return mat


def emissive_instance(role, colour, base):
    path = "%s/MI_Ship_%s" % (MATERIAL_DIR, role)
    mi = _asset(path) or _create("MI_Ship_" + role, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
    mel = unreal.MaterialEditingLibrary
    mel.set_material_instance_parent(mi, base)
    mel.set_material_instance_vector_parameter_value(
        mi, "Colour", unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
    mel.update_material_instance(mi)
    _save(mi)
    return mi


def star_material():
    existing = _asset(STAR_MATERIAL)
    if existing:
        return existing
    raise RuntimeError(STAR_MATERIAL + " is missing; it is created by the milestone 1 build")


def materials():
    """Role name -> material. Surfaces name a role, never a material, so
    swapping in real textures later is a change here alone."""
    out = {role: panelled_instance(role, *spec) for role, spec in PANELLED.items()}
    base = emissive_base()
    out.update({role: emissive_instance(role, c, base) for role, c in EMISSIVE.items()})
    out["glass"] = unreal.EditorAssetLibrary.load_asset(GLASS_MATERIAL)
    return out


# -- actors ------------------------------------------------------------------

def mesh_bounds(mesh):
    b = mesh.get_bounding_box()
    return (b.min.x, b.min.y, b.min.z), (b.max.x, b.max.y, b.max.z)


def spawn_box(actor_sub, box, mesh, material):
    """Place `mesh` so it exactly fills `box`, whatever the mesh's pivot.

    The three meshes disagree about their origin: SM_Cube's is at a corner,
    SM_ChamferCube's at its centre, SM_Cylinder's at its base. So scale comes
    from the mesh's measured extent and the offset from its measured centre;
    no convention is assumed. No rotation is ever needed: every part has been
    rotated into an axis-aligned size already, and a cube, chamfered cube or
    cylinder turned 90 degrees about Z is the same shape with its X and Y sizes
    swapped.
    """
    lo, hi = mesh_bounds(mesh)
    scale = [box.size[a] / (hi[a] - lo[a]) for a in range(3)]
    mesh_centre = [(lo[a] + hi[a]) / 2.0 for a in range(3)]
    location = unreal.Vector(*[box.centre[a] - mesh_centre[a] * scale[a] for a in range(3)])
    actor = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, location,
                                             unreal.Rotator(0, 0, 0))
    actor.set_actor_label(TAG + box.label)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_mobility(unreal.ComponentMobility.STATIC)
    component.set_material(0, material)
    return actor


def clear_previous(actor_sub):
    removed = 0
    for actor in actor_sub.get_all_level_actors():
        label = actor.get_actor_label()
        if (label.startswith(TAG) or label in TEMPLATE_CRUFT
                or isinstance(actor, SPACE_STRIPS)
                or isinstance(actor, unreal.PlayerStart)):
            actor_sub.destroy_actor(actor)
            removed += 1
    return removed


def tame_sky_light(actor_sub):
    """No atmosphere to capture in space, so a real-time-capture SkyLight only
    warns. Capture once, and keep it faint."""
    for actor in actor_sub.get_all_level_actors():
        if isinstance(actor, unreal.SkyLight):
            c = actor.get_component_by_class(unreal.SkyLightComponent)
            c.set_editor_property("real_time_capture", False)
            c.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
            c.set_editor_property("intensity", 0.05)


def place_lights(actor_sub, lights):
    for light in lights:
        actor = actor_sub.spawn_actor_from_class(
            unreal.PointLight, unreal.Vector(*light.location), unreal.Rotator(0, 0, 0))
        actor.set_actor_label(TAG + light.label)
        c = actor.get_component_by_class(unreal.PointLightComponent)
        c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        c.set_editor_property("intensity", float(light.intensity))
        c.set_editor_property("attenuation_radius", float(light.radius))
        c.set_editor_property("light_color", LIGHT_COLOUR)
        # Soft, even fill: many overlapping lights, none casting shadows. Also
        # what keeps thirty-odd lights cheap.
        c.set_editor_property("cast_shadows", False)


def scatter_stars(actor_sub, sphere, material):
    """Golden-angle spiral: even coverage where uniform random clumps."""
    golden = math.pi * (3.0 - math.sqrt(5.0))
    for i in range(STAR_COUNT):
        y = 1.0 - (i / float(STAR_COUNT - 1)) * 2.0
        r = math.sqrt(max(0.0, 1.0 - y * y))
        t = golden * i
        loc = unreal.Vector(math.cos(t) * r * STAR_RADIUS, y * STAR_RADIUS,
                            math.sin(t) * r * STAR_RADIUS + 500.0)
        star = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, loc,
                                                unreal.Rotator(0, 0, 0))
        star.set_actor_label(TAG + "star_%03d" % i)
        star.set_actor_scale3d(unreal.Vector(0.3, 0.3, 0.3))
        star.static_mesh_component.set_static_mesh(sphere)
        star.static_mesh_component.set_material(0, material)


def build():
    ship = L.generate()                      # raises PlanError on a bad plan

    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        level_sub.load_level(MAP_PATH)
    else:
        level_sub.new_level(MAP_PATH)

    removed = clear_previous(actor_sub)
    tame_sky_light(actor_sub)

    mats = materials()
    meshes = {k: unreal.EditorAssetLibrary.load_asset(p) for k, p in MESHES.items()}
    for box in ship.boxes:
        spawn_box(actor_sub, box, meshes[box.mesh], mats[box.role])

    place_lights(actor_sub, ship.lights)
    scatter_stars(actor_sub, unreal.EditorAssetLibrary.load_asset(SPHERE), star_material())

    console = actor_sub.spawn_actor_from_class(
        unreal.EditorAssetLibrary.load_blueprint_class(CONSOLE_BP),
        unreal.Vector(*ship.console_location), unreal.Rotator(0, 0, ship.console_yaw))
    console.set_actor_label(TAG + "console")
    console_mesh = console.get_component_by_class(unreal.StaticMeshComponent)
    if console_mesh:
        console_mesh.set_material(0, mats["furniture"])

    start = actor_sub.spawn_actor_from_class(
        unreal.PlayerStart, unreal.Vector(*ship.player_start), unreal.Rotator(0, 0, 0))
    start.set_actor_label(TAG + "player_start")

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property(
        "default_game_mode", unreal.EditorAssetLibrary.load_blueprint_class(GAMEMODE_BP))

    level_sub.save_current_level()

    summary = ("L_Hauler built: removed %d, placed %d boxes, %d lights, %d stars."
               % (removed, len(ship.boxes), len(ship.lights), STAR_COUNT))
    with open(os.path.join(unreal.Paths.project_saved_dir(), "hauler_build.txt"), "w") as f:
        f.write(summary + "\n")
    unreal.log(summary)


build()
```

- [ ] **Step 2: Replace the verifier**

Replace the whole of `Tools/verify_level.py`:

```python
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
                        ("player_start", ship.player_start)):
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
```

- [ ] **Step 3: Close the editor, then build**

```bash
# [U]nrealEditor, not UnrealEditor: the brackets stop pgrep matching this
# shell's own command line, which contains the pattern text.
pgrep -f "[U]nrealEditor .*DeepSpace.uproject" && echo "CLOSE THE EDITOR FIRST" || \
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/build_hauler.py" \
    -unattended -nopause -nosplash -NoLiveCoding 2>&1 | grep -E "Traceback|Success -|Failure -"
cat Saved/hauler_build.txt
ls Content/Materials/
```

Expected: `Success - 0 error(s)`; the summary reads `placed 182 boxes, 31 lights, 160 stars`; `Content/Materials/` contains `MI_Ship_accent`, `_ceiling`, `_floor`, `_furniture`, `_lamp`, `_screen`, `_seal`, `_trim`, `_wall`, and `M_ShipEmissive`, alongside the existing `M_Glass` and `M_Star`.

- [ ] **Step 4: Verify the built level**

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/verify_level.py" \
    -unattended -nopause -nosplash -NoLiveCoding >/dev/null 2>&1
cat Saved/verify_level.txt
```

Expected: `PASS: the built level matches the layout within 1.0 cm.`

- [ ] **Step 5: Commit**

```bash
git add Tools/build_hauler.py Tools/verify_level.py Content/Maps/L_Hauler.umap Content/Materials/
git commit -m "feat: build the expanded hauler with role-based materials and furniture

Panelled surfaces instance the template's world-aligned M_PrototypeGrid, so
seams stay constant on boxes scaled to any proportion. Emissive roles instance
a small generated unlit material. Meshes are placed from their measured bounds,
since SM_Cube, SM_ChamferCube and SM_Cylinder use three different pivots."
```

---

### Task 5: Look, playtest, and records

The only task that needs a human in the editor. It checks what no script can — that the ship reads as a coherent clean retro-future interior — and records what planning changed.

**Files:**
- Modify: `docs/playtest-checklist.md`
- Modify: `CLAUDE.md`
- Modify: `docs/decisions/0004-generated-level-geometry.md`
- Modify: `docs/superpowers/specs/2026-09-20-ship-expansion-design.md`

**Interfaces:**
- Consumes: the built level from Task 4.

- [ ] **Step 1: Walk the ship and judge the look**

`unreal-editor DeepSpace.uproject`, open `L_Hauler`, Play. You should wake in the bunk. Look for:

- walls reading as panels, not stretched grids — seams a constant size on the 14 m corridor walls and the 1 m wall stubs alike
- teal lintels over every opening; teal bands on the containers and reactor; glowing screens in the cockpit and on the workbench
- even, soft light, with no dark rooms and no blown-out ones
- furniture not clipping walls, the ceiling, or each other

**Tuning lives in two places only.** Colours, panel sizes and roughness are the `PANELLED` and `EMISSIVE` tables at the top of `Tools/build_hauler.py`. Light strength is `intensity` in `placement.resolve_lights`. Change the numbers, close the editor, re-run Task 4 Steps 3–4. Never adjust materials or actors in the editor: the next build overwrites them.

- [ ] **Step 2: Update the playtest checklist**

Replace the **Movement and collision**, **View** and **Interaction** sections of `docs/playtest-checklist.md`, keeping **Camera** and **Stability** as they are:

```markdown
## Movement and collision
- [ ] Spawn in the bunk, standing, camera at plausible eye height
- [ ] Walk every room: bunk, corridor, cockpit, galley, engineering, airlock, cargo bay
- [ ] The crawlway is too low to enter standing (crouch arrives in the movement sub-project)
- [ ] No falling through floors anywhere, including across every door threshold
- [ ] No getting stuck on doorframes, wall seams, or furniture
- [ ] Cannot walk through walls, furniture, or the airlock's outer door
- [ ] Cannot escape the ship interior
- [ ] Jump does not clip through any ceiling

## View
- [ ] Cockpit window shows stars
- [ ] Galley viewport shows stars
- [ ] The cargo bay reads as two storeys tall

## Interaction
- [ ] Approaching the engineering console within reach shows a prompt
- [ ] Prompt disappears when looking away, and when too far
- [ ] Pressing E toggles the console
- [ ] Powered console reads DRAW 620 W / 1000 W, HEADROOM 380 W
- [ ] Console reads OFFLINE on spawn
- [ ] No furniture produces a prompt
```

Then run the whole checklist and record results below it, dated, as milestone 1 did. Record failures as failures.

- [ ] **Step 3: Record the pipeline in `CLAUDE.md`**

In the **Generated level geometry** section, replace the first paragraph and the command block with:

````markdown
`Content/Maps/L_Hauler.umap` is **generated, not hand-edited**. The source of
truth is `Tools/hauler_layout.py`: a floor plan of rooms, doors, windows and
seals, plus furniture placements. `Tools/floorplan.py` derives the walls,
`Tools/props.py` holds the furniture templates, `Tools/placement.py` resolves
everything room-relative into world space, and `Tools/build_hauler.py`
realises it in the editor. Actors the script owns are prefixed `hauler_` and
are rebuilt on every run, so hand-placed changes to them are lost. Materials
are rebuilt too: tune them in `build_hauler.py`, not in the editor.

```bash
python3 Tools/test_floorplan.py && python3 Tools/test_placement.py
python3 Tools/validate_hauler.py        # no editor needed, ~1s
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/build_hauler.py" \
    -unattended -nopause -nosplash -NoLiveCoding
```
````

And replace the paragraph beginning `` `validate_hauler.py` voxelises the layout`` with:

```markdown
`validate_hauler.py` voxelises the ship at 10 cm and checks soundness — the
plan is self-consistent, the hull is sealed, everything is one connected piece
— and **intent**: every region is reachable in its posture by a capsule with
width, the crawlway can *only* be entered crouching, the corridor keeps a 12 m
clear run for sliding, and no furniture blocks a door or the console. A ship
can be perfectly playable and still fail on intent; that is the point.
Reachability is bounded to each room's floor, because roofs are standable and
connected — the milestone 1 validator could reach rooms across the roof.
```

- [ ] **Step 4: Amend ADR 0004 and the spec**

Append to `docs/decisions/0004-generated-level-geometry.md`:

```markdown
## Amendment — ship expansion (2026-09-20)

The box list was replaced by a floor plan: rooms, doors, windows, seals.
Walls are derived by rasterising the plan onto the 10 cm grid, where a wall is
any cell bordering an interior that is not itself interior. The generator now
rejects inconsistent plans before any geometry exists — and on its first run
rejected the expansion spec's own room table, whose coordinates were off the
grid the same spec required.

Three meshes, three pivots: `SM_Cube` is corner-origin, `SM_ChamferCube`
centre-origin, `SM_Cylinder` base-centre. With the pivot bug deliberately
reintroduced, the verifier reported 393 failures — every one on an `SM_Cube`
box. The centred furniture passed. A builder that assumed "centred" would have
looked half right.
```

Append to `docs/superpowers/specs/2026-09-20-ship-expansion-design.md`, and change its `**Status:**` line to `Implemented`:

```markdown
## Amendments during planning

Prototyping every file before writing the plan found the following; the plan's
*Deviations from the spec* table has the reasoning.

- Room coordinates shifted onto the 10 cm grid (the table above broke its own
  rule): corridor y −80, engineering and galley y 80, crawlway y 390, airlock
  y −340, bunk y −390. Sizes and adjacency unchanged.
- Cargo door 150 cm, the corridor's width; crawlway doors 90 cm, its width.
- Reactor 220 cm, clear of the derived lamps.
- Panelled materials instance the template's world-aligned `M_PrototypeGrid`
  rather than a hand-built graph; ceiling lamps are geometry.
- The validator models capsule width, and bounds reachability to room floors
  after finding that milestone 1's could walk across the roof.
```

- [ ] **Step 5: Commit**

```bash
git add docs/ CLAUDE.md
git commit -m "docs: record the ship expansion pipeline, playtest results, and planning amendments"
```

---

## Done when

- `python3 Tools/test_floorplan.py`, `python3 Tools/test_placement.py` and `python3 Tools/validate_hauler.py` pass.
- `Saved/verify_level.txt` reads PASS after a build.
- The updated playtest checklist passes, with results recorded.
- The ship reads as a coherent clean retro-future interior. That is a human judgement, and it is the point of this sub-project.

The crawlway's crouch-only property is verified by the validator alone until the movement sub-project provides crouch. Movement must honour the contract constants in `hauler_layout.py`: a crouched capsule under 90 cm, and no wider than the 90 cm crawlway allows.
