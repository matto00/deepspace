#include "Sky/LocalSystem.h"
#include "Engine/World.h"
#include "Universe/GenSeed.h"
#include "Ship/ShipSubsystem.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

namespace
{
    /**
     * How far out the neighbours the sky draws reach: the chart's own 12 ly,
     * procgen's nearby radius. Every star you could be sent to is drawn, and
     * nothing is drawn that the universe has not already been asked about.
     * About thirty systems at procgen's density.
     */
    constexpr double NeighbourRadiusCm = 12.0 * UniverseUnits::CmPerLightYear;

    const UShipSubsystem* ShipOf(const UWorld* World)
    {
        return World ? World->GetSubsystem<UShipSubsystem>() : nullptr;
    }
}

// Each function asks the subsystem that owns the answer, every call, and
// keeps nothing. With no world, no ship or no universe it answers as if
// nothing were out there -- the null-world branch the sky's world-free tests
// run against.

int32 LocalSystem::Serial(const UWorld* World)
{
    const UShipSubsystem* Ship = ShipOf(World);
    return Ship ? Ship->GetJumpSerial() : 0;
}

FSkySystem LocalSystem::Current(const UWorld* World)
{
    const UShipSubsystem* Ship = ShipOf(World);
    const UUniverseSubsystem* Universe = World ? World->GetSubsystem<UUniverseSubsystem>() : nullptr;
    if (!Ship || !Universe)
    {
        return FSkySystem();
    }

    const FUniversePosition Where = Ship->GetFlightState().GetUniversePosition();
    const TOptional<FStarSystem> Here = Universe->GetSystemAt(Where);
    if (!Here)
    {
        return FSkySystem();
    }
    return FSkySystem::FromSystem(*Here, Universe->GetSystemsNear(Where, NeighbourRadiusCm));
}

bool LocalSystem::InTransit(const UWorld* World)
{
    const UShipSubsystem* Ship = ShipOf(World);
    return Ship && Ship->IsInTransit();
}

uint64 LocalSystem::StarfieldSeed(const UWorld* World, uint64 Fallback)
{
    constexpr uint64 Purpose = GenSeed::Label("sky.starfield");
    const UUniverseSubsystem* Universe = World ? World->GetSubsystem<UUniverseSubsystem>() : nullptr;
    return GenSeed::Derive(Universe ? Universe->GetRootSeed() : Fallback, Purpose);
}

double LocalSystem::NearestSurfaceDistance(const FSkySystem& System, const FUniversePosition& Where)
{
    return NearestSurface(System, Where).Distance;
}

FSkyNearestSurface LocalSystem::NearestSurface(const FSkySystem& System, const FUniversePosition& Where)
{
    FSkyNearestSurface Nearest;
    if (System.IsEmpty())
    {
        return Nearest;
    }

    double Distance = TNumericLimits<double>::Max();
    const FSkyBody* Star = nullptr;
    for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = System.Bodies[Index];
        const double ToSurface = Where.DistanceTo(Body.Position) - Body.Radius;
        if (ToSurface < Distance)
        {
            Distance = ToSurface;
            Nearest.Body = Index;
        }
        if (!Star && Body.Kind == ESkyBodyKind::Star)
        {
            Star = &Body;
        }
    }

    // The edge is a surface too (plan conflict 10): leaving a planet the drive
    // speeds up, and without this it would carry on out past the point where
    // GetSystemAt stops answering. You leave a system by jumping.
    if (Star && System.EdgeRadius > 0.0)
    {
        const double ToEdge = System.EdgeRadius - Where.DistanceTo(Star->Position);
        if (ToEdge < Distance)
        {
            Distance = ToEdge;
            Nearest.Body = INDEX_NONE;
            Nearest.bEdge = true;
        }
    }
    Nearest.Distance = FMath::Max(Distance, 0.0);
    return Nearest;
}
