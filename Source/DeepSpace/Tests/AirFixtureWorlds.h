#pragma once

#include "CoreMinimal.h"
#include "Universe/AirFacts.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

/**
 * The atmospheres spec's fixture worlds (*Fixture worlds*), found by its
 * rules among the systems nearest home: the one statement of those rules,
 * which DeepSpace.Atmosphere.FixtureWorlds prints into the spec's table and
 * DeepSpace.Atmosphere.GroundSkySwatch draws. Tests/ only. Each role is the
 * first world, nearest home first and in orbit order, that its rule admits.
 */
namespace AirFixtureWorlds
{
    /** The universe the spec's fixtures are named in (*Fixture worlds*). */
    inline constexpr uint64 UniverseSeed = 20260925;

    /** How far the search goes, in systems nearest home. */
    inline constexpr int32 SearchSystems = 2000;

    struct FFixture
    {
        const TCHAR* Role = TEXT("");
        bool bFound = false;
        FStarSystem System;
        int32 Index = -1;
        double DistanceLy = 0.0;

        const FPlanet& Planet() const
        {
            return System.Planets[Index];
        }
    };

    inline bool IsTemperate(EPlanetKind Kind)
    {
        return Kind == EPlanetKind::Terrestrial || Kind == EPlanetKind::Ocean;
    }

    /** R, G, C, N and J, in that order, among the MaxSystems systems nearest
     *  Root's home under Priors. A role with no world has bFound false. */
    inline TArray<FFixture> Find(uint64 Root, const FGenPriors& Priors, int32 MaxSystems)
    {
        TArray<FFixture> Roles;
        for (const TCHAR* Role : {TEXT("R"), TEXT("G"), TEXT("C"), TEXT("N"), TEXT("J")})
        {
            FFixture& Added = Roles.AddDefaulted_GetRef();
            Added.Role = Role;
        }
        const FGalaxyGenerator Galaxy(Root, Priors);
        const TOptional<FStarSystemStub> Home = Galaxy.GenerateStub(Galaxy.StartSystem());
        if (!Home.IsSet())
        {
            return Roles;
        }
        TArray<FStarSystemStub> Near = Galaxy.FindSystemsWithin(Home->Position, FGalaxyGenerator::MaxSearchRadiusLy * UniverseUnits::CmPerLightYear);
        const FUniversePosition Origin = Home->Position;
        Near.Sort([&Origin](const FStarSystemStub& A, const FStarSystemStub& B) { return Origin.DistanceTo(A.Position) < Origin.DistanceTo(B.Position); });
        Near.SetNum(FMath::Min(Near.Num(), MaxSystems));

        const auto Claim = [&Origin](FFixture& Role, const FStarSystem& System, int32 Index)
        {
            if (!Role.bFound)
            {
                Role.bFound = true;
                Role.System = System;
                Role.Index = Index;
                Role.DistanceLy = Origin.DistanceTo(System.Stub.Position) / UniverseUnits::CmPerLightYear;
            }
        };
        for (int32 S = 0; S < Near.Num(); ++S)
        {
            const FStarSystem System = Galaxy.GenerateSystem(Near[S]);
            for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
            {
                const FPlanet& Planet = System.Planets[Index];
                const bool bNitrogen = IsTemperate(Planet.Kind) && Planet.AirMix == EAirMix::NitrogenOxygen;
                // R: home's N2/O2 world, else the nearest N2/O2 world under an M dwarf.
                if (bNitrogen && (S == 0 || System.Star.Class == EStarClass::M))
                {
                    Claim(Roles[0], System, Index);
                }
                // G: the nearest N2/O2 world under a 5,000-6,000 K star.
                if (bNitrogen && System.Star.TemperatureK >= 5000.0 && System.Star.TemperatureK <= 6000.0)
                {
                    Claim(Roles[1], System, Index);
                }
                // C: the nearest CO2 world.
                if (IsTemperate(Planet.Kind) && Planet.AirMix == EAirMix::CarbonDioxide)
                {
                    Claim(Roles[2], System, Index);
                }
                // N: home's first barren world.
                if (S == 0 && Planet.Kind == EPlanetKind::Barren)
                {
                    Claim(Roles[3], System, Index);
                }
                // J: the nearest giant.
                if (Planet.Kind == EPlanetKind::GasGiant)
                {
                    Claim(Roles[4], System, Index);
                }
            }
        }
        return Roles;
    }
}
