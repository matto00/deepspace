#include "UI/TargetMarker.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Ship/ShipFlightSurface.h"
#include "Ship/ShipTags.h"
#include "UI/NavText.h"
#include "UI/NavigationWidget.h"
#include "UI/ShipHUDWidget.h"
#include "Universe/UniverseUnits.h"

namespace
{
    /** The world an id names in System, or nothing: another system's id, an
     *  orbit it lacks, or a moon, which procgen does not make yet. */
    const FPlanet* Resolve(const FStarSystem& System, const FBodyId& Id)
    {
        if (Id.System != System.Stub.Id || Id.Moon != -1 || !System.Planets.IsValidIndex(Id.Planet))
        {
            return nullptr;
        }
        return &System.Planets[Id.Planet];
    }
}

TOptional<FTargetView> TargetMarker::View(const FStarSystem& System, const FBodyId& Id,
                                          const FUniversePosition& Ship, const FQuat& Orientation,
                                          const FVector& Velocity, double FloorCm, double BrakingAccel,
                                          double HoldSeconds, bool bInTransit)
{
    const FPlanet* Planet = Resolve(System, Id);
    if (bInTransit || !Planet)
    {
        return {};
    }

    // The same position and radius the sky draws it at (FSkySystem::
    // FromSystem), so the bracket lands on the disc.
    const FUniversePosition Centre = System.PlanetPosition(Id.Planet);
    const double Radius = Planet->RadiusEarth * UniverseUnits::CmPerEarthRadius;
    // Through the chunk index, never the offsets (ADR 0007).
    const FVector ToCentre = Centre - Ship;
    const double Distance = ToCentre.Size();

    FTargetView View;
    View.Id = Id;
    View.Name = NavText::WorldName(*Planet);
    View.CentreDistance = Distance;
    View.SurfaceDistance = FMath::Max(Distance - Radius, 0.0);
    View.ShipLocalDir = Orientation.UnrotateVector(ToCentre).GetSafeNormal();
    View.AngularRadius = Distance > Radius ? FMath::Asin(Radius / Distance) : 0.5 * UE_DOUBLE_PI;
    View.AheadRadians = FMath::Max(View.AngularRadius, AheadFloor);

    // Phase angle: the star and the ship, as seen from the world.
    const FVector ToStar = (System.Stub.Position - Centre).GetSafeNormal();
    const FVector ToShip = (-ToCentre).GetSafeNormal();
    const double CosAlpha = FMath::Clamp(FVector::DotProduct(ToStar, ToShip), -1.0, 1.0);
    View.LitFraction = 0.5 * (1.0 + CosAlpha);
    View.bNightSide = View.LitFraction < NightSideLit;

    // The live ETA (ruling 3). The cap's own test of whether this path comes
    // down on the world, and its own law for how long that takes: the time
    // is the flight, not an estimate beside it.
    const double Speed = Velocity.Size();
    if (Speed >= MinSpeed)
    {
        FFlightSurface Floor;
        Floor.Centre = Centre;
        Floor.Radius = Radius;
        Floor.Floor = FMath::Max(FloorCm, 0.0);
        const TOptional<double> ToFloor = ShipFlight::RayToFloor(Floor, Ship, Velocity);
        if (ToFloor)
        {
            const double Seconds = ShipFlight::SecondsToFloor(*ToFloor, Speed, BrakingAccel, HoldSeconds);
            if (FMath::IsFinite(Seconds))
            {
                View.EtaSeconds = Seconds;
            }
        }
        else if (FVector::DotProduct(Velocity, ToCentre) > 0.0)
        {
            // A path that misses: how high it passes, never a time to
            // nowhere. The miss from a cross product, as RayToFloor takes
            // it, so a grazing pass from far out keeps its digits.
            const double Miss = FVector::CrossProduct(ToCentre, Velocity / Speed).Size();
            View.PassingCm = FMath::Max(Miss - Radius, 0.0);
        }
    }
    return View;
}

FString TargetMarker::Line(const FTargetView& View)
{
    FString Line = FString(UNavigationWidget::PlottedMark) + TEXT(" ") + View.Name;
    Line += NavText::Separator + NavText::TargetBearing(View.ShipLocalDir, View.AheadRadians);
    Line += NavText::Separator + UShipHUDWidget::AltitudeWords(View.SurfaceDistance);
    if (View.EtaSeconds)
    {
        Line += NavText::Separator + (TEXT("ETA ") + NavText::Duration(*View.EtaSeconds));
    }
    else if (View.PassingCm)
    {
        Line += NavText::Separator + (TEXT("PASSING ") + UShipHUDWidget::AltitudeWords(*View.PassingCm) + TEXT(" UP"));
    }
    if (View.bNightSide)
    {
        Line += NavText::Separator;
        Line += TEXT("NIGHT SIDE");
    }
    return Line;
}

FVector TargetMarker::ProgradeShipLocal(const FVector& Velocity, const FQuat& Orientation)
{
    return Velocity.Size() >= MinSpeed ? Orientation.UnrotateVector(Velocity).GetSafeNormal() : FVector::ZeroVector;
}

FTargetMark TargetMarker::Place(bool bProjected, FVector2D Centre, float RadiusPx, FVector ViewSpaceDir,
                                FVector2D ViewSize, float MinPx, float Inset, bool bPilot, bool bSeenThroughGlass)
{
    FTargetMark Mark;
    if (!(ViewSize.X > 0.0) || !(ViewSize.Y > 0.0))
    {
        return Mark;
    }

    const bool bOnView = bProjected && Centre.X >= 0.0 && Centre.X <= ViewSize.X && Centre.Y >= 0.0
                         && Centre.Y <= ViewSize.Y;
    if (bOnView)
    {
        // The bracket is drawn from geometry, never from brightness, so a
        // black disc on the night side is bracketed like a lit one.
        if (!bSeenThroughGlass)
        {
            return Mark;
        }
        const float Side = FMath::Max(MinPx, 2.0f * (FMath::Max(RadiusPx, 0.0f) + BracketPadding));
        if (Side > HideFraction * static_cast<float>(FMath::Min(ViewSize.X, ViewSize.Y)))
        {
            return Mark;
        }
        Mark.Shape = ETargetMarkShape::Bracket;
        Mark.Centre = Centre;
        Mark.Size = Side;
        return Mark;
    }

    // Off the view the chevron is an aiming aid, like the caret, and the
    // pilot's alone: a chevron following a walker round the galley would be
    // noise. Not occluded, since it claims only which way to look.
    if (!bPilot)
    {
        return Mark;
    }
    const FVector2D Middle = 0.5 * ViewSize;
    // Where the projection worked, its own offset from the centre is the way
    // to turn. Behind the camera it fails, and the camera's axes say it:
    // right is +X on the widget and up is -Y.
    FVector2D Way = bProjected ? Centre - Middle : FVector2D(ViewSpaceDir.Y, -ViewSpaceDir.Z);
    if (!Way.Normalize())
    {
        // Dead astern every way round is as short; it points down, at the
        // deck, which is where a head turning round looks first.
        Way = FVector2D(0.0, 1.0);
    }
    const FVector2D Reach(FMath::Max(Middle.X - Inset, 0.0), FMath::Max(Middle.Y - Inset, 0.0));
    const double ToSide = FMath::Abs(Way.X) > UE_DOUBLE_SMALL_NUMBER ? Reach.X / FMath::Abs(Way.X) : UE_DOUBLE_BIG_NUMBER;
    const double ToTop = FMath::Abs(Way.Y) > UE_DOUBLE_SMALL_NUMBER ? Reach.Y / FMath::Abs(Way.Y) : UE_DOUBLE_BIG_NUMBER;

    Mark.Shape = ETargetMarkShape::Chevron;
    Mark.Centre = Middle + Way * FMath::Min(ToSide, ToTop);
    Mark.Pointing = Way;
    return Mark;
}

bool TargetMarker::SeenThroughGlass(const UWorld* World, FVector Eye, FVector Dir, const AActor* Viewer)
{
    const FVector Unit = Dir.GetSafeNormal();
    if (!World || Unit.IsZero())
    {
        return false;
    }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(TargetSeenThroughGlass), false, Viewer);
    const FVector End = Eye + Unit * GlassTraceCm;
    // What the eye starts inside hides nothing: it is stepped past and the
    // trace asked again. The camera is swept clear of every wall
    // (ADeepSpaceCharacter::PlaceCamera), so anything that encloses it is a
    // volume that blocks Visibility for another reason -- the helm seat's
    // reach box, there so a standing player's E finds the chair, envelops
    // the seated pilot's head, and taken as a wall it hid the target from
    // the one person steering. Bounded, since each pass ignores one more.
    for (int32 Pass = 0; Pass < MaxEnclosing; ++Pass)
    {
        FHitResult Hit;
        if (!World->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Params))
        {
            return true;
        }
        if (Hit.bStartPenetrating && Hit.GetComponent())
        {
            Params.AddIgnoredComponent(Hit.GetComponent());
            continue;
        }
        const AActor* Blocker = Hit.GetActor();
        return Blocker && Blocker->ActorHasTag(ShipTags::Glass);
    }
    return false;
}
