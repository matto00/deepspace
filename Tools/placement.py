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
