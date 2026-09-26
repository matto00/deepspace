#include "Ship/NavStart.h"

namespace
{
    /**
     * A planet's centre. This is FStarSystem::PlanetPosition's documented
     * rule -- the star's position plus a (cos phase, sin phase, 0) through
     * UniverseUnits -- written out here only because procgen's StarSystem.cpp
     * lands concurrently. Once it is in the tree this becomes a call to
     * PlanetPosition, so there is one conversion and not two.
     */
    FUniversePosition PlanetCentre(const FStarSystem& System, int32 Index)
    {
        const FPlanet& Planet = System.Planets[Index];
        const double RadiusCm = Planet.SemiMajorAxisAU * UniverseUnits::CmPerAU;
        return System.Stub.Position
            + FVector(FMath::Cos(Planet.PhaseRad), FMath::Sin(Planet.PhaseRad), 0.0) * RadiusCm;
    }

    int32 LargestPlanet(const FStarSystem& System)
    {
        int32 Largest = INDEX_NONE;
        for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
        {
            if (Largest == INDEX_NONE || System.Planets[Index].RadiusEarth > System.Planets[Largest].RadiusEarth)
            {
                Largest = Index;
            }
        }
        return Largest;
    }
}

FNavPlacement NavStart::OpeningPlacement(const FStarSystem& System)
{
    const int32 Index = LargestPlanet(System);
    if (Index == INDEX_NONE)
    {
        // From the far side of the star along universe -X, facing it.
        const double Standoff = ArrivalStandoffAU(System) * UniverseUnits::CmPerAU;
        const FUniversePosition From = System.Stub.Position + FVector(-2.0 * Standoff, 0.0, 0.0);
        FNavPlacement Placement;
        Placement.Position = ArrivalPoint(From, System);
        Placement.Orientation = FQuat::Identity;
        return Placement;
    }

    const FUniversePosition Planet = PlanetCentre(System, Index);

    // Toward the star from the planet, in the orbital plane. The ship faces
    // the planet along a line square to that, so the star is abeam: +Y is
    // starboard, and X = Y x Z keeps the ship's up on the system's up.
    FVector ToStar = (System.Stub.Position - Planet).GetSafeNormal();
    if (ToStar.IsNearlyZero())
    {
        // A planet at the star's centre is not a system anyone generates;
        // any abeam direction will do.
        ToStar = FVector::RightVector;
    }
    const FVector Nose = FVector::CrossProduct(ToStar, FVector::UpVector).GetSafeNormal();

    FNavPlacement Placement;
    Placement.Position = Planet + (-Nose * OpeningDistanceCm);

    // Nose exact; starboard is the star's direction from the ship, squared
    // off against the nose. From 40,000 km out a planet at 1 AU leaves the
    // star 0.015 degrees off the beam.
    const FVector StarFromShip = System.Stub.Position - Placement.Position;
    Placement.Orientation = FRotationMatrix::MakeFromXY(Nose, StarFromShip).ToQuat();
    return Placement;
}

double NavStart::ArrivalStandoffAU(double LuminositySolar, double OutermostOrbitAU, double StandoffAU)
{
    const double Irradiance = StandoffAU * FMath::Sqrt(FMath::Max(0.0, LuminositySolar));
    return FMath::Max(Irradiance, 1.5 * FMath::Max(0.0, OutermostOrbitAU));
}

double NavStart::ArrivalStandoffAU(const FStarSystem& System, double StandoffAU)
{
    double Outermost = 0.0;
    for (const FPlanet& Planet : System.Planets)
    {
        Outermost = FMath::Max(Outermost, Planet.SemiMajorAxisAU);
    }
    return ArrivalStandoffAU(System.Star.LuminositySolar, Outermost, StandoffAU);
}

FUniversePosition NavStart::ArrivalPoint(const FUniversePosition& From, const FStarSystem& Destination,
                                         double StandoffAU)
{
    // Through FUniversePosition::operator-, never through offsets: the two
    // ends are light years apart and in different chunks.
    FVector Dir = (Destination.Stub.Position - From).GetSafeNormal();
    if (Dir.IsZero())
    {
        Dir = FVector::ForwardVector;
    }
    const double StandoffCm = ArrivalStandoffAU(Destination, StandoffAU) * UniverseUnits::CmPerAU;
    return Destination.Stub.Position + (-Dir * StandoffCm);
}
