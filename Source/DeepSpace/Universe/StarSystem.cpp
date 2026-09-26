#include "Universe/StarSystem.h"

#include <cmath>

FUniversePosition FStarSystem::PlanetPosition(int32 Index) const
{
    check(Planets.IsValidIndex(Index));

    const FPlanet& Planet = Planets[Index];
    const double RadiusCm = Planet.SemiMajorAxisAU * UniverseUnits::CmPerAU;

    // Circular and coplanar in the universe XY plane: nothing on screen can
    // yet show an orbit's shape or tilt.
    return Stub.Position + FVector(RadiusCm * std::cos(Planet.PhaseRad), RadiusCm * std::sin(Planet.PhaseRad), 0.0);
}
