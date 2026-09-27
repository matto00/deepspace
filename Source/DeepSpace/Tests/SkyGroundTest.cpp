#include "Misc/AutomationTest.h"
#include "Sky/ShipSky.h"
#include "Sky/SkySystem.h"
#include "Surface/WorldReliefParams.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyGroundTest,
    "DeepSpace.Sky.WhatCanBeLanded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SkyGroundTestLocal
{
    /** A hand-written system with one world of each kind: data, not a
     *  generator (plan conflict 4), so this pins FromSystem alone. */
    FStarSystem OneOfEach()
    {
        FStarSystem System;
        System.Stub.Name = TEXT("Kessa");
        System.Stub.Seed = 0x1234ABCDull;
        System.Stub.Position = FUniversePosition(FInt64Vector(0, 0, 0), FVector(1.0e12, 0.0, 0.0));
        System.Star.MassSolar = 0.5;
        System.Star.RadiusSolar = 0.5;
        System.Star.LuminositySolar = 0.05;
        System.Star.TemperatureK = 3800.0;
        const EPlanetKind Kinds[] = { EPlanetKind::Barren, EPlanetKind::Terrestrial, EPlanetKind::Ocean, EPlanetKind::Ice, EPlanetKind::GasGiant };
        for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Kinds)); ++Index)
        {
            FPlanet& Planet = System.Planets.AddDefaulted_GetRef();
            Planet.Designation = FString::Printf(TEXT("Kessa %d"), Index + 1);
            Planet.Kind = Kinds[Index];
            Planet.MassEarth = 0.5 + Index;
            Planet.RadiusEarth = Kinds[Index] == EPlanetKind::GasGiant ? 11.0 : 0.8 + 0.1 * Index;
            Planet.SemiMajorAxisAU = 0.05 * (Index + 1);
            // Every world, oceans and giants too, carries a relief: the
            // generator draws none for them, but FromSystem must zero the
            // peak where there is no ground, and a 0 here would hide it.
            Planet.ReliefKm = 2.0 + Index;
            Planet.DayHours = Kinds[Index] == EPlanetKind::GasGiant ? 10.0 : 0.0;
        }
        return System;
    }
}

bool FSkyGroundTest::RunTest(const FString& Parameters)
{
    using namespace SkyGroundTestLocal;
    const FStarSystem System = OneOfEach();
    const FSkySystem Sky = FSkySystem::FromSystem(System, {});
    if (!TestEqual(TEXT("a star and five worlds"), Sky.Bodies.Num(), 6))
    {
        return false;
    }

    const FSkyBody& Star = Sky.Bodies[0];
    TestEqual(TEXT("the star's GM is the Sun's times its mass"), Star.GravParam, UniverseUnits::GMSunCm3PerS2 * 0.5);
    TestTrue(TEXT("a star has no ground"), Star.Ground == EGround::None && Star.Relief.Ground == EGround::None && Star.Relief.PeakCm == 0.0);

    for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
    {
        const FPlanet& Planet = System.Planets[Index];
        const FSkyBody& Body = Sky.Bodies[Index + 1];
        const bool bSolid = Planet.Kind == EPlanetKind::Barren || Planet.Kind == EPlanetKind::Ice || Planet.Kind == EPlanetKind::Terrestrial;
        const FString Name = Planet.Designation;
        TestEqual(Name + TEXT(": GM is the Earth's times its mass"), Body.GravParam, UniverseUnits::GMEarthCm3PerS2 * Planet.MassEarth);
        TestTrue(Name + (bSolid ? TEXT(": solid ground") : TEXT(": no ground")), Body.Ground == (bSolid ? EGround::Solid : EGround::None));
        TestTrue(Name + TEXT(": the relief says the same"), Body.Relief.Ground == Body.Ground);
        TestEqual(Name + TEXT(": the datum is the drawn radius"), Body.Relief.RadiusCm, Body.Radius);
        TestEqual(Name + TEXT(": the peak is the drawn relief, in cm, where there is ground"),
            Body.Relief.PeakCm, bSolid ? Planet.ReliefKm * UniverseUnits::CmPerKm : 0.0);
        TestEqual(Name + TEXT(": the ground keeps the look's craters"), Body.Relief.Cratering, Body.Cratering);
        TestEqual(Name + TEXT(": the seed offset is the face's"), Body.Relief.SeedOffset, SkyLook::SurfaceOffset(Body.SurfaceSeed));

        // Exactly the float M_SkyBody is handed: a multiple of 1/256 in
        // [0, 256), the same number in float and in double.
        const FLinearColor Gpu = ShipSky::SurfaceSeed(Body.SurfaceSeed, Body.BeltPairs);
        TestTrue(Name + TEXT(": the offset is exactly what the GPU gets"),
            Body.Relief.SeedOffset == FVector3d(Gpu.R, Gpu.G, Gpu.B));
        for (const double Part : { Body.Relief.SeedOffset.X, Body.Relief.SeedOffset.Y, Body.Relief.SeedOffset.Z })
        {
            TestTrue(Name + TEXT(": each part a multiple of 1/256 in [0, 256)"),
                Part >= 0.0 && Part < 256.0 && FMath::Frac(Part * 256.0) == 0.0);
        }
    }
    return true;
}

#endif
