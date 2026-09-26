#pragma once

#include "CoreMinimal.h"

/**
 * Every number the dressing generator decides with, and the clutter it
 * decides between (lived-in decision 1c). A pure struct: no UObject, no
 * reflection, so ShipDressing::Dress can be tested with no world. The
 * defaults live in the constructor, in one place.
 *
 * Tunables are here; guarantees are not. How many redraws before an item is
 * dropped, the Poisson cap, containment inside the surface, the excludes,
 * the keep-outs and Clear are DressGuarantees in ShipDressing.h, because no
 * tune may be able to put a mug through a shelf.
 */

/** The three meshes the ship is built from. Their pivots disagree -- SM_Cube
 *  at a corner, SM_ChamferCube at its centre, SM_Cylinder at its base -- so
 *  nothing here says where a pivot is: the subsystem measures each mesh's
 *  bounds and places every instance from them. */
enum class EDressMesh : uint8
{
    Cube,
    Chamfer,
    Cylinder,
};

/**
 * One primitive of a clutter template: Tools/props.py's Part, in C++. At is
 * its centre relative to the template's origin, which is the centre of the
 * template's footprint on the plane it rests on; Size is its extent in cm,
 * unturned. Role names its material, MI_Ship_<Role>; the role "fabric" takes
 * the item's drawn colour, MI_Ship_fabric_<colour>.
 */
struct FDressPart
{
    EDressMesh Mesh = EDressMesh::Cube;
    FVector At = FVector::ZeroVector;
    FVector Size = FVector::ZeroVector;
    FName Role;
};

/** Something that can be left on a surface. Authored with its long axis
 *  along Y, the way things lie along the edge of a counter. */
struct DEEPSPACE_API FDressTemplate
{
    FName Name;
    TArray<FDressPart> Parts;

    /** Books and crates pile up. */
    bool bStacks = false;

    /** Books on a pile cross each other, so it does not read as one block. */
    bool bAlternates = false;

    /** Half its footprint, unturned: X across a surface, Y along it. */
    FVector2D HalfExtent() const;

    /** Its top above the plane it rests on. */
    double Height() const;

    /** Whether any part wears the item's drawn colour. */
    bool TakesColour() const;
};

/** One entry of a categorical: a name and its weight. */
struct FDressWeight
{
    FName Name;
    double Weight = 0.0;
};

/** What a kind of surface carries: how many things on average, and which. */
struct FDressKind
{
    /** "<prop>.<surface>", or "<prop>" for every surface of that prop (the
     *  wall rack's three shelves are one kind of place). */
    FName Kind;

    /** Mean items on a surface of this kind at LivedIn 1. */
    double Lambda = 0.0;

    /** Which templates, in what proportion. */
    TArray<FDressWeight> Mix;
};

struct DEEPSPACE_API FShipDressingRules
{
    /** The defaults: the hauler as it is lived in. */
    FShipDressingRules();

    /** Scales every surface's mean count at once. 0.3 is freshly moved in,
     *  1 is lived in, 2 is squalid. ds.Dress.LivedIn is read into this at
     *  every dress. */
    double LivedIn = 1.0;

    TArray<FDressKind> Kinds;
    TArray<FDressTemplate> Templates;

    /** Colour for anything with a fabric part: skewed to the ship's issue
     *  colours, because most of what a hauler carries is drab and the few
     *  bright things are the personal ones. */
    TArray<FDressWeight> Colours;

    /** Position along a surface, as the fraction of the way from its use
     *  end: Beta(AlongUseA, AlongUseB). People leave things near where they
     *  sit and push them away from where they work. */
    double AlongUseA = 2.0;
    double AlongUseB = 4.0;

    /** Position along a surface used from the middle: Beta, clustering
     *  towards the centre and thinning at the ends. */
    double AlongCentreA = 3.0;
    double AlongCentreB = 3.0;

    /** How far towards the back edge: Beta. Things end up pushed against
     *  the wall, not balanced on the front lip. */
    double BackA = 4.0;
    double BackB = 2.0;

    /** A pile grows by one more with this chance, up to StackCap. Each item
     *  on a pile is one more independent decision to put it there. */
    double StackChance = 0.5;
    int32 StackCap = 3;

    /** Weight of each quarter turn relative to the surface: square to its
     *  edge, and mostly lengthwise along it, because that is how a thing is
     *  put down in front of you; a few end up turned across. */
    double TurnWeights[4] = { 3.0, 1.0, 3.0, 1.0 };

    /** Wear per furniture piece: Beta(WearA, WearB), a bounded proportion
     *  with most things well used. Not Weibull: that is wear over time, and
     *  the dressing is a snapshot of how things stand when the game opens. */
    double WearA = 5.0;
    double WearB = 2.0;

    /** Below this, the piece was swapped out for a newer one (about 11%). */
    double ReplacedBelow = 0.5;

    /** Above this, it has faded (about 11%). */
    double FadedAbove = 0.9;

    /** The rules for a surface kind: an exact "<prop>.<surface>" entry, or
     *  failing that the "<prop>" entry. Null if neither. */
    const FDressKind* FindKind(FName Kind) const;

    const FDressTemplate* FindTemplate(FName Name) const;
};
