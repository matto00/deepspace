"""
Resolves everything that sits in a room -- furniture, lights, the console, the
Player Start, named regions -- from room-relative terms into world positions.

Nothing in the layout is a raw world coordinate. Every hand-computed coordinate
is one that can silently disagree with the surface it belongs on; milestone 1's
pivot bug put every wall half its own size away from its floor. Positions are
given relative to a room's minimum corner instead, and move with the room.

No `unreal` import.
"""

import json
import math
import os
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

# colour: sRGB 0..255, what a light component's colour is written as.
# shadows: whether it casts them. Neutral and unshadowed unless it says so.
Light = namedtuple("Light", "label location intensity radius colour shadows",
                   defaults=((255, 255, 255), False))

# A room's light: its colour temperature in kelvin, and a brightness scale on
# the ceiling grid's intensity.
Mood = namedtuple("Mood", "kelvin scale")

# A lamp somebody put there: a prop with a bulb at its head, and the point
# light that bulb gives. It burns at its room's kelvin, because its glowing
# part wears the room's lamp role; a practical of a different colour from its
# room would glow one colour and light the desk another. Intensity is in
# candelas, like the ceiling grid's.
Practical = namedtuple("Practical", "place radius intensity")

# The actor tag every light in the ship carries. The same string is
# ShipPower::Lights in C++: one identifier for the power consumer and for the
# actors that answer to it. test_placement.py reads the C++ to hold them equal.
LIGHTS_TAG = "Power.Lights"

# The actor tag every glowing lamp box carries: the ceiling panels and every
# prop part whose role is a lamp. ShipLighting::LampsTag in C++ finds them by
# it and dims their Colour with the lights, so a panel never glows full over
# a browned-out room. test_placement.py reads the C++ to hold them equal.
LAMPS_TAG = "Power.Lamps"

# The actor tag every glass box carries: the cockpit's panes and the galley's
# window. The target bracket is drawn only where the target can be seen
# through the glass, and its trace counts a hit on an actor with this tag as
# the glass, not a wall (system map spec, decision 7). ShipTags::Glass in C++;
# test_placement.py reads the C++ to hold them equal. A pane without it hides
# the bracket; anything else with it would show the bracket through a wall.
GLASS_TAG = "Sky.Glass"

# AWorldGround::GroundTag: how verify_level finds hauler_ground.
GROUND_TAG = "Sky.Ground"

# The sky's assets, as the level build assigns them and the verifier checks
# them. Named from Tools/sky_material_contract.json, the list the materials
# are authored from and SkyMaterialContract.h mirrors, so no path is typed
# twice; test_placement.py reads the C++ to hold the two equal.
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "sky_material_contract.json")) as _f:
    SKY_DIRECTORY = json.load(_f)["directory"]


def sky_package(name):
    """Where a sky asset lives: /Game/Materials/Sky/M_SkyStar."""
    return "%s/%s" % (SKY_DIRECTORY, name)


def sky_asset(name):
    """A sky asset's object path, as Unreal reports a material's path name:
    /Game/Materials/Sky/M_SkyStar.M_SkyStar."""
    return "%s.%s" % (sky_package(name), name)


# The one mesh everything outside the hull is drawn with: centre-pivoted,
# though nothing that uses it assumes so.
SPHERE = "/Engine/BasicShapes/Sphere"

LIGHT_SPACING = 300     # cm; rooms get ceil(size / spacing) lights per axis
LAMP_SIZE = 60          # cm; the emissive ceiling panel under each light
LAMP_DEPTH = 2
PANEL_DEPTH = 8         # cm; how far a wall-mounted panel's centre sits off the wall
LAMP_GLOW = 6.0         # emissive over 1 reads as a source under auto exposure


def _world(room, at):
    return room.x + at[0], room.y + at[1]


def lamp_role(room):
    """The material role of a room's lamp panels, and of any glowing part of a
    prop placed there. One per room, so each room's panels glow its own
    colour."""
    return "lamp_" + room


def kelvin_to_rgb(kelvin):
    """The colour of a blackbody at `kelvin`, as sRGB 0..255.

    Tanner Helland's curve fit to the CIE 1964 blackbody table: good to a few
    percent from 1000 K to 40000 K, which is more precision than a lamp
    needs. White is 6500 K, near enough; below it light runs to amber, above
    it to blue. Kept as sRGB because that is what a light's colour is written
    in, and what the verifier reads back.
    """
    t = kelvin / 100.0
    if t <= 66:
        r = 255.0
        g = 99.4708025861 * math.log(t) - 161.1195681661
    else:
        r = 329.698727446 * (t - 60) ** -0.1332047592
        g = 288.1221695283 * (t - 60) ** -0.0755148492
    if t >= 66:
        b = 255.0
    elif t <= 19:
        b = 0.0
    else:
        b = 138.5177312231 * math.log(t - 10) - 305.0447927307
    return tuple(int(round(min(255.0, max(0.0, c)))) for c in (r, g, b))


def srgb_to_linear(c):
    """One sRGB channel, 0..1, to linear. Unreal converts a light's FColor
    this way before it lights anything, so an emissive panel given the linear
    value glows the same colour as the light beneath it."""
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def lamp_emissive(mood):
    """A room's lamp panel colour: its mood's kelvin, in linear, bright enough
    to read as the source of the light, and dimmed with the room so a dim
    cockpit's panels are not the brightest thing in it."""
    return tuple(srgb_to_linear(c / 255.0) * LAMP_GLOW * mood.scale
                 for c in kelvin_to_rgb(mood.kelvin))


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
            # A prop's glowing part is the lamp of whatever room it stands
            # in: the template does not know where it will be placed.
            role = lamp_role(place.room) if part.role == "lamp" else part.role
            out.append(Box("prop_%s_%d_%d" % (place.prop, n, k), centre, size,
                           role, part.mesh))
        _check_anchor(place, n, room, out[len(out) - len(P.PROPS[place.prop]):])
    return out


def _check_anchor(place, n, room, parts):
    """A prop anchored to the ceiling or a wall must touch it (props.ANCHOR):
    a counter floating in the middle of the galley, or a ceiling panel lying
    on the floor, is a PlanError rather than a ship that validates."""
    anchor = P.ANCHOR.get(place.prop)
    if anchor is None:
        return
    eps = 0.01
    lo = [[b.centre[a] - b.size[a] / 2.0 for a in range(3)] for b in parts]
    hi = [[b.centre[a] + b.size[a] / 2.0 for a in range(3)] for b in parts]
    if anchor == "ceiling":
        ok = any(h[2] >= room.height - eps for h in hi)
    elif anchor == "wall":
        ok = any(l[0] <= room.x + eps or h[0] >= room.x + room.w - eps
                 or l[1] <= room.y + eps or h[1] >= room.y + room.d - eps
                 for l, h in zip(lo, hi))
    else:
        raise PlanError("prop '%s' has unknown anchor '%s'" % (place.prop, anchor))
    if not ok:
        raise PlanError("%s #%d hangs from the %s but touches no %s of room '%s'"
                        % (place.prop, n, anchor, anchor, place.room))


# -- dressing surfaces --------------------------------------------------------

# cm around anything standing on a surface that clutter may not cover: the
# laptop's margin, and the chart's (plan conflict 16). Enough that a mug is
# never pushed up against a lamp's base or a screen's edge as if placed there
# to crowd it.
DRESS_MARGIN = 15

# The actor tags the dressing generator finds the layout's exports by, the
# same strings as ShipDressing's in C++ (test_placement.py reads the C++).
SURFACE_TAG = "Dress.Surface"
KEEP_OUT_TAG = "Dress.KeepOut"
WEAR_TAG = "Dress.Wear"
PIECE_TAG_PREFIX = "Piece."

# One surface, resolved: the contract AShipDressingSurface carries to C++.
# location: the centre of its resting plane, world. yaw: its prop's facing.
# size, back, use, clear: as props.Surface, in the surface's own frame.
# excludes: surface-local ((x0, y0), (x1, y1)) rectangles nothing may cover.
SurfaceMarker = namedtuple("SurfaceMarker", "label room kind ordinal location yaw size "
                                            "back use clear excludes")

# A world-space box no clutter may touch: a door's or the console's keep-clear
# zone, the corridor's slide run, the crawlway.
KeepOut = namedtuple("KeepOut", "label lo hi")


def piece_of(label):
    """The furniture piece a prop box belongs to: prop_<prop>_<n>_<k> is
    part k of <prop>_<n>. Wear is drawn per piece, so a desk wears whole."""
    if not label.startswith("prop_"):
        raise ValueError("%s is not a prop part" % label)
    return label[len("prop_"):].rsplit("_", 1)[0]


def _ascii(*names):
    """Room and kind are hashed into seeds as ASCII bytes (GenSeed::LabelText);
    a name that depended on a text encoding would reshuffle the ship the day
    the encoding did."""
    for name in names:
        try:
            name.encode("ascii")
        except UnicodeEncodeError:
            raise PlanError("'%s' is not plain ASCII, and dressing seeds hash it as ASCII" % name)


def _intersect(a, b):
    (ax0, ay0), (ax1, ay1) = a
    (bx0, by0), (bx1, by1) = b
    lo = (max(ax0, bx0), max(ay0, by0))
    hi = (min(ax1, bx1), min(ay1, by1))
    if hi[0] - lo[0] <= 1e-6 or hi[1] - lo[1] <= 1e-6:
        return None
    return lo, hi


def resolve_surfaces(plan, placements, accepts, world_excludes=()):
    """Every dressing surface in the ship, as the markers build_hauler spawns.

    `placements` must be hauler_layout.PLACEMENTS, never FURNITURE: the lamps
    stand on surfaces and the counter is one. `accepts` is ROOM_DRESSING: a
    surface is exported only if its room takes its kind, so a room that takes
    nothing has nothing for the C++ to find. `world_excludes` are floor-plan
    rectangles ((x0, y0), (x1, y1)) laid over every surface under them: the
    laptop's and the chart's.

    Each surface also excludes every placement resting on it -- its lowest
    part's underside on the surface's plane -- by that placement's whole plan
    footprint plus DRESS_MARGIN. The laptop's rule made general, so a lamp
    moved in the layout takes its exclude with it. The whole footprint, not
    the base: a lamp's arm reaches out over the desk, and a stack of books
    under it would stand through the shade.
    """
    resolved, counts = [], {}
    for place in placements:
        n = counts.get(place.prop, 0)
        counts[place.prop] = n + 1
        resolved.append((place, n, resolve_props(plan, [place])))

    out = []
    for place, n, _ in resolved:
        for surface in P.SURFACES.get(place.prop, ()):
            kind = "%s.%s" % (place.prop, surface.name)
            if kind not in accepts.get(place.room, ()):
                continue
            _ascii(place.room, kind)
            room = plan.room(place.room)
            ox, oy = _world(room, place.at)
            oz = place.elevation + place.level * P.height(place.prop)
            (cx, cy, cz), (sx, sy, _) = P.rotate(surface.at, surface.size + (0,), place.facing)
            centre = (ox + cx, oy + cy, oz + cz)
            world_rect = ((centre[0] - sx / 2.0, centre[1] - sy / 2.0),
                          (centre[0] + sx / 2.0, centre[1] + sy / 2.0))
            local_rect = ((-surface.size[0] / 2.0, -surface.size[1] / 2.0),
                          (surface.size[0] / 2.0, surface.size[1] / 2.0))

            # The prop's own parts on the surface, given prop-local.
            excludes = []
            for lo, hi in surface.exclude:
                clipped = _intersect(((lo[0] - surface.at[0], lo[1] - surface.at[1]),
                                      (hi[0] - surface.at[0], hi[1] - surface.at[1])), local_rect)
                if clipped:
                    excludes.append(clipped)

            # Everything else standing on it, and the laptop and the chart.
            laid = list(world_excludes)
            for other, m, parts in resolved:
                if (other.prop, m) == (place.prop, n):
                    continue
                low = min(parts, key=lambda b: b.centre[2] - b.size[2] / 2.0)
                bottom = low.centre[2] - low.size[2] / 2.0
                (x0, y0), (x1, y1) = world_rect
                if (abs(bottom - centre[2]) < 0.01
                        and x0 < low.centre[0] < x1 and y0 < low.centre[1] < y1):
                    laid.append((
                        (min(b.centre[0] - b.size[0] / 2.0 for b in parts) - DRESS_MARGIN,
                         min(b.centre[1] - b.size[1] / 2.0 for b in parts) - DRESS_MARGIN),
                        (max(b.centre[0] + b.size[0] / 2.0 for b in parts) + DRESS_MARGIN,
                         max(b.centre[1] + b.size[1] / 2.0 for b in parts) + DRESS_MARGIN)))
            back = (360 - place.facing) % 360
            for rect in laid:
                hit = _intersect(rect, world_rect)
                if not hit:
                    continue
                lo, hi = hit
                local = P.rotate_rect((lo[0] - centre[0], lo[1] - centre[1]),
                                      (hi[0] - centre[0], hi[1] - centre[1]), back)
                clipped = _intersect(local, local_rect)
                if clipped:
                    excludes.append(clipped)

            out.append(SurfaceMarker(
                "surface_%s_%d_%s" % (place.prop, n, surface.name), place.room, kind, n,
                centre, place.facing, tuple(surface.size), surface.back, surface.use,
                surface.clear, tuple(sorted(excludes))))
    return out


def resolve_lights(plan, moods):
    """A light on a grid in every room, each with an emissive panel on the
    ceiling above it, so the ship reads as lit by fixtures rather than by
    nothing. Each room's grid burns at its mood's colour and brightness.
    Returns (lights, lamp boxes). Raises PlanError for a room with no mood:
    a room nobody chose a light for would silently be lit neutral white."""
    lights, lamps = [], []
    for name in sorted(plan.rooms):
        if name not in moods:
            raise PlanError("room '%s' has no mood" % name)
        mood = moods[name]
        colour = kelvin_to_rgb(mood.kelvin)
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
                # Unshadowed fill: many overlapping lights, none casting
                # shadows, so the few practicals that do are what reads.
                lights.append(Light("light_" + label, (x, y, r.height - 20),
                                    3.0 * r.height / 250.0 * mood.scale, reach,
                                    colour, False))
                lamps.append(Box("lamp_" + label, (x, y, r.height - LAMP_DEPTH / 2.0),
                                 (LAMP_SIZE, LAMP_SIZE, LAMP_DEPTH), lamp_role(name)))
    return lights, lamps


def resolve_practicals(plan, practicals, moods):
    """Each practical's prop placement, and the shadowed light at its head.

    The head is a prop-local point in props.HEADS, turned and lifted with the
    prop, so moving the lamp moves its light. Returns (placements, lights).
    Raises PlanError for a prop with no head, or two practicals that would
    share a light's label.
    """
    places, lights, labels = [], [], set()
    for practical in practicals:
        place = practical.place
        if place.prop not in P.HEADS:
            raise PlanError("practical '%s' has no head in props.HEADS" % place.prop)
        if place.room not in moods:
            raise PlanError("room '%s' has no mood" % place.room)
        label = "light_practical_%s_%s" % (place.room, place.prop)
        if label in labels:
            raise PlanError("two practicals are '%s'" % label)
        labels.add(label)
        room = plan.room(place.room)
        ox, oy = _world(room, place.at)
        oz = place.elevation + place.level * P.height(place.prop)
        (hx, hy, hz), _ = P.rotate(P.HEADS[place.prop], (0, 0, 0), place.facing)
        places.append(place)
        # The only lights that cast shadows: a few strong, directional
        # sources are what make a room read as lit, and what sets a thing on
        # a table down onto it.
        lights.append(Light(label, (ox + hx, oy + hy, oz + hz), practical.intensity,
                            practical.radius, kelvin_to_rgb(moods[place.room].kelvin),
                            True))
    return places, lights


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
