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

import json
import os
import sys
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from floorplan import Door, FloorPlan, Room, Seal, Window, CELL
from placement import (Mount, Place, Region, resolve_lights, resolve_mount,
                       resolve_point, resolve_props)

# -- The contract with the character -------------------------------------
# The ship is built for these, and the character is built to fit them. They
# live in movement_contract.json because both sides read them: this file, and
# the C++ test DeepSpace.Player.MovementContract, which fails if the
# character's capsules stop fitting. Change them there, never here.
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "movement_contract.json")) as _f:
    _CONTRACT = json.load(_f)
STAND_CLEARANCE = _CONTRACT["stand_clearance"]
CROUCH_CLEARANCE = _CONTRACT["crouch_clearance"]
CAPSULE_RADIUS = _CONTRACT["capsule_radius"]   # the 10 cm grid rounds 34 to 3 cells

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
    # 150 cm: the crouched capsule is 144 cm, sized to the crouch-walk clip,
    # which carries the camera to 138 cm; a lower ceiling would be seen through.
    Room("crawlway",    0,    390,  390,  90,  150),
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
    # The crawlway's full width and height: the open end of a service duct.
    Door("cargo_bay", "crawlway", 90, 150),
    Door("crawlway", "engineering", 90, 150),
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

# The helm: the port pilot seat. APilotSeat is placed over the decorative
# pilot_seat prop there, at floor level, facing the way that prop faces. The
# starboard seat stays decorative.
PILOT_SEAT = ("cockpit", (175, 130), 0)

SLIDE_ROOM = "corridor"


Ship = namedtuple("Ship", "plan boxes lights console_location console_yaw "
                          "player_start regions keep_clear "
                          "pilot_seat_location pilot_seat_yaw")


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

    seat_room, seat_at, seat_yaw = PILOT_SEAT
    pilot_seat_location = resolve_point(plan, seat_room, seat_at)

    return Ship(plan, boxes, lights, console_location, console_yaw,
                player_start, regions, keep_clear,
                pilot_seat_location, seat_yaw)
