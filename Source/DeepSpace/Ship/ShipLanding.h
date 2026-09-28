#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipFlightSurface.h"

/**
 * The ship's footprint on the ground (landing decision 11): eight points in
 * ship space, as layout data -- four gear feet and the belly's four plan
 * corners -- and the least height of any of them above the ground, which
 * the descent cap, the ground's hard stop and (slice c) contact all read.
 *
 * Mirrored from GEAR and BELLY in Tools/hauler_layout.py; test_placement.py
 * reads the two tables below by name and holds them equal. Pure.
 */
namespace ShipLanding
{
    /** ds.Land.GearClearance's default, cm: the origin's height over flat
     *  ground at rest -- 10 cm of slab and 140 cm of notional gear. More than
     *  the drawn ground's error under the ship (GearClearance / 10), so the
     *  drawn and the flown ground never visibly disagree. */
    inline constexpr double DefaultGearClearanceCm = 150.0;

    /** The belly, the slab's underside, cm. */
    inline constexpr double BellyZCm = -10.0;

    /** The gear's feet in plan, cm; their Z is -GearClearance. */
    inline constexpr double GearFeetXY[4][2] = { { -700.0, -300.0 }, { -700.0, 400.0 }, { 1600.0, -100.0 }, { 1600.0, 200.0 } };

    /** The belly's plan corners at its underside, cm. */
    inline constexpr double BellyCorners[4][3] = { { -820.0, -410.0, -10.0 }, { -820.0, 510.0, -10.0 }, { 1770.0, -410.0, -10.0 }, { 1770.0, 510.0, -10.0 } };

    /** The eight points, ship space, cm: the feet first. */
    DEEPSPACE_API TArray<FVector, TFixedAllocator<8>> FootprintPoints(double GearClearanceCm);

    /** The farthest any footprint point is from the origin, cm. */
    DEEPSPACE_API double ReachCm(double GearClearanceCm);

    struct FFootprintClearance
    {
        /** The least height of any point above the ground, cm, radially:
         *  0 when a foot touches, negative when a point is under. */
        double Least = TNumericLimits<double>::Max();

        /** Which point, into FootprintPoints; INDEX_NONE with no ground. */
        int32 Point = INDEX_NONE;

        /** The ground's unit normal under that point, universe axes. */
        FVector GroundNormal = FVector::UpVector;
    };

    /** The footprint's clearance over Surface's ground, the ship at Origin
     *  turned by Orientation. Nothing (Point INDEX_NONE) without a ground. */
    DEEPSPACE_API FFootprintClearance FootprintClearance(const FFlightSurface& Surface, const FUniversePosition& Origin,
                                                         const FQuat& Orientation, double GearClearanceCm);
}
