#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipNavState.h"
#include "Universe/StarSystem.h"

/**
 * Every word the ship uses about navigation, in one pure place. The HUD, the
 * chart and ds.Nav.Near all word the same facts through here, so the three
 * can never disagree about them.
 *
 * Words, never gauges. A bearing is a fact about the sky -- nothing is late
 * and nothing gets worse -- and the jump's state is a word with no number in
 * it at all: no percentage, no bar, no countdown, no ETA.
 */
namespace NavText
{
    /** The middle dot every line is joined with. */
    inline const TCHAR* const Separator = TEXT(" · ");

    /**
     * Where a direction lies from the nose, in the pilot's words: "12° to
     * port, 3° up". "dead ahead" inside the cone, with no degrees at all:
     * once aligned there is nothing more to get right, and a number there
     * would become a target to chase. Past 90 degrees it is "astern" and the
     * side to turn toward, because degrees are no help that far round.
     *
     * ShipLocalDir is in ship axes (+X the nose, +Y starboard, +Z up), so the
     * words are relative to the ship and not to a free-looking head.
     */
    DEEPSPACE_API FString Bearing(const FVector& ShipLocalDir, double ConeRadians);

    /** "red dwarf", "yellow star": a star by its colour, which is what the
     *  window shows, rather than by its letter. */
    DEEPSPACE_API FString StarClass(EStarClass Class);

    /** A world by what it is made of, in procgen's own taxonomy and lower
     *  case: "barren", "terrestrial", "ocean", "ice", "gas giant". What the
     *  map's rows call each world. */
    DEEPSPACE_API FString WorldKind(EPlanetKind Kind);

    /** A world by name: its designation, "Kessa II", or for an inhabited
     *  world its given name and then the designation, "Halden · Kessa II" --
     *  the given name is what the people there call it, and the designation
     *  is still how it is found. */
    DEEPSPACE_API FString WorldName(const FPlanet& Planet);

    /** A system by name and colour, as the HUD and the chart name where the
     *  ship is: "Kessa · red dwarf". */
    DEEPSPACE_API FString Place(const FString& Name, EStarClass Class);

    /** The same with the ship's record, as the chart gives it: "Kessa · red
     *  dwarf · visited" once the ship has been there. */
    DEEPSPACE_API FString Place(const FString& Name, EStarClass Class, bool bVisited);

    /** "visited" for somewhere the ship has been, and nothing for somewhere
     *  it has not: never "unvisited", which would read as a list to finish. */
    DEEPSPACE_API FString Visited(bool bVisited);

    /** How far a star is, in light years to a tenth: "4.2 ly". A fact about
     *  the sky, like a bearing; a tenth is as fine as choosing needs. */
    DEEPSPACE_API FString Distance(double Cm);

    /** Where the course is, as the chart reads it: "Kessa · 12° to port", in
     *  the helm's own bearing words, or the name alone with no bearing to
     *  give -- between stars, when there is nothing to steer. */
    DEEPSPACE_API FString Course(const FString& CourseName, const TOptional<FVector>& ShipLocalDir,
                                 double ConeRadians);

    /** The chart's course with none plotted: "None". */
    DEEPSPACE_API FString NoCourse();

    /** The chart's word for the jump: "Idle", "Winding", "Ready",
     *  "Between stars". */
    DEEPSPACE_API FString JumpWord(EJumpState State);

    /** The HUD's line for the jump: "JUMP WINDING", "JUMP READY", or
     *  "BETWEEN STARS" in transit. */
    DEEPSPACE_API FString Jump(EJumpState State);

    /** The same with a course: "JUMP READY · Kessa · 12° to port". The
     *  bearing is left off in transit, when there is nothing to steer. */
    DEEPSPACE_API FString Jump(EJumpState State, const FString& CourseName,
                               const FVector& ShipLocalDir, double ConeRadians);
}
