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
