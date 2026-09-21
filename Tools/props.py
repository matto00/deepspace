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
