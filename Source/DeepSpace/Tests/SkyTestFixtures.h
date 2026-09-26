#pragma once

#include "CoreMinimal.h"
#include "Sky/SkySystem.h"
#include "Universe/UniversePosition.h"

/**
 * A star, three planets and a moon, written out by hand (plan conflict 4).
 * Test data, not a generator: every number is a literal, nothing is drawn,
 * and nothing outside Tests/ may include this.
 *
 * The star sits off the universe origin, in chunk (3, -2, 0), so every test
 * that uses the fixture crosses chunk boundaries the way the real galaxy
 * will. The opening view is the one the POC starts from: the home planet
 * 40,000 km dead ahead along +X, the star 1 AU off to +Y.
 */
namespace SkyTestFixtures
{
    inline constexpr int32 StarIndex = 0;
    inline constexpr int32 InnerIndex = 1;
    inline constexpr int32 HomeIndex = 2;
    inline constexpr int32 GiantIndex = 3;
    inline constexpr int32 MoonIndex = 4;

    inline FUniversePosition StarPosition()
    {
        return FUniversePosition(FInt64Vector(3, -2, 0), FVector(1.0e12, 5.0e12, 0.0));
    }

    /** Where the ship opens: the star 1 AU along +Y. */
    inline FUniversePosition Opening()
    {
        return StarPosition() + FVector(0.0, -1.495978707e13, 0.0);
    }

    inline FSkySystem System()
    {
        FSkySystem System;
        System.SystemId = TEXT("Fixture");
        System.EdgeRadius = 2.3651826181452e17;     // 0.25 ly

        FSkyBody& Sun = System.Bodies.AddDefaulted_GetRef();
        Sun.Id = TEXT("Fixture");
        Sun.Kind = ESkyBodyKind::Star;
        Sun.Position = StarPosition();
        Sun.Radius = 6.957e10;
        Sun.Colour = FLinearColor(1.0f, 0.94f, 0.88f);
        Sun.Luminosity = 1.0;
        Sun.TemperatureK = 5772.0;

        // Mercury-like, 0.39 AU.
        FSkyBody& Inner = System.Bodies.AddDefaulted_GetRef();
        Inner.Id = TEXT("Fixture I");
        Inner.Position = StarPosition() + FVector(-5.5e12, -2.0e12, 0.0);
        Inner.Radius = 2.4397e8;
        Inner.Albedo = 0.12;
        Inner.SurfaceSeed = 0xA4093822299F31D0ull;

        // Earth-like, 40,000 km ahead of the opening.
        FSkyBody& Home = System.Bodies.AddDefaulted_GetRef();
        Home.Id = TEXT("Fixture II");
        Home.Position = Opening() + FVector(4.0e9, 0.0, 0.0);
        Home.Radius = 6.3781e8;
        Home.Albedo = 0.3;
        Home.Rim = FLinearColor(0.18f, 0.32f, 0.70f);
        Home.SurfaceSeed = 0x243F6A8885A308D3ull;

        // Jupiter-like, 5.2 AU and a little out of the plane.
        FSkyBody& Giant = System.Bodies.AddDefaulted_GetRef();
        Giant.Id = TEXT("Fixture III");
        Giant.Position = StarPosition() + FVector(3.9e13, 6.7e13, 1.0e12);
        Giant.Radius = 6.9911e9;
        Giant.Albedo = 0.5;
        Giant.Surface = ESkySurface::Banded;
        Giant.SurfaceSeed = 0x13198A2E03707344ull;
        // The belts a 12 h day wears, the median giant's: not a whole number
        // and not Jupiter's 8, so a material that took any default instead
        // cannot pass for this one.
        Giant.BeltPairs = SkyLook::BeltPairs(12.0);

        // Home's moon, at the Moon's distance, on the far side from the star.
        FSkyBody& Moon = System.Bodies.AddDefaulted_GetRef();
        Moon.Id = TEXT("Fixture IIa");
        Moon.Kind = ESkyBodyKind::Moon;
        Moon.Position = Opening() + FVector(4.0e9, -3.844e10, 0.0);
        Moon.Radius = 1.7374e8;
        Moon.Albedo = 0.12;
        Moon.SurfaceSeed = 0x082EFA98EC4E6C89ull;

        return System;
    }
}
