#pragma once

#include "CoreMinimal.h"
#include "Universe/StarSystem.h"
#include "Universe/UniversePosition.h"

class AActor;
class UWorld;

/**
 * The target as the ship sees it: a world in the system it is in, which the
 * pilot marked at the map (system map spec, decisions 5-7). Everything the
 * HUD and the map say about it, and every number the bracket is drawn from,
 * is worked out here, once.
 *
 * Asked, never stored: View is computed from the system, the ship's position,
 * attitude and velocity every time it is wanted. The target is an id; the
 * world's position, radius, name and kind are procgen's.
 */
struct DEEPSPACE_API FTargetView
{
    FBodyId Id;

    /** NavText::WorldName: "Kessa II", or "Halden · Kessa II". */
    FString Name;

    /** Unit, ship axes (+X the nose, +Y starboard, +Z up), from the ship's
     *  origin toward the world's centre. Ship axes are world axes (ADR
     *  0005), so this is also the direction the HUD projects from the
     *  camera. */
    FVector ShipLocalDir = FVector::ForwardVector;

    /** Ship's origin to the world's centre, cm. */
    double CentreDistance = 0.0;

    /** To its surface, cm, never negative: the altitude line's measure. */
    double SurfaceDistance = 0.0;

    /** Its true angular radius, rad: what the sky draws once resolved. */
    double AngularRadius = 0.0;

    /** How far off the nose is still on the world, rad: max(AngularRadius,
     *  TargetMarker::AheadFloor). What NavText::TargetBearing is given. */
    double AheadRadians = 0.0;

    /** The fraction of its visible disc that is lit, (1 + cos alpha) / 2 at
     *  phase angle alpha: 1 full, 0.5 half, 0 new. */
    double LitFraction = 1.0;

    /** LitFraction under TargetMarker::NightSideLit: it is there, and it is
     *  its dark side that faces the ship. */
    bool bNightSide = false;

    /** Seconds to its floor at the present speed, when the velocity's ray
     *  meets the floor sphere (decision 6, ruling 3). */
    TOptional<double> EtaSeconds;

    /** When closing on a path that misses: the ray's closest approach less
     *  the world's radius, cm. Never set with EtaSeconds. */
    TOptional<double> PassingCm;
};

/** What TargetMarker::Place decided to draw. */
enum class ETargetMarkShape : uint8
{
    /** Nothing: no target in view that can be seen, or a world so large the
     *  glass is full of it. */
    None,

    /** Four corner ticks round a square, on the world. */
    Bracket,

    /** A chevron at the view's edge, pointing the way to turn. */
    Chevron,
};

struct DEEPSPACE_API FTargetMark
{
    ETargetMarkShape Shape = ETargetMarkShape::None;

    /** Widget space (+X right, +Y down), slate units: the bracket's centre,
     *  or where the chevron sits. */
    FVector2D Centre = FVector2D::ZeroVector;

    /** The bracket's side, slate units. 0 for a chevron. */
    float Size = 0.0f;

    /** The chevron's direction, unit, widget space: the way to look. Zero
     *  for a bracket. */
    FVector2D Pointing = FVector2D::ZeroVector;
};

/**
 * The target's arithmetic, pure except SeenThroughGlass, the one world query.
 * No UObject is held and no console variable read: the overlay reads
 * ds.HUD.TargetMinPixels and ds.HUD.TargetEdgeInset and passes them in, with
 * the defaults below.
 */
namespace TargetMarker
{
    /** The least off-boresight that still reads "dead ahead": 0.25 degrees.
     *  At 0.03 AU an Earth's own radius is 0.08 degrees, and 0.25 is 19,600
     *  km there, three Earth radii -- already inside the bracket's minimum,
     *  so the words and the mark agree about what "on it" looks like. Close
     *  in, the disc itself governs. */
    inline constexpr double AheadFloor = 0.25 * UE_DOUBLE_PI / 180.0;

    /** Under this share of its disc lit, a world is on its night side: a
     *  phase angle past about 134 degrees, where the Lambert disc gives back
     *  under 5% of full phase and four pixels of it are black on black. */
    inline constexpr double NightSideLit = 0.15;

    /** Below 1 m/s the ship is at rest, for the ETA and the prograde mark:
     *  a drift that slow has no direction worth drawing and no arrival
     *  worth a time. */
    inline constexpr double MinSpeed = 100.0;

    /** ds.HUD.TargetMinPixels' default, slate units: the smallest bracket,
     *  so a sub-pixel world and four black pixels on the night side get a
     *  mark that can be found across the glass. */
    inline constexpr float DefaultMinPixels = 28.0f;

    /** ds.HUD.TargetEdgeInset's default, slate units: how far inside the
     *  view's edge the chevron sits, clear of the corners' text. */
    inline constexpr float DefaultEdgeInset = 48.0f;

    /** Round a resolved disc, the bracket stands this far off each side of
     *  it, slate units: close enough to belong to it, far enough not to be
     *  read as its limb. */
    inline constexpr float BracketPadding = 6.0f;

    /** Past this share of the view's shorter side the bracket is not drawn:
     *  the world is its own marker, and a bracket round the whole glass says
     *  nothing. The HUD line still names it. */
    inline constexpr float HideFraction = 0.8f;

    /** How far the glass trace looks, cm: 30 m, past every wall of a 26 m
     *  hull from anywhere inside it, and nowhere near the sky's 50 km. */
    inline constexpr double GlassTraceCm = 3000.0;

    /**
     * The target as seen from the ship: unset when the id does not name a
     * world of System (another system's, an orbit it lacks, a moon), and in
     * transit, when the sky is hidden and the target line is empty.
     *
     * Velocity is the ship's, universe axes, cm/s. The ETA and PASSING are
     * decision 6's: at 1 m/s or more, an ETA exactly when the velocity's ray
     * meets the world's floor sphere (ShipFlight::RayToFloor, the cap's own
     * test), at ShipFlight::SecondsToFloor of that distance, the present
     * speed, BrakingAccel (cm/s^2, the boosters' present acceleration) and
     * HoldSeconds; PASSING when the ship closes on a path that misses; and
     * neither at rest or opening. FloorCm is the floor over this world, the
     * subsystem's FloorFor: the soft cap stops there, so that is where the
     * ship arrives.
     */
    DEEPSPACE_API TOptional<FTargetView> View(const FStarSystem& System, const FBodyId& Id,
                                              const FUniversePosition& Ship, const FQuat& Orientation,
                                              const FVector& Velocity, double FloorCm, double BrakingAccel,
                                              double HoldSeconds, bool bInTransit);

    /**
     * The target line, which the HUD and the map both print (decision 6):
     * "› Kessa II · 0.1° to starboard · 1,496 THOUSAND KM · ETA 65 S ·
     * NIGHT SIDE". The name, NavText::TargetBearing, the surface distance in
     * UShipHUDWidget::AltitudeWords, then "ETA <Duration>" or "PASSING
     * <altitude> UP" or neither, then "NIGHT SIDE" when it is.
     */
    DEEPSPACE_API FString Line(const FTargetView& View);

    /** The ship's velocity in ship axes, unit, for the prograde mark; zero
     *  under MinSpeed, when it is hidden. */
    DEEPSPACE_API FVector ProgradeShipLocal(const FVector& Velocity, const FQuat& Orientation);

    /**
     * What the overlay draws for the target, and where (decision 7), from
     * the projection the overlay made. Pure: every input is a parameter.
     *
     * - bProjected, Centre: whether the direction, laid out from the camera,
     *   projected to the widget, and where. False behind the camera.
     * - RadiusPx: the disc's projected radius, slate units; under a unit for
     *   a point.
     * - ViewSpaceDir: the direction in the camera's axes (+X forward, +Y
     *   right, +Z up), which says which way to turn when the projection
     *   cannot.
     * - ViewSize, MinPx, Inset: the widget's size, the least bracket, the
     *   chevron's inset from the edge.
     * - bPilot: the viewer is at the helm. The chevron is the pilot's alone.
     * - bSeenThroughGlass: SeenThroughGlass along the direction.
     *
     * On the view and seen through the glass: a bracket, centred, the larger
     * of MinPx and the disc's diameter plus BracketPadding a side, or nothing
     * once that is wider than HideFraction of the shorter side. On the view
     * but behind a wall: nothing, for everyone. Off it, or behind the
     * camera: a chevron for the pilot, Inset inside the edge along the way to
     * turn, and nothing for anyone else.
     */
    DEEPSPACE_API FTargetMark Place(bool bProjected, FVector2D Centre, float RadiusPx, FVector ViewSpaceDir,
                                    FVector2D ViewSize, float MinPx, float Inset, bool bPilot,
                                    bool bSeenThroughGlass);

    /**
     * Whether the sky can be seen from Eye along Dir: a Visibility trace for
     * GlassTraceCm, ignoring Viewer, that meets nothing or meets an actor
     * tagged ShipTags::Glass first. So the bracket is shown to anyone who
     * can see the target -- the pilot, someone at the fore glass, someone in
     * the galley whose window faces it -- and never drawn on a wall. The
     * dressing's clutter blocks only the camera channel, so a mug never
     * hides it. False with no world or no direction: no bracket is the safe
     * failure.
     */
    DEEPSPACE_API bool SeenThroughGlass(const UWorld* World, FVector Eye, FVector Dir, const AActor* Viewer);
}
