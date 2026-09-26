#pragma once

#include "CoreMinimal.h"
#include "Universe/StarSystem.h"
#include "Universe/UniversePosition.h"
#include "Universe/UniverseUnits.h"

/** Where to put the ship, and which way it faces. */
struct FNavPlacement
{
    FUniversePosition Position;
    FQuat Orientation = FQuat::Identity;
};

/**
 * Where the ship is put when it is not flown there: the opening shot, and the
 * arrival at the end of a jump. Pure geometry on a system passed in, so both
 * are tested with no world; UShipSubsystem is the one caller of each (plan
 * conflicts 3 and 9).
 */
namespace NavStart
{
    /**
     * The opening planet's distance, centre to ship, per Earth radius of the
     * planet: an Earth at 40,000 km (sky decision 7). The framing is an
     * angle, not a distance -- an 18 degree world that fills the cockpit
     * glass -- and 40,000 km is that angle for an Earth. Held at a fixed
     * distance, a gas giant of 11 Earth radii, 70,000 km in radius, would
     * open with the ship inside it, and a Mars would be half the world the
     * shot was framed for. The altitude is 84% of the distance for any
     * world, so no planet opens anywhere near the drive's floor.
     */
    inline constexpr double OpeningDistancePerEarthRadiusCm = 40000.0 * UniverseUnits::CmPerKm;

    /** The distance the opening shot puts Planet's centre from the ship, cm. */
    DEEPSPACE_API double OpeningDistanceCm(const FPlanet& Planet);

    /**
     * The nearest a jump ever lets go of a star, in its own radii: ten
     * radii shows it 11 degrees across, a wall of light. The rule below
     * never comes close to this for any real star -- a dim M dwarf is met
     * at hundreds of radii -- so this is the guard that a star with no
     * light, or a rule mistuned in play, is met outside it and never at
     * its centre.
     */
    inline constexpr double MinStandoffStellarRadii = 10.0;

    /** The default of ds.Nav.StandoffAU: a Sun-like star is met at 2.4 AU
     *  as a disc of about 7 px. */
    inline constexpr double DefaultStandoffAU = 2.4;

    /**
     * The opening shot (plan conflict 3). The system's largest planet dead
     * ahead along the ship's +X, OpeningDistanceCm from its centre, and its
     * star 90 degrees to starboard (+Y), so the planet is half lit -- a
     * terminator, not a full disc -- and the sun off to the side lays a patch
     * of light across the galley. The ship's up is the system's up, so the
     * orbits lie level across the glass.
     *
     * The largest because the biggest thing in the window is the best first
     * shot; ties go to the inner orbit. A system with no planets, which home
     * never is, gets the arrival placement instead: the star dead ahead from
     * the standoff.
     */
    DEEPSPACE_API FNavPlacement OpeningPlacement(const FStarSystem& System);

    /**
     * How far from its star a jump lets go, AU (plan conflict 9):
     * max(StandoffAU * sqrt(L), 1.5 * outermost orbit).
     *
     * The square root because it holds the irradiance at arrival constant:
     * every star is met as bright as the Sun at StandoffAU, so a red dwarf
     * at L = 0.01 is met at 0.24 AU as a disc of fifteen pixels rather than a
     * two-pixel point, and three arrivals in four are not "nothing happened".
     * The outer bound is "to a system, never into one": the fold never puts
     * you among the planets.
     */
    DEEPSPACE_API double ArrivalStandoffAU(double LuminositySolar, double OutermostOrbitAU,
                                           double StandoffAU = DefaultStandoffAU);

    /** The same, for a system: its star's luminosity and its outermost
     *  planet's orbit (0 with no planets), and never nearer the star than
     *  MinStandoffStellarRadii of its radius. */
    DEEPSPACE_API double ArrivalStandoffAU(const FStarSystem& System,
                                           double StandoffAU = DefaultStandoffAU);

    /**
     * Where a jump from From to Destination arrives: on the line from From
     * to the star, the standoff short of it. On that line, so the star is in
     * the same direction from the arrival as it was when the fold opened --
     * inside the cone -- and the jump need not turn the ship (nav decision 5).
     */
    DEEPSPACE_API FUniversePosition ArrivalPoint(const FUniversePosition& From,
                                                 const FStarSystem& Destination,
                                                 double StandoffAU = DefaultStandoffAU);
}
