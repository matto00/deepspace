#pragma once

#include "CoreMinimal.h"

/**
 * What a world's ground is made from, as data (landing decision 1): the
 * facts FWorldRelief is built from. Plain data and nothing else, so FSkyBody
 * can carry it before FWorldRelief exists, and so the sky, the flight and the
 * terrain are handed one set of numbers by one adapter
 * (FSkySystem::FromSystem). Pure: no UObject, and one comparison.
 */

/** Whether a body has ground a ship can set down on (landing decision 13):
 *  Solid for barren, ice and terrestrial worlds; None for oceans, giants and
 *  stars, over which the floor stays a sphere. */
enum class EGround : uint8
{
    None,
    Solid
};

struct FWorldReliefParams
{
    /** Where on the noise this world's face and ground are taken from: the
     *  xyz M_SkyBody's SurfaceSeed is handed (ShipSky::SurfaceSeed). Each is
     *  a multiple of 1/256 in [0, 256), so the float the GPU gets and this
     *  double are the same number exactly. */
    FVector3d SeedOffset = FVector3d::ZeroVector;

    /** The mean radius, cm: the datum every height is measured from. */
    double RadiusCm = 0.0;

    /** The highest this world's relief is drawn, cm (landing decision 3,
     *  FPlanet::ReliefKm). 0 for oceans, giants and stars. */
    double PeakCm = 0.0;

    /** How much of its craters the ground has kept, 0..1: the look's
     *  Cratering (SkySystem.cpp's LookOf). */
    double Cratering = 0.0;

    EGround Ground = EGround::None;
};

/** The same ground: every fact FWorldRelief is built from is equal. The
 *  ground restarts on a change (AWorldGround), and the sky re-bakes that
 *  world's shadow map (AShipSky's map keys). */
inline bool SameRelief(const FWorldReliefParams& A, const FWorldReliefParams& B)
{
    return A.SeedOffset == B.SeedOffset && A.RadiusCm == B.RadiusCm && A.PeakCm == B.PeakCm
        && A.Cratering == B.Cratering && A.Ground == B.Ground;
}
