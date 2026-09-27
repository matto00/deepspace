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
 * it at all: the charge has no percentage, no bar and no countdown, because
 * it is a wait before anything can happen. An approach is different: the
 * player is flying it, at a speed they can change at any moment, so its
 * time to arrival is live (the system map spec's ruling 3, Duration).
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

    /**
     * Where a world lies from the nose, precisely enough to fly onto it
     * (system map decision 6): Bearing's words, with two differences. "dead
     * ahead" means the nose is on the world, off-boresight no more than
     * AheadRadians -- TargetMarker's max(angular radius, AheadFloor), not
     * the jump's 8-degree cone, which from 0.03 AU misses an Earth by a
     * hundred of its radii. And degrees to a tenth under 10, "3.0° up",
     * "0.4° to starboard", whole above, "12° to port": whole degrees cannot
     * aim at a disc of 0.16. Each unit is chosen on the rounded value, so
     * 9.96 degrees reads "10°", never "10.0°". Astern as Bearing.
     */
    DEEPSPACE_API FString TargetBearing(const FVector& ShipLocalDir, double AheadRadians);

    /**
     * A time to arrival, as the target line prints it after "ETA " (ruling
     * 3): whole seconds under 100 s, "52 S"; whole minutes under an hour,
     * "12 MIN"; hours to a tenth under two days, "4.2 H"; whole days above,
     * "3 D". Each unit is chosen on the rounded value it would print, as
     * the altitude's words are, so it never shows "100 S" or "60 MIN". It
     * ticks each second only for the last hundred seconds of an approach and
     * is calm before. Negative is 0 S; a time that is not finite -- an
     * approach that never arrives -- has no words, and is empty.
     */
    DEEPSPACE_API FString Duration(double Seconds);

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

    /** The same for a jump that may be within the system (decision 12): an
     *  in-system fold is "In the fold", since it is not between stars. */
    DEEPSPACE_API FString JumpWord(EJumpState State, bool bInSystem);

    /** The HUD's line for the jump: "JUMP WINDING", "JUMP READY", or
     *  "BETWEEN STARS" in transit. */
    DEEPSPACE_API FString Jump(EJumpState State);

    /** The same for a jump that may be to a world in this system (decision
     *  12): "IN THE FOLD" in its transit, where a star jump's reads "BETWEEN
     *  STARS", which an in-system fold is not. Every other state reads as a
     *  star jump's: it is the one jump, with one charge and one cone. */
    DEEPSPACE_API FString Jump(EJumpState State, bool bInSystem);

    /** The same with a course: "JUMP READY · Kessa · 12° to port". The
     *  bearing is left off in transit, when there is nothing to steer. */
    DEEPSPACE_API FString Jump(EJumpState State, const FString& CourseName,
                               const FVector& ShipLocalDir, double ConeRadians);

    /** The same, for a course that may be a world in this system: "JUMP
     *  READY · Kessa II · dead ahead" in the jump's cone words, since it is
     *  the jump's cone, and "IN THE FOLD · Kessa II" in its transit. */
    DEEPSPACE_API FString Jump(EJumpState State, bool bInSystem, const FString& CourseName,
                               const FVector& ShipLocalDir, double ConeRadians);
}
