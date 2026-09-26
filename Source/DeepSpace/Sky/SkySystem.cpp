#include "Sky/SkySystem.h"
#include "Sky/SkyColour.h"
#include "Universe/GenSeed.h"
#include "Universe/UniverseUnits.h"

namespace
{
    /** How a kind of world looks from space. Procgen decides what a world is;
     *  this is only what that looks like, so it lives on the sky's side of
     *  the seam with the blackbody. */
    struct FWorldLook
    {
        double Albedo;
        FLinearColor Colour;
        FLinearColor Rim;
        ESkySurface Surface = ESkySurface::Rocky;
        double Cratering = 0.0;
    };

    /** The face's purpose label: a child of the system's seed that no
     *  generator stream reads, so choosing a look changes no world. */
    constexpr uint64 SurfacePurpose = GenSeed::Label("sky.surface");

    FWorldLook LookOf(EPlanetKind Kind)
    {
        switch (Kind)
        {
        // Bare rock, dark as the Moon and Mercury are: a tenth of the light back.
        case EPlanetKind::Barren:
            // Airless and dead, so every impact it ever took is still there.
            return { 0.12, FLinearColor(0.55f, 0.50f, 0.46f), FLinearColor::Black, ESkySurface::Rocky, 1.0 };
        // Land, cloud and a thin sky, Earth's 0.3, with a blue limb.
        case EPlanetKind::Terrestrial:
            // Weather and plates erase craters in a few hundred million
            // years: Earth keeps a handful, softened.
            return { 0.30, FLinearColor(0.50f, 0.52f, 0.44f), FLinearColor(0.18f, 0.32f, 0.70f), ESkySurface::Rocky, 0.15 };
        // Water under cloud: darker body, the same sky.
        case EPlanetKind::Ocean:
            return { 0.28, FLinearColor(0.24f, 0.38f, 0.60f), FLinearColor(0.18f, 0.32f, 0.70f), ESkySurface::Rocky, 0.0 };
        // Frost reflects most of what reaches it, as Europa does.
        case EPlanetKind::Ice:
            // Between Europa, resurfaced smooth, and Callisto, saturated.
            return { 0.60, FLinearColor(0.86f, 0.89f, 0.93f), FLinearColor::Black, ESkySurface::Rocky, 0.5 };
        // Banded cloud tops, Jupiter's half, with a faint haze at the limb.
        case EPlanetKind::GasGiant:
            return { 0.50, FLinearColor(0.80f, 0.70f, 0.56f), FLinearColor(0.20f, 0.18f, 0.14f), ESkySurface::Banded };
        }
        return { 0.30, FLinearColor::White, FLinearColor::Black };
    }
}

FSkySystem FSkySystem::FromSystem(const FStarSystem& System, TConstArrayView<FStarSystemStub> Neighbours)
{
    FSkySystem Sky;
    Sky.SystemId = FName(*System.Stub.Name);
    Sky.EdgeRadius = FStarSystem::InSystemRadiusCm;

    FSkyBody& Star = Sky.Bodies.AddDefaulted_GetRef();
    Star.Id = Sky.SystemId;
    Star.Kind = ESkyBodyKind::Star;
    Star.Position = System.Stub.Position;
    Star.Radius = System.Star.RadiusSolar * UniverseUnits::CmPerSolarRadius;
    Star.Colour = SkyColour::Blackbody(System.Star.TemperatureK);
    Star.Luminosity = System.Star.LuminositySolar;
    Star.TemperatureK = System.Star.TemperatureK;

    for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
    {
        const FPlanet& Planet = System.Planets[Index];
        const FWorldLook Look = LookOf(Planet.Kind);

        FSkyBody& Body = Sky.Bodies.AddDefaulted_GetRef();
        Body.Id = FName(*Planet.Designation);
        Body.Kind = ESkyBodyKind::Planet;
        Body.Position = System.PlanetPosition(Index);
        Body.Radius = Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius;
        Body.Colour = Look.Colour;
        Body.Albedo = Look.Albedo;
        Body.Rim = Look.Rim;
        Body.Surface = Look.Surface;
        Body.Cratering = Look.Cratering;
        // By orbit index, which never renumbers (FStarSystem::Planets): a
        // world keeps its face however many planets are added outside it.
        Body.SurfaceSeed = GenSeed::Derive(System.Stub.Seed, SurfacePurpose, static_cast<uint64>(Index));
        Body.BeltPairs = Look.Surface == ESkySurface::Banded ? SkyLook::BeltPairs(Planet.DayHours) : 0.0;
    }

    for (const FStarSystemStub& Stub : Neighbours)
    {
        if (Stub.Id == System.Stub.Id)
        {
            continue;
        }
        const FVector Delta = Stub.Position - System.Stub.Position;
        const double Distance = Delta.Size();

        FSkyNeighbour& Neighbour = Sky.Neighbours.AddDefaulted_GetRef();
        Neighbour.SystemId = FName(*Stub.Name);
        Neighbour.Direction = Distance > 0.0 ? Delta / Distance : FVector::ForwardVector;
        Neighbour.Distance = Distance;
        Neighbour.Luminosity = Stub.LuminositySolar;
        Neighbour.TemperatureK = Stub.TemperatureK;
    }
    return Sky;
}

double SkyLook::BeltPairs(double DayHours)
{
    return DayHours > 0.0 ? JupiterBeltPairs * FMath::Sqrt(JupiterDayHours / DayHours) : 0.0;
}
