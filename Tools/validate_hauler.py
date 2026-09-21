#!/usr/bin/env python3
"""
Validates a ship interior before it is ever opened in the editor.

Generated geometry fails in ways that are tedious to find by walking around:
a doorway that does not line up, a room sealed off behind a wall, a seam
between two slabs that leaks to space. This voxelises the layout and answers
those questions directly. Milestone 1 has one hand-written ship, but the game
is meant to generate them, so the check belongs in code rather than in eyes.

    python3 Tools/validate_hauler.py

Exits non-zero if anything fails, so it can gate a build.

Method: rasterise every box into a 10 cm occupancy grid, then
  * flood-fill *walkable* floor cells in 2D to prove every region connects;
  * flood-fill *air* in 3D from inside to prove the hull does not leak.
A walkable cell is one with solid ground beneath and a clear column above,
which is what stops the connectivity check from crawling through a 10 cm
crack no player could pass.
"""

import sys
import os
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hauler_layout as L

CELL = 10          # cm per voxel
MARGIN = 3         # empty cells padded around the hull, to detect leaks


class Grid:
    def __init__(self, boxes):
        mins = [min(L.bounds(c, s)[0][i] for _, c, s in boxes) for i in range(3)]
        maxs = [max(L.bounds(c, s)[1][i] for _, c, s in boxes) for i in range(3)]
        self.origin = [mins[i] - MARGIN * CELL for i in range(3)]
        self.dim = [int((maxs[i] - mins[i]) / CELL) + 2 * MARGIN + 1 for i in range(3)]
        self.solid = bytearray(self.dim[0] * self.dim[1] * self.dim[2])
        for _, centre, scale in boxes:
            self._fill(*L.bounds(centre, scale))

    def _index_range(self, axis, lo, hi):
        a = int((lo - self.origin[axis]) / CELL)
        b = int((hi - self.origin[axis]) / CELL)
        return range(max(0, a), min(self.dim[axis] - 1, b) + 1)

    def _fill(self, lo, hi):
        for i in self._index_range(0, lo[0], hi[0]):
            for j in self._index_range(1, lo[1], hi[1]):
                for k in self._index_range(2, lo[2], hi[2]):
                    self.solid[self._at(i, j, k)] = 1

    def _at(self, i, j, k):
        return (i * self.dim[1] + j) * self.dim[2] + k

    def inside(self, i, j, k):
        return 0 <= i < self.dim[0] and 0 <= j < self.dim[1] and 0 <= k < self.dim[2]

    def is_solid(self, i, j, k):
        return not self.inside(i, j, k) or self.solid[self._at(i, j, k)]

    def cell_of(self, x, y, z):
        return tuple(int((v - self.origin[a]) / CELL) for a, v in enumerate((x, y, z)))

    def on_boundary(self, i, j, k):
        return i in (0, self.dim[0] - 1) or j in (0, self.dim[1] - 1) or k in (0, self.dim[2] - 1)


def walkable_cells(grid):
    """Cells a standing player could occupy: solid floor below, clear column."""
    clearance = int(L.PLAYER_HEIGHT / CELL)
    out = set()
    for i in range(grid.dim[0]):
        for j in range(grid.dim[1]):
            for k in range(1, grid.dim[2]):
                if grid.is_solid(i, j, k) or not grid.is_solid(i, j, k - 1):
                    continue
                if all(not grid.is_solid(i, j, k + n) for n in range(clearance)):
                    out.add((i, j, k))
    return out


def flood(seeds, passable, neighbours):
    seen = set(s for s in seeds if s in passable)
    queue = deque(seen)
    while queue:
        cur = queue.popleft()
        for nxt in neighbours(cur):
            if nxt in passable and nxt not in seen:
                seen.add(nxt)
                queue.append(nxt)
    return seen


def walk_neighbours(cell):
    i, j, k = cell
    # Four compass directions, plus a one-cell step up or down so a threshold
    # or the edge of the bed does not read as a wall.
    for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        for dk in (0, 1, -1):
            yield (i + di, j + dj, k + dk)


def air_neighbours(cell):
    i, j, k = cell
    yield from ((i + 1, j, k), (i - 1, j, k), (i, j + 1, k),
                (i, j - 1, k), (i, j, k + 1), (i, j, k - 1))


def check_connectivity(grid, failures):
    walk = walkable_cells(grid)
    start = grid.cell_of(*L.PLAYER_START_LOC)
    seeds = [(start[0], start[1], k) for k in range(grid.dim[2])]
    reached = flood(seeds, walk, walk_neighbours)

    if not reached:
        failures.append("Player Start at %s is not standable at all." % (L.PLAYER_START_LOC,))
        return

    for name, (x, y) in L.REGIONS.items():
        cell = grid.cell_of(x, y, 0)
        column = [c for c in walk if c[0] == cell[0] and c[1] == cell[1]]
        if not column:
            failures.append("Region '%s' at (%d, %d) has no standable floor "
                            "(no floor, or under 180 cm of headroom)." % (name, x, y))
        elif not any(c in reached for c in column):
            failures.append("Region '%s' at (%d, %d) is standable but cannot be "
                            "walked to from the Player Start." % (name, x, y))


def check_hull(grid, failures):
    air = set()
    for i in range(grid.dim[0]):
        for j in range(grid.dim[1]):
            for k in range(grid.dim[2]):
                if not grid.is_solid(i, j, k):
                    air.add((i, j, k))

    start = grid.cell_of(*L.PLAYER_START_LOC)
    if start not in air:
        failures.append("Player Start is embedded in geometry.")
        return

    reached = flood([start], air, air_neighbours)
    leaks = [c for c in reached if grid.on_boundary(*c)]
    if leaks:
        i, j, k = min(leaks)
        world = tuple(round(grid.origin[a] + v * CELL) for a, v in enumerate((i, j, k)))
        failures.append("Hull leaks to space: interior air escapes the bounding box, "
                        "first near %s. %d boundary cells reached." % (world, len(leaks)))


def check_components(failures):
    """Every box should touch another. An isolated slab is a modelling slip
    that reads as a floating wall; a second component is a detached room."""
    tol = 0.01
    boxes = L.BOXES

    def touches(a, b):
        la, ha = L.bounds(a[1], a[2])
        lb, hb = L.bounds(b[1], b[2])
        return all(la[i] <= hb[i] + tol and lb[i] <= ha[i] + tol for i in range(3))

    adj = {i: set() for i in range(len(boxes))}
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            if touches(boxes[i], boxes[j]):
                adj[i].add(j)
                adj[j].add(i)

    seen, components = set(), []
    for i in range(len(boxes)):
        if i in seen:
            continue
        stack, group = [i], []
        while stack:
            cur = stack.pop()
            if cur in seen:
                continue
            seen.add(cur)
            group.append(cur)
            stack.extend(adj[cur] - seen)
        components.append(group)

    for i, neighbours in adj.items():
        if not neighbours:
            failures.append("Box '%s' touches nothing." % boxes[i][0])

    if len(components) > 1:
        summary = "; ".join(
            "{%s}" % ", ".join(boxes[k][0] for k in sorted(g)[:4])
            for g in components)
        failures.append("Geometry is in %d disjoint components: %s"
                        % (len(components), summary))


def check_console(grid, failures):
    cell = grid.cell_of(*L.CONSOLE_LOC)
    if grid.is_solid(*cell):
        failures.append("Console at %s is buried in geometry." % (L.CONSOLE_LOC,))


def main():
    grid = Grid(L.BOXES)
    failures = []
    check_hull(grid, failures)
    check_connectivity(grid, failures)
    check_console(grid, failures)
    check_components(failures)

    print("Grid %d x %d x %d at %d cm, origin %s"
          % (grid.dim[0], grid.dim[1], grid.dim[2], CELL,
             tuple(round(v) for v in grid.origin)))

    if failures:
        print("\nFAIL (%d):" % len(failures))
        for f in failures:
            print("  - " + f)
        return 1

    print("\nPASS: hull sealed, all %d regions reachable, %d boxes in one "
          "connected component, console clear."
          % (len(L.REGIONS), len(L.BOXES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
