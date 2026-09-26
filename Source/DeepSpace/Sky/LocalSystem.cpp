#include "Sky/LocalSystem.h"
#include "Engine/World.h"
#include "Universe/GenSeed.h"
#include "Universe/UniverseSubsystem.h"

// The null-world branch: what every function answers when nothing is out
// there to ask. The live branch -- the ship subsystem's jump serial and
// transit flag, and GetSystemAt(ship position) through FromSystem -- lands
// once procgen and navigation are merged, and changes nothing in this file's
// answers for a world that has neither.

int32 LocalSystem::Serial(const UWorld* World)
{
    return 0;
}

FSkySystem LocalSystem::Current(const UWorld* World)
{
    return FSkySystem();
}

bool LocalSystem::InTransit(const UWorld* World)
{
    return false;
}

uint64 LocalSystem::StarfieldSeed(const UWorld* World, uint64 Fallback)
{
    constexpr uint64 Purpose = GenSeed::Label("sky.starfield");
    const UUniverseSubsystem* Universe = World ? World->GetSubsystem<UUniverseSubsystem>() : nullptr;
    return GenSeed::Derive(Universe ? Universe->GetRootSeed() : Fallback, Purpose);
}

double LocalSystem::NearestSurfaceDistance(const FSkySystem& System, const FUniversePosition& Where)
{
    if (System.IsEmpty())
    {
        return 0.0;
    }

    double Nearest = TNumericLimits<double>::Max();
    const FSkyBody* Star = nullptr;
    for (const FSkyBody& Body : System.Bodies)
    {
        Nearest = FMath::Min(Nearest, Where.DistanceTo(Body.Position) - Body.Radius);
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
        Nearest = FMath::Min(Nearest, System.EdgeRadius - Where.DistanceTo(Star->Position));
    }
    return FMath::Max(Nearest, 0.0);
}
