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

from floorplan import Door, FloorPlan, PlanError, Room, Seal, Window, CELL
from placement import (DRESS_MARGIN, KeepOut, Mood, Mount, Place, Practical, Region,
                       resolve_lights, resolve_mount, resolve_point,
                       resolve_practicals, resolve_props, resolve_surfaces)

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
    # The cockpit glass is what you fly through, so it is as close to the full
    # fore wall as the structure allows: 360 of 400 cm wide, leaving a 20 cm
    # post at each corner, and 70 to 215 cm tall. The sill sits below a seated
    # pilot's eye line so the view carries down toward what you are
    # approaching, and the head clears the overhead panel at 228.
    Window("cockpit", "fore", 360, 70, 215),
    # Panoramic: the glass wraps the corners and runs a third of the way down
    # each side wall (120 of 350), hard against the fore end, at the same sill
    # and head as the fore pane so it reads as one continuous band. A 20 cm
    # post survives at each fore corner, which is what holds the roof up.
    Window("cockpit", "port", 120, 70, 215, 1700),
    Window("cockpit", "starboard", 120, 70, 215, 1700),
    Window("galley", "starboard", 200, 100, 170),
]

SEALS = [
    Seal("airlock", "port", 120, 220),
]

# Every placement except the lamps, which are in PRACTICALS because each also
# carries a light. PLACEMENTS, below them, is the one complete list: read that,
# never this, or the galley counter goes missing.
FURNITURE = [
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

    # Galley. The counter is in PRACTICALS: its under-cabinet strip is a lamp.
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

# Each room's light: colour temperature and brightness. Warm where people
# live, cooler where the ship works, and the cockpit the dimmest room aboard,
# because scale is only felt in contrast (docs/vision.md): the window should
# be the brightest thing in it. The scale multiplies the ceiling grid's
# intensity and its panels' glow alike.
ROOM_MOOD = {
    "cockpit":     Mood(4200, 0.45),  # dim, so the window is the brightest thing
    "corridor":    Mood(4600, 0.8),
    "cargo_bay":   Mood(5200, 1.0),   # a working hold
    "engineering": Mood(3600, 0.9),
    "galley":      Mood(2900, 0.85),
    "bunk":        Mood(2700, 0.6),
    "airlock":     Mood(6200, 0.9),   # clinical: the one room that is equipment
    "crawlway":    Mood(3200, 0.5),
}

# Lamps somebody put where they read, cook and work: the only lights that
# cast shadows. Each burns at its room's kelvin. Elevations are the tops they
# stand on: the bunk desk's at 77, the workbench's at 90.
PRACTICALS = [
    # At the desk's back corner, reaching over the desk towards the chair.
    Practical(Place("desk_lamp", "bunk", (348, 272), facing=180, elevation=77),
              radius=180, intensity=0.6),
    # The counter carries its own strip, under the uppers.
    Practical(Place("counter", "galley", (30, 200)), radius=250, intensity=1.0),
    # Clamped to the bench's starboard end, clear of its screen, head over the
    # work.
    Practical(Place("bench_lamp", "engineering", (388, 160), facing=180, elevation=90),
              radius=200, intensity=0.8),
]

# Every prop in the ship, lamps included: what resolve_props builds, and what
# anything asking "what furniture is there" -- the surfaces, the dressing --
# must read. A lamp stands on a surface like anything else, so a consumer that
# took FURNITURE alone would lose the counter and dress straight through the
# lamp bases.
PLACEMENTS = FURNITURE + [p.place for p in PRACTICALS]

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

# The laptop, on the galley table. Somewhere you sit down, not a station you
# report to: the allocation it edits is a preference, and the ship is no worse
# for never being asked about it (docs/vision.md, the anti-chore principle).
# (room, (x, y) from the room's corner, z, yaw). z is the table top: the
# galley_table prop's surface is 8 cm thick and centred at 75.
LAPTOP = ("galley", (300, 230), 79, 90)

# The laptop's plan outline, in its own frame: the 22 x 30 cm base, and the
# lid leaning back over +X to about 15 cm. Its user sits on its -X side.
# test_placement.py reads ShipLaptop.cpp to hold the base's size to the C++.
LAPTOP_FOOTPRINT = ((-11, -15), (15, 15))

# The chart: the starboard desk screen, turned into the one you choose a star
# at. The actor's origin is the centre of its glass, so this places the glass
# and not a casing. (room, (x, y) from the room's corner, z, yaw), shaped like
# LAPTOP. x 301 is the desk screen prop's aft face (302) less 1 cm: the
# chart's reach volume starts 0.5 cm behind its glass, and the prop blocks
# Visibility, so a mount that crept back by that half-centimetre would hand
# every bezel trace to the prop. y 285 is the prop's own centre line. Yaw 0
# faces -X, aft, towards the starboard pilot seat it is read from
# (AShipScreen::ConfigurePanel turns the panel round once, centrally).
NAV_SCREEN = ("cockpit", (301, 285), 105, 0)

# The chart's panel width, cm: AShipNavScreen's PanelWidthCm, mirrored here
# for the clutter exclude alone. test_placement.py reads the C++ to hold the
# two equal.
NAV_SCREEN_WIDTH = 68

# Plan conflict 16, recorded for slice 3's clutter generator: nothing may be
# dressed over the chart's footprint plus this margin, nor on the strip
# between the chart and the chair it is read from. The margin is the
# laptop's. generate() resolves it into ship.nav_screen_exclude.
NAV_SCREEN_MARGIN = DRESS_MARGIN
NAV_SCREEN_CHAIR = ("cockpit", (175, 270))    # the starboard pilot_seat

# The system map: the middle desk screen, turned into the one the whole
# cockpit shares (system map spec, decision 1). Shaped like NAV_SCREEN, and 1
# cm proud of the prop's aft face (302) for the chart's reason: the prop
# blocks Visibility, and a panel sunk into it hands every pointer trace to
# the prop. y 200 is the middle screen's own centre line, which is the
# ship's: it sits between the helm and the chart chair, so both can see it,
# and two people in the cockpit share one map rather than each having their
# own. Yaw 0 faces aft, as the chart does. It has no chair of its own and
# needs no clutter exclude: cockpit_desk exports only its wings, and the
# middle of the desk is not a surface.
MAP_SCREEN = ("cockpit", (301, 200), 105, 0)

# The map's panel width, cm: AShipMapScreen's PanelWidthCm, the prop's 70 cm
# face less a centimetre of bezel each side. test_placement.py reads the C++
# to hold the two equal.
MAP_SCREEN_WIDTH = 68

# The map's draw size, pixels: AShipMapScreen's DrawSizePixels (system map
# spec, 600 x 424). With the width it fixes the panel's height, 48 cm, which
# is what check_map_sightline samples the corners of. test_placement.py holds
# it equal to the C++ too.
MAP_DRAW_SIZE = (600, 424)
MAP_SCREEN_HEIGHT = MAP_SCREEN_WIDTH * MAP_DRAW_SIZE[1] / float(MAP_DRAW_SIZE[0])

# How far the eyes lean in, cm, when the chart chair zooms the map (decision
# 13): the chart's 60, so choosing one screen or the other frames the same
# way. build_hauler.py sets it on the placed map; verify_level.py checks it.
MAP_VIEW_DISTANCE = 60

# What each room lets the dressing put things on (lived-in decision 2): the
# surfaces, as "<prop>.<surface>" kinds. A kind not listed here is never
# exported, so nothing can be dressed onto it. The corridor keeps its slide
# run and the crawlway its crouch-only height by having nothing at all.
# Floor bands and walls are after the POC.
ROOM_DRESSING = {
    "corridor":    (),
    "crawlway":    (),
    "cockpit":     ("cockpit_desk.wing_port", "cockpit_desk.wing_stbd"),
    "cargo_bay":   ("wall_rack.shelf_0", "wall_rack.shelf_1", "wall_rack.shelf_2"),
    "engineering": ("workbench.top",),
    "galley":      ("galley_table.top", "counter.top"),
    "bunk":        ("desk.top", "locker.top"),
    "airlock":     ("airlock_bench.seat",),
}

# Where the ship is heard from (lived-in decision 9). One reactor, at the
# reactor prop's own place, carrying the whole voice. One air handler per
# room, centred 20 cm under its ceiling: air handling is not a power
# consumer, so it is the same quiet noise everywhere, and one per room keeps
# every room's air in the room rather than bleeding through a wall from the
# next. No two may share a point: each source seeds its noise from its
# rounded world position, and two at one point would hiss the same noise and
# comb into a whistle (test_placement.py).
HumSource = namedtuple("HumSource", "room at z kind")
HUM_SOURCES = ([HumSource("engineering", (200, 200), 120, "reactor")]
               + [HumSource(r.name, (r.w / 2, r.d / 2), r.height - 20, "air") for r in ROOMS])

# The game opens with waking aboard your ship.
PLAYER_START = ("bunk", (200, 150), 100)

# The helm: the port pilot seat. APilotSeat is placed over the decorative
# pilot_seat prop there, at floor level, facing the way that prop faces. The
# starboard seat stays decorative.
PILOT_SEAT = ("cockpit", (175, 130), 0)

# Where the pilot's eyes are, seated at the helm, from the helm seat's anchor
# on the floor, cm: (forward, starboard, up) in the seat's own frame. It is
# measured, not chosen: DeepSpace.Player.SeatedEyeIsPilotEye sits the real
# character in a helm seat, plays the sitting idle and reads where
# PlaceCamera puts the eyes -- 19 cm forward of the anchor, 2 cm to port and
# 125 cm up, moving less than a centimetre through the idle. It was 170, a
# standing eye; from there the nose line cleared the port desk screen, and
# from the real one it did not.
SEATED_EYE = (19, -2, 125)

# Where the pilot's eyes are, seated at the helm: the helm seat's point plus
# SEATED_EYE. (room, (x, y) from the room's corner, z), shaped like
# PLAYER_START. It is the eye the C++ tests look from (SkyTestWorld::PilotEye),
# and test_placement.py reads that header to hold the two equal, so the
# layout's checks from the helm -- the map in clear view, the glass ahead and
# a wall aft -- are about the same eye the sky and the target bracket are
# tested from. The helm faces yaw 0, so the seat's frame is the room's.
PILOT_EYE = (PILOT_SEAT[0],
             (PILOT_SEAT[1][0] + SEATED_EYE[0], PILOT_SEAT[1][1] + SEATED_EYE[1]),
             SEATED_EYE[2])

SLIDE_ROOM = "corridor"


Ship = namedtuple("Ship", "plan boxes lights console_location console_yaw "
                          "player_start regions keep_clear "
                          "pilot_seat_location pilot_seat_yaw "
                          "laptop_location laptop_yaw "
                          "nav_screen_location nav_screen_yaw nav_screen_exclude "
                          "hum_sources laptop_exclude surfaces keep_outs "
                          "map_screen_location map_screen_yaw pilot_eye")


def generate():
    """The whole ship, resolved: every box, light and fixed point in world
    space. Raises floorplan.PlanError if the plan is not self-consistent."""
    plan = FloorPlan(ROOMS, DOORS, WINDOWS, SEALS)
    lights, lamps = resolve_lights(plan, ROOM_MOOD)
    _, practical_lights = resolve_practicals(plan, PRACTICALS, ROOM_MOOD)
    lights += practical_lights
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

    laptop_room, laptop_at, laptop_z, laptop_yaw = LAPTOP
    laptop_location = resolve_point(plan, laptop_room, laptop_at, laptop_z)

    nav_room, nav_at, nav_z, nav_screen_yaw = NAV_SCREEN
    nav_screen_location = resolve_point(plan, nav_room, nav_at, nav_z)
    nav_screen_exclude = chart_exclude(plan, nav_screen_location, nav_screen_yaw)

    map_room, map_at, map_z, map_screen_yaw = MAP_SCREEN
    map_screen_location = resolve_point(plan, map_room, map_at, map_z)

    eye_room, eye_at, eye_z = PILOT_EYE
    pilot_eye = resolve_point(plan, eye_room, eye_at, eye_z)

    # (label, world location, kind). The reactor is hum_reactor; each room's
    # air is hum_<room>.
    hum_sources = [("hum_reactor" if s.kind == "reactor" else "hum_" + s.room,
                    resolve_point(plan, s.room, s.at, s.z), s.kind)
                   for s in HUM_SOURCES]

    laptop_exclude = laptop_exclude_rect(plan, laptop_location, laptop_yaw)
    surfaces = resolve_surfaces(plan, PLACEMENTS, ROOM_DRESSING,
                                (laptop_exclude, nav_screen_exclude))

    return Ship(plan, boxes, lights, console_location, console_yaw,
                player_start, regions, keep_clear,
                pilot_seat_location, seat_yaw,
                laptop_location, laptop_yaw,
                nav_screen_location, nav_screen_yaw, nav_screen_exclude,
                hum_sources, laptop_exclude, surfaces, keep_outs(plan, keep_clear),
                map_screen_location, map_screen_yaw, pilot_eye)


def laptop_exclude_rect(plan, location, yaw):
    """The laptop's clutter exclude as a world floor-plan rectangle: its
    footprint plus DRESS_MARGIN, and the whole strip from it to the table's
    edge on its user's side, so nothing is ever left between the reader and
    the screen. The strip runs out to the room's wall; the surface it lies on
    clips it to its own edge."""
    import props as P
    (x0, y0), (x1, y1) = LAPTOP_FOOTPRINT
    r = plan.room(LAPTOP[0])
    reach = r.w + r.d                     # past any edge of any table in the room
    lo, hi = P.rotate_rect((x0 - reach, y0 - DRESS_MARGIN),
                           (x1 + DRESS_MARGIN, y1 + DRESS_MARGIN), yaw)
    x, y, _ = location
    return (x + lo[0], y + lo[1]), (x + hi[0], y + hi[1])


def keep_outs(plan, keep_clear):
    """What the dressing may never touch, as world boxes the C++ generator
    is handed (Dress.KeepOut markers): every door's and the console's
    keep-clear zone, the corridor's slide run, and the crouch-only crawlway.
    validate_hauler.py cannot see clutter -- it is spawned at runtime -- so
    this guard is enforced by ShipDressing::Dress itself."""
    out = []
    doors = 0
    for name, lo, hi in keep_clear:
        if name == "console":
            out.append(KeepOut("keepout_console", lo, hi))
        else:
            out.append(KeepOut("keepout_door_%d" % doors, lo, hi))
            doors += 1
    for label, room in (("keepout_slide_run", SLIDE_ROOM), ("keepout_crawlway", "crawlway")):
        r = plan.room(room)
        out.append(KeepOut(label, (r.x, r.y, 0), (r.x + r.w, r.y + r.d, r.height)))
    return out


def chart_exclude(plan, location, yaw):
    """Plan conflict 16 as a world-space floor rectangle ((x0, y0), (x1, y1)):
    the chart's footprint plus NAV_SCREEN_MARGIN, stretched aft to the chair
    it is read from. Slice 3's surfaces take it as an exclude on the cockpit
    desk, as they take the laptop's on the galley table. Only a chart facing
    aft (yaw 0) is supported: that is the one the layout has, and a turned
    chart would need its chair turned with it."""
    if yaw != 0:
        raise PlanError("the chart exclude assumes the chart faces aft (yaw 0), not %s" % yaw)
    x, y, _ = location
    chair_x, _, _ = resolve_point(plan, NAV_SCREEN_CHAIR[0], NAV_SCREEN_CHAIR[1])
    half = NAV_SCREEN_WIDTH / 2.0 + NAV_SCREEN_MARGIN
    return (min(x, chair_x) - NAV_SCREEN_MARGIN, y - half), (x + NAV_SCREEN_MARGIN, y + half)
