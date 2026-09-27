#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Tests/SkyTestFixtures.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    /** A 4K pixel at 90 degrees: what "under a pixel" means in the spec. */
    constexpr double Pixel4K = 2.0 / 3840.0;

    constexpr double AU = 1.495978707e13;

    /** Direction the ship approaches every body from in the sweeps: off every
     *  axis, so no component is special. */
    FVector ApproachDirection()
    {
        return FVector(-0.8, 0.45, 0.4).GetSafeNormal();
    }

    double Floor(const FSkyBody& Body, const FSkyViewParams& Params)
    {
        return FMath::Max(Params.MinRenderedAltitude, Params.MinRenderedAltitudeOfRadius * Body.Radius);
    }

    /** Ship positions from Body's rendered-altitude floor out to 30 AU,
     *  log-spaced: every decade of the approach is sampled alike. */
    TArray<FUniversePosition> Sweep(const FSkyBody& Body, const FSkyViewParams& Params, int32 Steps = 48)
    {
        TArray<FUniversePosition> Ships;
        const double Near = Floor(Body, Params);
        const double Far = 30.0 * AU;
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            const double T = static_cast<double>(Step) / (Steps - 1);
            const double Altitude = Near * FMath::Pow(Far / Near, T);
            Ships.Add(Body.Position + ApproachDirection() * (Body.Radius + Altitude));
        }
        return Ships;
    }

    double ProxyNear(const FSkyBodyView& View) { return View.ProxyLocation.Size() - View.ProxyRadius; }
    double ProxyFar(const FSkyBodyView& View) { return View.ProxyLocation.Size() + View.ProxyRadius; }

    /** The angle between two vectors, accurate for tiny angles. */
    double AngleBetween(const FVector& A, const FVector& B)
    {
        return FMath::Atan2(FVector::CrossProduct(A, B).Size(), FVector::DotProduct(A, B));
    }

    /** Distance along unit U from the origin to the near side of a sphere,
     *  or a negative number if the ray misses. */
    double RayHit(const FVector& U, const FVector& Centre, double Radius)
    {
        const double B = FVector::DotProduct(U, Centre);
        const double C = Centre.SizeSquared() - Radius * Radius;
        const double Discriminant = B * B - C;
        if (Discriminant < 0.0)
        {
            return -1.0;
        }
        return B - FMath::Sqrt(Discriminant);
    }

    double RelativeError(double Actual, double Expected)
    {
        return FMath::Abs(Actual - Expected) / FMath::Max(FMath::Abs(Expected), UE_DOUBLE_SMALL_NUMBER);
    }

    FSkyBody MakeStar(const FUniversePosition& Where)
    {
        FSkyBody Star;
        Star.Id = TEXT("Star");
        Star.Kind = ESkyBodyKind::Star;
        Star.Position = Where;
        Star.Radius = 6.957e10;
        Star.TemperatureK = 5772.0;
        return Star;
    }

    FSkyBody MakeBody(const TCHAR* Id, const FUniversePosition& Where, double Radius, ESkyBodyKind Kind = ESkyBodyKind::Planet)
    {
        FSkyBody Body;
        Body.Id = Id;
        Body.Kind = Kind;
        Body.Position = Where;
        Body.Radius = Radius;
        return Body;
    }

    /**
     * Every pair's proxy depth intervals are disjoint, and wherever two true
     * discs overlap on screen, the body a ray meets first is drawn wholly in
     * front of the other and earlier in DepthOrder.
     */
    void CheckOcclusion(FAutomationTestBase& Test, const TCHAR* Case, const FSkySystem& System,
                        const FUniversePosition& Ship, const FSkyViewParams& Params)
    {
        const FSkyFrame Frame = SkyProjection::Project(System, Ship, Params);
        const int32 Count = Frame.Bodies.Num();

        TArray<int32> Rank;
        Rank.SetNum(Count);
        for (int32 Order = 0; Order < Count; ++Order)
        {
            Rank[Frame.DepthOrder[Order]] = Order;
        }

        for (int32 A = 0; A < Count; ++A)
        {
            for (int32 B = A + 1; B < Count; ++B)
            {
                const FSkyBodyView& ViewA = Frame.Bodies[A];
                const FSkyBodyView& ViewB = Frame.Bodies[B];
                const bool bDisjoint = ProxyFar(ViewA) < ProxyNear(ViewB) || ProxyFar(ViewB) < ProxyNear(ViewA);
                Test.TestTrue(FString::Printf(TEXT("%s: %s and %s have disjoint proxy depths"), Case,
                    *System.Bodies[A].Id.ToString(), *System.Bodies[B].Id.ToString()), bDisjoint);

                const double Separation = AngleBetween(ViewA.Direction, ViewB.Direction);
                if (Separation >= ViewA.AngularRadius + ViewB.AngularRadius)
                {
                    continue;
                }

                // The overlap region crosses the arc between the two centres,
                // so rays along that arc find it.
                const FVector CentreA = System.Bodies[A].Position - Ship;
                const FVector CentreB = System.Bodies[B].Position - Ship;
                int32 Checked = 0;
                for (int32 Step = 0; Step <= 64; ++Step)
                {
                    const double T = Step / 64.0;
                    const FVector U = ((1.0 - T) * ViewA.Direction + T * ViewB.Direction).GetSafeNormal();
                    const double HitA = RayHit(U, CentreA, System.Bodies[A].Radius);
                    const double HitB = RayHit(U, CentreB, System.Bodies[B].Radius);
                    if (HitA < 0.0 || HitB < 0.0)
                    {
                        continue;
                    }
                    ++Checked;
                    const int32 First = HitA < HitB ? A : B;
                    const int32 Second = First == A ? B : A;
                    if (ProxyFar(Frame.Bodies[First]) >= ProxyNear(Frame.Bodies[Second]) || Rank[First] >= Rank[Second])
                    {
                        Test.AddError(FString::Printf(TEXT("%s: a ray meets %s first, but it is not drawn in front of %s"),
                            Case, *System.Bodies[First].Id.ToString(), *System.Bodies[Second].Id.ToString()));
                        break;
                    }
                }
                Test.AddInfo(FString::Printf(TEXT("%s: %s/%s overlap on screen, %d rays checked"), Case,
                    *System.Bodies[A].Id.ToString(), *System.Bodies[B].Id.ToString(), Checked));
            }
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyProjectionTest,
    "DeepSpace.Sky.Projection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkyProjectionTest::RunTest(const FString& Parameters)
{
    const FSkyViewParams Params;
    const FSkySystem Fixture = SkyTestFixtures::System();
    const double MinDrawnRadius = 0.5 * Params.MinPointPixels * Params.PixelAngle;

    // Angular size is exact from the ship's origin -- or exactly a point's
    // two pixels -- for every body, from its floor to 30 AU. This pins the
    // homothety.
    {
        double Worst = 0.0;
        for (const FSkyBody& Approached : Fixture.Bodies)
        {
            for (const FUniversePosition& Ship : Sweep(Approached, Params))
            {
                const FSkyFrame Frame = SkyProjection::Project(Fixture, Ship, Params);
                for (const FSkyBodyView& View : Frame.Bodies)
                {
                    const double Drawn = FMath::Asin(View.ProxyRadius / View.ProxyLocation.Size());
                    const double Expected = FMath::Max(View.AngularRadius, MinDrawnRadius);
                    Worst = FMath::Max(Worst, RelativeError(Drawn, Expected));
                    Worst = FMath::Max(Worst, AngleBetween(View.ProxyLocation, View.Direction));
                }
            }
        }
        AddInfo(FString::Printf(TEXT("worst angular-size or direction error from the origin: %.3g"), Worst));
        TestTrue(TEXT("every proxy subtends its true angular size (or a point's) in its true direction, to 1e-9"), Worst < 1.0e-9);
    }

    // It is right from where the player stands. With the eye 20 m off the
    // origin along each axis, every body's direction is within a 4K pixel and
    // its angular radius within 0.1% of the truth wherever its near surface
    // is beyond NearProxy, and within 4 px down to the floor. The first
    // draft's 2 km near edge fails this by a factor of 25.
    {
        const TArray<FVector> Eyes = {
            FVector(2000.0, 0.0, 0.0), FVector(-2000.0, 0.0, 0.0),
            FVector(0.0, 2000.0, 0.0), FVector(0.0, -2000.0, 0.0),
            FVector(0.0, 0.0, 2000.0), FVector(0.0, 0.0, -2000.0) };
        double WorstFar = 0.0;
        double WorstNear = 0.0;
        double WorstSize = 0.0;
        for (const FSkyBody& Approached : Fixture.Bodies)
        {
            for (const FUniversePosition& Ship : Sweep(Approached, Params))
            {
                const FSkyFrame Frame = SkyProjection::Project(Fixture, Ship, Params);
                for (int32 Index = 0; Index < Frame.Bodies.Num(); ++Index)
                {
                    const FSkyBody& Body = Fixture.Bodies[Index];
                    const FSkyBodyView& View = Frame.Bodies[Index];
                    const bool bBeyondNear = View.Distance - Body.Radius > Params.NearProxy;
                    for (const FVector& Eye : Eyes)
                    {
                        const FVector TrueRelative = (Body.Position - Ship) - Eye;
                        const FVector ProxyRelative = View.ProxyLocation - Eye;
                        const double Error = AngleBetween(TrueRelative, ProxyRelative);
                        (bBeyondNear ? WorstFar : WorstNear) = FMath::Max(bBeyondNear ? WorstFar : WorstNear, Error);

                        if (bBeyondNear && View.AngularRadius >= MinDrawnRadius)
                        {
                            const double TrueSize = FMath::Asin(Body.Radius / TrueRelative.Size());
                            const double ProxySize = FMath::Asin(View.ProxyRadius / ProxyRelative.Size());
                            WorstSize = FMath::Max(WorstSize, RelativeError(ProxySize, TrueSize));
                        }
                    }
                }
            }
        }
        AddInfo(FString::Printf(TEXT("eye 20 m off: worst direction %.3g px beyond NearProxy, %.3g px nearer; worst size %.3g"),
            WorstFar / Pixel4K, WorstNear / Pixel4K, WorstSize));
        TestTrue(TEXT("beyond NearProxy, every body is within a 4K pixel of true from the eye"), WorstFar < Pixel4K);
        TestTrue(TEXT("down to the floor, within 4 pixels"), WorstNear < 4.0 * Pixel4K);
        TestTrue(TEXT("beyond NearProxy, angular size from the eye is true to 0.1%"), WorstSize < 1.0e-3);
    }

    // Direction is exact with the ship and the body in different chunks.
    {
        const double Chunk = FUniversePosition::ChunkSize;
        const FUniversePosition Ship(FInt64Vector(7, 0, 0), FVector(Chunk - 100.0, 5.0, 0.0));
        FSkySystem System;
        System.Bodies.Add(MakeStar(FUniversePosition(FInt64Vector(7, 3, 0), FVector::ZeroVector)));
        System.Bodies.Add(MakeBody(TEXT("Across"), FUniversePosition(FInt64Vector(8, 0, 0), FVector(900.0, 5.0, 0.0)), 10.0));
        const FSkyFrame Frame = SkyProjection::Project(System, Ship, Params);
        TestEqual(TEXT("a body across a chunk boundary is dead ahead"), Frame.Bodies[1].Direction, FVector(1.0, 0.0, 0.0));
        TestEqual(TEXT("at its true distance"), Frame.Bodies[1].Distance, 1000.0);
    }

    // Occlusion survives every case that broke a draft.
    {
        const FUniversePosition Origin;
        FSkySystem Behind;
        Behind.Bodies.Add(MakeStar(Origin + FVector(0.0, 1.5e13, 0.0)));
        Behind.Bodies.Add(MakeBody(TEXT("Planet"), Origin + FVector(1.0e10, 0.0, 0.0), 6.4e8));
        Behind.Bodies.Add(MakeBody(TEXT("Moon"), Origin + FVector(1.3e10, 1.0e8, 0.0), 1.7e8, ESkyBodyKind::Moon));
        CheckOcclusion(*this, TEXT("a moon behind its planet"), Behind, Origin, Params);

        FSkySystem Transit;
        Transit.Bodies.Add(MakeStar(Origin + FVector(1.5e13, 0.0, 0.0)));
        Transit.Bodies.Add(MakeBody(TEXT("Planet"), Origin + FVector(5.0e12, 1.0e8, 0.0), 6.4e8));
        CheckOcclusion(*this, TEXT("a planet transiting its star"), Transit, Origin, Params);

        // 150 km over the home planet's night side: the star, and everything
        // else round it, is behind the planet.
        const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
        const FVector AwayFromStar = (Home.Position - Fixture.Bodies[SkyTestFixtures::StarIndex].Position).GetSafeNormal();
        CheckOcclusion(*this, TEXT("150 km over a planet, the system behind it"), Fixture,
            Home.Position + AwayFromStar * (Home.Radius + 1.5e7), Params);

        // The review's counterexample: a planet at d = 100, R = 50 and a small
        // moon at d = 60, 28 degrees off the planet's centre -- inside its
        // disc, outside the planet, and truly in front of it.
        FSkySystem Counter;
        Counter.Bodies.Add(MakeStar(Origin + FVector(-1.5e13, 0.0, 0.0)));
        Counter.Bodies.Add(MakeBody(TEXT("Planet"), Origin + FVector(1.0e9, 0.0, 0.0), 5.0e8));
        const double Off = FMath::DegreesToRadians(28.0);
        Counter.Bodies.Add(MakeBody(TEXT("Moon"), Origin + FVector(6.0e8 * FMath::Cos(Off), 6.0e8 * FMath::Sin(Off), 0.0), 1.0e7, ESkyBodyKind::Moon));
        TestTrue(TEXT("the counterexample bites: by d - R the moon is behind the planet"),
            6.0e8 - 1.0e7 > 1.0e9 - 5.0e8);
        CheckOcclusion(*this, TEXT("the review's counterexample"), Counter, Origin, Params);
        const FSkyFrame Frame = SkyProjection::Project(Counter, Origin, Params);
        TestTrue(TEXT("the moon is drawn in front of the planet"),
            Frame.DepthOrder.IndexOfByKey(2) < Frame.DepthOrder.IndexOfByKey(1));
    }

    // The band holds: every proxy in [NearProxy, FarProxy], for the fixture
    // approached to every body's floor. The test that fails first if someone
    // gives a giant twenty moons.
    {
        double Nearest = TNumericLimits<double>::Max();
        double Farthest = 0.0;
        for (const FSkyBody& Approached : Fixture.Bodies)
        {
            for (const FUniversePosition& Ship : Sweep(Approached, Params))
            {
                const FSkyFrame Frame = SkyProjection::Project(Fixture, Ship, Params);
                for (const FSkyBodyView& View : Frame.Bodies)
                {
                    Nearest = FMath::Min(Nearest, ProxyNear(View));
                    Farthest = FMath::Max(Farthest, ProxyFar(View));
                }
            }
        }
        AddInfo(FString::Printf(TEXT("band used: %.4g to %.4g cm (%.0fx of %.0fx)"),
            Nearest, Farthest, Farthest / Nearest, Params.FarProxy / Params.NearProxy));
        TestTrue(TEXT("no proxy nearer than NearProxy"), Nearest >= Params.NearProxy * (1.0 - 1.0e-9));
        TestTrue(TEXT("no proxy farther than FarProxy"), Farthest <= Params.FarProxy);
    }

    // The resolve. One Earth at 1 AU from its star, the ship closing on it
    // along the line it is seen at half phase from.
    {
        const FUniversePosition Origin;
        FSkySystem System;
        System.Bodies.Add(MakeStar(Origin + FVector(0.0, AU, 0.0)));
        FSkyBody Earth = MakeBody(TEXT("Earth"), Origin, 6.3781e8);
        Earth.Albedo = 0.3;
        System.Bodies.Add(Earth);

        const auto ShipAtPixels = [&](double Pixels)
        {
            const double Radius = 0.5 * Pixels * Params.PixelAngle;
            return Origin + FVector(-Earth.Radius / FMath::Sin(Radius), 0.0, 0.0);
        };
        const auto Flux = [](const FSkyBodyView& View)
        {
            return View.Brightness * SkyProjection::ConeSolidAngle(View.DrawnAngularRadius)
                * (View.PointBlend + (1.0 - View.PointBlend) * View.Phase);
        };

        double LastBlend = 1.0;
        bool bMonotone = true;
        bool bPointBelow = true;
        bool bDiscAbove = true;
        bool bBoostOnlyWhenInflated = true;
        for (double Pixels = 0.05; Pixels < 12.0; Pixels *= 1.05)
        {
            const FSkyBodyView View = SkyProjection::Project(System, ShipAtPixels(Pixels), Params).Bodies[1];
            bMonotone &= View.PointBlend <= LastBlend;
            LastBlend = View.PointBlend;
            if (Pixels <= Params.MinPointPixels)
            {
                bPointBelow &= View.PointBlend == 1.0;
                bBoostOnlyWhenInflated &= Pixels == Params.MinPointPixels ? View.PointBoost == 1.0 : View.PointBoost > 1.0;
            }
            else
            {
                bBoostOnlyWhenInflated &= View.PointBoost == 1.0;
            }
            if (Pixels >= Params.MinPointPixels + Params.ResolveBandPixels)
            {
                bDiscAbove &= View.PointBlend == 0.0;
            }
        }
        TestTrue(TEXT("PointBlend is 1 below 2 px"), bPointBelow);
        TestTrue(TEXT("PointBlend is 0 above 4 px"), bDiscAbove);
        TestTrue(TEXT("PointBlend never rises as the body grows"), bMonotone);
        TestTrue(TEXT("PointBoost is above 1 only while the body is drawn larger than it is"), bBoostOnlyWhenInflated);

        FSkyViewParams Honest = Params;
        Honest.FluxGamma = 1.0;
        bool bNoBoost = true;
        for (double Pixels = 0.05; Pixels < 12.0; Pixels *= 1.2)
        {
            bNoBoost &= SkyProjection::Project(System, ShipAtPixels(Pixels), Honest).Bodies[1].PointBoost == 1.0;
        }
        TestTrue(TEXT("with FluxGamma 1 nothing is boosted"), bNoBoost);

        const double Below = Flux(SkyProjection::Project(System, ShipAtPixels(Params.MinPointPixels * (1.0 - 1.0e-12)), Params).Bodies[1]);
        const double Above = Flux(SkyProjection::Project(System, ShipAtPixels(Params.MinPointPixels * (1.0 + 1.0e-12)), Params).Bodies[1]);
        AddInfo(FString::Printf(TEXT("flux either side of 2 px: %.12g, %.12g"), Below, Above));
        TestTrue(TEXT("the displayed flux is continuous at 2 px, to 1e-9"), RelativeError(Below, Above) < 1.0e-9);

        // Through the band the flux is exactly the honest one: the point and
        // disc terms are the same light.
        bool bHonestInBand = true;
        for (double Pixels = 2.0; Pixels <= 4.0; Pixels += 0.125)
        {
            const FSkyBodyView View = SkyProjection::Project(System, ShipAtPixels(Pixels), Params).Bodies[1];
            const double Expected = View.SurfaceBrightness * View.Phase * SkyProjection::ConeSolidAngle(View.AngularRadius);
            bHonestInBand &= RelativeError(Flux(View), Expected) < 1.0e-9;
        }
        TestTrue(TEXT("through the resolve the flux is the honest one"), bHonestInBand);
    }

    // Surfaces are honest: once resolved, a planet and a star look as bright
    // per pixel at any distance. The first draft's approach dimmed 300x.
    {
        const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
        const FSkyBody& Star = Fixture.Bodies[SkyTestFixtures::StarIndex];
        for (const int32 Index : { SkyTestFixtures::HomeIndex, SkyTestFixtures::StarIndex })
        {
            const FSkyBody& Body = Fixture.Bodies[Index];
            // Along one ray from the body, so the phase is fixed too.
            const FVector Out = (Index == SkyTestFixtures::HomeIndex ? (Home.Position - Star.Position) : ApproachDirection()).GetSafeNormal()
                .RotateAngleAxis(60.0, FVector::UpVector);
            double First = -1.0;
            double FirstSurface = -1.0;
            double Worst = 0.0;
            int32 Resolved = 0;
            for (double Altitude = Floor(Body, Params); Altitude < 5.0 * AU; Altitude *= 1.7)
            {
                const FSkyBodyView View = SkyProjection::Project(Fixture, Body.Position + Out * (Body.Radius + Altitude), Params).Bodies[Index];
                if (View.PointBlend > 0.0)
                {
                    continue;
                }
                ++Resolved;
                if (First < 0.0)
                {
                    First = View.Brightness;
                    FirstSurface = View.SurfaceBrightness;
                }
                Worst = FMath::Max(Worst, RelativeError(View.Brightness, First));
                Worst = FMath::Max(Worst, RelativeError(View.SurfaceBrightness, FirstSurface));
                Worst = FMath::Max(Worst, RelativeError(View.Brightness, View.SurfaceBrightness));
            }
            TestTrue(FString::Printf(TEXT("%s is resolved over many distances"), *Body.Id.ToString()), Resolved > 10);
            TestTrue(FString::Printf(TEXT("%s: resolved brightness is independent of distance, to 1e-12"), *Body.Id.ToString()), Worst < 1.0e-12);
        }
    }

    // Compression and the phase law.
    {
        TestTrue(TEXT("a 900x ratio becomes 30x at gamma 0.5"), RelativeError(SkyProjection::Compress(900.0, 0.5), 30.0) < 1.0e-12);
        bool bMonotone = true;
        for (double X = 1.0e-4; X < 1.0e4; X *= 1.3)
        {
            bMonotone &= SkyProjection::Compress(X * 1.3, 0.5) > SkyProjection::Compress(X, 0.5);
        }
        TestTrue(TEXT("Compress is monotone"), bMonotone);
        TestEqual(TEXT("nothing compresses to nothing"), SkyProjection::Compress(0.0, 0.5), 0.0);

        TestTrue(TEXT("full phase is 1"), RelativeError(SkyProjection::LambertPhase(0.0), 1.0) < 1.0e-12);
        TestTrue(TEXT("half phase is 1/pi"), RelativeError(SkyProjection::LambertPhase(0.5 * UE_DOUBLE_PI), 1.0 / UE_DOUBLE_PI) < 1.0e-12);
        TestTrue(TEXT("new phase is dark"), FMath::Abs(SkyProjection::LambertPhase(UE_DOUBLE_PI)) < 1.0e-12);

        // M_SkyBody's shaded term, LambertDiscGain * saturate(N.L), averaged
        // over the visible disc, is LambertPhase: integrate it on a grid.
        double Worst = 0.0;
        for (double Alpha = 0.0; Alpha <= UE_DOUBLE_PI; Alpha += UE_DOUBLE_PI / 8.0)
        {
            const FVector Light(FMath::Sin(Alpha), 0.0, FMath::Cos(Alpha));
            double Sum = 0.0;
            int32 Samples = 0;
            constexpr int32 Grid = 400;
            for (int32 I = 0; I < Grid; ++I)
            {
                for (int32 J = 0; J < Grid; ++J)
                {
                    const double X = -1.0 + (I + 0.5) * 2.0 / Grid;
                    const double Y = -1.0 + (J + 0.5) * 2.0 / Grid;
                    const double R2 = X * X + Y * Y;
                    if (R2 >= 1.0)
                    {
                        continue;
                    }
                    const FVector Normal(X, Y, FMath::Sqrt(1.0 - R2));
                    Sum += SkyMaterial::LambertDiscGain * FMath::Max(FVector::DotProduct(Normal, Light), 0.0);
                    ++Samples;
                }
            }
            Worst = FMath::Max(Worst, FMath::Abs(Sum / Samples - SkyProjection::LambertPhase(Alpha)));
        }
        AddInfo(FString::Printf(TEXT("disc-averaged shading against LambertPhase: worst %.3g"), Worst));
        TestTrue(TEXT("the material's shaded term averages to LambertPhase over the disc"), Worst < 2.0e-3);
    }

    // The sun: its direction and irradiance from the opening, and no star
    // means no sunlight rather than a sun from nowhere.
    {
        const FSkyFrame Frame = SkyProjection::Project(Fixture, SkyTestFixtures::Opening(), Params);
        TestTrue(TEXT("from the opening the sun is off to +Y"), AngleBetween(Frame.SunDirection, FVector(0.0, 1.0, 0.0)) < 1.0e-9);
        TestTrue(TEXT("a Sun at 1 AU gives irradiance 1"), RelativeError(Frame.SunIrradiance, 1.0) < 1.0e-9);
        TestEqual(TEXT("from the opening nothing is in front of the sun"), Frame.SunVisibleFraction, 1.0);

        FSkySystem Starless;
        Starless.Bodies.Add(MakeBody(TEXT("Rogue"), FUniversePosition() + FVector(1.0e10, 0.0, 0.0), 6.4e8));
        const FSkyFrame Dark = SkyProjection::Project(Starless, FUniversePosition(), Params);
        TestEqual(TEXT("no star, no sunlight"), Dark.SunIrradiance, 0.0);
        TestEqual(TEXT("and an unlit world"), Dark.Bodies[0].Brightness, 0.0);

        const FSkyFrame Empty = SkyProjection::Project(FSkySystem(), FUniversePosition(), Params);
        TestEqual(TEXT("an empty system projects to nothing"), Empty.Bodies.Num(), 0);
    }

    // The rendered floor (flight-feel decision 6): 10 km for small bodies,
    // 1.6e-3 R for large, and Project draws a body from exactly R + that
    // floor whenever the ship is below it -- the one function the flight
    // law's floor also asks.
    {
        TestEqual(TEXT("a moon's floor is 10 km"), SkyProjection::RenderedFloor(1.7374e8, Params), 1.0e6);
        TestEqual(TEXT("so is anything smaller"), SkyProjection::RenderedFloor(1.0e7, Params), 1.0e6);
        TestTrue(TEXT("an Earth's is 1.6e-3 R, 10.2 km"),
                 RelativeError(SkyProjection::RenderedFloor(6.3781e8, Params), 1.6e-3 * 6.3781e8) < 1e-12);
        TestTrue(TEXT("a Jupiter's is 1.6e-3 R, 112 km"),
                 RelativeError(SkyProjection::RenderedFloor(6.9911e9, Params), 1.6e-3 * 6.9911e9) < 1e-12);
        TestTrue(TEXT("the two agree at 6,250 km"), RelativeError(SkyProjection::RenderedFloor(6.25e8, Params), 1.0e6) < 1e-12);
        FSkyViewParams Raised = Params;
        Raised.MinRenderedAltitude = 5.0e6;
        TestEqual(TEXT("it follows the params it is given"), SkyProjection::RenderedFloor(6.3781e8, Raised), 5.0e6);

        for (const int32 Index : { SkyTestFixtures::HomeIndex, SkyTestFixtures::GiantIndex, SkyTestFixtures::MoonIndex })
        {
            const FSkyBody& Body = Fixture.Bodies[Index];
            const double RenderedFloor = SkyProjection::RenderedFloor(Body.Radius, Params);
            const double Drawn = Body.Radius + RenderedFloor;
            for (const double Altitude : { 0.5 * RenderedFloor, 1.0e3, -0.5 * Body.Radius })
            {
                const FSkyBodyView View = SkyProjection::Project(Fixture, Body.Position + ApproachDirection() * (Body.Radius + Altitude), Params).Bodies[Index];
                TestTrue(FString::Printf(TEXT("%s, %.3g cm below its floor: drawn from exactly R + the floor"), *Body.Id.ToString(), RenderedFloor - Altitude),
                         RelativeError(View.ProxyLocation.Size() / View.ProxyRadius, Drawn / Body.Radius) < 1e-12
                         && RelativeError(View.DrawnAngularRadius, FMath::Asin(Body.Radius / Drawn)) < 1e-12);
            }
            const FSkyBodyView Above = SkyProjection::Project(Fixture, Body.Position + ApproachDirection() * (Drawn * 1.5), Params).Bodies[Index];
            TestTrue(FString::Printf(TEXT("%s above its floor is drawn from its true distance"), *Body.Id.ToString()),
                     RelativeError(Above.ProxyLocation.Size() / Above.ProxyRadius, Drawn * 1.5 / Body.Radius) < 1e-9);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyEclipseTest,
    "DeepSpace.Sky.Eclipse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Parking behind a planet darkens the ship (sky decision 5), and nothing
 * else does: the disc arithmetic, then the sun seen from inside a planet's
 * shadow, at its edge, beside it, and with a planet lined up behind the
 * star rather than in front of it.
 */
bool FSkyEclipseTest::RunTest(const FString& Parameters)
{
    using SkyProjection::DiscOverlapFraction;

    // Discs, in the plane.
    {
        TestEqual(TEXT("apart, nothing is covered"), DiscOverlapFraction(3.0, 1.0, 1.0), 0.0);
        TestEqual(TEXT("touching, nothing is covered"), DiscOverlapFraction(2.0, 1.0, 1.0), 0.0);
        TestEqual(TEXT("a larger disc centred on it covers it"), DiscOverlapFraction(0.0, 1.0, 2.0), 1.0);
        TestEqual(TEXT("and still does off-centre, while it lies inside"), DiscOverlapFraction(0.9, 1.0, 2.0), 1.0);
        TestTrue(TEXT("a disc half the radius inside it covers a quarter"),
            FMath::IsNearlyEqual(DiscOverlapFraction(0.2, 1.0, 0.5), 0.25, 1e-15));

        // Two equal discs cover half of each other at 0.80794550659903 radii
        // apart: the root of 2 acos(d / 2) - (d / 2) sqrt(4 - d^2) = pi / 2.
        const double Half = 0.8079455065990344;
        TestTrue(FString::Printf(TEXT("equal discs at the half-overlap separation cover half (%.12f)"), DiscOverlapFraction(Half, 1.0, 1.0)),
            FMath::IsNearlyEqual(DiscOverlapFraction(Half, 1.0, 1.0), 0.5, 1e-9));

        // The lens belongs to both discs: A's share of it times A's area is
        // B's share times B's. A slip in either segment breaks this for
        // unequal discs, where the known values above cannot see it.
        double WorstSymmetry = 0.0;
        for (const double A : { 0.3, 1.0, 2.5 })
        {
            for (const double B : { 0.2, 1.0, 1.7 })
            {
                for (double S = 0.0; S <= A + B; S += (A + B) / 37.0)
                {
                    const double Lens = DiscOverlapFraction(S, A, B) * A * A;
                    WorstSymmetry = FMath::Max(WorstSymmetry, FMath::Abs(Lens - DiscOverlapFraction(S, B, A) * B * B));
                }
            }
        }
        TestTrue(FString::Printf(TEXT("the lens is the same area seen from either disc (worst %.3g)"), WorstSymmetry), WorstSymmetry < 1e-12);

        // Sliding apart, the cover only falls, and it meets both ends with no
        // step: fully covered and wholly clear are where the lens begins and
        // ends, not a jump. A sun half a degree across at a limb 10 degrees
        // in, the case the deck will show.
        const double Sun = FMath::DegreesToRadians(0.27);
        const double Limb = FMath::DegreesToRadians(10.0);
        double Previous = 1.0;
        bool bMonotone = true;
        for (int32 Step = 0; Step <= 400; ++Step)
        {
            const double S = (Limb - Sun) + 2.0 * Sun * Step / 400.0;
            const double Cover = DiscOverlapFraction(S, Sun, Limb);
            bMonotone &= Cover <= Previous + 1e-15;
            Previous = Cover;
        }
        TestTrue(TEXT("sliding a sun off a limb, the cover only falls"), bMonotone);
        TestTrue(TEXT("from wholly covered with no step"), DiscOverlapFraction(Limb - Sun + 1e-12, Sun, Limb) > 1.0 - 1e-6);
        TestTrue(TEXT("to wholly clear with no step"), DiscOverlapFraction(Limb + Sun - 1e-12, Sun, Limb) < 1e-6);
        const double OnLimb = DiscOverlapFraction(Limb, Sun, Limb);
        TestTrue(FString::Printf(TEXT("with its centre on the limb, a small sun is about half covered (%.4f)"), OnLimb),
            OnLimb < 0.5 && OnLimb > 0.49);

        // Angles only: a far sun's micro-radians behave as a near one's
        // degrees.
        const double Scale = 1.0e-6;
        TestTrue(TEXT("the arithmetic holds at micro-radians"),
            FMath::IsNearlyEqual(DiscOverlapFraction(Half * Scale, Scale, Scale), 0.5, 1e-9)
            && FMath::IsNearlyEqual(DiscOverlapFraction(Limb * Scale, Sun * Scale, Limb * Scale), OnLimb, 1e-9));
    }

    // The sun, from where the ship is.
    const FSkyViewParams Params;
    const FSkySystem Fixture = SkyTestFixtures::System();
    const FSkyBody& Star = Fixture.Bodies[SkyTestFixtures::StarIndex];
    const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
    const FVector Shadow = (Home.Position - Star.Position).GetSafeNormal();
    const FVector Across = FVector::CrossProduct(Shadow, FVector::UpVector).GetSafeNormal();
    const double Behind = Home.Radius + 1.0e10;     // 100,000 km past the planet's centre
    const auto VisibleFrom = [&](double Offset)
    {
        return SkyProjection::Project(Fixture, Home.Position + Shadow * Behind + Across * Offset, Params).SunVisibleFraction;
    };
    {
        TestEqual(TEXT("in open sky the whole sun shows"),
            SkyProjection::Project(Fixture, SkyTestFixtures::Opening(), Params).SunVisibleFraction, 1.0);
        TestEqual(TEXT("parked squarely behind a planet, none of it does"), VisibleFrom(0.0), 0.0);
        TestEqual(TEXT("and on the planet's day side, all of it"),
            SkyProjection::Project(Fixture, Home.Position + Shadow * -Behind, Params).SunVisibleFraction, 1.0);

        // Out of the shadow sideways: dark, then a penumbra, then light, and
        // never back.
        double Previous = 0.0;
        bool bMonotone = true;
        for (int32 Step = 0; Step <= 300; ++Step)
        {
            const double Visible = VisibleFrom(3.0 * Home.Radius * Step / 300.0);
            bMonotone &= Visible >= Previous;
            Previous = Visible;
        }
        TestTrue(TEXT("stepping out of the shadow the sun only grows"), bMonotone);
        TestEqual(TEXT("and three radii out it is whole"), Previous, 1.0);

        // Where the star's centre sits on the planet's limb -- found by
        // bisection on where the ship is, not on anything the projection
        // reports -- half the sun is behind the planet.
        const auto LimbGap = [&](double Offset)
        {
            const FUniversePosition Ship = Home.Position + Shadow * Behind + Across * Offset;
            const FVector ToHome = Home.Position - Ship;
            const FVector ToStar = Star.Position - Ship;
            return AngleBetween(ToHome, ToStar) - FMath::Asin(Home.Radius / ToHome.Size());
        };
        double Low = 0.0;
        double High = 3.0 * Home.Radius;
        for (int32 Iteration = 0; Iteration < 80; ++Iteration)
        {
            const double Middle = 0.5 * (Low + High);
            (LimbGap(Middle) < 0.0 ? Low : High) = Middle;
        }
        const double AtLimb = VisibleFrom(0.5 * (Low + High));
        TestTrue(FString::Printf(TEXT("with the star's centre on the planet's limb, about half the sun shows (%.4f)"), AtLimb),
            FMath::Abs(AtLimb - 0.5) < 0.02);
    }

    // A planet lined up behind the star hides nothing: the eclipse follows
    // the depth order the picture is drawn in, not the discs alone.
    {
        const FSkyBody& Giant = Fixture.Bodies[SkyTestFixtures::GiantIndex];
        const FVector Line = (Giant.Position - Star.Position).GetSafeNormal();
        const FUniversePosition Ship = Star.Position + Line * -AU;
        const FSkyFrame Frame = SkyProjection::Project(Fixture, Ship, Params);
        const FSkyBodyView& StarView = Frame.Bodies[SkyTestFixtures::StarIndex];
        const FSkyBodyView& GiantView = Frame.Bodies[SkyTestFixtures::GiantIndex];
        TestTrue(TEXT("the giant's disc lies on the star's"),
            DiscOverlapFraction(AngleBetween(StarView.Direction, GiantView.Direction), StarView.AngularRadius, GiantView.AngularRadius) > 0.0);
        TestEqual(TEXT("but from behind it, so the whole sun shows"), Frame.SunVisibleFraction, 1.0);
    }

    // A body drawn as a point is drawn larger than it is; it hides only what
    // it truly covers. A 1 km rock 100,000 km sunward is a two-pixel point
    // and a speck against the sun.
    {
        FSkySystem Rock = Fixture;
        const FUniversePosition Ship = SkyTestFixtures::Opening();
        const FVector Sunward = (Star.Position - Ship).GetSafeNormal();
        Rock.Bodies.Add(MakeBody(TEXT("Rock"), Ship + Sunward * 1.0e10, 1.0e5, ESkyBodyKind::Moon));
        const FSkyFrame Frame = SkyProjection::Project(Rock, Ship, Params);
        const FSkyBodyView& RockView = Frame.Bodies.Last();
        const double SunRadius = Frame.Bodies[SkyTestFixtures::StarIndex].AngularRadius;
        TestTrue(TEXT("the rock is drawn larger than it is"), RockView.DrawnAngularRadius > 10.0 * RockView.AngularRadius);
        TestTrue(FString::Printf(TEXT("and hides its true share of the sun, %.3g"), 1.0 - Frame.SunVisibleFraction),
            FMath::IsNearlyEqual(1.0 - Frame.SunVisibleFraction, FMath::Square(RockView.AngularRadius / SunRadius), 1e-12));
    }

    // Two bodies over the star at once hide what both hide: shares are
    // summed, never the larger taken, so a second moon crossing always
    // darkens the deck further. Two moons side by side on the sun's face,
    // apart from each other and wholly inside its disc, where the sum is
    // exact and the larger share is half of it.
    {
        FSkySystem Pair = Fixture;
        const FUniversePosition Ship = SkyTestFixtures::Opening();
        const FVector Sunward = (Star.Position - Ship).GetSafeNormal();
        const FVector Side = FVector::CrossProduct(Sunward, FVector::UpVector).GetSafeNormal();
        const double SunRadius = SkyProjection::Project(Fixture, Ship, Params).Bodies[SkyTestFixtures::StarIndex].AngularRadius;
        const double Distance = 1.0e10;
        for (const double Sign : { -1.0, 1.0 })
        {
            const FVector Direction = (Sunward + Side * (Sign * FMath::Tan(0.5 * SunRadius))).GetSafeNormal();
            Pair.Bodies.Add(MakeBody(Sign < 0.0 ? TEXT("West") : TEXT("East"), Ship + Direction * Distance,
                0.3 * SunRadius * Distance, ESkyBodyKind::Moon));
        }
        const FSkyFrame Frame = SkyProjection::Project(Pair, Ship, Params);
        const double Sun = Frame.Bodies[SkyTestFixtures::StarIndex].AngularRadius;
        const double West = FMath::Square(Frame.Bodies[Frame.Bodies.Num() - 2].AngularRadius / Sun);
        const double East = FMath::Square(Frame.Bodies.Last().AngularRadius / Sun);
        TestTrue(FString::Printf(TEXT("each moon hides a real share (%.4f, %.4f)"), West, East), West > 0.05 && East > 0.05);
        TestTrue(FString::Printf(TEXT("and together they hide both shares, %.4f"), 1.0 - Frame.SunVisibleFraction),
            FMath::IsNearlyEqual(1.0 - Frame.SunVisibleFraction, West + East, 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyStarWarmthTest,
    "DeepSpace.Sky.StarWarmth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The local star's surface (flight-feel decision 9): honest T^4 to a ceiling
 * of 8x a Sun's, so that the arrival standoff, which equalises irradiance,
 * makes every sun's arrival glare alike -- and never so honest that the
 * hottest star finds the half-float ceiling, nor so dim that a disc loses
 * to a planet in its own sky.
 */
bool FSkyStarWarmthTest::RunTest(const FString& Parameters)
{
    const double Sun = UniverseUnits::SolarTemperatureK;
    const auto Fourth = [Sun](double TemperatureK) { return FMath::Pow(TemperatureK / Sun, 4.0); };

    // Honest below the ceiling, from the coolest red dwarf to just under it.
    {
        double Worst = 0.0;
        for (const double T : { 2300.0, 3200.0, 4400.0, 5772.0, 7000.0, 9000.0, 9600.0 })
        {
            Worst = FMath::Max(Worst, RelativeError(SkyProjection::StarWarmth(T), Fourth(T)));
        }
        TestTrue(FString::Printf(TEXT("below the ceiling a star's surface is (T / T_sun)^4, honest (worst %.2e)"), Worst), Worst < 1e-12);
        TestEqual(TEXT("a Sun is 1"), SkyProjection::StarWarmth(Sun), 1.0);
        TestEqual(TEXT("and 0 K is dark"), SkyProjection::StarWarmth(0.0), 0.0);
    }

    // The ceiling: 8x, reached at 1.68 times the Sun's temperature and held
    // above it, continuously.
    {
        const double AtCeiling = Sun * FMath::Pow(SkyProjection::MaxStarWarmth, 0.25);
        TestEqual(TEXT("the ceiling is 8x a Sun"), SkyProjection::MaxStarWarmth, 8.0);
        TestTrue(FString::Printf(TEXT("reached at %.0f K"), AtCeiling), FMath::IsNearlyEqual(AtCeiling, 9707.0, 5.0));
        TestTrue(TEXT("continuous there"),
                 FMath::IsNearlyEqual(SkyProjection::StarWarmth(AtCeiling * (1.0 - 1e-9)), 8.0, 1e-6));
        bool bHeld = true;
        for (const double T : { AtCeiling * 1.001, 12000.0, Sun * FMath::Pow(41.0, 0.25), 30000.0, 45000.0 })
        {
            bHeld &= SkyProjection::StarWarmth(T) == SkyProjection::MaxStarWarmth;
        }
        TestTrue(TEXT("and every hotter star is held at it, differing only in colour"), bHeld);
    }

    // The temporary side-by-side: gamma 0.5 is the compressed T^2 the sky
    // shipped with, which is what ds.Sky.StarWarmthGamma 0.5 puts back.
    TestTrue(TEXT("at gamma 0.5 a red dwarf is the old compressed T^2"),
             FMath::IsNearlyEqual(SkyProjection::StarWarmth(3200.0, 0.5), FMath::Square(3200.0 / Sun), 1e-12));
    TestTrue(TEXT("the ruled law is the projection's default"), FSkyViewParams().StarWarmthGamma == 1.0);

    // Through Project, with the sky's own knobs.
    const auto Knob = [](const TCHAR* Name) { return static_cast<double>(IConsoleManager::Get().FindConsoleVariable(Name)->GetFloat()); };
    FSkyViewParams Params;
    Params.StarSurface = Knob(TEXT("ds.Sky.StarSurface"));
    Params.FluxGamma = Knob(TEXT("ds.Sky.FluxGamma"));
    {
        FSkySystem Dwarf;
        FSkyBody& Star = Dwarf.Bodies.Add_GetRef(MakeStar(FUniversePosition()));
        Star.TemperatureK = 3200.0;
        const FSkyFrame Frame = SkyProjection::Project(Dwarf, FUniversePosition() + FVector(AU, 0.0, 0.0), Params);
        TestTrue(TEXT("Project draws a red dwarf's surface at StarSurface x (T / T_sun)^4"),
                 FMath::IsNearlyEqual(Frame.Bodies[0].SurfaceBrightness, Params.StarSurface * Fourth(3200.0),
                                      1e-9 * Params.StarSurface));
    }

    // The test seed's first sectors: every star at least twenty times the
    // brightest world in its own sky, and the hottest under half of
    // half-float's maximum at the default knobs. Honest, the dimmest star in
    // the 10,000-system corpus is 30 times; twenty leaves the priors room.
    {
        const FGalaxyGenerator Galaxy(20260925, FGenPriors{});
        const double SceneScale = Knob(TEXT("ds.Sky.Radiance")) * FMath::Pow(2.0, ShipSky::ManualExposureBias(Knob(TEXT("ds.Sky.Exposure"))));
        int32 Systems = 0;
        double LeastRatio = TNumericLimits<double>::Max();
        FString LeastName;
        double Hottest = 0.0;
        double HottestScene = 0.0;
        for (int32 X = -2; X <= 2; ++X)
        {
            for (int32 Y = -2; Y <= 2; ++Y)
            {
                for (int32 Z = -2; Z <= 2; ++Z)
                {
                    for (const FStarSystemStub& Stub : Galaxy.GenerateSector(FInt64Vector(X, Y, Z)))
                    {
                        const FStarSystem System = Galaxy.GenerateSystem(Stub);
                        const FSkySystem Sky = LocalSystem::Here(TOptional<FStarSystem>(System));
                        const int32 StarIndex = Sky.Bodies.IndexOfByPredicate([](const FSkyBody& Body) { return Body.Kind == ESkyBodyKind::Star; });
                        if (StarIndex == INDEX_NONE)
                        {
                            continue;
                        }
                        ++Systems;
                        const FSkyFrame Frame = SkyProjection::Project(Sky, Sky.Bodies[StarIndex].Position + FVector(0.0, 0.0, AU), Params);
                        const double StarSurface = Frame.Bodies[StarIndex].SurfaceBrightness;
                        double BrightestWorld = 0.0;
                        for (int32 Index = 0; Index < Frame.Bodies.Num(); ++Index)
                        {
                            if (Index != StarIndex)
                            {
                                BrightestWorld = FMath::Max(BrightestWorld, Frame.Bodies[Index].SurfaceBrightness);
                            }
                        }
                        if (BrightestWorld > 0.0 && StarSurface / BrightestWorld < LeastRatio)
                        {
                            LeastRatio = StarSurface / BrightestWorld;
                            LeastName = FString::Printf(TEXT("%s (%.0f K)"), *Stub.Name, Sky.Bodies[StarIndex].TemperatureK);
                        }
                        if (Sky.Bodies[StarIndex].TemperatureK > Hottest)
                        {
                            Hottest = Sky.Bodies[StarIndex].TemperatureK;
                            HottestScene = StarSurface * SceneScale;
                        }
                    }
                }
            }
        }
        TestTrue(FString::Printf(TEXT("the first 125 sectors hold systems to judge (%d)"), Systems), Systems > 40);
        TestTrue(FString::Printf(TEXT("every star is at least 20x the brightest world in its sky: least %.1fx, %s"), LeastRatio, *LeastName),
                 LeastRatio >= 20.0);
        TestTrue(FString::Printf(TEXT("the hottest star there (%.0f K) is %.0f in scene colour, under half of 65,504"), Hottest, HottestScene),
                 HottestScene > 0.0 && HottestScene < 0.5 * 65504.0);

        // And the hottest a star can be, whatever the priors make: the
        // ceiling is what holds it.
        FSkySystem Blue;
        FSkyBody& Star = Blue.Bodies.Add_GetRef(MakeStar(FUniversePosition()));
        Star.TemperatureK = 45000.0;
        const double Scene = SkyProjection::Project(Blue, FUniversePosition() + FVector(AU, 0.0, 0.0), Params).Bodies[0].SurfaceBrightness * SceneScale;
        TestTrue(FString::Printf(TEXT("a 45,000 K star is %.0f in scene colour, under half of 65,504"), Scene), Scene < 0.5 * 65504.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyColourTest,
    "DeepSpace.Sky.Colour",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkyColourTest::RunTest(const FString& Parameters)
{
    const FLinearColor RedDwarf = SkyColour::Blackbody(3200.0);
    const FLinearColor Sun = SkyColour::Blackbody(5772.0);
    const FLinearColor Hot = SkyColour::Blackbody(12000.0);

    TestTrue(TEXT("a red dwarf is red"), RedDwarf.R > RedDwarf.G && RedDwarf.G > RedDwarf.B);
    TestTrue(TEXT("a hot star is blue"), Hot.B > Hot.R);
    TestTrue(TEXT("the Sun is near white"), Sun.GetMin() > 0.8f);
    for (const FLinearColor& Colour : { RedDwarf, Sun, Hot, SkyColour::Blackbody(800.0), SkyColour::Blackbody(40000.0) })
    {
        TestTrue(TEXT("a blackbody is a chromaticity: its brightest channel is 1"), FMath::IsNearlyEqual(Colour.GetMax(), 1.0f));
        TestTrue(TEXT("and no channel is negative"), Colour.GetMin() >= 0.0f);
    }
    return true;
}

#endif
