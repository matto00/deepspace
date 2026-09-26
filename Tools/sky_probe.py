#!/usr/bin/env python3
"""The sky's numbers, before anyone looks: an approach flown under the drive.

Pure Python, no editor. For a system and a target planet it prints, from the
arrival standoff down to the rendered-altitude floor, what the player sees at
each step: true angular size and pixels, the resolve blend, the proxy's
stacked distance and the depth budget the whole system uses, how far the
eye's offset in the hull slides the body against the stars, the honest
surface brightness and the point boost, and how long the drive has been open.

It mirrors SkyProjection.cpp's arithmetic (the homothety, the power-order
stack, the photometry) and the drive's law from sky decision 8 --

    Room = max(0, NearestSurfaceDistance - DriveFloor)
    room falls as exp(-t / DriveTau) at full throttle

-- and the arrival standoff of plan conflict 9,

    Standoff = max(StandoffAU * sqrt(L), 1.5 * outermost orbit).

Two systems. HOME is what the developer's honest weights make most often: a
red dwarf (three suns in four, procgen spec) with a compact system, the
largest planet chosen for the opening view. SUNLIKE is the spec's own
reference, a Sun with an Earth at 1 AU, so the table can be checked against
the spec's "a point for the first 37 seconds, a world for the next 85".

    python3 Tools/sky_probe.py
"""

import math

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

# The drive (sky decision 8) and the arrival (plan conflict 9).
DRIVE_TAU = 15.0
DRIVE_FLOOR = 1.0e7
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


def nearest_surface(bodies, ship):
    return min(norm(sub(b["pos"], ship)) - b["radius"] for b in bodies)


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
    print("target %s, radius %.0f km, %.4f AU from its star; drive tau %.0f s, floor %.0f km" % (
        target["name"], target["radius"] / CM_PER_KM, norm(sub(target["pos"], star["pos"])) / CM_PER_AU,
        DRIVE_TAU, DRIVE_FLOOR / CM_PER_KM))
    print("the star from arrival: %.2f px, irradiance %.3f (compressed; 1 = a Sun at 1 AU)" % (
        2.0 * math.asin(star["radius"] / standoff) / PIXEL_ANGLE,
        (star["luminosity"] / (standoff / CM_PER_AU) ** 2) ** FLUX_GAMMA))
    print("-" * 118)
    print("%10s %12s %10s %8s %6s %12s %8s %9s %9s %7s %9s" % (
        "t (s)", "distance", "altitude", "px", "blend", "proxy near", "budget", "parallax",
        "surface", "boost", "flux"))

    start = norm(to_target)
    floor_room = max(nearest_surface(bodies, ship0) - DRIVE_FLOOR, 1.0)
    resolved_at = None
    rows = 0
    altitude = start - target["radius"]
    while True:
        ship = [ship0[i] + line[i] * (start - target["radius"] - altitude) for i in range(3)]
        room = max(nearest_surface(bodies, ship) - DRIVE_FLOOR, 1.0)
        seconds = DRIVE_TAU * math.log(floor_room / room)
        views, budget = project(bodies, ship)
        v = next(v for v in views if v["body"] is target)
        if resolved_at is None and v["pixels"] >= MIN_POINT_PIXELS:
            resolved_at = seconds
        flux = v["surface"] * v["phase"] * cone(max(v["radius"], 0.5 * MIN_POINT_PIXELS * PIXEL_ANGLE)) \
            * (cone(v["radius"]) / cone(max(v["radius"], 0.5 * MIN_POINT_PIXELS * PIXEL_ANGLE))) ** FLUX_GAMMA
        print("%10.1f %9.4g km %7.4g km %8.3g %6.2f %9.4g km %7.0fx %7.2f px %9.4g %7.2f %9.3g" % (
            seconds, v["d"] / CM_PER_KM, altitude / CM_PER_KM, v["pixels"], v["blend"],
            v["proxy_near"] / CM_PER_KM, budget, parallax_pixels(v), v["surface"], v["boost"], flux))
        rows += 1
        # The drive approaches its floor exponentially and never reaches it:
        # the last few km are cruise, and a row there would be a time to
        # infinity.
        altitude /= 3.0
        if altitude < 1.5 * DRIVE_FLOOR:
            break
    drive_end = DRIVE_TAU * math.log(floor_room / max(nearest_surface(bodies, [target["pos"][i] - line[i] * (target["radius"] + 4.0e9) for i in range(3)]) - DRIVE_FLOOR, 1.0))
    print("-" * 118)
    print("resolves (2 px) %.0f s after the drive opens; 40,000 km altitude at about %.0f s; "
          "the drive's room is spent at %.0f km" % (resolved_at or float("nan"), drive_end, DRIVE_FLOOR / CM_PER_KM))

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


def main():
    for title, (bodies, target) in (("HOME, a red dwarf", home_system()), ("SUNLIKE", sunlike_system())):
        opening(title, bodies, target)
        approach(title, bodies, target)
        print()


if __name__ == "__main__":
    main()
