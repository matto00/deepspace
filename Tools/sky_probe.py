#!/usr/bin/env python3
"""The sky's numbers, before anyone looks: an approach flown under the drive.

Pure Python, no editor. For a system and a target planet it prints, from the
arrival standoff down to the rendered-altitude floor, what the player sees at
each step: true angular size and pixels, the resolve blend, the proxy's
stacked distance and the depth budget the whole system uses, how far the
eye's offset in the hull slides the body against the stars, the honest
surface brightness and the point boost, the ship's speed, and how long since
the drive's lever went to 1 c.

It mirrors SkyProjection.cpp's arithmetic (the homothety, the power-order
stack, the photometry) and the drive's law from the flight-feel spec
(decisions 3-6) --

    the lever from STOP to its top, 1 c, eased in notch space at
    ds.Drive.Response, 3 notches a second (ShipDriveLever::Ease);
    the soft cap on the nose's own ray to the nearest floor sphere,
    speed <= min(max(d / 4 s, sqrt(1.6 a d)), d / step) (ShipFlight::MaySpeed);
    the floor the sky's own, max(10 km, 1.6e-3 R), one radius over a star

-- flown at 120 Hz substeps as FShipFlightState flies it, and the arrival
standoff of plan conflict 9,

    Standoff = max(StandoffAU * sqrt(L), 1.5 * outermost orbit).

Two systems. HOME is what the developer's honest weights make most often: a
red dwarf (three suns in four, procgen spec) with a compact system, the
largest planet chosen for the opening view. SUNLIKE is the sky spec's own
reference, a Sun with an Earth at 1 AU. Either table can be checked against
the flight-feel spec's decision 5: the leg at the lever's speed, then the
cap's last minute, about 64 s from where it binds to the floor at 1 c.

    python3 Tools/sky_probe.py
    python3 Tools/sky_probe.py --night

--night adds the .03 AU question's numbers (the system map spec, decision
8), for the geometry DeepSpace.Sky.NightSideIsDrawn draws: a Sun and an
Earth at 1 AU, the ship 0.03 AU beyond the Earth on the sun-planet line and
0.002 AU to one side, looking back at it with the Sun behind it, on a view
103 degrees across at 3840 px -- the helm's on the developer's display. For
that and a few distances either side it prints the Earth's pixels, the phase
angle, the Lambert phase and what it leaves of full brightness, the lit
fraction the target line's NIGHT SIDE is judged on, and how far the world
is from the Sun's centre. Whether those pixels can be *seen* is a question
for eyes: in play, ds.Sky.Goto <world> 4500000 night, then ds.Nav.Target
<world>.
"""

import math
import sys

CM_PER_KM = 1.0e5
CM_PER_AU = 1.495978707e13
CM_PER_SOLAR_RADIUS = 6.957e10
CM_PER_EARTH_RADIUS = 6.3781e8
SOLAR_T = 5772.0

# FSkyViewParams defaults.
PIXEL_ANGLE = 2.0 / 1920.0
PIXEL_4K = 2.0 / 3840.0
NEAR_PROXY = 5.0e6
FAR_PROXY = 1.25e10
STACK_GAP = 1.02
MIN_POINT_PIXELS = 2.0
RESOLVE_BAND_PIXELS = 2.0
FLUX_GAMMA = 0.5
MIN_ALTITUDE = 1.0e6
MIN_ALTITUDE_OF_RADIUS = 1.6e-3

# The drive (flight-feel decisions 3-6) and the arrival (plan conflict 9).
# ShipDriveLever.cpp's table, cm/s, STOP not included: a 1-2-5 series in km/s
# to 2,000, then in fractions of light to light itself.
LIGHT = 2.99792458e10
NOTCHES = [v * CM_PER_KM for v in (1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000)] + \
          [f * LIGHT for f in (0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0)]
EASE_SECONDS = 0.4
SETTLE_NOTCHES = 1.0e-3
RESPONSE = 3.0                  # ds.Drive.Response, notches/s
HOLD_SECONDS = 4.0              # ds.Drive.HoldSeconds
BRAKING_MARGIN = 0.8            # ShipFlight::BrakingMargin
BOOSTERS = 4000.0               # cm/s^2, full thrust
FLIGHT_FLOOR = 1.0e6            # ds.Flight.Floor, 10 km
STEP = 1.0 / 120.0              # FShipFlightState::FixedStep
AT_FLOOR = 100.0                # FShipFlightState::AtFloorCm
STANDOFF_AU = 2.4

# The farthest the eye gets from the ship's origin: the cockpit glass.
EYE_OFFSET = 1760.0


def body(name, kind, pos_au, radius_cm, albedo=0.3, luminosity=1.0, temperature=SOLAR_T):
    return dict(name=name, kind=kind, pos=[c * CM_PER_AU for c in pos_au], radius=radius_cm,
                albedo=albedo, luminosity=luminosity, temperature=temperature)


def home_system():
    """A red dwarf as procgen's priors make one: 0.2 solar masses, so
    L = 0.23 M^2.3, R = M^0.8, and a compact system inside 0.1 AU."""
    mass = 0.2
    luminosity = 0.23 * mass ** 2.3
    star = body("home star (M, %.4f Lsun)" % luminosity, "star", (0, 0, 0),
                mass ** 0.8 * CM_PER_SOLAR_RADIUS, luminosity=luminosity, temperature=3200.0)
    planets = [
        body("I (barren)", "planet", (0.025 * math.cos(0.4), 0.025 * math.sin(0.4), 0), 0.6 * CM_PER_EARTH_RADIUS, 0.12),
        body("II (terrestrial)", "planet", (0.040 * math.cos(2.1), 0.040 * math.sin(2.1), 0), 1.1 * CM_PER_EARTH_RADIUS, 0.30),
        body("III (barren)", "planet", (0.065 * math.cos(4.0), 0.065 * math.sin(4.0), 0), 0.8 * CM_PER_EARTH_RADIUS, 0.12),
    ]
    return [star] + planets, 2   # the largest planet is the opening's


def sunlike_system():
    star = body("Sun", "star", (0, 0, 0), CM_PER_SOLAR_RADIUS)
    planets = [
        body("Mercury-like", "planet", (0.39 * math.cos(3.5), 0.39 * math.sin(3.5), 0), 0.383 * CM_PER_EARTH_RADIUS, 0.12),
        body("Earth-like", "planet", (1.0, 0, 0), CM_PER_EARTH_RADIUS, 0.30),
        body("Jupiter-like", "planet", (5.2 * math.cos(1.0), 5.2 * math.sin(1.0), 0.1), 10.97 * CM_PER_EARTH_RADIUS, 0.50),
    ]
    return [star] + planets, 2


def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def norm(a):
    return math.sqrt(sum(c * c for c in a))


def cone(angular_radius):
    return 4.0 * math.pi * math.sin(0.5 * angular_radius) ** 2


def lambert_phase(alpha):
    alpha = min(max(alpha, 0.0), math.pi)
    return (math.sin(alpha) + (math.pi - alpha) * math.cos(alpha)) / math.pi


def project(bodies, ship):
    """SkyProjection::Project, for the numbers the probe prints."""
    star = next(b for b in bodies if b["kind"] == "star")
    min_drawn = 0.5 * MIN_POINT_PIXELS * PIXEL_ANGLE
    views = []
    for b in bodies:
        delta = sub(b["pos"], ship)
        d = norm(delta)
        floor = max(MIN_ALTITUDE, MIN_ALTITUDE_OF_RADIUS * b["radius"])
        dr = max(d, b["radius"] + floor)
        s = b["radius"] / dr
        radius = math.asin(s)
        inflated = radius < min_drawn
        drawn = min_drawn if inflated else radius
        sin_drawn = math.sin(drawn) if inflated else s
        near = 1.0 - sin_drawn if inflated else (dr - b["radius"]) / dr
        pixels = 2.0 * radius / PIXEL_ANGLE
        t = min(max((pixels - MIN_POINT_PIXELS) / RESOLVE_BAND_PIXELS, 0.0), 1.0)
        blend = 1.0 - t * t * (3.0 - 2.0 * t)
        if b["kind"] == "star":
            surface = (b["temperature"] / SOLAR_T) ** (4 * FLUX_GAMMA)
            phase = 1.0
        else:
            to_star = sub(star["pos"], b["pos"])
            ds = norm(to_star)
            surface = b["albedo"] * (star["luminosity"] / (ds / CM_PER_AU) ** 2) ** FLUX_GAMMA
            to_ship = [-c / d for c in delta]
            cos_alpha = sum(to_star[i] / ds * to_ship[i] for i in range(3))
            phase = lambert_phase(math.acos(min(max(cos_alpha, -1.0), 1.0)))
        boost = (cone(drawn) / cone(radius)) ** (1.0 - FLUX_GAMMA) if inflated else 1.0
        views.append(dict(body=b, d=d, radius=radius, pixels=pixels, blend=blend, surface=surface,
                          boost=boost, phase=phase, sin=sin_drawn, near=near, power=dr * dr - b["radius"] ** 2))
    cursor = NEAR_PROXY
    for v in sorted(views, key=lambda v: v["power"]):
        v["centre"] = cursor / v["near"]
        v["proxy_near"] = cursor
        v["proxy_far"] = v["centre"] * (1.0 + v["sin"])
        cursor = v["proxy_far"] * STACK_GAP
    budget = max(v["proxy_far"] for v in views) / NEAR_PROXY
    return views, budget


def parallax_pixels(view):
    """How far the eye at the cockpit glass sees the proxy's centre from the
    true body's, in 4K pixels: e / D_proxy against e / d_true, sideways."""
    e = EYE_OFFSET
    return abs(math.atan2(e, view["centre"]) - math.atan2(e, view["d"])) / PIXEL_4K


def speed_at(p):
    """ShipDriveLever::SpeedAt: linear from STOP, geometric between notches."""
    if p <= 0.0:
        return 0.0
    if p >= len(NOTCHES):
        return NOTCHES[-1]
    below = int(math.floor(p))
    frac = p - below
    if below == 0:
        return frac * NOTCHES[0]
    return NOTCHES[below - 1] * (NOTCHES[below] / NOTCHES[below - 1]) ** frac


def position_of(v):
    """ShipDriveLever::PositionOf, the inverse of speed_at."""
    if v <= 0.0:
        return 0.0
    if v >= NOTCHES[-1]:
        return float(len(NOTCHES))
    if v < NOTCHES[0]:
        return v / NOTCHES[0]
    below = 1
    while below + 1 < len(NOTCHES) and NOTCHES[below] <= v:
        below += 1
    return below + math.log(v / NOTCHES[below - 1]) / math.log(NOTCHES[below] / NOTCHES[below - 1])


def ease(p, target, dt, rate=RESPONSE, thrust=1.0):
    """ShipDriveLever::Ease: the rate limit, then the exponential, solved."""
    dt *= thrust
    start = abs(target - p)
    sign = 1.0 if target > p else -1.0
    error, left, knee = start, dt, rate * EASE_SECONDS
    if error > knee:
        to_knee = (error - knee) / rate
        if left <= to_knee:
            error, left = error - rate * left, 0.0
        else:
            error, left = knee, left - to_knee
    if left > 0.0:
        error *= math.exp(-left / EASE_SECONDS)
    if error <= SETTLE_NOTCHES and start <= rate * dt:
        return target
    return target - sign * error


def may_speed(d, a=BOOSTERS):
    """ShipFlight::MaySpeed: the hold, the braking curve, the substep bound."""
    if d <= 0.0:
        return 0.0
    return min(max(d / HOLD_SECONDS, math.sqrt(2.0 * BRAKING_MARGIN * a * d)), d / STEP)


def floor_of(b):
    """UShipSubsystem::FloorFor at the default CVars."""
    if b["kind"] == "star":
        return b["radius"]
    return max(FLIGHT_FLOOR, MIN_ALTITUDE_OF_RADIUS * b["radius"])


def ray_to_floor(b, ship, u):
    """ShipFlight::RayToFloor for a body: the near root, 0 on or under it
    heading in, None on a miss or heading away."""
    to_centre = sub(b["pos"], ship)
    dist = norm(to_centre)
    floor = b["radius"] + floor_of(b)
    along = sum(u[i] * to_centre[i] for i in range(3))
    if dist <= floor:
        return 0.0 if along > 0.0 else None
    chord = floor * floor - max(dist * dist - along * along, 0.0)
    if along <= 0.0 or chord < 0.0:
        return None
    return (dist - floor) * (dist + floor) / (along + math.sqrt(chord))


def fly(bodies, ship0, line, target):
    """The lever from STOP to 1 c at t = 0, the nose fixed on the target:
    (t, altitude, speed) at every substep while anything is happening, and
    once across each stretch at the top where nothing binds -- a straight
    run at constant speed, taken in one step rather than a million.
    Returns the track and when the cap first bound.

    The cap reads the target's floor sphere alone: the probe's line is taken
    to be clear, as a pilot would make it. The game reads every body, and a
    star on the line would hold the ship at its floor instead."""
    top = float(len(NOTCHES))
    p, s, t = 0.0, 0.0, 0.0
    binds = None
    track = []
    while t < 7.2e5:
        ship = [ship0[i] + line[i] * s for i in range(3)]
        d = ray_to_floor(target, ship, line)
        d = float("inf") if d is None else d
        altitude = norm(sub(target["pos"], ship)) - target["radius"]
        track.append((t, altitude, speed_at(p)))
        if altitude - floor_of(target) <= AT_FLOOR:
            break
        if p == top and d > 2.0 * LIGHT * HOLD_SECONDS:
            run = (d - 1.5 * LIGHT * HOLD_SECONDS) / LIGHT
            s, t = s + LIGHT * run, t + run
            continue
        p = ease(p, top, STEP)
        v = speed_at(p)
        cap = may_speed(d)
        if cap < v:
            v, p = cap, position_of(cap)
            if binds is None:
                binds = t
        s += v * STEP
        t += STEP
    return track, binds


def approach(title, bodies, target_index):
    star = bodies[0]
    target = bodies[target_index]
    outermost = max(norm(sub(b["pos"], star["pos"])) for b in bodies[1:])
    standoff = max(STANDOFF_AU * math.sqrt(star["luminosity"]) * CM_PER_AU, 1.5 * outermost)

    # Arrive at the standoff on the far side of the star from the target's
    # direction rotated 90 degrees, as good as any, and fly straight at it.
    tx, ty = target["pos"][0], target["pos"][1]
    r = math.hypot(tx, ty)
    ship0 = [-ty / r * standoff, tx / r * standoff, 0.0]
    to_target = sub(target["pos"], ship0)
    line = [c / norm(to_target) for c in to_target]

    print("=" * 118)
    print("%s -- arrival standoff %.3f AU (%s)" % (
        title, standoff / CM_PER_AU,
        "sqrt(L) term" if standoff > 1.5 * outermost + 1 else "1.5 x outermost orbit"))
    print("target %s, radius %.0f km, %.4f AU from its star; the drive's lever to 1 c, floor %.1f km" % (
        target["name"], target["radius"] / CM_PER_KM, norm(sub(target["pos"], star["pos"])) / CM_PER_AU,
        floor_of(target) / CM_PER_KM))
    print("the star from arrival: %.2f px, irradiance %.3f (compressed; 1 = a Sun at 1 AU)" % (
        2.0 * math.asin(star["radius"] / standoff) / PIXEL_ANGLE,
        (star["luminosity"] / (standoff / CM_PER_AU) ** 2) ** FLUX_GAMMA))
    print("-" * 118)
    print("%10s %12s %10s %8s %6s %12s %8s %9s %9s %7s %12s" % (
        "t (s)", "distance", "altitude", "px", "blend", "proxy near", "budget", "parallax",
        "surface", "boost", "speed"))

    track, binds = fly(bodies, ship0, line, target)
    start = norm(to_target)
    resolved_at = None
    altitude = start - target["radius"]
    index = 0
    while True:
        # The moment the flight comes down through this altitude: between
        # the two samples either side of it, which across a run at the top is
        # exact, the speed being constant there.
        while index < len(track) - 1 and track[index][1] > altitude:
            index += 1
        seconds, flown, speed = track[index]
        if index > 0 and flown < altitude:
            before, above, _ = track[index - 1]
            seconds = before + (seconds - before) * (above - altitude) / (above - flown)
            flown = altitude
        ship = [ship0[i] + line[i] * (start - target["radius"] - flown) for i in range(3)]
        views, budget = project(bodies, ship)
        v = next(v for v in views if v["body"] is target)
        if resolved_at is None and v["pixels"] >= MIN_POINT_PIXELS:
            resolved_at = seconds
        print("%10.1f %9.4g km %7.4g km %8.3g %6.2f %9.4g km %7.0fx %7.2f px %9.4g %7.2f %7.4g km/s" % (
            seconds, v["d"] / CM_PER_KM, flown / CM_PER_KM, v["pixels"], v["blend"],
            v["proxy_near"] / CM_PER_KM, budget, parallax_pixels(v), v["surface"], v["boost"], speed / CM_PER_KM))
        altitude /= 3.0
        if altitude < 1.5 * floor_of(target):
            break
    print("-" * 118)
    print("resolves (2 px) %.0f s after the lever goes to 1 c; the cap binds at %.0f s; on the %.1f km floor at %.0f s" % (
        resolved_at if resolved_at is not None else float("nan"), binds if binds is not None else float("nan"),
        floor_of(target) / CM_PER_KM, track[-1][0]))

    # The rendered floor, where the depth budget is tightest.
    floor_alt = max(MIN_ALTITUDE, MIN_ALTITUDE_OF_RADIUS * target["radius"])
    ship = [target["pos"][i] - line[i] * (target["radius"] + floor_alt) for i in range(3)]
    views, budget = project(bodies, ship)
    print("at the rendered floor (%.1f km): the system uses %.0fx of the %.0fx band" % (
        floor_alt / CM_PER_KM, budget, FAR_PROXY / NEAR_PROXY))


def opening(title, bodies, target_index):
    """The opening framing of plan conflict 3: the largest planet 40,000 km
    dead ahead, its star 90 degrees to starboard."""
    target = bodies[target_index]
    star = bodies[0]
    to_star = sub(star["pos"], target["pos"])
    u = [c / norm(to_star) for c in to_star]
    side = [-u[1], u[0], 0.0]
    ship = [target["pos"][i] - side[i] * 4.0e9 for i in range(3)]
    views, budget = project(bodies, ship)
    v = next(v for v in views if v["body"] is target)
    s = next(v for v in views if v["body"] is star)
    print("%s opening: %s subtends %.1f deg (%s), half lit (phase %.2f); the star %.1f px; budget %.0fx" % (
        title, target["name"], math.degrees(2.0 * v["radius"]), "a world" if v["blend"] == 0.0 else "a point",
        v["phase"], s["pixels"], budget))


# The helm's view on the developer's display (system map spec, Context):
# 103 degrees across 3840 pixels, which at the view's centre, where a
# perspective view's pixels are widest apart in angle, is 26.7 px a degree.
HELM_PIXEL_ANGLE = 2.0 * math.tan(0.5 * math.radians(103.0)) / 3840.0

# TargetMarker::NightSideLit: under this share of its disc lit, the target
# line says NIGHT SIDE.
NIGHT_SIDE_LIT = 0.15


def night():
    """The .03 AU geometry and its neighbours: the Earth between the ship
    and the Sun, a little to one side, seen dark in the glare."""
    bodies, _ = sunlike_system()
    star = bodies[0]
    earth = next(b for b in bodies if b["name"] == "Earth-like")
    side_per_au = 0.002 / 0.03       # the fixture's sideways offset, as a slope
    print("=" * 118)
    print("NIGHT SIDE -- a Sun and an Earth at 1 AU, the ship beyond it on the sun-planet line, "
          "%.1f deg to one side; the helm's %.1f px a degree" % (
              math.degrees(math.atan(side_per_au)), 1.0 / math.degrees(HELM_PIXEL_ANGLE)))
    print("-" * 118)
    print("%10s %12s %8s %8s %8s %10s %8s %6s %10s" % (
        "distance", "", "px", "phase", "Lambert", "of full", "lit", "night", "from sun"))
    for au in (0.003, 0.01, 0.03, 0.1, 0.3):
        ship = [earth["pos"][0] + au * CM_PER_AU, earth["pos"][1] + au * side_per_au * CM_PER_AU, earth["pos"][2]]
        to_earth = sub(earth["pos"], ship)
        to_sun = sub(star["pos"], ship)
        d = norm(to_earth)
        pixels = 2.0 * math.asin(earth["radius"] / d) / HELM_PIXEL_ANGLE
        # The phase angle is at the world, between the star and the ship.
        sunward = sub(star["pos"], earth["pos"])
        shipward = sub(ship, earth["pos"])
        cos_alpha = sum(sunward[i] * shipward[i] for i in range(3)) / (norm(sunward) * norm(shipward))
        alpha = math.acos(min(max(cos_alpha, -1.0), 1.0))
        phase = lambert_phase(alpha)
        lit = 0.5 * (1.0 + math.cos(alpha))
        cos_sep = sum(to_earth[i] * to_sun[i] for i in range(3)) / (d * norm(to_sun))
        separation = math.degrees(math.acos(min(max(cos_sep, -1.0), 1.0)))
        print("%7.3f AU %9.4g km %8.2f %7.1f° %8.2g %9.2g%% %8.3f %6s %9.2f°" % (
            au, d / CM_PER_KM, pixels, math.degrees(alpha), phase, 100.0 * phase, lit,
            "yes" if lit < NIGHT_SIDE_LIT else "no", separation))
    print("-" * 118)
    print("'of full' is the Lambert phase against full phase from the same distance: what the disc gives back.")


def main(argv):
    for title, (bodies, target) in (("HOME, a red dwarf", home_system()), ("SUNLIKE", sunlike_system())):
        opening(title, bodies, target)
        approach(title, bodies, target)
        print()
    if "--night" in argv[1:]:
        night()


if __name__ == "__main__":
    main(sys.argv)
