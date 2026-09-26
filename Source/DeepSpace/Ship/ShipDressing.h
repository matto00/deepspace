#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Ship/ShipDressingRules.h"
#include "Ship/ShipDressingTypes.h"

struct FGenStream;

/**
 * The dressing generator's pure core (lived-in decision 1): a function of
 * surfaces, keep-outs, a seed and the rules, returning a plan -- which
 * template goes where, in which colour, on which surface. No UObject, no
 * world, no assets, so everything it decides is tested headlessly.
 * UShipDressingSubsystem turns the plan into instances.
 *
 * It is the one dressing generator (ADR 0006), and its randomness is
 * GenSeed and FGenStream and nothing else (lived-in decision 4).
 */

/** Somewhere things can rest, in its own frame: X across the surface, with
 *  the Back edge at -X or +X; Y along it; Z up from the resting plane, whose
 *  centre is the frame's origin. */
struct FDressSurface
{
    /** Selects the room stream. */
    FName Room;

    /** "<prop>.<surface>": selects the mean count and the mix. */
    FName Kind;

    /** The placement's n, as in its prop_<name>_<n> labels. */
    int32 Ordinal = 0;

    /** Yaw in 90 degree steps only. */
    FTransform ToWorld = FTransform::Identity;

    FVector2D Size = FVector2D::ZeroVector;
    EDressEdge Back = EDressEdge::NegX;
    EDressUse Use = EDressUse::Centre;

    /** The tallest thing that fits, piles included. */
    float Clear = 40.0f;

    /** Surface-local rectangles nothing may cover. */
    TArray<FBox2D> Excludes;
};

/** One thing left on a surface. A pile is several, one per thing in it. */
struct FDressItem
{
    /** Index into the surfaces Dress was given. */
    int32 Surface = INDEX_NONE;

    FName Template;

    /** NAME_None unless the template takes one. */
    FName Colour;

    /** The centre of its footprint on the plane it rests on, world, turned
     *  as it lies. */
    FTransform World = FTransform::Identity;

    /** Its footprint's centre in the surface's frame. */
    FVector2D At = FVector2D::ZeroVector;

    /** Where its base rests above the surface's plane: 0, or the top of the
     *  thing below it on a pile. */
    double Base = 0.0;

    /** Quarter turns about Z, relative to the surface. */
    int32 Turns = 0;

    /** 0 for the thing on the surface, 1 for the one on it, and so on. */
    int32 StackIndex = 0;
};

enum class EDressWear : uint8
{
    Standard,
    Faded,
    Replaced,
};

/**
 * What no rule and no ini may change: the reason a mug can never go through
 * a shelf, into a doorway, or onto the floor.
 */
namespace DressGuarantees
{
    /** At most this many things (piles count once) on any surface. */
    inline constexpr int32 PoissonMax = 8;

    /** A candidate that overlaps something is redrawn this many times, then
     *  dropped: rejection as a floor, never as the method (ADR 0008). */
    inline constexpr int32 MaxAttempts = 8;

    /** Nothing is dressed onto a surface lower than this above the ship's
     *  floor, which is z = 0 (ADR 0005). Surface clutter never touches the
     *  floor; floor-band clutter is after the POC and needs the pocket check. */
    inline constexpr double MinRestHeightCm = 10.0;

    /** LivedIn is clamped here. The busiest surface's 5 at 4x stays well
     *  inside Poisson's mean limit of 30. */
    inline constexpr double MaxLivedIn = 4.0;
}

namespace ShipDressing
{
    /** The dressing's seed from the world's root: Derive(Derive(Root,
     *  "ship"), "dressing"). The intermediate ship seed is where a seeded
     *  layout will hang once the ship generator is ported, and the dressing
     *  is already under it, so that port does not reshuffle the mugs. */
    DEEPSPACE_API uint64 DressSeed(uint64 Root);

    /** A surface's own seed: from its room's stream, by its kind and
     *  ordinal. Never from where it came in a list, so the ship dresses the
     *  same whatever order its markers load in, and dressing the bunk
     *  differently does not reshuffle the galley. */
    DEEPSPACE_API uint64 SurfaceSeed(uint64 DressSeed, const FDressSurface& Surface);

    /**
     * The plan. Every item lies wholly inside its surface's footprint, rests
     * on it or on the thing below it, stands no taller than Clear, covers no
     * exclude, overlaps nothing else on its surface, and touches no KeepOut
     * box (world: the doors' and console's keep-clear zones, the slide run,
     * the crawlway). A surface below MinRestHeightCm, or of a kind the rules
     * do not know, or with a non-ASCII room or kind, gets nothing.
     *
     * Deterministic: the same inputs give the same plan, exactly, and the
     * order of Surfaces and KeepOut does not change what any surface gets.
     * Items come out grouped by surface, in the order Surfaces was given.
     */
    DEEPSPACE_API TArray<FDressItem> Dress(TConstArrayView<FDressSurface> Surfaces, TConstArrayView<FBox> KeepOut,
                                           uint64 Seed, const FShipDressingRules& Rules);

    /** One draw per piece of furniture, into decision 3's three buckets.
     *  Piece is "<prop>_<n>", as the Piece. tag names it. */
    DEEPSPACE_API EDressWear Wear(uint64 DressSeed, FStringView Piece, const FShipDressingRules& Rules);

    /** Where along a surface a candidate falls, as the fraction of the way
     *  from its -Y end: anchored at the use end, or centred. Exposed so the
     *  shape of the prior is tested on the draws themselves, before overlap
     *  rejection can skew them. */
    DEEPSPACE_API double DrawAlong(FGenStream& Stream, EDressUse Use, const FShipDressingRules& Rules);

    /** How far towards the back edge a candidate falls, 0 at the front lip
     *  and 1 against the back. */
    DEEPSPACE_API double DrawBack(FGenStream& Stream, const FShipDressingRules& Rules);

    /** Half a template's footprint after this many quarter turns. */
    DEEPSPACE_API FVector2D TurnedHalfExtent(const FDressTemplate& Template, int32 Turns);

    /** An item's world bounds, from its template's parts, as it lies. */
    DEEPSPACE_API FBox ItemBounds(const FDressItem& Item, const FDressSurface& Surface, const FShipDressingRules& Rules);

    /** Every material role an item's parts wear, in part order: a part's own
     *  role, or fabric_<colour> for a fabric part. */
    DEEPSPACE_API FName PartRole(const FDressPart& Part, FName Colour);
}
