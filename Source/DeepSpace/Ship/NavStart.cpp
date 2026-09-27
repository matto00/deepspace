#include "Ship/NavStart.h"

namespace
{
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

double NavStart::OpeningDistanceCm(const FPlanet& Planet)
{
    return OpeningDistancePerEarthRadiusCm * FMath::Max(0.0, Planet.RadiusEarth);
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

    // Procgen's one conversion from an orbit to a position, not a second copy of it.
    const FUniversePosition Planet = System.PlanetPosition(Index);

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
    Placement.Position = Planet + (-Nose * OpeningDistanceCm(System.Planets[Index]));

    // Nose exact; starboard is the star's direction from the ship, squared
    // off against the nose. An Earth at 1 AU, framed from 40,000 km, leaves
    // the star 0.015 degrees off the beam; a Jupiter at 5 AU from 450,000 km,
    // 0.03 degrees.
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
    const double StarRadiusAU = System.Star.RadiusSolar * UniverseUnits::CmPerSolarRadius / UniverseUnits::CmPerAU;
    return FMath::Max(ArrivalStandoffAU(System.Star.LuminositySolar, Outermost, StandoffAU),
                      MinStandoffStellarRadii * FMath::Max(0.0, StarRadiusAU));
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

double NavStart::WorldStandoffCm(double RadiusCm, double FloorCm, double StandoffDeg)
{
    const double Radius = FMath::Max(0.0, RadiusCm);
    // Half the angle subtended, and never a whole hemisphere: a standoff
    // CVar set silly still gives a point outside the world.
    const double HalfAngle = FMath::Clamp(FMath::DegreesToRadians(StandoffDeg) * 0.5, 1.0e-6, 0.5 * UE_DOUBLE_PI);
    return FMath::Max(Radius / FMath::Sin(HalfAngle), Radius + WorldStandoffFloors * FMath::Max(0.0, FloorCm));
}

FUniversePosition NavStart::WorldArrivalPoint(const FUniversePosition& From, const FUniversePosition& Centre,
                                              double RadiusCm, double FloorCm, double StandoffDeg,
                                              TConstArrayView<FFlightSurface> Others)
{
    // Outward from the world toward where the fold opened, through the
    // chunk index (ADR 0007).
    FVector Back = (From - Centre).GetSafeNormal();
    if (Back.IsZero())
    {
        Back = -FVector::ForwardVector;
    }
    double Out = WorldStandoffCm(RadiusCm, FloorCm, StandoffDeg);

    // Each floor sphere the point is inside pushes it to that sphere's far
    // side along the line. A push can land it in another, so go round until
    // a pass moves nothing; the spheres do not overlap in any system procgen
    // makes, so a pass or two settles it, and the bound only guards a
    // pathological list.
    for (int32 Pass = 0; Pass < 16; ++Pass)
    {
        bool bMoved = false;
        for (const FFlightSurface& Other : Others)
        {
            if (Other.bInsideOut)
            {
                continue;
            }
            const double Shell = FMath::Max(0.0, Other.Radius) + FMath::Max(0.0, Other.Floor);
            // The line is Centre + Back x s; the sphere, |Centre + Back x s - C| = Shell.
            const FVector ToOther = Other.Centre - Centre;
            const double Along = FVector::DotProduct(ToOther, Back);
            const double Miss2 = ToOther.SizeSquared() - Along * Along;
            if (Miss2 >= Shell * Shell)
            {
                continue;
            }
            const double Half = FMath::Sqrt(Shell * Shell - Miss2);
            if (Out > Along - Half && Out < Along + Half)
            {
                // A metre past its far side, so the arrival is outside it
                // and not on it.
                Out = Along + Half + 100.0;
                bMoved = true;
            }
        }
        if (!bMoved)
        {
            break;
        }
    }
    return Centre + Back * Out;
}
