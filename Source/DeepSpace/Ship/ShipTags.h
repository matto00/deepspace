#pragma once

#include "CoreMinimal.h"

/**
 * Actor tags the level build puts on generated actors that C++ reads the
 * ship by, in their own header so the level scripts and every reader share
 * one spelling. Generated actors are addressed by tag, never by name or
 * index: a label is for humans and an index changes whenever the layout does.
 */
namespace ShipTags
{
    /** A glass box: the cockpit's panes and the galley's window, tagged by
     *  build_hauler.py (placement.py's GLASS_TAG, held equal to this by
     *  test_placement.py) and checked on the built level by verify_level.py.
     *  A trace from an eye that meets one of these first has met the glass,
     *  so what lies beyond it can be seen; one that meets anything else first
     *  has met a wall. Nothing but glass may carry it, or a bracket would be
     *  drawn through the hull. */
    DEEPSPACE_API extern const FName Glass;
}
