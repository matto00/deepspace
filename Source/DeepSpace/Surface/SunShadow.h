#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Surface/WorldReliefParams.h"

class IGroundField;

/**
 * The cast shadow (the developer's ruling on slice (b)'s build, 2026-09-28:
 * baked, not marched): how much of its star a point of the ground sees past
 * the ground toward it. Pure: no UObject, no world, no CVar.
 *
 * Worlds do not spin and nothing orbits in this slice, so the star stands
 * still over every surface and the shadow is a fixed function of where the
 * point is. It is computed where the ground is built -- a tile's vertices,
 * off the game thread (TerrainTile::Build) -- and once a world for the
 * orbit (SunShadowMap), and never per pixel.
 *
 * The horizon toward the star is the highest elevation, above the point's
 * own level, of the ground along the great circle toward the star's
 * azimuth, from the exact triangle of the point, the ground and the world's
 * centre. The star is a disc of its own angular radius, and what shows is
 * the share of the disc above that horizon: DiscAbove((horizon - elevation)
 * / radius). A shadow is as dark as the night side: there is no fill light.
 *
 * Two exits skip the march. The night's is a proof: no horizon is lower
 * than the dip to the lowest ground the world can have. The day's is a
 * measurement with a margin: above atan(SteepestSlope) plus the star's
 * radius nothing can rise over the disc (SteepestSlope's comment). Inside
 * the march two exits are exact: the disc already hidden, and no ground from
 * there on -- none above the peak, all of it curving away -- able to rise
 * over what is found.
 */
namespace SunShadow
{
    /** The star as a surface sees it: the unit direction from the body's
     *  centre to the star, in the body's axes (the universe's: worlds do not
     *  spin) -- the sky's own light, SkyProjection::LightDirection -- and its
     *  angular radius, rad. A zero direction is no star, and casts nothing. */
    struct FSunLight
    {
        FVector3d Direction = FVector3d::ZeroVector;
        double AngularRadius = 0.0;

        bool IsSet() const { return !Direction.IsNearlyZero(); }
    };

    struct FSunVisibility
    {
        /** The star's disc seen past the ground, 0..1. */
        double Visible = 1.0;

        /** The highest horizon found, rad above the level; -pi/2 where none
         *  was read. */
        double Horizon = -UE_DOUBLE_HALF_PI;

        /** Heights read: the cost. */
        int32 Reads = 0;
    };

    /** A star seen from inside its own radius is pi/2 in radius, where the
     *  share of a disc stops meaning anything: clamped here. */
    inline constexpr double SunRadiusMax = 0.5;

    /** Nothing in the ground is finer than FWorldRelief::BandLimitCm (5 m),
     *  so the first sample is never nearer than half of it. */
    inline constexpr double NearestCm = 250.0;

    /** Each sample reads the heights at this many times the gap ahead of it:
     *  the face's own fade (filter_pixels), so no band is read at under two
     *  samples a cycle along the march. */
    inline constexpr double FootprintFactor = 2.0;

    /** How many samples a tile's vertex marches (ds.Terrain.ShadowSamples):
     *  planning measured 12 at mean |dv| 0.04-0.07 from the dense march at 5
     *  degrees, and 8 at 0.07-0.14 (DeepSpace.Surface.SunShadow.AgainstProfile). */
    inline constexpr int32 DefaultSamples = 12;

    /** The steepest |grad S| along the ground of twenty detail bands, and of
     *  all six crater bands, at footprint 0, over 131,072 samples (the plan's
     *  harness; DeepSpace.Surface.SunShadow.SteepestSlope re-measures it). A
     *  sample, not a proof: FWorldRelief::MaxSlope is the proof, and it is
     *  six times this, which would leave no day side to exit. */
    inline constexpr double DetailGradientSampled = 16.72;
    inline constexpr double CraterGradientSampled = 2.30;
    inline constexpr double SteepestMargin = 1.5;

    /** The share of a disc above a straight horizon X of its radii above
     *  the disc's centre: 1 at X <= -1, 0.5 at 0, 0 at X >= 1. */
    DEEPSPACE_API double DiscAbove(double X);

    /** The elevation, rad above the level at the point, of ground ThereCm
     *  above the datum at arc Arc (rad) along a great circle, seen from
     *  HereCm above it, over a datum of RadiusCm. Exact. */
    DEEPSPACE_API double Elevation(double RadiusCm, double HereCm, double ThereCm, double Arc);

    /** How far along the ground (rad) the march must look: where ground at
     *  PeakCm, the highest the world can have, falls under Lower (rad above
     *  the level) as the sphere curves away. acos(rho cos Lower) - Lower,
     *  rho = (R + Here) / (R + Peak); 0 where nothing can rise over Lower. */
    DEEPSPACE_API double MarchEnd(double RadiusCm, double HereCm, double PeakCm, double Lower);

    /** How far below the level (rad) the lowest ground the world can have,
     *  LowestCm, is at its tangent from HereCm. No horizon is lower, so a
     *  star whose upper edge is under -NightDip is hidden: a proof. */
    DEEPSPACE_API double NightDip(double RadiusCm, double HereCm, double LowestCm);

    /**
     * The shadow at D, a tile vertex's: DefaultSamples geometric samples from
     * Nearest (the footprint, or NearestCm) to MarchEnd, each read at
     * FootprintFactor times the gap ahead of it. SteepestSlope is the
     * day exit's, rise over run: SteepestSlope(Params) below for the real
     * ground, Ground.MaxSlope() for a proof. HereCm, if given, is D's own
     * height at the footprint, already read.
     */
    DEEPSPACE_API FSunVisibility Visible(const IGroundField& Ground, const FVector3d& D, const FSunLight& Sun, double FootprintCm,
                                         double SteepestSlope, int32 Samples = DefaultSamples, TOptional<double> HereCm = {});

    /**
     * The same horizon over a profile already read, the orbit map's: Heights[0]
     * is the point's own, Heights[k] the ground at arc k x Step (rad) toward the
     * star, all at one footprint; SunElevation the star's centre above the
     * level. Every sample out to MarchEnd is one, so nothing lies between
     * them and nothing is read twice. Reads counts the profile entries
     * past the point that were read.
     */
    DEEPSPACE_API FSunVisibility AlongProfile(double RadiusCm, double PeakCm, double LowestCm, double SteepestSlope,
                                              TConstArrayView<double> Heights, double Step, double SunElevation, double SunRadius);

    /** The real ground's steepest slope, rise over run, for the day exit:
     *  SlopeScale x SteepestMargin x (DetailGradientSampled + Cratering x
     *  CraterGradientSampled). PeakCap's slope is at most 1, so the capped
     *  height is never steeper than its sum. 0 on a world with no ground. */
    DEEPSPACE_API double SteepestSlope(const FWorldReliefParams& Params);
}
