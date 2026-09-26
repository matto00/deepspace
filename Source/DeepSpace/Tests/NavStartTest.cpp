#include "Misc/AutomationTest.h"
#include "Ship/NavStart.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNavStartTest,
    "DeepSpace.Ship.NavStart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    // Literal systems, not the generator: this test is about geometry, and
    // must not move when a prior does.

    FPlanet MakePlanet(int32 Index, double AU, double PhaseRad, double RadiusEarth)
    {
        FPlanet Planet;
        Planet.Id.Planet = Index;
        Planet.SemiMajorAxisAU = AU;
        Planet.PhaseRad = PhaseRad;
        Planet.RadiusEarth = RadiusEarth;
        return Planet;
    }

    /** Far from the origin and across chunk boundaries, so anything that
     *  subtracts offsets rather than positions is caught. */
    FUniversePosition FarAway()
    {
        return FUniversePosition(FInt64Vector(812345, -40211, 17), FVector(1.7e13, 3.1e12, 9.0e11));
    }

    FStarSystem SunLike()
    {
        FStarSystem System;
        System.Stub.Name = TEXT("Kessa");
        System.Stub.Class = EStarClass::G;
        System.Stub.LuminositySolar = 1.0;
        System.Stub.Position = FarAway();
        System.Star.Class = EStarClass::G;
        System.Star.LuminositySolar = 1.0;
        System.Planets.Add(MakePlanet(0, 0.4, 0.3, 0.4));
        System.Planets.Add(MakePlanet(1, 1.0, 1.9, 1.0));
        System.Planets.Add(MakePlanet(2, 5.2, 2.6, 11.2));   // the largest
        System.Planets.Add(MakePlanet(3, 9.5, 4.1, 9.4));
        return System;
    }

    FStarSystem DimRedDwarf()
    {
        FStarSystem System;
        System.Stub.Name = TEXT("Orvane");
        System.Stub.Class = EStarClass::M;
        System.Stub.LuminositySolar = 0.01;
        System.Stub.Position = FarAway() + FVector(3.0e18, -1.0e18, 2.0e17);
        System.Star.Class = EStarClass::M;
        System.Star.LuminositySolar = 0.01;
        System.Planets.Add(MakePlanet(0, 0.03, 0.0, 1.1));
        System.Planets.Add(MakePlanet(1, 0.1, 5.0, 0.8));
        return System;
    }

    /** Written out from FStarSystem::PlanetPosition's documented rule. */
    FUniversePosition ExpectedPlanet(const FStarSystem& System, int32 Index)
    {
        const FPlanet& Planet = System.Planets[Index];
        return System.Stub.Position
            + FVector(FMath::Cos(Planet.PhaseRad), FMath::Sin(Planet.PhaseRad), 0.0)
                * (Planet.SemiMajorAxisAU * UniverseUnits::CmPerAU);
    }

    double DegreesBetween(const FVector& A, const FVector& B)
    {
        // atan2 of the cross and dot products, not acos of the dot: acos has
        // no resolution near 0, which is exactly where these tests look.
        const FVector U = A.GetSafeNormal();
        const FVector V = B.GetSafeNormal();
        return FMath::RadiansToDegrees(FMath::Atan2((U ^ V).Size(), U | V));
    }
}

bool FNavStartTest::RunTest(const FString& Parameters)
{
    const double Km = UniverseUnits::CmPerKm;
    const double AU = UniverseUnits::CmPerAU;

    // The opening shot: the largest planet 40,000 km dead ahead, its star
    // abeam to starboard, so the first thing through the glass is half lit.
    {
        const FStarSystem System = SunLike();
        const FNavPlacement Opening = NavStart::OpeningPlacement(System);

        const FVector ToPlanet = Opening.Orientation.UnrotateVector(ExpectedPlanet(System, 2) - Opening.Position);
        TestTrue(TEXT("the largest planet is 40,000 km along +X, to 1 km"),
                 FMath::Abs(ToPlanet.X - 40000.0 * Km) <= 1.0 * Km);
        TestTrue(TEXT("and on the axis, to 1 km"), FVector2D(ToPlanet.Y, ToPlanet.Z).Size() <= 1.0 * Km);

        const FVector ToStar = Opening.Orientation.UnrotateVector(System.Stub.Position - Opening.Position);
        TestTrue(TEXT("the star is within 5 degrees of +Y"), DegreesBetween(ToStar, FVector::RightVector) <= 5.0);
        TestTrue(TEXT("the ship's up is the system's up"),
                 Opening.Orientation.GetUpVector().Equals(FVector::UpVector, 1e-9));
        TestTrue(TEXT("the orientation is a rotation"), FMath::IsNearlyEqual(Opening.Orientation.Size(), 1.0, 1e-9));

        const FVector ToSmaller = Opening.Orientation.UnrotateVector(ExpectedPlanet(System, 3) - Opening.Position);
        TestTrue(TEXT("it is not the outermost planet it frames"), ToSmaller.Size() > 1.0 * AU);
    }

    // Ties go to the inner orbit, and a system of one planet frames it.
    {
        FStarSystem System = SunLike();
        System.Planets[1].RadiusEarth = System.Planets[2].RadiusEarth;
        const FNavPlacement Opening = NavStart::OpeningPlacement(System);
        TestTrue(TEXT("a tie frames the inner planet"),
                 FMath::Abs(Opening.Position.DistanceTo(ExpectedPlanet(System, 1)) - 40000.0 * Km) <= 1.0 * Km);
    }

    // A system with no planets is met the way a jump meets it: the star
    // dead ahead from the standoff.
    {
        FStarSystem Empty = SunLike();
        Empty.Planets.Reset();
        const FNavPlacement Opening = NavStart::OpeningPlacement(Empty);
        const FVector ToStar = Opening.Orientation.UnrotateVector(Empty.Stub.Position - Opening.Position);
        TestTrue(TEXT("with no planets the star is dead ahead"), DegreesBetween(ToStar, FVector::ForwardVector) < 1e-6);
        TestTrue(TEXT("from the standoff"),
                 FMath::IsNearlyEqual(ToStar.Size(), NavStart::ArrivalStandoffAU(Empty) * AU, 1.0 * Km));
    }

    // The standoff (plan conflict 9): constant irradiance, and never inside
    // the planets.
    {
        const FStarSystem Dwarf = DimRedDwarf();
        TestTrue(TEXT("a dim M dwarf is met at 2.4 x sqrt(0.01) = 0.24 AU"),
                 FMath::IsNearlyEqual(NavStart::ArrivalStandoffAU(Dwarf), 0.24, 1e-12));
        TestTrue(TEXT("a Sun-like star with planets to 1 AU is met at 2.4 AU"),
                 FMath::IsNearlyEqual(NavStart::ArrivalStandoffAU(1.0, 1.0), 2.4, 1e-12));
        TestTrue(TEXT("a Sun-like star with planets to 9.5 AU is met outside them, at 1.5 x 9.5"),
                 FMath::IsNearlyEqual(NavStart::ArrivalStandoffAU(SunLike()), 14.25, 1e-12));
        TestTrue(TEXT("ds.Nav.StandoffAU still scales it"),
                 FMath::IsNearlyEqual(NavStart::ArrivalStandoffAU(1.0, 0.0, 4.0), 4.0, 1e-12));
        TestTrue(TEXT("a system of no planets and no light is met at the star, not inside it"),
                 NavStart::ArrivalStandoffAU(0.0, 0.0) >= 0.0);
    }

    // The arrival lies on the line to the star, the standoff short of it, so
    // the star is where the nose was and the jump need not turn the ship.
    {
        const FStarSystem Dwarf = DimRedDwarf();
        const FUniversePosition From = FarAway() + FVector(-2.0e18, 4.0e18, 0.0);
        const FUniversePosition Arrival = NavStart::ArrivalPoint(From, Dwarf);

        TestTrue(TEXT("the arrival is Standoff from the star, to 1 km"),
                 FMath::Abs(Arrival.DistanceTo(Dwarf.Stub.Position) - 0.24 * AU) <= 1.0 * Km);
        TestTrue(TEXT("and on the line from the departure"),
                 DegreesBetween(Dwarf.Stub.Position - From, Dwarf.Stub.Position - Arrival) < 1e-6);
        TestTrue(TEXT("on the near side"), Arrival.DistanceTo(From) < Dwarf.Stub.Position.DistanceTo(From));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
