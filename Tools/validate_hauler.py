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

    # Assumes a flat floor across the capsule's footprint: every neighbour is
    # checked at the same level k. True for a single-deck ship; a stepped or
    # multi-level floor needs each neighbour tested at its own floor height.
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
