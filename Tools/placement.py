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
