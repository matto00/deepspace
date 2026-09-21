"""
Geometry of the hauler interior, as plain data.

Deliberately free of any `unreal` import so that validation can run in a bare
python3 in about a second, instead of paying a 30-second editor startup. The
builder (`build_hauler.py`) consumes this inside the editor; the validator
(`validate_hauler.py`) consumes it outside.

Conventions: Unreal units are centimetres; X is forward, Y right, Z up.
SM_Cube is 100 units on a side with a centred pivot, so scale * 100 is the
size in cm and a slab's centre sits half its thickness off the surface.
"""

WALL = 0.1    # 10 cm thick
HEIGHT = 2.5  # 250 cm interior
MID = 125     # centre height of a full-height wall

# (label, centre, scale)
BOXES = [
    # --- Corridor: 150 wide, 800 long, running +X from the aft bulkhead.
    ("corridor_floor",      (400, 0, -5),      (8, 1.5, WALL)),
    ("corridor_ceiling",    (400, 0, 255),     (8, 1.5, WALL)),
    ("corridor_aft_wall",   (-5, 0, MID),      (WALL, 1.5, HEIGHT)),
    # Side walls double as each room's near wall; the gaps are the doorways.
    ("corridor_wall_r_1",   (150, 80, MID),    (3, WALL, HEIGHT)),
    ("corridor_wall_r_2",   (610, 80, MID),    (3.8, WALL, HEIGHT)),
    ("corridor_wall_l_1",   (140, -80, MID),   (2.8, WALL, HEIGHT)),
    ("corridor_wall_l_2",   (600, -80, MID),   (4, WALL, HEIGHT)),

    # --- Engineering: 300 x 400, off the corridor to starboard.
    ("eng_floor",           (350, 275, -5),    (3, 4, WALL)),
    ("eng_ceiling",         (350, 275, 255),   (3, 4, WALL)),
    ("eng_wall_aft",        (195, 275, MID),   (WALL, 4, HEIGHT)),
    ("eng_wall_fore",       (505, 275, MID),   (WALL, 4, HEIGHT)),
    ("eng_wall_far",        (350, 480, MID),   (3, WALL, HEIGHT)),

    # --- Bunk: 250 x 300, to port.
    ("bunk_floor",          (325, -225, -5),   (2.5, 3, WALL)),
    ("bunk_ceiling",        (325, -225, 255),  (2.5, 3, WALL)),
    ("bunk_wall_aft",       (195, -225, MID),  (WALL, 3, HEIGHT)),
    ("bunk_wall_fore",      (455, -225, MID),  (WALL, 3, HEIGHT)),
    ("bunk_wall_far",       (325, -380, MID),  (2.5, WALL, HEIGHT)),
    ("bunk_bed",            (250, -250, 25),   (0.9, 2, 0.5)),

    # --- Cockpit: 300 x 300 at the fore end.
    ("cockpit_floor",       (950, 0, -5),      (3, 3, WALL)),
    ("cockpit_ceiling",     (950, 0, 255),     (3, 3, WALL)),
    ("cockpit_wall_port",   (950, -155, MID),  (3, WALL, HEIGHT)),
    ("cockpit_wall_stbd",   (950, 155, MID),   (3, WALL, HEIGHT)),
    ("cockpit_wall_aft_p",  (795, -112, MID),  (WALL, 0.75, HEIGHT)),
    ("cockpit_wall_aft_s",  (795, 112, MID),   (WALL, 0.75, HEIGHT)),
    # Window is the Z=100..180 gap between these two.
    ("cockpit_wall_lower",  (1105, 0, 50),     (WALL, 3, 1.0)),
    ("cockpit_wall_upper",  (1105, 0, 215),    (WALL, 3, 0.7)),
    # Glass. Without it the hull is open to space, which the validator is
    # right to reject: a window is a hole until something fills it.
    ("cockpit_window",      (1105, 0, 140),    (WALL, 3, 0.8)),
]

# Interior lighting. The blockout has no windows but the one, so without these
# the ship is simply black inside. (x, y, z, intensity, radius)
LIGHTS = [
    (150, 0, 220, 6.0, 500),
    (450, 0, 220, 6.0, 500),
    (700, 0, 220, 6.0, 500),
    (350, 250, 220, 8.0, 700),
    (325, -250, 220, 5.0, 600),
    (950, 0, 220, 5.0, 700),
]

CONSOLE_LOC = (350, 470, 120)
# The panel's thin axis is X and its face points -X; yaw 90 turns it to -Y,
# so it faces into engineering from the far wall.
CONSOLE_ROT = (0, 0, 90)
PLAYER_START_LOC = (100, 0, 90)

# Places a player must be able to stand and walk between. Validation failures
# here mean rooms are sealed off or the doorways do not line up.
REGIONS = {
    "corridor_aft": (100, 0),
    "corridor_fore": (700, 0),
    "engineering": (350, 275),
    "console_front": (350, 420),
    "bunk": (325, -300),
    "cockpit": (950, 0),
}

# A standing player needs this much clear height, and the window should be
# somewhere near eye level.
PLAYER_HEIGHT = 180
EYE_HEIGHT = 64


def bounds(centre, scale):
    """Axis-aligned (min, max) corners in cm."""
    half = [s * 100.0 / 2.0 for s in scale]
    return (
        tuple(centre[i] - half[i] for i in range(3)),
        tuple(centre[i] + half[i] for i in range(3)),
    )
