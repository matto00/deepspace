#pragma once

#include "CoreMinimal.h"
#include "Universe/GenPriors.h"
#include "Universe/StarSystem.h"
#include "Universe/UniversePosition.h"

/**
 * The system map's arithmetic: where each thing in a star system is drawn on
 * the orrery, and which world a click lands on. Pure -- a system and a few
 * pixel sizes in, positions out -- so every rule of the map is tested with no
 * world and no Slate (system map spec, decisions 3 and 4).
 *
 * Top-down, fixed to the universe's axes: looking down universe -Z, +X is up
 * the glass and +Y is to the right. Seen from above in Unreal's left-handed
 * axes that is the right way round; +X to the right, the textbook habit,
 * would draw every system as its mirror image. The map never turns: a map
 * that holds still is one the eye learns, and steering is what the HUD is for.
 *
 * Radius is logarithmic, warped so that no two orbits are ever drawn closer
 * than MinRingGap: a linear map puts the inner worlds on the star, and a pure
 * log map lays the tightest pairs procgen makes 3 px apart. Everything that is
 * not an orbit -- the ship, the arrival point -- goes through the same warp,
 * so the ship crosses a ring on the map in the frame it crosses the orbit.
 *
 * Every size is in panel pixels, which at the map's 600 x 424 draw size are
 * helm pixels too (decision 1).
 */
namespace SystemMap
{
    /** The orrery's own sizes. The defaults are the panel's: a 256 px square
     *  whose centre is (128, 128) in the view's own space. */
    struct FMapPixels
    {
        /** The star's disc, radius. It stands for everything inside the
         *  innermost knot, so nothing is drawn inside it. */
        double StarPx = 7.0;

        /** Where the rim knot -- the arrival standoff with a margin -- is
         *  drawn. 12 px of the square are left outside it for the outermost
         *  world's numeral. */
        double RimPx = 116.0;

        FVector2D Centre = FVector2D(128.0, 128.0);

        /** The most worlds a system can have: the gap is derived from it, so
         *  the worst system procgen can make fits by construction. */
        int32 MaxPlanets = GenGuarantees::MaxPlanets;
    };

    /** The rim knot is the arrival standoff times this: every arrival is
     *  drawn inside the rim, with room to see the ship come in. */
    inline constexpr double RimMargin = 1.25;

    /** How far from a dot's centre a click still picks it, px. Larger than
     *  any dot, so a hand at arm's length need not hit the dot itself;
     *  nearest-wins inside it, so no two worlds ever share a pixel. */
    inline constexpr double PickRadius = 14.0;

    /** A world's dot, diameter, px, before the cap at its local gap. A giant
     *  is drawn larger because it is what the eye should find first. */
    inline constexpr double RockDotPx = 7.0;
    inline constexpr double GiantDotPx = 10.0;

    /** The target ring is its dot's size plus this, px (before its cap). */
    inline constexpr double TargetPadPx = 6.0;

    /** A dot or target ring is never drawn larger than its local gap less
     *  this, px, so neighbours never touch. */
    inline constexpr double DotClearancePx = 2.0;

    /** The ship: a ring this wide, px, and a tick this long along its nose. */
    inline constexpr double ShipRingPx = 9.0;
    inline constexpr double ShipTickPx = 6.0;

    /** A nose within this of vertical, degrees, has no direction in the
     *  plane worth drawing: the glyph is a ring alone. */
    inline constexpr double NoseHiddenWithinDeg = 20.0;

    /** A ship further than this off the plane, degrees, is told so in the
     *  footer: nearer, the projection and the truth look alike. */
    inline constexpr double ElevationShownPastDeg = 5.0;

    /** The space between a dot's edge and its numeral, px. */
    inline constexpr double NumeralGapPx = 2.0;

    /**
     * The least distance two rings are ever drawn apart, px:
     * floor((RimPx - StarPx) / (MaxPlanets + 1)), 8 at the panel's sizes.
     * One gap per world and one more: twelve rings, the gap inside the
     * first and the gap outside the last need 104 px of the 109, so a
     * crowded system is pushed inward by the warp and never off the rim, and
     * the outermost ring is always a gap inside it. That last gap is the
     * segment the arrival standoff is drawn in: without it a crowded
     * system's outermost ring sat on the rim, the whole way in from the
     * arrival to the outermost orbit was drawn in no pixels at all, and the
     * ship stood still on the map for the first leg of every approach.
     */
    DEEPSPACE_API double MinRingGap(const FMapPixels& Pixels);

    /**
     * The radial scale for one system: knots in log radius, each drawn at a
     * radius in pixels, and a piecewise-linear map between them. A function
     * of the system alone -- it never rescales as the ship flies, which
     * would make the map move under a still ship.
     */
    struct DEEPSPACE_API FMapScale
    {
        FMapPixels Pixels;

        /** The star's position: the centre of the map. */
        FUniversePosition Star;

        /** Inside this, AU, everything is drawn at the star's edge: half
         *  the innermost orbit, or the star's own surface with no worlds. */
        double InnerAU = 0.0;

        /** Past this, AU, everything is drawn on the rim. */
        double RimAU = 0.0;

        /** The knots, innermost first: InnerAU, each orbit, RimAU, as log10
         *  AU, and the pixel radius each is drawn at. Never decreasing. */
        TArray<double> KnotLog;
        TArray<double> KnotPx;

        /** Each orbit's ring, px, in orbit order: the knots less the ends. */
        TArray<double> RingPx;

        /** Where a distance from the star is drawn, px from the centre.
         *  Clamped to [StarPx, RimPx]; monotonic; exact at every knot. */
        double RadiusPx(double DistanceAU) const;
    };

    /**
     * The scale that fits System: its rim at the arrival standoff
     * (NavStart::ArrivalStandoffAU with StandoffAU, the ds.Nav.StandoffAU
     * it would be met at) times RimMargin, so every world and every arrival
     * lands on the map, and its rings placed by the log map and then warped
     * so every gap is at least MinRingGap and the outermost ring is at least
     * MinRingGap inside the rim.
     */
    DEEPSPACE_API FMapScale Fit(const FStarSystem& System, double StandoffAU, const FMapPixels& Pixels = FMapPixels());

    /**
     * Where a position is drawn: its true distance from the star through the
     * warp, at its azimuth in the plane. The true distance, not the
     * projected one, so a ship over the pole is drawn as far out as it is
     * rather than on the star. For the worlds, which are in the plane, the
     * two are the same.
     */
    DEEPSPACE_API FVector2D Place(const FMapScale& Scale, const FUniversePosition& Where);

    /** Whether the ship is drawn where it is, or held at an edge of the map. */
    enum class EMapPin : uint8
    {
        None,
        /** Inside InnerAU: drawn just outside the star's disc. */
        Inside,
        /** Past RimAU: drawn on the rim. */
        Beyond,
    };

    /** The ship's glyph, and what the footer should say about it. */
    struct FMapShip
    {
        FVector2D Centre = FVector2D::ZeroVector;

        /** Unit, panel axes (+X right, +Y down): the nose projected into the
         *  plane. Unset within NoseHiddenWithinDeg of vertical. */
        TOptional<FVector2D> Nose;

        EMapPin Pin = EMapPin::None;

        /** Above the plane, degrees; negative below it. */
        double ElevationDeg = 0.0;
    };

    /** The ship at Where, facing Orientation (universe axes), on Scale. Never
     *  drawn on the star: a ship inside it is held just outside the disc. */
    DEEPSPACE_API FMapShip Ship(const FMapScale& Scale, const FUniversePosition& Where, const FQuat& Orientation);

    /** One world as drawn. */
    struct FMapDot
    {
        int32 Orbit = 0;
        FVector2D Centre = FVector2D::ZeroVector;

        /** Its ring's radius, px. */
        double RingPx = 0.0;

        /** The room either side of its ring, px: to the next ring or the
         *  star's edge inward, to the next ring or the rim outward. Every
         *  size below is capped against it. */
        double GapPx = 0.0;

        /** Diameter, px. */
        double SizePx = 0.0;

        /** The target ring's diameter, px, if this world is the target. */
        double TargetRingPx = 0.0;

        /** "IV": the orbit's numeral. */
        FString Numeral;

        /** Unit, panel axes: away from the star, the side its numeral goes. */
        FVector2D Outward = FVector2D(0.0, -1.0);
    };

    /** Everything the orrery draws that does not move: the rings and the
     *  worlds. The map's cache holds this. */
    struct FMapLayout
    {
        FMapPixels Pixels;
        TArray<double> RingPx;
        TArray<FMapDot> Dots;
    };

    DEEPSPACE_API FMapLayout Layout(const FStarSystem& System, const FMapScale& Scale);

    /**
     * Which world a click at Point picks: the one whose dot centre is
     * nearest, if within MaxRadius. Every point belongs to at most one
     * world, the Voronoi cell of its dot clipped to a disc, so two worlds on
     * neighbouring rings split the space between them at the midpoint. Ties
     * go to the inner world. A click on the star picks nothing, even with a
     * dot in reach: the star cannot be targeted (decision 4).
     */
    DEEPSPACE_API TOptional<int32> Pick(const FMapLayout& Layout, const FVector2D& Point, double MaxRadius = PickRadius);

    /** What pressing a world on the map asks the ship to do. */
    enum class EMapSelect : uint8
    {
        /** Make this world the target. */
        Target,
        /** It already is: let it go. */
        Clear,
    };

    struct FMapSelection
    {
        EMapSelect Action = EMapSelect::Target;
        FBodyId Body;
    };

    /**
     * What pressing orbit Orbit of the system Here does, given the target as
     * the ship holds it: target it, or clear it if it is the target already
     * -- the chart's SelectRow rule, for worlds. Nothing for an orbit the
     * system lacks. Pure, so both pick paths are held to one rule before the
     * target they act on exists.
     */
    DEEPSPACE_API TOptional<FMapSelection> Select(const FStarSystem& Here, int32 Orbit, const TOptional<FBodyId>& Target);
}
