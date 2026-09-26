#pragma once

#include "CoreMinimal.h"
#include "ShipDressingTypes.generated.h"

/**
 * The two enums a dressing surface carries from the layout to the generator
 * (lived-in decision 1b), and the actor tags the two meet by. The only
 * reflection the pure core (ShipDressing.h) sees: it includes this header
 * and nothing else of Unreal's object system.
 */

/** Which edge of a surface things get pushed against: the wall behind a
 *  counter, the window beyond a desk. Tools/props.py's Surface.back. */
UENUM(BlueprintType)
enum class EDressEdge : uint8
{
    NegX,
    PosX,
};

/** Where the person using a surface sits or stands, along its length.
 *  Tools/props.py's Surface.use. */
UENUM(BlueprintType)
enum class EDressUse : uint8
{
    Centre,
    PosY,
    NegY,
};

/**
 * Actor tags. Generated actors are addressed by tag, never by name or index
 * (CLAUDE.md); these are the same strings as Tools/placement.py's, and
 * test_placement.py reads this file to hold them equal.
 */
namespace ShipDressingTags
{
    /** An AShipDressingSurface the layout exported. */
    DEEPSPACE_API extern const FName SurfaceTag;

    /** An AShipDressingKeepOut: a world box no clutter may touch. */
    DEEPSPACE_API extern const FName KeepOutTag;

    /** A furniture part whose material the wear draw may swap. */
    DEEPSPACE_API extern const FName WearTag;

    /** A furniture part's second tag, "Piece.<prop>_<n>": the placement it
     *  belongs to, so a whole desk wears together. */
    DEEPSPACE_API extern const FString PieceTagPrefix;

    /** The one transient actor the dressing's instances live on. */
    DEEPSPACE_API extern const FName ClutterTag;
}
