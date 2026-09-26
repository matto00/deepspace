#include "Misc/AutomationTest.h"
#include "Sky/LocalSystem.h"
#include "Tests/SkyTestFixtures.h"
#include "Universe/GenSeed.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLocalSystemTest,
    "DeepSpace.Sky.LocalSystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLocalSystemTest::RunTest(const FString& Parameters)
{
    // With no world nothing is out there, and every answer says so.
    {
        TestEqual(TEXT("no world, serial 0"), LocalSystem::Serial(nullptr), 0);
        TestFalse(TEXT("no world, not in transit"), LocalSystem::InTransit(nullptr));
        const FSkySystem Nothing = LocalSystem::Current(nullptr);
        TestTrue(TEXT("no world, an empty system"), Nothing.IsEmpty());
        TestEqual(TEXT("with no neighbours"), Nothing.Neighbours.Num(), 0);
        TestEqual(TEXT("no world, the starfield seed comes from the fallback"),
            LocalSystem::StarfieldSeed(nullptr, 20260922), GenSeed::Derive(20260922, GenSeed::Label("sky.starfield")));
    }

    const FSkySystem Fixture = SkyTestFixtures::System();
    const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
    const FSkyBody& Moon = Fixture.Bodies[SkyTestFixtures::MoonIndex];
    const FSkyBody& Star = Fixture.Bodies[SkyTestFixtures::StarIndex];

    // The drive's room: the distance to the nearest surface.
    {
        FSkySystem One;
        One.Bodies.Add(Home);
        const FUniversePosition Where = Home.Position + FVector(0.0, 0.0, Home.Radius + 1.0e9);
        TestTrue(TEXT("exact outside one sphere"), FMath::IsNearlyEqual(LocalSystem::NearestSurfaceDistance(One, Where), 1.0e9, 1.0e-3));

        TestTrue(TEXT("from the opening the nearest surface is the home planet's, 40,000 km less its radius"),
            FMath::IsNearlyEqual(LocalSystem::NearestSurfaceDistance(Fixture, SkyTestFixtures::Opening()), 4.0e9 - Home.Radius, 1.0));

        // Between the planet and its moon, nearer the moon.
        const FUniversePosition NearMoon = Moon.Position + FVector(0.0, 1.0e9 + Moon.Radius, 0.0);
        TestTrue(TEXT("picks the nearest of several"),
            FMath::IsNearlyEqual(LocalSystem::NearestSurfaceDistance(Fixture, NearMoon), 1.0e9, 1.0));

        TestEqual(TEXT("never negative inside a body"), LocalSystem::NearestSurfaceDistance(Fixture, Home.Position), 0.0);
        TestEqual(TEXT("an empty system gives the drive no room"),
            LocalSystem::NearestSurfaceDistance(FSkySystem(), SkyTestFixtures::Opening()), 0.0);
    }

    // The system's edge is a surface (plan conflict 10), so the drive slows
    // into it and never carries the ship out.
    {
        const FVector Out = FVector(1.0, 1.0, 0.0).GetSafeNormal();
        const FUniversePosition NearEdge = Star.Position + Out * (Fixture.EdgeRadius - 1.0e12);
        TestTrue(TEXT("far out, the nearest surface is the edge"),
            FMath::IsNearlyEqual(LocalSystem::NearestSurfaceDistance(Fixture, NearEdge), 1.0e12, 1.0e3));
        // Within the double's resolution at a quarter of a light year.
        TestTrue(TEXT("at the edge there is no room"),
            LocalSystem::NearestSurfaceDistance(Fixture, Star.Position + Out * Fixture.EdgeRadius) < 1.0e3);
        TestEqual(TEXT("past it, still none"),
            LocalSystem::NearestSurfaceDistance(Fixture, Star.Position + Out * (Fixture.EdgeRadius * 1.5)), 0.0);

        FSkySystem Edgeless = Fixture;
        Edgeless.EdgeRadius = 0.0;
        TestTrue(TEXT("with no edge, only bodies count"),
            LocalSystem::NearestSurfaceDistance(Edgeless, NearEdge) > Fixture.EdgeRadius * 0.5);
    }

    // The adapter from procgen's data: a hand-written system, not a
    // generated one, so this pins FromSystem and nothing of the generator.
    {
        FStarSystem System;
        System.Stub.Id.Sector = FInt64Vector(1, 2, 3);
        System.Stub.Id.Slot = 4;
        System.Stub.Name = TEXT("Kessa");
        System.Stub.Position = FUniversePosition(FInt64Vector(1, 2, 3), FVector(1.0e12, 2.0e12, 3.0e12));
        System.Star.Class = EStarClass::M;
        System.Star.RadiusSolar = 0.28;
        System.Star.LuminositySolar = 0.0057;
        System.Star.TemperatureK = 3200.0;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            FPlanet& Planet = System.Planets.AddDefaulted_GetRef();
            Planet.Designation = Index == 0 ? TEXT("Kessa I") : TEXT("Kessa II");
            Planet.Kind = Index == 0 ? EPlanetKind::Barren : EPlanetKind::Ocean;
            Planet.RadiusEarth = Index == 0 ? 0.6 : 1.1;
            Planet.SemiMajorAxisAU = Index == 0 ? 0.025 : 0.04;
            Planet.PhaseRad = Index == 0 ? 0.5 : 2.0;
        }

        FStarSystemStub Self = System.Stub;
        FStarSystemStub Near;
        Near.Id.Slot = 9;
        Near.Name = TEXT("Orin");
        Near.Position = System.Stub.Position + FVector(0.0, 3.0 * UniverseUnits::CmPerLightYear, 0.0);
        Near.LuminositySolar = 0.4;
        Near.TemperatureK = 4800.0;
        const TArray<FStarSystemStub> Stubs = { Self, Near };

        const FSkySystem Sky = FSkySystem::FromSystem(System, Stubs);
        TestEqual(TEXT("named for the system"), Sky.SystemId, FName(TEXT("Kessa")));
        TestEqual(TEXT("the edge is procgen's in-system radius"), Sky.EdgeRadius, FStarSystem::InSystemRadiusCm);
        if (TestEqual(TEXT("a star and two planets"), Sky.Bodies.Num(), 3))
        {
            TestTrue(TEXT("the star first"), Sky.Bodies[0].Kind == ESkyBodyKind::Star);
            TestEqual(TEXT("at the system's position"), Sky.Bodies[0].Position, System.Stub.Position);
            TestEqual(TEXT("its radius in centimetres"), Sky.Bodies[0].Radius, 0.28 * UniverseUnits::CmPerSolarRadius);
            TestEqual(TEXT("its luminosity"), Sky.Bodies[0].Luminosity, 0.0057);
            TestTrue(TEXT("a red dwarf is drawn red"), Sky.Bodies[0].Colour.R > Sky.Bodies[0].Colour.B);
            for (int32 Index = 0; Index < 2; ++Index)
            {
                const FSkyBody& Body = Sky.Bodies[Index + 1];
                TestTrue(TEXT("then planets"), Body.Kind == ESkyBodyKind::Planet);
                TestEqual(TEXT("named by designation"), Body.Id, FName(*System.Planets[Index].Designation));
                TestEqual(TEXT("where procgen puts it"), Body.Position, System.PlanetPosition(Index));
                TestEqual(TEXT("its radius in centimetres"), Body.Radius, System.Planets[Index].RadiusEarth * UniverseUnits::CmPerEarthRadius);
            }
            TestTrue(TEXT("bare rock is darker than an ocean world"), Sky.Bodies[1].Albedo < Sky.Bodies[2].Albedo);
            TestTrue(TEXT("an ocean world has a sky at its limb"), Sky.Bodies[2].Rim.B > 0.0f);
        }
        if (TestEqual(TEXT("one neighbour: the system itself is left out"), Sky.Neighbours.Num(), 1))
        {
            TestEqual(TEXT("the neighbour's name"), Sky.Neighbours[0].SystemId, FName(TEXT("Orin")));
            TestTrue(TEXT("its direction from this star"), Sky.Neighbours[0].Direction.Equals(FVector(0.0, 1.0, 0.0), 1.0e-12));
            TestTrue(TEXT("its distance"), FMath::IsNearlyEqual(Sky.Neighbours[0].Distance, 3.0 * UniverseUnits::CmPerLightYear, 1.0));
            TestEqual(TEXT("its luminosity"), Sky.Neighbours[0].Luminosity, 0.4);
        }
    }
    return true;
}

#endif
