#pragma once

#include "CoreMinimal.h"
#include "Sky/SkySystem.h"
#include "Universe/UniversePosition.h"

class UWorld;

/**
 * The one seam between the sky and the subsystems that know where the ship
 * is. Free functions that ask and store nothing (sky decision 7): nothing
 * calls into the sky, nothing pushes a system at it, and nothing needs to find
 * it. AShipSky polls these every frame, and a serial it last built for is the
 * only thing it keeps -- a cache key, never the answer.
 *
 * Every function is safe with a null world, a world with no ship subsystem,
 * and a world with no universe subsystem, and answers as if nothing were out
 * there. That is the whole of the null-world branch, and it is what the sky's
 * world-free tests run against.
 */
namespace LocalSystem
{
    /** Changes whenever Current() would: UShipSubsystem::GetJumpSerial(). 0
     *  with no ship subsystem. The opening placement happens before any actor
     *  ticks, so a consumer starting from INDEX_NONE builds for the placed
     *  ship without a bump of its own. */
    DEEPSPACE_API int32 Serial(const UWorld* World);

    /**
     * The system the ship is in, by value: FSkySystem::FromSystem of
     * UUniverseSubsystem::GetSystemAt(ship position) and its neighbours from
     * GetSystemsNear. By value because procgen caches nothing, so there is
     * nothing a reference could point into (plan conflict 1).
     *
     * Empty -- no bodies, no neighbours -- with no world, no universe, or the
     * ship between stars.
     */
    DEEPSPACE_API FSkySystem Current(const UWorld* World);

    /** UShipSubsystem::IsInTransit(). False with no ship subsystem. */
    DEEPSPACE_API bool InTransit(const UWorld* World);

    /** The background starfield's seed: Derive(GetRootSeed(),
     *  Label("sky.starfield")), so the galaxy's backdrop is world-level and
     *  shareable like everything else. Derive(Fallback, the same label) with
     *  no universe subsystem, which is where the counter-frame's StarSeed
     *  survives (plan conflict 4). */
    DEEPSPACE_API uint64 StarfieldSeed(const UWorld* World, uint64 Fallback);

    /**
     * Pure: the distance from Where to the nearest surface, cm -- the drive's
     * room. The nearest body's surface (centre distance minus radius), or
     * the system's edge (EdgeRadius minus the distance to the star, when
     * EdgeRadius > 0), whichever is closer, so the drive slows into the edge
     * as it slows into a planet and never flies the ship out of its system
     * (plan conflict 10). Never negative: 0 at or inside a body, at or past
     * the edge, and for an empty system, so an empty sky gives the drive no
     * room and it runs at cruise speed.
     */
    DEEPSPACE_API double NearestSurfaceDistance(const FSkySystem& System, const FUniversePosition& Where);
}
