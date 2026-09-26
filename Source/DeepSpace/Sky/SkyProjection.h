#pragma once

#include "CoreMinimal.h"
#include "Sky/SkySystem.h"
#include "Universe/UniversePosition.h"

/**
 * Where to draw what is out there, and how bright: pure arithmetic from a
 * system and the ship's position to a frame of proxies (sky decisions 1-3).
 *
 * Every body is drawn as a homothety about the ship -- the true sphere scaled
 * about the ship's origin until it lands in a depth band the renderer is
 * comfortable with. Scaling about the eye would keep direction, angular size
 * and phase exactly; scaling about the ship's origin keeps them to the eye's
 * offset over the proxy's distance, which the band's 50 km near edge holds
 * under a pixel anywhere in a 26 m hull.
 *
 * Works entirely in universe axes. The counter-frame's rotation is the whole
 * of the ship's attitude, so nothing here knows which way the ship points and
 * nothing depends on tick order.
 */

struct DEEPSPACE_API FSkyViewParams
{
    /** Radians per pixel: 2 tan(45 deg) / 1920, a 90-degree view on a 1080p
     *  screen, which is what -nullrhi falls back to. The actor sets it from
     *  the live camera and viewport every frame. */
    double PixelAngle = 2.0 * 1.0 / 1920.0;

    /** 50 km. The eye can be 17.6 m from the ship's origin, and 17.6 m over
     *  50 km is 0.68 of a 4K pixel: walking the hull cannot slide the nearest
     *  body against the stars. */
    double NearProxy = 5.0e6;

    /** 125,000 km: 2,500 times the near edge, the depth budget a system at
     *  150 km altitude uses about 150 of. */
    double FarProxy = 1.25e10;

    /** Each body's proxy starts this factor beyond the last one's far side,
     *  so no two depth intervals ever touch. */
    double StackGap = 1.02;

    /** Below this diameter a shaded sphere is noise -- its terminator falls
     *  between pixels -- so a body is drawn at least this big, as a point. */
    double MinPointPixels = 2.0;

    /** From MinPointPixels to MinPointPixels + this, a point becomes a disc. */
    double ResolveBandPixels = 2.0;

    /** Applied to irradiance and to the point boost only (sky decision 2).
     *  1 is fully honest; 0.5 turns a 900x fall-off into 30x. */
    double FluxGamma = 0.5;

    /** A resolved Sun's surface brightness. Every other star scales from it
     *  by (T / T_sun)^4, compressed. */
    double StarSurface = 1.0;

    /** 10 km. Nearer than this the proxy stops growing: the engine sphere's
     *  facets would show first, and below orbit landing replaces this. */
    double MinRenderedAltitude = 1.0e6;

    /**
     * And never nearer than this fraction of the body's radius. A proxy's own
     * depth ratio is (2R + h) / h, so a fixed 10 km floor that costs an Earth
     * 1,275x of the 2,500x band costs a Jupiter 14,000x, and the band breaks.
     * 1.6e-3 is where a body's ratio is half the band -- 10.2 km for an
     * Earth, so the two floors agree for the world the 10 km was sized on --
     * and facets, too, go with altitude over radius rather than altitude.
     */
    double MinRenderedAltitudeOfRadius = 1.6e-3;
};

struct DEEPSPACE_API FSkyBodyView
{
    /** Counter-frame local, which is universe axes, cm. */
    FVector ProxyLocation = FVector::ZeroVector;

    /** cm, the radius of the drawn sphere. */
    double ProxyRadius = 0.0;

    /** Unit, universe axes, from the ship's origin. */
    FVector Direction = FVector::ForwardVector;

    /** True, centre to ship, cm. */
    double Distance = 0.0;

    /** True, rad. */
    double AngularRadius = 0.0;

    /** What the proxy subtends, rad: the true one, or a point's minimum. */
    double DrawnAngularRadius = 0.0;

    /** 1 a point, 0 a shaded disc. */
    double PointBlend = 1.0;

    /** Disc-averaged brightness at the current phase, 1 full (stars: 1). */
    double Phase = 1.0;

    /** Honest: independent of the ship's distance. */
    double SurfaceBrightness = 0.0;

    /** > 1 only while drawn larger than true. */
    double PointBoost = 1.0;

    /** What the material gets: the per-pixel emission scale. SurfaceBrightness
     *  exactly, once resolved. */
    double Brightness = 0.0;

    /** Unit, universe axes, body toward its star. Zero for a star, which
     *  nothing lights, and for a body in a system with none. */
    FVector LightDirection = FVector::ZeroVector;
};

struct DEEPSPACE_API FSkyFrame
{
    /** Same order as FSkySystem::Bodies. */
    TArray<FSkyBodyView> Bodies;

    /** Indices into Bodies, nearest first by d^2 - R^2. */
    TArray<int32> DepthOrder;

    /** Unit, universe axes, ship toward the star. Zero with no star. */
    FVector SunDirection = FVector::ZeroVector;

    /** The star's irradiance at the ship, compressed, 1 for a Sun at 1 AU.
     *  0 with no star. */
    double SunIrradiance = 0.0;

    /** How much of the star's disc is not behind a nearer body: 1 in open
     *  sky, 0 parked in a planet's shadow (sky decision 5). 1 with no star:
     *  there is nothing to hide, and SunIrradiance is already 0. */
    double SunVisibleFraction = 1.0;
};

namespace SkyProjection
{
    DEEPSPACE_API FSkyFrame Project(const FSkySystem& System,
                                    const FUniversePosition& Ship,
                                    const FSkyViewParams& Params);

    /**
     * Disc-averaged Lambert brightness at phase angle Alpha, 1 at full phase:
     * (sin a + (pi - a) cos a) / pi. M_SkyBody's shaded term is
     * SkyMaterial::LambertDiscGain * saturate(N.L), whose average over the
     * visible disc is exactly this -- which is what keeps a point and a disc
     * at the same total flux through the resolve.
     */
    DEEPSPACE_API double LambertPhase(double Alpha);

    /**
     * The fraction of disc A covered by disc B, from their angular radii and
     * the angle between their centres, all in rad: 0 apart, 1 when B covers
     * A, (RadiusB / RadiusA)^2 when B sits wholly inside A.
     *
     * Plane discs, not spherical caps. Containment and separation are exact
     * either way, since both are angles; only the shape of the lens between
     * is approximate, by the difference between a planet limb's curvature on
     * the sphere and in the plane, which across a sun half a degree wide is
     * far below anything the deck's light level can show. The plane form
     * stays exact for micro-radian discs, where the spherical one's
     * arc-cosines of numbers next to 1 have nothing left to say.
     */
    DEEPSPACE_API double DiscOverlapFraction(double Separation, double RadiusA, double RadiusB);

    /** Ratio ^ Gamma: how irradiance spans are squeezed into a screen. */
    DEEPSPACE_API double Compress(double Ratio, double Gamma);

    /** A cone's solid angle, sr, stable for the micro-radian cones distant
     *  bodies subtend: 4 pi sin^2(a / 2), where 2 pi (1 - cos a) cancels to
     *  nothing. */
    DEEPSPACE_API double ConeSolidAngle(double AngularRadius);
}
